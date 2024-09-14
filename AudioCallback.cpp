#include "stdafx.h"
Audio* AudioCallback::audio_instance = 0; // 静态成员变量，用于存储 Audio 类的实例指针，该实例负责音频解码。
void AudioCallback::set_audio_instance(Audio* audio_instance)
{
	AudioCallback::audio_instance = audio_instance;
}

void AudioCallback::audio_callback(void* userdata, Uint8* stream, int len)
{
    AVCodecContext* aCodecCtx = (AVCodecContext*)userdata;
    int len1, audio_size;

    static uint8_t audio_buff[192000 * 3 / 2];  // 静态音频缓冲区，用于存储解码后的音频数据
    static unsigned int audio_buf_size = 0; // 存储在 audio_buff 中的音频数据的实际大小
    static unsigned int audio_buf_index = 0; // 指向 audio_buff 中下一个要读取的字节的索引

    SDL_memset(stream, 0, len); // 先将 stream 缓冲区清零

    while (len > 0)
    {
        if (audio_buf_index >= audio_buf_size)  // 如果 audio_buff 中的数据已经全部读取完毕，需要解码新的音频帧。
        {
            // 调用 Audio 类的实例方法 audio_decode_frame 来解码音频帧，并将解码后的数据存储到 audio_buff 中
            audio_size = AudioCallback::audio_instance->audio_decode_frame(aCodecCtx, audio_buff, sizeof(audio_buff));
            if (audio_size < 0)  // 如果解码失败，填充静音到 audio_buff 并重置 audio_buf_size 和 audio_buf_index
            {
                audio_buf_size = 1024;
                // 调用 memset 函数将 audio_buff 缓冲区的内容全部设置为0。
                // 这实际上是在填充静音数据到缓冲区，因为所有字节都被设置为0，在音频播放时这代表没有音频信号。
                memset(audio_buff, 0, audio_buf_size);
            }
            else
                // 将 audio_buf_size 设置为 audio_size。这个值表示解码后音频数据实际占用的字节大小
                audio_buf_size = audio_size;

            audio_buf_index = 0; // 重置索引为0
        }
        len1 = audio_buf_size - audio_buf_index; // 计算 audio_buff 中剩余的音频数据量
        if (len1 > len) // 如果剩余的音频数据量大于音频流 stream 需要填充的数据量，将其限制为 len。
            len1 = len;

        //  使用 SDL 函数将 audio_buff 中的音频数据混合到音频流 stream 中。
        SDL_MixAudio(stream, audio_buff + audio_buf_index, len, SDL_MIX_MAXVOLUME);

        len -= len1; // 减少 stream 需要填充的数据量
        stream += len1; // 移动 stream 指针，指向下一个需要填充的位置
        audio_buf_index += len1; // 增加 audio_buff 的索引，指向下一个要读取的字节
    }
}