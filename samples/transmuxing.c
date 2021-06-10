/*
container类型的转换，mp4/mkv/avi等都是container类型
container下包含a/v的stream，音视频流各自采用不同的压缩算法，如aac/h264
所以transmuxing如果指定了-c copy，是不用解码编码的。
*/
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>

int main(int argc, char **argv) {
    AVFormatContext *input_format_context = NULL, *output_format_context = NULL;
    AVPacket packet;
    const char *in_filename, *out_filename;
    int ret, i;
    int stream_index = 0;
    int *streams_list = NULL;
    int number_of_streams = 0;
    /*
    Fragmented MP4（fMP4）：碎片化的MP4文件
    其优点在于，当使用DASH或HLS进行流传输时，播放器软件仅需要下载观看者想要观看的片段。
    fMP4和ts文件的区别：
    .ts文件不提供关于时长等信息，你无法在ts文件里去实现音视频的seek操作
    .ts文件一般用于m3u8中, 或者提供了流媒体基础信息的前提下使用
    .mp4文件可以在不下载完全媒体文件的前提下进行seek操作;因为其头部记录moov信息
    */
    int fragmented_mp4_options = 0;
    if (argc < 3) {
        printf("Usage: transmuxing in_filename out_filename.\n");
        return -1;
    }
    else if (argc == 4) {
        fragmented_mp4_options = 1;
    }

    in_filename = argv[1];
    out_filename = argv[2];

    // 打开输入文件并读取文件的header信息，填充input_format_context相关字段
    if ((ret = avformat_open_input(&input_format_context, in_filename, NULL, NULL)) < 0) {
        fprintf(stderr, "Can't open input file '%s'.\n", in_filename);
        // 释放相关内存
        goto end;
    }

    // 读取stream信息填充context相关字段
    if ((ret = avformat_find_stream_info(input_format_context, NULL)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information.\n");
        goto end;
    }

    // 
    avformat_alloc_output_context2(&output_format_context, NULL, NULL, out_filename);
    if (!output_format_context) {
        fprintf(stderr, "Can't create output context.\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    streams_list = av_mallocz_array(input_format_context->nb_streams, sizeof(*streams_list));

    if (!streams_list) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    for (i = 0; i < input_format_context->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = input_format_context->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;
        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            streams_list[i] = -1;
            continue;
        }
        streams_list[i] = stream_index++;
        out_stream = avformat_new_stream(output_format_context, NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream.\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            fprintf(stderr, "Failed to copy parameters.\n");
            goto end;
        }
    }
    // 打印关于输入或输出格式的详细信息，例如持续时间，比特率，流，容器，程序，元数据，边数据，编解码器和时基。
    av_dump_format(output_format_context, 0, out_filename, 1);

    // Mux 是 Multiplex 的缩写，意为“多路传输”，其实就是“混流”、“封装”的意思
    // 进行与 muxing 相反的“分解复用”操作，“分离”一个文件中的视频和音频部分
    // DeMuxer: 解复用器
    // 当设置了AVFMT_NOFILE标志，不用创建AVIOContext这个I/O context
    // oformat: output container format.
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&output_format_context->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Can't open output file '%s'", out_filename);
            goto end;
        }
    }
    AVDictionary *opts = NULL;
    
    // 如果是fMP4，需要指定movflags参数
    // ffmpeg -i non_fragmented.mp4 -movflags frag_keyframe+empty_moov+default_base_moof fragmented.mp4
    if (fragmented_mp4_options) {
        // https://developer.mozilla.org/en-US/docs/Web/API/Media_Source_Extensions_API/Transcoding_assets_for_MSE
        av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
    }

    ret = avformat_write_header(output_format_context, &opts);
    if (ret < 0) {
        fprintf(stderr, "Error occur when opening output file.\n");
        goto end;
    }

    while (1) {
        AVStream *in_stream, *out_stream;
        ret = av_read_frame(input_format_context, &packet);
        if (ret < 0) break;
        in_stream = input_format_context->streams[packet.stream_index];
        // 非音频、视频、字幕的流在streams_list中被设置为了-1
        // 这里判断了一些异常，比如包的流id超过流的个数或者非音频、视频、字幕流
        if (packet.stream_index >= number_of_streams || streams_list[packet.stream_index] < 0) {
            av_packet_unref(&packet);
            continue;
        }
        // 由于streams_list中可能存在-1的流，所以packet.stream_index应该以streams_list为准
        packet.stream_index = streams_list[packet.stream_index];
        out_stream = output_format_context->streams[packet.stream_index];
        // copy packet
        // round模式：AV_ROUND_ZERO趋近于0，AV_ROUND_INF趋远于0
        // AV_ROUND_DOWN = floor, AV_ROUND_UP = ceil, AV_ROUND_NEAR_INF = round
        // AV_ROUND_PASS_MINMAX这个Flag并不表示一个舍入法，
        // 而是针对 INT64_MIN 和 INT64_MIN 这两个特殊值设立的，表示当针对这两个特殊值做舍入时，直接返回这两个特殊值即可，不做舍入。
        // 因为container变化，所以time_base可能发生改变，这时候要重新算pts，dts和duration！
        packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
        packet.pos = -1; // 在stream中的byte位置，如果未知，填充-1
        ret = av_interleaved_write_frame(output_format_context, &packet);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet.\n");
            break;
        }
        av_packet_unref(&packet);
    }
    // 输出文件尾，如果write了header，就一定要调用该函数，这两个函数成对出现。
    av_write_trailer(output_format_context);

end:
    avformat_close_input(&input_format_context);
    // close output
    // 同样，如果是AVFMT_NOFILE，表明并未打开pb，不需要close
    if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        printf("close!\n");
        avio_closep(&output_format_context->pb);
    }
    avformat_free_context(output_format_context);
    av_freep(&streams_list);
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error ouccured: %s.\n", av_err2str(ret));
        return 1;
    }

    return 0;
}