#include "widget.h"
#include "ui_widget.h"

const QString Widget::RECORD_DIR = "recordings";
const QString Widget::RECORD_EXTENSION = ".vrf"; // Video Recording File

// StatusIndicator 实现
StatusIndicator::StatusIndicator(QWidget *parent)
    : QWidget(parent), m_status(DeviceStatus::LIVE)
{
    setFixedSize(30, 30);
    setToolTip("Device Status");
}

void StatusIndicator::setStatus(DeviceStatus status)
{
    m_status = status;
    update();
}

void StatusIndicator::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 绘制圆形背景
    painter.setBrush(QBrush(getStatusColor()));
    painter.setPen(QPen(Qt::black, 2));
    painter.drawEllipse(5, 5, 20, 20);
}

QColor StatusIndicator::getStatusColor() const
{
    switch (m_status) {
        case DeviceStatus::LIVE:
            return QColor(255, 0, 0);      // 红色 - 实时显示
        case DeviceStatus::RECORDING:
            return QColor(0, 255, 0);      // 绿色 - 录制
        case DeviceStatus::REPLAYING:
            return QColor(0, 0, 255);      // 蓝色 - 视频文件回放
        default:
            return QColor(128, 128, 128);   // 灰色 - 默认
    }
}

// Widget 实现
Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , cam_vd(nullptr)
    , live_timer(nullptr)
    , replay_timer(nullptr)
    , image(nullptr)
    , cam_raw_buf(nullptr)
    , cam_rgb_buf(nullptr)
    , is_recording(false)
    , is_replaying(false)
    , is_paused(false)
    , replay_frame_index(0)
{
    ui->setupUi(this);

    // 初始化基本参数
    width = 320;
    height = 240;
    cam_raw_buf_len = width * height * 4;
    cam_rgb_buf_len = width * height * 3; // RGB888 format

    // 分配内存
    cam_raw_buf = (unsigned char*)malloc(cam_raw_buf_len);
    cam_rgb_buf = (unsigned char*)malloc(cam_rgb_buf_len);

    if (!cam_raw_buf || !cam_rgb_buf) {
        // 移除错误弹窗，改为在状态栏显示
        update_status("Memory allocation failed!");
        return;
    }

    // 创建图像对象
    image = new QImage(cam_rgb_buf, width, height, QImage::Format_RGB888);

    // 设置UI
    setupUI();

    // 初始化视频设备
    cam_name = "/dev/video0";
    cam_vd = new VideoDevice(cam_name);

    // 连接信号槽
    setupConnections();

    // 创建录制目录
    QDir dir;
    if (!dir.exists(RECORD_DIR)) {
        dir.mkpath(RECORD_DIR);
    }

    // 刷新历史文件列表
    refresh_history_files();

    // 延迟启动实时预览，确保UI完全加载
    QTimer::singleShot(200, this, [this]() {
        slot_live_capture();
    });

    update_status("Ready");
}

Widget::~Widget()
{
    cleanup_resources();
    delete ui;
}

