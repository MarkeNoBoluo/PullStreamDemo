#include "audioplayer.h"
#include <QAudioDeviceInfo>
#include <QCoreApplication>
#include "Logger.h"


AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent)
{
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
        QAudioFormat nearestFormat = deviceInfo.nearestFormat(m_audioFormat);
        LogInfo << "Original format: " << m_audioFormat.sampleRate() << "Hz, "
                << m_audioFormat.channelCount() << "ch, " << m_audioFormat.sampleSize() << "bit";
        LogInfo << "Nearest format: " << nearestFormat.sampleRate() << "Hz, "
                << nearestFormat.channelCount() << "ch, " << nearestFormat.sampleSize() << "bit";
        m_audioFormat = nearestFormat;

        // 更新内部参数以匹配实际格式
        m_sampleRate = m_audioFormat.sampleRate();
        m_channels = m_audioFormat.channelCount();
        m_sampleSize = m_audioFormat.sampleSize();
    }

    // 创建音频输出
    m_audioOutput = new QAudioOutput(m_audioFormat, this);
    if (!m_audioOutput) {
        LogErr << ("Failed to create audio output device");
        return false;
    }

    // 连接状态变化信号
    connect(m_audioOutput, &QAudioOutput::stateChanged,
            this, &AudioPlayer::handleStateChanged);

    // 设置初始音量
    m_audioOutput->setVolume(1.0f);

    // 1. 计算单个音频帧的大小
    int frameSize = 1024 * channels * (sampleSize / 8);  // 通常是4096字节

    // 2. 设置缓冲区为3-4个帧的大小，平衡延迟和稳定性
    int optimalBufferSize = frameSize * 3;  // 约12KB，延迟约92ms

    // 3. 确保不小于系统最小要求
    int minBufferSize = frameSize * 2;     // 最小2个帧
    int maxBufferSize = frameSize * 6;     // 最大6个帧，限制延迟

    int bufferSize = qBound(minBufferSize, optimalBufferSize, maxBufferSize);

    m_audioOutput->setBufferSize(bufferSize);

    LogInfo << "Audio player initialized: "
            << "Sample rate: " << m_audioFormat.sampleRate()
            << " Channels: " << m_audioFormat.channelCount()
            << " Sample size: " << m_audioFormat.sampleSize()
            << " Buffer size: " << m_audioOutput->bufferSize();

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
    m_audioFormat.setSampleType(QAudioFormat::SignedInt);
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

    // 启动音频设备
    m_audioDevice = m_audioOutput->start();
    if (!m_audioDevice) {
        emit errorOccurred("Failed to start audio output");
        return;
    }

    m_playing = true;
    m_paused = false;

    // 设置 notify 信号，减少触发频率避免过度调用
    m_audioOutput->setNotifyInterval(10);  // 改为10ms
    connect(m_audioOutput, &QAudioOutput::notify, this, &AudioPlayer::writeAudioData, Qt::UniqueConnection);
    LogDebug << "输出缓冲区大小：" <<m_audioOutput->bufferSize();
    // 重置时钟
    m_clockTimer.restart();
    m_bytesWritten = 0;

    LogInfo << "Audio playback started, waiting for audio data...";
}

void AudioPlayer::stop() {
    if (!m_playing) {
        return;
    }

    // 断开notify信号连接
    if (m_audioOutput) {
        disconnect(m_audioOutput, &QAudioOutput::notify, this, &AudioPlayer::writeAudioData);
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
        LogErr << "音频数据转换失败";
        return;
    }

    // 添加到缓冲区
    {
        QMutexLocker locker(&m_bufferMutex);

        // 防止缓冲区溢出
        while (m_audioBuffer.size() >= m_maxBufferSize) {
            m_audioBuffer.dequeue();
            LogWarn << "Audio buffer overflow, dropping frame";
        }

        m_audioBuffer.enqueue(audioData);
        LogDebug << "音频帧已添加到缓冲区，当前缓冲区大小:" << m_audioBuffer.size();
    }

    // 立即尝试写入数据，确保音频流能够激活
    if (m_playing && !m_paused) {
        writeAudioData();
    }
}

QByteArray AudioPlayer::convertAudioFrame(std::shared_ptr<AVFrame> frame) {
    if (!frame || frame->format != AV_SAMPLE_FMT_S16) {
        LogWarn << "Invalid frame or format not S16, format:" << frame->format;
        return QByteArray();
    }

    // 验证帧参数与播放器设置是否匹配
    if (frame->sample_rate != m_sampleRate) {
        LogWarn << "Sample rate mismatch: frame=" << frame->sample_rate
                << ", player=" << m_sampleRate;
        // 可以选择继续处理或返回空数据
    }

    if (frame->channels != m_channels) {
        LogWarn << "Channel count mismatch: frame=" << frame->channels
                << ", player=" << m_channels;
    }

    // 计算实际数据大小 (nb_samples * channels * 2 bytes per sample)
    int dataSize = frame->nb_samples * frame->channels * 2;
    if (dataSize <= 0 || !frame->data[0]) {
        LogWarn << "Invalid audio frame data, size:" << dataSize;
        return QByteArray();
    }

    // 验证数据大小是否为帧大小的倍数
    int frameSize = (m_sampleSize / 8) * m_channels;
    if (dataSize % frameSize != 0) {
        LogWarn << "Audio data size not aligned to frame boundary: "
                << dataSize << " bytes, frame size: " << frameSize;
    }

    LogDebug << "Audio frame converted - samples:" << frame->nb_samples
             << " channels:" << frame->channels
             << " sample_rate:" << frame->sample_rate
             << " data size:" << dataSize;

    return QByteArray(reinterpret_cast<const char*>(frame->data[0]), dataSize);
}

