#ifndef VIDEODECODETHREAD_H
#define VIDEODECODETHREAD_H

#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QSize>
#include <QImage>
#include <memory>
#include <QWaitCondition>
#include "DataStruct.h"


class VideoDecodeThread : public QThread
{
    Q_OBJECT
public:
    explicit VideoDecodeThread(QObject *parent = nullptr);
    ~VideoDecodeThread();

    bool init(AVCodecParameters *codecParams);
    void setTargetSize(const QSize &size);

    // 设置硬件解码
    void setHardwareDecoding(const bool &enable);

    // 关闭解码器
    void close();

    // 获取当前解码帧率
    double currentFrameRate() const { return m_frameRate; }

    // 获取视频尺寸
    QSize videoSize() const { return m_videoSize; }

    double frameRate() const;
    void setFrameRate(double newFrameRate);

signals:
    // 视频帧就绪信号
    void videoFrameDecoded(const QImage& image);
    // 错误信号
    void errorOccurred(const QString &error);
    // 视频信息信号
    void videoInfoUpdated(int width, int height, double frameRate);

public slots:
    // 接收视频包
    void onVideoPacketReceived(AVPacket *packet);
    // 更新音频时钟
    void updateAudioClock(qint64 clock);


protected:
    void run() override;

private:
    // 初始化硬件解码器
    bool initHardwareDecoder();

    // 创建SWS上下文
    bool createSwsContext();

    // 解码视频包
    bool decodePacket(AVPacket *packet);

    // 处理解码帧
    void processDecodedFrame(AVFrame *frame);

    // 转换帧为QImage
    QImage convertFrameToImage(AVFrame *frame);

    // 清理资源
    void cleanup();

private:
    // 成员变量
    AVCodecContext *m_codecContext = nullptr;
    AVBufferRef *m_hwDeviceContext = nullptr;
    const AVCodec *m_codec = nullptr;

    // 帧处理
    AVFrame *m_frame = nullptr;
    AVFrame *m_hwFrame = nullptr;
    enum AVPixelFormat m_hwPixelFormat = AV_PIX_FMT_NONE;


    // 转换
    SwsContext *m_swsContext = nullptr;
    std::unique_ptr<uint8_t[]> m_imageBuffer;
    int m_imageBufferSize = 0;

    // 包队列
    QQueue<AVPacket*> m_packetQueue;
    QMutex m_queueMutex;
    QWaitCondition m_queueCondition;

    // 状态控制
    std::atomic_bool m_running{false};
    std::atomic_bool m_hardwareDecoding{false};
    std::atomic_bool m_flushing{false};

    // 视频信息
    QSize m_targetSize;
    QSize m_videoSize;
    double m_frameRate = 0.0;
    std::atomic<qint64> m_audioClock{0};
};

#endif // VIDEODECODETHREAD_H
