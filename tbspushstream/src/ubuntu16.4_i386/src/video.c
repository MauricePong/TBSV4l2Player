#include "video.h"
GLOBALVAL_t gval;
VideoState  mVideoState[DEV_NO];

void DataQuene_Input(BufferQueue *bq,uint8_t * buffer,int size)
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
    bq->size += size;
    bq->DataQueneTail = node;
    SDL_UnlockMutex(bq->Mutex);
}

BufferDataNode *DataQuene_get(BufferQueue *bq)
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
        bq->size -= node->bufferSize;
    }
    SDL_UnlockMutex(bq->Mutex);
    return node;
}

void buffer_queue_flush(BufferQueue *bq)
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

void buffer_queue_deinit(BufferQueue *bq) {
    buffer_queue_flush(bq);
    SDL_DestroyMutex(bq->Mutex);
}

void buffer_queue_init(BufferQueue *bq) {
    memset(bq, 0, sizeof(BufferQueue));
    bq->Mutex = SDL_CreateMutex();
    bq->DataQueneHead = NULL;
    bq->DataQueneTail = NULL;
    bq->size = 0;
}

void packet_queue_flush(PacketQueue *q)
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

void packet_queue_deinit(PacketQueue *q) {
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

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
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



int read_video_thread(void *arg)
{
    SDL_SemWait(gval.m_vlock);
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
        if ((is->videoq.size > MAX_VIDEO_SIZE)||(is->videobq.size > MAX_VIDEO_SIZE)) {
          //  printf("is->videoq.size > MAX_VIDEO_SIZE\n");
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
    SDL_SemPost(gval.m_vlock);
    return 0;
}

int recoding_video_thread(void *arg)
{
    SDL_SemWait(gval.m_rlock);
    VideoState *is = (VideoState *) arg;
    is->recoding_video_tid_flg = 0;
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
    numBytes = avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width,pCodecCtx->height);
    out_buffer_yuv = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    avpicture_fill((AVPicture *) pFrameYUV, out_buffer_yuv, AV_PIX_FMT_YUV420P,
                   pCodecCtx->width, pCodecCtx->height);
    if(4 == is->pi){
        init_filters(is,pCodecCtx);
    }
    while(1)
    {
        if (gval.quit)
        {
            packet_queue_flush(&is->videoq); //清空队列
            break;
        }

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
        ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture,packet);
        if (ret < 0) {
            printf("decode error.\n");
            av_free_packet(packet);
            SDL_Delay(10);
            continue;
        }
        if (got_picture) {
            //            sws_scale(img_convert_ctx_yuv,
            //                      (uint8_t const * const *) pFrame->data,
            //                      pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data,
            //                      pFrameYUV->linesize);

            if(4 == is->pi){
                if (av_buffersrc_add_frame_flags(is->filter_buffer_ctx, pFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                    break;
                }
                while (1) {
                    ret = av_buffersink_get_frame(is->filter_buffersink_ctx, filtersFrame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                        break;
                    }
                    if (ret < 0){
                        return -1;
                    }
                }
                sws_scale(img_convert_ctx_yuv,
                          (uint8_t const * const *) filtersFrame->data,
                          filtersFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data,
                          pFrameYUV->linesize);

               // DataQuene_Input(&is->videobq,pFrameYUV->data[0],pFrameYUV->linesize[0]);
                DataQuene_Input(&is->videobq,out_buffer_yuv,numBytes);
                av_frame_unref(filtersFrame);
            }else if(7 == is->pi){
                sws_scale(img_convert_ctx_yuv,
                          (uint8_t const * const *) pFrame->data,
                          pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data,
                          pFrameYUV->linesize);
                DataQuene_Input(&is->videobq,out_buffer_yuv,numBytes);
            }
        }
        av_free_packet(packet);
    }
    avfilter_graph_free(&is->filter_graph);
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
    is->recoding_video_tid_flg = 1;
    SDL_SemPost(gval.m_rlock);
    return 0;
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
    // c->codec_id = AV_CODEC_ID_AAC;

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

static void open_audio(VideoState *is,AVFormatContext *oc, AVStream *st)
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
    AVDictionary *dictParam = NULL;
    // av_dict_set(&dictParam, "c:a", "aac",0);

    /* open it */
    if (avcodec_open2(c, codec, &dictParam) < 0) {
        printf("could not open codec\n");
        return;
    }
    /* init signal generator */
    // gval.t = 0;
    // gval.tincr = 2 * M_PI * 110.0 / c->sample_rate;
    /* increment frequency by 110 Hz per second */
    //gval.tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;
    is->audio_outbuf_size = 10000;
    is->audio_outbuf = (uint8_t *)av_malloc(is->audio_outbuf_size);
    if (c->frame_size <= 1) {
        is->audio_input_frame_size = is->audio_outbuf_size / c->channels;
        switch(st->codec->codec_id) {
        case AV_CODEC_ID_PCM_S16LE:
        case AV_CODEC_ID_PCM_S16BE:
        case AV_CODEC_ID_PCM_U16LE:
        case AV_CODEC_ID_PCM_U16BE:
            is->audio_input_frame_size >>= 1;
            break;
        default:
            break;
        }
    } else {
        is->audio_input_frame_size = c->frame_size;
    }
    is->samples = (int16_t *)av_malloc(is->audio_input_frame_size * 2 * c->channels);
}

static void write_audio_frame( VideoState *is ,AVFormatContext *oc, AVStream *st)
{

    AVCodecContext *c;
    AVPacket packet;
    AVPacket pkt;
    av_init_packet(&pkt);
    c = st->codec;
    if(packet_queue_get(&is->audioq, &packet, 0)<= 0){
        SDL_Delay(1); //延时1ms
        //printf()<<"no get audioq data";
        return;
    }else{
        memcpy(is->samples,packet.data, packet.size);
        av_free_packet(&packet);
    }
    //  BufferDataNode *node = DataQuene_get(&is->audioq);
    //    if (node == NULL)
    //   {
    //     SDL_Delay(1); //延时1ms
    //printf()<<"no get audioq data";
    //    return;
    // }
    //  else
    //    {
    //      memcpy(is->samples,node->buffer, node->bufferSize);
    //      free(node->buffer);
    //     free(node);
    // }
    //    fread(samples, 1, audio_input_frame_size*4, pcmInFp);

    pkt.size = avcodec_encode_audio(c, is->audio_outbuf, is->audio_outbuf_size, is->samples);
    if (c->coded_frame && c->coded_frame->pts != AV_NOPTS_VALUE)
        pkt.pts= av_rescale_q(c->coded_frame->pts, c->time_base, st->time_base);
    pkt.flags |= AV_PKT_FLAG_KEY;
    pkt.stream_index = st->index;
    pkt.data = is->audio_outbuf;
    /* write the compressed frame in the media file */

    if (av_interleaved_write_frame(oc, &pkt) != 0) {
        fprintf(stderr, "Error while writing audio frame\n");
        exit(1);
    }

}

static void close_audio(VideoState *is,AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);
    av_free(is->samples);
    av_free(is->audio_outbuf);
}

