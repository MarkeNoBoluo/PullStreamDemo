#ifndef AUDIODECODETHREAD_H
#define AUDIODECODETHREAD_H

#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QAtomicInteger>
#include <memory>
#include "DataStruct.h"
#include <QWaitCondition>


class AudioDecodeThread : public QThread
{
    Q_OBJECT
public:
    explicit AudioDecodeThread(QObject *parent = nullptr);
    ~AudioDecodeThread();

    bool init(AVCodecParameters *codecParams);
    // 设置目标音频参数
    void setTargetFormat(int sampleRate, int channels, AVSampleFormat format);

    // 关闭解码器
    void close();

    // 获取音频信息
    int sampleRate() const { return m_targetSampleRate; }
    int channels() const { return m_targetChannels; }
    AVSampleFormat sampleFormat() const { return m_targetFormat; }

signals:
    void audioFrameDecoded(std::shared_ptr<AVFrame> frame);

    // 音频时钟更新
    void audioClockUpdated(qint64 pts);

    void errorOccurred(const QString &error);

public slots:
    void onAudioPacketReceived(AVPacket *packet);

    // 更新播放状态
    void setPaused(bool paused);

protected:
    void run() override;

private:
    // 初始化重采样器
    bool initResampler();

    // 解码音频包
    bool decodePacket(AVPacket *packet);

    // 重采样音频帧
    std::shared_ptr<AVFrame> resampleFrame(AVFrame *frame);

    // 清理资源
    void cleanup();

private:
    // 成员变量
    AVCodecContext *m_codecContext = nullptr;
    SwrContext *m_swrContext = nullptr;
    const AVCodec *m_codec = nullptr;

    // 帧处理
    AVFrame *m_frame = nullptr;

    // 包队列
    QQueue<AVPacket*> m_packetQueue;
    QMutex m_queueMutex;
    QWaitCondition m_queueCondition;

    // 音频参数
    int m_targetSampleRate = 44100;
    int m_targetChannels = 2;
    AVSampleFormat m_targetFormat = AV_SAMPLE_FMT_S16;

    // 状态控制
    std::atomic_bool m_running{false};
    std::atomic_bool m_paused{false};
    std::atomic_bool m_flushing{false};
};

#endif // AUDIODECODETHREAD_H
