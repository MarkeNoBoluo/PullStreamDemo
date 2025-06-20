#ifndef DATASTRUCT_H
#define DATASTRUCT_H

#include <QObject>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
#include <libavutil/time.h>
#include <libavutil/frame.h>
}

enum class PushState {
    none,
    decode,
    play,
    pause,
    error,
    end
};
Q_DECLARE_METATYPE(PushState); // 在类的声明之后添加这个宏

#endif // DATASTRUCT_H
