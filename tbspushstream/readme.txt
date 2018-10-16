
1. useage   

    -v : video device number,eg: -v 0,1,2,3 
    -a : audio device number,eg: -a 1,2,3,4
    -r : Send TS to target IP by rtp,eg: -r 192.168.8.100:8888,192.168.8.101:8889,192.168.8.102:8890,192.168.8.103:8891
    -d : check  tool's version
    -h : output help infromation
   

2. example
	1> Collect audio and video, compress and encode into ts, and send the target ip through rtp
	 ./tbspushstream -v 0 -a 1 -r 192.168.8.100:8888
	 
3. This tool is precomile for ubuntu 16.04(32bit) and centos7.2(64bit) enviroment if you can not run it you can compile by yourself with these steps:

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