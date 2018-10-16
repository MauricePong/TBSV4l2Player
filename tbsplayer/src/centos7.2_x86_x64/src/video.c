#include "video.h"
GLOBALVAL_t gval;
VideoState  mVideoState[DEV_NO];

static void DataQuene_Input(BufferQueue *bq,uint8_t * buffer,int size)
{
    BufferDataNode * node = (BufferDataNode*)malloc(sizeof(BufferDataNode));
    node->buffer = (uint8_t *)malloc(size);
    node->bufferSize = size;
    node->next = NULL;

    memcpy(node->buffer,buffer,size);
    SDL_LockMutex(bq->Mutex);
    if (bq->DataQueneHead == NULL)
    {
        bq->DataQueneHead = node;
    }
    else
    {
        bq->DataQueneTail->next = node;
    }
    bq->DataQueneTail = node;
    SDL_UnlockMutex(bq->Mutex);
}

static BufferDataNode *DataQuene_get(BufferQueue *bq)
{
    BufferDataNode * node = NULL;
    SDL_LockMutex(bq->Mutex);
    if (bq->DataQueneHead != NULL)
    {
        node = bq->DataQueneHead;
        if (bq->DataQueneTail == bq->DataQueneHead)
        {
            bq->DataQueneTail = NULL;
        }
        bq->DataQueneHead = bq->DataQueneHead->next;
    }
    SDL_UnlockMutex(bq->Mutex);
    return node;
}

static void buffer_queue_flush(BufferQueue *bq)
{
    BufferDataNode *bnode;
    while(1){
        bnode = DataQuene_get(bq);
        if(NULL == bnode){
            break;
        }
        free(bnode->buffer);
        free(bnode);
        bnode = NULL;
    }
}

static void buffer_queue_deinit(BufferQueue *bq) {
    buffer_queue_flush(bq);
    SDL_DestroyMutex(bq->Mutex);
}

void buffer_queue_init(BufferQueue *bq) {
    memset(bq, 0, sizeof(BufferQueue));
    bq->Mutex = SDL_CreateMutex();
    bq->DataQueneHead = NULL;
    bq->DataQueneTail = NULL;
}

static void packet_queue_flush(PacketQueue *q)
{
    AVPacketList *pkt, *pkt1;

    SDL_LockMutex(q->mutex);
    for(pkt = q->first_pkt; pkt != NULL; pkt = pkt1)
    {
        pkt1 = pkt->next;

        if(pkt1->pkt.data != (uint8_t *)"FLUSH")
        {

        }
        av_free_packet(&pkt->pkt);
        av_freep(&pkt);

    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    SDL_UnlockMutex(q->mutex);
}

static void packet_queue_deinit(PacketQueue *q) {
    packet_queue_flush(q);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

void packet_queue_init(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
    q->size = 0;
    q->nb_packets = 0;
    q->first_pkt = NULL;
    q->last_pkt = NULL;
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

    AVPacketList *pkt1;
    if (av_dup_packet(pkt) < 0) {
        return -1;
    }
    pkt1 = (AVPacketList*)av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
    return 0;
}

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;) {

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }

    }

    SDL_UnlockMutex(q->mutex);
    return ret;
}