void Widget::setupUI()
{
    // 创建控件
    btn_rec = new QPushButton("Rec", this);
    btn_end = new QPushButton("End", this);
    btn_replay = new QPushButton("Replay", this);
    btn_pause = new QPushButton("Pause", this);
    btn_stop_replay = new QPushButton("Stop", this);
    btn_live = new QPushButton("Live", this);
    btn_select_file = new QPushButton("Select File", this);

    progress_slider = new QSlider(Qt::Horizontal, this);
    progress_slider->setEnabled(false);

    lbl_progress = new QLabel("0 / 0", this);
    lbl_status = new QLabel("Ready", this);
    lbl_video_display = new QLabel(this);
    lbl_video_display->setMinimumSize(320, 240);
    lbl_video_display->setStyleSheet("border: 1px solid gray;");

    combo_history_files = new QComboBox(this);
    combo_history_files->setMinimumWidth(200);

    // 创建状态指示器
    status_indicator = new StatusIndicator(this);

    // 创建布局
    main_layout = new QVBoxLayout(this);

    // 视频显示区域
    main_layout->addWidget(lbl_video_display);

    // 控制按钮组
    control_group = new QGroupBox("Controls", this);
    control_layout = new QHBoxLayout(control_group);
    control_layout->addWidget(btn_live);
    control_layout->addWidget(btn_rec);
    control_layout->addWidget(btn_end);
    control_layout->addWidget(btn_replay);
    control_layout->addWidget(btn_pause);
    control_layout->addWidget(btn_stop_replay);
    control_layout->addStretch();

    // 状态指示器布局（替换FPS设置）
    QHBoxLayout *status_layout = new QHBoxLayout();
    status_layout->addWidget(new QLabel("Status:", this));
    status_layout->addWidget(status_indicator);
    status_layout->addStretch();
    control_layout->addLayout(status_layout);

    main_layout->addWidget(control_group);

    // 进度条组
    progress_group = new QGroupBox("Progress", this);
    progress_layout = new QHBoxLayout(progress_group);
    progress_layout->addWidget(progress_slider);
    progress_layout->addWidget(lbl_progress);
    main_layout->addWidget(progress_group);

    // 文件选择组
    file_group = new QGroupBox("History Files", this);
    file_layout = new QHBoxLayout(file_group);
    file_layout->addWidget(combo_history_files);
    file_layout->addWidget(btn_select_file);
    main_layout->addWidget(file_group);

    // 状态栏
    main_layout->addWidget(lbl_status);

    // 初始按钮状态
    btn_end->setEnabled(false);
    btn_pause->setEnabled(false);
    btn_stop_replay->setEnabled(false);

    setLayout(main_layout);
    setWindowTitle("Video Capture & Replay System");
    resize(480, 272);
}

void Widget::setupConnections()
{
    // 视频设备错误信号
    connect(cam_vd, &VideoDevice::display_error, this, &Widget::slot_display_error);

    // 按钮信号
    connect(btn_rec, &QPushButton::clicked, this, &Widget::slot_start_recording);
    connect(btn_end, &QPushButton::clicked, this, &Widget::slot_stop_recording);
    connect(btn_replay, &QPushButton::clicked, this, &Widget::slot_start_replay);
    connect(btn_pause, &QPushButton::clicked, this, &Widget::slot_pause_replay);
    connect(btn_stop_replay, &QPushButton::clicked, this, &Widget::slot_stop_replay);
    connect(btn_live, &QPushButton::clicked, this, &Widget::slot_live_capture);
    connect(btn_select_file, &QPushButton::clicked, this, &Widget::slot_select_history_file);

    // 进度条信号
    connect(progress_slider, &QSlider::valueChanged, this, &Widget::slot_slider_changed);

    // 创建定时器
    live_timer = new QTimer(this);
    replay_timer = new QTimer(this);

    // 定时器信号
    connect(live_timer, &QTimer::timeout, this, [this]() {
        if (!is_recording && !is_replaying) {
            // 仅实时预览
            cam_vd->get_frame((void**)&cam_raw_buf, (size_t*)&cam_raw_buf_len);
            yuyv422_to_rgb888(cam_raw_buf, cam_rgb_buf, width, height);
            *image = QImage(cam_rgb_buf, width, height, QImage::Format_RGB888);
            cam_vd->unget_frame();
            update();
        } else if (is_recording) {
            // 录制模式
            cam_vd->get_frame((void**)&cam_raw_buf, (size_t*)&cam_raw_buf_len);
            yuyv422_to_rgb888(cam_raw_buf, cam_rgb_buf, width, height);
            *image = QImage(cam_rgb_buf, width, height, QImage::Format_RGB888);
            save_frame_to_record();
            cam_vd->unget_frame();
            update();
        }
    });

    connect(replay_timer, &QTimer::timeout, this, &Widget::slot_replay_frame);
}

void Widget::slot_display_error(QString msg)
{
    // 在状态栏显示
    update_status(QString("Device Error: %1").arg(msg));
    qDebug() << msg;
}

