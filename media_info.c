#include<libavformat/avformat.h>
#include<libavutil/log.h>

int main(int argc,char *argv[]) {

    av_log_set_level(AV_LOG_INFO);

    if(argc < 2){
        av_log(NULL,AV_LOG_ERROR,"without input url.\n");
        return -1;
    }

    char *input_url = argv[1];

    int ret;
    AVFormatContext *input_format_ctx = avformat_alloc_context();
    if(input_format_ctx == NULL){
        av_log(NULL,AV_LOG_ERROR,"avformat_alloc_context fail.\n");
        return -1;
    }
    ret = avformat_open_input(&input_format_ctx,input_url,NULL,NULL);
    if(ret != 0){
        av_log(NULL,AV_LOG_ERROR,"avformat_open_input error,ret = %d.\n",ret);
        goto end;
    }

    ret = avformat_find_stream_info(input_format_ctx,NULL);
    if(ret < 0){
        av_log(NULL,AV_LOG_ERROR,"avformat_find_stream_info error,ret = %d.\n",ret);
        goto end;
    }

    // char *dump_path = "./dump.txt";
    // // av_dump_format(input_format_ctx,0,dump_path,0);
    // av_dump_format(input_format_ctx,0,NULL,0);

    int stream_num = input_format_ctx->nb_streams;
    av_log(NULL,AV_LOG_INFO,"stream_num = %d.\n",stream_num);
    for(int i = 0;i<stream_num;i++){
        AVStream *stream = input_format_ctx->streams[i];
        int index = stream->index;
        av_log(NULL,AV_LOG_INFO,"stream_index = %d.\n",index);
        av_dump_format(input_format_ctx,index,NULL,0);
    }

    goto end;

    end:
    av_log(NULL,AV_LOG_INFO,"goto end.\n");
    if(input_format_ctx){
        avformat_close_input(&input_format_ctx);
        avformat_free_context(input_format_ctx);
        input_format_ctx = NULL;
    } 
    return 0;

}