static void close_video(VideoState *is,AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);
    av_free(is->picture->data[0]);
    av_free(is->picture);
    av_free(is->video_outbuf);
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
    // c->codec_id = AV_CODEC_ID_H264;
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
    //  c->qmin = 0;
    //  c->qmax = 0;
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

static void open_video(VideoState *is,AVFormatContext *oc, AVStream *st)
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
  //  av_dict_set(&dictParam, "preset", "ultrafast",0);
    //av_dict_set(&dictParam, "preset", "superfast",0);
    //av_dict_set(&dictParam, "preset", "veryfast",0);
    av_dict_set(&dictParam, "preset", "fast",0);

    //av_dict_set(&dictParam, "c:v", "h264",0);

    //  av_dict_set(&dictParam,"profile","main",0);
    // }
    /* open the codec */
    if (avcodec_open2(c, codec, &dictParam) < 0) {
        printf("could not open codec\n");
        return;
    }
    is->video_outbuf = NULL;
    if (!(oc->oformat->flags & AVFMT_RAWPICTURE)) {
        is->video_outbuf_size = 1920*1080*2;
        is->video_outbuf = (uint8_t *)av_malloc(is->video_outbuf_size);
    }
    /* allocate the encoded raw picture */
    is->picture = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!is->picture) {
        printf("Could not allocate picture\n");
        return;
    }
    is->picture_buf = (uint8_t *)av_malloc(c->width * c->height *4);
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
        memcpy(is->picture_buf,node->buffer, y_size*3/2);
        free(node->buffer);
        free(node);
        is->picture->data[0] = is->picture_buf;  // 亮度Y
        is->picture->data[1] = is->picture_buf+ y_size;  // U
        is->picture->data[2] = is->picture_buf+ y_size*5/4; // V
        is->picture->width = c->width;
        is->picture->height = c->height;
        is->picture->format = c->pix_fmt;
    }
    if (oc->oformat->flags & AVFMT_RAWPICTURE) {
        /* raw video case. The API will change slightly in the near
           future for that. */
        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.flags |= AV_PKT_FLAG_KEY;
        pkt.stream_index = st->index;
        pkt.data = (uint8_t *)is->picture;
        pkt.size = sizeof(AVPicture);
        ret = av_interleaved_write_frame(oc, &pkt);
    } else {
        /* encode the image */
        out_size = avcodec_encode_video(c, is->video_outbuf, is->video_outbuf_size, is->picture);
        // printf("out:%d\n",out_size);
        /* if zero size, it means the image was buffered */
        if (out_size > 0) {
            AVPacket pkt;
            av_init_packet(&pkt);
            if (c->coded_frame->pts != AV_NOPTS_VALUE)
                pkt.pts= av_rescale_q(c->coded_frame->pts, c->time_base, st->time_base);
            if(c->coded_frame->key_frame)
                pkt.flags |= AV_PKT_FLAG_KEY;

            pkt.stream_index = st->index;
            pkt.data = is->video_outbuf;
            pkt.size = out_size;
            /* write the compressed frame in the media file */

            ret = av_interleaved_write_frame(oc, &pkt);

            is->picture->pts++;
        } else {
            ret = 0;
        }
    }
    if (ret != 0) {
        fprintf(stderr, "Error while writing video frame\n");
        exit(1);
    }

}


