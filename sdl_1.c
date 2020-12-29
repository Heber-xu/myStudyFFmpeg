#include <SDL2/SDL.h>
#include <libavutil/log.h>

int main(int argc, char *argv[])
{

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;

    SDL_Init(SDL_INIT_VIDEO);// 初始化函数,可以确定希望激活的子系统

    window = SDL_CreateWindow("My First Window",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              640,
                              480,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);//  创建窗口

    if (!window) {
        return -1;
    }
    renderer = SDL_CreateRenderer(window, -1, 0);//基于窗口创建渲染器
    if (!renderer) {
        return -1;
    }
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255); //设置渲染器颜色 r、g、b、a
    SDL_RenderClear(renderer);//用指定的颜色清空缓冲区
    SDL_RenderPresent(renderer); //将缓冲区中的内容输出到目标窗口上。
    SDL_Delay(3000); // 延时3秒
    SDL_DestroyRenderer(renderer); //销毁渲染器
    SDL_DestroyWindow(window); // 销毁窗口
    SDL_Quit(); //退出SDL
    return 0;
}