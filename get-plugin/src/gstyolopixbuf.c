/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2018  <<doug@douglasteeple.com>>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:videofilter-yolo
 *
 * Yolo puts bounding boxes around detected objects in images.
 *
 * <refsect2>
 * <title>Yolo Objection detection filter</title>
 * |[
 * gst-launch-1.0 -v -m fakesrc ! yolo ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#define USE_PIXBUF

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/controller/controller.h>
#include <gst/video/video.h>

#ifdef USE_PIXBUF
#include <glib-2.0/glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

#include "darknet.h"
#include "network.h"
#include <sys/time.h>
#include <unistd.h>			// usleep

#include "gstyolo.h"

GST_DEBUG_CATEGORY_STATIC (gst_yolo_debug);
#define GST_CAT_DEFAULT gst_yolo_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_MODEL,
  PROP_CFG,
  PROP_NAMES,
  PROP_ALPHABET
};

#define FRAMES 3

static bool verbose = FALSE, running = FALSE;
static char **names;
static image **alphabet;
static int classes;

static network *net;
static image buff [FRAMES];
static image annotated_buff [FRAMES];
static image buff_letter[FRAMES];

static int buff_index = 0;
static int annotated = 0;
static int last_annotated = 0;
static float fps = 0;
static float thresh = 0.5;
static float hier = 0.5;
static float nms = 0.4;

const int frames = FRAMES;
static float **predictions;
static float *avg;
static int netsize = 0;
static pthread_t detect_thread;
static double timediff = 0;
static unsigned int counter = 0;

/* the capabilities of the inputs and outputs.
 *
 */
static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("video/x-raw, "
        "format = (string){ BGR, BGRx, RGB, RGBx, xBGR, xRGB }"))
);

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("video/x-raw, "
        "format = (string){ BGR }"))
);

#define gst_yolo_parent_class parent_class
G_DEFINE_TYPE (Gstyolo, gst_yolo, GST_TYPE_ELEMENT);

static void gst_yolo_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_yolo_get_property (GObject * object, guint prop_id,GValue * value, GParamSpec * pspec);

static gboolean gst_yolo_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_yolo_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);
static void init_yolo(char *cfgfile, char *weightfile, char *names, char *alphabetdir, Gstyolo *filter);
static void *detect_image(void *ptr);

static image make_new_image(int w, int h, int c)
{
    image out = make_empty_image(w,h,c);
    out.data = malloc(h*w*c*sizeof(float));
    return out;
}

static image letterbox_new_image(image im, int w, int h)
{
    int new_w = im.w;
    int new_h = im.h;
    if (((float)w/im.w) < ((float)h/im.h)) {
        new_w = w;
        new_h = (im.h * w)/im.w;
    } else {
        new_h = h;
        new_w = (im.w * h)/im.h;
    }
    image resized = resize_image(im, new_w, new_h);
    image boxed = make_new_image(w, h, im.c);
    fill_image(boxed, .5);
    embed_image(resized, boxed, (w-new_w)/2, (h-new_h)/2); 
    free_image(resized);
    return boxed;
}

/* GObject vmethod implementations */

/* initialize the yolo's class */
static void gst_yolo_class_init (GstyoloClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_yolo_set_property;
  gobject_class->get_property = gst_yolo_get_property;

  g_object_class_install_property (gobject_class, PROP_SILENT,
    g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          TRUE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_CFG,
      g_param_spec_string ("cfg",
                         "Cfg",
                         "cfg file name.",
                         "/usr/local/share/darknet/cfg/yolov3.cfg"  /* default value */,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));;

  g_object_class_install_property (gobject_class, PROP_MODEL,
      g_param_spec_string ("model",
                         "Model",
                         "model (weights) file name.",
                         "/usr/local/share/darknet/cfg/yolov3.weights"  /* default value */,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));;

  g_object_class_install_property (gobject_class, PROP_NAMES,
      g_param_spec_string ("names",
                         "Names",
                         "Class names file name.",
                         "/usr/local/share/darknet/data/coco.names"  /* default value */,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));;

  g_object_class_install_property (gobject_class, PROP_ALPHABET,
      g_param_spec_string ("alphabetdir",
                         "Alphabet",
                         "Alphabet images file path.",
                         "/usr/local/share/darknet/data/labels/"  /* default value */,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));;

  gst_element_class_set_details_simple (GST_ELEMENT_CLASS(gstelement_class),
    "yolo",
    "Generic/Filter/Video",
    "YOLO Object Detection Filter",
    " <<doug@douglasteeple.com>>");

  gst_element_class_add_pad_template (gstelement_class, gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class, gst_static_pad_template_get (&sink_factory));

  /* debug category for fltering log messages
   *
   */
  GST_DEBUG_CATEGORY_INIT (gst_yolo_debug, "yolo", 0, "YOLO");
}

