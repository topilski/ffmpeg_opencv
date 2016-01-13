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

#if defined(PROTOBUF_ENABLED)
void write_video_frame_inner_from_proto(media_stream_t* stream,
                                        Media__VideoPacket__VideoFrame *video_frame) {
  if (!stream->nalu) {
    return;
  }

  int len = 0;
  uint32_t mst = currentms();
  if (stream->ts_fpackv_in_stream_msec == 0) {
    stream->ts_fpackv_in_stream_msec = mst;
  }

  uint64_t cur_msr = av_rescale(video_frame->presentationtimestamp->value, 1000,
                                video_frame->presentationtimestamp->timescale);
  uint8_t* key_frame = create_sps_pps_key_frame(stream->nalu, video_frame->payload.data,
                                                video_frame->payload.len, &len);
  int is_key_f = is_key_frame(video_frame->payload.data, video_frame->payload.len);

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
#endif

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
#ifdef WITH_OPUS
  stream->resampler = NULL;
  stream->dst_data = NULL;
  stream->audio_pcm_buff = NULL;
  stream->audio_buf_pcm_cur_size = 0;
#endif
  stream->ostream = alloc_output_stream_without_codec(path_to_save);
  if (stream->ostream) {
    debug_msg("Created output media file path: %s!\n", path_to_save);
    AVFormatContext *formatContext = stream->ostream->oformat_context;
    int res = add_video_stream_without_codec(stream->ostream, AV_CODEC_ID_H264,
                                             params->width_video, params->height_video,
                                             params->video_fps);
    if (res == ERROR_RESULT_VALUE) {
      debug_error("add_video_stream_without_codec failed!\n");
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
#ifdef WITH_OPUS
    if (stream->ostream->audio_stream) {
      stream->resampler = alloc_resampler(stream->ostream->audio_stream->codec,
                                         params->audio_sample_rate, AV_SAMPLE_FMT_S16,
                                         av_get_default_channel_layout(params->audio_channels),
                                         PCM_SAMPLES_COUNT);
      stream->audio_pcm_buff = reinterpret_cast<uint8_t *>(calloc(PCM_SAMPLES_COUNT * 4,
                                                                 sizeof(uint8_t)));
      if (!stream->audio_pcm_buff) {
        debug_error("audio_pcm_buff alocation failed!\n");
      }

      if (stream->resampler) {
        /* allocate source and destination samples buffers */
        if (resampler_alloc_array_and_samples(stream->resampler, &stream->dst_data) < 0) {
          debug_error("Could not allocate destination samples\n");
        }
      } else {
        debug_error("fe_resample_open: Can't init convertor\n");
      }
    }
#endif
  } else {
    debug_error("WARNING initiator output video stream with path %s not opened!", path_to_save);
  }

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

int write_video_frame_to_media_stream(media_stream_t * stream, uint8_t *data, int size, int mkf) {
  if (!stream || !data) {
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

  if (mkf) {
    // <packet size: 4b><offset: 4b><multiframe: 1b>
    uint32_t offset = 0;

    uint32_t packet_size = 0;
    memcpy(&packet_size, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    packet_size = __bswap_32(packet_size);

    uint32_t poffset = 0;
    memcpy(&poffset, data + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    poffset = __bswap_32(poffset);

    uint8_t mt = *(data + offset);

    uint8_t multi_frame = mt & 0xF;
    uint8_t total_packet = (mt >> 4) & 0xF;
    offset += sizeof(uint8_t);

    if (multi_frame == 0) {
      if (stream->mkf_buffer) {
        free(stream->mkf_buffer);
        stream->mkf_buffer = NULL;
      }

      stream->mkf_buffer = reinterpret_cast<uint8_t *>(calloc(packet_size, sizeof(uint8_t)));
      memcpy(stream->mkf_buffer + poffset, data + offset, size - offset);
      return SUCCESS_RESULT_VALUE;
    } else {
      if (!stream->mkf_buffer) {
        return ERROR_RESULT_VALUE;
      }

      memcpy(stream->mkf_buffer + poffset, data + offset, size - offset);
    }

    if (stream->mkf_buffer && total_packet - 1 == multi_frame) {
      uint32_t len = 0;
      header_enc_frame_t * header = alloc_header_enc_frame_from_string(stream->mkf_buffer, &len);
      DCHECK(header);
      if (header) {
        debug_msg("try to write mkf frame composed of %u packages, total size: %u\n",
                  total_packet, len);
        write_video_frame_inner(stream, header);
        free_header_enc_frame(header);
      }

      free(stream->mkf_buffer);
      stream->mkf_buffer = NULL;
    }

    return SUCCESS_RESULT_VALUE;
  }

  uint8_t c = *data;
  uint32_t len = 0;
  if (c == OWN_NAL_UNIT_TYPE) {
    if (stream->nalu) {
      free_own_nal_unit(stream->nalu);
      stream->nalu = NULL;
    }
    stream->nalu = alloc_own_nal_unit_from_string(data, &len);
    DCHECK(stream->nalu);
    DCHECK(len == size);
    stream->video_frame_sps_pps_id++;
  } else if (c == OWN_FRAME_TYPE) {
    header_enc_frame_t * header = alloc_header_enc_frame_from_string(data, &len);
    DCHECK(header);
    if (header) {
      write_video_frame_inner(stream, header);
      free_header_enc_frame(header);
    }
  } else {
    debug_msg("UNKNOWN PACKET TYPE: %d", c);
    NOTREACHED();
  }

  return SUCCESS_RESULT_VALUE;
}

#if defined(PROTOBUF_ENABLED)
int write_proto_video_frame_to_media_stream(media_stream_t* stream,
                                            Media__VideoPacket* video_packet) {
  if (!stream || !video_packet) {
    return ERROR_RESULT_VALUE;
  }

  if (video_packet->formatdescription != NULL) {
    if (stream->nalu) {
      free_own_nal_unit(stream->nalu);
      stream->nalu = NULL;
    }
    stream->nalu = alloc_own_nal_unit_from_proto(video_packet->formatdescription);
    DCHECK(stream->nalu);
    stream->video_frame_sps_pps_id++;
  }
  if (video_packet->videoframe != NULL) {
    write_video_frame_inner_from_proto(stream, video_packet->videoframe);
  }

  return SUCCESS_RESULT_VALUE;
}
#endif

int write_audio_frame_to_media_stream(media_stream_t *stream, uint8_t *data, int size) {
  if (!stream || !data) {
    return ERROR_RESULT_VALUE;
  }
#ifdef WITH_OPUS
  if (!stream->resampler) {
    return ERROR_RESULT_VALUE;
  }

  uint32_t mst = utils::currentms();
  if (stream->ts_fpacka_in_stream_msec == 0) {
    stream->ts_fpacka_in_stream_msec = mst;
  }

  if (stream->audio_buf_pcm_cur_size >= PCM_SAMPLES_COUNT * 2) {  // ready for resampling
    uint8_t *buff = reinterpret_cast<uint8_t*>(calloc(PCM_SAMPLES_COUNT * 2, sizeof(uint8_t)));
    if (buff) {
      memcpy(buff, stream->audio_pcm_buff, PCM_SAMPLES_COUNT * 2);

      int ret = resampler_convert(stream->resampler, stream->dst_data,
                                  stream->resampler->dst_nb_samples,
                                 (const uint8_t **)&buff, stream->resampler->src_nb_samples);
      if (ret < 0) {
        debug_error("Error while resamle converting!");
      } else {
        DCHECK(ret == PCM_SAMPLES_COUNT);
        stream->ostream->auduo_frame_buffer->nb_samples = ret;
        AVPacket avpkt2 = {0};
        av_init_packet(&avpkt2);
        int got_packet = 0;
        if (encode_ostream_audio_buffer(stream->ostream, *stream->dst_data,
                                        stream->resampler->dst_linesize, &avpkt2,
                                        &got_packet) >= 0) {
          if (got_packet) {
            uint64_t t_diff = stream->cur_ts_video_remote_msec;
            uint64_t must_be_samples = t_diff / 125;
            int diff = must_be_samples - stream->sample_id;
            write_empty_audio_samples(stream, diff);
            update_audio_packet_pts(stream->ostream, stream->sample_id, &avpkt2);
            stream->sample_id++;
            write_audio_frame(stream->ostream, &avpkt2);
          }
        }
        av_free_packet(&avpkt2);
      }
        free(buff);
    } else {
      debug_error("buff alocation failed!\n");
    }

    int diff = stream->audio_buf_pcm_cur_size - PCM_SAMPLES_COUNT * 2;
    memmove(stream->audio_pcm_buff, stream->audio_pcm_buff + PCM_SAMPLES_COUNT * 2, diff);
    stream->audio_buf_pcm_cur_size = diff;
  }

  stream->audio_pcm_id++;

  memcpy(stream->audio_pcm_buff + stream->audio_buf_pcm_cur_size, data, size);
  stream->audio_buf_pcm_cur_size += size;
#else
  AVPacket avpkt2 = {0};
  init_audio_packet(stream->ostream, data, size, stream->sample_id, &avpkt2);
  stream->sample_id++;
  write_audio_frame(stream->ostream, &avpkt2);
  av_free_packet(&avpkt2);

  stream->audio_pcm_id++;
#endif
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
#ifdef WITH_OPUS
  if (stream->resampler) {
    free_resampler(stream->resampler);
    stream->resampler = NULL;
  }
  if (stream->dst_data) {
    free(*stream->dst_data);
    free(stream->dst_data);
    stream->dst_data = NULL;
  }
  if (stream->audio_pcm_buff) {
    free(stream->audio_pcm_buff);
    stream->audio_pcm_buff = NULL;
  }
  stream->audio_buf_pcm_cur_size = 0;
#endif
  stream->audio_pcm_id = 0;

  free(stream);
}

}  // namespace media
}  // namespace fasto
