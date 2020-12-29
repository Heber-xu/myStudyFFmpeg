#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>

typedef struct Decoder
{
    AVCodec *decodec;
    AVCodecContext *decode_ctx;
} Decoder;

typedef struct SdlContext
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;

} SdlContext;

static int init_video_decoder(Decoder *decoder, AVCodecParameters *parameters)
{
    AVCodec *decodec = avcodec_find_decoder(parameters->codec_id);
    if (!decodec)
    {
        av_log(NULL, AV_LOG_ERROR, "init_video_decoder decodec == NULL.\n");
        return -1;
    }
    AVCodecContext *decode_ctx = avcodec_alloc_context3(decodec);
    if (!decode_ctx)
    {
        av_log(NULL, AV_LOG_ERROR, "decode_ctx == NULL.\n");
        return -2;
    }
    avcodec_parameters_to_context(decode_ctx, parameters);
    decoder->decodec = decodec;
    decoder->decode_ctx = decode_ctx;
    av_log(NULL, AV_LOG_INFO, "decoder setup after.\n");
    return 0;
}

static int destory_video_decoder()
{
    return 0;
}

static int init_sdl2(SdlContext *sdl_ctx)
{

    //窗口大小
    // int screen_w = 640;
    // int screen_h = 480;
    int screen_w = 852;
    int screen_h = 480;

    int ret = SDL_Init(SDL_INIT_VIDEO);
    av_log(NULL, AV_LOG_INFO, "SDL_Init ret = %d.\n", ret);
    if (ret)
    {
        return -1;
    }

    SDL_Window *window = SDL_CreateWindow("player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateWindow NULL.\n");
        return -2;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer)
    {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateRenderer NULL.\n");
        return -3;
    }

    int pixformat = SDL_PIXELFORMAT_IYUV;
    SDL_Texture *texture = SDL_CreateTexture(renderer, pixformat, SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);
    if (!texture)
    {
        av_log(NULL, AV_LOG_ERROR, "SDL_CreateTexture NULL.\n");
        return -4;
    }

    sdl_ctx->window = window;
    sdl_ctx->renderer = renderer;
    sdl_ctx->texture = texture;

    return 0;
}

