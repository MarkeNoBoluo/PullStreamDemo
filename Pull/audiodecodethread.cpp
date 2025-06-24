#include "audiodecodethread.h"
#include <Logger.h>
#include <QElapsedTimer>

AudioDecodeThread::AudioDecodeThread(QObject *parent)
    : QThread{parent}
{

}

AudioDecodeThread::~AudioDecodeThread()
{
    close();
}

bool AudioDecodeThread::init(AVCodecParameters *codecParams)
{
    if (!codecParams) {
        emit errorOccurred("Invalid codec parameters");
        return false;
    }

    // 查找解码器
    m_codec = avcodec_find_decoder(codecParams->codec_id);
    if (!m_codec) {
        emit errorOccurred("Unsupported audio codec");
        return false;
    }

    // 创建解码器上下文
    m_codecContext = avcodec_alloc_context3(m_codec);
    if (!m_codecContext) {
        emit errorOccurred("Failed to allocate codec context");
        return false;
    }

    // 复制参数
    int ret = avcodec_parameters_to_context(m_codecContext, codecParams);
    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, error, sizeof(error));
        emit errorOccurred(QString("Failed to copy codec parameters: %1").arg(error));
        return false;
    }

    // 设置低延迟选项
    m_codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecContext->flags2 |= AV_CODEC_FLAG2_FAST;

    // 打开解码器
    ret = avcodec_open2(m_codecContext, m_codec, nullptr);
    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, error, sizeof(error));
        emit errorOccurred(QString("Failed to open decoder: %1").arg(error));
        return false;
    }

    // 创建帧
    m_frame = av_frame_alloc();
    if (!m_frame) {
        emit errorOccurred("Failed to allocate frame");
        return false;
    }

    // 初始化重采样器
    if (!initResampler()) {
        return false;
    }

    LogInfo << "Audio decoder initialized: "
            << "Codec: " << m_codec->name
            << " Channels: " << m_codecContext->channels
            << " Sample rate: " << m_codecContext->sample_rate
            << " Format: " << av_get_sample_fmt_name(m_codecContext->sample_fmt);

    return true;
}

void AudioDecodeThread::setTargetFormat(int sampleRate, int channels, AVSampleFormat format) {
    m_targetFormat = AV_SAMPLE_FMT_S16;
    m_targetSampleRate = sampleRate;
    m_targetChannels = channels;

    // 重新初始化重采样器
    if (m_swrContext) {
        swr_free(&m_swrContext);
        m_swrContext = nullptr;
    }

    if (!initResampler()) {
        LogWarn << "Failed to reinitialize resampler with new format";
    }
}