/* initialize the new element
 * initialize instance structure
 */
static void gst_yolo_init(Gstyolo *filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (filter->sinkpad, GST_DEBUG_FUNCPTR(gst_yolo_sink_event));
  gst_pad_set_chain_function (filter->sinkpad, GST_DEBUG_FUNCPTR(gst_yolo_chain));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->silent = TRUE;
  verbose = !filter->silent;
  filter->alphabetdir = NULL;
}

static void gst_yolo_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  Gstyolo *filter = GST_YOLO (object);

  switch (prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean (value);
	  verbose = !filter->silent;
      break;
    case PROP_CFG:
      g_free (filter->cfg);
      filter->cfg = g_value_dup_string (value);
	  if (!g_file_test (filter->cfg, G_FILE_TEST_EXISTS)) {
		g_print ("File %s does not exist\n", filter->cfg);
	  }
      break;
    case PROP_MODEL:
      g_free (filter->model);
      filter->model = g_value_dup_string (value);
	  if (!g_file_test (filter->model, G_FILE_TEST_EXISTS)) {
		g_print ("File %s does not exist\n", filter->model);
	  }
      break;
    case PROP_NAMES:
      g_free (filter->names);
      filter->names = g_value_dup_string (value);
	  if (!g_file_test (filter->names, G_FILE_TEST_EXISTS)) {
		g_print ("File %s does not exist\n", filter->names);
	  }
      break;
    case PROP_ALPHABET:
      g_free (filter->alphabetdir);
      filter->alphabetdir = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  if (g_file_test (filter->cfg, G_FILE_TEST_EXISTS) && 
	  g_file_test (filter->model, G_FILE_TEST_EXISTS) &&
      g_file_test (filter->names, G_FILE_TEST_EXISTS) && prop_id == PROP_ALPHABET) {
		init_yolo(filter->cfg, filter->model, filter->names, filter->alphabetdir, filter);
  }
}

