/* GStreamer
 *
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *   Author: Thiago Santos <ts.santos@sisa.samsung.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <gst/app/app.h>

static GstPad *mysrcpad, *mysinkpad;
static GstElement *dec;

#define TEST_VIDEO_WIDTH 640
#define TEST_VIDEO_HEIGHT 480
#define TEST_VIDEO_FPS_N 30
#define TEST_VIDEO_FPS_D 1

#define GST_VIDEO_DECODER_TESTER_TYPE gst_video_decoder_tester_get_type()
static GType gst_video_decoder_tester_get_type (void);

typedef struct _GstVideoDecoderTester GstVideoDecoderTester;
typedef struct _GstVideoDecoderTesterClass GstVideoDecoderTesterClass;

struct _GstVideoDecoderTester
{
  GstVideoDecoder parent;
};

struct _GstVideoDecoderTesterClass
{
  GstVideoDecoderClass parent_class;
};

G_DEFINE_TYPE (GstVideoDecoderTester, gst_video_decoder_tester,
    GST_TYPE_VIDEO_DECODER);

static gboolean
gst_video_decoder_tester_start (GstVideoDecoder * dec)
{
  return TRUE;
}

static gboolean
gst_video_decoder_tester_stop (GstVideoDecoder * dec)
{
  return TRUE;
}

static gboolean
gst_video_decoder_tester_set_format (GstVideoDecoder * dec,
    GstVideoCodecState * state)
{
  GstVideoCodecState *res = gst_video_decoder_set_output_state (dec,
      GST_VIDEO_FORMAT_GRAY8, 640, 480, NULL);

  gst_video_codec_state_unref (res);
  return TRUE;
}

static GstFlowReturn
gst_video_decoder_tester_handle_frame (GstVideoDecoder * dec,
    GstVideoCodecFrame * frame)
{
  guint8 *data;
  gint size;
  GstMapInfo map;

  /* the output is gray8 */
  size = TEST_VIDEO_WIDTH * TEST_VIDEO_HEIGHT;
  data = g_malloc0 (size);

  gst_buffer_map (frame->input_buffer, &map, GST_MAP_READ);
  memcpy (data, map.data, sizeof (guint64));
  gst_buffer_unmap (frame->input_buffer, &map);

  frame->output_buffer = gst_buffer_new_wrapped (data, size);
  frame->pts = GST_BUFFER_PTS (frame->input_buffer);
  frame->duration = GST_BUFFER_DURATION (frame->input_buffer);

  return gst_video_decoder_finish_frame (dec, frame);
}

static void
gst_video_decoder_tester_class_init (GstVideoDecoderTesterClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *audiosink_class = GST_VIDEO_DECODER_CLASS (klass);

  static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-test-custom"));

  static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw"));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));

  gst_element_class_set_metadata (element_class,
      "VideoDecoderTester", "Decoder/Video", "yep", "me");

  audiosink_class->start = gst_video_decoder_tester_start;
  audiosink_class->stop = gst_video_decoder_tester_stop;
  audiosink_class->handle_frame = gst_video_decoder_tester_handle_frame;
  audiosink_class->set_format = gst_video_decoder_tester_set_format;
}

static void
gst_video_decoder_tester_init (GstVideoDecoderTester * tester)
{
}

static void
setup_videodecodertester (void)
{
  GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw")
      );
  GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-test-custom")
      );

  dec = g_object_new (GST_VIDEO_DECODER_TESTER_TYPE, NULL);
  mysrcpad = gst_check_setup_src_pad (dec, &srctemplate);
  mysinkpad = gst_check_setup_sink_pad (dec, &sinktemplate);
}

static void
cleanup_videodecodertest (void)
{
  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (dec);
  gst_check_teardown_sink_pad (dec);
  gst_check_teardown_element (dec);
}

#define NUM_BUFFERS 1000
GST_START_TEST (videodecoder_playback)
{
  GstSegment segment;
  GstCaps *caps;
  GstBuffer *buffer;
  guint64 i;
  GList *iter;

  setup_videodecodertester ();

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  fail_unless (gst_pad_push_event (mysrcpad,
          gst_event_new_stream_start ("randomvalue")));

  /* push caps */
  caps =
      gst_caps_new_simple ("video/x-test-custom", "width", G_TYPE_INT,
      TEST_VIDEO_WIDTH, "height", G_TYPE_INT, TEST_VIDEO_HEIGHT, "framerate",
      GST_TYPE_FRACTION, TEST_VIDEO_FPS_N, TEST_VIDEO_FPS_D, NULL);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_caps (caps)));

  /* push a new segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push buffers, the data is actually a number so we can track them */
  for (i = 0; i < NUM_BUFFERS; i++) {
    guint64 *data = g_malloc (sizeof (guint64));

    *data = i;

    buffer = gst_buffer_new_wrapped (data, sizeof (guint64));

    GST_BUFFER_PTS (buffer) =
        gst_util_uint64_scale_round (i, GST_SECOND * TEST_VIDEO_FPS_D,
        TEST_VIDEO_FPS_N);
    GST_BUFFER_DURATION (buffer) =
        gst_util_uint64_scale_round (GST_SECOND, TEST_VIDEO_FPS_D,
        TEST_VIDEO_FPS_N);

    fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  }

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* check that all buffers were received by our source pad */
  fail_unless (g_list_length (buffers) == NUM_BUFFERS);
  i = 0;
  for (iter = buffers; iter; iter = g_list_next (iter)) {
    GstMapInfo map;
    guint64 num;

    buffer = iter->data;

    gst_buffer_map (buffer, &map, GST_MAP_READ);


    num = *(guint64 *) map.data;
    fail_unless (i == num);
    fail_unless (GST_BUFFER_PTS (buffer) == gst_util_uint64_scale_round (i,
            GST_SECOND * TEST_VIDEO_FPS_D, TEST_VIDEO_FPS_N));
    fail_unless (GST_BUFFER_DURATION (buffer) ==
        gst_util_uint64_scale_round (GST_SECOND, TEST_VIDEO_FPS_D,
            TEST_VIDEO_FPS_N));

    gst_buffer_unmap (buffer, &map);
    i++;
  }

  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  cleanup_videodecodertest ();
}

GST_END_TEST;

static Suite *
gst_videodecoder_suite (void)
{
  Suite *s = suite_create ("GstVideoDecoder");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, videodecoder_playback);

  return s;
}

GST_CHECK_MAIN (gst_videodecoder);
