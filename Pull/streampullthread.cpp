#include "streampullthread.h"
#include <QElapsedTimer>
#include <QCoreApplication>
#include <Logger.h>

StreamPullThread::StreamPullThread(QObject *parent)
    : QThread{parent}
{
    // 初始化FFmpeg
    avformat_network_init();
    avdevice_register_all();
}

StreamPullThread::~StreamPullThread()
{
    close();
//    avformat_network_deinit();
}

bool StreamPullThread::open(const QString &url, int timeoutMs)
{
    if (m_running) {
        LogWarn << "StreamPullThread is already running";
        return true;
    }

    m_timeoutMs = timeoutMs;

    // 设置连接参数
    av_dict_set(&m_options, "rtsp_transport", "tcp", 0);
    av_dict_set(&m_options, "stimeout", QString::number(m_timeoutMs * 1000).toUtf8(), 0);
    av_dict_set(&m_options, "max_delay", "500", 0);
    av_dict_set(&m_options, "reorder_queue_size", "1000", 0);
    av_dict_set(&m_options, "analyzeduration", "1000000", 0);
    av_dict_set(&m_options, "probesize", "1000000", 0);

    // 打开输入流
    if (!openInput(url)) {
        cleanup();
        return false;
    }

    // 查找流信息
    if (!findStreamInfo()) {
        cleanup();
        return false;
    }

    // 启动线程
    m_running = true;
    start();

    return true;

}

void StreamPullThread::close()
{
    if (!m_running) return;

    m_running = false;

    // 等待线程结束
    if (isRunning()) {
        wait(2000);
        if (isRunning()) {
            terminate();
            wait(1000);
        }
    }

    cleanup();
}

void StreamPullThread::setHardwareDecoding(bool enable) {
    m_hardwareDecoding = enable;
}

void StreamPullThread::setTimeout(int timeoutMs) {
    m_timeoutMs = timeoutMs;
}

AVCodecParameters* StreamPullThread::videoCodecParameters() const {
    if (m_videoStreamIndex >= 0 && m_formatContext) {
        return m_formatContext->streams[m_videoStreamIndex]->codecpar;
    }
    return nullptr;
}

AVCodecParameters* StreamPullThread::audioCodecParameters() const {
    if (m_audioStreamIndex >= 0 && m_formatContext) {
        return m_formatContext->streams[m_audioStreamIndex]->codecpar;
    }
    return nullptr;
}

void StreamPullThread::run()
{
    AVPacket *packet = av_packet_alloc();
    if (!packet) {
        emit errorOccurred("Failed to allocate packet");
        return;
    }

    QElapsedTimer timer;
    timer.start();

    while (m_running) {
        int ret = av_read_frame(m_formatContext, packet);
        if (ret < 0) {
            // 处理读取结束或错误
            if (ret == AVERROR_EOF) {
                LogInfo << "End of stream reached";
                break;
            }

            // 检查超时
            if (timer.elapsed() > m_timeoutMs) {
                emit errorOccurred("Stream read timeout");
                break;
            }

            // 短暂休眠后重试
            QThread::msleep(10);
            continue;
        }

        // 重置超时计时器
        timer.restart();

        // 处理数据包
        processPacket(packet);

        // 重置数据包
        av_packet_unref(packet);
    }

    // 发送空包表示结束
    if (m_running) {
        AVPacket eofPacket;
        av_init_packet(&eofPacket);
        eofPacket.data = nullptr;
        eofPacket.size = 0;
        eofPacket.stream_index = m_videoStreamIndex;
        emit videoPacketReady(&eofPacket);

        eofPacket.stream_index = m_audioStreamIndex;
        emit audioPacketReady(&eofPacket);
    }

    av_packet_free(&packet);
}

bool StreamPullThread::openInput(const QString &url) {
    // 分配格式上下文
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        emit errorOccurred("Failed to allocate format context");
        return false;
    }

    // 打开输入流
    int ret = avformat_open_input(&m_formatContext, url.toUtf8().constData(), nullptr, &m_options);
    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, error, sizeof(error));
        emit errorOccurred(QString("Failed to open input: %1").arg(error));
        return false;
    }

    return true;
}

bool StreamPullThread::findStreamInfo() {
    // 设置超时参数
    AVDictionary *probeOptions = nullptr;
    av_dict_set(&probeOptions, "timeout", QString::number(m_timeoutMs * 1000).toUtf8(), 0);

    // 查找流信息
    int ret = avformat_find_stream_info(m_formatContext, &probeOptions);
    if (probeOptions) {
        av_dict_free(&probeOptions);
    }

    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, error, sizeof(error));
        emit errorOccurred(QString("Failed to find stream info: %1").arg(error));
        return false;
    }

    // 打印流信息
    av_dump_format(m_formatContext, 0, m_formatContext->url, 0);

    // 查找视频流和音频流
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;

    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        AVStream *stream = m_formatContext->streams[i];
        AVCodecParameters *codecpar = stream->codecpar;

        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex < 0) {
            m_videoStreamIndex = i;
            LogInfo << "Found video stream: index=" << i
                    << " codec=" << avcodec_get_name(codecpar->codec_id)
                    << " resolution=" << codecpar->width << "x" << codecpar->height;

            // 发送视频流信息
            double frameRate = av_q2d(stream->avg_frame_rate);
            if (frameRate <= 0) frameRate = av_q2d(stream->r_frame_rate);
            emit streamInfoReady(codecpar->width, codecpar->height, frameRate);
        }
        else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_audioStreamIndex < 0) {
            m_audioStreamIndex = i;
            LogInfo << "Found audio stream: index=" << i
                    << " codec=" << avcodec_get_name(codecpar->codec_id)
                    << " channels=" << codecpar->channels
                    << " sample_rate=" << codecpar->sample_rate;
        }
    }

    if (m_videoStreamIndex < 0 && m_audioStreamIndex < 0) {
        emit errorOccurred("No video or audio streams found");
        return false;
    }

    return true;
}

void StreamPullThread::processPacket(AVPacket *packet) {
    if (packet->stream_index == m_videoStreamIndex) {
        // 复制视频包
        AVPacket *videoPacket = av_packet_alloc();
        av_packet_ref(videoPacket, packet);
        emit videoPacketReady(videoPacket);
    }
    else if (packet->stream_index == m_audioStreamIndex) {
        // 复制音频包
        AVPacket *audioPacket = av_packet_alloc();
        av_packet_ref(audioPacket, packet);
        emit audioPacketReady(audioPacket);
    }
}

void StreamPullThread::cleanup() {
    // 清理格式上下文
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
    }

    // 清理选项
    if (m_options) {
        av_dict_free(&m_options);
        m_options = nullptr;
    }

    // 重置流索引
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
}
