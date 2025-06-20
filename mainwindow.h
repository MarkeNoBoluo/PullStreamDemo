#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QHash>
#include "DataStruct.h"

class RTSPSyncPull;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected slots:
    void flushPlayStateSlot(int state, const QString &objName);
private slots:
    void on_btn_Open_clicked();

private:
    Ui::MainWindow *ui;
    bool m_isRunning = false;
    qreal m_previousVolume = 1.0; // 用于静音前保存音量

    RTSPSyncPull *m_pull = nullptr;
};
#endif // MAINWINDOW_H
