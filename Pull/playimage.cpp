#include "playimage.h"
#include <QDebug>
#include <QPainter>
#include <QtMath>
#include "Logger.h"

PlayImage::PlayImage(QWidget *parent)
    : QWidget(parent),
    m_state(null)
{
    // 适用调色板设置背景色
    QPalette palette(this->palette());
    palette.setColor(QPalette::Window, Qt::transparent);   //设置背景透明
    this->setPalette(palette);
    this->setAutoFillBackground(true);
    // 初始化相位差（120度间隔形成波浪效果）
    m_phase[0] = 0;
    m_phase[1] = 2 * PI / 3;
    m_phase[2] = 4 * PI / 3;

    // 创建统一动画定时器
    m_animTimer = new QTimer(this);
    connect(m_animTimer, &QTimer::timeout, this, [=]{
        m_animTime += 50;  // 每50ms增加时间基准
        update();
    });
}

/**
 * @brief        FFMpegThread传入Qimage图片显示
 * @param image
 */
void PlayImage::updateImage(const QImage& image)
{
    if(m_state == null)    return;

    QSharedPointer<QImage> newImage = QSharedPointer<QImage>(new QImage(image.copy()));
    if(!newImage.isNull()){
        QSharedPointer<QPixmap> newPixmap = QSharedPointer<QPixmap>(new QPixmap(QPixmap::fromImage(*newImage.data())));
        if(!newPixmap.isNull()){
            updatePixmap(*newPixmap.data());
        }

    }
}

/**
 * @brief        转为QPixmap图片显示
 * @param pixmap
 */
void PlayImage::updatePixmap(const QPixmap &pixmap)
{
    {
        QMutexLocker locker(&m_mutex);
        m_pixmap = pixmap;
    }
    update();
}

void PlayImage::onPlayState(PushState status,const QString &name)
{
    if (name.isEmpty()) return;
//    QString currentThreadName = this->property("thread").value<FFmpegThread*>()->objectName();
//    if (name != currentThreadName) return;

    switch (status) {
    case PushState::end:
        m_state = end;
        flushPlayState(0, name);
        StopTimer();
        update();  // 强制更新
        break;
    case PushState::play:
        m_state = play;
        flushPlayState(2, name);
        StopTimer();
        update();  // 强制更新
        break;
    case PushState::decode:
        m_state = decode;
        flushPlayState(1, name);
        InitTimer();
        update();  // 强制更新
        break;
    case PushState::error:
        m_state = error;
        flushPlayState(-1, name);
        StopTimer();
        update();  // 强制更新
        break;
    default:
        break;
    }
}

void PlayImage::DrawNoPlayStatus()
{
    QPainter painter(this); // 创建与绘图设备关联的 QPainter 对象
    // 设置画笔属性
    QPen pen;
    pen.setWidth(2); // 线宽
    pen.setColor(Qt::black); // 画线颜色
    pen.setStyle(Qt::DashLine); // 线的样式
    pen.setCapStyle(Qt::FlatCap); // 线的端点样式
    pen.setJoinStyle(Qt::BevelJoin); // 线的连接点样式
    // 设置画刷属性
    QBrush brush;
    brush.setColor(Qt::transparent); // 画刷颜色
    brush.setStyle(Qt::SolidPattern); // 画刷填充样式
    // 设置字体属性
    QFont font;
    font.setFamily("Microsoft YaHei UI");
    font.setBold(true);
    font.setPointSize(18);
    painter.setFont(font);
    painter.setPen(pen);
    painter.setBrush(brush);
    painter.setRenderHint(QPainter::Antialiasing,true); // 设置反锯齿
    painter.setRenderHint(QPainter::TextAntialiasing,true); // 设置文本反锯齿
    // 先画底色为黑色
    painter.fillRect(this->rect(), Qt::transparent);
    // 获取窗口的中心点
    QPoint center = rect().center();
    QRect dropHere(center-QPoint(50,50),QSize(100,100));
    pen.setColor(QColor(74, 139, 185));
    painter.setPen(pen);
    painter.drawRoundedRect(dropHere,10,10);
    pen.setStyle(Qt::SolidLine);
    painter.setPen(pen);
    QVector<QLine>lines;
    lines.append(QLine(center-QPoint(0,25),center+QPoint(0,25)));
    lines.append(QLine(center-QPoint(25,0),center+QPoint(25,0)));
    painter.drawLines(lines);
    font.setPointSize(16);
    painter.setFont(font);
    QString text = tr("拖拽到此处播放");
    painter.drawText(center - QPoint(75, -100),text);
}

