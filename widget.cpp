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
    painter.setPen(QPen(Qt::black, 1));
    painter.drawEllipse(4, 4, 15, 15);
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
    , time_update_timer(nullptr)  // 初始化时间更新定时器
    , image(nullptr)
    , cam_raw_buf(nullptr)
    , cam_rgb_buf(nullptr)
    , is_recording(false)
    , is_replaying(false)
    , is_paused(false)
    , replay_frame_index(0)
    , overlay_text("华迪505实训室")  // 默认覆盖文字
    , current_replay_overlay_text("")           // 初始化回放文字
    , current_replay_time("")                   // 初始化回放时间
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

    // 初始化时间更新定时器
//    time_update_timer = new QTimer(this);
//    connect(time_update_timer, &QTimer::timeout, this, [this]() {
//        update(); // 触发重绘以更新时间显示
//    });
//    time_update_timer->start(1000); // 每秒更新一次

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
    if (time_update_timer) {
        time_update_timer->stop();
    }
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
    btn_live = new QPushButton("Open", this);

    // 新的文件选择按钮
    btn_prev_file = new QPushButton("◀", this);
    btn_next_file = new QPushButton("▶", this);
    lbl_file_info = new QLabel("No files", this);

    // 设置按钮尺寸
    btn_rec->setFixedSize(65, 30);
    btn_end->setFixedSize(65, 30);
    btn_replay->setFixedSize(65, 30);
    btn_pause->setFixedSize(65, 30);
    btn_stop_replay->setFixedSize(65, 30);
    btn_live->setFixedSize(65, 30);
    btn_prev_file->setFixedSize(65, 30);
    btn_next_file->setFixedSize(65, 30);

    progress_slider = new QSlider(Qt::Horizontal, this);
    progress_slider->setEnabled(false);
    progress_slider->setFixedHeight(20);

    lbl_progress = new QLabel("0/0", this);
    lbl_progress->setMinimumWidth(40);

    lbl_status = new QLabel("Ready", this);
    lbl_status->setFixedWidth(140);
    lbl_status->setFixedHeight(40);

    lbl_video_display = new QLabel(this);
    lbl_video_display->setFixedSize(320, 240);  // 固定视频显示区域
    lbl_video_display->setStyleSheet("border: 1px solid gray;");

    lbl_file_info->setFixedWidth(140);
    lbl_file_info->setStyleSheet("font-size: 10px;");

    // 创建状态指示器
    status_indicator = new StatusIndicator(this);
    status_indicator->setFixedSize(20, 20);

    // 创建主布局
    main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(2);
    main_layout->setContentsMargins(2, 2, 2, 2);

    // 创建上部区域：视频显示 + 右侧按钮
    QHBoxLayout *top_layout = new QHBoxLayout();
    top_layout->setSpacing(5);

    // 视频显示区域
    top_layout->addWidget(lbl_video_display);

    // 右侧按钮区域
    QVBoxLayout *right_buttons_layout = new QVBoxLayout();
    right_buttons_layout->setSpacing(3);

    // 状态指示器
    QHBoxLayout *status_layout = new QHBoxLayout();
    status_layout->addWidget(new QLabel("Status:", this));
    status_layout->addWidget(status_indicator);
    status_layout->addStretch();
    right_buttons_layout->addLayout(status_layout);

    // 主要控制按钮 - 第一行
    QHBoxLayout *main_controls_row1 = new QHBoxLayout();
    main_controls_row1->setSpacing(2);
    main_controls_row1->addWidget(btn_live);
    main_controls_row1->addWidget(btn_rec);
    right_buttons_layout->addLayout(main_controls_row1);

    // 主要控制按钮 - 第二行
    QHBoxLayout *main_controls_row2 = new QHBoxLayout();
    main_controls_row2->setSpacing(2);
    main_controls_row2->addWidget(btn_end);
    main_controls_row2->addWidget(btn_replay);
    right_buttons_layout->addLayout(main_controls_row2);

    // 回放控制按钮 - 第三行
    QHBoxLayout *replay_controls_row1 = new QHBoxLayout();
    replay_controls_row1->setSpacing(2);
    replay_controls_row1->addWidget(btn_pause);
    replay_controls_row1->addWidget(btn_stop_replay);
    right_buttons_layout->addLayout(replay_controls_row1);

    // 文件选择控件 - 第四行
    QHBoxLayout *file_controls = new QHBoxLayout();
    file_controls->setSpacing(2);
    file_controls->addWidget(btn_prev_file);
    file_controls->addWidget(btn_next_file);
    right_buttons_layout->addLayout(file_controls);

    // 文件信息显示
    right_buttons_layout->addWidget(lbl_file_info);

    // 将状态栏放到右侧
    lbl_status->setWordWrap(true);
    lbl_status->setStyleSheet("font-size: 12px; border: 1px solid gray; padding: 2px;");
    right_buttons_layout->addWidget(lbl_status);

    right_buttons_layout->addStretch();

    top_layout->addLayout(right_buttons_layout);
    main_layout->addLayout(top_layout);

    // 进度条区域
    QHBoxLayout *progress_layout = new QHBoxLayout();
    progress_layout->setSpacing(5);
    progress_layout->addWidget(progress_slider);
    progress_layout->addWidget(lbl_progress);
    main_layout->addLayout(progress_layout);

    // 状态栏已移到右侧，不再需要添加到主布局
    // main_layout->addWidget(lbl_status);  // 删除这行

    // 初始按钮状态
    btn_end->setEnabled(false);
    btn_pause->setEnabled(false);
    btn_stop_replay->setEnabled(false);
    btn_prev_file->setEnabled(false);
    btn_next_file->setEnabled(false);

    // 初始化文件选择
    current_file_index = 0;

    setLayout(main_layout);
    setWindowTitle("Video System");
    setFixedSize(480, 272);  // 固定窗口大小
}
//连接槽函数
void Widget::setupConnections()
{
    // 原有连接保持不变...
    // 视频设备错误信号
    connect(cam_vd, &VideoDevice::display_error, this, &Widget::slot_display_error);

    // 按钮信号
    connect(btn_rec, &QPushButton::clicked, this, &Widget::slot_start_recording);
    connect(btn_end, &QPushButton::clicked, this, &Widget::slot_stop_recording);
    connect(btn_replay, &QPushButton::clicked, this, &Widget::slot_start_replay);
    connect(btn_pause, &QPushButton::clicked, this, &Widget::slot_pause_replay);
    connect(btn_stop_replay, &QPushButton::clicked, this, &Widget::slot_stop_replay);
    connect(btn_live, &QPushButton::clicked, this, &Widget::slot_live_capture);

    // 新的文件选择按钮
    connect(btn_prev_file, &QPushButton::clicked, this, &Widget::slot_prev_file);
    connect(btn_next_file, &QPushButton::clicked, this, &Widget::slot_next_file);

    // 进度条信号
    connect(progress_slider, &QSlider::valueChanged, this, &Widget::slot_slider_changed);

    // 创建定时器
    live_timer = new QTimer(this);
    replay_timer = new QTimer(this);

    // 定时器信号连接保持不变...
    connect(live_timer, &QTimer::timeout, this, [this]() {
        if (!is_recording && !is_replaying) {
            cam_vd->get_frame((void**)&cam_raw_buf, (size_t*)&cam_raw_buf_len);
            yuyv422_to_rgb888(cam_raw_buf, cam_rgb_buf, width, height);
            *image = QImage(cam_rgb_buf, width, height, QImage::Format_RGB888);
            cam_vd->unget_frame();
            update();
        } else if (is_recording) {
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

void Widget::slot_prev_file()
{
    if (history_files.isEmpty()) return;

    current_file_index = (current_file_index - 1 + history_files.size()) % history_files.size();
    update_file_display();
}

void Widget::slot_next_file()
{
    if (history_files.isEmpty()) return;

    current_file_index = (current_file_index + 1) % history_files.size();
    update_file_display();
}

void Widget::update_file_display()
{
    if (history_files.isEmpty()) {
        lbl_file_info->setText("No files");
        btn_prev_file->setEnabled(false);
        btn_next_file->setEnabled(false);
        return;
    }

    btn_prev_file->setEnabled(true);
    btn_next_file->setEnabled(true);

    QString filename = history_files[current_file_index];
    lbl_file_info->setText(QString("%1/%2\n%3").arg(current_file_index + 1)
                                               .arg(history_files.size())
                                               .arg(filename));
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
    btn_live->setEnabled(false);

    update_status("Recording...");
    update_status_indicator();

    // 确保摄像头正在工作
    if (!live_timer->isActive()) {
        cam_vd->open_device();
        cam_vd->init_device(width,height);
        cam_vd->start_capturing();
        live_timer->start(1000 / DEFAULT_FPS);
    }
}

void Widget::slot_stop_recording()
{
    if (!is_recording) return;

    is_recording = false;

    // 保存录制文件
    QString filename = QString("%1/REC_%2%3")
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
    btn_live->setEnabled(true);

    update_status_indicator();
}

void Widget::slot_start_replay()
{
    if (is_recording) {
        update_status("Please stop recording first!");
        return;
    }

    if (history_files.isEmpty()) {
        update_status("No files to replay!");
        return;
    }

    QString selected_file = history_files[current_file_index];
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
    btn_live->setEnabled(false);
    is_camera_opened = false;//

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
    btn_live->setEnabled(true);
    is_camera_opened = false;
    //重新启动摄像头
    reset_replay_state();
    slot_live_capture();

}

void Widget::slot_replay_frame()
{
    if (!is_replaying || is_paused || replay_frame_index >= replay_frames.size()) {
        if (replay_frame_index >= replay_frames.size()) {
            reset_replay_state();
            slot_live_capture();
        }
        return;
    }

    const VideoFrame &frame = replay_frames[replay_frame_index];
    memcpy(cam_rgb_buf, frame.data.constData(), cam_rgb_buf_len);
    *image = QImage(cam_rgb_buf, width, height, QImage::Format_RGB888);

    // 设置当前帧的文字信息用于显示
    current_replay_overlay_text = frame.overlay_text;
    current_replay_time = frame.recorded_time;

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

        // 设置当前帧的文字信息用于显示
        current_replay_overlay_text = frame.overlay_text;
        current_replay_time = frame.recorded_time;

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
    if (is_recording || is_replaying) return;

    btn_live->setEnabled(false);
    QTimer::singleShot(500, this, [=]() { btn_live->setEnabled(true);});
    if (!is_camera_opened) {
        // 打开摄像头
        reset_replay_state();  // 清除回放状态
        cleanup_camera();

        if (initialize_camera()) {
            live_timer->start(1000 / DEFAULT_FPS);
            btn_rec->setEnabled(true);
            update_status("Live preview started");
            update_status_indicator();

            btn_live->setText("Close");
            is_camera_opened = true;
        } else {
            update_status("Failed to initialize camera");
            btn_rec->setEnabled(false);
        }
    } else {
        // 关闭摄像头
        live_timer->stop();
        cleanup_camera();
        btn_rec->setEnabled(false);
        update_status("Camera stopped");
        update_status_indicator();

        btn_live->setText("Open");
        is_camera_opened = false;
    }
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
    if (cam_vd->init_device(width,height) != 0) {
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
    cleanup_flag = !cleanup_flag;

    bool success = true;
    if(!cleanup_flag)
    {
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
    }
    return success;
}

void Widget::paintEvent(QPaintEvent *)
{
    if (image && lbl_video_display) {
        // 创建一个QPixmap副本用于绘制文字
        QPixmap pixmap = QPixmap::fromImage(*image);

        // 在图像上绘制文字和时间
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);

        // 设置字体
        QFont font("Arial", 12, QFont::Bold);
        painter.setFont(font);

        // 设置文字颜色
        painter.setPen(QPen(Qt::white, 2));

        // 根据当前状态确定显示的文字和时间
        QString displayText;
        QString timeText;

        if (is_replaying) {
            // 回放状态：显示录制时保存的文字和时间
            displayText = current_replay_overlay_text.isEmpty() ? overlay_text : current_replay_overlay_text;
            timeText = current_replay_time.isEmpty() ?
                      QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") : current_replay_time;
        } else {
            // 实时状态：显示当前文字和当前时间
            displayText = overlay_text;
            timeText = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        }

        QString combinedText = displayText + "\n" + timeText;

        QFontMetrics metrics(font);
        QRect textRect = metrics.boundingRect(QRect(0, 0, pixmap.width(), pixmap.height()),
                                            Qt::AlignLeft | Qt::AlignTop, combinedText);

        // 调整文字区域，添加一些边距
        textRect.adjust(-5, -2, 5, 2);
        textRect.moveTo(8, 8);

        // 绘制半透明黑色背景
        painter.fillRect(textRect, QColor(0, 0, 0, 128));

        // 绘制文字
        painter.setPen(Qt::white);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignTop, combinedText);

        painter.end();

        lbl_video_display->setPixmap(pixmap);
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

    // 创建VideoFrame对象，保存图像数据和文字信息
    VideoFrame frame;
    frame.data = QByteArray((char*)cam_rgb_buf, cam_rgb_buf_len);
    frame.timestamp = QDateTime::currentMSecsSinceEpoch();
    frame.overlay_text = overlay_text;  // 保存当前覆盖文字
    frame.recorded_time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");  // 保存录制时间

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

    // 写入头信息 - 增加版本号以支持新格式
    out << quint32(0x12345678); // 魔数
    out << quint32(2); // 版本号改为2，支持文字信息
    out << quint32(width);
    out << quint32(height);
    out << quint32(recorded_frames.size());

    // 写入帧数据，包括文字信息
    for (const VideoFrame &frame : recorded_frames) {
        out << frame.timestamp;
        out << frame.data;
        out << frame.overlay_text;      // 写入覆盖文字
        out << frame.recorded_time;     // 写入录制时间
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

    if (magic != 0x12345678) {
        file.close();
        return false;
    }

    // 检查版本兼容性
    if (version != 1 && version != 2) {
        file.close();
        update_status("Unsupported file version!");
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

        // 如果是版本2，读取文字信息
        if (version == 2) {
            in >> frame.overlay_text;
            in >> frame.recorded_time;
        } else {
            // 版本1的兼容性处理
            frame.overlay_text = "Version";  // 默认文字
            frame.recorded_time = QDateTime::fromMSecsSinceEpoch(frame.timestamp)
                                    .toString("yyyy-MM-dd hh:mm:ss");
        }

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
    history_files.clear();

    QDir dir(RECORD_DIR);
    QStringList filters;
    filters << QString("*%1").arg(RECORD_EXTENSION);
    dir.setNameFilters(filters);

    QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time);
    for (const QFileInfo &info : files) {
        history_files.append(info.fileName());
    }

    current_file_index = 0;
    update_file_display();
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

// 设置覆盖文字的方法：
void Widget::setOverlayText(const QString &text)
{
    overlay_text = text;
    update(); // 立即更新显示
}
