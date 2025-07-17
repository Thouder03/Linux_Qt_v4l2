#ifndef RECORDINGTHREAD_H
#define RECORDINGTHREAD_H
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QVector>

// 常量
static const QString RECORD_DIR = "recordings";
static const QString RECORD_EXTENSION = ".vrf";
static const int DEFAULT_FPS = 30;
static const int MAX_FRAMES_IN_MEMORY = 18000; // 30fps * 10分钟

struct VideoFrame {
    QByteArray data;
    qint64 timestamp;

    // 默认构造函数
    VideoFrame() : timestamp(0) {}

    // 带参数的构造函数
    VideoFrame(const QByteArray &frameData, qint64 ts)
        : data(frameData), timestamp(ts) {}
};


// 添加录制线程类
class RecordingThread : public QThread
{
    Q_OBJECT

public:
    RecordingThread(QObject *parent = nullptr);
    ~RecordingThread();

    void startRecording();
    void stopRecording();
    void addFrame(const QByteArray &frameData, qint64 timestamp);

protected:
    void run() override;

private:
    QVector<VideoFrame> m_frameQueue;
    QMutex m_mutex;
    QWaitCondition m_condition;
    bool m_recording;
    bool m_quit;

signals:
    void frameRecorded(int frameCount);
    void recordingStopped(const QVector<VideoFrame> &frames);
};

#endif // RECORDINGTHREAD_H
