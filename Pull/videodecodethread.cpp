#include "videodecodethread.h"

#include <Logger.h>
#include <QElapsedTimer>

VideoDecodeThread::VideoDecodeThread(QObject *parent)
    : QThread{parent}
{

}

VideoDecodeThread::~VideoDecodeThread()
{
    close();
}

bool VideoDecodeThread::init(AVCodecParameters *codecParams) {
    if (!codecParams) {
        emit errorOccurred("Invalid codec parameters");
        return false;
    }

    // 查找解码器
    m_codec = avcodec_find_decoder(codecParams->codec_id);
    if (!m_codec) {
        emit errorOccurred("Unsupported video codec");
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

    // 设置解码选项
    m_codecContext->flags2 |= AV_CODEC_FLAG2_FAST;

    // 尝试硬件解码
    if (m_hardwareDecoding) {
        if (!initHardwareDecoder()) {
            LogWarn << "Hardware decoding initialization failed, falling back to software";
            m_hardwareDecoding = false;
        }
    }

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
    m_hwFrame = av_frame_alloc();
    if (!m_frame || !m_hwFrame) {
        emit errorOccurred("Failed to allocate frames");
        return false;
    }

    // 保存视频信息
    m_videoSize = QSize(m_codecContext->width, m_codecContext->height);
    if (m_targetSize.isEmpty()) {
        m_targetSize = m_videoSize;
    }


    LogInfo << "Video decoder initialized: "
            << "Codec: " << m_codec->name
            << " Size: " << m_videoSize.width() << "x" << m_videoSize.height()
            << " Frame rate: " << m_frameRate
            << " HW decoding: " << (m_hardwareDecoding ? "enabled" : "disabled");

    emit videoInfoUpdated(m_videoSize.width(), m_videoSize.height(), m_frameRate);

    return true;
}

void VideoDecodeThread::setTargetSize(const QSize &size) {
    if (size.isValid()) {
        m_targetSize = size;
        LogInfo << "Target size set to: " << size.width() << "x" << size.height();

        // 重新创建SWS上下文
        if (m_swsContext) {
            sws_freeContext(m_swsContext);
            m_swsContext = nullptr;
        }
    }
}

void VideoDecodeThread::setHardwareDecoding(const bool &enable) {
    m_hardwareDecoding = enable;
}

void VideoDecodeThread::close() {
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

void VideoDecodeThread::onVideoPacketReceived(AVPacket *packet) {
    if (!m_running || !packet) return;

    QMutexLocker locker(&m_queueMutex);

    // 空包表示流结束
    if (packet->size == 0 && packet->data == nullptr) {
        m_flushing = true;
        av_packet_free(&packet);
        m_queueCondition.wakeAll();
        return;
    }

    // 添加到队列
    m_packetQueue.enqueue(packet);
    m_queueCondition.wakeOne();
}

void VideoDecodeThread::updateAudioClock(qint64 clock) {
    m_audioClock = clock;
}

void VideoDecodeThread::run() {
    m_running = true;
    m_flushing = false;

    QElapsedTimer frameTimer;
    frameTimer.start();

    int64_t lastPts = AV_NOPTS_VALUE;
    double frameDuration = 0;

    if (m_frameRate > 0) {
        frameDuration = 1000.0 / m_frameRate;
    }

    while (m_running) {
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
            if (!decodePacket(packet)) {
                av_packet_free(&packet);
                continue;
            }
            av_packet_free(&packet);
        }

        // 帧同步处理
        if (lastPts != AV_NOPTS_VALUE && frameDuration > 0) {
            int64_t elapsed = frameTimer.elapsed();
            int64_t expected = static_cast<int64_t>(frameDuration * (m_frame->pts - lastPts));

            if (elapsed < expected) {
                QThread::msleep(expected - elapsed);
            }
        }

        lastPts = m_frame->pts;
        frameTimer.restart();
    }

    // 刷新解码器
    decodePacket(nullptr);

    // 发送结束信号
    emit videoFrameDecoded(QImage());

    LogInfo << "Video decoding thread stopped";
}

bool VideoDecodeThread::initHardwareDecoder() {
    if (!m_codec) return false;

    // 查找支持的硬件配置
    AVHWDeviceType hwType = AV_HWDEVICE_TYPE_NONE;
    bool hwSupported = false;

    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(m_codec, i);
        if (!config) break;

        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
            // 优先选择D3D11VA和DXVA2
            if (config->device_type == AV_HWDEVICE_TYPE_D3D11VA ||
                config->device_type == AV_HWDEVICE_TYPE_DXVA2) {
                hwType = config->device_type;
                m_hwPixelFormat = config->pix_fmt;
                hwSupported = true;
                break;
            }
        }
    }

    if (!hwSupported) {
        LogWarn << "No suitable hardware decoder found";
        return false;
    }

    // 创建硬件设备上下文
    int ret = av_hwdevice_ctx_create(&m_hwDeviceContext, hwType, nullptr, nullptr, 0);
    if (ret < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, error, sizeof(error));
        LogWarn << "Failed to create hardware device context: " << error;
        return false;
    }

    // 设置硬件解码
    m_codecContext->hw_device_ctx = av_buffer_ref(m_hwDeviceContext);
    LogInfo << "Hardware decoder initialized: " << av_hwdevice_get_type_name(hwType);

    return true;
}