void PlayImage::DrawPlayStatus()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing,true); // 设置反锯齿
    painter.setRenderHint(QPainter::TextAntialiasing,true); // 设置文本反锯齿
    QPixmap pixmap;
    {
        QMutexLocker locker(&m_mutex);
        pixmap = m_pixmap.scaled(this->size(), Qt::KeepAspectRatio);
    }
    int x = (this->width() - pixmap.width()) / 2;
    int y = (this->height() - pixmap.height()) / 2;
    painter.drawPixmap(x, y, pixmap);
}

void PlayImage::InitTimer()
{

    m_animTimer->start(50);
}

void PlayImage::StopTimer()
{
    if(m_animTimer && m_animTimer->isActive()){
        m_animTimer->stop();
        m_animTime = 0;
    }
}

void PlayImage::DrawDecodeStatus()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRect parentRect = rect();
    parentRect.adjust(2,2,-2,-2);
    painter.fillRect(parentRect, Qt::transparent);

    // 设置动画参数
    const int baseRadius = 10;
    const int amplitude = 5;   // 振幅
    const double speed = 0.5;  // 动画速度因子

    QPen pen(QColor(74, 139, 185, 255), 2);
    painter.setPen(pen);

    QBrush brush(QColor(74, 139, 185));
    painter.setBrush(brush);

    QFont font("Microsoft YaHei UI", 25, QFont::Bold);
    painter.setFont(font);
    QPoint center = rect().center();
    QFontMetrics fm(font);
    // 绘制文字
    QRect textRect = fm.boundingRect(tr("加载视频中"));
    painter.drawText(center - QPoint(textRect.width()/2+70, -6), tr("加载视频中"));


    // 绘制三个带相位差的圆
    for(int i = 0; i < 3; ++i){
        // 使用正弦函数计算平滑半径（范围：10-15-20-15-10...）
        double radians = speed * m_animTime + m_phase[i];
        int radius = baseRadius + amplitude * (1 + sin(radians));
        painter.drawEllipse(center + QPoint(50 + 45*i, 0), radius, radius);
    }

}

void PlayImage::DrawErrorStatus()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing,true);
    QRect parentRect = rect();
    parentRect.adjust(2,2,-2,-2);
    painter.fillRect(rect(), Qt::transparent);

    QPen pen(QColor(74, 139, 185, 255), 2);
    painter.setPen(pen);

    QBrush brush(QColor(74, 139, 185));
    painter.setBrush(brush);

    QFont font("Microsoft YaHei UI", 25, QFont::Bold);
    painter.setFont(font);

    QPoint center = rect().center();
    QPixmap pix(":/Resources/Icon/playError.png");
    painter.drawPixmap(center - QPoint(100, 150), pix);
    QString str = tr("加载视频失败!");
    painter.drawText(center - QPoint(100, 150), str);
}

/**
 * @brief        使用Qpainter显示图片
 * @param event
 */
void PlayImage::paintEvent(QPaintEvent *event)
{
    if (m_state == play && !m_pixmap.isNull()) {
        DrawPlayStatus();
    } else if (m_state == null || m_state == end || m_state == error) {
        DrawNoPlayStatus();
    } else if (m_state == decode) {
        DrawDecodeStatus();
    } else {
        QWidget::paintEvent(event);
    }
}

void PlayImage::mouseDoubleClickEvent(QMouseEvent *event)
{
//    m_isEnlarge = !m_isEnlarge;
//    emit enlargePlayWindow(this->objectName(), m_isEnlarge);
//    m_fullscreenBtn->setIcon(QIcon(m_isEnlarge ?
//                                       ":/Resources/Icon/Reduce.png" : ":/Resources/Icon/Enlarge.png"));
    QWidget::mouseDoubleClickEvent(event);
}

void PlayImage::resizeEvent(QResizeEvent *event)
{
    emit updatePlayWindowSize(this->geometry().size());//发出窗口大小改变信号
    updateControlBarPosition();
    QWidget::resizeEvent(event);
}


