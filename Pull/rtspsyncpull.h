#ifndef RTSPSYNCPULL_H
#define RTSPSYNCPULL_H

#include <QObject>
#include <QMutex>
#include <memory>
#include "DataStruct.h"

class PlayImage;
class StreamPullThread;
class AudioDecodeThread;
class VideoDecodeThread;
class AudioPlayer;

class RTSPSyncPull : public QObject
{
    Q_OBJECT
public:
    explicit RTSPSyncPull(QObject *parent = nullptr);
    ~RTSPSyncPull();

    void start(const QString &rtspUrl);
    void stop();
    void pause();
    void resume();
    void setVideoOutput(PlayImage *videoOutput);

    // 获取时钟信息
    qint64 getAudioClock() ;
    qint64 getVideoClock() ;

    // 获取播放状态
    bool isPlaying() const;

signals:
    void errorOccurred(const QString &error);
    void playbackStarted();
    void playbackStopped();
    void stateChanged(PushState state,const QString &objName);

public slots:
    void handleAudioDecoded(std::shared_ptr<AVFrame> frame);
    void handleVideoDecoded(const QImage& image);

private:
    // 初始化方法
    bool initializeDecoders();
    bool initializeAudioPlayer();

    // 信号连接管理
    void connectSignals();
    void disconnectSignals();

private:
    StreamPullThread *m_pullThread;      // 拉流线程
    AudioDecodeThread *m_audioDecodeThread; // 音频解码线程
    VideoDecodeThread *m_videoDecodeThread; // 视频解码线程
    AudioPlayer *m_audioPlayer;          // 音频播放器
    PlayImage *m_videoOutput;            // 视频显示组件

    // 同步控制
    qint64 m_audioClock = 0;             // 音频时钟 (主时钟)
    qint64 m_videoClock = 0;             // 视频时钟
    QMutex m_clockMutex;

};

#endif // RTSPSYNCPULL_H