int audio_stream_component_open(VideoState *is, int stream_index)
{
    AVFormatContext *ic = is->ic_a;
    AVCodecContext *codecCtx;
    AVCodec *codec;

    int64_t wanted_channel_layout = 0;
    int wanted_nb_channels;

    if (stream_index < 0 || stream_index >= ic->nb_streams) {
        return -1;
    }

    codecCtx = ic->streams[stream_index]->codec;
    wanted_nb_channels = codecCtx->channels;
    if (!wanted_channel_layout
            || wanted_nb_channels
            != av_get_channel_layout_nb_channels(
                wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(
                    wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }

    /* 把设置好的参数保存到大结构中 */
    is->audio_src_fmt = is->audio_tgt_fmt = AV_SAMPLE_FMT_S16;
    is->audio_src_freq = is->audio_tgt_freq = 44100;
    is->audio_src_channel_layout = is->audio_tgt_channel_layout =
            wanted_channel_layout;
    is->audio_src_channels = is->audio_tgt_channels = 2;

    codec = avcodec_find_decoder(codecCtx->codec_id);
    if (!codec || (avcodec_open2(codecCtx, codec, NULL) < 0)) {
        fprintf(stderr,"Unsupported codec!\n");
        return -1;
    }
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
    switch (codecCtx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        //        is->audioStream = stream_index;
        is->audio_st = ic->streams[stream_index];
        is->audio_buf_size = 0;
        is->audio_buf_index = 0;
        memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
        //        packet_queue_init(&is->audioq);
        break;
    default:
        break;
    }

    return 0;
}
void RaiseVolume(char* buf, int size, int uRepeat, double vol)
//buf为需要调节音量的音频数据块首地址指针，size为长度，uRepeat为重复次数，通常设为1，vol为增益倍数,可以小于1
{
    int i, j;
    if (!size)
    {
        return;
    }
    for ( i = 0; i < size; i += 2)
    {
        short wData;
        wData = MAKEWORD(buf[i], buf[i + 1]);
        long dwData = wData;
        for ( j = 0; j < uRepeat; j++)
        {
            dwData = dwData * vol;
            if (dwData < -0x8000)
            {
                dwData = -0x8000;
            }
            else if (dwData > 0x7FFF)
            {
                dwData = 0x7FFF;
            }
        }
        wData = LOWORD(dwData);
        buf[i] = LOBYTE(wData);
        buf[i + 1] = HIBYTE(wData);
    }
}

static int audio_decode_frame(VideoState *is, double *pts_ptr)
{
    int len1, len2, decoded_data_size;
    AVPacket *pkt = &is->audio_pkt;
    int got_frame = 0;
    int64_t dec_channel_layout;
    int wanted_nb_samples, resampled_data_size, n;
    double pts;
    for (;;) {
        while (is->audio_pkt_size > 0) {

            if (!is->audio_frame) {
                if (!(is->audio_frame = avcodec_alloc_frame())) {
                    return AVERROR(ENOMEM);
                }
            } else
                avcodec_get_frame_defaults(is->audio_frame);
            len1 = avcodec_decode_audio4(is->audio_st->codec, is->audio_frame,
                                         &got_frame, pkt);
            if (len1 < 0) {
                // error, skip the frame
                is->audio_pkt_size = 0;
                break;
            }
            is->audio_pkt_data += len1;
            is->audio_pkt_size -= len1;
            if (!got_frame){
                SDL_Delay(10);
                continue;
            }
            /* 计算解码出来的桢需要的缓冲大小 */
            decoded_data_size = av_samples_get_buffer_size(NULL,
                                                           is->audio_frame->channels,
                                                           is->audio_frame->nb_samples,
                                                           (enum AVSampleFormat)(is->audio_frame->format),
                                                           1);
            dec_channel_layout =
                    (is->audio_frame->channel_layout
                     && is->audio_frame->channels
                     == av_get_channel_layout_nb_channels(
                         is->audio_frame->channel_layout)) ?
                        is->audio_frame->channel_layout :
                        av_get_default_channel_layout(
                            is->audio_frame->channels);
            wanted_nb_samples = is->audio_frame->nb_samples;
            if (is->audio_frame->format != is->audio_src_fmt
                    || dec_channel_layout != is->audio_src_channel_layout
                    || is->audio_frame->sample_rate != is->audio_src_freq
                    || (wanted_nb_samples != is->audio_frame->nb_samples
                        && !is->swr_ctx)) {
                if (is->swr_ctx)
                    swr_free(&is->swr_ctx);
                is->swr_ctx = swr_alloc_set_opts(NULL,
                                                 is->audio_tgt_channel_layout,
                                                 (enum AVSampleFormat)is->audio_tgt_fmt,
                                                 is->audio_tgt_freq, dec_channel_layout,
                                                 (enum AVSampleFormat)is->audio_frame->format,
                                                 is->audio_frame->sample_rate,
                                                 0, NULL);
                if (!is->swr_ctx || swr_init(is->swr_ctx) < 0) {
                    fprintf(stderr,"swr_init() failed\n");
                    break;
                }
                is->audio_src_channel_layout = dec_channel_layout;
                is->audio_src_channels = is->audio_st->codec->channels;
                is->audio_src_freq = is->audio_st->codec->sample_rate;
                is->audio_src_fmt = is->audio_st->codec->sample_fmt;
            }

            /* 这里我们可以对采样数进行调整，增加或者减少，一般可以用来做声画同步 */
            if (is->swr_ctx) {
                const uint8_t **in =
                        (const uint8_t **) is->audio_frame->extended_data;
                uint8_t *out[] = { is->audio_buf2 };
                if (wanted_nb_samples != is->audio_frame->nb_samples) {
                    if (swr_set_compensation(is->swr_ctx,
                                             (wanted_nb_samples - is->audio_frame->nb_samples)
                                             * is->audio_tgt_freq
                                             / is->audio_frame->sample_rate,
                                             wanted_nb_samples * is->audio_tgt_freq
                                             / is->audio_frame->sample_rate) < 0) {
                        fprintf(stderr,"swr_set_compensation() failed\n");
                        break;
                    }
                }
                len2 = swr_convert(is->swr_ctx, out,
                                   sizeof(is->audio_buf2) / is->audio_tgt_channels
                                   / av_get_bytes_per_sample(is->audio_tgt_fmt),
                                   in, is->audio_frame->nb_samples);
                if (len2 < 0) {
                    fprintf(stderr,"swr_convert() failed\n");
                    break;
                }
                if (len2 == sizeof(is->audio_buf2) / is->audio_tgt_channels
                        / av_get_bytes_per_sample(is->audio_tgt_fmt)) {
                    fprintf(stderr,"warning: audio buffer is probably too small\n");
                    swr_init(is->swr_ctx);
                }
                is->audio_buf = is->audio_buf2;
                resampled_data_size = len2 * is->audio_tgt_channels
                        * av_get_bytes_per_sample(is->audio_tgt_fmt);
            } else {
                resampled_data_size = decoded_data_size;
                is->audio_buf = is->audio_frame->data[0];
            }

            pts = is->audio_clock;
            *pts_ptr = pts;
            n = 2 * is->audio_st->codec->channels;
            is->audio_clock += (double) resampled_data_size
                    / (double) (n * is->audio_st->codec->sample_rate);
            // We have data, return it and come back for more later
            //           printf("resampled_data_size:%d\n",resampled_data_size);
            return resampled_data_size;
        }
        if (pkt->data)
            av_free_packet(pkt);
        memset(pkt, 0, sizeof(*pkt));
        if (gval.quit)
        {
            packet_queue_flush(&is->audioq);
            return -1;
        }
        if (packet_queue_get(&is->audioq, pkt, 0) <= 0)
        {
            //return -1;
            SDL_Delay(10);
            continue;
        }
        //收到这个数据 说明刚刚执行过跳转 现在需要把解码器的数据 清除一下
        if(strcmp((char*)pkt->data,FLUSH_DATA) == 0)
        {
            avcodec_flush_buffers(is->audio_st->codec);
            av_free_packet(pkt);
            SDL_Delay(10);
            continue;
        }
        is->audio_pkt_data = pkt->data;
        is->audio_pkt_size = pkt->size;

        /* if update, update the audio clock w/pts */
        if (pkt->pts != AV_NOPTS_VALUE) {
            is->audio_clock = av_q2d(is->audio_st->time_base) * pkt->pts;
        }

        if( 3 == gval.Mode_i){
            if(0 == gval.rawdataflg){
                DataQuene_Input(&is->audiobq,pkt->data,pkt->size);
            }else if(1 == gval.rawdataflg){
                DataQuene_Input(&is->audiocpybq,pkt->data,pkt->size);
            }
        }
        // printf("audio_decode_frame (for(;;))\n");
    }
    printf("audio_decode_frame end\n");
    return -1;
}
static void audio_callback(void *userdata, Uint8 *stream, int len)
{

    VideoState *is = (VideoState *) userdata;
    int len1, audio_data_size;
    double pts;
    /*   len是由SDL传入的SDL缓冲区的大小，如果这个缓冲未满，我们就一直往里填充数据 */
    while (len > 0) {
        /*  audio_buf_index 和 audio_buf_size 标示我们自己用来放置解码出来的数据的缓冲区，*/
        /*   这些数据待copy到SDL缓冲区， 当audio_buf_index >= audio_buf_size的时候意味着我*/
        /*   们的缓冲为空，没有数据可供copy，这时候需要调用audio_decode_frame来解码出更
         /*   多的桢数据 */
        //        qDebug()<<__FUNCTION__<<is->audio_buf_index<<is->audio_buf_size;
        if (is->audio_buf_index >= is->audio_buf_size) {
            audio_data_size = audio_decode_frame(is, &pts);
            /* audio_data_size < 0 标示没能解码出数据，我们默认播放静音 */
            if (audio_data_size < 0) {
                //                printf("audio_callback: audio_data_size<0\n");
                /* silence */
                is->audio_buf_size = 1024;
                /* 清零，静音 */
                if (is->audio_buf == NULL) return;
                memset(is->audio_buf, 0, is->audio_buf_size);
            } else {
                is->audio_buf_size = audio_data_size;
            }
            is->audio_buf_index = 0;
        }
        /*  查看stream可用空间，决定一次copy多少数据，剩下的下次继续copy */
        len1 = is->audio_buf_size - is->audio_buf_index;
        if (len1 > len) {
            len1 = len;
        }

        if (is->audio_buf == NULL) return;

        if (is->isMute) //静音 或者 是在暂停的时候跳转了
        {
            memset(is->audio_buf + is->audio_buf_index, 0, len1);
        }
        else
        {
            RaiseVolume((char*)is->audio_buf + is->audio_buf_index, len1, 1, is->mVolume);
        }

        memcpy(stream, (uint8_t *) is->audio_buf + is->audio_buf_index, len1);
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
        // printf("audio_callback:len = %d\n",len);
    }
    //    printf("audio_callback:end\n");
}

static double get_audio_clock(VideoState *is)
{
    double pts;
    int hw_buf_size, bytes_per_sec, n;

    pts = is->audio_clock; /* maintained in the audio thread */
    hw_buf_size = is->audio_buf_size - is->audio_buf_index;
    bytes_per_sec = 0;
    n = is->audio_st->codec->channels * 2;
    if(is->audio_st)
    {
        bytes_per_sec = is->audio_st->codec->sample_rate * n;
    }
    if(bytes_per_sec)
    {
        pts -= (double)hw_buf_size / bytes_per_sec;
    }
    return pts;
}

static double synchronize_video(VideoState *is, AVFrame *src_frame, double pts) {

    double frame_delay;

    if (pts != 0) {
        /* if we have pts, set video clock to it */
        is->video_clock = pts;
    } else {
        /* if we aren't given a pts, set it to the clock */
        pts = is->video_clock;
    }
    /* update the video clock */
    frame_delay = av_q2d(is->video_st->codec->time_base);
    /* if we are repeating a frame, adjust clock accordingly */
    frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
    is->video_clock += frame_delay;
    return pts;
}

static int sdlPlayerInit()
{
    //init SDL windows////////////////////////////
    if(SDL_Init(SDL_INIT_VIDEO| SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        printf( " initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    //SDL 2.0 Support for multiple windows
    // gval.screen = SDL_CreateWindow("TBSPlayer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
    //    gval.screen = SDL_CreateWindow("TBSPlayer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
    //                                   gval.mainval.w, gval.mainval.h,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    //    if(!gval.screen) {
    //        printf("SDL: could not create window - exiting:%s\n",SDL_GetError());
    //        return -1;
    //    }
    // gval.sdlRenderer = SDL_CreateRenderer(gval.screen, -1, 0);
    // Uint32 pixformat= SDL_PIXELFORMAT_UYVY;
    //gval.sdlTexture = SDL_CreateTexture(gval.sdlRenderer,pixformat, SDL_TEXTUREACCESS_STREAMING,
    //                                  gval.mainval.w, gval.mainval.h);
}


int openSDL(int no)
{
    VideoState* is = &mVideoState[no];
    SDL_AudioSpec wanted_spec, spec;
    int64_t wanted_channel_layout = 0;
    int wanted_nb_channels = 2;
    // int samplerate = 44100;
    int samplerate = is->audio_st->codec->sample_rate;
    int i  = 0;
    /*  SDL支持的声道数为 1, 2, 4, 6 */
    //    /*  后面我们会使用这个数组来纠正不支持的声道数目 */
    //    const int next_nb_channels[] = { 0, 0, 1, 6, 2, 6, 4, 6 };

    if (!wanted_channel_layout
            || wanted_nb_channels
            != av_get_channel_layout_nb_channels(
                wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(
                    wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_spec.channels = av_get_channel_layout_nb_channels(
                wanted_channel_layout);
    wanted_spec.freq = samplerate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        //fprintf(stderr,"Invalid sample rate or channel count!\n");
        return -1;
    }
    wanted_spec.format = AUDIO_S16SYS; // 具体含义请查看“SDL宏定义”部分
    wanted_spec.silence = 0;            // 0指示静音
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;  // 自定义SDL缓冲区大小
    wanted_spec.callback = audio_callback;        // 音频解码的关键回调函数
    wanted_spec.userdata = is;                    // 传给上面回调函数的外带数据

    int num = SDL_GetNumAudioDevices(0);
    for (i =0;i<num;i++)
    {
        is->mAudioID = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(i,0), false, &wanted_spec, &spec,0);
        if (is->mAudioID > 0)
        {
            break;
        }
    }

    /* 检查实际使用的配置（保存在spec,由SDL_OpenAudio()填充） */
    if (spec.format != AUDIO_S16SYS) {
        printf("SDL advised audio format %#x is not supported!\n",spec.format);
        return -1;
    }
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
            fprintf(stderr,"SDL advised channel count %d is not supported!\n",spec.channels);
            return -1;
        }
    }
    is->audio_hw_buf_size = spec.size;
    /* 把设置好的参数保存到大结构中 */
    is->audio_src_fmt = is->audio_tgt_fmt = AV_SAMPLE_FMT_S16;
    is->audio_src_freq = is->audio_tgt_freq = spec.freq;
    is->audio_src_channel_layout = is->audio_tgt_channel_layout =
            wanted_channel_layout;
    is->audio_src_channels = is->audio_tgt_channels = spec.channels;
    is->audio_buf_size = 0;
    is->audio_buf_index = 0;
    memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
    return 0;
}

void closeSDL(int no)
{
    if (mVideoState[no].mAudioID > 0)
    {
        SDL_CloseAudioDevice(mVideoState[no].mAudioID);
    }

    mVideoState[no].mAudioID = -1;
}
int keydone_thread(void *arg)
{
    int i = 0;
    SDL_Event myEvent;
    while(!gval.quit){
        // while(SDL_PollEvent(&myEvent)) {
        while(SDL_WaitEvent(&myEvent)) {
            switch (myEvent.type) {
            case SDL_KEYDOWN:
                if(myEvent.key.keysym.sym == SDLK_ESCAPE){

                    for(i = 0; i < gval.mainval.devcount;i++){
                        SDL_DestroyTexture(mVideoState[i].sdlTexture);
                        SDL_DestroyRenderer(mVideoState[i].sdlRenderer);
                        SDL_DestroyWindow(mVideoState[i].screen);
                    }
                    SDL_Quit();
                    gval.quit = 1;
                }else if(myEvent.key.keysym.sym == SDLK_F9){
                    if(1 ==  mVideoState[0].windows){
                        SDL_SetWindowFullscreen(mVideoState[0].screen,1);
                        mVideoState[0].windows = 0;
                    }else if(0 == mVideoState[0].windows){
                        SDL_SetWindowFullscreen(mVideoState[0].screen,0);
                        mVideoState[0].windows = 1;
                    }
                }
                else if(myEvent.key.keysym.sym == SDLK_F10){
                    if(1 ==  mVideoState[1].windows){
                        SDL_SetWindowFullscreen(mVideoState[1].screen,1);
                        mVideoState[1].windows = 0;
                    }else if(0 == mVideoState[1].windows){
                        SDL_SetWindowFullscreen(mVideoState[1].screen,0);
                        mVideoState[1].windows = 1;
                    }
                }
                else if(myEvent.key.keysym.sym == SDLK_F11){
                    if(1 ==  mVideoState[2].windows){
                        SDL_SetWindowFullscreen(mVideoState[2].screen,1);
                        mVideoState[2].windows = 0;
                    }else if(0 == mVideoState[2].windows){
                        SDL_SetWindowFullscreen(mVideoState[2].screen,0);
                        mVideoState[2].windows = 1;
                    }
                }
                else if(myEvent.key.keysym.sym == SDLK_F12){
                    if(1 ==  mVideoState[3].windows){
                        SDL_SetWindowFullscreen(mVideoState[3].screen,1);
                        mVideoState[3].windows = 0;
                    }else if(0 == mVideoState[3].windows){
                        SDL_SetWindowFullscreen(mVideoState[3].screen,0);
                        mVideoState[3].windows = 1;
                    }
                }
                break;
            case SDL_QUIT:
                for(i = 0; i < gval.mainval.devcount;i++){
                    SDL_DestroyTexture(mVideoState[i].sdlTexture);
                    SDL_DestroyRenderer(mVideoState[i].sdlRenderer);
                    SDL_DestroyWindow(mVideoState[i].screen);
                }
                SDL_Quit();
                gval.quit = 1;
                break;
            default:
                break;
            }
            SDL_Delay(10);
        }
        SDL_Delay(10);
    }
    gval.quit = 1;
    return 0;
}

int local_video_thread(void *arg)
{
    SDL_SemWait(gval.m_vdlock);
    printf("local_video_thread\n");
    VideoState *is = (VideoState *) arg;
    is->video_tid_flg = 0;
    AVPacket pkt1, *packet = &pkt1;
    int ret, got_picture, numBytes;
    double video_pts = 0; //当前视频的pts
    double audio_pts = 0; //音频pts
    int i = 0;
    int w,h = 0;
    ///解码视频相关
    AVFrame *pFrame, *pFrameyuv;
    uint8_t *out_buffer_yuv; //解码后的rgb数据
    struct SwsContext *img_convert_ctx;  //用于解码后的视频格式转换
    AVCodecContext *pCodecCtx = is->video_st->codec; //视频解码器
    pFrame = av_frame_alloc();
    pFrameyuv = av_frame_alloc();

    ///这里我们改成了 将解码后的YUV数据转换成RGB32
    img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                                     pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
                                     AV_PIX_FMT_UYVY422, SWS_BICUBIC, NULL, NULL, NULL);
    numBytes = avpicture_get_size(AV_PIX_FMT_UYVY422, pCodecCtx->width,pCodecCtx->height);
    out_buffer_yuv = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    avpicture_fill((AVPicture *) pFrameyuv, out_buffer_yuv, AV_PIX_FMT_UYVY422,
                   pCodecCtx->width, pCodecCtx->height);

    char titile[36] = {'\0'};
    // sprintf(titile,"/dev/video%d,hw:%d,0",is->v,is->a);
    strcpy(titile,gval.mainval.fil);
    is->screen = SDL_CreateWindow(titile, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  pCodecCtx->width, pCodecCtx->height,SDL_WINDOW_OPENGL|
                                  SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    if(!is->screen) {
        printf("SDL: could not create window - exiting:%s\n",SDL_GetError());
        return -1;
    }
    is->windows = 1;
    is->sdlRenderer = SDL_CreateRenderer(is->screen, -1, 0);
    SDL_SetRenderDrawColor(is->sdlRenderer,0,0,0,255);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
    Uint32 pixformat= SDL_PIXELFORMAT_UYVY;
    is->sdlTexture = SDL_CreateTexture(is->sdlRenderer,pixformat, SDL_TEXTUREACCESS_STREAMING,
                                       pCodecCtx->width, pCodecCtx->height);
    while(1)
    {
        if (gval.quit)
        {
            printf("%s quit \n",__FUNCTION__);
            packet_queue_flush(&is->videoq); //清空队列
            break;
        }

        //qDebug()<<__FUNCTION__<<"000!";
        if (packet_queue_get(&is->videoq, packet, 0) <= 0)
        {
            if (is->readFinished)
            {//队列里面没有数据了且读取完毕了
                break;
            }
            else
            {
                //qDebug("222222222222");
                SDL_Delay(10); //队列只是暂时没有数据而已
                continue;
            }
        }

        //收到这个数据 说明刚刚执行过跳转 现在需要把解码器的数据 清除一下
        if(strcmp((char*)packet->data,FLUSH_DATA) == 0)
        {
            avcodec_flush_buffers(is->video_st->codec);
            av_free_packet(packet);
            SDL_Delay(10);
            continue;
        }
        ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture,packet);
        if (ret < 0) {
            printf("decode error.\n");
            av_free_packet(packet);
            SDL_Delay(10);
            continue;
        }
        if (packet->dts == AV_NOPTS_VALUE && pFrame->opaque&& *(uint64_t*) pFrame->opaque != AV_NOPTS_VALUE)
        {
            video_pts = *(uint64_t *) pFrame->opaque;
        }
        else if (packet->dts != AV_NOPTS_VALUE)
        {
            video_pts = packet->dts;
        }
        else
        {
            video_pts = 0;
        }
        video_pts *= av_q2d(is->video_st->time_base);
        video_pts = synchronize_video(is, pFrame, video_pts);

        while(1)
        {
            if (gval.quit)
            {
                break;
            }

            if (is->readFinished && is->audioq.size == 0)
            {//读取完了 且音频数据也播放完了 就剩下视频数据了  直接显示出来了 不用同步了
                break;
            }
            audio_pts = is->audio_clock;
            //主要是 跳转的时候 我们把video_clock设置成0了
            //因此这里需要更新video_pts
            //否则当从后面跳转到前面的时候 会卡在这里
            video_pts = is->video_clock;
            //qDebug()<<__FUNCTION__<<video_pts<<audio_pts;
            if (video_pts <= audio_pts) break;
            int delayTime = (video_pts - audio_pts) * 1000;
            delayTime = delayTime > 5 ? 5:delayTime;
            SDL_Delay(delayTime);
        }
        if (got_picture) {
            sws_scale(img_convert_ctx,
                      (uint8_t const * const *) pFrame->data,
                      pFrame->linesize, 0, pCodecCtx->height, pFrameyuv->data,
                      pFrameyuv->linesize);
            SDL_UpdateTexture( is->sdlTexture, NULL,out_buffer_yuv, (pCodecCtx->width*2));
            //FIX: If window is resize
            SDL_GetWindowSize(is->screen,&w,&h);

            is->sdlRect.x = 0;
            is->sdlRect.y = 0;
            is->sdlRect.w = w;//gval.mainval.devcount;
            is->sdlRect.h = h;

            SDL_RenderClear(is->sdlRenderer );
            SDL_RenderCopy(is->sdlRenderer, is->sdlTexture, NULL, &is->sdlRect);
            SDL_RenderPresent(is->sdlRenderer );

        }
        av_free_packet(packet);
    }
    av_free(pFrame);
    av_free(pFrameyuv);
    av_free(out_buffer_yuv);
    sws_freeContext(img_convert_ctx);
    if (!gval.quit)
    {
        gval.quit = true;
    }
    is->videoThreadFinished = true;
    is->video_tid_flg = 1;
    SDL_SemPost(gval.m_vdlock);
    return 0;
}

int video_thread(void *arg)
{
    SDL_SemWait(gval.m_vdlock);
    printf("video_thread____\n");
    VideoState *is = (VideoState *) arg;
    is->video_tid_flg = 0;
    AVPacket pkt1, *packet = &pkt1;
    int ret, got_picture;
    double video_pts = 0; //当前视频的pts
    double audio_pts = 0; //音频pts
    int i = 0;
    int w,h = 0;
    ///解码视频相关
    AVFrame *pFrame,*filtersFrame;
    AVCodecContext *pCodecCtx = is->video_st->codec; //视频解码器
    char titile[36] = {'\0'};
    sprintf(titile,"/dev/video%d,hw:%d,0",is->v,is->a);
    pFrame = av_frame_alloc();
    filtersFrame = av_frame_alloc();
    //int numbytes = avpicture_get_size(AV_PIX_FMT_UYVY422,pCodecCtx->width,pCodecCtx->height);
    // filtersbuf = (u8 *)av_malloc(numbytes);
    is->screen = SDL_CreateWindow(titile, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  pCodecCtx->width, pCodecCtx->height,SDL_WINDOW_OPENGL|
                                  SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    if(!is->screen) {
        printf("SDL: could not create window - exiting:%s\n",SDL_GetError());
        return -1;
    }
    is->windows = 1;
    is->sdlRenderer = SDL_CreateRenderer(is->screen, -1, SDL_RENDERER_ACCELERATED
                                         |SDL_RENDERER_PRESENTVSYNC|SDL_RENDERER_TARGETTEXTURE);

    SDL_SetRenderDrawColor(is->sdlRenderer,0,0,0,255);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
    //  SDL_SetHint(SDL_HINT_RENDER_VSYNC,"1");
    // SDL_RenderSetLogicalSize(is->sdlRenderer, pCodecCtx->width, pCodecCtx->height);
    Uint32 pixformat= SDL_PIXELFORMAT_UYVY;
    is->sdlTexture = SDL_CreateTexture(is->sdlRenderer,pixformat, SDL_TEXTUREACCESS_STREAMING,
                                       pCodecCtx->width, pCodecCtx->height);
    //avpicture_fill((AVPicture *)filtersFrame,filtersbuf,AV_PIX_FMT_UYVY422,pCodecCtx->width,pCodecCtx->height);

    if(4 == gval.pi){
        init_filters(pCodecCtx);
    }
    while(1)
    {
        if (gval.quit)
        {
            printf("%s quit \n",__FUNCTION__);
            packet_queue_flush(&is->videoq); //清空队列
            break;
        }

        //qDebug()<<__FUNCTION__<<"000!";
        if (packet_queue_get(&is->videoq, packet, 0) <= 0)
        {
            if (is->readFinished)
            {//队列里面没有数据了且读取完毕了
                break;
            }
            else
            {
                SDL_Delay(10); //队列只是暂时没有数据而已
                continue;
            }
        }

        //收到这个数据 说明刚刚执行过跳转 现在需要把解码器的数据 清除一下
        if(strcmp((char*)packet->data,FLUSH_DATA) == 0)
        {
            avcodec_flush_buffers(is->video_st->codec);
            av_free_packet(packet);
            SDL_Delay(10);
            continue;
        }
        ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture,packet);
        if (ret < 0) {
            printf("decode error.\n");
            av_free_packet(packet);
            SDL_Delay(10);
            continue;
        }
        if (packet->dts == AV_NOPTS_VALUE && pFrame->opaque&& *(uint64_t*) pFrame->opaque != AV_NOPTS_VALUE)
        {
            video_pts = *(uint64_t *) pFrame->opaque;
        }
        else if (packet->dts != AV_NOPTS_VALUE)
        {
            video_pts = packet->dts;
        }
        else
        {
            video_pts = 0;
        }
        video_pts *= av_q2d(is->video_st->time_base);
        video_pts = synchronize_video(is, pFrame, video_pts);

        while(1)
        {
            if (gval.quit)
            {
                break;
            }

            if (is->readFinished && is->audioq.size == 0)
            {//读取完了 且音频数据也播放完了 就剩下视频数据了  直接显示出来了 不用同步了
                break;
            }
            audio_pts = is->audio_clock;
            //主要是 跳转的时候 我们把video_clock设置成0了
            //因此这里需要更新video_pts
            //否则当从后面跳转到前面的时候 会卡在这里
            video_pts = is->video_clock;
            //qDebug()<<__FUNCTION__<<video_pts<<audio_pts;
            if (video_pts <= audio_pts) break;
            int delayTime = (video_pts - audio_pts) * 1000;
            delayTime = delayTime > 5 ? 5:delayTime;
            SDL_Delay(delayTime);
        }
        if (got_picture) {
            if(4 == gval.pi){
                /* push the decoded frame into the filtergraph */
                if (av_buffersrc_add_frame_flags(gval.filter_buffer_ctx, pFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                    break;
                }

                /* pull filtered frames from the filtergraph */
                while (1) {
                    ret = av_buffersink_get_frame(gval.filter_buffersink_ctx, filtersFrame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                        break;
                    }
                    if (ret < 0){
                        return -1;
                    }
                }
            }

            //  printf("fiters:w=%d h=%d\n",filtersFrame->width,filtersFrame->height);
            //FIX: If window is resize
            SDL_GetWindowSize(is->screen,&w,&h);

            is->sdlRect.x = 0;
            is->sdlRect.y = 0;
            is->sdlRect.w = w;//gval.mainval.devcount;
            is->sdlRect.h = h;
            // SDL_UpdateTexture( is->sdlTexture, &is->sdlRect,packet->data, (pCodecCtx->width*2));
            if(4 == gval.pi){
                SDL_UpdateTexture( is->sdlTexture, NULL,filtersFrame->data[0], (filtersFrame->linesize[0]));
            }else{
                SDL_UpdateTexture( is->sdlTexture, NULL,pFrame->data[0], (pFrame->linesize[0]));
            }
            SDL_RenderClear(is->sdlRenderer );
            SDL_RenderCopy(is->sdlRenderer, is->sdlTexture, NULL, &is->sdlRect);
            SDL_RenderPresent(is->sdlRenderer );
            if(4 == gval.pi){
                av_frame_unref(filtersFrame);
            }

        }
        av_free_packet(packet);
    }
    avfilter_graph_free(&gval.filter_graph);
    av_free(pFrame);
    av_free(filtersFrame);
    is->videoThreadFinished = true;
    is->video_tid_flg = 1;
    SDL_SemPost(gval.m_vdlock);
    return 0;
}
int recoding_video_thread(void *arg)
{
    VideoState *is = (VideoState *) arg;
    is->recording_video_tid_flg = 0;
    AVPacket pkt1, *packet = &pkt1;
    int ret, got_picture, numBytes;
    double video_pts = 0; //当前视频的pts
    double audio_pts = 0; //音频pts
    ///解码视频相关
    AVFrame *pFrame,*pFrameYUV,*filtersFrame;
    uint8_t *out_buffer_yuv; //解码后的yuv420p数据
    struct SwsContext *img_convert_ctx_yuv;  //用于解码后的视频格式转换
    AVCodecContext *pCodecCtx = is->video_st->codec; //视频解码器
    int w,h;
    pFrame = av_frame_alloc();
    pFrameYUV = av_frame_alloc();
    filtersFrame = av_frame_alloc();
    ///这里我们改成了 将解码后的YUV422数据转换成yuv420p
    img_convert_ctx_yuv = sws_getContext(pCodecCtx->width, pCodecCtx->height,
                                         pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
                                         AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
    //  AV_PIX_FMT_YUV420P, SWS_POINT, NULL, NULL, NULL);

    numBytes = avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width,pCodecCtx->height);
    out_buffer_yuv = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    avpicture_fill((AVPicture *) pFrameYUV, out_buffer_yuv, AV_PIX_FMT_YUV420P,
                   pCodecCtx->width, pCodecCtx->height);

    char titile[36] = {'\0'};
    sprintf(titile,"/dev/video%d,hw:%d,0",is->v,is->a);
    is->screen = SDL_CreateWindow(titile, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  pCodecCtx->width, pCodecCtx->height,SDL_WINDOW_OPENGL|
                                  SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    if(!is->screen) {
        printf("SDL: could not create window - exiting:%s\n",SDL_GetError());
        return -1;
    }
    is->windows = 1;
    is->sdlRenderer = SDL_CreateRenderer(is->screen, -1, 0);
    SDL_SetRenderDrawColor(is->sdlRenderer,0,0,0,255);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"linear");
    Uint32 pixformat= SDL_PIXELFORMAT_UYVY;
    is->sdlTexture = SDL_CreateTexture(is->sdlRenderer,pixformat, SDL_TEXTUREACCESS_STREAMING,
                                       pCodecCtx->width, pCodecCtx->height);

    if(4 == gval.pi){
        init_filters(pCodecCtx);
    }
    while(1)
    {
        if (gval.quit)
        {
            packet_queue_flush(&is->videoq); //清空队列
            break;
        }

        //qDebug()<<__FUNCTION__<<"000!";
        if (packet_queue_get(&is->videoq, packet, 0) <= 0)
        {
            if (is->readFinished)
            {//队列里面没有数据了且读取完毕了
                break;
            }
            else
            {
                SDL_Delay(1); //队列只是暂时没有数据而已
                continue;
            }
        }
        //收到这个数据 说明刚刚执行过跳转 现在需要把解码器的数据 清除一下
        if(strcmp((char*)packet->data,FLUSH_DATA) == 0)
        {
            avcodec_flush_buffers(is->video_st->codec);
            av_free_packet(packet);
            SDL_Delay(10);
            continue;
        }
        ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture,packet);
        if (ret < 0) {
            printf("decode error.\n");
            av_free_packet(packet);
            SDL_Delay(10);
            continue;
        }
        if (packet->dts == AV_NOPTS_VALUE && pFrame->opaque&& *(uint64_t*) pFrame->opaque != AV_NOPTS_VALUE)
        {
            video_pts = *(uint64_t *) pFrame->opaque;
        }
        else if (packet->dts != AV_NOPTS_VALUE)
        {
            video_pts = packet->dts;
        }
        else
        {
            video_pts = 0;
        }
        video_pts *= av_q2d(is->video_st->time_base);
        video_pts = synchronize_video(is, pFrame, video_pts);

        while(1)
        {
            if (gval.quit)
            {
                break;
            }
            if (is->readFinished && is->audioq.size == 0)
            {//读取完了 且音频数据也播放完了 就剩下视频数据了  直接显示出来了 不用同步了
                break;
            }
            audio_pts = is->audio_clock;
            //主要是 跳转的时候 把video_clock设置成0了
            //因此这里需要更新video_pts
            //否则当从后面跳转到前面的时候 会卡在这里
            video_pts = is->video_clock;
            if (video_pts <= audio_pts) break;
            int delayTime = (video_pts - audio_pts) * 1000;
            delayTime = delayTime > 5 ? 5:delayTime;
            SDL_Delay(delayTime);
        }
        if (got_picture) {
            if(0 == gval.rawdataflg){
                sws_scale(img_convert_ctx_yuv,
                          (uint8_t const * const *) pFrame->data,
                          pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data,
                          pFrameYUV->linesize);
                DataQuene_Input(&is->videobq,out_buffer_yuv,numBytes);
            }else if(1 == gval.rawdataflg){
                DataQuene_Input(&is->videocpybq, packet->data,packet->size);
            }
            if(4 == gval.pi){
                if (av_buffersrc_add_frame_flags(gval.filter_buffer_ctx, pFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                    break;
                }

                /* pull filtered frames from the filtergraph */
                while (1) {
                    ret = av_buffersink_get_frame(gval.filter_buffersink_ctx, filtersFrame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                        break;
                    }
                    if (ret < 0){
                        return -1;
                    }
                }
                SDL_UpdateTexture( is->sdlTexture, NULL,filtersFrame->data[0], filtersFrame->linesize[0]);
            }else{
                SDL_UpdateTexture( is->sdlTexture, NULL,pFrame->data[0], pFrame->linesize[0]);
            }
            //FIX: If window is resize
            SDL_GetWindowSize(is->screen,&w,&h);

            is->sdlRect.x = 0;
            is->sdlRect.y = 0;
            is->sdlRect.w = w;//gval.mainval.devcount;
            is->sdlRect.h = h;

            SDL_RenderClear(is->sdlRenderer );
            SDL_RenderCopy(is->sdlRenderer, is->sdlTexture, NULL, &is->sdlRect);
            SDL_RenderPresent(is->sdlRenderer );
            if(4 == gval.pi){
                av_frame_unref(filtersFrame);
            }
        }
        av_free_packet(packet);
    }
    avfilter_graph_free(&gval.filter_graph);
    av_free(pFrame);
    av_free(pFrameYUV);
    av_free(filtersFrame);
    av_free(out_buffer_yuv);
    sws_freeContext(img_convert_ctx_yuv);
    if (!gval.quit)
    {
        gval.quit = true;
    }
    is->videoThreadFinished = true;
    is->recording_video_tid_flg = 1;
    return 0;
}
int writeVideo(void *arg)
{
    VideoState *is = (VideoState *) arg;
    is->wrvideo_tid_flg = 0;
    BufferDataNode *bnode = NULL;
    if(NULL == is->yuv422fd){
        is->yuv422fd = fopen(is->yuvname,"wb+");
    }
    while(1){
        if (gval.quit)
        {
            buffer_queue_flush(&is->videocpybq);
            printf("writeVideo quit\n");
            break;
        }
        bnode = DataQuene_get(&is->videocpybq);
        if(bnode == NULL){
            SDL_Delay(1);
            continue;
        }
        fwrite(bnode->buffer,bnode->bufferSize,1,is->yuv422fd);
        free(bnode->buffer);
        free(bnode);
    }
    fclose(is->yuv422fd);
    is->yuv422fd = NULL;
    is->wrvideo_tid_flg = 1;
}

int writeAudio(void *arg)
{
    VideoState *is = (VideoState *) arg;
    is->wraudio_tid_flg = 0;
    BufferDataNode *bnode = NULL;
    if(NULL == is->pcmfd){
        is->pcmfd = fopen(is->pcmname,"wb+");
    }
    while(1){
        if (gval.quit)
        {
            buffer_queue_flush(&is->videocpybq);
            printf("writeAudio quit\n");
            break;
        }
        bnode = DataQuene_get(&is->audiocpybq);
        if(bnode == NULL){
            SDL_Delay(1);
            continue;
        }
        fwrite(bnode->buffer,bnode->bufferSize,1,is->pcmfd);
        free(bnode->buffer);
        free(bnode);
    }
    fclose(is->pcmfd);
    is->pcmfd = NULL;
    is->wraudio_tid_flg = 1;
}



/*
 * add an audio output stream
 */
static AVStream *add_audio_stream(VideoState *is,AVFormatContext *oc, enum AVCodecID codec_id)
{
    AVCodecContext *c;
    AVStream *st;
    st = avformat_new_stream(oc, NULL);
    if (!st) {
        printf("Could not alloc stream\n");
        return st;
    }
    st->id = 1;
    c = st->codec;
    c->codec_id = codec_id;
    c->codec_type = AVMEDIA_TYPE_AUDIO;
    // c->codec_type = is->audio_st->codec->codec_type;
    /* put sample parameters */
    //c->sample_fmt = AV_SAMPLE_FMT_S16;
    c->sample_fmt = is->audio_st->codec->sample_fmt;
    c->bit_rate = 192000;
    c->sample_rate = 48000;
    c->channels = is->audio_st->codec->channels;
    c->channel_layout = AV_CH_LAYOUT_STEREO;

    st->time_base.den = c->sample_rate;
    st->time_base.num = 1;
    // some formats want stream headers to be separate
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;
    return st;
}

static void open_audio(AVFormatContext *oc, AVStream *st)
{
    AVCodecContext *c;
    AVCodec *codec;
    c = st->codec;
    /* find the audio encoder */
    codec = avcodec_find_encoder(c->codec_id);
    if (!codec) {
        printf("codec not found\n");
        return;
    }
    /* open it */
    if (avcodec_open2(c, codec,NULL) < 0) {
        printf("could not open codec\n");
        return;
    }
    /* init signal generator */
    gval.t = 0;
    gval.tincr = 2 * M_PI * 110.0 / c->sample_rate;
    /* increment frequency by 110 Hz per second */
    gval.tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;
    gval.audio_outbuf_size = 10000;
    gval.audio_outbuf = (uint8_t *)av_malloc(gval.audio_outbuf_size);
    if (c->frame_size <= 1) {
        gval.audio_input_frame_size = gval.audio_outbuf_size / c->channels;
        switch(st->codec->codec_id) {
        case AV_CODEC_ID_PCM_S16LE:
        case AV_CODEC_ID_PCM_S16BE:
        case AV_CODEC_ID_PCM_U16LE:
        case AV_CODEC_ID_PCM_U16BE:
            gval.audio_input_frame_size >>= 1;
            break;
        default:
            break;
        }
    } else {
        gval.audio_input_frame_size = c->frame_size;
    }
    gval.samples = (int16_t *)av_malloc(gval.audio_input_frame_size * 2 * c->channels);
}

static void write_audio_frame( VideoState *is ,AVFormatContext *oc, AVStream *st)
{
    AVCodecContext *c;
    AVPacket pkt;
    av_init_packet(&pkt);
    c = st->codec;
    BufferDataNode *node = DataQuene_get(&is->audiobq);
    if (node == NULL)
    {
        SDL_Delay(1); //延时1ms
        //printf()<<"no get audioq data";
        return;
    }
    else
    {
        memcpy(gval.samples,node->buffer, node->bufferSize);
        free(node->buffer);
        free(node);
    }
    //    fread(samples, 1, audio_input_frame_size*4, pcmInFp);
    pkt.size = avcodec_encode_audio(c, gval.audio_outbuf, gval.audio_outbuf_size, gval.samples);
    if (c->coded_frame && c->coded_frame->pts != AV_NOPTS_VALUE)
        pkt.pts= av_rescale_q(c->coded_frame->pts, c->time_base, st->time_base);
    pkt.flags |= AV_PKT_FLAG_KEY;
    pkt.stream_index = st->index;
    pkt.data = gval.audio_outbuf;
    /* write the compressed frame in the media file */
    if (av_interleaved_write_frame(oc, &pkt) != 0) {
        fprintf(stderr, "Error while writing audio frame\n");
        exit(1);
    }
}

static void close_audio(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);
    av_free(gval.samples);
    av_free(gval.audio_outbuf);
}

/**************************************************************/
/* add a video output stream */
static AVStream *add_video_stream( VideoState *is,AVFormatContext *oc, enum AVCodecID codec_id)
{
    AVCodecContext *c;
    AVStream *st;
    AVCodec *codec;

    st = avformat_new_stream(oc, NULL);
    if (!st) {
        printf("Could not alloc stream\n");
        return st;
    }
    c = st->codec;
    /* find the video encoder */
    codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        printf("codec not found\n");
        return st;
    }
    avcodec_get_context_defaults3(c, codec);
    c->codec_id = codec_id;
    /* put sample parameters */
    //c->bit_rate = 400000;
    c->bit_rate = 8500000;
    /* resolution must be a multiple of two */
    c->width = is->video_st->codec->width;
    c->height = is->video_st->codec->height;
    /* time base: this is the fundamental unit of time (in seconds) in terms
       of which frame timestamps are represented. for fixed-fps content,
       timebase should be 1/framerate and timestamp increments should be
       identically 1. */
    c->time_base.den = is->timebase_den;
    c->time_base.num = 1;
    c->gop_size = 12; /* emit one intra frame every twelve frames at most */
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->thread_count = 1;
    st->time_base =  c->time_base;
    c->qmin = 0;
    c->qmax = 0;
    if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        /* just for testing, we also add B frames */
        c->max_b_frames = 2;
    }
    if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO){
        /* Needed to avoid using macroblocks in which some coeffs overflow.
           This does not happen with normal video, it just happens here as
           the motion of the chroma plane does not match the luma plane. */
        c->mb_decision=2;
    }
    // some formats want stream headers to be separate
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;
    return st;
}

