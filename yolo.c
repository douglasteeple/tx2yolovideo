/*
 * gstreamer yolo driver, which replaces yolopipeline.sh. 
 * Requires the gstyolo plugin which is in
 * gst-template/gst-plugin/src/.libs/
 * use this command to enable gstreamer-1.0 to find it:
 * export GST_PLUGIN_PATH=$HOME3/gst-template/gst-plugin/src/.libs
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

static GstElement *pipeline = NULL;
static GMainLoop *loop = NULL;

static gboolean bus_call(GstBus *bus,
                          GstMessage *msg,
                          gpointer    data)
{
    GMainLoop *loop =(GMainLoop *)data;
    
    switch(GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
			gst_element_send_event(pipeline, gst_event_new_eos()); ;
		    g_main_loop_quit(loop);
        break;

        case GST_MESSAGE_ERROR: {
            gchar *debug = NULL;
            GError *err = NULL;
            
            gst_message_parse_error(msg, &err, &debug);
            
            g_print("Error: %s\n", err->message);
            g_error_free(err);
            
            if(debug) {
                g_print("Debug details: %s\n", debug);
                g_free(debug);
            }
            
            g_main_loop_quit(loop);
            break;
        }
        default:
        break;
    }
    
    return TRUE;
}

/* Signal handler for ctrl+c */
void intHandler(int dummy) {

	/* Out of the main loop, clean up nicely */
	g_print ("Stopping...\n");
	gst_element_send_event(pipeline, gst_event_new_eos());
	gst_element_set_state (pipeline, GST_STATE_NULL);
	gst_object_unref (GST_OBJECT (pipeline));
	g_main_loop_unref (loop);

	exit(0);
}

gint main(gint argc, gchar *argv[])
{
    
    /* initialization */
    gst_init(&argc, &argv);
 	signal(SIGINT, intHandler);
	loop = g_main_loop_new(NULL, FALSE);

	gboolean silent = TRUE;
 	int width = 640;
	int height = 360;
    int camerawidth = 2592;
	int cameraheight = 1944;
	int mode = 1;
	int framerate = 30;
	char *movie = NULL;
	char buff[4096];

    /* parse args */
 	for (int i=1; i<argc; i++) {
		char *arg = strdup(argv[i]);
		char *equals = strchr(arg, '=');
		if (equals != NULL) {
			*equals = '\0';
			equals++;
			if (!strcmp(arg, "width")) {
				width = atoi(equals);
			} else if (!strcmp(arg, "height")) {
				height = atoi(equals);
			} else if (!strcmp(arg, "mode")) {
				mode = atoi(equals);
			} else if (!strcmp(arg, "movie")) {
				movie = strdup(equals);
			} else if (!strcmp(arg, "silent")) {
				silent = !strcmp(equals, "TRUE");
			}
		} else if (!strcmp(arg, "--help")) {
			printf("usage: yolo [mode=[1|2|3]] [movie=<name>.mp4] [silent=[TRUE|FALSE]] [width=<n>] [height=<n>]\n");
			printf("       Jetson TX2 camera modes:\n");
			printf("       mode 1: 2592 x 1944 FR=30.000000  CF=0x1109208a10 SensorModeType=4 CSIPixelBitDepth=10 DynPixelBitDepth=10\n");
			printf("       mode 2: 2592 x 1458 FR=30.000000  CF=0x1109208a10 SensorModeType=4 CSIPixelBitDepth=10 DynPixelBitDepth=10\n");
			printf("       mode 3: 1280 x 720  FR=120.000000 CF=0x1109208a10 SensorModeType=4 CSIPixelBitDepth=10 DynPixelBitDepth=10\n");
			exit(0);
		}
		free(arg);
	}

	switch (mode) {
		case 1:
			camerawidth = 2594;
			cameraheight = 1944;
			width = camerawidth/4;
			height = cameraheight/4;
		break;
		case 2:
			camerawidth = 2592;
			cameraheight = 1458;
			width = camerawidth/4;
			height = cameraheight/4;
		break;
		case 3:
			camerawidth = 1280;
			cameraheight = 720;
			framerate = 120;
			width = camerawidth/2;
			height = cameraheight/2;
		break;
		default:
		break;
	}

	if (movie) {
		sprintf(buff, "nvcamerasrc ! video/x-raw(memory:NVMM),width=%d, height=%d, framerate=%d/1 ! nvvidconv ! videoconvert ! video/x-raw, width=%d, height=%d, format=(string)BGR ! yolo name=yolo ! videoconvert ! clockoverlay halignment=2 valignment=1 ! tee name=t t. ! queue  ! videoconvert ! omxh264enc ! video/x-h264, stream-format=(string)byte-stream ! h264parse ! qtmux ! filesink location=%s sync=false  t. ! queue ! videoconvert ! ximagesink", camerawidth, cameraheight, framerate, width, height, movie);
	} else {
		sprintf(buff, "nvcamerasrc ! video/x-raw(memory:NVMM),width=%d, height=%d, framerate=%d/1 ! nvvidconv ! videoconvert ! video/x-raw, width=%d, height=%d, format=(string)BGR ! yolo name=yolo ! videoconvert ! clockoverlay halignment=2 valignment=1 ! videoconvert ! ximagesink", camerawidth, cameraheight, framerate, width, height);
	}
	GError *error = NULL;
	pipeline = gst_parse_launch(buff, &error);
	if (!pipeline) {
		g_print("Parse error: %s\n%s\n", error->message, buff);
		exit(1);
	}
    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    guint watch_id = gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);
	GstElement *yolo = gst_bin_get_by_name (GST_BIN (pipeline), "yolo");
	g_object_set(G_OBJECT(yolo), "silent", silent, NULL);
	g_object_unref (yolo);
   
    /* run */
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);

    if(ret == GST_STATE_CHANGE_FAILURE) {
        GstMessage *msg;
        
        g_print("Failed to start up pipeline!\n");
        
        /* check if there is an error message with details on the bus */
        msg = gst_bus_poll(bus, GST_MESSAGE_ERROR, 0);
        if (msg) {
            GError *err = NULL;
            gst_message_parse_error(msg, &err, NULL);
            g_print("ERROR: %s\n", err->message);
            g_error_free(err);
            gst_message_unref(msg);
        }
        return ret;
    }
    
    g_main_loop_run(loop);
    
    /* clean up */
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_source_remove(watch_id);
    g_main_loop_unref(loop);
    
    return ret;
}


