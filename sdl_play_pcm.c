#include <stdio.h>
// #include <tchar.h>
#include <SDL2/SDL_types.h>
#include "SDL2/SDL.h"
#include <string.h>

static Uint8 *audio_chunk;
static Uint32 audio_len;
static Uint8 *audio_pos;
int pcm_buffer_size = 4096;

//回调函数，音频设备需要更多数据的时候会调用该回调函数
void audio_callback(void *udata, Uint8 *stream, int len)
{
    SDL_memset(stream, 0, len);
    if (audio_len == 0)
        return;
    len = (len > audio_len ? audio_len : len);

    //向stream中填充数据
    SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
    // memcpy(stream, audio_pos, len);
    audio_pos += len;
    audio_len -= len;
}

int main(int argc, char *argv[])
{

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        printf("Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    // SDL_AudioSpec spec;
    // spec.freq = 44100; //根据你录制的PCM采样率决定
    // spec.format = AUDIO_S16SYS;
    // spec.channels = 1; //单声道
    // spec.silence = 0;
    // spec.samples = 1024;
    // spec.callback = read_audio_data;
    // spec.userdata = NULL;

    //test.pcm 对应的信息如下
    SDL_AudioSpec spec;
    spec.freq = 44100;
    spec.format = AUDIO_S16SYS;
    spec.channels = 2;
    spec.silence = 0;
    spec.samples = 1024;
    spec.callback = audio_callback;
    spec.userdata = NULL;

    if (SDL_OpenAudio(&spec, NULL) < 0)
    {
        printf("can't open audio.\n");
        return -1;
    }

    char *filepath = "./test.pcm";

    FILE *fp = fopen(filepath, "rb+");
    if (fp == NULL)
    {
        printf("cannot open this file\n");
        return -1;
    }
    char *pcm_buffer = (char *)malloc(pcm_buffer_size);

    //播放
    SDL_PauseAudio(0);

    while (1)
    {
        if (fread(pcm_buffer, 1, pcm_buffer_size, fp) != pcm_buffer_size)
        { //从文件中读取数据，剩下的就交给音频设备去完成了，它播放完一段数据后会执行回调函数，获取等多的数据
            break;
        }

        audio_chunk = (Uint8 *)pcm_buffer;
        audio_len = pcm_buffer_size; //长度为读出数据长度，在read_audio_data中做减法
        audio_pos = audio_chunk;

        while (audio_len > 0) //判断是否播放完毕
            SDL_Delay(1);
    }
    free(pcm_buffer);
    SDL_Quit();

    return 0;
}