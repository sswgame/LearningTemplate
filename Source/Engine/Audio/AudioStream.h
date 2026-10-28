/**
 * @file AudioStream.h
 * @brief 긴 소리(음악, 긴 앰비언스)를 통째로 풀지 않고 재생하는 스트리밍입니다. 압축된 바이트를 한 벌만 들고, 재생하는 보이스마다 디코더가 앞으로 가며 조금씩 풉니다.
 * @details 3 분 스테레오 48 kHz 를 float 로 풀면 약 69 MB 인데, 같은 곡의 OGG 는 3 ~ 4 MB 다. 스트리밍 클립(`AudioClipData::_pStream`)은 샘플 배열을 비워 두고, 보이스가 디코더로 필요한 만큼만
 *          창(window)에 풀어 읽는다. 언리얼의 스트리밍 사운드 웨이브, 유니티의 Streaming 로드 타입과 같은 모양이다.
 *
 *          - **디코더는 보이스마다 하나**다(`AudioStreamSource::openDecoder`). 같은 곡을 두 보이스가 재생해도 압축 바이트는 한 벌이다.
 *          - **디코드는 믹서 스레드에서** 한다(블록마다 필요한 만큼). OGG 한 블록(256 프레임)이 수 마이크로초라 워커 스레드 링 버퍼는 두지 않았다.
 *          - 파일을 처음부터 끝까지 메모리에 읽는다. 팩의 부분 읽기 API 가 없어서 디스크 스트리밍은 아니다.
 *          - 지원 형식: OGG Vorbis(채널 1 ~ 8), WAV(16 비트 정수 · 32 비트 float PCM, 채널 1 ~ 8). 나머지는 통째로 푼다.
 *          - 3 채널 이상은 프레임을 읽을 때 명시 다운믹스(`AudioDownmix`)로 스테레오로 줄인다. 행렬은 소스를 만들 때 한 번 정한다(WAV 는 채널 마스크, Vorbis 는 명세의 순서).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/AudioDownmix.h"

namespace sw
{
    /** @brief 스트리밍 소리의 형식입니다. */
    struct AudioStreamInfo
    {
        uint32 _sampleRate{ 0 };
        uint32 _frameCount{ 0 };   ///< 채널당 전체 프레임 수
        uint16 _channelCount{ 0 }; ///< 파일의 채널 수(1 ~ 8)다. 재생은 3 채널 이상이면 다운믹스한 2 채널이다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAudioStreamDecoder
     * @brief 압축된 소리 하나를 앞으로 가며 읽는 디코더입니다. 믹서 스레드 하나만 부릅니다.
     */
    class SW_API IAudioStreamDecoder
    {
    public:
        IAudioStreamDecoder()          = default;
        virtual ~IAudioStreamDecoder() = default;

        IAudioStreamDecoder( const IAudioStreamDecoder& )            = delete;
        IAudioStreamDecoder& operator=( const IAudioStreamDecoder& ) = delete;

        /**
         * @brief 다음 @p frameCount 프레임까지를 @p pOut(채널 교차 float)에 풉니다.
         * @return 실제로 푼 프레임 수입니다. 끝에 닿으면 요청보다 적고, 더 없으면 0 입니다.
         */
        virtual uint32 readFrames( float32* pOut, uint32 frameCount ) = 0;
        /** @brief 다음에 읽을 프레임을 @p frame 으로 옮깁니다. 범위 밖이거나 실패하면 false 입니다. */
        [[nodiscard]] virtual bool seekToFrame( uint32 frame ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AudioStreamSource
     * @brief 스트리밍하는 소리 하나의 압축 바이트와 형식입니다. 만든 뒤 바뀌지 않아 보이스들이 나눠 읽습니다.
     */
    class SW_API AudioStreamSource
    {
    public:
        /**
         * @brief 바이트가 스트리밍할 수 있는 형식이고 길이가 @p minSeconds 초 이상이면 소스를 만듭니다.
         * @param path 확장자(`.ogg`, `.wav`)로 형식을 고릅니다.
         * @param inoutBytes 파일 바이트입니다. **만들었을 때만** 소스가 가져가고(비워집니다), 실패하면 그대로 남아 호출자가 통째로 풉니다.
         * @param minSeconds 이보다 짧은 소리는 만들지 않습니다(클립 자신의 샘플레이트로 측정한 길이). 0 이면 길이를 보지 않습니다.
         * @return 짧거나 지원하지 않는 형식이거나 헤더가 틀렸으면 nullptr 입니다.
         */
        static shared_ptr<const AudioStreamSource> tryCreate( string_view path, vector<uint8>& inoutBytes, float32 minSeconds );

        const AudioStreamInfo& getInfo() const { return _info; }
        /** @brief 재생 채널 수(1 · 2)입니다. 3 채널 이상인 파일은 다운믹스해 2 입니다. */
        uint16 getPlaybackChannelCount() const { return _info._channelCount >= 2 ? static_cast<uint16>( 2 ) : _info._channelCount; }
        /** @brief 3 채널 이상인 파일의 다운믹스 행렬입니다(`_channelCount` 0 이면 다운믹스 없음). */
        const AudioDownmix& getDownmix() const { return _downmix; }
        /** @brief 압축 바이트 수입니다. */
        size_t getByteCount() const { return _pBytes != nullptr ? _pBytes->size() : 0u; }
        /** @brief 처음부터 읽는 디코더를 엽니다. 실패하면 nullptr 입니다. */
        unique_ptr<IAudioStreamDecoder> openDecoder() const;

    private:
        /** @brief 소스의 바이트 형식입니다. */
        enum class Format : uint8
        {
            Vorbis = 0,
            WAVPcm16,
            WAVFloat32
        };

        shared_ptr<const vector<uint8>> _pBytes{};
        AudioStreamInfo                 _info{};
        AudioDownmix                    _downmix{};       ///< 3 채널 이상일 때의 행렬이다.
        size_t                          _dataOffset{ 0 }; ///< WAV 의 data 청크 시작
        uint32                          _dataBytes{ 0 };
        Format                          _format{ Format::Vorbis };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AudioStreamWindow
     * @brief 디코더 위의 미끄러지는 창입니다. 보이스가 프레임 번호로 읽으면 창 안이면 바로, 창 바로 뒤면 한 덩어리(`kChunkFrames`)를 더 풀어 이어 붙이고, 그 밖이면 되감아(seek) 새 창을 엽니다.
     * @details 재생은 앞으로 가므로 창 뒤쪽 읽기가 대부분이고, 되감기는 루프를 한 바퀴 돌 때와 위치를 옮길 때만 일어난다. 창은 지나간 프레임을 버리되 마지막 몇 프레임(`kRetainFrames`)은 남긴다 —
     *          보간이 같은 프레임을 다시 읽는다. 재사용하는 버퍼라 정상 상태에서는 할당하지 않는다.
     */
    class SW_API AudioStreamWindow
    {
    public:
        /** @brief 한 번에 푸는 프레임 수입니다(48 kHz 에서 약 43 ms). */
        static constexpr uint32 kChunkFrames = 2048;
        /** @brief 창을 이어 붙일 때 남기는 마지막 프레임 수입니다. */
        static constexpr uint32 kRetainFrames = 2;

        AudioStreamWindow();

        /** @brief 소스의 디코더를 열어 창을 비웁니다. 디코더를 열지 못하면 false 입니다. */
        [[nodiscard]] bool open( const AudioStreamSource& source );
        /**
         * @brief 이미 만든 디코더(@p channelCount 채널 교차 출력)로 창을 엽니다. 한 번에 @p chunkFrames 프레임씩 풉니다.
         * @details 소리 그래프(`AudioGraphInstance`)가 이 길로 보이스의 창이 된다. 그래프는 게임 파라미터를 블록마다 받으므로 덩어리를 블록 길이로 줄여 늦음을 한 블록 안에 둔다.
         * @return 디코더가 nullptr 이거나 채널이 1 · 2 가 아니거나 덩어리가 0 이면 false 입니다.
         */
        [[nodiscard]] bool openDecoder( unique_ptr<IAudioStreamDecoder> pDecoder, uint16 channelCount, uint32 chunkFrames );
        /** @brief 디코더를 놓고 창을 비웁니다. */
        void close();
        /** @brief 열려 있는지입니다. */
        bool isOpen() const { return _pDecoder != nullptr; }

        /**
         * @brief 클립의 @p frame 번째 프레임을 읽습니다(모노는 두 값이 같고, 3 채널 이상은 다운믹스한 값입니다).
         * @return 스트림 끝을 넘었거나 디코드할 수 없으면 false 이고 출력은 0 입니다.
         */
        [[nodiscard]] bool readFrame( uint32 frame, float32& outLeft, float32& outRight );

        /** @brief 지금까지 되감은 횟수입니다(진단 · 테스트). */
        uint32 getSeekCount() const { return _seekCount; }
        /** @brief 지금까지 푼 프레임 수입니다(진단 · 테스트). */
        uint64 getDecodedFrameCount() const { return _decodedFrameCount; }

    private:
        /** @brief 창 뒤에 한 덩어리를 더 풉니다. @p retainFrames 만큼은 창 앞에서 남겨 옮깁니다. 하나도 못 풀면 false 입니다. */
        bool refill( uint32 retainFrames );

    private:
        shared_ptr<IAudioStreamDecoder> _pDecoder{};
        vector<float32>                 _listWindow{}; ///< 푼 프레임(채널 교차)입니다. 첫 프레임이 `_windowStart` 입니다.
        AudioDownmix                    _downmix{};    ///< 소스의 다운믹스 행렬 복사본입니다(3 채널 이상).
        uint64                          _decodedFrameCount{ 0 };
        uint32                          _windowStart{ 0 };
        uint32                          _windowFrames{ 0 };
        uint32                          _seekCount{ 0 };
        uint32                          _chunkFrames{ kChunkFrames }; ///< 한 번에 푸는 프레임 수다(파일 스트림은 `kChunkFrames`, 소리 그래프는 블록 길이).
        uint16                          _channelCount{ 0 };
    };
} // namespace sw
