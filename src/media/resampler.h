// Copyright (c) 2016 Alexandr Topilski. All rights reserved.

#pragma once

extern "C" {
#include <libswresample/swresample.h>
#include <libavcodec/avcodec.h>
}

#include "macros.h"

namespace fasto {
namespace media {

typedef struct resampler_t {
  SwrContext *swr_ctx;

  int src_rate;
  enum AVSampleFormat src_fmt;
  uint64_t src_ch_layout;
  int src_nb_samples;

  int dst_rate;
  enum AVSampleFormat dst_fmt;
  uint64_t dst_ch_layout;
  int dst_nb_samples;
  int dst_linesize;
} resampler_t;

resampler_t* alloc_resampler(AVCodecContext *outctx, int src_rate, enum AVSampleFormat src_fmt,
                             uint64_t src_ch_layout, int src_nb_samples);
int resampler_alloc_array_and_samples(resampler_t *resampler, uint8_t ***dst_data);
int resampler_convert(resampler_t *resampler, uint8_t **out, int out_count,
                      const uint8_t **in , int in_count);
void free_resampler(resampler_t *resampler);

}  // namespace media
}  // namespace fasto