static AVFrame *alloc_picture(enum PixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    uint8_t *picture_buf;
    int size;

    picture = avcodec_alloc_frame();
    if (!picture)
        return NULL;
    size = avpicture_get_size(pix_fmt, width, height);
    picture_buf = (uint8_t *)av_malloc(size);
    if (!picture_buf) {
        av_free(picture);
        return NULL;
    }
    avpicture_fill((AVPicture *)picture, picture_buf,
                   pix_fmt, width, height);
    return picture;
}

static void open_video(AVFormatContext *oc, AVStream *st)
{
    AVCodec *codec;
    AVCodecContext *c;
    // st->time_base = {1,};
    c = st->codec;
    /* find the video encoder */
    codec = avcodec_find_encoder(c->codec_id);
    if (!codec) {
        printf("codec not found\n");
        return;
    }
    AVDictionary *dictParam = NULL;
    // if(c->codec_id == AV_CODEC_ID_H264)
    //  {
    av_dict_set(&dictParam,"tune","zerolatency",0);
    av_dict_set(&dictParam, "preset", "ultrafast",0);

    //  av_dict_set(&dictParam,"profile","main",0);
    // }
    /* open the codec */
    if (avcodec_open2(c, codec, &dictParam) < 0) {
        printf("could not open codec\n");
        return;
    }
    gval.video_outbuf = NULL;
    if (!(oc->oformat->flags & AVFMT_RAWPICTURE)) {
        gval.video_outbuf_size = 1920*1080*2;
        gval.video_outbuf = (uint8_t *)av_malloc(gval.video_outbuf_size);
    }
    /* allocate the encoded raw picture */
    gval.picture = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!gval.picture) {
        printf("Could not allocate picture\n");
        return;
    }
    gval.picture_buf = (uint8_t *)av_malloc(c->width * c->height *4);
}