void AudioDecodeThread::close() {
    if (!m_running) return;

    m_running = false;

    // 唤醒等待的线程
    m_queueCondition.wakeAll();

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

void AudioDecodeThread::onAudioPacketReceived(AVPacket *packet) {
    if (!m_running || !packet) {
        LogErr << "音频包数据为空";
        return;
    }

    QMutexLocker locker(&m_queueMutex);

    // 空包表示流结束
    if (packet->size == 0 && packet->data == nullptr) {
        m_flushing = true;
        // 不释放，直接加入队列
    }

    // 检查队列大小，防止内存溢出
    if (m_packetQueue.size() >= m_maxQueueSize) {
        LogWarn << "Audio packet queue overflow, dropping oldest packets";

        // 丢弃旧的数据包
        while (m_packetQueue.size() >= m_maxQueueSize / 2) {
            AVPacket *oldPacket = m_packetQueue.dequeue();
            av_packet_free(&oldPacket);
        }

        m_dropFrames = true;  // 设置丢帧标志
    }
    LogInfo << "音频包数据添加到队列";
    // 添加到队列
    m_packetQueue.enqueue(packet);
    m_queueCondition.wakeOne();
}

void AudioDecodeThread::setPaused(bool paused) {
    m_paused = paused;

    if (!paused) {
        QMutexLocker locker(&m_queueMutex);
        m_queueCondition.wakeAll();
    }
}

void AudioDecodeThread::run() {
    m_running = true;
    m_flushing = false;
    m_paused = false;
    m_dropFrames = false;

    while (m_running) {
        // 处理暂停状态
        if (m_paused) {
            QMutexLocker locker(&m_queueMutex);
            m_queueCondition.wait(&m_queueMutex, 100);
            continue;
        }

        AVPacket *packet = nullptr;

        // 从队列获取数据包
        {
            QMutexLocker locker(&m_queueMutex);

            if (m_packetQueue.isEmpty()) {
                if (m_flushing) {
                    // 没有更多数据包且处于刷新状态，退出循环
                    break;
                }
                // 等待新数据包
                m_queueCondition.wait(&m_queueMutex, 100);
                continue;
            }

            packet = m_packetQueue.dequeue();
        }

        // 处理数据包
        if (packet) {

            // 如果设置了丢帧标志，跳过非关键帧
            if (m_dropFrames  && packet->data != nullptr) {
                av_packet_free(&packet);
                LogWarn << "丢帧";
                continue;
            }

            if (decodePacket(packet)) {
                // 如果队列大小恢复正常，清除丢帧标志
                if (m_dropFrames && m_packetQueue.size() < m_maxQueueSize / 4) {
                    m_dropFrames = false;
                    LogInfo << "Audio queue recovered, resuming normal processing";
                }
            }
            av_packet_free(&packet);
        }

    }

    // 刷新解码器
    decodePacket(nullptr);

    // 发送结束信号
    emit audioFrameDecoded(nullptr);

    LogInfo << "Audio decoding thread stopped";
}

bool AudioDecodeThread::initResampler()
{
    if (m_swrContext) {
        swr_free(&m_swrContext);
    }

    // 获取输入和输出通道布局
    int64_t inChannelLayout = av_get_default_channel_layout(m_codecContext->channels);
    int64_t outChannelLayout = av_get_default_channel_layout(m_targetChannels);

    LogInfo << "重采样器配置: "
            << "输入: " << av_get_sample_fmt_name(m_codecContext->sample_fmt)
            << " @ " << m_codecContext->sample_rate << "Hz, " << m_codecContext->channels << "ch"
            << " -> 输出: " << av_get_sample_fmt_name(m_targetFormat)
            << " @ " << m_targetSampleRate << "Hz, " << m_targetChannels << "ch";

    // 检查是否需要重采样
    bool needResampling = (m_codecContext->sample_rate != m_targetSampleRate) ||
                         (m_codecContext->channels != m_targetChannels) ||
                         (m_codecContext->sample_fmt != m_targetFormat);

    if (!needResampling) {
        LogInfo << "音频参数匹配，无需重采样";
        return true;  // 不需要重采样
    }

    // 创建重采样上下文
    m_swrContext = swr_alloc_set_opts(
        nullptr,
        outChannelLayout,
        m_targetFormat,
        m_targetSampleRate,
        inChannelLayout,
        m_codecContext->sample_fmt,
        m_codecContext->sample_rate,
        0,
        nullptr
    );

    if (!m_swrContext) {
        emit errorOccurred("Failed to allocate resampler context");
        return false;
    }

    int ret = swr_init(m_swrContext);
    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, error, sizeof(error));
        emit errorOccurred(QString("Failed to initialize resampler: %1").arg(error));
        swr_free(&m_swrContext);
        return false;
    }

    LogInfo << "Audio resampler initialized successfully";
    return true;
}

