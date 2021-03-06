// Copyright (c) 2016 Alexandr Topilski. All rights reserved.

#pragma once

#include "macros.h"

#include <opencv2/opencv.hpp>

#define DUMP_MEDIA 0

namespace fasto {
namespace media {

struct output_stream_t;
struct resampler_t;
struct own_nal_unit_t;

typedef struct media_stream_params_t {
  uint32_t height_video;
  uint32_t width_video;
  uint32_t video_fps;

  uint32_t audio_channels;
  uint32_t audio_sample_rate;
  uint32_t bit_per_sample;
  uint32_t audio_bit_rate;

  uint32_t audio_channels_out;
  uint32_t audio_sample_rate_out;
  uint32_t audio_bit_rate_out;

  bool need_encode;
} media_stream_params_t;

typedef struct media_stream_t {
  struct output_stream_t* ostream;
  struct own_nal_unit_t * nalu;

  uint64_t audio_pcm_id;
  uint64_t video_frame_id;
  uint64_t video_frame_sps_pps_id;
  uint32_t ts_fpackv_in_stream_msec;
  uint32_t ts_fpacka_in_stream_msec;

  uint32_t cur_ts_video_remote_msec;
  uint32_t cur_ts_video_local_msec;
  uint64_t sample_id;
  uint8_t * mkf_buffer;

#if DUMP_MEDIA
  FILE * media_dump;
  FILE * only_mkf;
#endif

  media_stream_params_t params;
} media_stream_t;

media_stream_t* alloc_video_stream(const char * path_to_save,
                                   media_stream_params_t * params);  // h264, aac
const char * get_media_stream_file_path(media_stream_t* stream);
int write_video_frame_to_media_stream(media_stream_t * stream, const cv::Mat *mat);
int write_audio_frame_to_media_stream(media_stream_t * stream, uint8_t *data, size_t size);
void free_video_stream(media_stream_t * stream);

}  // namespace media
}  // namespace fasto