static int destory_sdl2(SdlContext *renderer)
{
    return 0;
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
    av_log(NULL, AV_LOG_INFO, "input_url = %s.\n", input_url);

    AVFormatContext *input_format_ctx = NULL;

    int ret;
    ret = avformat_open_input(&input_format_ctx, input_url, NULL, NULL);
    if (ret != 0)
    {
        av_log(NULL, AV_LOG_ERROR, "avformat_open_input error,ret = %d.\n", ret);
        goto end;
    }

    ret = avformat_find_stream_info(input_format_ctx, NULL);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "avformat_find_stream_info error,ret = %d.\n", ret);
        goto end;
    }

    int stream_num = input_format_ctx->nb_streams;
    av_log(NULL, AV_LOG_INFO, "stream_num = %d.\n", stream_num);
    if (stream_num < 1)
    {
        goto end;
    }

    Decoder *video_decoder = (Decoder *)malloc(sizeof(Decoder));
    int video_stream_index = 0;
    for (int i = 0; i < stream_num; i++)
    {

        AVStream *stream = input_format_ctx->streams[i];
        int index = stream->index;
        int id = stream->id;
        av_log(NULL, AV_LOG_INFO, "index = %d,id = %d.\n", index, id);
        AVCodecParameters *parameters = stream->codecpar;
        if (AVMEDIA_TYPE_VIDEO != parameters->codec_type)
        {
            continue;
        }

        video_stream_index = index;
        av_log(NULL, AV_LOG_INFO, "video_stream_index = %d\n", video_stream_index);

        ret = init_video_decoder(video_decoder, parameters);
        av_log(NULL, AV_LOG_INFO, "init_video_decoder ret = %d\n", ret);
        if (ret != 0)
        {
            goto end;
        }

        ret = avcodec_open2(video_decoder->decode_ctx, video_decoder->decodec, NULL);
        av_log(NULL, AV_LOG_INFO, "avcodec_open2 ret = %d.\n", ret);
    }

    SdlContext *sdl_ctx = (SdlContext *)malloc(sizeof(SdlContext));
    ret = init_sdl2(sdl_ctx);
    av_log(NULL, AV_LOG_INFO, "init_sdl2 ret = %d.\n", ret);

    int srcW = video_decoder->decode_ctx->width;
    int srcH = video_decoder->decode_ctx->height;
    enum AVPixelFormat srcFormat = video_decoder->decode_ctx->pix_fmt;
    // int dstW = 640;
    // int dstH = 480;
    int dstW = 852;
    int dstH = 480;
    enum AVPixelFormat dstFormat = AV_PIX_FMT_YUV420P;
    int sws_flags = SWS_FAST_BILINEAR;

    struct SwsContext *sws_ctx = sws_getContext(srcW, srcH, srcFormat, dstW, dstH, dstFormat, sws_flags, NULL, NULL, NULL);
    if (!sws_ctx)
    {
        av_log(NULL, AV_LOG_ERROR, "sws_ctx == NULL.\n");
    }
    else
    {
        av_log(NULL, AV_LOG_INFO, "sws_ctx != NULL.\n");
    }
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVPicture *picture = (AVPicture *)malloc(sizeof(AVPicture));
    ret = avpicture_alloc(picture, dstFormat, dstW, dstH);
    SDL_Rect rect;
    //for event
    SDL_Event event;

    av_log(NULL, AV_LOG_INFO, "avpicture_alloc ret = %d.\n", ret);

    while (av_read_frame(input_format_ctx, packet) >= 0)
    {
        // av_log(NULL, AV_LOG_INFO, "av_read_frame packet->stream_index = %d\n", packet->stream_index);
        if (packet->stream_index != video_stream_index)
        {
            continue;
        }

        ret = avcodec_send_packet(video_decoder->decode_ctx, packet);
        // av_log(NULL, AV_LOG_INFO, "avcodec_send_packet ret = %d\n", ret);
        if (ret == 0)
        {
            while (avcodec_receive_frame(video_decoder->decode_ctx, frame) == 0)
            {
                //todo 处理视频
                int64_t dts = frame->pkt_dts;
                int h = frame->height;
                int w = frame->width;
                av_log(NULL, AV_LOG_INFO, "avcodec_receive_frame dts = %lld,h = %d,w = %d.\n", dts, h, w);

                // Convert the image into YUV format that SDL uses
                sws_scale(sws_ctx, (uint8_t const *const *)frame->data, frame->linesize, 0, 480, picture->data, picture->linesize);
                ret = SDL_UpdateYUVTexture(sdl_ctx->texture, &rect,
                                           picture->data[0], picture->linesize[0],
                                           picture->data[1], picture->linesize[1],
                                           picture->data[2], picture->linesize[2]);
                av_log(NULL, AV_LOG_INFO, "SDL_UpdateYUVTexture ret = %d.\n", ret);

                rect.x = 0;
                rect.y = 0;
                rect.w = 852;
                rect.h = 480;

                SDL_RenderClear(sdl_ctx->renderer);
                SDL_RenderCopy(sdl_ctx->renderer, sdl_ctx->texture, NULL, &rect);
                SDL_RenderPresent(sdl_ctx->renderer);

                av_frame_unref(frame);
            }
        }
        else
        {
            char *err = av_err2str(ret);
            av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet err = %s.\n", err);
        }
        av_packet_unref(packet);

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

    goto end;

end:
    av_log(NULL, AV_LOG_INFO, "goto end.\n");
    if (input_format_ctx)
    {
        avformat_close_input(&input_format_ctx);
        avformat_free_context(input_format_ctx);
        input_format_ctx = NULL;
    }
    if (video_decoder)
    {
        if (video_decoder->decode_ctx)
        {
            avcodec_free_context(&video_decoder->decode_ctx);
            video_decoder->decode_ctx = NULL;
            free(video_decoder);
        }
        video_decoder = NULL;
    }
    if (packet)
    {
        av_free_packet(packet);
    }
    if (frame)
    {
        av_frame_free(&frame);
    }
    return 0;
}