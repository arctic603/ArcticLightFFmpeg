#include "FFmpegArc.h"
#include <QFileDialog>

ArcticLight::ArcticLight(QWidget *parent)
	: QMainWindow(parent), formatContext(nullptr), codecContext(nullptr), frame(nullptr), rgbFrame(nullptr), swsContext(nullptr), videoStreamIndex(-1), isPlaying(false), timer(new QTimer(this)),
	currentPts(AV_NOPTS_VALUE), lastPts(AV_NOPTS_VALUE), fps(0), frameIntervalMs(0), hasMoreFrames(true)
{
    ui.setupUi(this);
	ui.videoLabel->setAcceptDrops(true);

	//连接信号槽
	connect(ui.playButton, &QPushButton::clicked, this, &ArcticLight::onPlayButtonClicked);
	connect(ui.pauseButton, &QPushButton::clicked, this, &ArcticLight::onPauseButtonClicked);
	connect(ui.ResetButton, &QPushButton::clicked, this, &ArcticLight::onResetButtonClicked);
	connect(timer, &QTimer::timeout, this, &ArcticLight::onTimerTimeout);
	connect(ui.actionOpen_VideoFile, &QAction::triggered, this, &ArcticLight::openFileDialog);
	connect(ui.horizontalSlider, &QSlider::valueChanged, this, &ArcticLight::onhorizontalSliderValueChanged);

	connect(ui.ForwardButton, &QPushButton::clicked, this, &ArcticLight::ForwardButtonClicked);
	connect(ui.BackwardButton, &QPushButton::clicked, this, &ArcticLight::BackwardButtonClicked);
	connect(ui.playSpeedComboBox, &QComboBox::currentTextChanged, this, [this](const QString& text) {
		if (playSpeedMap.contains(text)) {
			setPlayBackSpeed(playSpeedMap[text]);
		}
		});

	playSpeedMap["0.5x"] = 0.5;
	playSpeedMap["1x"] = 1.0;
	playSpeedMap["1.5x"] = 1.5;
	playSpeedMap["2x"] = 2.0;

}

ArcticLight::~ArcticLight()
{
	closeVideoFile();
}

void ArcticLight::closeVideoFile() {
	if (timer) {
		timer->stop();
		
	}

	if (swsContext) {
		sws_freeContext(swsContext);
		swsContext = nullptr;
	}

	if (rgbFrame) {
		if (rgbFrame->data[0]) {
			av_free(rgbFrame->data[0]); // 释放 buffer
			rgbFrame->data[0] = nullptr;
		}
		av_frame_free(&rgbFrame);
	}

	if (frame) {
		av_frame_free(&frame);
	}

	if (codecContext) {
		avcodec_free_context(&codecContext);
	}

	if (formatContext) {
		avformat_close_input(&formatContext);
	}

	// 释放预分配的图像缓冲区
	if (alignedData) {
		delete[] alignedData;
		alignedData = nullptr;
	}

	curVideoFile.clear();
	videoStreamIndex = -1;
	hasMoreFrames = false;
	isPlaying = false;
	currentPts = AV_NOPTS_VALUE;
	lastPts = AV_NOPTS_VALUE;
	duration = 0.0;
	fps = 0.0;
	frameIntervalMs = 1;
	totalVideoTime = "00:00:00";

	ui.playSpeedComboBox->setCurrentIndex(1); // 重置为 1x
}


void ArcticLight::openFileDialog() {
	QString filePath = QFileDialog::getOpenFileName(this, "打开视频文件", "../video/", "视频文件 (*.mp4 *.avi *.mkv);;所有文件 (*.*)");
	if (!filePath.isEmpty()) {
		openVideoFile(filePath);
		decodeNextFrame();
	}

}

