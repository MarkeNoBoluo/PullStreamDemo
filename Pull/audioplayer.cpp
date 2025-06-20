#include "audioplayer.h"
#include <QAudioDeviceInfo>
#include <QCoreApplication>
#include <QTimer>
#include "Logger.h"


AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent)
{
    // 创建写入定时器
    m_writeTimer = new QTimer(this);
    m_writeTimer->setInterval(20); // 20ms写入周期
    connect(m_writeTimer, &QTimer::timeout, this, &AudioPlayer::writeAudioData);

    // 初始化时钟计时器
    m_clockTimer.start();
}

AudioPlayer::~AudioPlayer()
{
    stop();
    if (m_audioOutput) {
        delete m_audioOutput;
        m_audioOutput = nullptr;
    }
}

bool AudioPlayer::initialize(int sampleRate, int channels, int sampleSize)
{
    if (m_initialized) {
        LogWarn << "Audio player already initialized";
        return true;
    }

    // 保存音频参数
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_sampleSize = sampleSize;

    // 设置音频格式
    setupAudioFormat();

    // 检查设备支持
    QAudioDeviceInfo deviceInfo = QAudioDeviceInfo::defaultOutputDevice();
    if (!deviceInfo.isFormatSupported(m_audioFormat)) {
        LogWarn << "Preferred audio format not supported, using nearest match";
        m_audioFormat = deviceInfo.nearestFormat(m_audioFormat);
    }

    // 创建音频输出
    m_audioOutput = new QAudioOutput(m_audioFormat, this);
    if (!m_audioOutput) {
        emit errorOccurred("Failed to create audio output device");
        return false;
    }

    // 连接状态变化信号
    connect(m_audioOutput, &QAudioOutput::stateChanged,
            this, &AudioPlayer::handleStateChanged);

    // 设置初始音量
    m_audioOutput->setVolume(0.8f);

    LogInfo << "Audio player initialized: "
            << "Sample rate: " << m_audioFormat.sampleRate()
            << " Channels: " << m_audioFormat.channelCount()
            << " Sample size: " << m_audioFormat.sampleSize();

    m_initialized = true;
    return true;
}

void AudioPlayer::setupAudioFormat()
{
    m_audioFormat.setSampleRate(m_sampleRate);
    m_audioFormat.setChannelCount(m_channels);
    m_audioFormat.setSampleSize(m_sampleSize);
    m_audioFormat.setCodec("audio/pcm");
    m_audioFormat.setByteOrder(QAudioFormat::LittleEndian);
    m_audioFormat.setSampleType(m_sampleSize == 8 ?
                                    QAudioFormat::UnSignedInt :
                                    QAudioFormat::SignedInt);
}

void AudioPlayer::setVolume(float volume) {
    if (!m_audioOutput) return;

    volume = qBound(0.0f, volume, 1.0f);
    m_audioOutput->setVolume(volume);
}

float AudioPlayer::volume() const {
    return m_audioOutput ? m_audioOutput->volume() : 0.0f;
}

bool AudioPlayer::isPlaying() const
{
    return m_playing;
}

void AudioPlayer::start() {
    if (!m_initialized) {
        emit errorOccurred("Audio player not initialized");
        return;
    }

    if (m_playing) {
        return;
    }

    // 清空缓冲区
    clearBuffer();

    // 启动音频设备
    m_audioDevice = m_audioOutput->start();
    if (!m_audioDevice) {
        emit errorOccurred("Failed to start audio output");
        return;
    }

    m_playing = true;
    m_paused = false;

    // 启动写入定时器
    m_writeTimer->start();

    // 重置时钟
    m_clockTimer.restart();
    m_bytesWritten = 0;

    LogInfo << "Audio playback started";
}

void AudioPlayer::stop() {
    if (!m_playing) {
        return;
    }

    // 停止定时器
    m_writeTimer->stop();

    // 停止音频输出
    if (m_audioOutput) {
        m_audioOutput->stop();
    }

    m_audioDevice = nullptr;
    m_playing = false;
    m_paused = false;

    // 清空缓冲区
    clearBuffer();

    LogInfo << "Audio playback stopped";
}

void AudioPlayer::pause() {
    if (!m_playing || m_paused) {
        return;
    }

    if (m_audioOutput) {
        m_audioOutput->suspend();
    }

    m_paused = true;
    m_writeTimer->stop();

    LogInfo << "Audio playback paused";
}

