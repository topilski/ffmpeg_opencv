// Copyright (c) 2016 Alexandr Topilski. All rights reserved.

#include "media/resampler.h"

#include "log.h"

namespace fasto {
namespace media {

resampler_t* alloc_resampler(AVCodecContext *outctx, int src_rate, enum AVSampleFormat src_fmt,
                             uint64_t src_ch_layout, int src_nb_samples) {
    if (!outctx) {
        debug_perror("alloc_resampler", EINVAL);
        return NULL;
    }

    resampler_t* resampler = reinterpret_cast<resampler_t*>(calloc(1, sizeof(resampler_t)));
    if (!resampler) {
        debug_perror("calloc", ENOMEM);
        return NULL;
    }

    resampler->src_rate = src_rate;
    resampler->src_fmt = src_fmt;
    resampler->src_ch_layout = src_ch_layout;
    resampler->src_nb_samples = src_nb_samples;

    resampler->dst_rate = outctx->sample_rate;
    resampler->dst_fmt = outctx->sample_fmt;
    resampler->dst_ch_layout = outctx->channel_layout;
    resampler->dst_linesize = -1;

    /* compute the number of converted samples: buffering is avoided
    * ensuring that the output buffer will contain at least all the
    * converted input samples */
    resampler->dst_nb_samples = av_rescale_rnd(resampler->src_nb_samples, resampler->dst_rate,
                                               resampler->src_rate, AV_ROUND_UP);

    resampler->swr_ctx = swr_alloc_set_opts(NULL, resampler->dst_ch_layout, resampler->dst_fmt,
                                            resampler->dst_rate, resampler->src_ch_layout,
                                            resampler->src_fmt, resampler->src_rate, 0, NULL);

    if (!resampler->swr_ctx) {
        debug_msg("Could not setup SwrContext\n");
        free(resampler);
        return NULL;
    }

    if (swr_init(resampler->swr_ctx) < 0) {
        debug_msg("fe_resample_open: Can't init convertor\n");
        free(resampler);
        return NULL;
    }

    return resampler;
}

int resampler_alloc_array_and_samples(resampler_t *resampler, uint8_t ***dst_data) {
    if (!resampler) {
        debug_perror("resampler_alloc_array_and_samples", EINVAL);
        return ERROR_RESULT_VALUE;
    }

    /* buffer is going to be directly written to a rawaudio file, no alignment */
    int dst_nb_channels = av_get_channel_layout_nb_channels(resampler->dst_ch_layout);
    int ret = av_samples_alloc_array_and_samples(dst_data, &resampler->dst_linesize,
                                                 dst_nb_channels, resampler->dst_nb_samples,
                                                 resampler->dst_fmt, 0);

    if (ret < 0) {
        debug_msg("Could not allocate destination samples\n");
        return ERROR_RESULT_VALUE;
    }

    return ret;
}

int resampler_convert(resampler_t *resampler, uint8_t **out, int out_count,
                      const uint8_t **in , int in_count) {
    if (!resampler) {
        debug_perror("resampler_alloc_array_and_samples", EINVAL);
        return ERROR_RESULT_VALUE;
    }

    int ret = swr_convert(resampler->swr_ctx, out, out_count, in, in_count);
    if (ret < 0) {
        debug_msg("Error while converting!\n");
        return ERROR_RESULT_VALUE;
    }

    return ret;
}

void free_resampler(resampler_t *resampler) {
    if (!resampler) {
        debug_perror("free_resampler", EINVAL);
        return;
    }

    if (resampler->swr_ctx) {
        swr_free(&resampler->swr_ctx);
    }
    free(resampler);
}

}  // namespace media
}  // namespace fasto
