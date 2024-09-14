#include "stdafx.h"

using namespace std;

Player* Player::instance = 0;
Player* Player::get_instance()
{
	if (instance == 0)
		instance = new Player();
	return instance;
}

void Player::run(std::string video_addr, std::string window_name)
{
	this->video_addr = video_addr;
	this->window_name = window_name;

	this->open();
	this->malloc();
	this->create_display();
	this->display_video();
}

void Player::open()
{
	audioStream = -1;

	// open video，解复用，AVFormatContext* pFormatCtx，容器格式上下文
	int res = avformat_open_input(&pFormatCtx, this->video_addr.c_str(), NULL, NULL);

	// check video
	if (res != 0)
		Utils::display_ffmpeg_exception(res);

	// get video info，获取码流信息
	res = avformat_find_stream_info(pFormatCtx, NULL);
	if (res < 0)
		Utils::display_ffmpeg_exception(res);

	// get video stream
	videoStream = get_video_stream();
	if (videoStream == -1)
		Utils::display_exception("Error opening your video using AVCodecParameters, probably doesnt have codecpar_type type AVMEDIA_TYPE_VIDEO");

	// open
	read_audio_video_codec();
}

void Player::clear()
{
	// close context info
	avformat_close_input(&pFormatCtx);
	avcodec_free_context(&pCodecCtx);

	// free buffers
	av_free(buffer);
	av_free(pFrameRGB);

	// Free the YUV frame
	av_free(pFrame);

	// Close the codecs
	avcodec_close(pCodecCtx);

	// Close the video file
	avformat_close_input(&pFormatCtx);

	delete Player::get_instance();
}

/*
Acquires video stream
*/
int Player::get_video_stream(void)
{
	int videoStream = -1;

	for (unsigned int i = 0; i<pFormatCtx->nb_streams; i++){
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) videoStream = i;
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) audioStream = i;
	}

	if (videoStream == -1)
		Utils::display_exception("Couldnt find stream");

	pCodecParameters = pFormatCtx->streams[videoStream]->codecpar; // 视频编解码参数
	if(audioStream != -1) pCodecAudioParameters = pFormatCtx->streams[audioStream]->codecpar; // 音频编解码参数

	return videoStream;
}

/*
Reads audio and video codec
*/
int Player::read_audio_video_codec(void) 
{
	pCodec = avcodec_find_decoder(pCodecParameters->codec_id); // 视频编解码信息
	pAudioCodec = avcodec_find_decoder(pCodecAudioParameters->codec_id); // 音频编解码信息

	if (pCodec == NULL)
		Utils::display_exception("Video decoder not found");

	if (pAudioCodec == NULL) 
		Utils::display_exception("Audio decoder not found");

	pCodecCtx = avcodec_alloc_context3(pCodec);  // 通过传递pCodec编解码信息来初始化视频编解码器上下文

	if(pCodecCtx == NULL)
		Utils::display_exception("Failed to allocate video context decoder");

	pCodecAudioCtx = avcodec_alloc_context3(pAudioCodec);  // 通过传递pAudioCodec编解码信息来初始化音频编解码器上下文

	if(pCodecAudioCtx == NULL)
		Utils::display_exception("Failed to allocate audio context decoder");

	int res = avcodec_parameters_to_context(pCodecCtx, pCodecParameters); // 把编解码参数拷贝到编解码器上下文

	if(res < 0)
		Utils::display_exception("Failed to transfer video parameters to context");

	res = avcodec_parameters_to_context(pCodecAudioCtx, pCodecAudioParameters); // 把编解码参数拷贝到编解码器上下文

	if (res < 0) 
		Utils::display_exception("Failed to transfer audio parameters to context");

	res = avcodec_open2(pCodecCtx, pCodec, NULL); //打开一个解码器（或编码器），并将它与一个编解码器上下文关联起来。

	if(res < 0)
		Utils::display_exception("Failed to open video codec");

	res = avcodec_open2(pCodecAudioCtx, pAudioCodec, NULL); //打开一个解码器（或编码器），并将它与一个编解码器上下文关联起来。

	if (res < 0)
		Utils::display_exception("Failed to open auvio codec");

	return 1;
}

