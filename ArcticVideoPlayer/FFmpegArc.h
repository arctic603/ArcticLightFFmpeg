#pragma once

extern "C" {
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h> 
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h> 
#include <libavutil/opt.h>
}

#include <QtWidgets/QMainWindow>
#include <QTimer>
#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <QFile>
#include <QDataStream>

#include "ui_FFmpegArc.h"

class ArcticLight : public QMainWindow
{
    Q_OBJECT

public:
	ArcticLight(QWidget *parent = nullptr);
    ~ArcticLight();
	
private slots:
    void onPlayButtonClicked();
	void onPauseButtonClicked();
	void onResetButtonClicked();
	void ForwardButtonClicked();
	void BackwardButtonClicked();
	void onTimerTimeout();
	void openFileDialog();
	void onhorizontalSliderValueChanged(int value);
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	
private:
	void closeVideoFile();
	void openVideoFile(const QString& filePath);
	void decodeNextFrame();
	void updateUI();
	void invokeUpdateInfoEdit(const QString &info);
	void seekRelative(int milliseconds);
	void setPlayBackSpeed(double speed);
	
	// FFmpeg 相关成员
	AVFormatContext* formatContext = nullptr;
	AVCodecContext* codecContext = nullptr;
	AVFrame* frame = nullptr;
	AVFrame* rgbFrame = nullptr;
	struct SwsContext* swsContext = nullptr;
	int videoStreamIndex = -1;
	QString curVideoFile;
	QString totalVideoTime;
	
	// 播放状态
	bool isPlaying = false;
	QTimer* timer = nullptr;
	uchar* alignedData = nullptr;
	int alignedBytesPerLine = 0;
	int currentBufferSize = 0;

	// 快进/快退相关
	int fastForwardInterval = 5000;
	double playbackSpeed = 1.0;
	QMap<QString, double> playSpeedMap;

	// 音频
	// 音频流索引
	int audioStreamIndex = -1;
	// 音频解码器上下文
	AVCodecContext* audioCodecContext = nullptr;
	// 音频帧
	AVFrame* audioFrame = nullptr;
	// 重采样上下文（把音频转成统一格式）
	struct SwrContext* swrContext = nullptr;
	// Qt 音频输出设备
	QAudioSink* audioOutput = nullptr;
	QIODevice* audioIODevice = nullptr;
	
	// 播放时间跟踪
	int64_t playStartTime = 0;
	int64_t playStartPts = AV_NOPTS_VALUE;

	// 当前播放时间 (PTS)
	int64_t currentPts = AV_NOPTS_VALUE;
	int64_t lastPts = AV_NOPTS_VALUE;

	// 帧率控制
	int frameIntervalMs = 16;
	double fps = 0.0;

	// 编码缓冲区
	AVPacket packet;
	bool hasMoreFrames = true;
	double duration = 0.0;

	Ui::ArcticLightClass ui;
};
