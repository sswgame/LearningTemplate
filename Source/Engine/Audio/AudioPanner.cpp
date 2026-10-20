#include "pch.h"

#include "Engine/Audio/AudioPanner.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct AudioPannerInternal
        {
            /** @brief 링 위의 스피커 하나입니다. 방위는 앞 0, 오른쪽 +(도)입니다. */
            struct Speaker
            {
                uint32  _channelIndex;
                float32 _azimuthDegrees;
            };

            /** @brief 5.1(ITU-R BS.775) 스피커를 방위 오름차순으로 둡니다. LFE 는 링에 없습니다. */
            static constexpr Speaker kArr51Ring[] = {
                {4, -110.0f},
                {0,  -30.0f},
                {2,    0.0f},
                {1,   30.0f},
                {5,  110.0f},
            };
            /** @brief 7.1(ITU-R BS.775, 옆 ±90 · 뒤 ±150) 스피커를 방위 오름차순으로 둡니다. */
            static constexpr Speaker kArr71Ring[] = {
                {4, -150.0f},
                {6,  -90.0f},
                {0,  -30.0f},
                {2,    0.0f},
                {1,   30.0f},
                {7,   90.0f},
                {5,  150.0f},
            };

            /** @brief 등전력 스테레오 법칙을 앞 두 채널에 씁니다. */
            static void computeFrontStereo( float32 pan, float32 ( &outArrGain )[audio::kMaxChannelCount] )
            {
                // 등전력: θ = (pan + 1)·π/4, L = cos θ, R = sin θ.
                const float32 theta = ( MathUtil::clamp( pan, -1.0f, 1.0f ) + 1.0f ) * MathUtil::kPi * 0.25f;
                outArrGain[0]       = MathUtil::cos( theta );
                outArrGain[1]       = MathUtil::sin( theta );
            }

            /** @brief 방위 @p azimuth(라디안)가 놓인 두 스피커 사이를 등전력으로 나눕니다(점 음원). */
            template <size_t TCount>
            static void computeRing( const Speaker ( &arrRing )[TCount], float32 azimuth, float32 ( &outArrGain )[audio::kMaxChannelCount] )
            {
                // 마지막 스피커에서 첫 스피커로 넘어가는 쌍(뒤쪽)이 기본이고, 그 사이에 있으면 안쪽 쌍을 고른다.
                size_t  lowerIndex = TCount - 1;
                size_t  upperIndex = 0;
                float32 lower      = arrRing[TCount - 1]._azimuthDegrees * MathUtil::kDegreeToRadian;
                float32 span       = arrRing[0]._azimuthDegrees * MathUtil::kDegreeToRadian + MathUtil::kTwoPi - lower;
                for ( size_t ringIndex = 0; ringIndex + 1 < TCount; ++ringIndex )
                {
                    const float32 from        = arrRing[ringIndex]._azimuthDegrees * MathUtil::kDegreeToRadian;
                    const float32 to          = arrRing[ringIndex + 1]._azimuthDegrees * MathUtil::kDegreeToRadian;
                    const bool    bInsidePair = from <= azimuth && azimuth < to;
                    if ( bInsidePair )
                    {
                        lowerIndex = ringIndex;
                        upperIndex = ringIndex + 1;
                        lower      = from;
                        span       = to - from;
                        break;
                    }
                }
                float32 offset = azimuth - lower;
                if ( offset < 0.0f )
                    offset += MathUtil::kTwoPi;
                const float32 t                               = MathUtil::saturate( offset / span );
                outArrGain[arrRing[lowerIndex]._channelIndex] = MathUtil::cos( t * MathUtil::kHalfPi );
                outArrGain[arrRing[upperIndex]._channelIndex] = MathUtil::sin( t * MathUtil::kHalfPi );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AudioPanDirection AudioPanDirection::makeStereoPan( float32 pan )
    {
        AudioPanDirection direction;
        direction._right = MathUtil::clamp( pan, -1.0f, 1.0f );
        direction._front = 0.0f;
        direction._mode  = AudioPanMode::FrontStereo;
        return direction;
    }

    void AudioPanDirection::addPan( float32 pan )
    {
        _right                 = MathUtil::clamp( _right + pan, -1.0f, 1.0f );
        const float32 frontMax = MathUtil::sqrt( MathUtil::max( 0.0f, 1.0f - _right * _right ) );
        _front                 = MathUtil::clamp( _front, -frontMax, frontMax );
    }

    void AudioPanner::computeGains( AudioChannelLayout layout, const AudioPanDirection& direction, float32 ( &outArrGain )[audio::kMaxChannelCount] )
    {
        for ( float32& gain : outArrGain )
        {
            gain = 0.0f;
        }
        // 바이노럴의 3D 모노 소리는 보이스의 HRTF 상태(`AudioBinauralFilter`)가 낸다. 이 상태 없는 패너는 두 채널 레이아웃을 모두 스테레오 법칙으로 답한다.
        const bool bTwoChannel  = AudioChannelLayoutUtil::getChannelCount( layout ) == audio::kStereoChannelCount;
        const bool bFrontStereo = bTwoChannel || direction._mode == AudioPanMode::FrontStereo;
        if ( bFrontStereo )
        {
            AudioPannerInternal::computeFrontStereo( direction._right, outArrGain );
            return;
        }

        float32       arrPoint[audio::kMaxChannelCount]{};
        const float32 azimuth = MathUtil::atan2( direction._right, direction._front );
        switch ( layout )
        {
            case AudioChannelLayout::Stereo:
            case AudioChannelLayout::Binaural:
            {
                break;
            }
            case AudioChannelLayout::Surround51:
            {
                AudioPannerInternal::computeRing( AudioPannerInternal::kArr51Ring, azimuth, arrPoint );
                break;
            }
            case AudioChannelLayout::Surround71:
            {
                AudioPannerInternal::computeRing( AudioPannerInternal::kArr71Ring, azimuth, arrPoint );
                break;
            }
        }

        // 퍼짐: 점 게인과 균등 게인을 전력으로 섞는다. 합 전력은 L² + (1 − L²) = 1 이다.
        const float32 focusPower   = MathUtil::saturate( direction._right * direction._right + direction._front * direction._front );
        const uint32  channelCount = AudioChannelLayoutUtil::getChannelCount( layout );
        const float32 uniformPower = ( 1.0f - focusPower ) / static_cast<float32>( AudioChannelLayoutUtil::getFullRangeChannelCount( layout ) );
        for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
        {
            const bool bLfe = AudioChannelLayoutUtil::hasLfe( layout ) && channelIndex == audio::kLfeChannelIndex;
            if ( bLfe )
                continue;
            outArrGain[channelIndex] = MathUtil::sqrt( focusPower * arrPoint[channelIndex] * arrPoint[channelIndex] + uniformPower );
        }
    }
} // namespace sw
