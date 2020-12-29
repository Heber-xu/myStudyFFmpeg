#include <libavutil/samplefmt.h>
#include <libavutil/log.h>

int main(int argc, char *argv[])
{

    av_log_set_level(AV_LOG_INFO);

    enum AVSampleFormat format = AV_SAMPLE_FMT_S16;

    char *name = av_get_sample_fmt_name(format);
    av_log(NULL, AV_LOG_INFO, "av_get_sample_fmt_name name = %s.\n", name);

    int byte_count = av_get_bytes_per_sample(format);
    av_log(NULL, AV_LOG_INFO, "av_get_bytes_per_sample byte_count = %d.\n", byte_count);

    // av_samples_get_buffer_size();

    return 0;
}