static void write_video_frame( VideoState *is ,AVFormatContext *oc, AVStream *st)
{

    int out_size, ret;
    AVCodecContext *c;

    c = st->codec;

    BufferDataNode *node = DataQuene_get(&is->videobq);

    if (node == NULL)
    {
        SDL_Delay(1); //延时1ms
        //qDebug()<<"no get videobq data";
        return;
    }
    else
    {
        int y_size = c->width * c->height;
        memcpy(gval.picture_buf,node->buffer, y_size*3/2);
        free(node->buffer);
        free(node);
        gval.picture->data[0] = gval.picture_buf;  // 亮度Y
        gval.picture->data[1] = gval.picture_buf+ y_size;  // U
        gval.picture->data[2] = gval.picture_buf+ y_size*5/4; // V
		gval.picture->width = c->width;
		gval.picture->height = c->height;
		gval.picture->format = c->pix_fmt;
    }

    if (oc->oformat->flags & AVFMT_RAWPICTURE) {
        /* raw video case. The API will change slightly in the near
           future for that. */
        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.flags |= AV_PKT_FLAG_KEY;
        pkt.stream_index = st->index;
        pkt.data = (uint8_t *)gval.picture;
        pkt.size = sizeof(AVPicture);
        ret = av_interleaved_write_frame(oc, &pkt);
    } else {
        /* encode the image */
        out_size = avcodec_encode_video(c, gval.video_outbuf, gval.video_outbuf_size, gval.picture);
        /* if zero size, it means the image was buffered */
        if (out_size > 0) {
            AVPacket pkt;
            av_init_packet(&pkt);
            if (c->coded_frame->pts != AV_NOPTS_VALUE)
                pkt.pts= av_rescale_q(c->coded_frame->pts, c->time_base, st->time_base);
            if(c->coded_frame->key_frame)
                pkt.flags |= AV_PKT_FLAG_KEY;
            pkt.stream_index = st->index;
            pkt.data = gval.video_outbuf;
            pkt.size = out_size;
            /* write the compressed frame in the media file */
            ret = av_interleaved_write_frame(oc, &pkt);
            gval.picture->pts++;
        } else {
            ret = 0;
        }
    }
    if (ret != 0) {
        fprintf(stderr, "Error while writing video frame\n");
        exit(1);
    }

}

