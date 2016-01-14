// Copyright (c) 2016 Alexandr Topilski. All rights reserved.

#include <opencv2/opencv.hpp>
extern "C" {
#include <libavformat/avformat.h>
}

#include "media/media_stream_output.h"

#define BIT_PER_SAMPLE 2
#define AUDIO_CHANNELS 1
#define AUDIO_BITRATE 8000
#define AUDIO_SAMPLE_RATE 8000
#define AUDIO_BITRATE_OUT 48000

const char * outfilename = "out.mp4";

int main(int argc, char *argv[]) {
  av_register_all();

  cv::VideoCapture cap(0); // open the default camera
  if(!cap.isOpened())  // check if we succeeded
    return EXIT_FAILURE;

  int frame_width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
  int frame_height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);

  fasto::media::media_stream_params_t params;
  params.height_video = frame_height;
  params.width_video = frame_width;
  params.video_fps = 15;

  params.audio_bit_rate = AUDIO_BITRATE;
  params.audio_channels = AUDIO_CHANNELS;
  params.audio_sample_rate = AUDIO_SAMPLE_RATE;
  params.bit_per_sample = BIT_PER_SAMPLE;

  params.audio_bit_rate_out = AUDIO_BITRATE_OUT;
  params.audio_channels_out = AUDIO_CHANNELS;
  params.audio_sample_rate_out = AUDIO_SAMPLE_RATE;
  params.need_encode = true;  // encodeing and after that write to file

  fasto::media::media_stream_t* ostream = fasto::media::alloc_video_stream(outfilename, &params);
  if(!ostream){
      return EXIT_FAILURE;
  }

  for(;;){
       cv::Mat frame;
       cap >> frame;
       cv::imshow( "Frame", frame);
       fasto::media::write_video_frame_to_media_stream(ostream, &frame);
       if(cv::waitKey(30) >= 0)
         break;
  }

  fasto::media::free_video_stream(ostream);

  // the camera will be deinitialized automatically in VideoCapture destructor
  return EXIT_SUCCESS;
}
