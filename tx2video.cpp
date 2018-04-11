#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <iostream>
#include <unistd.h>
#include <getopt.h>

#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>

/*
 *
 * Simple openCV viewer that uses gstreamer to build a pipeline
 * from the Jetson onboard camera to various output sinks.
 *
 * D. Teeple March 2018
 *
*/

using namespace std;

int main(int argc, char **argv)
{
    int c;
	int verbose_flag = false;
	bool cv = true;	
	bool doyolo = false;
	bool doyolo3 = false;
	const char *host = "127.0.0.1";
	const char *port = "8000";
	int width = 640, height = 360;

    struct option long_options[] =
        {
          /* These options set a flag. */
          {"verbose", no_argument,       &verbose_flag, 1},
          {"brief",   no_argument,       &verbose_flag, 0},
          /* These options don’t set a flag.
             We distinguish them by their indices. */
          {"X",     no_argument,		0, 'x'},
          {"model", required_argument,	0, 'm'},
          {"cfg",   required_argument,	0, 'f'},
          {"names", required_argument,	0, 'n'},
          {"yolo2", no_argument,		0, 'y'},
          {"yolo3", no_argument,		0, '3'},
          {"web",   no_argument,		0, 'w'},
          {"app",   no_argument, 		0, 'a'},
          {"xw",	no_argument,		0, 'b'},
          {"caption",required_argument, 0, 'c'},
          {"demo",	required_argument,  0, 'd'},
          {"help",	no_argument,		0, 'h'},
          {0, 0, 0, 0}
        };

	char gst[8192] = {""};
	char function[480] = {""};
	char sink[1024] = {"appsink"};
	char namesfile[64] = {"coco.names"};
	char modelfile[64] = {"yolo.weights"};
	char cfgfile[64] = {"yolo.cfg"};
	char share[64] = {"/usr/local/share/darknetv2"};	// location of the cfg and weight files for
	const char *yolo_object_detection = "yolo_object_detection -cfg=%s/cfg/%s -model=%s/cfg/%s -class_names=%s/data/%s -source=\"nvcamerasrc ! video/x-raw(memory:NVMM), width=(int)%d, height=(int)%d, format=(string)I420, framerate=(fraction)30/1 ! nvvidconv ! video/x-raw, format=(string)BGRx ! videoconvert ! video/x-raw, format=(string)BGR ! appsink\"";
	char whichdemo[64] = {"detector demo"};	// segmenter demo, art, nightmare

	opterr = 0;
	/* getopt_long stores the option index here. */
	int option_index = 0;

    // Retrieve the options:
    while ( (c = getopt_long(argc, argv, "xywab3hm:f:n:c:d:", long_options, &option_index)) != -1 ) {  // for each option...
        switch ( c ) {
         case 0:
          /* If this option sets a flag, do nothing else now. */
          if (long_options[option_index].flag != 0)
            break;

		  if (verbose_flag) {
		      printf("option %s", long_options[option_index].name);
		      if (optarg)
		        printf(" with arg %s", optarg);
		      printf("\n");
		  }
          break;

        case 'h':
		  cout << argv[0] << ":    [-x|-w|--xw|-y|-h|--help] [<width>x<height>] [--caption='text'] [<gstreamer effects>] [--model=<filename> --cfg=<filename> --names=<filename>]" << endl;
		  cout << "Stream video from the Jetson TX2 camera to an X window[-x], web[-w], both[--xw], yolo[-y] or opencv2 (default)." << endl;
		  cout << "Optionally apply gstreamer effects such as: " << endl;
		  cout << "  agingtv, burn, chromium, 'coloreffects preset=heat', cvlaplace, dodge, edgedetect, edgetv" << endl;
		  cout << "  exclusion, faceblur, facedetect, fisheye, kaleidoscope, marble, mirror, revtv, retinex, rippletv" << endl;
		  cout << "  'textoverlay text=\"My text\" valignment=top halignment=left font-desc=\"Sans, 72\" shaded-background=true'" << endl;
		  cout << "  pinch, stretch, streaktv, solarize, shagadelictv, sphere, twirl, tunnel, waterripple" << endl;
		  cout << "Detector: \"detector demo\" \"segmenter demo\", art, nightmare" << endl;          
		  return 0;

        case 'x':
          sprintf(sink, "ximagesink");
		  cv = false;
          break;

        case 'w':
          sprintf(sink, "vp8enc ! webmmux ! shout2send ip=%s port=%s password=hackme mount=/tx2.webm", host, port);
          cv = false;
		  break;

        case 'a':
			// default
           cv = true;
         break;

        case 'c':
			sprintf(&function[strlen(function)], " ! textoverlay text=\"%s\" valignment=top halignment=center font-desc=\"Times, 20\" shaded-background=true", optarg);
         break;

        case 'b':
          sprintf(sink, "tee name=t ! videoconvert ! queue ! ximagesink t. ! vp8enc ! webmmux ! shout2send ip=%s port=%s password=hackme mount=/tx2.webm", host, port);
          cv = false;
		  break;

		case 'd':
			strcpy(whichdemo, optarg);	// "detector demo" "segmenter demo", art, nightmare
			break;

        case 'm':
          strcpy(modelfile, optarg);
          break;

        case 'f':
          strcpy(cfgfile, optarg);
          break;

        case 'n':
          strcpy(namesfile, optarg);
          break;

        case 'y':
          cv = false;
          doyolo = true;
          break;

        case '3':
          cv = false;
		  doyolo3 = true;
		  strcpy(namesfile, "coco.data");
		  strcpy(modelfile, "yolov3.weights");
		  strcpy(cfgfile, "yolov3.cfg");
		  strcpy(share, "/usr/local/share/darknet");	// location of the cfg and weight files for
          break;

        case '?':
          /* getopt_long already printed an error message. */
          break;

        default:
          cout << "Invalid option" << (optarg?optarg:"") << " " << (char)c << " " << option_index << endl;
        }
    }
    /* Add any remaining command line arguments (not options) to function list. */
    if (optind < argc) {
		while (optind < argc) {
			if (2==sscanf(argv[optind], "%dx%d", &width, &height)) {
				// got it...
			} else {
				sprintf(&function[strlen(function)], " ! %s", argv[optind]);
			}
			optind++;
		}
    }

	if (cv) {

		sprintf(gst, "nvcamerasrc ! video/x-raw(memory:NVMM),width=1280, height=720, framerate=120/1 ! nvvidconv ! video/x-raw, format=I420, width=%d, height=%d ! videoconvert %s ! clockoverlay halignment=2 valignment=1 ! videoconvert ! video/x-raw, format=(string)BGR ! %s", width, height, function, sink);

		if (verbose_flag) {
			cout << gst << endl;
		}

		cv::VideoCapture cap(gst);

		if (!cap.isOpened()) {
			cout << "Failed to open camera." << endl;
			return -1;
		}

		unsigned int width = cap.get(CV_CAP_PROP_FRAME_WIDTH); 
		unsigned int height = cap.get(CV_CAP_PROP_FRAME_HEIGHT); 
		unsigned int pixels = width*height;

		if (verbose_flag) {
			cout << "Frame size : " << width << " x " << height << ", " << pixels << " Pixels " << endl;
		}
		cv::namedWindow("Jetson TX2 Camera", CV_WINDOW_AUTOSIZE);
		cv::Mat frame_in(width, height, CV_8UC3);

		while (true) {
			if (!cap.read(frame_in)) {
				cerr << "Capture read error" << endl;
				break;
			} else {
	   			cv::imshow("Jetson TX2 Camera",frame_in);
				cv::waitKey(1000/120); // let imshow draw and wait for next frame 8 ms for 120 fps
			}	
		}

		cap.release();
	} else if (doyolo) {
		sprintf(gst, yolo_object_detection, share, cfgfile, share, modelfile, share, namesfile, width, height);
		if (verbose_flag) {
			cout << gst << endl;
		}
		system(gst);
	} else if (doyolo3) {
		sprintf(gst, "darknet %s %s/cfg/%s %s/cfg/%s %s/cfg/%s \"nvcamerasrc ! video/x-raw(memory:NVMM), width=(int)1280, height=(int)720,format=(string)I420, framerate=(fraction)30/1 ! nvvidconv flip-method=0 ! video/x-raw, format=(string)BGRx ! videoconvert ! video/x-raw, format=(string)BGR ! %s\"", whichdemo, share, namesfile, share, cfgfile, share, modelfile, sink);
		if (verbose_flag) {
			cout << gst << endl;
		}
		system(gst);
	} else {
		sprintf(gst, "gst-launch-1.0 nvcamerasrc ! 'video/x-raw(memory:NVMM),width=1280, height=720, framerate=120/1' ! nvvidconv ! video/x-raw, format=I420, width=%d, height=%d ! videoconvert %s ! videoconvert ! clockoverlay halignment=2 valignment=1 time-format='%%Y/%%m/%%d %%H:%%M:%%S' ! %s", width, height, function, sink);
		if (verbose_flag) {
			cout << gst << endl;
		}
		system(gst);
	}
    return 0;
}
