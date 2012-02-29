/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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
 * SECTION:gstvideofilter
 * @short_description: Base class for video filters
 * 
 * <refsect2>
 * <para>
 * Provides useful functions and a base class for video filters.
 * </para>
 * <para>
 * The videofilter will by default enable QoS on the parent GstBaseTransform
 * to implement frame dropping.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideofilter.h"

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

GST_DEBUG_CATEGORY_STATIC (gst_video_filter_debug);
#define GST_CAT_DEFAULT gst_video_filter_debug

#define gst_video_filter_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstVideoFilter, gst_video_filter,
    GST_TYPE_BASE_TRANSFORM);

/* Answer the allocation query downstream. */
static gboolean
gst_video_filter_propose_allocation (GstBaseTransform * trans,
    gboolean passthrough, GstQuery * query)
{
  GstVideoFilter *filter = GST_VIDEO_FILTER_CAST (trans);
  GstVideoInfo info;
  GstBufferPool *pool;
  GstCaps *caps;
  gboolean need_pool;
  guint size;

  /* we're passthrough, let the parent implementation hande things */
  if (passthrough)
    return GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
        passthrough, query);

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);

  if (need_pool) {
    GstStructure *structure;

    pool = gst_video_buffer_pool_new ();

    structure = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set (structure, caps, size, 0, 0, 0, 15);
    if (!gst_buffer_pool_set_config (pool, structure))
      goto config_failed;
  } else
    pool = NULL;

  gst_query_set_allocation_params (query, size, 0, 0, 0, 15, pool);
  gst_object_unref (pool);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE);

  return TRUE;

  /* ERRORS */
config_failed:
  {
    GST_ERROR_OBJECT (filter, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }
}

/* configure the allocation query that was answered downstream, we can configure
 * some properties on it. Only called when not in passthrough mode. */
static gboolean
gst_video_filter_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstBufferPool *pool = NULL;
  guint size, min, max, prefix, alignment;

  gst_query_parse_allocation_params (query, &size, &min, &max, &prefix,
      &alignment, &pool);

  if (pool) {
    GstStructure *config;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_set_config (pool, config);
  }
  return TRUE;
}


/* our output size only depends on the caps, not on the input caps */
static gboolean
gst_video_filter_transform_size (GstBaseTransform * btrans,
    GstPadDirection direction, GstCaps * caps, gsize size,
    GstCaps * othercaps, gsize * othersize)
{
  gboolean ret = TRUE;
  GstVideoInfo info;

  g_assert (size);

  ret = gst_video_info_from_caps (&info, othercaps);
  if (ret)
    *othersize = info.size;

  return ret;
}

static gboolean
gst_video_filter_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    gsize * size)
{
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (btrans, "Failed to parse caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  *size = info.size;

  GST_DEBUG_OBJECT (btrans, "Returning size %" G_GSIZE_FORMAT " bytes"
      "for caps %" GST_PTR_FORMAT, *size, caps);

  return TRUE;
}

static gboolean
gst_video_filter_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoFilter *filter = GST_VIDEO_FILTER_CAST (trans);
  GstVideoFilterClass *fclass;
  GstVideoInfo in_info, out_info;
  gboolean res;

  /* input caps */
  if (!gst_video_info_from_caps (&in_info, incaps))
    goto invalid_caps;

  /* output caps */
  if (!gst_video_info_from_caps (&out_info, outcaps))
    goto invalid_caps;

  fclass = GST_VIDEO_FILTER_GET_CLASS (filter);
  if (fclass->set_info)
    res = fclass->set_info (filter, incaps, &in_info, outcaps, &out_info);
  else
    res = TRUE;

  if (res) {
    filter->in_info = in_info;
    filter->out_info = out_info;
    if (fclass->transform_frame == NULL)
      gst_base_transform_set_in_place (trans, TRUE);
  }
  filter->negotiated = res;

  return res;

  /* ERRORS */
invalid_caps:
  {
    GST_ERROR_OBJECT (filter, "invalid caps");
    filter->negotiated = FALSE;
    return FALSE;
  }
}