int encode_thread(void *arg)
{
    SDL_SemWait(gval.m_elcok);
    int i;
    char filename[128] = {'\0'};
    AVOutputFormat *fmt;
    AVFormatContext *oc;
    AVStream *audio_st, *video_st;
    double audio_pts, video_pts;
    VideoState *is = (VideoState *) arg;
    is->encode_tid_flg = 0;
    strcpy(filename,is->rtp);
    /* allocate the output media context */
    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
    if (!oc) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&oc, NULL, "rtp_mpegts", filename);
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
        open_video(is,oc, video_st);
    if (audio_st)
        open_audio(is,oc, audio_st);

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
    is->picture->pts = 0;
    while(1)
    {
        if (gval.quit)
        {
            buffer_queue_flush(&is->videobq); //清空队列
            // buffer_queue_flush(&is->audioq);

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

        //printf("apts:%lf, vpts:%lf\n",audio_pts,video_pts);
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
        close_video(is,oc, video_st);
    if (audio_st)
        close_audio(is,oc, audio_st);
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
    SDL_SemPost(gval.m_elcok);
    return 0;
}



int child_rtp_sendTS(void *arg)
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
    is->v = cap->v;
    is->a = cap->a;
    is->no = cap->no;
    sprintf(vfile_path,"/dev/video%d",is->v);
    sprintf(afile_path,"hw:%d",is->a);
    int capfps = getFps(is->v);
    is->pi = getPI(is->v);
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
    // if (audioStream >= 0) {
    /* 所有设置SDL音频流信息的步骤都在这个函数里完成 */
    //     audio_stream_component_open(is, audioStream);
    // }
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
    buffer_queue_init(&is->videobq);
    //if(4 == is->pi){
    //    is->timebase_den = capfps/2+capfps%2;
    //}else if(7 == is->pi){
        is->timebase_den = capfps;
    //}
    sprintf(is->rtp,"rtp://%s",cap->rtp);
    printf("%s\n",is->rtp);
    ///创建一个线程专门用来解码视频
    is->recoding_video_tid = SDL_CreateThread(recoding_video_thread, "recoding_video_thread", is);
    AVPacket *apacket = (AVPacket *) malloc(sizeof(AVPacket)); //分配一个packet 用来存放读取的视频
    is->read_video_tid = SDL_CreateThread(read_video_thread, "read_video_thread", is);
    is->encode_tid = SDL_CreateThread(encode_thread, "encode_thread", is);

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
        if((1 == is->recoding_video_tid_flg)&&(1 == is->read_video_tid_flg)&&(1 == is->encode_tid_flg)){
            SDL_WaitThread(is->recoding_video_tid, NULL);
            SDL_WaitThread(is->read_video_tid, NULL);
            SDL_WaitThread(is->encode_tid, NULL);
            is->recoding_video_tid_flg = 0;
            is->encode_tid_flg = 0;
            is->read_video_tid_flg = 0;
            break;
        }else{
            SDL_Delay(10);
        }
    }
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
    SDL_SemPost(gval.m_lock);
    return 0;
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
                    SDL_Quit();
                    gval.quit = 1;
                }
            case SDL_QUIT:
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