void Widget::slot_start_recording()
{
    if (is_replaying) {
        update_status("Please stop replay first!");
        return;
    }

    if (recorded_frames.size() >= MAX_FRAMES_IN_MEMORY) {
        update_status("Maximum recording length reached!");
        return;
    }

    is_recording = true;
    recorded_frames.clear();
    record_start_time = QDateTime::currentDateTime();

    btn_rec->setEnabled(false);
    btn_end->setEnabled(true);
    btn_replay->setEnabled(false);

    update_status("Recording...");
    update_status_indicator();

    // 确保摄像头正在工作
    if (!live_timer->isActive()) {
        cam_vd->open_device();
        cam_vd->init_device();
        cam_vd->start_capturing();
        live_timer->start(1000 / DEFAULT_FPS);
    }
}

void Widget::slot_stop_recording()
{
    if (!is_recording) return;

    is_recording = false;

    // 保存录制文件
    QString filename = QString("%1/recording_%2%3")
        .arg(RECORD_DIR)
        .arg(record_start_time.toString("yyyyMMdd_hhmmss"))
        .arg(RECORD_EXTENSION);

    if (save_recording_to_file(filename)) {
        update_status(QString("Recording saved: %1 frames").arg(recorded_frames.size()));
        refresh_history_files();
    } else {
        update_status("Failed to save recording!");
    }

    btn_rec->setEnabled(true);
    btn_end->setEnabled(false);
    btn_replay->setEnabled(true);

    update_status_indicator();
}

void Widget::slot_start_replay()
{
    if (is_recording) {
        update_status("Please stop recording first!");
        return;
    }

    QString selected_file = combo_history_files->currentText();
    if (selected_file.isEmpty()) {
        update_status("Please select a file to replay!");
        return;
    }

    QString full_path = QString("%1/%2").arg(RECORD_DIR).arg(selected_file);
    if (!load_recording_from_file(full_path)) {
        update_status("Failed to load recording file!");
        return;
    }

    // 先停止并清理摄像头资源
    if (live_timer->isActive()) {
        live_timer->stop();
    }

    cleanup_camera();

    is_replaying = true;
    is_paused = false;
    replay_frame_index = 0;

    // 设置进度条
    progress_slider->setMaximum(replay_frames.size() - 1);
    progress_slider->setValue(0);
    progress_slider->setEnabled(true);

    // 更新按钮状态
    btn_replay->setEnabled(false);
    btn_pause->setEnabled(true);
    btn_stop_replay->setEnabled(true);
    btn_rec->setEnabled(false);

    // 启动回放定时器
    replay_timer->start(1000 / DEFAULT_FPS);

    update_status("Replaying...");
    update_status_indicator();
}

void Widget::slot_pause_replay()
{
    if (!is_replaying) return;

    is_paused = !is_paused;

    if (is_paused) {
        replay_timer->stop();
        btn_pause->setText("Resume");
        update_status("Paused");
    } else {
        replay_timer->start(1000 / DEFAULT_FPS);
        btn_pause->setText("Pause");
        update_status("Replaying...");
    }
}

void Widget::slot_stop_replay()
{
    //重新启动摄像头
    slot_live_capture();
    // 延迟重新启动摄像头，确保设备完全释放
    //QTimer::singleShot(100, this, &Widget::slot_live_capture);
    reset_replay_state();
}

void Widget::slot_replay_frame()
{
    if (!is_replaying || is_paused || replay_frame_index >= replay_frames.size()) {
        if (replay_frame_index >= replay_frames.size()) {
            slot_live_capture();
            // 回放结束，延迟重新启动摄像头
            //QTimer::singleShot(100, this, &Widget::slot_live_capture);
            reset_replay_state();
        }
        return;
    }

    const VideoFrame &frame = replay_frames[replay_frame_index];
    memcpy(cam_rgb_buf, frame.data.constData(), cam_rgb_buf_len);
    *image = QImage(cam_rgb_buf, width, height, QImage::Format_RGB888);

    // 更新进度条
    progress_slider->setValue(replay_frame_index);
    update_progress_display();

    replay_frame_index++;
    update();
}

void Widget::slot_slider_changed(int value)
{
    if (is_replaying && !replay_frames.isEmpty()) {
        replay_frame_index = qBound(0, value, replay_frames.size() - 1);

        // 立即显示该帧
        const VideoFrame &frame = replay_frames[replay_frame_index];
        memcpy(cam_rgb_buf, frame.data.constData(), cam_rgb_buf_len);
        *image = QImage(cam_rgb_buf, width, height, QImage::Format_RGB888);

        update_progress_display();
        update();
    }
}

