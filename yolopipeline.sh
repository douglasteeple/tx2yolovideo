#!/bin/bash
#
# run the yolo plugin in gstreamer-1.0
#
export GST_PLUGIN_PATH=$HOME3/gst-template/gst-plugin/src/.lib
movie=test.mp4
#facedetect="! videoconvert ! facedetect"
rm -f ${movie}
gst-launch-1.0 -e nvcamerasrc ! 'video/x-raw(memory:NVMM),width=2592, height=1458, framerate=30/1' ! nvvidconv ! videoconvert ! 'video/x-raw, width=640, height=360, format=(string)BGR' ! yolo $1 $2 $3 ${facedetect} ! videoconvert ! clockoverlay halignment=2 valignment=1 ! tee name=t t. ! queue  ! videoconvert ! omxh264enc ! 'video/x-h264, stream-format=(string)byte-stream' ! h264parse ! qtmux ! filesink location=${movie} sync=false  t. ! queue ! videoconvert ! ximagesink
#gst-launch-1.0 -e -v nvcamerasrc ! 'video/x-raw(memory:NVMM),width=1280, height=720, framerate=120/1' ! nvvidconv flip-method=0 ! videoconvert ! queue ! omxh264enc ! 'video/x-h264, stream-format=(string)byte-stream' ! h264parse ! qtmux ! filesink location=${movie} sync=false