void AudioPlayer::resume() {
    if (!m_playing || !m_paused) {
        return;
    }

    if (m_audioOutput) {
        m_audioOutput->resume();
    }

    m_paused = false;
    m_writeTimer->start();

    LogInfo << "Audio playback resumed";
}

void AudioPlayer::clearBuffer() {
    QMutexLocker locker(&m_bufferMutex);
    m_audioBuffer.clear();
}

qint64 AudioPlayer::audioClock() {
    QMutexLocker locker(&m_clockMutex);
    return m_audioClock;
}

void AudioPlayer::onAudioFrameReady(std::shared_ptr<AVFrame> frame) {
    if (!frame || !m_playing) {
        return;
    }

    // 转换音频帧
    QByteArray audioData = convertAudioFrame(frame);
    if (audioData.isEmpty()) {
        return;
    }

    // 添加到缓冲区
    {
        QMutexLocker locker(&m_bufferMutex);

        // 缓冲区管理
        const int MAX_BUFFER_SIZE = 50; // 最大50帧缓冲
        while (m_audioBuffer.size() >= MAX_BUFFER_SIZE) {
            m_audioBuffer.dequeue();
            LogWarn << "Audio buffer overflow, dropping frame";
        }

        m_audioBuffer.enqueue(audioData);
    }
}

QByteArray AudioPlayer::convertAudioFrame(std::shared_ptr<AVFrame> frame) {
    if (!frame) {
        return QByteArray();
    }

    // 计算数据大小
    int dataSize = frame->nb_samples * frame->channels *
                   av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format));

    if (dataSize <= 0) {
        LogWarn << "Invalid audio frame size: " << dataSize;
        return QByteArray();
    }

    // 创建QByteArray并复制数据
    return QByteArray(reinterpret_cast<const char*>(frame->data[0]), dataSize);
}

void AudioPlayer::writeAudioData() {
    if (!m_audioDevice || !m_playing || m_paused) {
        return;
    }

    // 检查可写入空间
    int freeBytes = m_audioOutput->bytesFree();
    if (freeBytes <= 0) {
        return;
    }

    QMutexLocker locker(&m_bufferMutex);

    // 检查是否有数据可写
    if (m_audioBuffer.isEmpty()) {
        return;
    }

    // 写入数据
    int bytesWritten = 0;
    while (!m_audioBuffer.isEmpty() && freeBytes > 0) {
        QByteArray &data = m_audioBuffer.head();
        int bytesToWrite = qMin(data.size(), freeBytes);

        // 写入数据
        int written = m_audioDevice->write(data.constData(), bytesToWrite);
        if (written < 0) {
            emit errorOccurred("Failed to write audio data");
            break;
        }

        bytesWritten += written;
        freeBytes -= written;
        m_bytesWritten += written;

        // 更新音频时钟
        {
            QMutexLocker clockLocker(&m_clockMutex);
            // 计算基于数据量的时钟
            double bytesPerMs = (m_sampleRate * m_channels * (m_sampleSize / 8.0)) / 1000.0;
            m_audioClock = m_clockTimer.elapsed() + (m_bytesWritten / bytesPerMs);
            emit audioClockUpdated(m_audioClock);
        }

        // 处理未完全写入的情况
        if (written < data.size()) {
            // 移除已写入部分
            data = data.mid(written);
            break;
        }

        // 移除已完全写入的数据
        m_audioBuffer.dequeue();
    }

    // 性能日志
    static QElapsedTimer logTimer;
    if (logTimer.elapsed() > 5000) {
        LogDebug << "Audio buffer: " << m_audioBuffer.size() << " frames, "
                 << "Bytes written: " << bytesWritten;
        logTimer.restart();
    }
}

void AudioPlayer::handleStateChanged(QAudio::State state) {
    emit stateChanged(state);

    switch (state) {
    case QAudio::ActiveState:
        LogInfo << "Audio state: Active";
        break;
    case QAudio::SuspendedState:
        LogInfo << "Audio state: Suspended";
        break;
    case QAudio::StoppedState:
        // 检查是否出错
        if (m_audioOutput && m_audioOutput->error() != QAudio::NoError) {
            emit errorOccurred(QString("Audio error: %1").arg(m_audioOutput->error()));
        }
        LogInfo << "Audio state: Stopped";
        break;
    case QAudio::IdleState:
        LogInfo << "Audio state: Idle";
        break;
    }
}

void AudioPlayer::updateAudioClock(qint64 pts) {
    QMutexLocker locker(&m_clockMutex);
    m_audioClock = pts;
}
