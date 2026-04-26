#include "FFmpegArc.h"
#include <QFileDialog>
#include <QCoreApplication>

ArcticLight::ArcticLight(QWidget *parent)
	: QMainWindow(parent), formatContext(nullptr), codecContext(nullptr), frame(nullptr), rgbFrame(nullptr), swsContext(nullptr), 
	  videoStreamIndex(-1), isPlaying(false), timer(new QTimer(this)),
	  currentPts(AV_NOPTS_VALUE), lastPts(AV_NOPTS_VALUE), fps(0), frameIntervalMs(16), hasMoreFrames(true)
{
    ui.setupUi(this);
	ui.videoLabel->setAcceptDrops(true);

	// 连接信号槽
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

	// 初始化音频索引
	audioStreamIndex = -1;
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
			av_free(rgbFrame->data[0]);
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

	if (alignedData) {
		delete[] alignedData;
		alignedData = nullptr;
	}

	if (audioOutput) {
		audioOutput->stop();
		delete audioOutput;
		audioOutput = nullptr;
		audioIODevice = nullptr;
	}

	if (swrContext) {
		swr_free(&swrContext);
		swrContext = nullptr;
	}

	if (audioFrame) {
		av_frame_free(&audioFrame);
	}

	if (audioCodecContext) {
		avcodec_free_context(&audioCodecContext);
	}

	curVideoFile.clear();
	videoStreamIndex = -1;
	audioStreamIndex = -1;
	hasMoreFrames = false;
	isPlaying = false;
	currentPts = AV_NOPTS_VALUE;
	lastPts = AV_NOPTS_VALUE;
	duration = 0.0;
	fps = 0.0;
	frameIntervalMs = 16;
	totalVideoTime = "00:00:00";
	playStartPts = AV_NOPTS_VALUE;

	ui.playSpeedComboBox->setCurrentIndex(0);
}

void ArcticLight::openFileDialog() {
	QString filePath = QFileDialog::getOpenFileName(this, "打开视频文件", "../video/", "视频文件 (*.mp4 *.avi *.mkv);;所有文件 (*.*)");
	if (!filePath.isEmpty()) {
		openVideoFile(filePath);
		decodeNextFrame();
	}
}

