#include "recordingthread.h"

// RecordingThread 实现
RecordingThread::RecordingThread(QObject *parent)
    : QThread(parent), m_recording(false), m_quit(false)
{
}

RecordingThread::~RecordingThread()
{
    m_quit = true;
    m_condition.wakeAll();
    wait();
}

void RecordingThread::startRecording()
{
    QMutexLocker locker(&m_mutex);
    m_recording = true;
    m_frameQueue.clear();
    m_frameQueue.reserve(MAX_FRAMES_IN_MEMORY);

    if (!isRunning()) {
        start();
    }
}

void RecordingThread::stopRecording()
{
    QMutexLocker locker(&m_mutex);
    m_recording = false;

    // 发送录制完成信号
    emit recordingStopped(m_frameQueue);
}

void RecordingThread::addFrame(const QByteArray &frameData, qint64 timestamp)
{
    QMutexLocker locker(&m_mutex);

    if (m_recording && m_frameQueue.size() < MAX_FRAMES_IN_MEMORY) {
        VideoFrame frame;
        frame.data = frameData;  // QByteArray 会自动深拷贝
        frame.timestamp = timestamp;
        m_frameQueue.append(frame);

        // 每30帧发送一次状态更新
        if (m_frameQueue.size() % 30 == 0) {
            emit frameRecorded(m_frameQueue.size());
        }
    }
}

void RecordingThread::run()
{
    while (!m_quit) {
        QMutexLocker locker(&m_mutex);
        m_condition.wait(&m_mutex, 100); // 100ms超时
    }
}
