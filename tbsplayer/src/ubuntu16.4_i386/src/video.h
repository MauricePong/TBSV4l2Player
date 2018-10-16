#ifndef VIDEO_H
#define VIDEO_H
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef enum{false=0,true=!false} bool ;
typedef     signed char         int8_t;
typedef     signed short        int16_t;
typedef     signed int          int32_t;
typedef     unsigned char       uint8_t;
typedef     unsigned short      uint16_t;
typedef     unsigned int        uint32_t;
typedef unsigned long       DWORD;
typedef int                 BOOL;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef float               FLOAT;
typedef FLOAT               *PFLOAT;
typedef int                 INT;
typedef unsigned int        UINT;
typedef unsigned int        *PUINT;
typedef unsigned long ULONG_PTR, *PULONG_PTR;
typedef ULONG_PTR DWORD_PTR, *PDWORD_PTR;
#define MAKEWORD(a, b)      ((WORD)(((BYTE)(((DWORD_PTR)(a)) & 0xff)) | ((WORD)((BYTE)(((DWORD_PTR)(b)) & 0xff))) << 8))
#define MAKELONG(a, b)      ((LONG)(((WORD)(((DWORD_PTR)(a)) & 0xffff)) | ((DWORD)((WORD)(((DWORD_PTR)(b)) & 0xffff))) << 16))
#define LOWORD(l)           ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l)           ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w)           ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w)           ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define SDL_AUDIO_BUFFER_SIZE 1024
//4096
//1024
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio
#define FLUSH_DATA "FLUSH"
#define DEV_NO  4
#define VIDEO_PICTURE_QUEUE_SIZE 1
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio
#define MAX_AUDIO_SIZE (25 * 16 * 1024)
#define MAX_VIDEO_SIZE (25 * 256 * 1024)
#define msleep(n)    usleep(1000*(n))

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <asm/types.h>
#include <getopt.h>
#include "get_media_devices.h"

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/time.h"
#include "libavutil/pixfmt.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavdevice/avdevice.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_audio.h"
#include "SDL2/SDL_types.h"
#include "SDL2/SDL_name.h"
#include "SDL2/SDL_main.h"
#include "SDL2/SDL_config.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_thread.h"
#include "SDL2/SDL_events.h"

#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/avutil.h>

typedef struct MAINFLG{
    bool vid;
    bool aud;
    bool boa;
    bool cap;
    bool fil;
    bool play;
    bool rectype;
    bool rec;
    bool aut;
    bool manual;
    bool inf;
    bool enudev;
    bool ver;
    bool help;
} MAINFLG_t;


typedef struct MAINVAL{
    int  vid[DEV_NO];
    int  aud[DEV_NO];
    int  boa[DEV_NO];
    int  cap[DEV_NO];
    char fil[128];
    int  rectype;
    char rec[128];
    int  manual;
    int  devcount;
} MAINVAL_t;

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

typedef struct BufferDataNode
{
    uint8_t * buffer;
    int bufferSize;
    struct BufferDataNode * next;
} BufferDataNode;

typedef struct BufferQueue{
    BufferDataNode * DataQueneHead;
    BufferDataNode * DataQueneTail;
    SDL_mutex *Mutex;
} BufferQueue;

typedef struct VideoState {
    AVFormatContext *ic;
    AVFormatContext *ic_a;
    int videoStream, audioStream;
    AVFrame *audio_frame;// 解码音频过程中的使用缓存
    PacketQueue audioq;
    AVStream *audio_st; //音频流
    unsigned int audio_buf_size;
    unsigned int audio_buf_index;
    AVPacket audio_pkt;
    uint8_t *audio_pkt_data;
    int audio_pkt_size;
    uint8_t *audio_buf;
    DECLARE_ALIGNED(16,uint8_t,audio_buf2) [AVCODEC_MAX_AUDIO_FRAME_SIZE * 4];
    enum AVSampleFormat audio_src_fmt;
    enum AVSampleFormat audio_tgt_fmt;
    int audio_src_channels;
    int audio_tgt_channels;
    int64_t audio_src_channel_layout;
    int64_t audio_tgt_channel_layout;
    int audio_src_freq;
    int audio_tgt_freq;
    struct SwrContext *swr_ctx; //用于解码后的音频格式转换
    int audio_hw_buf_size;
    double audio_clock; ///音频时钟
    double video_clock; ///<pts of last decoded frame / predicted pts of next decoded frame
    AVStream *video_st;
    PacketQueue videoq;
    /// 跳转相关的变量
    ///播放控制相关
    //  bool quit;  //停止
    bool readFinished; //文件读取完毕
    bool readThreadFinished;
    bool videoThreadFinished;
    SDL_Thread *video_tid;  //视频线程id
    SDL_Thread *read_video_tid;
    SDL_Thread *recording_video_tid;
    SDL_Thread *encode_tid;
    SDL_Thread *wrvideo_tid;
    SDL_Thread* wraudio_tid;
    bool video_tid_flg;
    bool read_video_tid_flg;
    bool recording_video_tid_flg;
    bool encode_tid_flg;
    bool wrvideo_tid_flg;
    bool wraudio_tid_flg;
    FILE *yuv422fd;
    FILE *pcmfd;
    BufferQueue videobq;
    BufferQueue audiobq;
    BufferQueue videocpybq;
    BufferQueue audiocpybq;
    char recordname[128];
    char yuvname[128];
    char pcmname[128];
    int timebase_den;
    bool isMute; //静音标识
    float mVolume; //0~1 超过1 表示放大倍数
    SDL_AudioDeviceID mAudioID;
    SDL_Rect sdlRect;
    SDL_Renderer* sdlRenderer;
    SDL_Texture *sdlTexture;
    SDL_Window *screen;
    int windows;
    int no;
    int v;
    int a;
} VideoState;


typedef struct CAPARG{
    int v;
    int a;
    int no;
    int boa;
    int cap;
} CAPARG_t;

typedef struct GLOBALVAL{
    MAINVAL_t mainval;
    CAPARG_t caparg[DEV_NO];
    SDL_sem *m_lock;
    SDL_sem *m_vdlock;
    SDL_sem *m_rvlock;
    SDL_sem *m_relock;
    int quit;
    int rawdataflg;
    int Mode_i;
    AVFrame *picture;
    uint8_t *picture_buf;
    uint8_t *video_outbuf;
    int video_outbuf_size;
    float t, tincr, tincr2;
    int16_t *samples;
    uint8_t *audio_outbuf;
    int  audio_outbuf_size;
    int audio_input_frame_size;
    AVFilterContext *filter_buffer_ctx;
    AVFilterContext *filter_yadif_ctx;
    AVFilterContext *filter_buffersink_ctx;
    AVFilterGraph *filter_graph;
    int pi;

} GLOBALVAL_t;


extern GLOBALVAL_t gval;
extern VideoState  mVideoState[DEV_NO];
extern int init_filters(AVCodecContext *dec_ctx);
extern int openSDL(int no);
extern void closeSDL(int no);
extern int capturePlay(void);
extern int localPlay(void);
extern int recordPlay(void);
extern int showMediaInformation(void);
extern int autoAndmaual(int autoflg, int ma);
#endif