void AudioPlayer::setMaxBufferSize(int newMaxBufferSize)
{
    m_maxBufferSize = newMaxBufferSize;
}

int AudioPlayer::getBufferDelayMs() const {
    if (!m_audioOutput) return 0;

    // 计算缓冲区延迟（毫秒）
    int bufferSize = m_audioOutput->bufferSize();
    double bytesPerMs = (m_sampleRate * m_channels * (m_sampleSize / 8.0)) / 1000.0;
    return static_cast<int>(bufferSize / bytesPerMs);
}

// 优化writeAudioData逻辑，减少碎片化写入
void AudioPlayer::writeAudioData() {
    if (!m_audioDevice || !m_playing || m_paused) {
        return;
    }

    QMutexLocker locker(&m_bufferMutex);
    if (m_audioBuffer.isEmpty()) {
        return;
    }

    int totalBytesWritten = 0;
    int frameSize = (m_sampleSize / 8) * m_channels;

    // === 批量写入策略 ===
    while (!m_audioBuffer.isEmpty()) {
        int freeBytes = m_audioOutput->bytesFree();

        // 如果可用空间太小，等待下次
        if (freeBytes < frameSize * 2) {  // 至少需要2个采样点的空间
            LogDebug << "Audio buffer nearly full, free bytes:" << freeBytes
                     << ", waiting for more space";
            break;
        }

        QByteArray &data = m_audioBuffer.head();
        int bytesToWrite = qMin(data.size(), freeBytes);

        // 确保写入完整的采样点
        bytesToWrite = (bytesToWrite / frameSize) * frameSize;

        if (bytesToWrite <= 0) {
            break;
        }

        int written = m_audioDevice->write(data.constData(), bytesToWrite);
        if (written <= 0) {
            if (written < 0) {
                LogErr << "Failed to write audio data, error:" << written;
                emit errorOccurred("Failed to write audio data");
            }
            break;
        }

        totalBytesWritten += written;
        m_bytesWritten += written;

        // 更新音频时钟（简化版本）
        updateAudioClockFromBytes();

        // 处理数据更新
        if (written >= data.size()) {
            m_audioBuffer.dequeue();
        } else {
            data = data.mid(written);
            break;
        }
    }

    if (totalBytesWritten > 0) {
        LogDebug << "成功写入音频数据:" << totalBytesWritten << "字节"
                 << ", 剩余缓冲区:" << m_audioBuffer.size()
                 << ", 可写空间:" << m_audioOutput->bytesFree()
                 << ", 缓冲区使用率:" << (100.0 * (m_audioOutput->bufferSize() - m_audioOutput->bytesFree()) / m_audioOutput->bufferSize()) << "%";
    }
}

// 简化的音频时钟更新
void AudioPlayer::updateAudioClockFromBytes() {
    QMutexLocker clockLocker(&m_clockMutex);

    // 计算基于字节的播放位置
    double bytesPerMs = (m_sampleRate * m_channels * (m_sampleSize / 8.0)) / 1000.0;
    if (bytesPerMs > 0) {
        // 考虑缓冲区中还未播放的数据
        int bufferedBytes = m_audioOutput->bufferSize() - m_audioOutput->bytesFree();
        qint64 playedBytes = qMax(0LL, m_bytesWritten - bufferedBytes);

        m_audioClock = static_cast<qint64>(playedBytes / bytesPerMs);
        LogDebug << "音频时钟:"<<m_audioClock;
        emit audioClockUpdated(m_audioClock);
    }
}

void AudioPlayer::handleStateChanged(QAudio::State state) {
    emit stateChanged(state);

    switch (state) {
    case QAudio::ActiveState:
        LogInfo << "Audio state: Active - 音频流已激活";
        break;
    case QAudio::SuspendedState:
        LogInfo << "Audio state: Suspended - 音频流已暂停";
        break;
    case QAudio::StoppedState:
        // 检查是否出错
        if (m_audioOutput && m_audioOutput->error() != QAudio::NoError) {
            LogErr << "Audio error:" << m_audioOutput->error();
            emit errorOccurred(QString("Audio error: %1").arg(m_audioOutput->error()));
        }
        LogInfo << "Audio state: Stopped - 音频流已停止";
        break;
    case QAudio::IdleState:
        LogInfo << "Audio state: Idle - 等待音频数据，缓冲区大小:" << m_audioBuffer.size();
            // 在Idle状态时尝试写入数据
            if (m_playing && !m_paused) {
            writeAudioData();
        }
        break;
    }
}

void AudioPlayer::updateAudioClock(qint64 pts) {
    QMutexLocker locker(&m_clockMutex);
    m_audioClock = pts;
}
