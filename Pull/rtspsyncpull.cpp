#include "rtspsyncpull.h"
#include "streampullthread.h"
#include "audiodecodethread.h"
#include "videodecodethread.h"
#include "audioplayer.h"
#include "playimage.h"
#include "Logger.h"
#include <QImage>

RTSPSyncPull::RTSPSyncPull(QObject *parent)
    : QObject{parent}
    , m_pullThread(nullptr)
    , m_audioDecodeThread(nullptr)
    , m_videoDecodeThread(nullptr)
    , m_audioPlayer(nullptr)
    , m_videoOutput(nullptr)
    , m_audioClock(0)
    , m_videoClock(0)
{
    // 创建线程对象
    m_pullThread = new StreamPullThread(this);
    m_pullThread->setHardwareDecoding(false);
    m_audioDecodeThread = new AudioDecodeThread(this);

    m_videoDecodeThread = new VideoDecodeThread(this);
    m_videoDecodeThread->setTargetSize(QSize(1280, 720));
    m_audioPlayer = new AudioPlayer(this);
}

RTSPSyncPull::~RTSPSyncPull()
{
    stop();
}

void RTSPSyncPull::start(const QString &rtspUrl)
{
    if (rtspUrl.isEmpty()) {
        emit errorOccurred("RTSP URL 不能为空");
        return;
    }

//    // 先停止当前播放
//    stop();

    emit stateChanged(PushState::decode,this->objectName());
    // 设置拉流参数
    m_pullThread->setTimeout(3000); // 10秒超时
    m_pullThread->setHardwareDecoding(true); // 启用硬件解码

    // 打开RTSP流
    if (!m_pullThread->open(rtspUrl)) {
        emit errorOccurred("无法打开RTSP流: " + rtspUrl);
        return;
    }

    // 初始化解码器
    if (!initializeDecoders()) {
        emit errorOccurred("初始化解码器失败");
        stop();
        return;
    }

    // 初始化音频播放器
    if (!initializeAudioPlayer()) {
        emit errorOccurred("初始化音频播放器失败");
        stop();
        return;
    }

    // 连接信号槽
    connectSignals();
    emit stateChanged(PushState::play,this->objectName());

    // 启动线程
    m_pullThread->start();
    m_audioDecodeThread->start();
    m_videoDecodeThread->start();
    m_audioPlayer->start();

    emit playbackStarted();
}

void RTSPSyncPull::stop()
{
    emit stateChanged(PushState::end,this->objectName());
    // 断开信号连接
    disconnectSignals();

    // 停止线程
    if (m_pullThread) {
        m_pullThread->close();
        if (m_pullThread->isRunning()) {
            m_pullThread->quit();
            m_pullThread->wait(3000);
        }
    }

    if (m_audioDecodeThread) {
        m_audioDecodeThread->close();
        if (m_audioDecodeThread->isRunning()) {
            m_audioDecodeThread->quit();
            m_audioDecodeThread->wait(3000);
        }
    }

    if (m_videoDecodeThread) {
        m_videoDecodeThread->close();
        if (m_videoDecodeThread->isRunning()) {
            m_videoDecodeThread->quit();
            m_videoDecodeThread->wait(3000);
        }
    }

    if (m_audioPlayer) {
        m_audioPlayer->stop();
        m_audioPlayer->clearBuffer();
    }

    // 重置时钟
    QMutexLocker locker(&m_clockMutex);
    m_audioClock = 0;
    m_videoClock = 0;

    emit playbackStopped();
}

void RTSPSyncPull::pause()
{
    if (m_audioPlayer) {
        m_audioPlayer->pause();
    }

    if (m_audioDecodeThread) {
        m_audioDecodeThread->setPaused(true);
    }

    if (m_videoDecodeThread) {
        // 视频解码线程需要实现暂停功能
        // m_videoDecodeThread->setPaused(true);
    }
}

void RTSPSyncPull::resume()
{
    if (m_audioPlayer) {
        m_audioPlayer->resume();
    }

    if (m_audioDecodeThread) {
        m_audioDecodeThread->setPaused(false);
    }

    if (m_videoDecodeThread) {
        // 视频解码线程需要实现恢复功能
//         m_videoDecodeThread->setPaused(false);
    }
}

