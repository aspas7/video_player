#include "stdafx.h"

Audio* Audio::instance = 0;

Audio* Audio::get_instance()
{
    if (instance == 0)
        instance = new Audio();
    return instance;
}

void Audio::open()
{
    // 配置并打开音频设备。wantedSpec 是期望的音频规格，而 audioSpec 是实际获得的音频规格
    SDLWrapper::open_audio(&wantedSpec, &audioSpec);

    wanted_frame.format = AV_SAMPLE_FMT_S16; // 设置音频帧的样本格式为 16 位整数
    wanted_frame.sample_rate = audioSpec.freq; // 设置音频帧的采样率为 SDL 音频设备实际配置的采样率
    // av_get_default_channel_layout 是 FFmpeg 提供的函数，用于根据通道数推断通道布局。
    wanted_frame.channel_layout = av_get_default_channel_layout(audioSpec.channels); // 根据音频规格中的通道数获取默认的通道布局
    // 设置音频帧的通道数为 SDL 音频设备实际配置的通道数。
    wanted_frame.channels = audioSpec.channels;

    init_audio_packet(&audioq);
    SDL_PauseAudio(0); // 开始音频播放。参数 0 表示开始播放，而不是暂停。
}

void Audio::init_audio_packet(AudioPacket* q)
{
    q->last = NULL;
    q->first = NULL;
    q->mutex = SDL_CreateMutex(); // 创建一个互斥锁
    q->cond = SDL_CreateCond(); // 创建一个条件变量，条件变量用于控制线程的等待和唤醒，当音频数据包队列需要等待新的数据包到来时，相关线程可以在此条件变量上等待。
}

void Audio::malloc(AVCodecContext* pCodecAudioCtx)
{
    AudioCallback::set_audio_instance(this);

    swrCtx = swr_alloc(); // 音频样本格式转换上下文
    if (swrCtx == NULL)
        Utils::display_exception("Failed to load audio");

    av_opt_set_channel_layout(swrCtx, "in_channel_layout", pCodecAudioCtx->channel_layout, 0);  //设置输入的通道布局
    av_opt_set_channel_layout(swrCtx, "out_channel_layout", pCodecAudioCtx->channel_layout, 0); //设置输出的通道布局
    av_opt_set_int(swrCtx, "in_sample_rate", pCodecAudioCtx->sample_rate, 0); //设置输入的采样率
    av_opt_set_int(swrCtx, "out_sample_rate", pCodecAudioCtx->sample_rate, 0); //设置输出的采样率
    //设置输入和输出的样本格式，输入格式使用解码器的格式，输出格式设置为 AV_SAMPLE_FMT_FLT（32位浮点数），这是 SDL 音频播放通常支持的格式
    av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", pCodecAudioCtx->sample_fmt, 0);
    av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

    int res = swr_init(swrCtx); // 初始化一个已经配置好的 SwrContext（音频转换上下文）

    if (res != 0)
        Utils::display_exception("Failed to initialize audio");

    memset(&wantedSpec, 0, sizeof(wantedSpec));  // 使用 memset 清零 wantedSpec 结构体。

    // 设置音频的通道数（即立体声、单声道、四声道等）。
    wantedSpec.channels = pCodecAudioCtx->channels;//从解码器的音频上下文 (AVCodecContext) 获取通道数，这表示音频数据的原始通道配置。
    // 设置音频的采样率（单位是 Hz），即每秒钟采样的次数。 
    wantedSpec.freq = pCodecAudioCtx->sample_rate; 
    wantedSpec.format = AUDIO_S16SYS;  // 表示音频样本的格式为 16 位整数。
    wantedSpec.silence = 0;  // 这个字段设置在音频流中表示“静音”的值。 0: 表示静音值是 0。在实际播放中，如果需要插入静音，SDL 会使用这个值。
    wantedSpec.samples = SDL_AUDIO_BUFFER_SIZE; // 定义了音频缓冲区的大小。
    wantedSpec.userdata = pCodecAudioCtx;  // 设置为指向解码器上下文的指针，以便在音频回调中使用。
    wantedSpec.callback = AudioCallback::audio_callback;  // 这是音频数据填充回调函数。
}

