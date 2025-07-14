#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QString>
#include <QTimer>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QFileDialog>
#include <QDir>
#include <QProgressBar>
#include <QSpinBox>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>
#include <QDesktopWidget>
#include <QScreen>
#include <QDateTime>
#include <QVector>
#include <QFileInfo>
#include <QTextStream>
#include <QDataStream>
#include <QFile>
#include <QPixmap>
#include <QImage>
#include <QPaintEvent>
#include <QDebug>
#include <QThread>
#include "videodevice.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

struct VideoFrame {
    QByteArray data;
    qint64 timestamp;
};

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

    void paintEvent(QPaintEvent *);

private slots:
    void slot_display_error(QString);
    void slot_start_recording();
    void slot_stop_recording();
    void slot_start_replay();
    void slot_stop_replay();
    void slot_pause_replay();
    void slot_replay_frame();
    void slot_slider_changed(int value);
    void slot_select_history_file();
    void slot_live_capture();

private:
    Ui::Widget *ui;

    // 视频设备相关
    QString cam_name;
    VideoDevice *cam_vd;
    QTimer *live_timer;
    QTimer *replay_timer;

    // 图像数据
    int width, height;
    QImage *image;
    unsigned char *cam_raw_buf;
    unsigned int cam_raw_buf_len;
    unsigned char *cam_rgb_buf;
    unsigned int cam_rgb_buf_len;

    // 录制相关
    bool is_recording;
    QVector<VideoFrame> recorded_frames;
    QString current_record_file;
    QDateTime record_start_time;

    // 回放相关
    bool is_replaying;
    bool is_paused;
    int replay_frame_index;
    QVector<VideoFrame> replay_frames;
    QString current_replay_file;

    // UI控件
    QPushButton *btn_rec;
    QPushButton *btn_end;
    QPushButton *btn_replay;
    QPushButton *btn_pause;
    QPushButton *btn_stop_replay;
    QPushButton *btn_live;
    QPushButton *btn_select_file;
    QSlider *progress_slider;
    QLabel *lbl_progress;
    QLabel *lbl_status;
    QLabel *lbl_video_display;
    QComboBox *combo_history_files;
    QLabel *lbl_fps;
    QSpinBox *spin_fps;

    // 布局
    QVBoxLayout *main_layout;
    QHBoxLayout *control_layout;
    QHBoxLayout *progress_layout;
    QHBoxLayout *file_layout;
    QHBoxLayout *fps_layout;
    QGroupBox *control_group;
    QGroupBox *progress_group;
    QGroupBox *file_group;

    // 方法
    void setupUI();
    void setupConnections();
    void yuyv422_to_rgb888(unsigned char *yuyvdata, unsigned char *rgbdata, int w, int h);
    void save_frame_to_record();
    bool save_recording_to_file(const QString &filename);
    bool load_recording_from_file(const QString &filename);
    void update_progress_display();
    void refresh_history_files();
    void update_status(const QString &status);
    void reset_replay_state();
    void cleanup_resources();
    bool initialize_camera();
    bool cleanup_camera();

    // 常量
    static const QString RECORD_DIR;
    static const QString RECORD_EXTENSION;
    static const int DEFAULT_FPS = 30;
    static const int MAX_FRAMES_IN_MEMORY = 18000; // 30fps * 10分钟


    Qt::GlobalColor currentColor;
    bool isVisible;  // 控制是否显示颜色
    bool isActive;   // 标记是否已激活（首次点击按钮后启动定时器）
};

#endif // WIDGET_H