void ArcticLight::openVideoFile(const QString& filePath) {
	// 重置播放状态
	isPlaying = false;
	timer->stop();
	hasMoreFrames = true;
	currentPts = AV_NOPTS_VALUE;
	lastPts = AV_NOPTS_VALUE;
	closeVideoFile();
	
	QByteArray videoPathBytes = filePath.toUtf8();
	const char* videoPath = videoPathBytes.constData();
	int ret = avformat_open_input(&formatContext, videoPath, nullptr, nullptr);
	if (ret < 0) {
		QMessageBox::critical(this, "错误", "无法打开视频文件");
		return;
	}
	curVideoFile = videoPathBytes;
	
	ret = avformat_find_stream_info(formatContext, nullptr);
	if (ret < 0) {
		QMessageBox::critical(this, "错误", "无法获取流信息");
		avformat_close_input(&formatContext);
		return;
	}

	// 找到视频流和音频流
	for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
		if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex == -1) {
			videoStreamIndex = i;
		}
		else if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex == -1) {
			audioStreamIndex = i;
		}
	}

	if (videoStreamIndex == -1) {
		QMessageBox::critical(this, "错误", "未找到视频流");
		avformat_close_input(&formatContext);
		return;
	}
	
	// 初始化视频解码器
	AVCodecParameters* codecParams = formatContext->streams[videoStreamIndex]->codecpar;
	const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
	if (!codec) {
		QMessageBox::critical(this, "错误", "不支持的视频编码格式");
		avformat_close_input(&formatContext);
		return;
	}

	codecContext = avcodec_alloc_context3(codec);
	if (!codecContext) {
		QMessageBox::critical(this, "错误", "无法分配解码器上下文");
		avformat_close_input(&formatContext);
		return;
	}

	ret = avcodec_parameters_to_context(codecContext, codecParams);
	if (ret < 0) {
		QMessageBox::critical(this, "错误", "无法复制解码器参数");
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return;
	}

	ret = avcodec_open2(codecContext, codec, nullptr);
	if (ret < 0) {
		QMessageBox::critical(this, "错误", "无法打开解码器");
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return;
	}

	// 计算时长和帧率
	duration = formatContext->duration / 1000000.0;
	if (duration <= 0) {
		duration = 10.0;
	}

	fps = av_q2d(formatContext->streams[videoStreamIndex]->avg_frame_rate);
	if (fps <= 0) {
		fps = av_q2d(formatContext->streams[videoStreamIndex]->r_frame_rate);
		if (fps <= 0) {
			fps = 25.0;
		}
	}
	
	// 创建帧
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
	
	int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);
	uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
	if (!buffer) {
		QMessageBox::critical(this, "错误", "无法分配图像缓冲区");
		av_frame_free(&rgbFrame);
		av_frame_free(&frame);
		avcodec_free_context(&codecContext);
		avformat_close_input(&formatContext);
		return;
	}

	av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer, AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);
	
	swsContext = sws_getContext(codecContext->width, codecContext->height, 
		codecContext->pix_fmt, codecContext->width, codecContext->height, 
		AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);

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

	int bytesPerLine = codecContext->width * 3;
	alignedBytesPerLine = (bytesPerLine + 3) & ~3;
	alignedData = new uchar[alignedBytesPerLine * codecContext->height];

	// 显示音频流信息
	if (audioStreamIndex != -1) {
		AVCodecParameters* audioParams = formatContext->streams[audioStreamIndex]->codecpar;
		const AVCodec* audioCodec = avcodec_find_decoder(audioParams->codec_id);
		if (!audioCodec) {
			invokeUpdateInfoEdit("⚠️ 检测到音频流，但不支持的编码格式");
		}else {
			//创建解码器上下文
			audioCodecContext = avcodec_alloc_context3(audioCodec); // 为音频解码器分配上下文
			avcodec_parameters_to_context(audioCodecContext, audioParams); // 将音频流的参数复制到解码器上下文
			avcodec_open2(audioCodecContext, audioCodec, nullptr);// 这里只是为了获取编码信息，实际解码和播放音频需要更多处理

			audioFrame = av_frame_alloc();// 为音频帧分配内存
			invokeUpdateInfoEdit(QString("🔊 音频流 - 采样率: %1 Hz, 声道: %2, 编码: %3")
				.arg(audioCodecContext->sample_rate)
				.arg(audioCodecContext->ch_layout.nb_channels)
				.arg(avcodec_get_name(audioParams->codec_id)));
		}
	}
	// 创建重采样器件并初始化音频输出
	if (audioCodecContext) {
		// 设置输出音频格式：48000Hz, 立体声, Float32
		QAudioFormat format;
		format.setSampleRate(48000);
		format.setChannelCount(2);
		format.setSampleFormat(QAudioFormat::Float);

		QAudioDevice device = QMediaDevices::defaultAudioOutput();
		if (!device.isFormatSupported(format)) {
			invokeUpdateInfoEdit("⚠️ 默认音频设备不支持 Float32 格式，尝试 Int16");
			format.setSampleFormat(QAudioFormat::Int16);
		}

		audioOutput = new QAudioSink(device, format, this);
		audioIODevice = audioOutput->start();

		// 创建重采样上下文，将解码后的音频转为目标格式
		AVChannelLayout outChLayout;
		av_channel_layout_default(&outChLayout, 2); // 立体声
		swrContext = swr_alloc();
		av_opt_set_chlayout(swrContext, "in_chlayout", &audioCodecContext->ch_layout, 0);
		av_opt_set_int(swrContext, "in_sample_rate", audioCodecContext->sample_rate, 0);
		av_opt_set_sample_fmt(swrContext, "in_sample_fmt", audioCodecContext->sample_fmt, 0);
		av_opt_set_chlayout(swrContext, "out_chlayout", &outChLayout, 0);
		av_opt_set_int(swrContext, "out_sample_rate", 48000, 0);
		av_opt_set_sample_fmt(swrContext, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
		swr_init(swrContext);
		av_channel_layout_uninit(&outChLayout);
	}

	// 设置视频显示标签的固定宽高比
	int videoWidth = codecContext->width;
	int videoHeight = codecContext->height;

	int maxWidth = 800;
	int maxHeight = 600;

	double videoAspect = (double)videoWidth / videoHeight;
	double displayWidth, displayHeight;
	if (videoAspect > (double)maxWidth / maxHeight) {
		displayWidth = maxWidth;
		displayHeight = (int)(maxWidth / videoAspect);
	}
	else {
		displayHeight = maxHeight;
		displayWidth = (int)(maxHeight * videoAspect);
	}

	ui.videoLabel->setMinimumSize(displayWidth, displayHeight);
	ui.videoLabel->setMaximumSize(displayWidth, displayHeight);
	ui.videoLabel->setScaledContents(false);

	updateUI();

	QString infoText = "📁 文件: " + QString::fromUtf8(formatContext->url) + "\n";
	infoText += "🎬 分辨率: " + QString::number(codecContext->width) + "x" + QString::number(codecContext->height) + "\n";
	infoText += "⏱️ 时长: " + QString::number(duration, 'f', 2) + " 秒\n";
	infoText += "🎞️ 帧率: " + QString::number(fps, 'f', 2) + " FPS\n";
	infoText += "💾 编码: " + QString::fromUtf8(avcodec_get_name(codecContext->codec_id)) + "\n";
	ui.videoInfolabel->setText(infoText);

	int totalSeconds = static_cast<int>(duration);
	int hours = totalSeconds / 3600;
	int minutes = (totalSeconds % 3600) / 60;
	int seconds = totalSeconds % 60;
	QString hourStr = QString::number(hours).rightJustified(2, '0');
	QString minStr = QString::number(minutes).rightJustified(2, '0');
	QString secStr = QString::number(seconds).rightJustified(2, '0');
	totalVideoTime = QString("%1:%2:%3").arg(hourStr).arg(minStr).arg(secStr);
	
	ui.videoProgresslabel->setText(QString("00:00:00 / %1").arg(totalVideoTime));
	ui.horizontalSlider->setRange(0, static_cast<int>(duration * 1000));
}