int Audio::audio_decode_frame(AVCodecContext* aCodecCtx, uint8_t* audio_buf, int buf_size) {

    // 使用静态变量可以确保 pkt 在连续的解码调用之间保持其值，这样音频数据包的状态就可以跨多次函数调用保持连续性
    static AVPacket pkt;
    static uint8_t* audio_pkt_data = NULL;
    static int audio_pkt_size = 0;
    static AVFrame frame;

    int len1;
    int data_size = 0;

    SwrContext* swr_ctx = NULL; // 音频转换的上下文，它包含了音频转换所需的所有状态和配置信息。

    while (1)
    {
        while (audio_pkt_size > 0)
        {
            int got_frame = 0;

            avcodec_send_packet(aCodecCtx, &pkt);  // 使用 avcodec_send_packet 将音频包发送到解码器
            avcodec_receive_frame(aCodecCtx, &frame); // 使用 avcodec_receive_frame 从解码器接收解码后的音频帧

            len1 = frame.pkt_size; // 计算解码后的音频帧数据大小，并将其复制到提供的缓冲区 audio_buf 中  , // static uint8_t audio_buff[192000 * 3 / 2];
            if (len1 < 0)
            {
                audio_pkt_size = 0;
                break;
            }

            audio_pkt_data += len1;
            audio_pkt_size -= len1;

            data_size = 0;

            if (got_frame)
            {
                int linesize = 1;
                // 计算给定参数下音频样本缓冲区所需的大小。
                // 这个函数用于确定存储解码后的音频样本所需的内存大小。它基于音频的通道数、采样数、样本格式等因素来计算。
                data_size = av_samples_get_buffer_size(&linesize, aCodecCtx->channels, frame.nb_samples, aCodecCtx->sample_fmt, 1);
                // 调试辅助语句，用于确保 data_size（解码后的音频数据大小）不超过缓冲区的大小 buf_size。
                // 如果 data_size 超过了 buf_size，程序将触发断言失败，通常会导致程序终止运行。
                assert(data_size <= buf_size); // 断言
                // 用于从 frame.data[0]（解码后的音频帧的第一个通道的数据指针）复制 data_size 字节的数据到 audio_buf（目标缓冲区）
                memcpy(audio_buf, frame.data[0], data_size); 
            }

            if (frame.channels > 0 && frame.channel_layout == 0)
                frame.channel_layout = av_get_default_channel_layout(frame.channels);
            else if (frame.channels == 0 && frame.channel_layout > 0)
                frame.channels = av_get_channel_layout_nb_channels(frame.channel_layout);

            if (swr_ctx)
            {
                swr_free(&swr_ctx);
                swr_ctx = NULL;
            }

            // 创建一个新的 SwrContext，配置它以将音频数据从原始格式（由 frame 的相关字段指定）转换为目标格式（由 wanted_frame 的相关字段指定
            swr_ctx = swr_alloc_set_opts(NULL, wanted_frame.channel_layout, (AVSampleFormat)wanted_frame.format, wanted_frame.sample_rate,
                frame.channel_layout, (AVSampleFormat)frame.format, frame.sample_rate, 0, NULL);

            if (!swr_ctx || swr_init(swr_ctx) < 0)
                Utils::display_exception("swr_init failed");

            // 计算出的目标样本数量 (dst_nb_samples) 用于确定在转换过程中，目标缓冲区需要容纳多少样本
            int dst_nb_samples = (int)av_rescale_rnd(swr_get_delay(swr_ctx, frame.sample_rate) + frame.nb_samples,
                wanted_frame.sample_rate, wanted_frame.format, AV_ROUND_INF);

            // 将音频数据从输入格式转换为目标格式，并可能在转换过程中改变采样率。
            int len2 = swr_convert(swr_ctx, &audio_buf, dst_nb_samples,
                (const uint8_t**)frame.data, frame.nb_samples);
            if (len2 < 0)
                Utils::display_exception("swr_convert failed");

            av_packet_unref(&pkt);

            if (swr_ctx)
            {
                swr_free(&swr_ctx);
                swr_ctx = NULL;
            }

            // wanted_frame.channels: 表示音频的目标通道配置
            // len2: 是 swr_convert 函数返回的成功转换的样本数量，表示在转换（可能包括重采样和格式转换）后，目标缓冲区中实际得到的样本数
            // av_get_bytes_per_sample(AV_SAMPLE_FMT_S16): 这是一个调用 FFmpeg 库函数来获取特定样本格式的字节大小。
            // AV_SAMPLE_FMT_S16 是一个枚举值，代表16位整数样本格式。这个函数返回16位样本的字节数，即2字节。
            return wanted_frame.channels * len2 * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
        }

        if (Player::get_instance()->getAudioPacket(&audioq, &pkt, 1) < 0)
            return -1;

        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
    }
}

int Audio::put_audio_packet(AVPacket* packet)
{
    AVPacketList* pktl; // 声明一个新的 AVPacketList 指针 
    // AVPacketList 结构体通常包含以下成员：
    // AVPacket pkt：存储 AVPacket 数据的成员。
    // AVPacketList *next：指向链表中下一个 AVPacketList 节点的指针。
    AVPacket* newPkt; // 声明一个新的 AVPacket 指针
    // av_mallocz_array 分配内存以存储一个新的 AVPacket 结构体。这个函数会分配足够的内存来存储 AVPacket 结构体，并将其初始化为零
    newPkt = (AVPacket*)av_mallocz_array(1, sizeof(AVPacket));
    // av_packet_ref 函数用于复制传入的 packet 到新分配的 newPkt。
    if (av_packet_ref(newPkt, packet) < 0)  
        return -1;

    // av_malloc 分配内存以存储一个新的 AVPacketList 结构体，这个结构体将用于在队列中管理音频包。
    pktl = (AVPacketList*)av_malloc(sizeof(AVPacketList));
    if (!pktl)
        return -1;

    pktl->pkt = *newPkt; // 将新分配的 AVPacket 的内容复制到 pktl->pkt
    pktl->next = NULL;

    SDL_LockMutex(audioq.mutex); // SDL_LockMutex(audioq.mutex); 锁定音频队列的互斥锁，以确保在修改队列时不会发生冲突。

    if (!audioq.last)
        audioq.first = pktl;  // 队列为空，将 pktl 设置为队列的第一个元素
    else
        audioq.last->next = pktl;  // 否则，将 pktl 附加到队列的末尾

    audioq.last = pktl; // 更新队列末尾指针：将 audioq.last 设置为 pktl，更新队列的末尾指针

    audioq.nb_packets++;  //  增加队列中的包数量
    audioq.size += newPkt->size; // 更新队列中总数据的大小

    SDL_CondSignal(audioq.cond);  // 发送一个信号，通知可能在等待新数据的音频回调线程
    SDL_UnlockMutex(audioq.mutex); // 解锁音频队列的互斥锁。

    return 0;
}