void Widget::slot_select_history_file()
{
    QString filename = QFileDialog::getOpenFileName(this,
        "Select Video File",
        RECORD_DIR,
        QString("Video Files (*%1)").arg(RECORD_EXTENSION));

    if (!filename.isEmpty()) {
        QFileInfo info(filename);
        combo_history_files->setCurrentText(info.fileName());
    }
}

void Widget::slot_live_capture()
{
    if (is_recording) return;

    if(!is_replaying)
    {
        // 重置状态
        reset_replay_state();
        // 重新初始化摄像头
        cleanup_camera();
    }

    // 短暂延迟后重新打开
    QTimer::singleShot(100, this, [this]() {
        if (initialize_camera()) {
            live_timer->start(1000 / DEFAULT_FPS);

            btn_rec->setEnabled(true);
            btn_replay->setEnabled(true);

            update_status("Live preview");
            update_status_indicator();
        } else {
            update_status("Failed to initialize camera");
            btn_rec->setEnabled(false);
        }
    });
}

bool Widget::initialize_camera()
{
    if (!cam_vd) return false;

    // 尝试打开设备
    if (cam_vd->open_device() != 0) {
        qDebug() << "Failed to open camera device";
        return false;
    }

    // 初始化设备
    if (cam_vd->init_device() != 0) {
        qDebug() << "Failed to initialize camera device";
        cam_vd->close_device();
        return false;
    }

    // 开始捕获
    if (cam_vd->start_capturing() != 0) {
        qDebug() << "Failed to start camera capturing";
        cam_vd->uninit_device();
        cam_vd->close_device();
        return false;
    }

    return true;
}

bool Widget::cleanup_camera()
{
    if (!cam_vd) return true;

    bool success = true;

    // 按照正确的顺序关闭设备
    try {
        if (cam_vd->stop_capturing() != 0) {
            qDebug() << "Warning: Failed to stop capturing";
            success = false;
        }

        if (cam_vd->close_device() != 0) {
            qDebug() << "Warning: Failed to close device";
            success = false;
        }

        if (cam_vd->uninit_device() != 0) {
            qDebug() << "Warning: Failed to uninit device";
            success = false;
        }

    } catch (...) {
        qDebug() << "Exception during camera cleanup";
        success = false;
    }

    // 短暂延迟让系统释放资源
    QThread::msleep(50);

    return success;
}

void Widget::paintEvent(QPaintEvent *)
{
    if (image && lbl_video_display) {
        lbl_video_display->setPixmap(QPixmap::fromImage(*image));
    }
}

void Widget::yuyv422_to_rgb888(unsigned char *yuyvdata, unsigned char *rgbdata, int w, int h)
{
    int r1, g1, b1;
    int r2, g2, b2;

    for (int i = 0; i < w * h / 2; i++) {
        unsigned char Y0 = yuyvdata[i * 4];
        unsigned char U0 = yuyvdata[i * 4 + 1];
        unsigned char Y1 = yuyvdata[i * 4 + 2];
        unsigned char V1 = yuyvdata[i * 4 + 3];

        // 第一个像素
        r1 = Y0 + 1.4075 * (V1 - 128);
        g1 = Y0 - 0.3455 * (U0 - 128) - 0.7169 * (V1 - 128);
        b1 = Y0 + 1.779 * (U0 - 128);

        // 第二个像素
        r2 = Y1 + 1.4075 * (V1 - 128);
        g2 = Y1 - 0.3455 * (U0 - 128) - 0.7169 * (V1 - 128);
        b2 = Y1 + 1.779 * (U0 - 128);

        // 限制范围
        r1 = qBound(0, r1, 255);
        g1 = qBound(0, g1, 255);
        b1 = qBound(0, b1, 255);
        r2 = qBound(0, r2, 255);
        g2 = qBound(0, g2, 255);
        b2 = qBound(0, b2, 255);

        rgbdata[i * 6] = r1;
        rgbdata[i * 6 + 1] = g1;
        rgbdata[i * 6 + 2] = b1;
        rgbdata[i * 6 + 3] = r2;
        rgbdata[i * 6 + 4] = g2;
        rgbdata[i * 6 + 5] = b2;
    }
}