static GstFlowReturn
gst_video_filter_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstFlowReturn res;
  GstVideoFilter *filter = GST_VIDEO_FILTER_CAST (trans);
  GstVideoFilterClass *fclass;

  if (G_UNLIKELY (!filter->negotiated))
    goto unknown_format;

  fclass = GST_VIDEO_FILTER_GET_CLASS (filter);
  if (fclass->transform_frame) {
    GstVideoFrame in_frame, out_frame;

    if (!gst_video_frame_map (&in_frame, &filter->in_info, inbuf, GST_MAP_READ))
      goto invalid_buffer;

    if (!gst_video_frame_map (&out_frame, &filter->out_info, outbuf,
            GST_MAP_WRITE))
      goto invalid_buffer;

    res = fclass->transform_frame (filter, &in_frame, &out_frame);

    gst_video_frame_unmap (&out_frame);
    gst_video_frame_unmap (&in_frame);
  } else {
    GST_DEBUG_OBJECT (trans, "no transform_frame vmethod");
    res = GST_FLOW_OK;
  }

  return res;

  /* ERRORS */
unknown_format:
  {
    GST_ELEMENT_ERROR (filter, CORE, NOT_IMPLEMENTED, (NULL),
        ("unknown format"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
invalid_buffer:
  {
    GST_ELEMENT_WARNING (filter, CORE, NOT_IMPLEMENTED, (NULL),
        ("invalid video buffer received"));
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_video_filter_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstFlowReturn res;
  GstVideoFilter *filter = GST_VIDEO_FILTER_CAST (trans);
  GstVideoFilterClass *fclass;

  if (G_UNLIKELY (!filter->negotiated))
    goto unknown_format;

  fclass = GST_VIDEO_FILTER_GET_CLASS (filter);
  if (fclass->transform_frame_ip) {
    GstVideoFrame frame;

    if (!gst_video_frame_map (&frame, &filter->in_info, buf, GST_MAP_READWRITE))
      goto invalid_buffer;

    res = fclass->transform_frame_ip (filter, &frame);

    gst_video_frame_unmap (&frame);
  } else {
    GST_DEBUG_OBJECT (trans, "no transform_frame_ip vmethod");
    res = GST_FLOW_OK;
  }

  return res;

  /* ERRORS */
unknown_format:
  {
    GST_ELEMENT_ERROR (filter, CORE, NOT_IMPLEMENTED, (NULL),
        ("unknown format"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
invalid_buffer:
  {
    GST_ELEMENT_WARNING (filter, CORE, NOT_IMPLEMENTED, (NULL),
        ("invalid video buffer received"));
    return GST_FLOW_OK;
  }
}

static void
gst_video_filter_class_init (GstVideoFilterClass * g_class)
{
  GstBaseTransformClass *trans_class;
  GstVideoFilterClass *klass;

  klass = (GstVideoFilterClass *) g_class;
  trans_class = (GstBaseTransformClass *) klass;

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_filter_set_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_video_filter_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_video_filter_decide_allocation);
  trans_class->transform_size =
      GST_DEBUG_FUNCPTR (gst_video_filter_transform_size);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_video_filter_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_video_filter_transform);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_video_filter_transform_ip);

  GST_DEBUG_CATEGORY_INIT (gst_video_filter_debug, "videofilter", 0,
      "videofilter");
}

static void
gst_video_filter_init (GstVideoFilter * instance)
{
  GstVideoFilter *videofilter = GST_VIDEO_FILTER (instance);

  GST_DEBUG_OBJECT (videofilter, "gst_video_filter_init");

  videofilter->negotiated = FALSE;
  /* enable QoS */
  gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM (videofilter), TRUE);
}