/*
Alloc memory for the display
*/
int Player::malloc(void)
{
	// 为音频处理分配必要的资源，包括为音频帧、音频转换上下文（如 SwrContext）和其他音频处理相关的结构分配内存。
	Audio::get_instance()->malloc(pCodecAudioCtx); 

	Audio::get_instance()->open();

	pFrame = av_frame_alloc();//使用 av_frame_alloc 分配一个用于存储原始视频帧数据的 AVFrame 结构体 pFrame
	if (pFrame == NULL)
		Utils::display_exception("Couldnt allocate frame memory");

	pFrameRGB = av_frame_alloc();//使用 av_frame_alloc 分配另一个 AVFrame 结构体 pFrameRGB，用于存储转换为RGB格式后的视频帧数据
	if (pFrameRGB == NULL)
		Utils::display_exception("Couldnt allocate rgb frame memory");

	//计算存储转换后的RGB图像所需的缓冲区大小。根据图像的像素格式、宽度、高度和铝线数（在这里设置为1）来计算所需的内存大小
	int numBytes = av_image_get_buffer_size(VIDEO_FORMAT, pCodecCtx->width, pCodecCtx->height,1);

	//使用 av_malloc 分配一个缓冲区 buffer 来存储RGB图像数据。分配的内存大小由 av_image_get_buffer_size 计算得出。
	buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));

	//函数用于填充 AVFrame 结构体的 data 和 linesize 数组。data 数组包含了指向图像平面的指针，而 linesize 数组包含了每个图像平面的行大小
	// pFrameRGB->data：指向 AVFrame 结构体中的数据指针数组。这个数组将被填充为指向图像平面的指针。
	// pFrameRGB->linesize：指向 AVFrame 结构体中的行大小数组。这个数组将被填充为每个图像平面的行的字节大小。
	// buffer：指向之前分配的内存缓冲区，这个缓冲区将被用来存储图像数据。
	// VIDEO_FORMAT：一个宏，定义了图像数据的像素格式（如 AV_PIX_FMT_RGB24），这个格式表示图像数据将被存储为RGB格式。
	// pCodecCtx->width 和 pCodecCtx->height：视频帧的宽度和高度。
	// 1: 这个参数通常用于指定图像数据的读取方式，1 表示按行读取，0 表示按平面读取。
	// av_image_fill_arrays: 这个函数用于分配内存，并将其填充到图像帧的数据指针数组和行大小数组中。
	// 函数作用是为一个 AVFrame 分配内存，并将其填充为可以存储指定格式、尺寸的图像数据的缓冲区。
	// 如果 av_image_fill_arrays 成功执行，pFrameRGB 将包含指向正确大小和格式的图像数据的指针，可以用于图像处理或显示。
	int res = av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer, VIDEO_FORMAT, pCodecCtx->width, pCodecCtx->height, 1);
	if (res < 0)
		Utils::display_ffmpeg_exception(res);
	
	return 1;
}


int Player::getAudioPacket(AudioPacket* q, AVPacket* pkt, int block){

	AVPacketList* pktl;  // 用于存储指向队列中数据包节点的指针
    int ret;  // 用于存储方法的返回值，表示操作的成功与否

    SDL_LockMutex(q->mutex);  // 锁定 AudioPacket 对象的互斥锁，以确保线程安全地访问队列

    while (1)
    {
        pktl = q->first;
        if (pktl)
        {
            q->first = pktl->next;  // 更新队列的头部指针和尾部指针，减少队列的包计数和大小
            if (!q->first)
                q->last = NULL;

            q->nb_packets--;
            q->size -= pktl->pkt.size;

            *pkt = pktl->pkt;  //  将获取到的数据包复制到提供的 AVPacket 结构体中
            av_free(pktl);  // 释放数据包节点的内存
            ret = 1;
            break;
        }
        else if (!block) //  如果 block 参数为0，即处于非阻塞模式，且队列为空，则设置返回值为0，表示没有数据包可用。
        {
            ret = 0;
            break;
        }
        else  // 如果处于阻塞模式（block 参数为1），且队列为空，则调用 SDL_CondWait 在互斥锁下等待条件变量被信号唤醒。
        {
            SDL_CondWait(q->cond, q->mutex);
        }
    }

    SDL_UnlockMutex(q->mutex); // 在循环结束后，解锁互斥锁

    return ret;
}

