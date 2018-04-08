/* 
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2018  <<user@hostname.org>>
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
 
#ifndef __GST_YOLO_H__
#define __GST_YOLO_H__

#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_YOLO \
  (gst_yolo_get_type())
#define GST_YOLO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_YOLO,Gstyolo))
#define GST_YOLO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_YOLO,GstyoloClass))
#define GST_IS_YOLO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_YOLO))
#define GST_IS_YOLO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_YOLO))

typedef struct _Gstyolo      Gstyolo;
typedef struct _GstyoloClass GstyoloClass;

struct _Gstyolo {
  GstElement element;
  GstPad *sinkpad, *srcpad;
  gint32 width, height;
  gboolean silent;
  int layer;
  char *cfg;
  char *model;
  char *names;
  // text overlays
  double textwidth, textheight;
  CvFont font;
  gint32 xpos;
  gint32 ypos;
  gint32 thickness;
  guchar colorR;
  guchar colorG;
  guchar colorB;
};

struct _GstyoloClass {
  GstVideoFilter parent_class;
};

GType gst_yolo_get_type (void);

G_END_DECLS

#endif /* __GST_YOLO_H__ */