static void gst_yolo_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstyolo *filter = GST_YOLO (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    case PROP_CFG:
      g_value_set_string (value, filter->cfg);
      break;
    case PROP_MODEL:
      g_value_set_string (value, filter->model);
      break;
    case PROP_NAMES:
      g_value_set_string (value, filter->names);
      break;
    case PROP_ALPHABET:
      g_value_set_string (value, filter->alphabetdir);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* vmethod implementations */

static gboolean gst_yolo_sink_event(GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret = FALSE;
  Gstyolo *filter = GST_YOLO (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
	  GST_OBJECT_LOCK (filter);
	  GstStructure *structure = gst_caps_get_structure (caps, 0);
	  if (gst_structure_get_int (structure, "width", &filter->width) &&
		  gst_structure_get_int (structure, "height", &filter->height)) {
 		
		for (unsigned i=0; i<frames; i++) {
			buff[i] = make_new_image(filter->width, filter->height, 3);
			annotated_buff[i] = make_new_image(filter->width, filter->height, 3);
			buff_letter[i] = letterbox_new_image(buff[i], net->w, net->h);
		}
 		running = TRUE;
   		if (pthread_create(&detect_thread, NULL, detect_image, NULL)) {
			fprintf(stderr, "Thread creation failed\n");
		}
		if (!filter->silent) {
			fprintf(stderr, "Thread started...\n");
		}
		ret = TRUE;
	  }
	  GST_OBJECT_UNLOCK (filter);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}


inline int size_network(network *net)
{
    int count = 0;
    for(int i = 0; i < net->n; ++i){
        layer l = net->layers[i];
        if(l.type == YOLO || l.type == REGION || l.type == DETECTION){
            count += l.outputs;
        }
    }
    return count;
}

static void remember_network(network *net)
{
    int i;
    int count = 0;
    for(i = 0; i < net->n; ++i){
        layer l = net->layers[i];
        if(l.type == YOLO || l.type == REGION || l.type == DETECTION){
            memcpy(predictions[annotated] + count, net->layers[i].output, sizeof(float) * l.outputs);
            count += l.outputs;
        }
    }
}

static detection *avg_predictions(network *net, int *nboxes)
{
    int count = 0;
    fill_cpu(netsize, 0, avg, 1);
    for(int j = 0; j < frames; ++j){
        axpy_cpu(netsize, 1.0/(float)frames, predictions[j], 1, avg, 1);
    }
    for(int i = 0; i < net->n; ++i){
        layer l = net->layers[i];
        if(l.type == YOLO || l.type == REGION || l.type == DETECTION){
            memcpy(l.output, avg + count, sizeof(float) * l.outputs);
            count += l.outputs;
        }
    }
    detection *dets = get_network_boxes(net, buff[0].w, buff[0].h, thresh, hier, 0, 1, nboxes);
    return dets;
}

static void *detect_image(void *ptr)
{
	while (running) {
		double time = what_time_is_it_now();
		//annotated = (annotated+frames-1) % frames;
		copy_image_into(buff[buff_index], annotated_buff[annotated]);
		layer l = net->layers[net->n-1];
		network_predict(net, buff_letter[buff_index].data);

		remember_network(net);
		detection *dets = NULL;
		int nboxes = 0;
		dets = avg_predictions(net, &nboxes);

		if (nms > 0) 
			do_nms_obj(dets, nboxes, l.classes, nms);

		draw_detections(annotated_buff[annotated], dets, nboxes, thresh, names, alphabet, classes);
		timediff = what_time_is_it_now() - time;
		fps = 1.0/timediff;
		if (verbose) {
			int count = 0;
			for(int i = 0; i < nboxes; ++i){
				for(int j = 0; j < l.classes; ++j){
				    if (dets[i].prob[j] > thresh){
				        fprintf(stderr, "%s: %.0f%% ", names[j], dets[i].prob[j]*100);
						count++;
				    }
				}
			}
			fprintf(stderr, "%d objects detected in %.02f seconds, fps= %.02f\n", count, timediff, fps);
		}
		free_detections(dets, nboxes);
		last_annotated = annotated;
		annotated = (annotated+1) % frames;
		usleep(10);
	}
	return NULL;
}

inline float get_pixel(image m, int x, int y, int c)
{
    g_assert(x < m.w && y < m.h && c < m.c);
    return m.data[c*m.h*m.w + y*m.w + x];
}
inline void set_pixel(image m, int x, int y, int c, float val)
{
    g_assert(x < m.w && y < m.h && c < m.c);
    m.data[c*m.h*m.w + y*m.w + x] = val;
}
#ifdef USE_PIXBUF
inline void pixbuf_to_image(GdkPixbuf *pixbuf, image im)
{
	int n_channels = gdk_pixbuf_get_n_channels (pixbuf);
	int width = gdk_pixbuf_get_width (pixbuf);
	int height = gdk_pixbuf_get_height (pixbuf);
	int rowstride = gdk_pixbuf_get_rowstride (pixbuf);
	guchar *pixels = gdk_pixbuf_get_pixels (pixbuf);

	g_assert (height == im.h && width == im.w);

    for(int j = 0; j < im.h; ++j){
        for(int i = 0; i < im.w; ++i){
			guchar *p = pixels + j * rowstride + i * n_channels;
            set_pixel(im, i, j, 0, (float)(p[0])/255.0);
            set_pixel(im, i, j, 1, (float)(p[1])/255.0);
            set_pixel(im, i, j, 2, (float)(p[2])/255.0);
        }
    }
}

inline void image_to_pixbuf(image im, GdkPixbuf *pixbuf) {
	int n_channels = gdk_pixbuf_get_n_channels (pixbuf);
	int width = gdk_pixbuf_get_width (pixbuf);
	int height = gdk_pixbuf_get_height (pixbuf);
	int rowstride = gdk_pixbuf_get_rowstride (pixbuf);
	guchar *pixels = gdk_pixbuf_get_pixels (pixbuf);

	g_assert (height == im.h && width == im.w);

    for(int j = 0; j < im.h; ++j){
        for(int i = 0; i < im.w; ++i){
			guchar *p = pixels + j * rowstride + i * n_channels;
            p[0] = (unsigned char)(get_pixel(im, i, j, 0)*255.0);
            p[1] = (unsigned char)(get_pixel(im, i, j, 1)*255.0);
            p[2] = (unsigned char)(get_pixel(im, i, j, 2)*255.0);
        }
    }
}
#endif
inline void guchar_to_image(guchar *pixels, image im) 
{
	int stride = ((im.w * 3)+3)&~3;
    for(int j = 0; j < im.h; ++j){
        for(int i = 0; i < im.w; ++i){
			guchar *p = pixels + j * stride + i * 3;
            set_pixel(im, i, j, 0, (float)(p[0])/255.0);
            set_pixel(im, i, j, 1, (float)(p[1])/255.0);
            set_pixel(im, i, j, 2, (float)(p[2])/255.0);
        }
    }
}

inline void image_to_guchar(image im, guchar *pixels) 
{
	int stride = ((im.w * 3)+3)&~3;
    for(int j = 0; j < im.h; ++j){
        for(int i = 0; i < im.w; ++i){
			guchar *p = pixels + j * stride + i * 3;
            p[0] = (unsigned char)(get_pixel(im, i, j, 0)*255.0);
            p[1] = (unsigned char)(get_pixel(im, i, j, 1)*255.0);
            p[2] = (unsigned char)(get_pixel(im, i, j, 2)*255.0);
        }
    }
}

static void init_yolo(char *cfgfile, char *weightfile, char *namefile, char *alphabetdir, Gstyolo *filter)
{
   	gpu_index = 0;

	if (!filter->silent) {
		fprintf(stderr, "Loading network: %s %s\n", cfgfile, weightfile);
	}
    net = load_network(cfgfile, weightfile, 0);
    set_batch_network(net, 1);

    srand(2222222);

	layer l = net->layers[net->n-1];
	classes = l.classes;
    netsize = size_network(net);
    predictions = malloc(frames*sizeof(float*));
    for (int i = 0; i < frames; ++i){
        predictions[i] = malloc(netsize*sizeof(float));
    }
    avg = malloc(netsize*sizeof(float));
	if (!filter->silent) {
		fprintf(stderr, "Loading names: %s \n", namefile);
	}
	names = get_labels(namefile);
	if (!filter->silent) {
	for (int i=0; i<classes; i++) {
			fprintf(stderr, "%d:%s ", i, names[i]);
		}
		fprintf(stderr, "\n");
	}

	if (alphabetdir) {
		if (!filter->silent) {
			fprintf(stderr, "Loading alphabet: %s \n", alphabetdir);
		}
	    alphabet = load_alphabet();
		if (!filter->silent && alphabet != NULL) {
			fprintf(stderr, "Init alphabet loaded %s \n", alphabetdir);
		}
	}

	if (!filter->silent) {
		fprintf(stderr, "Init called %s %s %s %s size:%d\n", cfgfile, weightfile, namefile, alphabetdir, netsize);
    	fprintf(stderr, "Learning Rate: %g, Momentum: %g, Decay: %g\n", net->learning_rate, net->momentum, net->decay);
    	fprintf(stderr, "Classes: %d, Threshold: %.02f, Hier: %0.2f\n", classes, thresh, hier);
#ifdef USE_PIXBUF
		fprintf(stderr, "Using PIXBUF\n");
#endif
		//print_network(net);
	}
}

/* this function does the actual processing
 */
static GstFlowReturn gst_yolo_chain(GstPad *pad, GstObject *parent, GstBuffer *buf)
{
	Gstyolo *filter = GST_YOLO(parent);

	if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buf))) {
		gst_object_sync_values (GST_OBJECT (filter), GST_BUFFER_TIMESTAMP (buf));
	}
	counter++;
	if (counter % 2) {	// prevents too slow messages
		GstMapInfo map;
		if (gst_buffer_map(buf, &map, GST_MAP_READWRITE)) {
#ifdef USE_PIXBUF
			GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(map.data,
				GDK_COLORSPACE_RGB, FALSE, 8, filter->width, filter->height,
				GST_ROUND_UP_4(filter->width * 3),
				NULL, NULL);

			pixbuf_to_image(pixbuf, buff[buff_index]);
			letterbox_image_into(buff[buff_index], net->w, net->h, buff_letter[buff_index]);
		    buff_index = (buff_index+1) % frames;

			image_to_guchar(annotated_buff[last_annotated], map.data);
			//image_to_pixbuf(annotated_buff[last_annotated], pixbuf);
#else
			guchar_to_image(map.data, buff[buff_index]);
			letterbox_image_into(buff[buff_index], net->w, net->h, buff_letter[buff_index]);
		    buff_index = (buff_index+1) % frames;

			image_to_guchar(annotated_buff[last_annotated], map.data);
#endif
 			gst_buffer_unmap(buf, &map);
    	}
	}

   return gst_pad_push (filter->srcpad, buf);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean yolo_init (GstPlugin * yolo)
{
  GST_DEBUG_CATEGORY_INIT(gst_yolo_debug, "yolo", 0, "YOLO");
  return gst_element_register (yolo, "yolo", GST_RANK_NONE, GST_TYPE_YOLO);
}

#ifndef PACKAGE
#define PACKAGE "yolo"
#endif
 
/* gstreamer looks for this structure to register yolos
 *
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    yolo,
    "YOLO Object Detection",
    yolo_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