void RTSPSyncPull::setVideoOutput(PlayImage *videoOutput)
{
    m_videoOutput = videoOutput;
    this->setObjectName("Plauer");
    connect(this,&RTSPSyncPull::stateChanged,m_videoOutput,&PlayImage::onPlayState);
}

void RTSPSyncPull::handleAudioDecoded(std::shared_ptr<AVFrame> frame) {
    if (!frame) return;

    // 首次收到音频帧时确保播放器已启动
    static bool firstFrame = true;
    if (firstFrame && m_audioPlayer) {
        m_audioPlayer->start();
        firstFrame = false;
    }

    // 更新时钟和发送帧...
    if (frame->pts != AV_NOPTS_VALUE) {
        QMutexLocker locker(&m_clockMutex);
        m_audioClock = frame->pts;
    }

    if (m_audioPlayer) {
        m_audioPlayer->onAudioFrameReady(frame);
    }
}

void RTSPSyncPull::handleVideoDecoded(const QImage &image)
{
    if (image.isNull()) return;

    // 将视频帧显示到界面
    if (m_videoOutput) {
        m_videoOutput->updateImage(image);
    }
}

bool RTSPSyncPull::initializeDecoders()
{
    // 初始化音频解码器
    if (m_pullThread->audioStreamIndex() >= 0) {
        AVCodecParameters* audioParams = m_pullThread->audioCodecParameters();
        if (audioParams) {
            if (!m_audioDecodeThread->init(audioParams)) {
                LogErr << "音频解码器初始化失败";
                return false;
            }

            // 关键修改：使用原始采样率，不强制转换
            int originalSampleRate = audioParams->sample_rate;
            int originalChannels = audioParams->channels;

            // 如果原始采样率异常，使用默认值
            if (originalSampleRate <= 0) originalSampleRate = 44100;
            if (originalChannels <= 0) originalChannels = 2;

            LogInfo << "原始音频参数: " << originalSampleRate << "Hz, " << originalChannels << "ch";

            // 设置目标音频格式（保持原始采样率）
            m_audioDecodeThread->setTargetFormat(originalSampleRate, originalChannels, AV_SAMPLE_FMT_S16);
        }
    }

    // 初始化视频解码器
    if (m_pullThread->videoStreamIndex() >= 0) {
        AVCodecParameters* videoParams = m_pullThread->videoCodecParameters();
        if (videoParams) {
            if (!m_videoDecodeThread->init(videoParams)) {
                LogErr << "视频解码器初始化失败";
                return false;
            }

            // 启用硬件解码
            m_videoDecodeThread->setHardwareDecoding(false);

            // 设置目标尺寸（可选）
            if (m_videoOutput) {
                m_videoDecodeThread->setTargetSize(m_videoOutput->size());
            }
        }
    }

    return true;
}

bool RTSPSyncPull::initializeAudioPlayer()
{
    if (m_pullThread->audioStreamIndex() < 0) {
        LogDebug << "没有音频流，不需要初始化";
        return true;
    }

    // 关键修改：使用与解码器相同的音频参数
    int sampleRate = m_audioDecodeThread->sampleRate();
    int channels = m_audioDecodeThread->channels();

    LogInfo << "初始化音频播放器: " << sampleRate << "Hz, " << channels << "ch";

    // 增加缓冲区大小，改善播放稳定性
    m_audioPlayer->setMaxBufferSize(6144);

    // 使用正确的采样率初始化音频播放器
    if (!m_audioPlayer->initialize(sampleRate, channels, 16)) {
        LogErr << "音频播放器初始化失败";
        return false;
    }

    m_audioPlayer->setVolume(0.5f);
    LogInfo << "音频播放器初始化成功";
    return true;
}

