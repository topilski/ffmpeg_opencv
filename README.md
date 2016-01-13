build ffmpeg:
./configure --enable-shared

build opencv:
http://answers.opencv.org/question/40425/opencv-build-fails-because-i-cannot-download-icv-on-our-build-farm/
http://www.linuxfromscratch.org/blfs/view/svn/general/opencv.html

/** @brief VideoWriter constructors

The constructors/functions initialize video writers. On Linux FFMPEG is used to write videos; on
Windows FFMPEG or VFW is used; on MacOSX QTKit is used.
 */
