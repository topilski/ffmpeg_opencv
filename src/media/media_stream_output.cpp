// Copyright (c) 2016 Alexandr Topilski. All rights reserved.

#include "media/media_stream_output.h"

#include "log.h"

#include "media/codec_holder.h"
#include "media/nal_units.h"

#ifdef WITH_OPUS
#include "media/resampler.h"
#endif

#include "utils/time_utils.h"
#include "utils/utils.h"

#define WITH_CODEC 0

#define PCM_SAMPLES_COUNT 1024

#define SAVE_LOCAL_TIME 0
#define SAVE_REMOTE_TIME 1
#define SAVE_FRAME_ID 2
#define SAVE_FRAME_POLICY SAVE_REMOTE_TIME

namespace fasto {
namespace media {

namespace {

void write_empty_audio_samples(media_stream_t *stream, int count) {
  if (count <= 0) {
      return;
  }

  debug_warning("try to write empty audio packets count %d\n", count);
  int i = 0;
  for (i = 0; i < count; ++i) {
    AVPacket avpkt2 = {0};
    av_init_packet(&avpkt2);
    int got_packet = 0;
    static uint8_t zero[PCM_SAMPLES_COUNT * 4] = {0};
    if (encode_ostream_audio_buffer(stream->ostream, zero, PCM_SAMPLES_COUNT * 4,
                                     &avpkt2, &got_packet) >= 0) {
      if (got_packet) {
        update_audio_packet_pts(stream->ostream, stream->sample_id, &avpkt2);
        stream->sample_id++;
        write_audio_frame(stream->ostream, &avpkt2);
      }
    }
  }
}

void write_video_frame_inner(media_stream_t * stream, header_enc_frame_t * header) {
  if (!header) {
    return;
  }

  if (!stream->nalu) {
    return;
  }

  uint32_t len = 0;
  uint32_t mst = utils::currentms();
  if (stream->ts_fpackv_in_stream_msec == 0) {
    stream->ts_fpackv_in_stream_msec = mst;
  }

  frame_data_t *fdata = &header->frame_data;
  if (!fdata) {
    return;
  }

  uint64_t cur_msr = av_rescale(header->t1.value, 1000, header->t1.timescale);
  uint8_t* key_frame = create_sps_pps_key_frame(stream->nalu, fdata->data, fdata->len, &len);
  int is_key_f = is_key_frame(fdata->data, fdata->len);

  // DCHECK(key_frame);
  if (key_frame) {
    AVPacket pkt = {0};
    uint32_t cur_msl = mst - stream->ts_fpackv_in_stream_msec;
#if SAVE_FRAME_POLICY == SAVE_FRAME_ID
    init_video_packet(stream->ostream, key_frame, len, stream->frame_id, &pkt);
#elif SAVE_FRAME_POLICY == SAVE_REMOTE_TIME
    init_video_packet_ms(stream->ostream, key_frame, len, cur_msr, &pkt);
#elif SAVE_FRAME_POLICY == SAVE_LOCAL_TIME
    init_video_packet_ms(stream->ostream, key_frame, len, cur_msl, &pkt);
#else
#error please specify policy to save
#endif
    stream->cur_ts_video_remote_msec = cur_msr;
    stream->cur_ts_video_local_msec = cur_msl;
    stream->video_frame_id++;
    if (is_key_f) {
      pkt.flags |= AV_PKT_FLAG_KEY;
    }
    write_video_frame(stream->ostream, &pkt);

    av_free_packet(&pkt);
    free(key_frame);
  }
}

}  // namespace

media_stream_t* alloc_video_stream(const char * path_to_save, media_stream_params_t * params) {
  if (!path_to_save || !params) {
    return NULL;
  }

  media_stream_t* stream = reinterpret_cast<media_stream_t*>(malloc(sizeof(media_stream_t)));
  if (!stream) {
    return NULL;
  }

  stream->ostream = NULL;
  stream->nalu = NULL;
  stream->audio_pcm_id = 0;
  stream->video_frame_id = 0;
  stream->video_frame_sps_pps_id = 0;
  stream->cur_ts_video_remote_msec = 0;
  stream->cur_ts_video_local_msec = 0;
  stream->ts_fpackv_in_stream_msec = 0;
  stream->ts_fpacka_in_stream_msec = 0;
  stream->sample_id = 0;
  stream->mkf_buffer = NULL;
#if DUMP_MEDIA
  char media_dump_path[PATH_MAX] = {0};
  sprintf(media_dump_path, "%s.data", path_to_save);
  stream->media_dump = fopen(media_dump_path, "wb");

  char media_dump_mkf_path[PATH_MAX] = {0};
  sprintf(media_dump_mkf_path, "%s.data.mkf", path_to_save);
  stream->only_mkf = fopen(media_dump_mkf_path, "wb");
#endif
  int res;
  AVFormatContext *formatContext;

  if(!params->need_encode){
    stream->ostream = alloc_output_stream(NULL, path_to_save, NULL);
    if (stream->ostream) {
      debug_msg("Created output media file path: %s!\n", path_to_save);
      formatContext = stream->ostream->oformat_context;
      res = add_video_stream(stream->ostream, AV_CODEC_ID_H264,
                                               params->width_video, params->height_video,
                                               40000, params->video_fps);
    } else {
      debug_error("WARNING initiator output video stream with path %s not opened!", path_to_save);
      free(stream);
      return NULL;
    }
  } else {
    stream->ostream = alloc_output_stream_without_codec(path_to_save);
    if (stream->ostream) {
      debug_msg("Created output media file path: %s!\n", path_to_save);
      formatContext = stream->ostream->oformat_context;
      res = add_video_stream_without_codec(stream->ostream, AV_CODEC_ID_H264,
                                               params->width_video, params->height_video,
                                               params->video_fps);
    } else {
      debug_error("WARNING initiator output video stream with path %s not opened!", path_to_save);
      free(stream);
      return NULL;
    }
  }

  if (res == ERROR_RESULT_VALUE) {
    debug_av_perror("add_video_stream", res);
    free_output_stream(stream->ostream);
    free(stream);
    return NULL;
  }

  res = add_audio_stream(stream->ostream, AV_CODEC_ID_AAC, params->audio_sample_rate_out,
                         params->audio_channels_out, params->audio_bit_rate_out);
  if (res == ERROR_RESULT_VALUE) {
    debug_error("add_audio_stream failed!\n");
    free_output_stream(stream->ostream);
    free(stream);
    return NULL;
  }

  AVDictionary* opt = NULL;
  av_dict_set(&opt, "strict", "experimental", 0);
  res = open_audio_stream(stream->ostream, opt);
  if (res < 0) {
    debug_error("open_audio_stream failed!\n");
  }

// av_dump_format(stream->ostream->oformat_context, 0, path_to_save, 1);

  AVDictionary* opt2 = NULL;
  av_dict_set_int(&opt2, "hls_time", 5, 0);
  av_dict_set_int(&opt2, "hls_list_size", 0, 0);
  res = avformat_write_header(formatContext, &opt2);
  if (res < 0) {
    debug_error("avformat_write_header failed: error %d!", res);
  }
  av_dict_free(&opt2);

  av_dump_format(stream->ostream->oformat_context, 0, path_to_save, 1);

  stream->params = *params;

  return stream;
}

const char * get_media_stream_file_path(media_stream_t* stream) {
  if (!stream || !stream->ostream) {
    return NULL;
  }

  AVFormatContext *formatContext = stream->ostream->oformat_context;
  if (!formatContext) {
    return NULL;
  }

  return formatContext->filename;
}

static AVFrame * icv_alloc_picture_FFMPEG(int pix_fmt, int width, int height, bool alloc)
{
    AVFrame * picture;
    uint8_t * picture_buf;
    int size;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    picture->format = pix_fmt;
    picture->width = width;
    picture->height = height;

    size = avpicture_get_size( (AVPixelFormat) pix_fmt, width, height);
    if(alloc){
        picture_buf = (uint8_t *) malloc(size);
        if (!picture_buf)
        {
            av_free(picture);
            return NULL;
        }
        avpicture_fill((AVPicture *)picture, picture_buf,
                       (AVPixelFormat) pix_fmt, width, height);
    }
    else {
    }
    return picture;
}

int write_video_frame_to_media_stream(media_stream_t * stream, const cv::Mat *mat) {
  if (!stream || !mat) {
    return ERROR_RESULT_VALUE;
  }

#if DUMP_MEDIA
  if (stream->media_dump) {
    fwrite(data, sizeof(uint8_t), size, stream->media_dump);
  }
  if (mkf && stream->only_mkf) {
    fwrite(data, sizeof(uint8_t), size, stream->only_mkf);
  }
#endif

  if(!stream->params.need_encode){
    AVCodecContext *codec_ctx = stream->ostream->video_stream->codec;

    AVFrame * frame = av_frame_alloc();
    int res = avpicture_fill((AVPicture*)frame, mat->data, AV_PIX_FMT_BGR24,
                   codec_ctx->width, codec_ctx->height);


    struct SwsContext *final_sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height,
        AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height,
        codec_ctx->pix_fmt, SWS_BICUBIC, 0, 0, 0);

    AVFrame * yframe = icv_alloc_picture_FFMPEG(codec_ctx->pix_fmt, codec_ctx->width, codec_ctx->height, true);
    res = sws_scale(final_sws_ctx, frame->data,
              frame->linesize,
              0, codec_ctx->height,
              yframe->data,
              yframe->linesize);

    AVPacket avpkt2 = {0};
    av_init_packet(&avpkt2);
    int got_packet = 0;
    if (encode_ostream_video_frame(stream->ostream, frame, &avpkt2,
                                    &got_packet) >= 0) {
      if (got_packet) {
        write_video_frame(stream->ostream, &avpkt2);
      }
    }

    av_frame_free(&yframe);
    av_frame_free(&frame);
  } else {
    uint32_t mst = utils::currentms();
    if (stream->ts_fpackv_in_stream_msec == 0) {
      stream->ts_fpackv_in_stream_msec = mst;
    }

    AVPacket avpkt2 = {0};
    size_t sz = mat->cols * mat->rows;
    init_video_packet_ms(stream->ostream, mat->data, sz, mst - stream->ts_fpackv_in_stream_msec, &avpkt2);
    write_video_frame(stream->ostream, &avpkt2);
  }

