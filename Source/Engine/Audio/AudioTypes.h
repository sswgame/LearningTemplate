/**
 * @file AudioTypes.h
 * @brief 오디오 엔진이 함께 쓰는 낱말 — 출력 형식 상수 · 식별자 · dB 변환 · 결정적 난수입니다.
 * @details 믹서는 **고정 형식** 하나로 돕니다: 48 kHz · 스테레오 · float32 교차(interleaved). 클립은 읽을 때 float 로 풀고, 재생할 때 피치와 함께
 *          선형 보간으로 리샘플합니다. 블록(`kAudioBlockFrameCount`) 단위로 명령 · 파라미터 · 공간화 · 보이스 제한을 갱신하고, 블록 안에서는 샘플마다
 *          게인을 램프합니다(지퍼 잡음 없음).
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    /** @brief 오디오 엔진의 고정 값입니다. */
    namespace audio
    {
        /** @brief 믹서 출력 샘플레이트(Hz)입니다. 장치도 이 형식으로 엽니다. */
        inline constexpr uint32 kSampleRate = 48000;
        /** @brief 믹서 출력 채널 수입니다(스테레오). */
        inline constexpr uint32 kChannelCount = 2;
        /** @brief 믹서가 한 번에 처리하는 프레임 수입니다(5.3 ms). 파라미터 · 공간화는 블록마다 갱신됩니다. */
        inline constexpr uint32 kBlockFrameCount = 256;
        /** @brief 블록 하나의 길이(초)입니다. 이벤트 · 음악 페이드가 블록마다 이만큼 나아갑니다. */
        inline constexpr float32 kBlockSeconds = static_cast<float32>( kBlockFrameCount ) / static_cast<float32>( kSampleRate );
        /** @brief "들리지 않음" 으로 보는 dB 바닥입니다. 이 아래는 0 으로 다룹니다. */
        inline constexpr float32 kSilenceDb = -96.0f;
        /** @brief 필터 컷오프의 위 끝(Hz)입니다. 이 이상이면 필터를 건너뜁니다. */
        inline constexpr float32 kFilterOpenHz = 20000.0f;
    } // namespace audio
} // namespace sw

namespace sw
{
    /** @brief 재생 하나(이벤트 인스턴스 · 클립 재생)의 id 입니다. 0 은 없음이고, 다시 쓰지 않습니다. */
    using AudioPlayingId = uint64;
    /** @brief 소리를 내는 자리(게임 오브젝트)의 id 입니다. 0 은 "자리 없음 — 2D(공간화 없음)" 입니다. */
    using AudioEmitterId = uint64;
} // namespace sw

namespace sw
{
    /**
     * @struct AudioMath
     * @brief dB · 선형 게인 · 반음 변환입니다. dB 바닥(`audio::kSilenceDb`) 아래는 게인 0 입니다.
     */
    struct AudioMath
    {
        /** @brief dB 를 선형 게인으로 바꿉니다. 바닥 이하는 0 입니다. */
        static SW_INLINE float32 dbToLinear( float32 decibel )
        {
            if ( decibel <= audio::kSilenceDb )
                return 0.0f;
            return MathUtil::pow( 10.0f, decibel / 20.0f );
        }

        /** @brief 선형 게인을 dB 로 바꿉니다. 0 이하는 바닥 값입니다. */
        static SW_INLINE float32 linearToDb( float32 gain )
        {
            if ( gain <= 0.0f )
                return audio::kSilenceDb;
            // 20·log10(g) = (20 / ln 10)·ln(g)
            constexpr float32 kDbPerNeper = 8.68588964f;
            const float32     decibel     = kDbPerNeper * MathUtil::log( gain );
            return decibel < audio::kSilenceDb ? audio::kSilenceDb : decibel;
        }

        /** @brief 반음 수를 재생 속도 비로 바꿉니다(12 반음 = 2 배). */
        static SW_INLINE float32 semitonesToRatio( float32 semitones ) { return MathUtil::pow( 2.0f, semitones / 12.0f ); }

        /** @brief 두 주파수 사이를 로그 축에서 보간합니다(필터 컷오프 블렌드용). @p t 는 [0, 1] 로 묶습니다. */
        static SW_INLINE float32 lerpFrequency( float32 fromHz, float32 toHz, float32 t )
        {
            const float32 clamped  = MathUtil::saturate( t );
            const float32 safeFrom = fromHz < 1.0f ? 1.0f : fromHz;
            const float32 safeTo   = toHz < 1.0f ? 1.0f : toHz;
            return safeFrom * MathUtil::pow( safeTo / safeFrom, clamped );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AudioRandom
     * @brief 씨앗으로 정해지는 난수(SplitMix64)입니다. 컨테이너의 클립 고르기 · 볼륨 · 피치 범위가 씁니다.
     * @details 오프라인 렌더 시험이 같은 출력을 내도록 엔진이 씨앗을 듭니다(`AudioEngine::setRandomSeed`). 전역 난수(`MathUtil::getRandom`)는
     *          스레드마다 씨앗이 달라 쓰지 않습니다.
     */
    struct AudioRandom
    {
        uint64 _state{ HashUtil::kGoldenRatio64 };

        /** @brief 다음 64 비트 값입니다. */
        uint64 nextUint64()
        {
            _state += HashUtil::kGoldenRatio64;
            uint64 mixed = _state;
            return HashUtil::mix64( mixed );
        }

        /** @brief [0, 1) 의 값입니다. */
        float32 nextUnit() { return static_cast<float32>( nextUint64() >> 40 ) / static_cast<float32>( 1ull << 24 ); }

        /** @brief [lowValue, highValue] 의 값입니다. 둘이 같으면 그 값입니다. */
        float32 nextRange( float32 lowValue, float32 highValue ) { return lowValue + ( highValue - lowValue ) * nextUnit(); }
    };
} // namespace sw

namespace sw
{
    /** @brief 엔진이 아는 버스 이름입니다. 기본 그래프가 이 이름들을 듭니다. */
    namespace AudioBusNames
    {
        inline constexpr utf8 kMaster[]  = "master";
        inline constexpr utf8 kMusic[]   = "music";
        inline constexpr utf8 kSfx[]     = "sfx";
        inline constexpr utf8 kVoice[]   = "voice";
        inline constexpr utf8 kAmbient[] = "ambient";
        inline constexpr utf8 kUi[]      = "ui";
        inline constexpr utf8 kReverb[]  = "reverb";
    } // namespace AudioBusNames
} // namespace sw
