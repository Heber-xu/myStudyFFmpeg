#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>

typedef struct Decoder
{
    AVCodec *decodec;
    AVCodecContext *decode_ctx;
} Decoder;

static Uint8 *audio_chunk;
static Uint32 audio_len;
static Uint8 *audio_pos;
int pcm_buffer_size = 4096;

//回调函数，音频设备需要更多数据的时候会调用该回调函数
static void audio_callback(void *udata, Uint8 *stream, int len)
{
    SDL_memset(stream, 0, len);
    if (audio_len == 0)
        return;
    len = (len > audio_len ? audio_len : len);

    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
    audio_pos += len;
    audio_len -= len;
}

static int init_audio_decoder(Decoder *decoder, AVCodecParameters *parameters)
{
    AVCodec *decodec = avcodec_find_decoder(parameters->codec_id);
    if (!decodec)
    {
        av_log(NULL, AV_LOG_ERROR, "init_audio_decoder decodec == NULL.\n");
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

static int init_swr_ctx(SwrContext *swr_ctx)
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

    Decoder *audio_decoder = (Decoder *)malloc(sizeof(Decoder));
    int audio_stream_index = 0;
    for (int i = 0; i < stream_num; i++)
    {
        AVStream *stream = input_format_ctx->streams[i];
        int index = stream->index;
        int id = stream->id;
        av_log(NULL, AV_LOG_INFO, "index = %d,id = %d.\n", index, id);
        AVCodecParameters *parameters = stream->codecpar;
        if (AVMEDIA_TYPE_AUDIO != parameters->codec_type)
        {
            continue;
        }

        audio_stream_index = index;
        av_log(NULL, AV_LOG_INFO, "audio_stream_index = %d,format = %d\n", audio_stream_index, parameters->format);

        ret = init_audio_decoder(audio_decoder, parameters);
        av_log(NULL, AV_LOG_INFO, "init_audio_decoder ret = %d\n", ret);
        if (ret != 0)
        {
            goto end;
        }

        ret = avcodec_open2(audio_decoder->decode_ctx, audio_decoder->decodec, NULL);
        av_log(NULL, AV_LOG_INFO, "avcodec_open2 ret = %d.\n", ret);
    }

    //test.pcm 对应的信息如下
    SDL_AudioSpec spec;
    spec.freq = audio_decoder->decode_ctx->sample_rate;
    //AV_SAMPLE_FMT_FLTP
    spec.format = AUDIO_S16SYS;
    spec.channels = audio_decoder->decode_ctx->channels;
    spec.silence = 0;
    spec.samples = 1024;
    spec.callback = audio_callback;
    spec.userdata = NULL;

    if (SDL_OpenAudio(&spec, NULL) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "can't open audio.\n");
        return -1;
    }

    /* create resampler context */
    SwrContext *swr_ctx = swr_alloc();
    if (!swr_ctx)
    {
        fprintf(stderr, "Could not allocate resampler context\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    int src_ch_layout = audio_decoder->decode_ctx->channel_layout;
    int dst_ch_layout = src_ch_layout;
    int src_rate = audio_decoder->decode_ctx->sample_rate;
    int dst_rate = src_rate;
    enum AVSampleFormat src_sample_fmt = AV_SAMPLE_FMT_FLTP;
    enum AVSampleFormat dst_sample_fmt = AV_SAMPLE_FMT_S16;

    /* set options */
    av_opt_set_int(swr_ctx, "in_channel_layout", src_ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", src_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0);

    av_opt_set_int(swr_ctx, "out_channel_layout", dst_ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", dst_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);

    /* initialize the resampling context */
    if ((ret = swr_init(swr_ctx)) < 0)
    {
        av_log(NULL, AV_LOG_INFO, "swr_init ret = %d.\n", ret);
        goto end;
    }

    // uint8_t **ou
    int swr_convert(struct SwrContext *s, uint8_t **out, int out_count,
                                const uint8_t **in , int in_count);

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    while (av_read_frame(input_format_ctx, packet) >= 0)
    {
        // av_log(NULL, AV_LOG_INFO, "av_read_frame packet->stream_index = %d\n", packet->stream_index);
        if (packet->stream_index != audio_stream_index)
        {
            continue;
        }

        ret = avcodec_send_packet(audio_decoder->decode_ctx, packet);
        // av_log(NULL, AV_LOG_INFO, "avcodec_send_packet ret = %d\n", ret);
        if (ret == 0)
        {
            while (avcodec_receive_frame(audio_decoder->decode_ctx, frame) == 0)
            {
                //todo 处理音频
                int channels = frame->channels;
                int64_t dts = frame->pkt_dts;
                // av_log(NULL, AV_LOG_INFO, "avcodec_receive_frame channels = %d,dts = %lld.\n", channels, dts);

                swr_convert(swr_ctx, );
                //
                audio_chunk = (Uint8 *)frame->data;
                audio_len = frame->pkt_size; //长度为读出数据长度，在read_audio_data中做减法
                audio_pos = audio_chunk;

                // while (audio_len > 0) //判断是否播放完毕
                SDL_Delay(1);

                av_frame_unref(frame);
            }
        }
        else
        {
            char *err = av_err2str(ret);
            av_log(NULL, AV_LOG_ERROR, "avcodec_send_packet err = %s.\n", err);
        }
        av_packet_unref(packet);
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
    if (audio_decoder)
    {
        if (audio_decoder->decode_ctx)
        {
            avcodec_free_context(&audio_decoder->decode_ctx);
            audio_decoder->decode_ctx = NULL;
            free(audio_decoder);
        }
        audio_decoder = NULL;
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