// 解码下一帧
void ArcticLight::decodeNextFrame() {
	av_packet_unref(&packet);
	int ret = av_read_frame(formatContext, &packet);
	if (ret < 0) {
		hasMoreFrames = false;
		return;
	}
	
	if (packet.stream_index == videoStreamIndex) {
		ret = avcodec_send_packet(codecContext, &packet);
		if (ret < 0) {
			invokeUpdateInfoEdit(QString("❌ 发送数据包失败: %1").arg(ret));
			hasMoreFrames = false;
			return;
		}

		ret = avcodec_receive_frame(codecContext, frame);
		if (ret == AVERROR(EAGAIN)) {
			// 需要更多数据包
		}
		else if (ret == AVERROR_EOF) {
			invokeUpdateInfoEdit("🏁 解码完成");
			hasMoreFrames = false;
		}
		else if (ret < 0) {
			invokeUpdateInfoEdit(QString("❌ 接收帧失败: %1").arg(ret));
			hasMoreFrames = false;
		}
		else {
			if (frame->pts != AV_NOPTS_VALUE) {
				currentPts = frame->pts;
			}

			// 转换像素格式
			sws_scale(swsContext, frame->data, frame->linesize, 0, codecContext->height, rgbFrame->data, rgbFrame->linesize);

			// 更新进度
			AVRational streamTimeBase = formatContext->streams[videoStreamIndex]->time_base;
			if (frame->pts != AV_NOPTS_VALUE) {
				int64_t currentTimeUs = av_rescale_q(frame->pts, streamTimeBase, { 1, 1000000 });
				if (currentTimeUs >= 0) {
					int currentTimeMs = static_cast<int>(currentTimeUs / 1000);
					int clampedValue = qBound(ui.horizontalSlider->minimum(), currentTimeMs, ui.horizontalSlider->maximum());
					ui.horizontalSlider->blockSignals(true);
					ui.horizontalSlider->setValue(clampedValue);
					ui.horizontalSlider->blockSignals(false);

					int totalSeconds = currentTimeMs / 1000;
					int hours = totalSeconds / 3600;
					int minutes = (totalSeconds % 3600) / 60;
					int seconds = totalSeconds % 60;
					QString currentTimeDisplay = QString("%1:%2:%3")
						.arg(hours, 2, 10, QChar('0'))
						.arg(minutes, 2, 10, QChar('0'))
						.arg(seconds, 2, 10, QChar('0'));
					ui.videoProgresslabel->setText(QString("%1 / %2").arg(currentTimeDisplay).arg(totalVideoTime));
				}
			}

			updateUI();
		}
	}
	else if (packet.stream_index == audioStreamIndex && audioCodecContext && swrContext && audioIODevice) {
		// 发送音频数据包到解码器
		int ret = avcodec_send_packet(audioCodecContext, &packet);
		if (ret < 0) {
			return; // 音频解码错误，继续播放视频
		}

		// 接收解码后的音频帧
		while (ret >= 0) {
			ret = avcodec_receive_frame(audioCodecContext, audioFrame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			}
			if (ret < 0) {
				break;
			}

			// 重采样：将音频帧转为目标格式
			int outSamples = av_rescale_rnd(swr_get_delay(swrContext, audioCodecContext->sample_rate) + audioFrame->nb_samples,
				48000, audioCodecContext->sample_rate, AV_ROUND_UP);
			uint8_t* outBuffer = nullptr;
			int outBufferSize = av_samples_alloc(&outBuffer, nullptr, 2, outSamples, AV_SAMPLE_FMT_FLT, 0);
			
			if (outBuffer && outBufferSize > 0) {
				int convertedSamples = swr_convert(swrContext, &outBuffer, outSamples,
					(const uint8_t**)audioFrame->data, audioFrame->nb_samples);
				if (convertedSamples > 0) {
					int dataSize = av_samples_get_buffer_size(nullptr, 2, convertedSamples, AV_SAMPLE_FMT_FLT, 0);
					audioIODevice->write((const char*)outBuffer, dataSize);
				}
				av_freep(&outBuffer);
			}

			av_frame_unref(audioFrame);
		}
	}
}