bool PlayImage::isEnlarge() const
{
    return m_isEnlarge;
}

void PlayImage::setupControlBar()
{
    // Create semi-transparent control bar
    m_controlBar = new QWidget(this);
    m_controlBar->raise();
    m_controlBar->setObjectName("controlBar");
    m_controlBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_controlBar->setFixedHeight(40);
    m_controlBar->setStyleSheet(R"(
        QWidget#controlBar {
            background-color: rgba(0, 0, 0, 180);
        }
        QPushButton {
            background: transparent;
            border: none;
            color: white;
            padding: 4px;
            min-width: 32px;
            min-height: 32px;
        }
        QPushButton:hover {
            background: rgba(255, 255, 255, 30);
        }
        QLabel {
            color: white;
            padding: 0 8px;
            font: 700 10pt "Microsoft YaHei UI";
        }
    )");

    // Create horizontal layout
    QHBoxLayout *layout = new QHBoxLayout(m_controlBar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // URL display label
    m_urlLabel = new QLabel("没有正在播放的视频源", m_controlBar);
    m_urlLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Add spring to push buttons to the right
    layout->addWidget(m_urlLabel);
    layout->addStretch(1);

    // Control buttons
    m_fullscreenBtn = new QPushButton(m_controlBar);
    m_fullscreenBtn->setIcon(QIcon(":/Resources/Icon/Enlarge.png"));
    m_fullscreenBtn->setToolTip(tr("全屏"));

    m_closeBtn = new QPushButton(m_controlBar);
    m_closeBtn->setIcon(QIcon(":/Resources/Icon/close.png"));
    m_closeBtn->setToolTip(tr("关闭"));

    layout->addWidget(m_fullscreenBtn);
    layout->addWidget(m_closeBtn);

    // Initially hide control bar
    m_controlBar->hide();

    // Create timer for auto-hide
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(5000);

    // Connect signals
    connect(m_hideTimer, &QTimer::timeout, this, &PlayImage::hideControlBar);
    connect(m_closeBtn, &QPushButton::clicked, this, [this](){
        if(m_state == play){//视频正在播放
            emit closed();
            resetLabel();
            m_state = end;
            update();
        }

    });
    connect(m_fullscreenBtn, &QPushButton::clicked, this, [this]() {
        m_isEnlarge = !m_isEnlarge;
        emit enlargePlayWindow(this->objectName(), m_isEnlarge);
        m_fullscreenBtn->setIcon(QIcon(m_isEnlarge ? ":/Resources/Icon/Reduce.png" : ":/Resources/Icon/Enlarge.png"));
    });

    // Install event filters
    m_controlBar->installEventFilter(this);
    this->installEventFilter(this);
}

void PlayImage::updateControlBarPosition()
{
    if (m_controlBar) {
        const int barHeight = m_controlBar->sizeHint().height();
        m_controlBar->setGeometry(0, 0, width(), barHeight);
    }
}

void PlayImage::showControlBar()
{
    if (m_controlBar) {
        m_controlBar->show();
        m_hideTimer->start();
    }
}

void PlayImage::hideControlBar()
{
    if (m_controlBar) {
        m_controlBar->hide();
    }
}

void PlayImage::enterEvent(QEvent *event)
{
    showControlBar();
    QWidget::enterEvent(event);
}

void PlayImage::leaveEvent(QEvent *event)
{
    hideControlBar();
    QWidget::leaveEvent(event);
}

bool PlayImage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseMove) {
        showControlBar();
    }
    return QWidget::eventFilter(watched, event);
}


void PlayImage::showEvent(QShowEvent *event)
{
    updateControlBarPosition();
    QWidget::showEvent(event);
}

void PlayImage::setUrl(const QString &url)
{
    m_currentUrl = url;
    m_urlLabel->setText(url);
}

void PlayImage::resetLabel()
{
    m_urlLabel->setText("没有正在播放的视频源");
}

void PlayImage::setStatus(const int state)
{
    switch (state) {
    case -1://停止加载动画
        m_state = null;
        StopTimer();
        update();  // 强制更新
        break;
    case 1://开始加载动画
        m_state = decode;
        InitTimer();
        update();  // 强制更新
        break;
    default:
        break;
    }
}

