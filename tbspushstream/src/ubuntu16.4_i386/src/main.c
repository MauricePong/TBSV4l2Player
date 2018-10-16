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
    return 0;
}


static int split_string(char *sa)
{
    int i = 0;
    int j = 0;
    int k = 0;

    //char *result = NULL;
    if(NULL == strstr(sa,",")){
        strcpy(gval.mainval.rtp[i],sa);
    }else{
        while(1){
            if('\0' == sa[i]){
                strcpy(gval.mainval.rtp[k],&sa[i-j+1]);
                break;
            }
            if(',' == sa[i]){
                if(k == 0){
                    memcpy(gval.mainval.rtp[k],&sa[i-j],j);
                }else{
                    memcpy(gval.mainval.rtp[k],&sa[i-j+1],j-1);
                }
                j = 0;
                k++;
            }
            j++;
            i++;
        }
    }
    return 0;
}

static const struct option long_options[] =
{
{ "videos",         required_argument, NULL, 'v' },
{ "audios",         required_argument, NULL, 'a' },
{ "rtp    ",        required_argument, NULL, 'r' },
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
    printf("-r : Send TS to target IP by rtp,"
           "eg: -r 192.168.8.100:8888,192.168.8.101:8889,192.168.8.102:8890,"
           "192.168.8.103:8891");
    printf("-d : check  tool's version\n");
    printf("-h : output help infromation\n");
    printf("eg : ./tbspushstream -v 0 -a 1 -r 192.168.8.100:8888\n");
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
    // init arg
    if(i_argc == 1)
        usage();
    while ( (c = getopt_long(i_argc, pp_argv, "v:a:r:dh", long_options, NULL)) != -1 )
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
        case 'r':
            mflg.rtp = 1;
            split_string(optarg);
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
    if((optind < i_argc )||(1 == mflg.help))
        usage();
    if(1 == mflg.ver){
        printf("The tool'version is v.1.0.0.0\n");
        return 0;
    }
    if((1 == mflg.rtp)
            &&(1 == mflg.vid)
            &&(1 == mflg.aud)){
        av_register_all(); //初始化FFMPEG  调用了这个才能正常使用编码器和解码器
        avdevice_register_all();
        avformat_network_init(); //支持打开网络文件
        avfilter_register_all();
        if (SDL_Init(SDL_INIT_AUDIO)) {
            fprintf(stderr,"Could not initialize SDL - %s. \n", SDL_GetError());
            exit(1);
        }
        rtp_sendTS();
    }else{
        usage();
    }
    return 0;
}