int rtp_sendTS()
{
    int i = 0;
    if(SDL_Init(SDL_INIT_TIMER)) {
        printf( " initialize SDL - %s\n", SDL_GetError());
        return -1;
    }
   // SDL_CreateThread(keydone_thread, "keydone_thread", NULL);
    for(i = 0;i < gval.mainval.devcount;i++){
        gval.caparg[i].v  = gval.mainval.vid[i];
        gval.caparg[i].a  = gval.mainval.aud[i];
        gval.caparg[i].no = i;
        strcpy(gval.caparg[i].rtp,gval.mainval.rtp[i]);
        SDL_CreateThread(child_rtp_sendTS, "child_rtp_sendTS", &gval.caparg[i]);
        SDL_Delay(1000);
    }

    while(1 !=  gval.quit){
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

int init_filters(VideoState *is,AVCodecContext *dec_ctx)
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
    is->filter_graph=avfilter_graph_alloc();

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

    avfilter_graph_create_filter(&is->filter_buffer_ctx, avfilter_get_by_name("buffer"), "in",
                                 args, NULL, is->filter_graph);
    //创建yadif过滤器
    avfilter_graph_create_filter(&is->filter_yadif_ctx, avfilter_get_by_name("yadif"), "yadif",
                                 // "mode=send_frame:parity=auto:deint=interlaced", NULL, gval.filter_graph);
                                 "mode=send_frame:parity=auto:deint=all", NULL, is->filter_graph);
    // avfilter_graph_create_filter(&gval.filter_yadif_ctx, avfilter_get_by_name("kerndeint"), "kerndeint",
    //                                      "thresh=0:map=0:order=1:sharp=0:twoway=0", NULL, gval.filter_graph);
    //  printf("kerndeint %d\n",re);
    //创建buffersink过滤器
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_UYVY422, AV_PIX_FMT_NONE };
    avfilter_graph_create_filter(&is->filter_buffersink_ctx, avfilter_get_by_name("buffersink"), "out",
                                 NULL, NULL,is->filter_graph);
    av_opt_set_int_list(is->filter_buffersink_ctx, "pix_fmts", pix_fmts,
                        AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    //第四步,连接过滤器
    avfilter_link(is->filter_buffer_ctx, 0, is->filter_yadif_ctx, 0);
    avfilter_link(is->filter_yadif_ctx, 0, is->filter_buffersink_ctx, 0);
    //第五步,检查所有配置是否正确:
    if ((ret = avfilter_graph_config(is->filter_graph, NULL)) < 0){
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

