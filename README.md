# tx2yolovideo

This project contains various attempts at grabbing video from the onboard Jetson TX2 camera. Particularly interesting is yolo.c which is a gstreamer pipeline that use yolo V3 to detect and put bounding boxes around objects.

Briefly:

* yolo_objection_detection.cpp: darknet V2 C++ version
* yolo.c: Darknet V3 gstreamer pipeline, that also will save stream as mp4, needs libgstyolo.so and libdarknes.so
* tegra-cam.py: actually not for TX2, kind of a webcam thing
* httplaunch.c: experimantal - don't use
* tx2video.cpp: experimental - don't use
* gst.sh: simple pipeline

Super thanks to Joseph Redmon for darknet: https://pjreddie.com/darknet/yolo/

@article{yolov3,
  title={YOLOv3: An Incremental Improvement},
  author={Redmon, Joseph and Farhadi, Ali},
  journal = {arXiv},
  year={2018}
}