void ArcticLight::openVideoFile(const QString& filePath) {
	{// 重置播放状态
		isPlaying = false;
		timer->stop();
		hasMoreFrames = true;
		currentPts = AV_NOPTS_VALUE;
		lastPts = AV_NOPTS_VALUE;
		//清理旧资源
		closeVideoFile();
	}
	
	
	QByteArray videoPathBytes = filePath.toUtf8(); // 保存为局部变量
	const char* videoPath = videoPathBytes.constData(); // 获取指针
	int ret = avformat_open_input(&formatContext, videoPath, nullptr, nullptr);//打开视频文件
	if (ret < 0) {
		QMessageBox::critical(this, "错误", "无法打开视频文件");
		return;
	}
	curVideoFile = videoPathBytes;
	ret = avformat_find_stream_info(formatContext, nullptr);//获取流信息
	if(ret < 0) {
		QMessageBox::critical(this, "错误", "无法获取流信息");
		avformat_close_input(&formatContext);
		return;
	}

	//找到视频流和音频流
	for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
		if(formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoStreamIndex = i;
			break;
		}
		else if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioStreamIndex = i;
			break;
		}

	}

	if (videoStreamIndex == -1) {
		QMessageBox::critical(this, "错误", "未找到视频流");
		avformat_close_input(&formatContext);
		return;
	}
	
	//创建并初始化解码器
	AVCodecParameters* codecParams = formatContext->streams[videoStreamIndex]->codecpar;
	const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);//根据编码ID找到解码器
	if (!codec) {
		QMessageBox::critical(this, "错误", "不支持的视频编码格式");
		avformat_close_input(&formatContext);
		return;
	}

	codecContext = avcodec_alloc_context3(codec);//分配解码器上下文
	if (!codecContext) {
		QMessageBox::critical(this, "错误", "无法分配解码器上下文");
		avformat_close_input(&formatContext);
		return;
	}

	ret = avcodec_parameters_to_context(codecContext, codecParams);//将流参数复制到解码器上下文	
	if (ret < 0) {
		QMessageBox::critical(this, "错误", "无法复制解码器参数");
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return;
	}

	ret = avcodec_open2(codecContext, codec, nullptr);//打开解码器
	if (ret < 0) {
		QMessageBox::critical(this, "错误", "无法打开解码器");
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return;
	}

	//计算时长和帧率
	duration = formatContext->duration / 1000000.0;//将微秒转换为秒
	if (duration <= 0) {
		duration = 10.0; // 默认 10 秒
	}

	fps = av_q2d(formatContext->streams[videoStreamIndex]->avg_frame_rate);
	if (fps <= 0) {
		fps = av_q2d(formatContext->streams[videoStreamIndex]->r_frame_rate);
		if (fps <= 0) {
			fps = 25.0; // 默认 25 FPS
		}
	}
	//计算每帧的时间间隔
	frameIntervalMs = static_cast<int>(1000.0 / fps);
	if(frameIntervalMs < 1) {
		frameIntervalMs = 1;//最小间隔为1毫秒
	}
	
	//创建帧和转换上下文
	frame = av_frame_alloc();
	rgbFrame = av_frame_alloc();
	if (!frame || !rgbFrame) {
		QMessageBox::critical(this, "错误", "无法分配帧");
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return;
	}
	rgbFrame->width = codecContext->width;
	rgbFrame->height = codecContext->height;
	rgbFrame->format = AV_PIX_FMT_RGB24;
	// 预分配图像缓冲区
	int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);//分配RGB帧的缓冲区
	uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
	if (!buffer) {
		QMessageBox::critical(this, "错误", "无法分配图像缓冲区");
		av_frame_free(&rgbFrame);
		av_frame_free(&frame);
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return;
	}

	av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer, AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);//创建SWS上下文用于像素格式转换
	//创建SWS上下文用于像素格式转换
	swsContext = sws_getContext(codecContext->width, codecContext->height, 
		codecContext->pix_fmt, codecContext->width, codecContext->height, 
		AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);//打开解码器

	if (!swsContext) {
		QMessageBox::critical(this, "错误", "无法创建图像转换上下文");
		av_free(buffer);
		rgbFrame->data[0] = nullptr;
		av_frame_free(&rgbFrame);
		av_frame_free(&frame);
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return;
	}

	// 计算对齐后的行字节数
	int bytesPerLine = codecContext->width * 3; // RGB24每像素3字节
	alignedBytesPerLine = (bytesPerLine + 3) & ~3; // 确保每行字节数是4的倍数
	alignedData = new uchar[alignedBytesPerLine * codecContext->height];

	//音频流处理
	if (audioStreamIndex != -1) {
		AVCodecParameters* codecParams = formatContext->streams[audioStreamIndex]->codecpar;
		const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);//根据编码ID找到解码器
		if (!codec) {
			QMessageBox::critical(this, "错误", "不支持的音频编码格式");
			return;
		}

		audioCodecContext = avcodec_alloc_context3(codec);//分配解码器上下文
		if (!audioCodecContext) {
			QMessageBox::critical(this, "错误", "无法分配音频解码器上下文");
			return;
		}

		ret = avcodec_parameters_to_context(audioCodecContext, codecParams);//将流参数复制到解码器上下文
		if (ret < 0) {
			QMessageBox::critical(this, "错误", "无法复制音频解码器参数");
			avcodec_free_context(&audioCodecContext);
			return;
		}

		ret = avcodec_open2(audioCodecContext, codec, nullptr);//打开解码器
		if (ret < 0) {
			QMessageBox::critical(this, "错误", "无法打开音频解码器");
			avcodec_free_context(&audioCodecContext);
			return;
		}
		//初始化音频输出
		inputAudioOutput();
	}

	//更新UI
	updateUI();

	QString infoText = "📁 文件路径: " + QString::fromUtf8(formatContext->url) + "\n";
	infoText += "🎬 视频分辨率: " + QString::number(codecContext->width) + "x" + QString::number(codecContext->height) + "\n";
	infoText += "⏱️ 时长: " + QString::number(duration, 'f', 2) + " 秒\n";
	infoText += "🎞️ 帧率: " + QString::number(fps, 'f', 2) + " FPS\n";
	infoText += "💾 编码格式: " + QString::fromUtf8(avcodec_get_name(codecContext->codec_id)) + "\n";
	ui.videoInfolabel->setText(infoText);

	//读取时长
	infoText = "00:00:00 / ";
	int totalSeconds = static_cast<int>(duration);

	// 分解为时分秒
	int hours = totalSeconds / 3600;
	int minutes = (totalSeconds % 3600) / 60;
	int seconds = totalSeconds % 60;

	// 格式化为 HH:MM:SS
	QString hourStr = QString::number(hours).rightJustified(2, '0');
	QString minStr = QString::number(minutes).rightJustified(2, '0');
	QString secStr = QString::number(seconds).rightJustified(2, '0');
	totalVideoTime = QString("%1:%2:%3").arg(hourStr).arg(minStr).arg(secStr);
	infoText += totalVideoTime;

	ui.videoProgresslabel->setText(infoText);
	//初始化进度条
	ui.horizontalSlider->setRange(0, static_cast<int>(duration * 1000)); //以毫秒为单位设置范围

}