void RTSPSyncPull::connectSignals()
{
    // 拉流线程信号连接
    connect(m_pullThread, &StreamPullThread::videoPacketReady,
            m_videoDecodeThread, &VideoDecodeThread::onVideoPacketReceived,
            Qt::QueuedConnection);

    connect(m_pullThread, &StreamPullThread::audioPacketReady,
            m_audioDecodeThread, &AudioDecodeThread::onAudioPacketReceived,
            Qt::QueuedConnection);

    connect(m_pullThread, &StreamPullThread::errorOccurred,
            this, &RTSPSyncPull::errorOccurred,
            Qt::QueuedConnection);

    connect(m_pullThread, &StreamPullThread::streamInfoReady,
        this, [this](int width, int height, double frameRate) {
            LogInfo << QString("流信息: %1x%2 @%3fps")
                           .arg(width).arg(height).arg(frameRate);
            if (m_videoOutput) {
                emit m_videoOutput->updatePlayWindowSize(QSize(width, height));
            }
            if(m_videoDecodeThread){
                m_videoDecodeThread->setFrameRate(frameRate);
            }
        }, Qt::QueuedConnection);

    // 音频解码线程信号连接
    qRegisterMetaType<std::shared_ptr<AVFrame>>("std::shared_ptr<AVFrame>");
    connect(m_audioDecodeThread, &AudioDecodeThread::audioFrameDecoded,
            this, &RTSPSyncPull::handleAudioDecoded,
            Qt::QueuedConnection);

    connect(m_audioDecodeThread, &AudioDecodeThread::audioClockUpdated,
        this, [this](qint64 pts) {
            QMutexLocker locker(&m_clockMutex);
            m_audioClock = pts;
        }, Qt::QueuedConnection);

    connect(m_audioDecodeThread, &AudioDecodeThread::errorOccurred,
            this, &RTSPSyncPull::errorOccurred,
            Qt::QueuedConnection);

    // 视频解码线程信号连接
    connect(m_videoDecodeThread, &VideoDecodeThread::videoFrameDecoded,
            this, &RTSPSyncPull::handleVideoDecoded,
            Qt::QueuedConnection);

    connect(m_videoDecodeThread, &VideoDecodeThread::errorOccurred,
            this, &RTSPSyncPull::errorOccurred,
            Qt::QueuedConnection);

    connect(m_videoDecodeThread, &VideoDecodeThread::videoInfoUpdated,
        this, [this](int width, int height, double frameRate) {
            LogInfo << QString("视频信息更新: %1x%2 @%3fps")
                           .arg(width).arg(height).arg(frameRate);
        }, Qt::QueuedConnection);

    // 音频播放器信号连接
    connect(m_audioPlayer, &AudioPlayer::errorOccurred,
            this, &RTSPSyncPull::errorOccurred,
            Qt::QueuedConnection);

    connect(m_audioPlayer, &AudioPlayer::audioClockUpdated,
        this, [this](qint64 pts) {
            QMutexLocker locker(&m_clockMutex);
            m_audioClock = pts;
            // 通知视频解码线程音频时钟更新
            if (m_videoDecodeThread) {
                m_videoDecodeThread->updateAudioClock(pts);
            }
        }, Qt::QueuedConnection);
}

void RTSPSyncPull::disconnectSignals()
{
    // 断开所有信号连接
    if (m_pullThread) {
        disconnect(m_pullThread, nullptr, this, nullptr);
        disconnect(m_pullThread, nullptr, m_audioDecodeThread, nullptr);
        disconnect(m_pullThread, nullptr, m_videoDecodeThread, nullptr);
    }

    if (m_audioDecodeThread) {
        disconnect(m_audioDecodeThread, nullptr, this, nullptr);
        disconnect(m_audioDecodeThread, nullptr, m_audioPlayer, nullptr);
    }

    if (m_videoDecodeThread) {
        disconnect(m_videoDecodeThread, nullptr, this, nullptr);
    }

    if (m_audioPlayer) {
        disconnect(m_audioPlayer, nullptr, this, nullptr);
        disconnect(m_audioPlayer, nullptr, m_videoDecodeThread, nullptr);
    }
}

qint64 RTSPSyncPull::getAudioClock()
{
    QMutexLocker locker(&m_clockMutex);
    return m_audioClock;
}

qint64 RTSPSyncPull::getVideoClock()
{
    QMutexLocker locker(&m_clockMutex);
    return m_videoClock;
}

bool RTSPSyncPull::isPlaying() const
{
    return m_audioPlayer && m_audioPlayer->isPlaying();
}