bool VideoDecodeThread::createSwsContext() {
    if (m_swsContext) {
        return true;
    }

    // 确定源像素格式（硬件解码时使用软件帧格式）
    AVPixelFormat srcFormat = m_hardwareDecoding ?
                                  static_cast<AVPixelFormat>(m_hwFrame->format) :
                                  static_cast<AVPixelFormat>(m_frame->format);

    // 创建SWS上下文
    m_swsContext = sws_getContext(
        m_videoSize.width(), m_videoSize.height(), srcFormat,
        m_targetSize.width(), m_targetSize.height(), AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
        );

    if (!m_swsContext) {
        emit errorOccurred("Failed to create image conversion context");
        return false;
    }

    // 分配图像缓冲区
    m_imageBufferSize = av_image_get_buffer_size(
        AV_PIX_FMT_RGBA, m_targetSize.width(), m_targetSize.height(), 1
        );

    if (m_imageBufferSize <= 0) {
        emit errorOccurred("Invalid image buffer size");
        return false;
    }

    m_imageBuffer = std::make_unique<uint8_t[]>(m_imageBufferSize);

    LogInfo << "SWS context created for conversion: "
            << av_get_pix_fmt_name(srcFormat) << " -> RGBA "
            << m_videoSize.width() << "x" << m_videoSize.height()
            << " -> " << m_targetSize.width() << "x" << m_targetSize.height();

    return true;
}

bool VideoDecodeThread::decodePacket(AVPacket *packet) {
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
            return true;
        }
        if (ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, error, sizeof(error));
            LogWarn << "Error receiving frame from decoder: " << error;
            return false;
        }

        // 处理解码帧
        processDecodedFrame(m_frame);

        // 释放帧引用
        av_frame_unref(m_frame);
    }

    return true;
}

void VideoDecodeThread::processDecodedFrame(AVFrame *frame) {
    AVFrame *frameToProcess = frame;

    // 硬件解码处理
    if (m_hardwareDecoding && frame->format == m_hwPixelFormat) {
        // 将硬件帧传输到软件帧
        int ret = av_hwframe_transfer_data(m_hwFrame, frame, 0);
        if (ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, error, sizeof(error));
            LogWarn << "Failed to transfer hardware frame: " << error;
            return;
        }

        // 复制帧属性
        av_frame_copy_props(m_hwFrame, frame);
        frameToProcess = m_hwFrame;
    }

    // 创建SWS上下文（如果不存在）
    if (!createSwsContext()) {
        return;
    }

    // 转换帧为QImage
    QImage image = convertFrameToImage(frameToProcess);
    if (!image.isNull()) {
        emit videoFrameDecoded(image);
    }

    // 释放硬件帧（如果使用）
    if (frameToProcess == m_hwFrame) {
        av_frame_unref(m_hwFrame);
    }
}

QImage VideoDecodeThread::convertFrameToImage(AVFrame *frame) {
    uint8_t *dstData[1] = { m_imageBuffer.get() };
    int dstLinesize[1] = { m_targetSize.width() * 4 };

    // 转换帧格式
    int ret = sws_scale(m_swsContext,
                        frame->data, frame->linesize,
                        0, frame->height,
                        dstData, dstLinesize);

    if (ret <= 0) {
        LogWarn << "Failed to convert frame to image";
        return QImage();
    }

    // 创建QImage（使用内存拷贝）
    QImage image(m_imageBuffer.get(),
                 m_targetSize.width(), m_targetSize.height(),
                 QImage::Format_RGBA8888);

    return image.copy();
}

void VideoDecodeThread::cleanup() {
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

    if (m_hwFrame) {
        av_frame_free(&m_hwFrame);
        m_hwFrame = nullptr;
    }

    // 释放解码器上下文
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }

    // 释放硬件设备上下文
    if (m_hwDeviceContext) {
        av_buffer_unref(&m_hwDeviceContext);
        m_hwDeviceContext = nullptr;
    }

    // 释放SWS上下文
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }

    // 释放图像缓冲区
    m_imageBuffer.reset();
    m_imageBufferSize = 0;

    // 重置状态
    m_running = false;
    m_flushing = false;
    m_hwPixelFormat = AV_PIX_FMT_NONE;

    LogInfo << "Video decoder resources cleaned up";
}
