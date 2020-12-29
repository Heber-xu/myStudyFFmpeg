
# 音频相关

[参考资料](https://www.cnblogs.com/wangguchangqing/p/5851490.html)

## sample_format

```shell
clang -o sample_format sample_format.c `pkg-config --cflags --libs libavutil`
./sample_format
```

# media_info

```shell
//编译
clang -o media_info media_info.c `pkg-config --cflags --libs libavformat libavutil`
//执行
./media_info aaa.mp4
```

# metadata

```shell
//编译
clang -o metadata metadata.c `pkg-config --cflags --libs libavformat libavutil`
//执行
./metadata aaa.mp4
```

metadata.c 是从ffmpeg源码中拷贝出来的，但是打印的结果和解析mp4文件box结果略有不同。为什么？

# play_audio

只播放音频。

[音频重采样](https://blog.csdn.net/u011003120/article/details/81542347)

```shell
//编译
clang -o play_audio play_audio.c `pkg-config --cflags --libs libavformat libavcodec SDL2`
//执行
./play_audio yi.mp3
```

# sdl

[sdl参考资料](https://github.com/David1840/SimplePlayer)

## sdl_1

sdl_1.c编译之后在mac上无法正常运行，google上面说是没有加入事件处理。[参考](https://stackoverflow.com/questions/34424816/sdl-window-does-not-show)。

sdl_1.c[参考资料](https://david1840.github.io/2019/04/11/SDL2%E9%9F%B3%E8%A7%86%E9%A2%91%E6%B8%B2%E6%9F%93%E5%85%A5%E9%97%A8/)。

```shell
//编译
clang -v -o sdl_1 sdl_1.c `pkg-config --cflags --libs SDL2 libavutil`
//执行
./sdl_1
```

## sdl_2

**SDL2纹理渲染**。

可以直接编译运行。

sdl_2.c[参考资料](https://david1840.github.io/2019/04/16/SDL2%E7%BA%B9%E7%90%86%E6%B8%B2%E6%9F%93/)。

```shell
//编译
clang -v -o sdl_2 sdl_2.c `pkg-config --cflags --libs SDL2 libavutil`
//执行
./sdl_2
```

## sdl_play_pcm

使用sdl播放PCM音频。

```shell
//编译
clang -o sdl_play_pcm sdl_play_pcm.c `pkg-config --cflags --libs SDL2`
//执行
./sdl_play_pcm
```

## sdl_play_audio

使用sdl播放音频。

没有明白的地方，为什么播放音频没有使用SDL_MixAudio？

```shell
//编译
clang -o sdl_play_audio sdl_play_audio.c `pkg-config --cflags --libs libavformat libavcodec libswresample SDL2`
//运行
./sdl_play_audio ../yi.mp3
```

## play_video

播放音视频。

```shell
//编译
clang -o play_video play_video.c `pkg-config --cflags --libs libavformat libavcodec libswscale SDL2`
//执行
./play_video aaa.mp4 
```

# player

播放音视频，音视频没有同步。

```shell
//编译
clang -o player player.c `pkg-config --cflags --libs libavformat libavcodec libswscale libswresample SDL2`
//运行
./player ../aaa.mp4
//下面两个网络视频播放音频有问题，甚至会造成应用卡死
./player http://202.69.67.66:443/webcast/bshdlive-mobile/playlist.m3u8
./player http://ivi.bupt.edu.cn/hls/cctv1hd.m3u8
```

# player_sync

播放音视频，音视频同步。

```shell
//编译
clang -o player_sync player_sync.c `pkg-config --cflags --libs libavformat libavcodec libswscale libswresample SDL2`
//运行
./player_sync ../aaa.mp4
```

# transcoding

```shell
clang -g -o transcoding transcoding.c video_debugging.c `pkg-config --cflags --libs libavcodec libavutil libavformat`
./transcoding aaa.mp4 bbb.mp4
```