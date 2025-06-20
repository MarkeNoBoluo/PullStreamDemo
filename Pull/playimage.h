#ifndef PLAYIMAGE_H
#define PLAYIMAGE_H

#include <QWidget>
#include <qmutex.h>
#include <QSharedPointer>
#include <QLabel>
#include <QEvent>
#include <QPushButton>
#include <QHBoxLayout>
#include "DataStruct.h"
#include <QTimer>

class PlayImage : public QWidget
{
    Q_OBJECT
public:
    enum State{
        null,
        decode,
        play,
        end,
        error
    };
    explicit PlayImage(QWidget *parent = nullptr);

    bool isEnlarge() const;
    void setUrl(const QString &url);
    void setupControlBar();//设置浮动控制栏，支持放大和关闭
    void resetLabel();//重置标题
    void setStatus(const int state);//设置加载动画
public slots:
    void updateImage(const QImage& image);
    void updatePixmap(const QPixmap& pixmap);
    void onPlayState(PushState status,const QString &name);

private:
    void DrawNoPlayStatus();
    void DrawPlayStatus();
    void InitTimer();
    void StopTimer();
    void DrawDecodeStatus();
    void DrawErrorStatus();
    void updateControlBarPosition();
    void showControlBar();
    void hideControlBar();
signals:
    void flushPlayState(int state,QString objName);
    void updatePlayWindowSize(const QSize &size);
    void enlargePlayWindow(const QString &objectName,const bool &isEnlarge);
    void closed();
protected:
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QPixmap m_pixmap;
    QImage m_image;
    QMutex m_mutex;
    State m_state = null;
    bool m_isFirst = true;
    bool m_isEnlarge = false;//界面是否扩大

    QWidget *m_controlBar = nullptr;
    QLabel *m_urlLabel = nullptr;
    QPushButton *m_fullscreenBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QTimer *m_hideTimer = nullptr;
    QString m_currentUrl;

    // 移除原有定时器数组和半径数组
    QTimer* m_animTimer = nullptr;   // 统一动画定时器
    int m_animTime = 0;    // 动画时间基准
    double m_phase[3];     // 每个圆的相位差
    const double PI = 3.14159265358979323846;
};

#endif // PLAYIMAGE_H
