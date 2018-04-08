# tx2yolovideo

This project contains various attempts at grabbing video from the onboard Jetson TX2 camera. Particularly interesting is yolo.c which is a gstreamer pipeline that use yolo V3 to detect and put bounding boxes around objects.

Briefly:

* yolo.c: Darknet V3 gstreamer pipeline, that also will save stream as mp4, needs libgstyolo.so and libdarknes.so
* yolo_objection_detection.cpp: darknet V2 C++ version
* tegra-cam.py: actually not for TX2, kind of a webcam thing
* httplaunch.c: experimantal - don't use
* tx2video.cpp: experimental - don't use
* gst.sh: simple pipeline

The yolo app is the most developed. Here is a screen shot:

<img height=25% width=25% src="./elephant.png"/>

Super thanks to Joseph Redmon for darknet: https://pjreddie.com/darknet/yolo/

@article{yolov3,
  title={YOLOv3: An Incremental Improvement},
  author={Redmon, Joseph and Farhadi, Ali},
  journal = {arXiv},
  year={2018}
}
