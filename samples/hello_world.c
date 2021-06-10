#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

static void logging(const char *fmt, ...);
static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame);
static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename);

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("You need to specify a media file.\n");
        return -1;
    }

    logging("Initializing all the containers, codecs and protocols.");

    // AVFormatContext存放格式的header信息（container）
    // 为该组件分配内存空间
    // 结构体参考：http://ffmpeg.org/doxygen/trunk/structAVFormatContext.html
    AVFormatContext *pFormatContext = avformat_alloc_context();
    if (!pFormatContext) {
        logging("Allocate memory for Format Context failed!");
        return -1;
    }

    logging("Open the input file (%s) and loading format (containder) header!", argv[1]);

    // 打开文件并读取header信息，并未打开编解码器
    // 以下函数输入参数包括：
    // AVFormatContext：存放header信息的结构体
    // url (filename)
    // AVInputFormat，如果没有指定，则执行自动检测
    // AVDictionary是解码器的一个选项，可以为空（这个参数的含义待学习）
    if (avformat_open_input(&pFormatContext, argv[1], NULL, NULL) != 0) {
        logging("Can't open the media file!");
        return -1;
    }

    // 至此，我们可以获取指定媒体文件的一些header信息，比如文件的format（container）等
    logging("Format %s, duration %lld us bit_rate %lld", pFormatContext->iformat->name, pFormatContext->duration, pFormatContext->bit_rate);

    logging("Finding stream info from format!");

    // 从Format中读取Packets以获取stream信息
    // 以下函数为pFormatContext->stream赋值
    // 第二个参数表示对应于每个stream的编解码器选项
    // 返回时，每个dictionary将被填上没有找到的选项，NULL相当于没有指定任何选项
    if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
        logging("Can't get the stream info!");
        return -1;
    }

    // 编解码器（codec）是负责对stream进行编码和解码的组件
    AVCodec *pCodec = NULL;

    // 该组件描述了第i个stream所使用的的codec的特性
    AVCodecParameters *pCodecParameters = NULL;

    int video_stream_index = -1;

    //  循环所有stream并打印其主要信息
    for (int i = 0; i < pFormatContext->nb_streams; i++) {
        AVCodecParameters *pLocalCodecParameters = NULL;
        // codecpar是codec parameters的缩写
        pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
        // den是denominator（分母）的缩写，num是numerator（分子）的缩写，也就是time_base和r_frame_rate都是一个分数
        // FFMpeg中时间基（time_base）像一把尺子一样，把1s分成N等份，每一个刻度是(1/N)s，比如time_base为{1, 90000}
        // 这里分母被称为timescale，可以被frame rate(fps)整除
        logging("AVStream->time_base before open coded %d/%d", pFormatContext->streams[i]->time_base.num, pFormatContext->streams[i]->time_base.den);
        logging("AVStream->r_frame_rate before open coded %d/%d", pFormatContext->streams[i]->r_frame_rate.num, pFormatContext->streams[i]->r_frame_rate.den);
        // PRId64跨平台打印int64_t时需要使用它，64位：%ld，32位：%lld
        logging("AVStream->start_time %" PRId64, pFormatContext->streams[i]->start_time);
        logging("AVStream->duration %" PRId64, pFormatContext->streams[i]->duration);
        logging("Finding the proper decoder (CODEC)");
        AVCodec *pLocalCodec = NULL;
        pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);
        if (pLocalCodec == NULL) {
            logging("Unsupported codec!");
            continue;
        }

        // 当当前stream是视频，则存储它的index，编解码器参数和编解码器
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (video_stream_index == -1) {
                video_stream_index = i;
                pCodec = pLocalCodec;
                pCodecParameters = pLocalCodecParameters;
            }
            logging("Video Codec: resolution %d x %d", pLocalCodecParameters->width, pLocalCodecParameters->height);
        }
        else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            logging("Audio Codec: %d channels, sample rate %d", pLocalCodecParameters->channels, pLocalCodecParameters->sample_rate);
        }

        // 打印Codec的名称、id和bitrate
        logging("\tCodec %s ID %d bit_rate %" PRId64, pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate);
    }

    if (video_stream_index == -1) {
        logging("File %s does not contain a video stream!", argv[1]);
        return -1;
    }
    
    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
    if (!pCodecContext) {
        logging("Failed to allocate memory for AVCodecContext!");
        return -1;
    }

    // 根据所提供的编解码器参数值来填充编解码器上下文
    if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0) {
        logging("Failed to copy codec parameters to codec context!");
        return -1;
    }

    // 初始化AVCodecContext使用给定的Codec
    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) {
        logging("Failed to open codec!");
        return -1;
    }
    
    
    AVFrame *pFrame = av_frame_alloc();
    if (!pFrame) {
        logging("Failed to allocate memory for AVFrame!");
        return -1;
    }

    AVPacket *pPacket = av_packet_alloc();
    if (!pPacket) {
        logging("Failed to allocate memory for AVPacket!");
        return -1;
    }

    int response = 0;
    int how_many_packets_to_process = 8;

    // 从stream取数据填充Packet
    while (av_read_frame(pFormatContext, pPacket) >= 0) {
        // 只取视频的包
        if (pPacket->stream_index == video_stream_index) {
            // pts: Presentation Time Stamp，即显示这一帧的时间戳
            // 数值代表相对time_base的倍数，比如3000，则3000 * (1 / 90000)，
            // 即该帧显示时间为1/30s
            logging("AVPacket->pts %" PRId64, pPacket->pts);
            response = decode_packet(pPacket, pCodecContext, pFrame);
            if (response < 0) {
                break;
            }
            if (--how_many_packets_to_process <= 0) break;
        }
        // 解除对数据包所引用的缓冲区的引用，并将剩余的数据包字段重置为其默认值。
        av_packet_unref(pPacket);
    }

    logging("Releasing all the resources!");

    avformat_close_input(&pFormatContext);
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecContext);

    return 0;
}