static void close_video(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);
    av_free(gval.picture->data[0]);
    av_free(gval.picture);
    av_free(gval.video_outbuf);
}

int encode_thread(void *arg)
{
    int i;
    char filename[128] = {'\0'};
    AVOutputFormat *fmt;
    AVFormatContext *oc;
    AVStream *audio_st, *video_st;
    double audio_pts, video_pts;
    VideoState *is = (VideoState *) arg;
    is->encode_tid_flg = 0;
    /* initialize libavcodec, and register all codecs and formats */
    //  av_register_all();
    strcpy(filename,is->recordname);
    /* allocate the output media context */
    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
    if (!oc) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
    }
    if (!oc) {
        return 1;
    }
    fmt = oc->oformat;
    /* add the audio and video streams using the default format codecs
       and initialize the codecs */
    video_st = NULL;
    audio_st = NULL;
    if (fmt->video_codec != AV_CODEC_ID_NONE) {
        video_st = add_video_stream(is,oc, fmt->video_codec);
    }
    if (fmt->audio_codec != AV_CODEC_ID_NONE) {
        audio_st = add_audio_stream(is,oc, fmt->audio_codec);
    }
    av_dump_format(oc, 0, filename, 1);
    /* now that all the parameters are set, we can open the audio and
       video codecs and allocate the necessary encode buffers */
    if (video_st)
        open_video(oc, video_st);
    if (audio_st)
        open_audio(oc, audio_st);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        if (avio_open(&oc->pb, filename, AVIO_FLAG_WRITE) < 0) {
            printf("Could not open '%s'\n", filename);
            return 1;
        }
    }
    /* write the stream header, if any */
    //    av_write_header(oc);
    avformat_write_header(oc,NULL);
    gval.picture->pts = 0;
    while(1)
    {
        if (gval.quit)
        {
            buffer_queue_flush(&is->videobq); //清空队列
            buffer_queue_flush(&is->audiobq);
            //停止播放了
            break;
        }
        /* compute current audio and video time */
        if (audio_st)
            audio_pts = (double)audio_st->pts.val * audio_st->time_base.num / audio_st->time_base.den;
        else
            audio_pts = 0.0;

        if (video_st)
            video_pts = (double)video_st->pts.val * video_st->time_base.num / video_st->time_base.den;
        else
            video_pts = 0.0;

        //qDebug()<<audio_pts<<video_pts;
        if ((!audio_st)&&(!video_st))
            break;
        /* write interleaved audio and video frames */
        if (!video_st || (video_st && audio_st && audio_pts < video_pts)) {
            write_audio_frame(is,oc, audio_st);
        } else {
            write_video_frame(is,oc, video_st);
        }
    }
    av_write_trailer(oc);
    /* close each codec */
    if (video_st)
        close_video(oc, video_st);
    if (audio_st)
        close_audio(oc, audio_st);
    /* free the streams */
    for(i = 0; i < oc->nb_streams; i++) {
        av_freep(&oc->streams[i]->codec);
        av_freep(&oc->streams[i]);
    }
    if (!(fmt->flags & AVFMT_NOFILE)) {
        /* close the output file */
        avio_close(oc->pb);
    }
    /* free the stream */
    av_free(oc);
    is->encode_tid_flg = 1;
    return 0;
}


int read_video_thread(void *arg)
{
    SDL_SemWait(gval.m_rvlock);
    printf("read_video_thread____\n");
    VideoState *is = (VideoState *) arg;
    is->read_video_tid_flg = 0;
    AVPacket *vpacket = (AVPacket *) malloc(sizeof(AVPacket)); //分配一个packet 用来存放读取的视频
    while (1)
    {
        if (gval.quit)
        {
            //停止播放了
            break;
        }
        if (is->videoq.size > MAX_VIDEO_SIZE) {
            printf("is->videoq.size > MAX_VIDEO_SIZE\n");
            SDL_Delay(10);
            continue;
        }
        if (av_read_frame(is->ic, vpacket) < 0)
        {
            is->readFinished = true;
            if (gval.quit)
            {
                break; //解码线程也执行完了 可以退出了
            }
            SDL_Delay(10);
            continue;
        }
        if (vpacket->stream_index == is->videoStream)
        {
            packet_queue_put(&is->videoq, vpacket);
            //这里我们将数据存入队列 因此不调用 av_free_packet 释放
        }
        else
        {
            av_free_packet(vpacket);
        }
        // SDL_Delay(5);
    }
    is->read_video_tid_flg = 1;
    SDL_SemPost(gval.m_rvlock);
    return 0;
}

