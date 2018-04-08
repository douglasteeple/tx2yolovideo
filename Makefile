######################################################################
#
# makefile for tx2 video and yolo pipelines
#
######################################################################

TARGETS=tx2video yolo_object_detection yolo httplaunch 
LIBS=libgstyolo

INSTALLDIR=/usr/local/bin/
INSTALLLIBDIR=/usr/local/lib/
# gstreamer-1.0
GSTDIR=$(HOME3)/gst-template/gst-plugin/
GSTPLUGINDIR=/usr/lib/aarch64-linux-gnu/gstreamer-1.0/
GSTLIBDIR=$(GSTDIR)/src/.libs/
# darknet
DARKNETDIR=$(HOME3)/Projects/darknet/

all: $(TARGETS) $(LIBS)

tx2video: tx2video.cpp
	g++ $@.cpp -O3 -o $@ -Wno-unused-result -L/usr/local/cuda/nvvm/lib -L/usr/local/cuda/lib64 -I/usr/include/ -L/usr/local/opencv-3.1.0/lib -lopencv_core -lopencv_videoio -lopencv_highgui  

yolo_object_detection: yolo_object_detection.cpp
	g++ $< -O3 -o $@ -L/usr/lib/ `pkg-config --libs cuda-9.0` `pkg-config --libs --cflags opencv`   

yolo: yolo.c
	gcc $< -O3 -o $@ `pkg-config --cflags --libs gstreamer-1.0` `pkg-config --libs --cflags opencv` -L/usr/local/opencv-3.1.0/lib -lGL -lopencv_core -lopencv_videoio -lopencv_highgui

httplaunch: httplaunch.c
	gcc $< -O3 -o $@ `pkg-config --cflags --libs gstreamer-1.0` `pkg-config --cflags --libs gio-2.0`  

libgstyolo: 
	make -C $(GSTDIR)

zip:
	tar --exclude=tx2yolovideo.tgz -zcvf tx2yolovideo.tgz * -C $(GSTLIBDIR) libgstyolo.so libgstyolo.la -C $(DARKNETDIR) libdarknet.so libdarknet.a

clean:
	rm -f $(TARGETS)
	make -C $(GSTDIR) clean
	rm -f tx2yolovideo.tgz

remake: clean all

install: $(TARGETS) $(LIBS)
	for t in $(TARGETS) ; do sudo install $${t} $(INSTALLDIR); done
	sudo install $(GSTLIBDIR)/libgstyolo.so $(GSTLIBDIR)/libgstyolo.la $(GSTPLUGINDIR)
	sudo install $(DARKNETDIR)libdarknet.so $(DARKNETDIR)libdarknet.a $(INSTALLLIBDIR)

uninstall:
	for t in $(TARGETS) ; do sudo rm -f $(INSTALLDIR)$${t}; done
	rm -f $(GTSPLUGINDIR)/libgstyolo.so $(GTSPLUGINDIR)/libgstyolo.la


