/*
 * GStreamer
 * Copyright(C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright(C) 2018  <<doug@douglasteeple.com>>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or(at your option) any later version.
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
 * Yolo puts bounding boxes around detected objects in images. Requires
 * libdarknet.so version 3 of darknet.
 *
 * <refsect2>
 * <title>Yolo Objection detection filter</title>
 * |[
 * gst-launch-1.0 -v -m fakesrc ! yolo ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#include<pthread.h>
#include<stdlib.h>
#include <unistd.h>			// usleep
#include <sys/time.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// gstreamer-1.0
#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/controller/controller.h>
#include <gst/video/video.h>

// opencv
#include "highgui.h"

// yolo
#include "darknet.h"
#include "network.h"

#include "gstyolo.h"

GST_DEBUG_CATEGORY_STATIC(gst_yolo_debug);
#define GST_CAT_DEFAULT gst_yolo_debug

#define DEFAULT_PROP_WIDTH		0.75
#define DEFAULT_PROP_HEIGHT 	0.75
#define DEFAULT_PROP_XPOS		20
#define DEFAULT_PROP_YPOS		20
#define DEFAULT_PROP_THICKNESS	1
#define DEFAULT_PROP_COLOR_R	240
#define DEFAULT_PROP_COLOR_G	240
#define DEFAULT_PROP_COLOR_B	0
#define MAX_LAYERS				256

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
  PROP_LAYER
};

#define MAX_CLASSES 100

typedef struct {
	bool live;
	int class_;
	char *name;
	float probability;
	int top, left;
	int bottom, right;
} stats_t;

stats_t stats[MAX_CLASSES];

static bool verbose = FALSE, running = FALSE;
static char **names;
static int classes;

static network *net;
static image image_buffer;
static char textbuf[4096];

static double fps;
static float thresh = 0.5;
static float hier = 0.5;
static float nms = 0.4;

static int netsize = 0;
static pthread_t detect_thread;
static pthread_mutex_t lock;
static int nboxes = 0;

static int layer_to_show = -1;
static int detection_layers = -1;

IplImage* cvImage;

/* the capabilities of the inputs and outputs.
 *
 */
static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE(
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("video/x-raw, "
        "format =(string){ BGR, BGRx, RGB, RGBx, xBGR, xRGB }"))
);

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE(
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("video/x-raw, "
        "format =(string){ BGR }"))
);

#define gst_yolo_parent_class parent_class
G_DEFINE_TYPE(Gstyolo, gst_yolo, GST_TYPE_ELEMENT);

static void gst_yolo_set_property(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_yolo_get_property(GObject * object, guint prop_id,GValue * value, GParamSpec * pspec);

static gboolean gst_yolo_sink_event(GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_yolo_chain(GstPad * pad, GstObject * parent, GstBuffer * buf);
static void init_yolo(char *cfgfile, char *weightfile, char *names, Gstyolo *filter);

static void *detect_image_thread(void *ptr);
static int get_detection_layer_count(network *net);

/* GObject vmethod implementations */

/* initialize the yolo's class */
static void gst_yolo_class_init(GstyoloClass *klass)
{
  GObjectClass *gobject_class =(GObjectClass *) klass;
  GstElementClass *gstelement_class =(GstElementClass *) klass;

  gobject_class->set_property = gst_yolo_set_property;
  gobject_class->get_property = gst_yolo_get_property;

  g_object_class_install_property(gobject_class, PROP_SILENT,
    g_param_spec_boolean("silent", "Silent", "Produce verbose output ?",
          TRUE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property(gobject_class, PROP_CFG,
      g_param_spec_string("cfg",
                         "Cfg",
                         "cfg file name.",
                         "/usr/local/share/darknet/cfg/yolov3.cfg"  /* default value */,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));;

  g_object_class_install_property(gobject_class, PROP_MODEL,
      g_param_spec_string("model",
                         "Model",
                         "model(weights) file name.",
                         "/usr/local/share/darknet/cfg/yolov3.weights"  /* default value */,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));;

  g_object_class_install_property(gobject_class, PROP_NAMES,
      g_param_spec_string("names",
                         "Names",
                         "Class names file name.",
                         "/usr/local/share/darknet/data/coco.names"  /* default value */,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));;

  g_object_class_install_property(gobject_class, PROP_LAYER,
      g_param_spec_int("layer",
                         "Layer",
                         "Which network layer to show.",
						 -1, MAX_LAYERS,
                         -1  /* default value */,
                         G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));;

  gst_element_class_set_details_simple(GST_ELEMENT_CLASS(gstelement_class),
    "yolo",
    "Generic/Filter/Video",
    "YOLO Object Detection Filter",
    "<<doug@douglasteeple.com>>");

  gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&src_factory));
  gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&sink_factory));

  /* debug category for fltering log messages
   *
   */
  GST_DEBUG_CATEGORY_INIT(gst_yolo_debug, "yolo", 0, "YOLO");
}