int child_localPlay(void *arg)
{
    char file_path[128] = {'\0'};
    strcpy(file_path,gval.mainval.fil);
    VideoState *is = &mVideoState[0];
    memset(is,0,sizeof(VideoState)); //为了安全起见  先将结构体的数据初始化成0了
    is->isMute = false;
    is->mVolume = 1;
    AVFormatContext *pFormatCtx;
    AVCodecContext *pCodecCtx;
    AVCodec *pCodec;
    AVCodecContext *aCodecCtx;
    AVCodec *aCodec;
    int audioStream ,videoStream, i;
    //Allocate an AVFormatContext.
    pFormatCtx = avformat_alloc_context();
    if (avformat_open_input(&pFormatCtx, file_path, NULL, NULL) != 0) {
        printf("can't open the file. \n");
        return -1;
    }
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf("Could't find stream infomation.\n");
        return -1;
    }
    videoStream = -1;
    audioStream = -1;
    ///循环查找视频中包含的流信息，
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = i;
        }
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO  && audioStream < 0)
        {
            audioStream = i;
        }
    }

    ///如果videoStream为-1 说明没有找到视频流
    if (videoStream == -1) {
        printf("Didn't find a video stream.\n");
        return -1;
    }

    if (audioStream == -1) {
        printf("Didn't find a audio stream.\n");
        return -1;
    }
    is->ic = pFormatCtx;
    is->ic_a = pFormatCtx;
    is->videoStream = videoStream;
    is->audioStream = audioStream;
    if (audioStream >= 0) {
        /* 所有设置SDL音频流信息的步骤都在这个函数里完成 */
        audio_stream_component_open(&mVideoState, audioStream);
    }
    ///查找音频解码器
    aCodecCtx = pFormatCtx->streams[audioStream]->codec;
    aCodec = avcodec_find_decoder(aCodecCtx->codec_id);

    if (aCodec == NULL) {
        printf("ACodec not found.\n");
        return -1;
    }
    ///打开音频解码器
    if (avcodec_open2(aCodecCtx, aCodec, NULL) < 0) {
        printf("Could not open audio codec.\n");
        return -1;
    }
    is->audio_st = pFormatCtx->streams[audioStream];
    ///查找视频解码器
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (pCodec == NULL) {
        printf("PCodec not found.\n");
        return -1;
    }
    ///打开视频解码器
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        printf("Could not open video codec.\n");
        return -1;
    }
    is->video_st = pFormatCtx->streams[videoStream];
    packet_queue_init(&is->audioq);
    packet_queue_init(&is->videoq);
    ///创建一个线程专门用来解码视频
    is->video_tid = SDL_CreateThread(local_video_thread, "local_video_thread", is);
    AVPacket *packet = (AVPacket *) malloc(sizeof(AVPacket)); //分配一个packet 用来存放读取的视频
    openSDL(0);
    SDL_LockAudioDevice(is->mAudioID);
    SDL_PauseAudioDevice(is->mAudioID,0);
    SDL_UnlockAudioDevice(is->mAudioID);
    while (1)
    {
        if (gval.quit)
        {
            //停止播放了
            break;
        }

        //这里做了个限制  当队列里面的数据超过某个大小的时候 就暂停读取  防止一下子就把视频读完了，导致的空间分配不足
        /* 这里audioq.size是指队列中的所有数据包带的音频数据的总量或者视频数据总量，并不是包的数量 */
        //这个值可以稍微写大一些
        //        qDebug()<<__FUNCTION__<<is->audioq.size<<MAX_AUDIO_SIZE<<is->videoq.size<<MAX_VIDEO_SIZE;
        if (is->audioq.size > MAX_AUDIO_SIZE || is->videoq.size > MAX_VIDEO_SIZE) {
            SDL_Delay(10);
            continue;
        }

        if (av_read_frame(pFormatCtx, packet) < 0)
        {
            is->readFinished = true;
            if (gval.quit)
            {
                break; //解码线程也执行完了 可以退出了
            }
            SDL_Delay(10);
            continue;
        }
        if (packet->stream_index == videoStream)
        {
            packet_queue_put(&is->videoq, packet);
            //这里我们将数据存入队列 因此不调用 av_free_packet 释放
        }
        else if( packet->stream_index == audioStream )
        {
            packet_queue_put(&is->audioq, packet);
            //这里我们将数据存入队列 因此不调用 av_free_packet 释放
        }
        else
        {
            // Free the packet that was allocated by av_read_frame
            av_free_packet(packet);
        }
    }
    while(1){
        if(1 == is->video_tid_flg){
            SDL_WaitThread(is->video_tid, NULL);
            is->video_tid_flg = 0;
            break;
        }else{
            SDL_Delay(10);
        }
    }
    ///文件读取结束 跳出循环的情况
    ///等待播放完毕
    while (!gval.quit) {
        SDL_Delay(100);
    }
    SDL_LockAudioDevice(is->mAudioID);
    SDL_PauseAudioDevice(is->mAudioID,1);
    SDL_UnlockAudioDevice(is->mAudioID);
    closeSDL(0);
    while(!is->videoThreadFinished)
    {
        SDL_Delay(10);
    } //确保视频线程结束后 再销毁队列
    avcodec_close(aCodecCtx);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);
    avformat_free_context(pFormatCtx);
    free(packet);
    packet_queue_deinit(&is->videoq);
    packet_queue_deinit(&is->audioq);
    is->readThreadFinished = true;
}
int child_recordPlay(void *arg)
{
    SDL_SemWait(gval.m_lock);
    CAPARG_t*cap = (CAPARG_t *)(arg);
    VideoState* is = &mVideoState[cap->no];
    AVInputFormat *vInputFmt;
    AVFormatContext *vpFormatCtx;
    AVCodecContext *vpCodecCtx;
    AVCodec *vpCodec;
    AVInputFormat *aInputFmt;
    AVFormatContext *apFormatCtx;
    AVCodecContext *apCodecCtx;
    AVCodec *apCodec;
    int audioStream ,videoStream, i;
    char* vinput_name= "video4linux2";
    char* ainput_name= "alsa";
    char vfile_path[20] = {'\0'};
    char afile_path[20] = {'\0'};
    memset(is,0,sizeof(VideoState)); //为了安全起见  先将结构体的数据初始化成0了
    is->isMute = false;
    is->mVolume = 1;
    is->v = cap->v;
    is->a = cap->a;
    is->no = cap->no;
    sprintf(vfile_path,"/dev/video%d",is->v);
    sprintf(afile_path,"hw:%d",is->a);
    int capfps = getFps(is->v);
    gval.pi = getPI(is->v);
    //1. av_find_input_format (input_name);
    vInputFmt = av_find_input_format(vinput_name);
    if (NULL == vInputFmt)    {
        printf("can not find_vinput_format\n");
        return -1;
    }
    aInputFmt = av_find_input_format(ainput_name);
    if (NULL == vInputFmt)    {
        printf("can not find_ainput_format\n");
        return -1;
    }
    //2.avformat_open_input
    //Allocate an AVFormatContext.
    vpFormatCtx = avformat_alloc_context();
    apFormatCtx = avformat_alloc_context();
    if (avformat_open_input(&vpFormatCtx, vfile_path, vInputFmt, NULL) < 0) {
        printf("can't open the vfile. \n");
        return -1;
    }
    if (avformat_open_input(&apFormatCtx, afile_path, aInputFmt, NULL) < 0) {
        printf("can't open the afile. \n");
        return -1;
    }
    //3.avformat_find_stream_info
    if (avformat_find_stream_info(vpFormatCtx, NULL) < 0) {
        printf("Could't find vstream infomation.\n");
        return -1;
    }
    if (avformat_find_stream_info(apFormatCtx, NULL) < 0) {
        printf("Could't find astream infomation.\n");
        return -1;
    }
    //4.av_dump_format
    av_dump_format(vpFormatCtx, 0, vfile_path, 0);
    av_dump_format(apFormatCtx, 0, afile_path, 0);

    //5.循环查找视频中包含的流信息，
    videoStream = -1;
    audioStream = -1;
    for (i = 0; i < vpFormatCtx->nb_streams; i++) {
        if (vpFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0)
        {
            videoStream = i;
        }
    }
    for (i = 0; i < apFormatCtx->nb_streams; i++) {
        if (apFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO  && audioStream < 0)
        {
            audioStream = i;
        }
    }
    ///如果videoStream为-1 说明没有找到视频流
    if (videoStream == -1) {
        printf("Didn't find a video stream.\n");
        return -1;
    }
    if (audioStream == -1) {
        printf("Didn't find a audio stream.\n");
        return -1;
    }
    is->ic = vpFormatCtx;
    is->ic_a = apFormatCtx;
    is->videoStream = videoStream;
    is->audioStream = audioStream;
    // emit sig_TotalTimeChanged(getTotalTime());
    if (audioStream >= 0) {
        /* 所有设置SDL音频流信息的步骤都在这个函数里完成 */
        audio_stream_component_open(is, audioStream);
    }
    ///查找音频解码器
    apCodecCtx = apFormatCtx->streams[audioStream]->codec;
    apCodec = avcodec_find_decoder(apCodecCtx->codec_id);

    if (apCodec == NULL) {
        printf("ACodec not found.\n");
        return -1;
    }
    ///打开音频解码器
    if (avcodec_open2(apCodecCtx, apCodec, NULL) < 0) {
        printf("Could not open audio codec.\n");
        return -1;
    }
    is->audio_st = apFormatCtx->streams[audioStream];
    ///查找视频解码器
    vpCodecCtx = vpFormatCtx->streams[videoStream]->codec;
    vpCodec = avcodec_find_decoder(vpCodecCtx->codec_id);
    if (vpCodec == NULL) {
        printf("Vpcodec not found.\n");
        return -1;
    }
    ///打开视频解码器
    if (avcodec_open2(vpCodecCtx, vpCodec, NULL) < 0) {
        printf("Could not open video codec.");
        return -1;
    }
    is->video_st = vpFormatCtx->streams[videoStream];
    packet_queue_init(&is->audioq);
    packet_queue_init(&is->videoq);

    buffer_queue_init(&is->audiobq);
    buffer_queue_init(&is->videobq);
    buffer_queue_init(&is->videocpybq);
    buffer_queue_init(&is->audiocpybq);

    //if(4 == gval.pi){
    //    is->timebase_den = capfps/2+capfps%2;
    //}else if(7 == gval.pi){
        is->timebase_den = capfps;
    //}
    if( 0 == gval.mainval.rectype){
        sprintf(is->recordname,"%s.mp4",gval.mainval.rec);
        gval.rawdataflg = 0;
    }else if(1 == gval.mainval.rectype){
        sprintf(is->recordname,"%s.mov",gval.mainval.rec);
        gval.rawdataflg = 0;
    }else if(2 == gval.mainval.rectype){
        sprintf(is->yuvname,"%s.yuv",gval.mainval.rec);
        sprintf(is->pcmname,"%s.pcm",gval.mainval.rec);
        gval.rawdataflg = 1;
    }
    if(gval.rawdataflg == 1){
        is->pcmfd = NULL;
        is->yuv422fd = NULL;
        is->wrvideo_tid = SDL_CreateThread(writeVideo, "writeVideo", is);
        is->wraudio_tid = SDL_CreateThread(writeAudio, "writeAudio", is);
    }

    ///创建一个线程专门用来解码视频
    is->video_tid = SDL_CreateThread(recoding_video_thread, "recoding_video_thread", is);
    openSDL(is->no);
    SDL_LockAudioDevice(is->mAudioID);
    SDL_PauseAudioDevice(is->mAudioID,0);
    SDL_UnlockAudioDevice(is->mAudioID);
    AVPacket *apacket = (AVPacket *) malloc(sizeof(AVPacket)); //分配一个packet 用来存放读取的视频
    is->read_video_tid = SDL_CreateThread(read_video_thread, "read_video_thread", is);
    if(gval.rawdataflg == 0){
        is->encode_tid = SDL_CreateThread(encode_thread, "encode_thread", is);
    }
    while (1)
    {
        if (gval.quit)
        {
            //停止播放了
            break;
        }
        if (is->audioq.size > MAX_AUDIO_SIZE)
        {
            printf("is->audioq.size > MAX_AUDIO_SIZE\n");
            SDL_Delay(10);
            continue;
        }
        if (av_read_frame(is->ic_a, apacket) < 0)
        {
            is->readFinished = true;
            if (gval.quit)
            {
                break; //解码线程也执行完了 可以退出了
            }
            SDL_Delay(10);
            continue;
        }
        if( apacket->stream_index == is->audioStream )
        {
            packet_queue_put(&is->audioq, apacket);
            //这里我们将数据存入队列 因此不调用 av_free_packet 释放
        }
        else
        {
            // Free the packet that was allocated by av_read_frame
            av_free_packet(apacket);
        }
    }
    while(1){
        if((1 == is->video_tid_flg)&&(1 == is->read_video_tid_flg)){
            SDL_WaitThread(is->video_tid, NULL);
            SDL_WaitThread(is->read_video_tid, NULL);
            is->video_tid_flg = 0;
            is->read_video_tid_flg = 0;
            break;
        }else{
            SDL_Delay(10);
        }
    }
    ///文件读取结束 跳出循环的情况
    ///等待播放完毕
    while (!gval.quit) {
        SDL_Delay(100);
    }

    SDL_LockAudioDevice(is->mAudioID);
    SDL_PauseAudioDevice(is->mAudioID,1);
    SDL_UnlockAudioDevice(is->mAudioID);
    closeSDL(is->no);
    while(!is->videoThreadFinished)
    {
        SDL_Delay(10);
    } //确保视频线程结束后 再销毁队列
    avcodec_close(vpCodecCtx);
    avcodec_close(apCodecCtx);
    avformat_close_input(&vpFormatCtx);
    avformat_close_input(&apFormatCtx);
    avformat_free_context(vpFormatCtx);
    avformat_free_context(apFormatCtx);
    free(apacket);
    packet_queue_deinit(&is->videoq);
    packet_queue_deinit(&is->audioq);
    buffer_queue_deinit(&is->videobq);
    buffer_queue_deinit(&is->audiobq);
    buffer_queue_deinit(&is->videocpybq);
    buffer_queue_deinit(&is->audiocpybq);
    is->readThreadFinished = true;
    while(1){
        if((1 == is->recording_video_tid_flg)&&(1 == is->read_video_tid_flg)){
            SDL_WaitThread(is->recording_video_tid, NULL);
            SDL_WaitThread(is->read_video_tid, NULL);
            is->recording_video_tid_flg = 0;
            is->read_video_tid_flg = 0;
            break;
        }else{
            SDL_Delay(10);
        }
    }
    while(1){
        if(gval.rawdataflg == 0){
            if(1 == is->encode_tid_flg){
                SDL_WaitThread(is->encode_tid, NULL);
                is->encode_tid_flg = 0;
                break;
            }else{
                SDL_Delay(10);
            }
        }else{
            break;
        }
    }
    while(1){
        if(gval.rawdataflg == 1){
            if((1 == is->wraudio_tid_flg) &&(1 == is->wrvideo_tid_flg)){
                SDL_WaitThread(is->wraudio_tid, NULL);
                SDL_WaitThread(is->wrvideo_tid, NULL);
                is->wraudio_tid_flg = 0;
                is->wrvideo_tid_flg = 0;
                break;
            }else{
                SDL_Delay(10);
            }

        }else{
            break;
        }
    }
    return 0;
}