static void logging(const char *fmt, ...) {
    va_list args;
    fprintf(stderr, "LOG: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame) {
    // 提供原始数据包作为解码器的输入。
    int response = avcodec_send_packet(pCodecContext, pPacket);
    if (response < 0) {
        logging("Error while sending a packet to the decoder: %s", av_err2str(response));
        return response;
    }

    while (response >= 0) {
        // 返回解码器的解码输出数据。
        response = avcodec_receive_frame(pCodecContext, pFrame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        }
        else if (response < 0) {
            logging("Error while receiving a frame from the decoder: %s", av_err2str(response));
            return response;
        }
        
        // 解码成功
        if (response >= 0) {
            logging(
                "Frame %d (type=%c, size=%d bytes, format=%d) pts %d key_frame %d [DTS %d]",
                pCodecContext->frame_number,
                av_get_picture_type_char(pFrame->pict_type),
                pFrame->pkt_size,
                pFrame->format,
                pFrame->pts,
                pFrame->key_frame,
                pFrame->coded_picture_number
            );
        }

        char frame_filename[1024];
        snprintf(frame_filename, sizeof(frame_filename), "%s-%d.pgm", "frame", pCodecContext->frame_number);
        
        if (pFrame->format != AV_PIX_FMT_YUV420P) {
            logging("The generated file may not be a grayscale image, but could e.g. be just the R channel if the video format is RGB!");
        }
        save_gray_frame(pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, frame_filename);
    }
    return 0;
}

static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename) {
    FILE *f;
    int i;
    f = fopen(filename, "w");
    // 编写PGM文件格式所需的最小header
    // pgm: portable graymap format -> https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);

    for (i = 0; i < ysize; i++) {
        fwrite(buf + i * wrap, 1, xsize, f);
    }
    fclose(f);
}