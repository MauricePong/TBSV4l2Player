#include "video.h"
#undef main

static int strtoint(int *ia,char *ca)
{
    int i = 0;
    int j = 0;
    while('\0' != ca[i]){
        if((ca[i] < '0') || (ca[i] >'9')){
            if(',' == ca[i]){
                j++;
                ia[j] =  0;
            }
        }else{
            if((0 == j)&&(-1 == ia[j])){
                ia[j] = 0;
            }
            ia[j] *=10;
            ia[j] += (ca[i] - '0');
        }
        i++;
    }
    gval.mainval.devcount = j+1;
    //printf("dec:%d\n",gval.mainval.devcount);
    return 0;
}
static int enumAVDevice(void)
{
    //0. Get all video devices:
    //void *md = discover_media_devices();
    const char *devname = NULL;
    const char *vid = NULL;
    int i  = 0;
    void *md = discover_media_devices();
    do {
        vid = get_associated_device(md, vid, MEDIA_V4L_VIDEO,NULL, NONE);
        if (!vid)
            break;
        printf("v4l2 video: %s\n", vid);
        do {
            devname = get_associated_device(md, devname, MEDIA_SND_CAP,
                                            vid, MEDIA_V4L_VIDEO);
            if (devname){
                printf("alsa audio: %s\n", devname);
                //sscanf(devname,"hw:%d,0",&gval.hw[i]);
            }
        } while (devname);
        i++;
    } while (vid);
    free_media_devices(md);
    return i;
}

static const struct option long_options[] =
{
{ "videos",         required_argument, NULL, 'v' },
{ "audios",         required_argument, NULL, 'a' },
{ "board ",         required_argument, NULL, 'b' },
{ "capture",        required_argument, NULL, 'c' },
{ "file", 	        required_argument, NULL, 'f' },
{ "play", 	        no_argument,       NULL, 'p' },
{ "record type", 	required_argument, NULL, 't' },
{ "record", 	    required_argument, NULL, 'r' },
{ "Auto",           no_argument,       NULL, 'A' },
{ "Manual", 	    required_argument, NULL, 'M' },
{ "information", 	no_argument,       NULL, 'i' },
{ "enum device",    no_argument,       NULL, 'e' },
{ "version",        no_argument,       NULL, 'd' },
{ "help",           no_argument,       NULL, 'h' },
{ 0, 0, 0, 0 }
};


static void usage()
{
    printf("\n");
    printf("Usage:\n");
    printf("-v : video device number,eg: -v 0,1,2,3 \n");
    printf("-a : audio device number,eg: -a 1,2,3,4 \n");
    printf("-b : TBS board module,eg: -b 6314,6314,6314,6314 \n");
    printf("-c : TBS board capture number,eg: -c 0,1,2,3 \n");
    printf("-f : play the video file path(only support .flv, .mp4, .avi, .mp4 and .mov),eg: -f tbsvid.mp4\n");
    printf("-p : start play video\n");
    printf("-t : record video file format(only  support .mp4, .mov and rawdata),"
           " 0 is .mp4, 1 is .mov,2 is rawdata(.yuv and .pcm)\n");
    printf("-r : record video file path name,eg: -r  tbsvideo\n");
    printf("-A : auto encode mode\n");
    printf("-M : manual encode mode, 0 is enable p->i,1 is eable i->i, 2 is enable p->i invert, 3 is enable i->i invert,"
           "4 is disable p->i, 5 is disable i->i, 6 is disable p->i invert,7 is disable i->i invert\n");
    printf("-i : output configuration information\n");
    printf("-e : enum audio and video device\n");
    printf("-d : check  tbsplayer's version\n");
    printf("-h : output help infromation\n");
    printf("keyboard key: Esc is exiting the video'windows;F9~F12 are the full screen of the video window,"
           "corresponding to the video windows 0,1,2,3\n");
    printf("\n");
    exit(1);
}