bool AudioDecodeThread::decodePacket(AVPacket *packet) {
    // 发送包到解码器
    int ret = avcodec_send_packet(m_codecContext, packet);
    if (ret < 0) {
        // 忽略EOF和EAGAIN错误
        if (ret != AVERROR_EOF && ret != AVERROR(EAGAIN)) {
            char error[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, error, sizeof(error));
            LogWarn << "Error sending packet to decoder: " << error;
        }
        return false;
    }

    // 接收解码帧
    while (ret >= 0) {
        ret = avcodec_receive_frame(m_codecContext, m_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            LogWarn << "监测到结束符,ret"<< ret;
            return true;
        }
        if (ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, error, sizeof(error));
            LogWarn << "Error receiving frame from decoder: " << error;
            return false;
        }

        // 更新音频时钟
        if (m_frame->pts != AV_NOPTS_VALUE) {
            // 转换时间戳为毫秒
            qint64 pts = av_rescale_q(m_frame->pts,
                                      m_codecContext->time_base,
                                      {1, 1000});
            emit audioClockUpdated(pts);
        }

        // 重采样音频帧
        auto resampledFrame = resampleFrame(m_frame);
        if (resampledFrame) {
            // 发送重采样后的帧
            LogWarn << "发送重采样后的帧";
            emit audioFrameDecoded(resampledFrame);
        }

        // 释放帧引用
        av_frame_unref(m_frame);
    }

    return true;
}

std::shared_ptr<AVFrame> AudioDecodeThread::resampleFrame(AVFrame *frame) {
    // 如果不需要重采样，直接复制帧
    if (!m_swrContext) {
        AVFrame *outFrame = av_frame_alloc();
        if (!outFrame) {
            LogWarn << "Failed to allocate output frame";
            return nullptr;
        }

        if (av_frame_ref(outFrame, frame) < 0) {
            av_frame_free(&outFrame);
            return nullptr;
        }

        return std::shared_ptr<AVFrame>(outFrame, [](AVFrame *f) {
            if (f) av_frame_free(&f);
        });
    }

    // 计算输出样本数
    int outSamples = swr_get_out_samples(m_swrContext, frame->nb_samples);
    if (outSamples <= 0) {
        LogWarn << "Invalid output sample count: " << outSamples;
        return nullptr;
    }

    // 创建输出帧
    AVFrame *outFrame = av_frame_alloc();
    if (!outFrame) {
        LogWarn << "Failed to allocate output frame";
        return nullptr;
    }

    // 设置输出帧参数
    outFrame->sample_rate = m_targetSampleRate;
    outFrame->channels = m_targetChannels;
    outFrame->channel_layout = av_get_default_channel_layout(m_targetChannels);
    outFrame->format = m_targetFormat;
    outFrame->nb_samples = outSamples;

    // 分配缓冲区
    int ret = av_frame_get_buffer(outFrame, 0);
    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, error, sizeof(error));
        LogWarn << "Failed to allocate output buffer: " << error;
        av_frame_free(&outFrame);
        return nullptr;
    }

    // 执行重采样
    ret = swr_convert(m_swrContext,
                      outFrame->data, outSamples,
                      (const uint8_t**)frame->data, frame->nb_samples);
    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, error, sizeof(error));
        LogWarn << "Failed to resample audio: " << error;
        av_frame_free(&outFrame);
        return nullptr;
    }

    outFrame->nb_samples = ret;
    outFrame->pts = frame->pts;

    return std::shared_ptr<AVFrame>(outFrame, [](AVFrame *f) {
        if (f) av_frame_free(&f);
    });
}

void AudioDecodeThread::cleanup() {
    // 清空包队列
    {
        QMutexLocker locker(&m_queueMutex);
        while (!m_packetQueue.isEmpty()) {
            AVPacket *packet = m_packetQueue.dequeue();
            av_packet_free(&packet);
        }
    }

    // 释放帧
    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }

    // 释放解码器上下文
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }

    // 释放重采样器
    if (m_swrContext) {
        swr_free(&m_swrContext);
        m_swrContext = nullptr;
    }

    // 重置状态
    m_running = false;
    m_paused = false;
    m_flushing = false;
    m_dropFrames = false;

    LogInfo << "Audio decoder resources cleaned up";
}