/* initialize the new element
 * initialize instance structure
 */
static void gst_yolo_init(Gstyolo *filter)
{
  filter->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
  gst_pad_set_event_function(filter->sinkpad, GST_DEBUG_FUNCPTR(gst_yolo_sink_event));
  gst_pad_set_chain_function(filter->sinkpad, GST_DEBUG_FUNCPTR(gst_yolo_chain));
  GST_PAD_SET_PROXY_CAPS(filter->sinkpad);
  gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS(filter->srcpad);
  gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

  filter->silent = TRUE;
  verbose = !filter->silent;
  filter->layer = -1;	// show default layer
  filter->textwidth = DEFAULT_PROP_WIDTH;
  filter->textheight = DEFAULT_PROP_HEIGHT;
  filter->xpos = DEFAULT_PROP_XPOS;
  filter->ypos = DEFAULT_PROP_YPOS;
  filter->thickness = DEFAULT_PROP_THICKNESS;
  filter->colorR = DEFAULT_PROP_COLOR_R;
  filter->colorG = DEFAULT_PROP_COLOR_G;
  filter->colorB = DEFAULT_PROP_COLOR_B;
}

static void gst_yolo_set_property(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  Gstyolo *filter = GST_YOLO(object);

  switch(prop_id) {
    case PROP_SILENT:
      filter->silent = g_value_get_boolean(value);
	  verbose = !filter->silent;
      break;
    case PROP_CFG:
      g_free(filter->cfg);
      filter->cfg = g_value_dup_string(value);
	  if(!g_file_test(filter->cfg, G_FILE_TEST_EXISTS)) {
		g_print("File %s does not exist\n", filter->cfg);
	  }
      break;
    case PROP_MODEL:
      g_free(filter->model);
      filter->model = g_value_dup_string(value);
	  if(!g_file_test(filter->model, G_FILE_TEST_EXISTS)) {
		g_print("File %s does not exist\n", filter->model);
	  }
      break;
    case PROP_NAMES:
      g_free(filter->names);
      filter->names = g_value_dup_string(value);
	  if(!g_file_test(filter->names, G_FILE_TEST_EXISTS)) {
		g_print("File %s does not exist\n", filter->names);
	  }
      break;
    case PROP_LAYER:
	  if (G_VALUE_HOLDS_INT(value)) {
      	layer_to_show = filter->layer = g_value_get_int(value);
	  }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void gst_yolo_get_property(GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstyolo *filter = GST_YOLO(object);

  switch(prop_id) {
    case PROP_SILENT:
      g_value_set_boolean(value, filter->silent);
      break;
    case PROP_CFG:
      g_value_set_string(value, filter->cfg);
      break;
    case PROP_MODEL:
      g_value_set_string(value, filter->model);
      break;
    case PROP_NAMES:
      g_value_set_string(value, filter->names);
      break;
    case PROP_LAYER:
      g_value_set_int(value, filter->layer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

/* vmethod implementations */

static gboolean gst_yolo_sink_event(GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret = FALSE;
  Gstyolo *filter = GST_YOLO(parent);

  switch(GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps(event, &caps);
	  GST_OBJECT_LOCK(filter);
	  GstStructure *structure = gst_caps_get_structure(caps, 0);
	  if(gst_structure_get_int(structure, "width", &filter->width) &&
		  gst_structure_get_int(structure, "height", &filter->height)) {
 		
		if(g_file_test(filter->cfg, G_FILE_TEST_EXISTS) && 
			  g_file_test(filter->model, G_FILE_TEST_EXISTS) &&
			  g_file_test(filter->names, G_FILE_TEST_EXISTS) && net == NULL) {
				init_yolo(filter->cfg, filter->model, filter->names, filter);
		}
		image_buffer = make_image(filter->width, filter->height, 3);
		cvImage = cvCreateImage(cvSize (filter->width, filter->height), IPL_DEPTH_8U, 3);
 		running = TRUE;
		if (pthread_mutex_init(&lock, NULL) != 0) {
			g_print("mutex init failed\n");
		}
   		if (pthread_create(&detect_thread, NULL, detect_image_thread, NULL)) {
			g_print("Thread creation failed\n");
		}
		if (!filter->silent) {
			g_print("Thread started...\n");
		}
		ret = TRUE;
	  }
	  GST_OBJECT_UNLOCK(filter);
      ret = gst_pad_event_default(pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default(pad, parent, event);
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

static int get_detection_layer_count(network *net) {
	int k = 0;
    for(int i = 0; i < net->n; ++i){
        layer l = net->layers[i];
        if(l.type == YOLO || l.type == REGION || l.type == DETECTION){
            k++;
        }
    }
    return k;
}
static int get_ith_network_detection_layer(network *net, int start)
{
    for (int i = start; i < net->n; ++i){
        layer l = net->layers[i];
        if(l.type == YOLO || l.type == REGION || l.type == DETECTION){
            return i;
        }
    }
    return -1;
}

static image gst_get_network_image(network *net, int which) {
	for (int i=0; i<which; i++) {
		if (get_ith_network_detection_layer(net, i) >= 0) {
			return get_network_image_layer(net, i);
		}
	}
    image def = {0};
    return def;
} 


static void *detect_image_thread(void *ptr)
{
	while(running) {
		double starttime = what_time_is_it_now();

		network_predict_image(net, image_buffer);
    	detection *dets = get_network_boxes(net, image_buffer.w, image_buffer.h, thresh, hier, 0, 1, &nboxes);
		if(nms > 0) 
			do_nms_obj(dets, nboxes, classes, nms);

    	pthread_mutex_lock(&lock);
		double timediff = what_time_is_it_now() - starttime;
		double fps = 1.0/timediff;
		int count = 0;
		textbuf[0] = '\0';
		for(int i = 0; i < nboxes; ++i){
			for(int j = 0; j < classes; ++j){
			    if(dets[i].prob[j] > thresh){
			        if (verbose) g_print("%s: %.0f%% ", names[j], dets[i].prob[j]*100);
					stats[count+1].live = FALSE;
					stats[count].live = TRUE;
					stats[count].class_ = j;
					stats[count].name = names[j];
					stats[count].probability = dets[i].prob[j]*100.0;
            		box b = dets[i].bbox;
 					stats[count].top = (b.y-b.h/2.)*image_buffer.h;
					stats[count].left = (b.x-b.w/2.)*image_buffer.w;
 					stats[count].bottom = (b.y+b.h/2)*image_buffer.h;
					stats[count].right = (b.x+b.w/2)*image_buffer.w;
					count++;
			    }
				if (count > classes-1)
					break;
			}
			if (count > classes-1)
				break;
		}
		stats[count].live = FALSE;
		if (verbose) g_print("%d objects detected in %.02f seconds, fps= %.02f\n", count, timediff, fps);
		sprintf(&textbuf[strlen(textbuf)], "%.02f sec, %.02f fps", timediff, fps);
		free_detections(dets, nboxes);
    	pthread_mutex_unlock(&lock);
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

inline void guchar_to_image(guchar *pixels, image im) 
{
	int stride =((im.w * 3)+3)&~3;
    for(int j = 0; j < im.h; ++j){
        for(int i = 0; i < im.w; ++i){
			guchar *p = pixels + j * stride + i * 3;
            set_pixel(im, i, j, 0,(float)(p[0])/255.0);
            set_pixel(im, i, j, 1,(float)(p[1])/255.0);
            set_pixel(im, i, j, 2,(float)(p[2])/255.0);
        }
    }
}

inline void image_to_guchar(image im, guchar *pixels) 
{
	int stride =((im.w * 3)+3)&~3;
    for(int j = 0; j < im.h; ++j){
        for(int i = 0; i < im.w; ++i){
			guchar *p = pixels + j * stride + i * 3;
            p[0] =(unsigned char)(get_pixel(im, i, j, 0)*255.0);
            p[1] =(unsigned char)(get_pixel(im, i, j, 1)*255.0);
            p[2] =(unsigned char)(get_pixel(im, i, j, 2)*255.0);
        }
    }
}

static void init_yolo(char *cfgfile, char *weightfile, char *namefile, Gstyolo *filter)
{
   	gpu_index = 0;

	if(!filter->silent) {
		g_print("Loading network: %s %s\n", cfgfile, weightfile);
	}
    net = load_network(cfgfile, weightfile, 0);
    set_batch_network(net, 1);

    srand(2222222);

	layer l = net->layers[net->n-1];
	classes = l.classes;
    netsize = size_network(net);
	if(!filter->silent) {
		g_print("Loading names: %s \n", namefile);
	}
	names = get_labels(namefile);
	if(!filter->silent) {
	for(int i=0; i<classes; i++) {
			g_print("%d:%s ", i, names[i]);
		}
		g_print("\n");
	}
    detection_layers = get_detection_layer_count(net);
	if(!filter->silent) {
		g_print("Init called %s %s %s size:%d\n", cfgfile, weightfile, namefile, netsize);
    	g_print("Learning Rate: %g, Momentum: %g, Decay: %g Layer: %d\n", net->learning_rate, net->momentum, net->decay, layer_to_show);
    	g_print("Classes: %d, Threshold: %.02f, Hier: %0.2f, Detection Layers: %d\n", classes, thresh, hier, get_detection_layer_count(net));
		//print_network(net);
	}
}

/* this function does the actual processing
 */
static GstFlowReturn gst_yolo_chain(GstPad *pad, GstObject *parent, GstBuffer *buf)
{
	Gstyolo *filter = GST_YOLO(parent);

	if(GST_CLOCK_TIME_IS_VALID(GST_BUFFER_TIMESTAMP(buf))) {
		gst_object_sync_values(GST_OBJECT(filter), GST_BUFFER_TIMESTAMP(buf));
	}
	double starttime = what_time_is_it_now();
	GstMapInfo map;
	if(gst_buffer_map(buf, &map, GST_MAP_READWRITE)) {
    	pthread_mutex_lock(&lock);
		guchar_to_image(map.data, image_buffer);
		if (layer_to_show >= 0 && layer_to_show < detection_layers) {
			image_to_guchar(gst_get_network_image(net, layer_to_show), map.data);
		} else {
			//image_to_guchar(buff[buff_index], map.data);
		}

		cvImage->imageData = (char *)map.data;
  		cvInitFont(&(filter->font), CV_FONT_HERSHEY_COMPLEX_SMALL, filter->textwidth, filter->textheight, 0, filter->thickness, 0);
		//sprintf(&textbuf[strlen(textbuf)], " %.02f fps", fps);	
		cvPutText(cvImage, textbuf, cvPoint(filter->xpos, filter->ypos), &(filter->font), cvScalar (filter->colorR, filter->colorG, filter->colorB, 0));
		int i = 0;
		CvSize textsize;
		while (stats[i].live) {
			char buffer[1024];
			sprintf(buffer, "%s %.02f%%", stats[i].name, stats[i].probability);
			int baseline = 0;
            int offset = stats[i].class_*123457 % classes;
            guint red = (guint)(get_color(2,offset,classes)*255.0);
            guint green = (guint)(get_color(1,offset,classes)*255.0);
            guint blue = (guint)(get_color(0,offset,classes)*255.0);
			cvInitFont(&(filter->font), CV_FONT_HERSHEY_SIMPLEX, 0.5, 0.5, 0, 1, 0);
			cvGetTextSize(buffer, &(filter->font), &textsize, &baseline);
    		cvRectangle(cvImage, cvPoint(stats[i].left, stats[i].top), cvPoint(stats[i].left+textsize.width*1.2, stats[i].top-textsize.height*2), cvScalar(red, green, blue, 0), CV_FILLED, 8, 0);
    		cvRectangle(cvImage, cvPoint(stats[i].left, stats[i].top), cvPoint(stats[i].right, stats[i].bottom), cvScalar(red, green, blue, 0), 2, 8, 0);
  			cvPutText(cvImage, buffer, cvPoint(stats[i].left+textsize.width/strlen(buffer), stats[i].top-baseline), &(filter->font), cvScalar(0, 0, 0, 0));
			i++;		
		}
		fps = 1.0/(what_time_is_it_now() - starttime);
		gst_buffer_unmap(buf, &map);
    	pthread_mutex_unlock(&lock);
	}

   return gst_pad_push(filter->srcpad, buf);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean yolo_init(GstPlugin * yolo)
{
  GST_DEBUG_CATEGORY_INIT(gst_yolo_debug, "yolo", 0, "YOLO");
  return gst_element_register(yolo, "yolo", GST_RANK_NONE, GST_TYPE_YOLO);
}

#ifndef PACKAGE
#define PACKAGE "yolo"
#endif
 
/* gstreamer looks for this structure to register yolo
 *
 */
GST_PLUGIN_DEFINE(
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
