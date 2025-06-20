#ifndef STREAMPULLTHREAD_H
#define STREAMPULLTHREAD_H

#include <QThread>
#include <QMutex>
#include "DataStruct.h"



class StreamPullThread : public QThread
{
    Q_OBJECT
public:
    explicit StreamPullThread(QObject *parent = nullptr);
    ~StreamPullThread();

    bool open(const QString &url, int timeoutMs = 5000);
    void close();
    // 设置硬件解码
    void setHardwareDecoding(bool enable);

    // 设置超时时间
    void setTimeout(int timeoutMs);

    // 获取视频流信息
    int videoStreamIndex() const { return m_videoStreamIndex; }

    // 获取音频流信息
    int audioStreamIndex() const { return m_audioStreamIndex; }

    // 获取编解码参数
    AVCodecParameters* videoCodecParameters() const;
    AVCodecParameters* audioCodecParameters() const;

    // 获取格式上下文
    AVFormatContext* formatContext() const { return m_formatContext; }
signals:
    // 视频包就绪信号
    void videoPacketReady(AVPacket *packet);

    // 音频包就绪信号
    void audioPacketReady(AVPacket *packet);

    // 错误信号
    void errorOccurred(const QString &error);

    // 流信息就绪信号
    void streamInfoReady(int width, int height, double frameRate);

protected:
    void run() override;

private:
    // 初始化FFmpeg
    void initFFmpeg();

    // 打开输入流
    bool openInput(const QString &url);

    // 查找流信息
    bool findStreamInfo();

    // 处理数据包
    void processPacket(AVPacket *packet);

    // 清理资源
    void cleanup();

private:
    AVFormatContext *m_formatContext = nullptr;
    AVDictionary *m_options = nullptr;

    int m_audioStreamIndex = -1;
    int m_videoStreamIndex = -1;

    std::atomic_bool m_running{false};
    std::atomic_bool m_hardwareDecoding{false};
    int m_timeoutMs = 5000;
    QMutex m_mutex;
};

#endif // STREAMPULLTHREAD_H
