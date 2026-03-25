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
	void inokeUpdateInfoEdit(const QString &info);
	void seekRelative(int milliseconds);
	void setPlayBackSpeed(double speed);
	bool initAudioOutput();
	void writeAudioToFile();

	//FFmpeg相关成员
	AVFormatContext* formatContext;
	AVCodecContext* codecContext;
	AVFrame* frame;
	AVFrame* rgbFrame;
	struct SwsContext* swsContext;
	int videoStreamIndex;
	QString curVideoFile;
	QString totalVideoTime;
	
	//播放状态
	bool isPlaying;
	QTimer* timer;
	uchar* alignedData;
	int alignedBytesPerLine;

	//快进/快退相关
	int fastForwardInterval = 5000;
	double playbackSpeed = 1.0;
	QMap<QString, double>playSpeedMap;

	//音频 - 简化版
	int audioStreamIndex = -1;
	AVCodecContext* audioCodecContext = nullptr;
	AVFrame* audioFrame = nullptr;
	struct SwrContext* swrContext = nullptr;
	
	// 当前播放时间(PTS)
	int64_t currentPts;
	int64_t lastPts;

	//帧率控制
	int frameIntervalMs;
	double fps;

	//编码缓冲区
	AVPacket packet;
	bool hasMoreFrames;
	double duration;

	Ui::ArcticLightClass ui;
};