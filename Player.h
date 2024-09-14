#include "stdafx.h"

class Player 
{

public:
	static Player* get_instance();

	void run(std::string, std::string window_name="my_player");
	void clear();

	static int getAudioPacket(AudioPacket*, AVPacket*, int);

private:
	static Player* instance;
	Player() {}

	void open();

	int malloc(void);
	int display_video(void);
	int create_display(void);

	int get_video_stream(void);

	int read_audio_video_codec(void);

	std::string video_addr;
	std::string window_name;

	int videoStream = 0;
	int audioStream = 0;

	AVFormatContext* pFormatCtx = NULL; // 容器格式上下文，可以理解为mp4或flv的容器

	AVCodecParameters* pCodecParameters = NULL;   // 视频编解码参数
	AVCodecParameters* pCodecAudioParameters = NULL;  // 音频编解码参数

	AVCodecContext* pCodecCtx = NULL;  // 视频编解码器上下文
	AVCodecContext* pCodecAudioCtx = NULL;  // 音频编解码器上下文

	AVCodec* pCodec = NULL;  // 视频编解码信息
	AVCodec* pAudioCodec = NULL; // 音频编解码信息
	AVFrame* pFrame = NULL; // 解码之后的YUV数据
	AVFrame* pFrameRGB = NULL;

	uint8_t* buffer = NULL;

	SDL_Window* screen = NULL;
	SDL_Renderer* renderer = NULL;
	SDL_Texture* bmp = NULL;

	struct SwsContext* sws_ctx = NULL;
};