
1. useage   

-v : video device number,eg: -v 0,1,2,3 
-a : audio device number,eg: -a 1,2,3,4
-b : TBS board module,eg: -b 6314,6314,6314,6314
-c : TBS board capture number,eg: -c 0,1,2,3 \n");
-f : play the video file path(only support .flv, .mp4, .avi, .mp4 and .mov),eg: -f tbsvid.mp4
-p : start play video
-t : record video file format(only  support .mp4, .mov and rawdata  0 is .mp4, 1 is .mov,2 is rawdata(.yuv and .pcm)
-r : record video file path name,eg: -r  tbsvideo
-A : auto encode mode
-M : manual encode mode, 0 is enable p->i,1 is eable i->i, 2 is enable p->i invert, 3 is enable i->i invert,4 is disable p->i, 5 is disable i->i, 6 is disable p->i invert,7 is disable i->i invert
-i : output configuration information
-e : enum video and audio device
-d : check  tbsplayer's version
-h : output help infromation
keyboard key: Esc is exiting the video'windows;F9~F12 are the full screen of the video window,corresponding to the video windows 0,1,2,3


2. example
	1> Play the audio and video of the capture card
		./tbsplayer -v 0 -a 1  -p
	
	2> Play audio and video files
		./tbsplayer -f tbsvid.mp4  -p
	
	3> Record the audio and video of the capture card and play it
		./tbsplayer -v 0 -a 1 -t 0 -r tbsvideo  -p
		
	4> set auto encode mode
		./tbsplayer -v 0 -a 1 -b 6314 -c 0 -A
	
	5> set manual encode mode
		./tbsplayer -v 0 -a 1 -b 6314 -c 0 -M 0
		
	6>  output configuration information
		./tbsplayer -v 0 -a 1 -b 6314 -c 0 -i

	7>  enum video and audio device
		./tbsplayer -e
		
3.This tool is precomile for ubuntu 16.04(32bit) and centos 7.2(64bit) enviroment if you can not run it you can compile by yourself with these steps:
/usr/include/linux/videodev2.h
add the following struct:
struct v4l2_tbs_data
{
	__u32 baseaddr;
	__u32 reg;
	__u32 value;
};

#define VIDIOC_TBS_G_CTL	_IOWR('V', 105, struct v4l2_tbs_data)
#define VIDIOC_TBS_S_CTL	_IOWR('V', 106, struct v4l2_tbs_data)

make