int main(int i_argc, char **pp_argv )
{
    MAINFLG_t mflg;
    int c;
    char psz_tmp[128];
    int i = 0;
    int j = 0;
    memset(&mflg,0,sizeof(MAINFLG_t));
    memset(&gval,0,sizeof(GLOBALVAL_t));
    memset(gval.mainval.vid,-1,16*4);
    memset(gval.mainval.aud,-1,16*4);
    memset(gval.mainval.boa,-1,16*4);
    memset(gval.mainval.cap,-1,16*4);
    // init arg
    if(i_argc == 1)
        usage();
    while ( (c = getopt_long(i_argc, pp_argv, "v:a:b:c:f:pt:r:AM:W:H:iedh", long_options, NULL)) != -1 )
    {
        switch ( c )
        {
        case 'v':
            mflg.vid = 1;
            strtoint(gval.mainval.vid,optarg);
            break;
        case 'a':
            mflg.aud = 1;
            strtoint(gval.mainval.aud,optarg);
            break;
        case 'b':
            mflg.boa = 1;
            strtoint(gval.mainval.boa,optarg);
            break;
        case 'c':
            mflg.cap = 1;
            strtoint(gval.mainval.cap,optarg);
            break;
        case 'f':
            mflg.fil = 1;
            strcpy(gval.mainval.fil,optarg);
            break;
        case 'p':
            mflg.play = 1;
            break;
        case 't':
            mflg.rectype = 1;
            gval.mainval.rectype = strtol( optarg, NULL, 0 );
            break;
        case 'r':
            mflg.rec = 1;
            strcpy(gval.mainval.rec,optarg);
            break;
        case 'A':
            mflg.aut = 1;
            break;
        case 'M':
            mflg.manual = 1;
            gval.mainval.manual = strtol( optarg, NULL, 0 );
            break;
        case 'i':
            mflg.inf = 1;
            break;
        case 'e':
            mflg.enudev = 1;
            break;
        case 'd':
            mflg.ver = 1;
            break;
        case 'h':
            mflg.help = 1;
            break;
        default:
            usage();
            break;
        }
    }
    if(optind < i_argc )
        usage();
    if(1 == mflg.help){
        usage();
    }
    else if(1 == mflg.ver){
        printf("The tool'version is v.1.0.0.0\n");
        return 0;
    }
    else if(1 == mflg.enudev){
        enumAVDevice();
        return 0;
    }else if((1 == mflg.inf)&&(1 == mflg.vid)&&(1 == mflg.aud)&&(1== mflg.cap)&&(1 == mflg.boa)){
        av_register_all(); //初始化FFMPEG  调用了这个才能正常使用编码器和解码器
        avdevice_register_all();
        avformat_network_init(); //支持打开网络文件
        showMediaInformation();
        return 0;
    }else if((1 == mflg.aut)&&(1 == mflg.vid)&&(1== mflg.cap)&&(1 == mflg.boa)){
        autoAndmaual(1,0);
        printf("set Auto mode is ok\n");
        return 0;
    }else if((1 == mflg.manual)&&(1 == mflg.vid)&&(1== mflg.cap)&&(1 == mflg.boa)){
        autoAndmaual(0,gval.mainval.manual);
        printf("set Manual is ok\n");
        return 0;
    }
    if(1 == mflg.play){

        av_register_all(); //初始化FFMPEG  调用了这个才能正常使用编码器和解码器
        avdevice_register_all();
        avformat_network_init(); //支持打开网络文件
        avfilter_register_all();
        if (SDL_Init(SDL_INIT_AUDIO)) {
            fprintf(stderr,"Could not initialize SDL - %s. \n", SDL_GetError());
            exit(1);
        }
        if((1 == mflg.vid)&&(1 == mflg.aud)&&(1 == mflg.rec)&&(1 == mflg.rectype)){
            recordPlay();
        }else if((1 == mflg.vid)&&(1 == mflg.aud)){
            capturePlay();
        }else if(1 == mflg.fil){
            localPlay();
        }else{
            usage();
        }
    }else {
        usage();
    }
    return 0;
}

