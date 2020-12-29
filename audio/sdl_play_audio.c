#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

typedef struct Decoder
{
  AVCodec *decodec;
  AVCodecContext *decode_ctx;
} Decoder;

typedef struct PacketQueue
{
  AVPacketList *first_pkt, *last_pkt;
  int nb_packets;
  int size;
  SDL_mutex *mutex;
  SDL_cond *cond;
} PacketQueue;

PacketQueue audioq;

SwrContext *swr_ctx;

int quit = 0;

//for event
SDL_Event event;

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

static Uint8 *audio_chunk;
static Uint32 audio_len;
static Uint8 *audio_pos;
int pcm_buffer_size = 4096;

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

//回调函数，音频设备需要更多数据的时候会调用该回调函数
// static void audio_callback(void *udata, Uint8 *stream, int len)
// {
//     SDL_memset(stream, 0, len);
//     if (audio_len == 0)
//         return;
//     len = (len > audio_len ? audio_len : len);

//     SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
//     audio_pos += len;
//     audio_len -= len;
// }

void audio_callback(void *userdata, Uint8 *stream, int len)
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
      av_log(NULL, AV_LOG_INFO, "audio_decode_frame audio_size = %d.\n", audio_size);
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
    // SDL_MixAudio(stream,(uint8_t *)audio_buf + audio_buf_index,len1,SDL_MIX_MAXVOLUME);
    // SDL_MixAudio(stream,(uint8_t *)audio_buf,len1,SDL_MIX_MAXVOLUME);
    len -= len1;
    stream += len1;
    audio_buf_index += len1;
  }
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

  /* create resampler context */
  // SwrContext *swr_ctx = swr_alloc();
  swr_ctx = swr_alloc();
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
  swr_alloc_set_opts(swr_ctx,
                     dst_ch_layout,
                     dst_sample_fmt,
                     dst_rate,
                     src_ch_layout,
                     src_sample_fmt,
                     src_rate,
                     0,
                     NULL);

  /* initialize the resampling context */
  if ((ret = swr_init(swr_ctx)) < 0)
  {
    av_log(NULL, AV_LOG_INFO, "swr_init ret = %d.\n", ret);
    goto end;
  }

  if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER))
  {
    av_log(AV_LOG_ERROR, "Could not initialize SDL - %s\n", SDL_GetError());
    return -1;
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
  // spec.userdata = NULL;
  spec.userdata = audio_decoder->decode_ctx;

  if (SDL_OpenAudio(&spec, NULL) < 0)
  {
    av_log(NULL, AV_LOG_ERROR, "can't open audio.\n");
    return -1;
  }

  packet_queue_init(&audioq);

  AVPacket *packet = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();

  //播放
  SDL_PauseAudio(0);

  while (av_read_frame(input_format_ctx, packet) >= 0)
  {
    // av_log(NULL, AV_LOG_INFO, "av_read_frame packet->stream_index = %d\n", packet->stream_index);
    if (packet->stream_index != audio_stream_index)
    {
      continue;
    }

    packet_queue_put(&audioq, packet);
    // av_packet_unref(packet);

    //延迟一下，以免太快结束
    SDL_Delay(10);

    SDL_PollEvent(&event);
    switch (event.type)
    {
    case SDL_QUIT:
      quit = 1;
      goto __QUIT;
      break;
    default:
      break;
    }
  }

  av_log(NULL, AV_LOG_INFO, "av_read_frame finish.\n");
  goto end;

__QUIT:
  ret = 0;
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