/*
Read frames and display
*/
int Player::display_video(void) {

	AVPacket packet;  // 声明一个 AVPacket 类型的变量 packet，用于存储从视频文件中读取的数据包。

	// video context，初始化图像转换上下文，该上下文可以用于在不同的像素格式之间转换图像，或者改变图像的大小
	// 这里指定了原始视频的宽度、高度和像素格式，以及目标格式
	sws_ctx = sws_getContext(pCodecCtx->width, 
		pCodecCtx->height,
		pCodecCtx->pix_fmt,
		pCodecCtx->width,
		pCodecCtx->height,
		VIDEO_FORMAT,
		SWS_BILINEAR,
		NULL,
		NULL,
		NULL
		);
	SDL_Event evt;  // 创建一个 SDL_Event 类型的变量 evt，用于在事件循环中存储和处理事件

	while (av_read_frame(pFormatCtx, &packet) >= 0) {

		if (packet.stream_index == audioStream) {
			Audio::get_instance()->put_audio_packet(&packet);
		}

		if (packet.stream_index == videoStream) 
		{

			int res = avcodec_send_packet(pCodecCtx, &packet); // 往视频解码器发送一个avpacket
			if (res < 0)
				Utils::display_ffmpeg_exception(res);

			res = avcodec_receive_frame(pCodecCtx, pFrame); // 从视频解码器读取一个avframe
			
			// 使用 SDL 函数更新 YUV 纹理 bmp。这个函数将解码后的 YUV 数据从 pFrame 复制到纹理中，准备渲染
			SDL_UpdateYUVTexture(bmp, NULL, pFrame->data[0], pFrame->linesize[0],
				pFrame->data[1], pFrame->linesize[1],
				pFrame->data[2], pFrame->linesize[2]);
			SDL_RenderCopy(renderer, bmp, NULL, NULL); //  将纹理复制到渲染目标（通常是屏幕或窗口）。
			SDL_RenderPresent(renderer); // 呈现渲染目标，使更新的纹理可见
			SDL_UpdateWindowSurface(screen); // 更新整个窗口表面，确保新渲染的帧被显示。
			SDL_Delay(1000/30); // 延迟大约 33 毫秒（1000 毫秒除以 30），以匹配大约 30 帧每秒的帧率。这个延迟有助于控制视频播放的速度，使其更加平滑。
		}

		SDL_PollEvent(&evt);

		av_packet_unref(&packet); // 减少 AVPacket 结构体的引用计数，并在引用计数达到0时释放所有与该数据包关联的资源
		// AVPacket 结构体可能在多个地方被引用，例如在解码器、网络接收缓冲区或文件读取缓冲区中。
	}

	return 1;
}


/*
Create the display for the received video
*/
int Player::create_display(void) 
{
	// 调用 SDL_CreateWindow 函数创建一个新的 SDL 窗口。窗口的标题由 window_name 决定，尺寸由解码器上下文 pCodecCtx 中的视频宽度和高度决定。
    // SDL_WINDOWPOS_CENTERED 确保窗口在屏幕中央显示。
    // SDL_WINDOW_OPENGL 标志表示窗口将使用 OpenGL 进行渲染。
	screen = SDL_CreateWindow(window_name.c_str(), 
			SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED,
			pCodecCtx->width, pCodecCtx->height,
			SDL_WINDOW_OPENGL);
	
	if (!screen)
		Utils::display_exception("Couldn't show display window");

	// 调用 SDL_CreateRenderer 函数创建一个新的 SDL 渲染器，与刚刚创建的窗口关联。
	// 第二个参数 -1 表示让 SDL 选择第一个合适的渲染器。
	// 第三个参数 0 用于指定渲染器的选项（在这里不设置任何特定选项）。
	renderer = SDL_CreateRenderer(screen, -1, 0);

	// 调用 SDL_CreateTexture 函数创建一个新的 SDL 纹理，用于存储 YUV 格式的视频帧数据。
	// SDL_PIXELFORMAT_YV12 指定纹理的像素格式为 YV12，这是一种常用的 YUV 格式。
	// SDL_TEXTUREACCESS_STATIC 表示纹理数据不会频繁更改。
	// 纹理的宽度和高度与视频的宽度和高度相同。
	bmp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STATIC, pCodecCtx->width, pCodecCtx->height);

	return 1;
}