void Widget::save_frame_to_record()
{
    if (!is_recording || recorded_frames.size() >= MAX_FRAMES_IN_MEMORY) return;

    VideoFrame frame;
    frame.data = QByteArray((char*)cam_rgb_buf, cam_rgb_buf_len);
    frame.timestamp = QDateTime::currentMSecsSinceEpoch();

    recorded_frames.append(frame);

    // 更新状态
    update_status(QString("Recording... %1 frames").arg(recorded_frames.size()));
}

bool Widget::save_recording_to_file(const QString &filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_5_0);

    // 写入头信息
    out << quint32(0x12345678); // 魔数
    out << quint32(1); // 版本
    out << quint32(width);
    out << quint32(height);
    out << quint32(recorded_frames.size());

    // 写入帧数据
    for (const VideoFrame &frame : recorded_frames) {
        out << frame.timestamp;
        out << frame.data;
    }

    file.close();
    return true;
}

bool Widget::load_recording_from_file(const QString &filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_5_0);

    // 读取头信息
    quint32 magic, version, w, h, frame_count;
    in >> magic >> version >> w >> h >> frame_count;

    if (magic != 0x12345678 || version != 1) {
        file.close();
        return false;
    }

    if (w != width || h != height) {
        file.close();
        update_status("Video dimensions don't match!");
        return false;
    }

    replay_frames.clear();
    replay_frames.reserve(frame_count);

    // 读取帧数据
    for (quint32 i = 0; i < frame_count; i++) {
        VideoFrame frame;
        in >> frame.timestamp;
        in >> frame.data;
        replay_frames.append(frame);
    }

    file.close();
    current_replay_file = filename;
    return true;
}

void Widget::update_progress_display()
{
    if (is_replaying && !replay_frames.isEmpty()) {
        lbl_progress->setText(QString("%1 / %2").arg(replay_frame_index + 1).arg(replay_frames.size()));
    } else {
        lbl_progress->setText("0 / 0");
    }
}

void Widget::refresh_history_files()
{
    combo_history_files->clear();

    QDir dir(RECORD_DIR);
    QStringList filters;
    filters << QString("*%1").arg(RECORD_EXTENSION);
    dir.setNameFilters(filters);

    QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time);
    for (const QFileInfo &info : files) {
        combo_history_files->addItem(info.fileName());
    }
}

void Widget::update_status(const QString &status)
{
    lbl_status->setText(QString("Status: %1").arg(status));
}

void Widget::update_status_indicator()
{
    if (is_recording) {
        status_indicator->setStatus(DeviceStatus::RECORDING);
    } else if (is_replaying) {
        status_indicator->setStatus(DeviceStatus::REPLAYING);
    } else {
        status_indicator->setStatus(DeviceStatus::LIVE);
    }
}

void Widget::reset_replay_state()
{
    is_replaying = false;
    is_paused = false;
    replay_frame_index = 0;

    replay_timer->stop();

    progress_slider->setValue(0);
    progress_slider->setEnabled(false);

    btn_replay->setEnabled(true);
    btn_pause->setEnabled(false);
    btn_stop_replay->setEnabled(false);
    btn_rec->setEnabled(true);
    btn_pause->setText("Pause");

    update_progress_display();
    update_status_indicator();
}

void Widget::cleanup_resources()
{
    if (live_timer) {
        live_timer->stop();
    }
    if (replay_timer) {
        replay_timer->stop();
    }

    cleanup_camera();

    if (cam_vd) {
        delete cam_vd;
        cam_vd = nullptr;
    }

    if (cam_raw_buf) {
        free(cam_raw_buf);
        cam_raw_buf = nullptr;
    }
    if (cam_rgb_buf) {
        free(cam_rgb_buf);
        cam_rgb_buf = nullptr;
    }
    if (image) {
        delete image;
        image = nullptr;
    }
}
