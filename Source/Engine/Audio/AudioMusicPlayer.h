/**
 * @file AudioMusicPlayer.h
 * @brief 적응형 음악(`AudioMusicDesc`: 구간 · 레이어 · 전환)을 재생하는 플레이어와, 플레이어가 보이스 풀에 하는 일의 창(`IAudioMusicVoices`)을 선언합니다.
 * @details 플레이어는 재생 위치와 전환 예약만 들고, 소리는 엔진의 보이스로 낸다. 보이스 슬롯의 생김새는 모르고 창의 다섯 일(시작 · 페이드 · 놓기 · 레이어 게인 · 시각)만 쓴다.
 *          `AudioEngine` 이 하나를 들고 오디오 스레드에서만 부른다. Wwise Interactive Music · FMOD 의 전환 타임라인, Godot `AudioStreamInteractive` 처럼
 *          음악의 박 · 마디 맞춤을 일반 이벤트 재생과 따로 두는 나눔이다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Audio/AudioEngineState.h"
#include "Engine/Audio/AudioTypes.h"

namespace sw
{
    struct AudioMusicDesc;

    class AudioMusicPlayer;
} // namespace sw

namespace sw
{
    /** @brief 음악 레이어 · 스팅어 보이스 하나를 시작하는 요청입니다. 클립이 늦게 와도 `_startFrame` 에 맞춰 소리를 낸다. */
    struct AudioMusicVoiceRequest
    {
        hashed_string  _clipPath{};
        hashed_string  _bus{};
        AudioPlayingID _playingID{ 0 };
        uint64         _startFrame{ 0 }; ///< 소리가 시작할 렌더 프레임이다.
        int32          _segment{ -1 };   ///< 구간 번호다(스팅어는 `AudioMusicPlayer::kStingerSegment`).
        int32          _layer{ -1 };     ///< 레이어 번호다(-1 = 스팅어, 게인을 따라가지 않는다).
        float32        _gain{ 1.0f };    ///< 시작 레이어 게인(선형)
        float32        _fadeInSeconds{ 0.0f };
        bool           _bLoop{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IAudioMusicVoices
     * @brief 음악 플레이어가 보이스 풀에 하는 일입니다. `AudioEngine` 이 구현합니다(오디오 스레드).
     */
    class IAudioMusicVoices
    {
    public:
        virtual ~IAudioMusicVoices() = default;

        /** @brief 지금까지 렌더한 프레임입니다(블록 시작 시각). */
        virtual uint64 getMusicClock() const = 0;
        /** @brief 게임 파라미터의 현재 값입니다(레이어 게인 곡선). 모르는 파라미터면 0 입니다. */
        virtual float32 findMusicParameter( const hashed_string& name ) const = 0;
        /** @brief 보이스 하나를 잡아 클립을 기다리게 합니다. 풀이 비었으면 false 입니다. */
        [[nodiscard]] virtual bool startMusicVoice( const AudioMusicVoiceRequest& request ) = 0;
        /**
         * @brief 재생 @p playingID 의 보이스 중 구간 @p segment(`AudioMusicPlayer::kAnySegment` 면 모두)를 @p delayFrames 뒤부터 @p fadeFrames 동안 줄여 멈춥니다.
         * @details 클립을 아직 기다리는 보이스는 소리를 낸 적이 없으므로 바로 놓는다.
         */
        virtual void fadeMusicVoices( AudioPlayingID playingID, int32 segment, uint32 fadeFrames, uint32 delayFrames ) = 0;
        /** @brief 재생 @p playingID 의 보이스 중 구간 @p segment(`kAnySegment` 면 모두)를 바로 놓습니다. */
        virtual void freeMusicVoices( AudioPlayingID playingID, int32 segment ) = 0;
        /** @brief 재생 @p playingID 의 레이어 보이스마다 게인을 @p player 의 목표(`computeLayerGain`) 쪽으로 최대 @p maxStep 만큼 옮깁니다. */
        virtual void stepMusicLayerGains( AudioPlayingID playingID, float32 maxStep, const AudioMusicPlayer& player ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AudioMusicPlayer
     * @brief 적응형 음악 하나의 재생 위치 · 구간 전환 예약 · 세로 레이어 게인을 맡습니다.
     */
    class SW_API AudioMusicPlayer
    {
    public:
        /** @brief 스팅어 보이스의 구간 표시입니다. 레이어가 아니라 구간이 바뀌어도 페이드하지 않습니다. */
        static constexpr int32 kStingerSegment = -2;
        /** @brief `IAudioMusicVoices` 의 페이드 · 놓기가 구간을 가리지 않음을 뜻합니다. */
        static constexpr int32 kAnySegment = -3;

        /** @brief 음악을 시작합니다. 재생 중인 음악은 @p fadeSeconds 동안 줄이고, 지금 블록에서 시작 구간을 재생합니다. */
        void start( IAudioMusicVoices& voices, shared_ptr<const AudioMusicDesc> pMusic, AudioPlayingID playingID, float32 fadeSeconds );
        /** @brief 구간 전환을 전환 규칙의 맞춤 지점(박, 마디, 구간 끝)에 예약하고 스팅어를 냅니다. 같은 구간이면 아무것도 하지 않습니다. */
        void setSegment( IAudioMusicVoices& voices, const hashed_string& segment ) { scheduleTransition( voices, segment, false ); }
        /** @brief 음악 보이스를 @p fadeSeconds 로 멈춥니다. */
        void stop( IAudioMusicVoices& voices, float32 fadeSeconds );
        /** @brief 예약한 전환 시각이 되면 구간을 넘깁니다. 루프하지 않는 구간의 끝은 다음 구간으로 연결하고, 레이어 게인을 파라미터에 맞춥니다. */
        void update( IAudioMusicVoices& voices );
        /** @brief 보이스를 건드리지 않고 상태만 비웁니다(엔진 종료 — 보이스는 엔진이 함께 지운다). */
        void reset();

        /** @brief @p renderedFrameCount 시각의 음악 위치입니다(게시용). */
        AudioMusicStatus makeStatus( uint64 renderedFrameCount ) const { return _playback.makeStatus( renderedFrameCount ); }
        /** @brief 레이어의 목표 게인(선형)을 반환합니다. 볼륨과 파라미터 곡선을 합친 값입니다. */
        float32 computeLayerGain( const IAudioMusicVoices& voices, int32 segmentIndex, int32 layerIndex ) const;

    private:
        /** @brief 구간 전환을 맞춤 지점에 예약합니다. @p bAtSegmentEnd 가 true 면 규칙과 상관없이 구간 끝에서 전환하며, 루프하지 않는 구간의 다음 구간에 쓰입니다. */
        void scheduleTransition( IAudioMusicVoices& voices, const hashed_string& segment, bool bAtSegmentEnd );
        /** @brief 현재 구간에서 @p sync 의 다음 맞춤 지점(렌더 프레임)을 반환합니다. */
        uint64 computeSyncFrame( uint64 now, AudioMusicSync sync ) const;
        /** @brief 구간의 레이어 보이스를 @p startFrame 에 시작하도록 만듭니다. */
        void scheduleSegment( IAudioMusicVoices& voices, int32 segmentIndex, uint64 startFrame, float32 fadeInSeconds );
        /** @brief 음악이 내는 버스입니다. 설명자가 비워 두면 `music` 입니다. */
        hashed_string getBus() const;

    private:
        AudioMusicPlayback _playback; /**< 적응형 음악의 재생 위치와 전환 예약입니다. */
    };
} // namespace sw
