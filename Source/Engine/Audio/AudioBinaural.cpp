#include "pch.h"

#include "Engine/Audio/AudioBinaural.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Audio/AudioSpatial.h"
#include "Engine/Audio/AudioTypes.h"

namespace sw
{
    namespace
    {
        struct AudioBinauralInternal
        {
            /** @brief 구 머리 모델의 머리 반지름(m)입니다(Brown · Duda 의 평균 머리). */
            static constexpr float32 kHeadRadiusMeters = 0.0875f;
            /** @brief 머리 그림자의 가장 작은 α 와 그 각(150°)입니다. */
            static constexpr float32 kMinAlpha             = 0.1f;
            static constexpr float32 kMinAlphaAngleRadians = 150.0f * MathUtil::kDegreeToRadian;

            /** @brief 두 각 사이(0 ~ π)의 차이입니다. */
            static float32 computeAngleBetween( float32 fromRadians, float32 toRadians )
            {
                float32 difference = MathUtil::abs( fromRadians - toRadians );
                if ( difference > MathUtil::kPi )
                    difference = MathUtil::kTwoPi - difference;
                return difference;
            }

            /** @brief 지연선에서 @p delayFrames 앞의 값을 선형 보간으로 읽습니다. */
            static float32 readDelayed( const float32 ( &arrHistory )[AudioBinauralFilter::kDelayLineLength], uint32 writeIndex, float32 delayFrames )
            {
                constexpr uint32 kMask    = AudioBinauralFilter::kDelayLineLength - 1;
                const uint32     whole    = static_cast<uint32>( delayFrames );
                const float32    fraction = delayFrames - static_cast<float32>( whole );
                const float32    newer    = arrHistory[( writeIndex - whole ) & kMask];
                const float32    older    = arrHistory[( writeIndex - whole - 1 ) & kMask];
                return newer + ( older - newer ) * fraction;
            }

            /** @brief 귀 하나의 지연 · 필터를 한 프레임 지나게 합니다. */
            static float32 processEar( AudioBinauralFilter::Ear& ear, float32 delayed )
            {
                const float32 output = ear._b0 * delayed + ear._b1 * ear._x1 - ear._a1 * ear._y1;
                ear._x1              = delayed;
                ear._y1              = MathUtil::abs( output ) < 1e-20f ? 0.0f : output;
                return output;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void AudioBinauralFilter::reset()
    {
        *this = AudioBinauralFilter{};
    }

    float32 AudioBinauralFilter::computeDelayFrames( float32 incidenceRadians )
    {
        const float32 radiusSeconds = AudioBinauralInternal::kHeadRadiusMeters / AudioSpatializer::kSpeedOfSound;
        const float32 angle         = MathUtil::clamp( incidenceRadians, 0.0f, MathUtil::kPi );
        const float32 delaySeconds  = angle < MathUtil::kHalfPi ? radiusSeconds * ( 1.0f - MathUtil::cos( angle ) ) : radiusSeconds * ( 1.0f + angle - MathUtil::kHalfPi );
        return delaySeconds * static_cast<float32>( audio::kSampleRate );
    }

    void AudioBinauralFilter::computeShadow( float32 incidenceRadians, Ear& outEar )
    {
        using Internal      = AudioBinauralInternal;
        const float32 angle = MathUtil::clamp( incidenceRadians, 0.0f, MathUtil::kPi );
        const float32 alpha = ( 1.0f + Internal::kMinAlpha * 0.5f ) + ( 1.0f - Internal::kMinAlpha * 0.5f ) * MathUtil::cos( angle / Internal::kMinAlphaAngleRadians * MathUtil::kPi );
        // H(s) = (β + α·s) / (β + s), β = 2ω₀ = 2c/a 를 쌍선형 변환(s = K(1 − z⁻¹)/(1 + z⁻¹), K = 2·fs)한다.
        const float32 beta        = 2.0f * AudioSpatializer::kSpeedOfSound / Internal::kHeadRadiusMeters;
        const float32 bilinear    = 2.0f * static_cast<float32>( audio::kSampleRate );
        const float32 denominator = beta + bilinear;
        outEar._b0                = ( beta + alpha * bilinear ) / denominator;
        outEar._b1                = ( beta - alpha * bilinear ) / denominator;
        outEar._a1                = ( beta - bilinear ) / denominator;
    }

    void AudioBinauralFilter::setDirection( const AudioPanDirection& direction )
    {
        // 방위: 앞 0, 오른쪽 +π/2. 오른쪽 귀 축은 +π/2, 왼쪽 귀 축은 −π/2 다.
        const float32 azimuth     = MathUtil::atan2( direction._right, direction._front );
        const float32 leftAngle   = AudioBinauralInternal::computeAngleBetween( azimuth, -MathUtil::kHalfPi );
        const float32 rightAngle  = AudioBinauralInternal::computeAngleBetween( azimuth, MathUtil::kHalfPi );
        _left._targetDelayFrames  = computeDelayFrames( leftAngle );
        _right._targetDelayFrames = computeDelayFrames( rightAngle );
        computeShadow( leftAngle, _left );
        computeShadow( rightAngle, _right );
        _focus = MathUtil::saturate( MathUtil::sqrt( direction._right * direction._right + direction._front * direction._front ) );
        if ( _bPrimed == false )
        {
            _left._delayFrames  = _left._targetDelayFrames;
            _right._delayFrames = _right._targetDelayFrames;
            _bPrimed            = true;
        }
    }

    void AudioBinauralFilter::process( const float32* pSource, uint32 frameCount, float32* pOutLeft, float32* pOutRight )
    {
        using Internal              = AudioBinauralInternal;
        constexpr uint32 kMask      = kDelayLineLength - 1;
        const float32    invFrames  = frameCount > 0 ? 1.0f / static_cast<float32>( frameCount ) : 0.0f;
        const float32    leftStep   = ( _left._targetDelayFrames - _left._delayFrames ) * invFrames;
        const float32    rightStep  = ( _right._targetDelayFrames - _right._delayFrames ) * invFrames;
        const float32    dryWeight  = ( 1.0f - _focus ) * MathUtil::kInvSqrt2;
        float32          leftDelay  = _left._delayFrames;
        float32          rightDelay = _right._delayFrames;
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            const float32 input      = pSource[frameIndex];
            _arrHistory[_writeIndex] = input;
            leftDelay += leftStep;
            rightDelay += rightStep;
            const float32 leftEar  = Internal::processEar( _left, Internal::readDelayed( _arrHistory, _writeIndex, leftDelay ) );
            const float32 rightEar = Internal::processEar( _right, Internal::readDelayed( _arrHistory, _writeIndex, rightDelay ) );
            pOutLeft[frameIndex]   = leftEar * _focus + input * dryWeight;
            pOutRight[frameIndex]  = rightEar * _focus + input * dryWeight;
            _writeIndex            = ( _writeIndex + 1 ) & kMask;
        }
        _left._delayFrames  = _left._targetDelayFrames;
        _right._delayFrames = _right._targetDelayFrames;
    }
} // namespace sw
