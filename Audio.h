#pragma once
class Audio
{
public:
	static Audio* get_instance();

	struct SwrContext* swrCtx = NULL; // 一个指向 SwrContext 结构体的指针，这个结构体用于音频样本格式转换。
	AVFrame wanted_frame;

/*typedef struct _AudioPacket
{
	AVPacketList* first, * last;
	int nb_packets, size;
	SDL_mutex* mutex;
	SDL_cond* cond;
}AudioPacket;*/
	AudioPacket audioq; //AudioPacket 类型的实例，用于管理音频数据包队列

	void open();
	void malloc(AVCodecContext*);
	void init_audio_packet(AudioPacket*);
	int audio_decode_frame(AVCodecContext*, uint8_t*, int);
	int put_audio_packet(AVPacket*);

private:
	Audio() {}
	static Audio* instance;

	// 两个 SDL_AudioSpec 结构体实例，用于存储音频规格信息。wantedSpec 存储期望的音频规格，而 audioSpec 存储实际的音频规格。
	SDL_AudioSpec wantedSpec = { 0 }, audioSpec = { 0 };
};