void ArcticLight::initAudioOutput()
{
	//设置音频格式
	QAudioFormat format;
	format.setSampleRate(audioCodecContext->sample_rate);//设置采样率
	format.setChannelCount(audioCodecContext->channels);//设置声道数
	format.setSampleSize(16);//设置采样位数
	format.setCodec("audio/pcm");//设置编码格式
	format.setByteOrder(QAudioFormat::LittleEndian);//设置字节序
	format.setSampleType(QAudioFormat::SignedInt);//设置样本类型

	//创建音频输出对象
	audioOutput = new QAudioOutput(format, this);
	audioDevice = audioOutput->start();

	//连接音频缓冲区
	connect(audioOutput, &QAudioOutput::stateChanged, this, [=](QAudio::State state) {
		/*if (state == QAudio::IdleState) {
			audioOutput->stop();
		}
		else */if (state == QAudio::StoppedState && audioOutput->error() != QAudio::NoError) {
			if (audioOutput->error() != QAudio::NoError) {
				QMessageBox::critical(this, "错误", "音频输出错误");
			}
		}
		});
}

void ArcticLight::inokeUpdateInfoEdit(const QString& info)
{
	QMetaObject::invokeMethod(ui.infoTextEdit, [=] {
		ui.infoTextEdit->setPlainText(info);
		}, Qt::QueuedConnection);
	
}
void ArcticLight::decodeNextFrame() {
	av_packet_unref(&packet);
	int ret = av_read_frame(formatContext, &packet);//读取下一帧数据
	if (ret < 0) {
		//视频结束
		hasMoreFrames = false;
		return;
	}
	
	if (packet.stream_index == videoStreamIndex) {
		ret = avcodec_send_packet(codecContext, &packet);//发送数据包到解码器
		if (ret < 0) {
			inokeUpdateInfoEdit(QString("❌ 无法发送数据包到解码器, 错误码:%1").arg(ret));
			av_packet_unref(&packet);
			hasMoreFrames = false;
			return;
		}

		while (ret >= 0) {
			ret = avcodec_receive_frame(codecContext, frame);//从解码器接收解码后的帧
			if (ret == AVERROR(EAGAIN)) {
				continue;
			}
			else if (ret == AVERROR_EOF) {
				inokeUpdateInfoEdit("🏁 解码器已耗尽所有帧");
				break;
			}
			else if (ret < 0) {
				inokeUpdateInfoEdit(QString("❌ 无法从解码器接收帧, 错误码:%1").arg(ret));
				av_packet_unref(&packet);
				hasMoreFrames = false;
				return;

			}
			//检查frame有效性
			if (!frame || frame->width <= 0 || frame->height <= 0) {
				av_packet_unref(&packet);
				hasMoreFrames = false;

				inokeUpdateInfoEdit("⚠️ 接收到无效帧");
				return;
			}

			inokeUpdateInfoEdit(QString("✅ 成功解码一帧, PTS:%1").arg(frame->pts));
			{
				if (frame->pts != AV_NOPTS_VALUE) {
					currentPts = frame->pts;
				}
			}
			

			//转换像素格式
			sws_scale(swsContext, frame->data, frame->linesize, 0, codecContext->height, rgbFrame->data, rgbFrame->linesize);
			// --- ✅ 将 onTimerTimeout 中的更新逻辑移到这里 ---
			AVRational streamTimeBase = formatContext->streams[videoStreamIndex]->time_base;

			if (frame->pts != AV_NOPTS_VALUE) {
				int64_t pts = frame->pts;

				// 使用 av_rescale_q_safe 防止溢出，如果没有则回退到 av_rescale_q
				int64_t currentTimeUs = av_rescale_q(pts, streamTimeBase, { 1, 1000000 }); // 先转换为微秒

				if (currentTimeUs >= 0) { // 确保结果非负
					int currentTimeMs = static_cast<int>(currentTimeUs / 1000);

					// 确保值在进度条范围内，防止 setValue 导致不必要的信号发射
					int clampedValue = qBound(ui.horizontalSlider->minimum(), currentTimeMs, ui.horizontalSlider->maximum());
					ui.horizontalSlider->blockSignals(true);
					ui.horizontalSlider->setValue(clampedValue);
					ui.horizontalSlider->blockSignals(false);
					// 更新时间显示
					int totalSeconds = currentTimeMs / 1000;
					int hours = totalSeconds / 3600;
					int minutes = (totalSeconds % 3600) / 60;
					int seconds = totalSeconds % 60;

					QString hourStr = QString::number(hours).rightJustified(2, '0');
					QString minStr = QString::number(minutes).rightJustified(2, '0');
					QString secStr = QString::number(seconds).rightJustified(2, '0');
					QString currentTimeDisplay = QString("%1:%2:%3").arg(hourStr).arg(minStr).arg(secStr);

					ui.videoProgresslabel->setText(QString("%1 / %2").arg(currentTimeDisplay).arg(totalVideoTime));
				}
			}
			
			//更新UI
			updateUI();

			
		}
	} 
	else if (packet.stream_index == audioStreamindex && audioCodecContext) {
		//音频解码逻辑
		ret = avcodec_send_packet(audioCodecContext, &packet);
		if (ret < 0) {
			inokeUpdateInfoEdit(QString("❌ 无法发送音频包到解码器, 错误码:%1").arg(ret));
			av_packet_unref(&packet);
			return;
		}

		while (ret >= 0) {
			ret = avcodec_receive_frame(audioCodecContext, audioFrame);//从解码器接收解码后的音频帧

		}
	}
	av_packet_unref(&packet);
}