int child_capturePlay(void *arg)
{
    SDL_SemWait(gval.m_lock);
    CAPARG_t*cap = (CAPARG_t *)(arg);
    VideoState* is = &mVideoState[cap->no];
    AVInputFormat *vInputFmt;
    AVFormatContext *vpFormatCtx;
    AVCodecContext *vpCodecCtx;
    AVCodec *vpCodec;
    AVInputFormat *aInputFmt;
    AVFormatContext *apFormatCtx;
    AVCodecContext *apCodecCtx;
    AVCodec *apCodec;
    int audioStream ,videoStream, i;
    char* vinput_name= "video4linux2";
    char* ainput_name= "alsa";
    char vfile_path[20] = {'\0'};
    char afile_path[20] = {'\0'};
    memset(is,0,sizeof(VideoState)); //为了安全起见  先将结构体的数据初始化成0了
    is->isMute = false;
    is->mVolume = 1;
    is->v = cap->v;
    is->a = cap->a;
    is->no = cap->no;
    sprintf(vfile_path,"/dev/video%d",is->v);
    sprintf(afile_path,"hw:%d",is->a);
    gval.pi = getPI(is->v);
    //1. av_find_input_format (input_name);
    vInputFmt = av_find_input_format(vinput_name);
    if (NULL == vInputFmt)    {
        printf("can not find_vinput_format\n");
        return -1;
    }
    aInputFmt = av_find_input_format(ainput_name);
    if (NULL == vInputFmt)    {
        printf("can not find_ainput_format\n");
        return -1;
    }
    //2.avformat_open_input
    //Allocate an AVFormatContext.
    vpFormatCtx = avformat_alloc_context();
    apFormatCtx = avformat_alloc_context();
    if (avformat_open_input(&vpFormatCtx, vfile_path, vInputFmt, NULL) < 0) {
        printf("can't open the vfile. \n");
        return -1;
    }
    if (avformat_open_input(&apFormatCtx, afile_path, aInputFmt, NULL) < 0) {
        printf("can't open the afile. \n");
        return -1;
    }
    //3.avformat_find_stream_info
    if (avformat_find_stream_info(vpFormatCtx, NULL) < 0) {
        printf("Could't find vstream infomation.\n");
        return -1;
    }
    if (avformat_find_stream_info(apFormatCtx, NULL) < 0) {
        printf("Could't find astream infomation.\n");
        return -1;
    }
    //4.av_dump_format
    av_dump_format(vpFormatCtx, 0, vfile_path, 0);
    av_dump_format(apFormatCtx, 0, afile_path, 0);

    //5.循环查找视频中包含的流信息，
    videoStream = -1;
    audioStream = -1;
    for (i = 0; i < vpFormatCtx->nb_streams; i++) {
        if (vpFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0)
        {
            videoStream = i;
        }
    }
    for (i = 0; i < apFormatCtx->nb_streams; i++) {
        if (apFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO  && audioStream < 0)
        {
            audioStream = i;
        }
    }
    ///如果videoStream为-1 说明没有找到视频流
    if (videoStream == -1) {
        printf("Didn't find a video stream.\n");
        return -1;
    }
    if (audioStream == -1) {
        printf("Didn't find a audio stream.\n");
        return -1;
    }
    is->ic = vpFormatCtx;
    is->ic_a = apFormatCtx;
    is->videoStream = videoStream;
    is->audioStream = audioStream;
    // emit sig_TotalTimeChanged(getTotalTime());
    if (audioStream >= 0) {
        /* 所有设置SDL音频流信息的步骤都在这个函数里完成 */
        audio_stream_component_open(is, audioStream);
    }
    ///查找音频解码器
    apCodecCtx = apFormatCtx->streams[audioStream]->codec;
    apCodec = avcodec_find_decoder(apCodecCtx->codec_id);

    if (apCodec == NULL) {
        printf("ACodec not found.\n");
        return -1;
    }
    ///打开音频解码器
    if (avcodec_open2(apCodecCtx, apCodec, NULL) < 0) {
        printf("Could not open audio codec.\n");
        return -1;
    }
    is->audio_st = apFormatCtx->streams[audioStream];
    ///查找视频解码器
    vpCodecCtx = vpFormatCtx->streams[videoStream]->codec;
    vpCodec = avcodec_find_decoder(vpCodecCtx->codec_id);
    if (vpCodec == NULL) {
        printf("Vpcodec not found.\n");
        return -1;
    }
    ///打开视频解码器
    if (avcodec_open2(vpCodecCtx, vpCodec, NULL) < 0) {
        printf("Could not open video codec.");
        return -1;
    }
    is->video_st = vpFormatCtx->streams[videoStream];
    packet_queue_init(&is->audioq);
    packet_queue_init(&is->videoq);
    ///创建一个线程专门用来解码视频
    is->video_tid = SDL_CreateThread(video_thread, "video_thread", is);
    openSDL(is->no);
    SDL_LockAudioDevice(is->mAudioID);
    SDL_PauseAudioDevice(is->mAudioID,0);
    SDL_UnlockAudioDevice(is->mAudioID);
    AVPacket *apacket = (AVPacket *) malloc(sizeof(AVPacket)); //分配一个packet 用来存放读取的视频
    is->read_video_tid = SDL_CreateThread(read_video_thread, "read_video_thread", is);

    while (1)
    {
        if (gval.quit)
        {
            //停止播放了
            break;
        }
        if (is->audioq.size > MAX_AUDIO_SIZE)
        {
            printf("is->audioq.size > MAX_AUDIO_SIZE\n");
            SDL_Delay(10);
            continue;
        }
        if (av_read_frame(is->ic_a, apacket) < 0)
        {
            is->readFinished = true;
            if (gval.quit)
            {
                break; //解码线程也执行完了 可以退出了
            }
            SDL_Delay(10);
            continue;
        }
        if( apacket->stream_index == is->audioStream )
        {
            packet_queue_put(&is->audioq, apacket);
            //这里我们将数据存入队列 因此不调用 av_free_packet 释放
        }
        else
        {
            // Free the packet that was allocated by av_read_frame
            av_free_packet(apacket);
        }
        // SDL_Delay(5);
    }
    while(1){
        if((1 == is->video_tid_flg)&&(1 == is->read_video_tid_flg)){
            SDL_WaitThread(is->video_tid, NULL);
            SDL_WaitThread(is->read_video_tid, NULL);
            is->video_tid_flg = 0;
            is->read_video_tid_flg = 0;
            break;
        }else{
            SDL_Delay(10);
        }
    }
    ///文件读取结束 跳出循环的情况
    ///等待播放完毕
    while (!gval.quit) {
        SDL_Delay(100);
    }

    SDL_LockAudioDevice(is->mAudioID);
    SDL_PauseAudioDevice(is->mAudioID,1);
    SDL_UnlockAudioDevice(is->mAudioID);
    closeSDL(is->no);
    while(!is->videoThreadFinished)
    {
        SDL_Delay(10);
    } //确保视频线程结束后 再销毁队列
    avcodec_close(vpCodecCtx);
    avcodec_close(apCodecCtx);
    avformat_close_input(&vpFormatCtx);
    avformat_close_input(&apFormatCtx);
    avformat_free_context(vpFormatCtx);
    avformat_free_context(apFormatCtx);
    free(apacket);
    packet_queue_deinit(&is->videoq);
    packet_queue_deinit(&is->audioq);
    SDL_SemPost(gval.m_lock);

    return 0;
}

int capturePlay()
{
    int i = 0;
    gval.Mode_i = 2;
    sdlPlayerInit();
    SDL_CreateThread(keydone_thread, "keydone_thread", NULL);
    i = 0;

    while(-1 != gval.mainval.vid[i]){

        gval.caparg[i].v  = gval.mainval.vid[i];
        gval.caparg[i].a  = gval.mainval.aud[i];
        gval.caparg[i].no = i;
        SDL_CreateThread(child_capturePlay, "child_capturePlay", &gval.caparg[i]);
        //  printf("v[%d]:%d,a[%d]:%d\n",i,gval.mainval.vid[i],i,gval.mainval.aud[i]);
        SDL_Delay(100);
        i++;
        if(i >= 4){
            break;
        }
    }

    /*
    gval.caparg[i].v  = gval.mainval.vid[i];
    gval.caparg[i].a  = gval.mainval.aud[i];
    gval.caparg[i].no = i;
    child_capturePlay(&gval.caparg[i]);
    */
    while(1 != gval.quit){
        SDL_Delay(2000);
    };
    SDL_Delay(2000);
    return 0;
}

int localPlay()
{
    gval.Mode_i = 1;
    sdlPlayerInit();
    SDL_CreateThread(keydone_thread, "keydone_thread", NULL);
    SDL_CreateThread(child_localPlay, "child_localPlay", NULL);

    while(1 != gval.quit){
        SDL_Delay(2000);
    };
    SDL_Delay(2000);
    return 0;
}

int recordPlay()
{
    gval.Mode_i = 3;
    sdlPlayerInit();
    SDL_CreateThread(keydone_thread, "keydone_thread", NULL);
    gval.caparg[0].v  = gval.mainval.vid[0];
    gval.caparg[0].a  = gval.mainval.aud[0];
    gval.caparg[0].no = 0;
    SDL_CreateThread(child_recordPlay, "child_recordPlay", &gval.caparg[0]);

    while(1 != gval.quit){
        SDL_Delay(2000);
    };
    SDL_Delay(2000);
    return 0;
}


int tbswrite(int fd, int reg, int val)
{
    int ret;
    struct v4l2_tbs_data data;
    data.baseaddr = 0;
    data.reg = reg;
    data.value = val;
    ret = ioctl(fd, VIDIOC_TBS_S_CTL, &data);
    if (ret < 0) {
        printf("VIDIOC_TBS_S_CTL failed (%d)\n", ret);
        return ret;
    }
    return 0;
}

int tbsread(int fd, int reg, int *val)
{
    int ret;
    struct v4l2_tbs_data data;
    data.baseaddr = 0;
    data.reg = reg;
    ret = ioctl(fd, VIDIOC_TBS_G_CTL, &data);
    if (ret < 0) {
        printf("VIDIOC_TBS_G_CTL failed (%d)\n", ret);
        return ret;
    }
    *val = data.value;
    return 0;
}


int getFps(int v)
{
    //1. Open Device
    int fps = -1;
    int ret = 0;
    char camer_device[128] = {'0'};
    sprintf(camer_device,"/dev/video%d",v);
    int vfd = open(camer_device, O_RDWR|O_NONBLOCK, 0);
    if (-1 == vfd) {
        fprintf(stderr,"Cannot open '%s':%d,%s\n",camer_device,errno,strerror(errno));
        return fps;
    }
    //qDebug("open device %s is finished",camer_device);

    //set frame number
    struct v4l2_streamparm *v4l2fps;
    v4l2fps=(struct v4l2_streamparm *) malloc(sizeof(struct v4l2_streamparm));
    if(v4l2fps == NULL)
    {
        fprintf(stderr,"'%s':%d,%s,malloc\n",camer_device,errno,strerror(errno));
        free(v4l2fps);
        close(vfd);
        return fps;
    }
    memset(v4l2fps, 0, sizeof(struct v4l2_streamparm));
    v4l2fps->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(vfd, VIDIOC_G_PARM, v4l2fps);
    if(ret == 0)
    {
        //qDebug("Frame rate: %u/%u",
        //       v4l2fps->parm.capture.timeperframe.denominator,
        //       v4l2fps->parm.capture.timeperframe.numerator);
        fps =  v4l2fps->parm.capture.timeperframe.denominator;

    }else{
        fprintf(stderr,"'%s':%d,%s,VIDIOC_G_PARM\n",camer_device,errno,strerror(errno));
    }
    free(v4l2fps);
    close(vfd);
    return fps;
}

int getPI(int v)
{
    //1. Open Device
    int pi = -1;
    int ret = 0;
    struct v4l2_format fmt;
    char camer_device[128] = {'0'};
    sprintf(camer_device,"/dev/video%d",v);
    int vfd = open(camer_device, O_RDWR|O_NONBLOCK, 0);
    if (-1 == vfd) {
        fprintf(stderr,"Cannot open '%s':%d,%s\n",camer_device,errno,strerror(errno));
        return pi;
    }
    //fprintf(stdout,"open device %s is finished\n",camer_device);
    memset(&fmt, 0, sizeof(fmt));
    fmt.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
    //Get Stream Format
    ret = ioctl(vfd, VIDIOC_G_FMT, &fmt);
    if (ret < 0) {
        fprintf(stderr," %s:%d,%s,VIDIOC_G_FMT\n",camer_device,errno,strerror(errno));
        close(vfd);
        return pi;
    }
    //fprintf(stdout,"%s ,field:%d \n",camer_device, fmt.fmt.pix.field);
    pi = fmt.fmt.pix.field;
    close(vfd);
    return pi;
}
/*
printf("     	     video0              video1 \n");
printf("PtoI:          2*4                 8*4(read 5*4)\n");
printf("ItoI:          3*4                 9*4(read 6*4)\n");
printf("PtoI invert:   11*4                13*4    \n");
printf("ItoI invert:   12*4                14*4    \n");

*/
int hardWareEncoding(int v, int ma, int tbsid, int capno)
{
    int vfd = 0;
    int reg = 0;
    int val = 0;
    int p2i = 0;
    char camer_device[128] = {'0'};
    sprintf(camer_device,"/dev/video%d",v);
    if((ma >= 0)&&(ma <=3)){
        val = 1;
        p2i = (ma+1);
    }else if((ma>=4) && (ma <= 7)){
        val = 0;
        p2i = ma%4 +1;
    }else {
        return 0;
    }
    vfd = open(camer_device, O_RDWR|O_NONBLOCK, 0);
    //tbsread(vfd,32,tmp);
    if( 6312 == tbsid){
        switch(p2i){
        case 1:
            if(0 == capno){
                reg = 2*4;
            }else if(1 == capno){
                reg = 8*4;
            }else{
                close(vfd);
                return 0;
            }
            break;
        case 2:
            if(0 == capno){
                reg = 3*4;
            }else if(1 == capno){
                reg = 9*4;
            }else{
                close(vfd);
                return 0;
            }
            break;
        case 3:
            reg = 11*4;
            if(0 == capno){
                reg = 11*4;
            }else if(1 == capno){
                reg = 13*4;
            }else{
                close(vfd);
                return 0;
            }
            break;
        case 4:
            if(0 == capno){
                reg = 12*4;
            }else if(1 == capno){
                reg = 14*4;
            }else{
                close(vfd);
                return 0;
            }
            break;
        default:
            close(vfd);
            return 0;
            break;
        }
    }else if((6314 == tbsid)||(6324 == tbsid)){
        switch(p2i){
        case 1:
            reg = 2*4;
            break;
        case 2:
            reg = 3*4;
            break;
        case 3:
            reg = 11*4;
            break;
        case 4:
            reg = 12*4;
            break;
        default:
            close(vfd);
            return 0;
            break;
        }
    }else{
        close(vfd);
        return 0;
    }
    tbswrite(vfd,reg,val);
    close(vfd);
    return 0;
}

