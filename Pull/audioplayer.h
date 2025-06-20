#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <QObject>
#include <QIODevice>
#include <QAudioOutput>
#include <QAudioFormat>
#include <QBuffer>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <memory>
#include <QElapsedTimer>

#include "DataStruct.h"


class AudioPlayer : public QObject
{
    Q_OBJECT

public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer();

    // 初始化音频输出
    bool initialize(int sampleRate = 44100, int channels = 2, int sampleSize = 16);

    // 控制播放
    void start();
    void stop();
    void pause();
    void resume();

    // 设置音量 (0.0 - 1.0)
    void setVolume(float volume);

    // 获取当前音量
    float volume() const;

    // 获取状态
    bool isPlaying() const;

    // 清空缓冲区
    void clearBuffer();

    // 获取当前音频时钟 (毫秒)
    qint64 audioClock() ;

public slots:
    // 接收音频帧
    void onAudioFrameReady(std::shared_ptr<AVFrame> frame);

    // 更新音频时钟 (用于外部同步)
    void updateAudioClock(qint64 pts);
signals:
    // 状态变化信号
    void stateChanged(QAudio::State state);

    // 错误信号
    void errorOccurred(const QString &error);

    // 音频时钟更新信号
    void audioClockUpdated(qint64 pts);

private slots:
    // 处理状态变化
    void handleStateChanged(QAudio::State state);

    // 写入音频数据
    void writeAudioData();

private:
    // 设置音频格式
    void setupAudioFormat();

    // 转换音频帧
    QByteArray convertAudioFrame(std::shared_ptr<AVFrame> frame);

private:
    QAudioOutput *m_audioOutput = nullptr;
    QIODevice *m_audioDevice = nullptr;

    // 音频格式
    QAudioFormat m_audioFormat;
    int m_sampleRate = 44100;
    int m_channels = 2;
    int m_sampleSize = 16;

    // 音频缓冲区
    QQueue<QByteArray> m_audioBuffer;
    mutable QMutex m_bufferMutex;

    // 音频时钟
    qint64 m_audioClock = 0;
    QMutex m_clockMutex;

    // 播放状态
    std::atomic_bool m_initialized{false};
    std::atomic_bool m_playing{false};
    std::atomic_bool m_paused{false};

    // 写入定时器
    QTimer *m_writeTimer = nullptr;

    // 性能统计
    qint64 m_bytesWritten = 0;
    QElapsedTimer m_clockTimer;
};

#endif // AUDIOPLAYER_H