void ArcticLight::updateUI() {
	// 检查 rgbFrame 是否有效
	if (!rgbFrame || rgbFrame->width <= 0 || rgbFrame->height <= 0) {
		inokeUpdateInfoEdit("⚠️ RGB帧无效，无法更新UI");
		return;
	}

	// 创建 QImage
	int bytesPerLine = codecContext->width * 3; // RGB24每像素3字节
	for (int i = 0; i < codecContext->height; i++) {
		memcpy(alignedData + i * alignedBytesPerLine, rgbFrame->data[0] + i * rgbFrame->linesize[0], bytesPerLine);
	}

	QImage image(alignedData, codecContext->width, codecContext->height, alignedBytesPerLine, QImage::Format_RGB888);
	// 自动适配控件大小并保持宽高比
	ui.videoLabel->setPixmap(QPixmap::fromImage(image).scaled(ui.videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
	ui.videoLabel->adjustSize();

	// 显示信息
	QString infoText = "🖼️ 当前帧信息:\n";
	infoText += "🔢 帧序号 (PTS): " + QString::number(frame->pts) + "\n";

	const char* pixFmtName = av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format));
	if (pixFmtName) {
		infoText += "📐 像素格式: " + QString::fromUtf8(pixFmtName) + "\n";
	}
	else {
		infoText += "📐 像素格式: 未知\n";
	}

	infoText += "📊 帧宽高: " + QString::number(frame->width) + "x" + QString::number(frame->height) + "\n";
	infoText += "📏 行大小: " + QString::number(frame->linesize[0]) + " 字节\n";

	inokeUpdateInfoEdit(infoText);
}
void ArcticLight::onhorizontalSliderValueChanged(int value)
{
	if (curVideoFile.isEmpty()) {
		ui.horizontalSlider->setValue(0);
		return;
	}

	//检查视频流是否存在
	if (videoStreamIndex < 0 || videoStreamIndex >= formatContext->nb_streams) {
		QMessageBox::warning(this, "警告", "视频流不存在");
		return;
	}

	AVRational streamTimeBase = formatContext->streams[videoStreamIndex]->time_base;//获取视频流的时间基
	if (streamTimeBase.den <= 0 || streamTimeBase.num <= 0) {
		QMessageBox::critical(this, "警告", "视频流时间基非法");
		return;
	}
	// 安全转换时间戳
	int64_t targetPts = av_rescale_q(value, { 1,1000 }, streamTimeBase);

	if (targetPts == AV_NOPTS_VALUE || targetPts == INT64_MIN) {
		QMessageBox::warning(this, "警告", "无法计算目标时间戳");
		return;
	}

	// 修复：使用 AVSEEK_FLAG_BACKWARD + AVSEEK_FLAG_ANY 来确保能定位到关键帧
	int ret = av_seek_frame(formatContext, videoStreamIndex, targetPts, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
	if (ret < 0) {
		QMessageBox::warning(this, "警告", "无法跳转到指定位置");
		return;
	}

	avcodec_flush_buffers(codecContext);//刷新解码器缓冲区

	hasMoreFrames = true;//重置帧可用状态
	currentPts = AV_NOPTS_VALUE;//重置当前PTS
	lastPts = AV_NOPTS_VALUE;//重置上次PTS
	decodeNextFrame();

}

void ArcticLight::setPlayBackSpeed(double speed)
{
	if (speed <= 0) {
		QMessageBox::warning(this, "警告", "播放速度必须大于0");
		return;
	}

	playbackSpeed = speed;

	//重新计算帧间隔
	frameIntervalMs = static_cast<int>(1000.0 / (fps * playbackSpeed));
	if(frameIntervalMs < 1) {
		frameIntervalMs = 1;//最小间隔为1毫秒
	}

	if (isPlaying) {
		timer->setInterval(frameIntervalMs);
	}


}

void ArcticLight::onPauseButtonClicked()
{
	isPlaying = false;
	timer->stop();
}

void ArcticLight::onPlayButtonClicked()
{
	if (!curVideoFile.isEmpty() && !isPlaying) {
		isPlaying = true;
		currentPts = AV_NOPTS_VALUE;//重置当前PTS
		lastPts = AV_NOPTS_VALUE;//重置上次PTS
		hasMoreFrames = true;//重置帧可用状态
		timer->start(frameIntervalMs);
		decodeNextFrame();//立即解码第一帧
	}

	
}


void ArcticLight::onResetButtonClicked()
{
	if (curVideoFile.isEmpty()) {
		return;
	}

	isPlaying = false;
	timer->stop();
	//重置解码器状态
	hasMoreFrames = true;
	currentPts = AV_NOPTS_VALUE;
	lastPts = AV_NOPTS_VALUE;
	ui.horizontalSlider->setValue(0);
	ui.videoProgresslabel->setText(QString("%1 / %2").arg("00:00:00").arg(totalVideoTime));
	int ret = av_seek_frame(formatContext, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);//从头开始
	if (ret < 0) {
		QMessageBox::warning(this, "警告", "无法重置视频");
		return;
	}

	//刷新解码器缓冲区
	avcodec_flush_buffers(codecContext);

	decodeNextFrame();//立即解码第一帧
	
}
void ArcticLight::onTimerTimeout() {
	if(!isPlaying || !hasMoreFrames) {
		isPlaying = false;
		timer->stop();
		return;
	}

	//解码下一帧
	decodeNextFrame();
	//如果没有更多帧，停止播放
	if (!hasMoreFrames) {
		isPlaying = false;
		timer->stop();
		//可以选择在这里将进度条拉到最后
		ui.horizontalSlider->blockSignals(true); // 阻止信号发射
		ui.horizontalSlider->setValue(ui.horizontalSlider->maximum()); // 播放结束时，进度条拉到最大值
		ui.horizontalSlider->blockSignals(false); // 恢复信号发射
		QMessageBox::information(this, "提示", "视频播放结束");
		return;
	}
	
}

void ArcticLight::ForwardButtonClicked() {
	seekRelative(fastForwardInterval);
}

void ArcticLight::BackwardButtonClicked() {
	seekRelative(-fastForwardInterval);
}
void ArcticLight::seekRelative(int milliseconds)
{
	if (videoStreamIndex < 0 || videoStreamIndex >= formatContext->nb_streams) {
		QMessageBox::warning(this, "警告", "视频流不存在");
		return;
	}

	AVRational streamTimeBase = formatContext->streams[videoStreamIndex]->time_base;

	//获取当前时间戳（毫秒） 
	int64_t currentPtsMs = 0;
	if (currentPts != AV_NOPTS_VALUE) {
		currentPtsMs = av_rescale_q(currentPts, streamTimeBase, { 1,1000 });
	}
	
	//计算目标时间戳（毫秒）
	int64_t targetPtsMs = currentPtsMs + milliseconds;
	targetPtsMs = qBound(0LL, targetPtsMs, static_cast<int64_t>(duration * 1000)); // 确保目标时间戳在有效范围内

	//转换为Pts单位
	int64_t targetPts = av_rescale_q(targetPtsMs, { 1,1000 }, streamTimeBase);

	if (targetPts == AV_NOPTS_VALUE || targetPts == INT64_MIN) {
		QMessageBox::warning(this, "警告", "无法计算目标时间戳");
		return;
	}

	//使用 AVSEEK_FLAG_BACKWARD + AVSEEK_FLAG_ANY 来确保能定位到关键帧
	int ret = av_seek_frame(formatContext, videoStreamIndex, targetPts, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
	if (ret < 0) {
		QMessageBox::warning(this, "警告", "无法跳转到指定位置");
		return;
	}

	avcodec_flush_buffers(codecContext);//刷新解码器缓冲区
	hasMoreFrames = true;//重置帧可用状态
	currentPts = AV_NOPTS_VALUE;//重置当前PTS
	lastPts = AV_NOPTS_VALUE;//重置上次PTS

	currentPts = targetPts; // 更新当前PTS为目标PTS

	decodeNextFrame();
}

void ArcticLight::keyPressEvent(QKeyEvent* event) {
	if (curVideoFile.isEmpty()) {
		return;
	}

	switch (event->key()) {
	case Qt::Key_Left:
		seekRelative(-fastForwardInterval);
		break;
	case Qt::Key_Right:
		seekRelative(fastForwardInterval);
		break;
	case Qt::Key_PageUp:
		seekRelative(-10000);
		break;
	case Qt::Key_PageDown:
		seekRelative(10000);
		break;
	default:
		QMainWindow::keyPressEvent(event);
		break;
	}
}

void ArcticLight::dragEnterEvent(QDragEnterEvent* event) {

	//检查是否有文件被拖入
	if (event->mimeData()->hasUrls())
	{
		//获取第一个文件的路径
		QUrl url = event->mimeData()->urls().first();
		QString filePath = url.toLocalFile();
		//检查文件是否是视频文件（简单检查扩展名）
		QStringList videoExtensions = { ".mp4", ".avi", ".mkv", ".mov", ".flv" };
		QString fileExtension = QFileInfo(filePath).suffix().toLower();
		if (videoExtensions.contains("." + fileExtension)) {
			//接受拖放事件,并显示视觉反馈
			event->acceptProposedAction();
			ui.videoLabel->setStyleSheet("border: 2px dashed #4CAF50; background-color: #e8f5e9; color: #4CAF50;");
		}
		else {
			//拒绝拖放
			event->ignore();
			ui.videoLabel->setStyleSheet("border: 1px solid #ccc; background-color: #f0f0f0; color: #999;");
		}
	}
	else {
		event->ignore();
		ui.videoLabel->setStyleSheet("border: 1px solid #ccc; background-color: #f0f0f0; color: #999;");
	}

}

void ArcticLight::dropEvent(QDropEvent* event) {
	//重置视觉反馈
	ui.videoLabel->setStyleSheet("border: 1px solid #ccc; background-color: #f0f0f0; color: #999;");
	//检查是否有文件被拖入
	if (event->mimeData()->hasUrls()) {
		//获取第一个文件的路径
		QUrl url = event->mimeData()->urls().first();
		QString filePath = url.toLocalFile();

		QStringList videoExtensions = { ".mp4", ".avi", ".mkv", ".mov", ".flv" };
		QString fileExtension = QFileInfo(filePath).suffix().toLower();
		if (videoExtensions.contains("." + fileExtension)) {
			//接受拖放事件
			event->acceptProposedAction();
			openVideoFile(filePath);
			decodeNextFrame();

		}
		else {
			//拒绝拖放
			event->ignore();
			QMessageBox::warning(this, "警告", "不支持的文件类型");
		}

	}
	else {
		event->ignore();
		QMessageBox::warning(this, "警告", "没有检测到文件");
	}

}