  return SUCCESS_RESULT_VALUE;
}

int write_audio_frame_to_media_stream(media_stream_t *stream, uint8_t *data, size_t size) {
  if (!stream || !data) {
    return ERROR_RESULT_VALUE;
  }

  AVPacket avpkt2 = {0};
  init_audio_packet(stream->ostream, data, size, stream->sample_id, &avpkt2);
  stream->sample_id++;
  write_audio_frame(stream->ostream, &avpkt2);
  av_free_packet(&avpkt2);

  stream->audio_pcm_id++;
  return SUCCESS_RESULT_VALUE;
}

void free_video_stream(media_stream_t * stream) {
  if (!stream) {
    return;
  }

  if (stream->ostream) {
    uint32_t video_lenght_sec = stream->cur_ts_video_remote_msec/1000UL;
    int den = stream->ostream->video_stream->codec->time_base.den;
    debug_msg("For filepath %s:\n"
              "    VIDEO_REMOTE_MSEC %u, VIDEO_LOCAL_MSEC %u\n"
              "    VIDEO_FRAME_COUNT %"PRIu64", sps_pps %"PRIu64", must be nearly %u\n"
              "    AUDIO_FRAME_COUNT %"PRIu64" must be nearly %u\n"
              "    AUDIO_PCM_COUNT %"PRIu64"\n"
              "    VIDEO_LENGHT %u seconds\n"
              "    VIDEO_FIRST_FRAME %u msec, AUDIO_FIRST_FRAME %u msec\n",
              stream->ostream->oformat_context->filename,
              stream->cur_ts_video_remote_msec, stream->cur_ts_video_local_msec,
              stream->video_frame_id, stream->video_frame_sps_pps_id, video_lenght_sec * den,
              stream->sample_id, video_lenght_sec * 8,
              stream->audio_pcm_id,
              video_lenght_sec,
              stream->ts_fpackv_in_stream_msec, stream->ts_fpacka_in_stream_msec);

    AVFormatContext *formatContext = stream->ostream->oformat_context;
    int ret = av_write_trailer(formatContext);
    if (ret < 0) {
      debug_av_perror("av_write_trailer", ret);
    }
    close_output_stream(stream->ostream);
    free_output_stream(stream->ostream);
    stream->ostream = NULL;
  }
#if DUMP_MEDIA
  if (stream->media_dump) {
    fclose(stream->media_dump);
    stream->media_dump = NULL;
  }
  if (stream->only_mkf) {
    fclose(stream->only_mkf);
    stream->only_mkf = NULL;
  }
#endif

  stream->video_frame_id = 0;
  stream->video_frame_sps_pps_id = 0;
  stream->cur_ts_video_remote_msec = 0;
  stream->cur_ts_video_local_msec = 0;
  stream->ts_fpackv_in_stream_msec = 0;
  stream->ts_fpacka_in_stream_msec = 0;
  stream->sample_id = 0;

  if (stream->mkf_buffer) {
    free(stream->mkf_buffer);
    stream->mkf_buffer = NULL;
  }

  if (stream->nalu) {
    free_own_nal_unit(stream->nalu);
    stream->nalu = NULL;
  }
  stream->audio_pcm_id = 0;

  free(stream);
}

}  // namespace media
}  // namespace fasto
