#pragma once

extern "C" {
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h> 
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h> 
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
#include <QAudioOutput>


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
	void dragEnterEvent(QDragEnterEvent* event) override;//处理拖放视频文件
	void dropEvent(QDropEvent* event) override;//处理拖放事件
	void keyPressEvent(QKeyEvent* event) override;//处理键盘事件
	

private:
	void closeVideoFile();
	//void initializeFFmpeg() {}
	void openVideoFile(const QString& filePath);
	void decodeNextFrame();
	void updateUI();
	void inokeUpdateInfoEdit(const QString &info);
	void seekRelative(int milliseconds);//相对寻址，正数快进，负数快退
	void setPlayBackSpeed(double speed);
	void initAudioOutput();

	//FFmpeg相关成员
	AVFormatContext* formatContext;
	AVCodecContext* codecContext;
	AVFrame* frame;
	AVFrame* rgbFrame;
	struct SwsContext* swsContext;
	int videoStreamIndex;
	QString curVideoFile;
	QString totalVideoTime;//当前视频文件时长
	//AVRational streamTimeBase = { 0,1 };//视频流的时间基
	//播放状态
	bool isPlaying;
	QTimer* timer;
	uchar* alignedData;
	int alignedBytesPerLine;

	//快进/快退相关
	int fastForwardInterval = 5000;//快进/快退的时间间隔（毫秒）
	double playbackSpeed = 1.0;//当前播放速度（倍速）
	QMap<QString, double>playSpeedMap;

	//音频
	int audioStreamIndex = -1;//音频流索引
	AVCodecContext* audioCodecContext = nullptr;//音频解码器上下文
	AVFrame* audioFrame = nullptr;//音频帧
	uint8_t audioBuffer = nullptr;
	int audioBufferSize = 0;//音频缓冲区大小
	QAudioOutput* audioOutput = nullptr;//Qt音频输出对象
	QIODevice* audioDevice = nullptr;//Qt音频输入输出设备

	//当前播放时间(PTS)
	int64_t currentPts;
	int64_t lastPts;

	//帧率控制
	int frameIntervalMs;//每帧的时间间隔（毫秒）
	double fps;

	//编码缓冲区
	AVPacket packet;
	bool hasMoreFrames;//是否还有更多帧可供解码
	// 当前帧信息
	double duration;

	Ui::ArcticLightClass ui;
};