int setAuto_or_Manual(int v, int autoflg, int tbsid, int capno)
{
    int vfd = 0;
    int reg = 0;
    int val = autoflg;
    char camer_device[128] = {'0'};
    sprintf(camer_device,"/dev/video%d",v);
    vfd = open(camer_device, O_RDWR|O_NONBLOCK, 0);
    //tbsread(vfd,32,tmp);
    if( 6312 == tbsid){
        if(0 == capno){
            reg = 15*4;
        }else if(1 == capno){
            reg = 16*4;
        }else {
            close(vfd);
            return 0;
        }
    }else if((6314 == tbsid)||(6324 == tbsid)){
        if((capno >= 0)&&(capno <= 3)){
            reg = 15*4;
        }else {
            close(vfd);
            return 0;
        }
    }else{
        close(vfd);
        return 0;
    }
    tbswrite(vfd,reg,val);
    close(vfd);
    return 0;
}


int getAuto_or_Manual(int v,int*val,int tbsid, int capno)
{
    int vfd = 0;
    int reg = 0;
    char camer_device[128] = {'0'};
    sprintf(camer_device,"/dev/video%d",v);
    vfd = open(camer_device, O_RDWR|O_NONBLOCK, 0);
    //tbsread(vfd,32,tmp);
    if( 6312 == tbsid){
        if(0 == capno){
            reg = 15*4;
        }else if(1 == capno){
            reg = 16*4;
        }else {
            close(vfd);
            return 0;
        }
    }else if((6314 == tbsid)||(6324 == tbsid)){
        if((capno >= 0)&&(capno <= 3)){
            reg = 15*4;
        }else {
            close(vfd);
            return 0;
        }
    }else{
        close(vfd);
        return 0;
    }
    tbsread(vfd,reg,val);
    close(vfd);
    return 0;
}
int getP2IStatus(int v, int tbsid, int capno)
{
    int vfd = 0;
    int reg = 0;
    int val = 0;
    char camer_device[128] = {'0'};
    sprintf(camer_device,"/dev/video%d",v);
    vfd = open(camer_device, O_RDWR|O_NONBLOCK, 0);
    //tbsread(vfd,32,tbsid);
    if( 6312 == tbsid){
        if(0 == capno){
            reg = 2*4;
        }else if(1 == capno){
            reg = 5*4;
        }else{
            close(vfd);
            return val;
        }
    }else if((6314 == tbsid)||(6324 == tbsid)){
        if((capno < 0) || (capno >3)){
            close(vfd);
            return val;
        }
        reg = 2*4;
    }else{
        close(vfd);
        return val;
    }
    tbsread(vfd,reg,&val);
    close(vfd);
    return val;
}



static int getMediaInf(CAPARG_t *arg)
{
    VideoState* is = &mVideoState[arg->no];
    AVInputFormat *vInputFmt;
    AVFormatContext *vpFormatCtx;
    AVInputFormat *aInputFmt;
    AVFormatContext *apFormatCtx;
    char* vinput_name= "video4linux2";
    char* ainput_name= "alsa";
    char vfile_path[20] = {'\0'};
    char afile_path[20] = {'\0'};
    sprintf(vfile_path,"/dev/video%d",arg->v);
    sprintf(afile_path,"hw:%d",arg->a);
    memset(is,0,sizeof(VideoState)); //为了安全起见  先将结构体的数据初始化成0了
    is->no = arg->no;
    //0
    // int capfps = getFps(arg->v);
    int pi = getPI(arg->v);
    int dop2i = getP2IStatus(arg->v,arg->boa,arg->cap);
    int automa = 0;
    getAuto_or_Manual(arg->v,&automa,arg->boa, arg->cap);
    char ch_pi[24] = {'\0'};
    if(4 == pi){
        strcpy(ch_pi,"Interlaced");
        //ch_pi = 'I';
    }else if(7 == pi){
        //ch_pi = 'P';
        strcpy(ch_pi,"Progressive");
    }
    char ch_p2i[8] = {'\0'};
    if(1 == dop2i){
        strcpy(ch_p2i,"(P->I)");
    }
    // printf("ch_p2i:%s %d",ch_p2i,dop2i);

    //1. av_find_input_format (input_name);
    vInputFmt = av_find_input_format(vinput_name);
    if (NULL == vInputFmt)    {
        printf("can not find_vinput_format\n");
        return -1;
    }
    aInputFmt = av_find_input_format(ainput_name);
    if (NULL == vInputFmt)    {
        printf("can not find_ainput_format\n");
        return -1;
    }
    //2.avformat_open_input
    //Allocate an AVFormatContext.
    vpFormatCtx = avformat_alloc_context();
    apFormatCtx = avformat_alloc_context();
    if (avformat_open_input(&vpFormatCtx, vfile_path, vInputFmt, NULL) < 0) {
        printf("can't open the vfile. \n");
        return -1;
    }
    if (avformat_open_input(&apFormatCtx, afile_path, aInputFmt, NULL) < 0) {
        printf("can't open the afile. \n");
        return -1;
    }
    //3.avformat_find_stream_info
    if (avformat_find_stream_info(vpFormatCtx, NULL) < 0) {
        printf("Could't find vstream infomation.\n");
        return -1;
    }
    if (avformat_find_stream_info(apFormatCtx, NULL) < 0) {
        printf("Could't find astream infomation.\n");
        return -1;
    }
    //4.av_dump_format
    av_dump_format(vpFormatCtx, 0, vfile_path, 0);
    if(1 == automa){
        printf("%s %s auto mode\n",ch_pi,ch_p2i);
    }else if(0 == automa){
        printf("%s %s manual mode\n",ch_pi,ch_p2i);
    }
    av_dump_format(apFormatCtx, 0, afile_path, 0);
    avformat_close_input(&vpFormatCtx);
    avformat_close_input(&apFormatCtx);
    return 0;
}

int showMediaInformation(void)
{
    int i = 0;
    printf("Media Information: \n");
    while(-1 != gval.mainval.vid[i]){
        printf("************************\n");
        gval.caparg[i].v  = gval.mainval.vid[i];
        gval.caparg[i].a  = gval.mainval.aud[i];
        gval.caparg[i].boa = gval.mainval.boa[i];
        gval.caparg[i].cap = gval.mainval.cap[i];
        gval.caparg[i].no = i;
        getMediaInf(&gval.caparg[i]);
        i++;
        if(i >= 4){
            break;
        }
    }
    return 0;
}

int autoAndmaual(int autoflg,int ma)
{
    //setAuto_or_Manual();
    int i = 0;
    while(-1 != gval.mainval.vid[i]){
        setAuto_or_Manual(gval.mainval.vid[i],
                          autoflg,
                          gval.mainval.boa[i],
                          gval.mainval.cap[i]);
        if(autoflg  == 0){
            hardWareEncoding(gval.mainval.vid[i],
                             ma,
                             gval.mainval.boa[i],
                             gval.mainval.cap[i]);
        }
        i++;
        if(i >= 4){
            break;
        }
    }
    return 0;
}

int init_filters(AVCodecContext *dec_ctx)
{
    char args[512] = {'\0'};
    int ret = 0;
    //buffer过滤器--->yadif过滤器--->buffersink过滤器
    //过滤器相关的结构体:
    //AVFilterGraph: 管理所有的过滤器图像
    //AVFilterContext: 过滤器上下文
    //AVFilter: 过滤器
    //下面来看如何创建过滤器链:
    //第一步,创建AVFilterGraph
    gval.filter_graph=avfilter_graph_alloc();

    //第二步,获取要使用的过滤器:
    //AVFilter *filter_buffer=avfilter_get_by_name("buffer");
    //AVFilter *filter_yadif=avfilter_get_by_name("yadif");
    //AVFilter *filter_buffersink=avfilter_get_by_name("buffersink");
    //第三步,创建过滤器上下文,即AVFilterContext:
    //int avfilter_graph_create_filter(AVFilterContext **filt_ctx, const AVFilter *filt,
    //                                 const char *name, const char *args, void *opaque,
    //                                AVFilterGraph *graph_ctx);
    //参数说明:filt_ctx用来保存创建好的过滤器上下文,filt是过滤器,name是过滤器名称(在过滤器链中应该唯一),
    //args是传给过滤器的参数(每个过滤器不同,可以在相应的过滤器代码找到),opaque在代码中没有被使用,graph_ctx是过滤器图像管理指针.例:
    //创建buffer过滤器
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
             dec_ctx->time_base.num, dec_ctx->time_base.den,
             dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

    avfilter_graph_create_filter(&gval.filter_buffer_ctx, avfilter_get_by_name("buffer"), "in",
                                 args, NULL, gval.filter_graph);
    //创建yadif过滤器
    avfilter_graph_create_filter(&gval.filter_yadif_ctx, avfilter_get_by_name("yadif"), "yadif",
                                 // "mode=send_frame:parity=auto:deint=interlaced", NULL, gval.filter_graph);
                                 "mode=send_frame:parity=auto:deint=all", NULL, gval.filter_graph);
    // avfilter_graph_create_filter(&gval.filter_yadif_ctx, avfilter_get_by_name("kerndeint"), "kerndeint",
    //                                      "thresh=0:map=0:order=1:sharp=0:twoway=0", NULL, gval.filter_graph);
    //  printf("kerndeint %d\n",re);
    //创建buffersink过滤器
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_UYVY422, AV_PIX_FMT_NONE };
    avfilter_graph_create_filter(&gval.filter_buffersink_ctx, avfilter_get_by_name("buffersink"), "out",
                                 NULL, NULL,gval.filter_graph);
    av_opt_set_int_list(gval.filter_buffersink_ctx, "pix_fmts", pix_fmts,
                        AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    //第四步,连接过滤器
    avfilter_link(gval.filter_buffer_ctx, 0, gval.filter_yadif_ctx, 0);
    avfilter_link(gval.filter_yadif_ctx, 0, gval.filter_buffersink_ctx, 0);
    //第五步,检查所有配置是否正确:
    if ((ret = avfilter_graph_config(gval.filter_graph, NULL)) < 0){
        printf("avfilter_graph_config:%d\n",ret);
    }

    //注意上面所有的函数都应该检查返回值,这里是略写,到这里如果没出错的话,过滤器链就创建好了.
    //如何使用过滤器链进行过滤,主要是使用两个函数:
    //将解码后的frame推送给过滤器链
    //  int av_buffersrc_add_frame_flags(AVFilterContext *buffer_src,
    //                                     AVFrame *frame, int flags);
    //将处理完的frame拉取出来:
    //  int av_buffersink_get_frame(AVFilterContext *ctx, AVFrame *frame);
    /*
    //例如:
    av_buffersrc_add_frame_flags(filter_buffer_ctx, orgin_frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    while(1){
        ret = av_buffersink_get_frame(filter_buffersink_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
            break;
        }
        display(frame);
    }
*/
    // avfilter_graph_free(&filter_graph);
    return 0;
}
