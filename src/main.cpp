// Copyright (c) 2016 Alexandr Topilski. All rights reserved.

#include <opencv2/opencv.hpp>

int main(int argc, char *argv[]) {
  cv::VideoCapture cap(0); // open the default camera
  // cv::VideoCapture cap(url);
  if(!cap.isOpened())  // check if we succeeded
    return EXIT_FAILURE;

  int frame_width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
  int frame_height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);

  cv::VideoWriter video("out.avi", CV_FOURCC('M','J','P','G'), 10, cv::Size(frame_width, frame_height),true);

  for(;;){
       cv::Mat frame;
       cap >> frame;
       video.write(frame);
       cv::imshow( "Frame", frame );
       if(cv::waitKey(30) >= 0)
         break;
   }

  // the camera will be deinitialized automatically in VideoCapture destructor
  return EXIT_SUCCESS;
}
