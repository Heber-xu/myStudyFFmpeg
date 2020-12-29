#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
#include <string.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

SwrContext *swr_ctx;

typedef struct PacketQueue
{
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

PacketQueue audioq;
int quit = 0;

void packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    AVPacketList *pkt1;
    if (av_dup_packet(pkt) < 0)
    {
        return -1;
    }
    pkt1 = av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)
    {
        q->first_pkt = pkt1;
    }
    else
    {
        q->last_pkt->next = pkt1;
    }

    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
    return 0;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;)
    {
        if (quit)
        {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1)
        {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        }
        else if (!block)
        {
            ret = 0;
            break;
        }
        else
        {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size)
{

    static AVPacket pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    static AVFrame frame;

    int len1, data_size = 0;

    for (;;)
    {
        while (audio_pkt_size > 0)
        {
            int got_frame = 0;
            len1 = avcodec_decode_audio4(aCodecCtx, &frame, &got_frame, &pkt);
            if (len1 < 0)
            {
                /* if error, skip frame */
                audio_pkt_size = 0;
                break;
            }
            audio_pkt_data += len1;
            audio_pkt_size -= len1;
            data_size = 0;
            if (got_frame)
            {
                //fprintf(stderr, "channels:%d, nb_samples:%d, sample_fmt:%d \n", aCodecCtx->channels, frame.nb_samples, aCodecCtx->sample_fmt);
                /*
	data_size = av_samples_get_buffer_size(NULL, 
					       aCodecCtx->channels,
					       frame.nb_samples,
					       aCodecCtx->sample_fmt,
					       1);
        */
                data_size = 2 * 2 * frame.nb_samples;

                // assert(data_size <= buf_size);
                swr_convert(swr_ctx,
                            &audio_buf,
                            MAX_AUDIO_FRAME_SIZE * 3 / 2,
                            (const uint8_t **)frame.data,
                            frame.nb_samples);

                //memcpy(audio_buf, frame.data[0], data_size);
            }
            if (data_size <= 0)
            {
                /* No data yet, get more frames */
                continue;
            }
            /* We have data, return it and come back for more later */
            return data_size;
        }
        if (pkt.data)
            av_free_packet(&pkt);

        if (quit)
        {
            return -1;
        }

        if (packet_queue_get(&audioq, &pkt, 1) < 0)
        {
            return -1;
        }
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
    }
}

//stream：A pointer to the audio data buffer.
//len：The length of that buffer in bytes
static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    av_log(NULL, AV_LOG_INFO, "audio_callback.\n");

    AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
    int len1, audio_size;

    static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    while (len > 0)
    {
        if (audio_buf_index >= audio_buf_size)
        {

            /* We have already sent all our data; get more */
            audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
            if (audio_size < 0)
            {
                /* If error, output silence */
                audio_buf_size = 1024; // arbitrary?
                memset(audio_buf, 0, audio_buf_size);
            }
            else
            {
                audio_buf_size = audio_size;
            }
            audio_buf_index = 0;
        }
        len1 = audio_buf_size - audio_buf_index;
        if (len1 > len)
            len1 = len;
        // fprintf(stderr, "index=%d, len1=%d, len=%d\n",
        //         audio_buf_index,
        //         len,
        //         len1);
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        // SDL_MixAudio(stream,(const Uint8 *)audio_buf + audio_buf_index,len1,SDL_MIX_MAXVOLUME);
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
}

int main(int argc, char *argv[])
{

    if (argc != 2)
    {
        av_log(NULL, AV_LOG_ERROR, "without input url.\n");
        return -1;
    }

    av_register_all();

    char *input_url = argv[1];
    int ret = 0;

    //step 1：打开输入文件
    //1、打开输入文件
    AVFormatContext *input_format_ctx = NULL;
    ret = avformat_open_input(&input_format_ctx, input_url, NULL, NULL);
    if (ret != 0)
    {
        av_log(NULL, AV_LOG_ERROR, "avformat_open_input error,ret = %d.\n", ret);
        goto end;
    }
    //2、完善流信息
    ret = avformat_find_stream_info(input_format_ctx, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "avformat_find_stream_info error,ret = %d.\n", ret);
        goto end;
    }

    //step 2：初始化解码器（音、视频解码器）
    //1、找到音、视频相关信息
    int video_stream_index;
    int audio_stream_index;
    AVCodecParameters *video_codecpar;
    AVCodecParameters *audio_codecpar;
    int stream_num = input_format_ctx->nb_streams;
    for (int i = 0; i < stream_num; i++)
    {
        AVStream *stream = input_format_ctx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream_index = stream->index;
            video_codecpar = stream->codecpar;
        }
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_stream_index = stream->index;
            audio_codecpar = stream->codecpar;
        }
    }
    av_log(NULL, AV_LOG_INFO, "video_stream_index = %d,audio_stream_index = %d.\n", video_stream_index, audio_stream_index);
    //2、完善视频解码器
    AVCodec *video_codec = NULL;
    AVCodecContext *video_codec_ctx = NULL;
    video_codec = avcodec_find_decoder(video_codecpar->codec_id);
    if (!video_codec)
    {
        av_log(NULL, AV_LOG_ERROR, "video_codec == NULL.\n");
        goto end;
    }
    video_codec_ctx = avcodec_alloc_context3(video_codec);
    if (!video_codec_ctx)
    {
        av_log(NULL, AV_LOG_ERROR, "video_codec_ctx == NULL.\n");
        goto end;
    }
    avcodec_parameters_to_context(video_codec_ctx, video_codecpar);
    //3、完善音频解码器
    AVCodec *audio_codec = NULL;
    AVCodecContext *audio_codec_ctx = NULL;
    audio_codec = avcodec_find_decoder(audio_codecpar->codec_id);
    if (!audio_codec)
    {
        av_log(NULL, AV_LOG_ERROR, "audio_codec == NULL.\n");
        goto end;
    }
    audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    if (!audio_codec_ctx)
    {
        av_log(NULL, AV_LOG_ERROR, "audio_codec_ctx == NULL.\n");
        goto end;
    }
    avcodec_parameters_to_context(audio_codec_ctx, audio_codecpar);
    //4、打开视频解码器
    ret = avcodec_open2(video_codec_ctx, video_codec, NULL);
    if (ret)
    {
        av_log(NULL, AV_LOG_ERROR, "avcodec_open2 video ret = %d.\n", ret);
        goto end;
    }
    //5、打开音频解码器
    ret = avcodec_open2(audio_codec_ctx, audio_codec, NULL);
    if (ret)
    {
        av_log(NULL, AV_LOG_ERROR, "avcodec_open2 audio ret = %d.\n", ret);
        goto end;
    }

    //step 3：初始化渲染器
    //1、视频
    int screen_w = 640;
    int screen_h = 480;
    int pixformat = SDL_PIXELFORMAT_IYUV;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_Event event;
    SDL_Rect rect;
    ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS);
    if (ret)
    {
        av_log(NULL, AV_LOG_INFO, "SDL_Init ret = %d.\n", ret);
        goto end;
    }
    window = SDL_CreateWindow("player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateWindow NULL.\n");
        goto end;
    }
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer)
    {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateRenderer NULL.\n");
        goto end;
    }
    texture = SDL_CreateTexture(renderer, pixformat, SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);
    if (!texture)
    {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateTexture NULL.\n");
        goto end;
    }
    //2、音频
    SDL_AudioSpec spec;
    spec.freq = audio_codecpar->sample_rate;
    //audio_codecpar->format;
    spec.format = AUDIO_S16SYS;
    spec.channels = audio_codecpar->channels;
    spec.silence = 0;
    spec.samples = 1024;
    spec.callback = audio_callback;
    spec.userdata = audio_codec_ctx;
    ret = SDL_OpenAudio(&spec, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "can't open audio ret = %d.\n", ret);
        goto end;
    }
    packet_queue_init(&audioq);

    //step 4：初始化图像转换器
    int srcW = video_codecpar->width;
    int srcH = video_codecpar->height;
    enum AVPixelFormat srcFormat = video_codecpar->format;
    int dstW = 640;
    int dstH = 480;
    enum AVPixelFormat dstFormat = AV_PIX_FMT_YUV420P;
    // srcW：源图像的宽
    // srcH：源图像的高
    // srcFormat：源图像的像素格式
    // dstW：目标图像的宽
    // dstH：目标图像的高
    // dstFormat：目标图像的像素格式
    // flags：设定图像拉伸使用的算法
    struct SwsContext *sws_ctx = sws_getContext(srcW, srcH, srcFormat, dstW, dstH, dstFormat, SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx)
    {
        av_log(NULL, AV_LOG_ERROR, "sws_getContext NULL.\n");
        goto end;
    }
    AVPicture *output_picture = (AVPicture *)malloc(sizeof(AVPicture));
    avpicture_alloc(output_picture, dstFormat, dstW, dstH);

    //step 5：初始化音频重采样
    swr_ctx = swr_alloc();
    if (!swr_ctx)
    {
        av_log(NULL, AV_LOG_ERROR, "swr_alloc NULL.\n");
        goto end;
    }
    /* set options */
    int64_t src_ch_layout = audio_codecpar->channel_layout;
    int64_t dst_ch_layout = src_ch_layout;
    int src_rate = audio_codecpar->sample_rate;
    int dst_rate = src_rate;
    enum AVSampleFormat src_sample_fmt = audio_codecpar->format;
    enum AVSampleFormat dst_sample_fmt = AV_SAMPLE_FMT_S16;
    swr_alloc_set_opts(swr_ctx,
                       dst_ch_layout,
                       dst_sample_fmt,
                       dst_rate,
                       src_ch_layout,
                       src_sample_fmt,
                       src_rate,
                       0,
                       NULL);
    if ((ret = swr_init(swr_ctx)) < 0)
    {
        av_log(NULL, AV_LOG_INFO, "swr_init ret = %d.\n", ret);
        goto end;
    }

    //开始播放
    SDL_PauseAudio(0);

    //step 6：解码
    AVPacket *input_packet = av_packet_alloc();
    AVFrame *input_frame = av_frame_alloc();
    while (av_read_frame(input_format_ctx, input_packet) == 0)
    {
        //视频
        if (input_packet->stream_index == video_stream_index)
        {
            av_log(NULL, AV_LOG_INFO, "av_read_frame video.\n");
            ret = avcodec_send_packet(video_codec_ctx, input_packet);
            if (ret == 0)
            {
                while (avcodec_receive_frame(video_codec_ctx, input_frame) == 0)
                {
                    //渲染视频
                    // for (int i = 0; i < 8; i++)
                    // {
                    //     av_log(NULL, AV_LOG_INFO, "input_frame_linesize[%d] = %d,output_picture_linesize[%d] = %d.\n", i, input_frame->linesize[i], i, output_picture->linesize[i]);
                    // }
                    //参数1
                    //参数二：input_frame->data 输入数据（一帧图像数据），yuv420p是3个平面
                    //参数三：input_frame->linesize 输入数据不同平面每个平面的每行的个数
                    //参数四：源图像Y方向位置
                    //参数五：图像高度（为什么需要高度，C语言用指针表示数组，并不知道数组大小）
                    //参数六：输出图像存储数据
                    //参数七：输出图像不同平面每行的个数
                    sws_scale(sws_ctx, input_frame->data, input_frame->linesize, 0, input_frame->height, output_picture->data, output_picture->linesize);
                    // for (int i = 0; i < 3; i++)
                    // {
                    //     av_log(NULL, AV_LOG_INFO, "after sws_scale output_picture_linesize[%d] = %d.\n", i, output_picture->linesize[i]);
                    // }
                    SDL_UpdateYUVTexture(texture, NULL,
                                         output_picture->data[0], output_picture->linesize[0],
                                         output_picture->data[1], output_picture->linesize[1],
                                         output_picture->data[2], output_picture->linesize[2]);

                    // rect.x = 0;
                    // rect.y = 0;
                    // rect.w = dstW;
                    // rect.h = dstH;

                    SDL_RenderClear(renderer);
                    // SDL_RenderCopy(renderer, texture, NULL, &rect);
                    SDL_RenderCopy(renderer, texture, NULL, NULL);
                    SDL_RenderPresent(renderer);

                    // av_free_packet(&input_packet);
                }
            }
        }
        //音频
        else if (input_packet->stream_index == audio_stream_index)
        {
            av_log(NULL, AV_LOG_INFO, "av_read_frame audio.\n");
            packet_queue_put(&audioq, input_packet);
        }

        // Free the packet that was allocated by av_read_frame
        SDL_PollEvent(&event);
        switch (event.type)
        {
        case SDL_QUIT:
            // quit = 1;
            goto end;
            break;
        default:
            break;
        }
    }

end:
    if (input_format_ctx)
    {
        avformat_close_input(&input_format_ctx);
        avformat_free_context(input_format_ctx);
        input_format_ctx = NULL;
    }
    if (video_codec_ctx)
    {
        avcodec_free_context(&video_codec_ctx);
        video_codec_ctx = NULL;
    }
    if (audio_codec_ctx)
    {
        avcodec_free_context(&audio_codec_ctx);
        audio_codec_ctx = NULL;
    }

    //sdl释放
    if (texture)
    {
        SDL_DestroyTexture(texture);
    }
    if (renderer)
    {
        SDL_DestroyRenderer(renderer);
    }
    if (window)
    {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();

    if (sws_ctx)
    {
        sws_freeContext(sws_ctx);
        sws_ctx = NULL;
    }

    if (swr_ctx)
    {
        swr_free(swr_ctx);
        swr_ctx = NULL;
    }

    //
    if (input_packet)
    {
        av_packet_unref(input_packet);
        input_packet = NULL;
    }
    if (input_frame)
    {
        av_frame_free(&input_frame);
        input_frame = NULL;
    }
    return 0;
}