void ArcticLight::updateUI() {
	if (!rgbFrame || rgbFrame->width <= 0 || rgbFrame->height <= 0) {
		return;
	}
	
	int actualWidth = rgbFrame->width;
	int actualHeight = rgbFrame->height;
	int actualLineSize = rgbFrame->linesize[0];
	int bytesPerLine = actualWidth * 3;

	// 如果缓冲区不够，重新分配
	int neededSize = ((bytesPerLine + 3) & ~3) * actualHeight;
	if (currentBufferSize < neededSize) {
		delete[] alignedData;
		alignedData = new uchar[neededSize];
		alignedBytesPerLine = (bytesPerLine + 3) & ~3;
		currentBufferSize = neededSize;
	}

	// 复制数据
	int copyBytes = qMin(bytesPerLine, actualLineSize);
	for (int i = 0; i < actualHeight; i++) {
		memcpy(alignedData + i * alignedBytesPerLine,
			rgbFrame->data[0] + i * actualLineSize,
			copyBytes);
	}

	// 创建 QImage
	QImage image(alignedData, actualWidth, actualHeight, alignedBytesPerLine, QImage::Format_RGB888);
	ui.videoLabel->setPixmap(QPixmap::fromImage(image).scaled(ui.videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
	ui.videoLabel->adjustSize();

	QString infoText = "📊 尺寸: " + QString::number(frame->width) + "x" + QString::number(frame->height);
	invokeUpdateInfoEdit(infoText);
}

void ArcticLight::onhorizontalSliderValueChanged(int value)
{
	if (curVideoFile.isEmpty()) {
		ui.horizontalSlider->setValue(0);
		return;
	}

	if (videoStreamIndex < 0 || videoStreamIndex >= (int)formatContext->nb_streams) {
		return;
	}

	AVRational streamTimeBase = formatContext->streams[videoStreamIndex]->time_base;
	int64_t targetPts = av_rescale_q(value, { 1,1000 }, streamTimeBase);

	int ret = av_seek_frame(formatContext, videoStreamIndex, targetPts, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
	if (ret < 0) {
		return;
	}

	avcodec_flush_buffers(codecContext);
	hasMoreFrames = true;
	currentPts = AV_NOPTS_VALUE;
	lastPts = AV_NOPTS_VALUE;
	decodeNextFrame();
}

void ArcticLight::setPlayBackSpeed(double speed)
{
	if (speed <= 0) return;
	playbackSpeed = speed;
	frameIntervalMs = static_cast<int>(1000.0 / (fps * playbackSpeed));
	if (frameIntervalMs < 1) frameIntervalMs = 1;
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
		currentPts = AV_NOPTS_VALUE;
		lastPts = AV_NOPTS_VALUE;
		hasMoreFrames = true;
		timer->start(frameIntervalMs);
		decodeNextFrame();
	}
}

void ArcticLight::onResetButtonClicked()
{
	if (curVideoFile.isEmpty()) return;
	isPlaying = false;
	timer->stop();
	hasMoreFrames = true;
	currentPts = AV_NOPTS_VALUE;
	lastPts = AV_NOPTS_VALUE;
	ui.horizontalSlider->setValue(0);
	ui.videoProgresslabel->setText(QString("00:00:00 / %1").arg(totalVideoTime));
	
	int ret = av_seek_frame(formatContext, videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
	if (ret < 0) {
		QMessageBox::warning(this, "警告", "无法重置视频");
		return;
	}
	avcodec_flush_buffers(codecContext);
	decodeNextFrame();
}

void ArcticLight::onTimerTimeout() {
	if (!isPlaying || !hasMoreFrames) {
		isPlaying = false;
		timer->stop();
		return;
	}
	decodeNextFrame();
	if (!hasMoreFrames) {
		isPlaying = false;
		timer->stop();
		ui.horizontalSlider->blockSignals(true);
		ui.horizontalSlider->setValue(ui.horizontalSlider->maximum());
		ui.horizontalSlider->blockSignals(false);
		QMessageBox::information(this, "提示", "视频播放结束");
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
	if (videoStreamIndex < 0 || videoStreamIndex >= (int)formatContext->nb_streams) return;
	
	AVRational streamTimeBase = formatContext->streams[videoStreamIndex]->time_base;
	int64_t currentPtsMs = 0;
	if (currentPts != AV_NOPTS_VALUE) {
		currentPtsMs = av_rescale_q(currentPts, streamTimeBase, { 1,1000 });
	}
	
	int64_t targetPtsMs = qBound(0LL, currentPtsMs + milliseconds, static_cast<int64_t>(duration * 1000));
	int64_t targetPts = av_rescale_q(targetPtsMs, { 1,1000 }, streamTimeBase);

	int ret = av_seek_frame(formatContext, videoStreamIndex, targetPts, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
	if (ret < 0) return;

	avcodec_flush_buffers(codecContext);
	hasMoreFrames = true;
	currentPts = AV_NOPTS_VALUE;
	lastPts = AV_NOPTS_VALUE;
	currentPts = targetPts;
	decodeNextFrame();
}

void ArcticLight::keyPressEvent(QKeyEvent* event) {
	if (curVideoFile.isEmpty()) return;
	
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
	}
}

void ArcticLight::dragEnterEvent(QDragEnterEvent* event) {
	if (event->mimeData()->hasUrls()) {
		QUrl url = event->mimeData()->urls().first();
		QString filePath = url.toLocalFile();
		QStringList videoExtensions = { "mp4", "avi", "mkv", "mov", "flv" };
		QString fileExtension = QFileInfo(filePath).suffix().toLower();
		if (videoExtensions.contains(fileExtension)) {
			event->acceptProposedAction();
			ui.videoLabel->setStyleSheet("border: 2px dashed #4CAF50; background-color: #e8f5e9;");
		}
	}
}

void ArcticLight::dropEvent(QDropEvent* event) {
	ui.videoLabel->setStyleSheet("border: 1px solid #ccc;");
	if (event->mimeData()->hasUrls()) {
		QUrl url = event->mimeData()->urls().first();
		QString filePath = url.toLocalFile();
		QStringList videoExtensions = { "mp4", "avi", "mkv", "mov", "flv" };
		QString fileExtension = QFileInfo(filePath).suffix().toLower();
		if (videoExtensions.contains(fileExtension)) {
			event->acceptProposedAction();
			openVideoFile(filePath);
			decodeNextFrame();
		}
	}
}

void ArcticLight::invokeUpdateInfoEdit(const QString& info)
{
	QMetaObject::invokeMethod(this, [this, info] {
		ui.infoTextEdit->setPlainText(info);
	}, Qt::QueuedConnection);
}
