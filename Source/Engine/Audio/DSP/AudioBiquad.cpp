#include "pch.h"

#include "Engine/Audio/DSP/AudioBiquad.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    AudioBiquadCoefficients AudioBiquadCoefficients::make( AudioBiquadType type, float32 frequencyHz, float32 q, float32 gainDb, float32 sampleRate )
    {
        const float64 nyquist   = static_cast<float64>( sampleRate ) * 0.5;
        const float64 frequency = MathUtil::clamp( static_cast<float64>( frequencyHz ), 1.0, nyquist * 0.999 );
        const float64 safeQ     = MathUtil::max( static_cast<float64>( q ), 0.01 );
        const float64 omega     = 2.0 * MathUtil::kPi64 * frequency / static_cast<float64>( sampleRate );
        const float64 sinOmega  = std::sin( omega );
        const float64 cosOmega  = std::cos( omega );
        const float64 alpha     = sinOmega / ( 2.0 * safeQ );
        const float64 amplitude = std::pow( 10.0, static_cast<float64>( gainDb ) / 40.0 );

        float64 b0 = 1.0;
        float64 b1 = 0.0;
        float64 b2 = 0.0;
        float64 a0 = 1.0;
        float64 a1 = 0.0;
        float64 a2 = 0.0;
        switch ( type )
        {
            case AudioBiquadType::LowPass:
            {
                b0 = ( 1.0 - cosOmega ) * 0.5;
                b1 = 1.0 - cosOmega;
                b2 = ( 1.0 - cosOmega ) * 0.5;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosOmega;
                a2 = 1.0 - alpha;
                break;
            }
            case AudioBiquadType::HighPass:
            {
                b0 = ( 1.0 + cosOmega ) * 0.5;
                b1 = -( 1.0 + cosOmega );
                b2 = ( 1.0 + cosOmega ) * 0.5;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosOmega;
                a2 = 1.0 - alpha;
                break;
            }
            case AudioBiquadType::BandPass:
            {
                b0 = alpha;
                b1 = 0.0;
                b2 = -alpha;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosOmega;
                a2 = 1.0 - alpha;
                break;
            }
            case AudioBiquadType::Peaking:
            {
                b0 = 1.0 + alpha * amplitude;
                b1 = -2.0 * cosOmega;
                b2 = 1.0 - alpha * amplitude;
                a0 = 1.0 + alpha / amplitude;
                a1 = -2.0 * cosOmega;
                a2 = 1.0 - alpha / amplitude;
                break;
            }
            case AudioBiquadType::LowShelf:
            {
                const float64 twoSqrtAAlpha = 2.0 * std::sqrt( amplitude ) * alpha;
                b0                          = amplitude * ( ( amplitude + 1.0 ) - ( amplitude - 1.0 ) * cosOmega + twoSqrtAAlpha );
                b1                          = 2.0 * amplitude * ( ( amplitude - 1.0 ) - ( amplitude + 1.0 ) * cosOmega );
                b2                          = amplitude * ( ( amplitude + 1.0 ) - ( amplitude - 1.0 ) * cosOmega - twoSqrtAAlpha );
                a0                          = ( amplitude + 1.0 ) + ( amplitude - 1.0 ) * cosOmega + twoSqrtAAlpha;
                a1                          = -2.0 * ( ( amplitude - 1.0 ) + ( amplitude + 1.0 ) * cosOmega );
                a2                          = ( amplitude + 1.0 ) + ( amplitude - 1.0 ) * cosOmega - twoSqrtAAlpha;
                break;
            }
            case AudioBiquadType::HighShelf:
            {
                const float64 twoSqrtAAlpha = 2.0 * std::sqrt( amplitude ) * alpha;
                b0                          = amplitude * ( ( amplitude + 1.0 ) + ( amplitude - 1.0 ) * cosOmega + twoSqrtAAlpha );
                b1                          = -2.0 * amplitude * ( ( amplitude - 1.0 ) + ( amplitude + 1.0 ) * cosOmega );
                b2                          = amplitude * ( ( amplitude + 1.0 ) + ( amplitude - 1.0 ) * cosOmega - twoSqrtAAlpha );
                a0                          = ( amplitude + 1.0 ) - ( amplitude - 1.0 ) * cosOmega + twoSqrtAAlpha;
                a1                          = 2.0 * ( ( amplitude - 1.0 ) - ( amplitude + 1.0 ) * cosOmega );
                a2                          = ( amplitude + 1.0 ) - ( amplitude - 1.0 ) * cosOmega - twoSqrtAAlpha;
                break;
            }
        }

        AudioBiquadCoefficients coefficients;
        coefficients._b0 = static_cast<float32>( b0 / a0 );
        coefficients._b1 = static_cast<float32>( b1 / a0 );
        coefficients._b2 = static_cast<float32>( b2 / a0 );
        coefficients._a1 = static_cast<float32>( a1 / a0 );
        coefficients._a2 = static_cast<float32>( a2 / a0 );
        return coefficients;
    }

    float32 AudioBiquadCoefficients::computeMagnitude( float32 frequencyHz, float32 sampleRate ) const
    {
        // H(z) = (b0 + b1 z⁻¹ + b2 z⁻²) / (1 + a1 z⁻¹ + a2 z⁻²), z = e^{jω}.
        const float64 omega         = 2.0 * MathUtil::kPi64 * static_cast<float64>( frequencyHz ) / static_cast<float64>( sampleRate );
        const float64 cos1          = std::cos( omega );
        const float64 sin1          = std::sin( omega );
        const float64 cos2          = std::cos( 2.0 * omega );
        const float64 sin2          = std::sin( 2.0 * omega );
        const float64 numeratorReal = static_cast<float64>( _b0 ) + static_cast<float64>( _b1 ) * cos1 + static_cast<float64>( _b2 ) * cos2;
        const float64 numeratorImag = -( static_cast<float64>( _b1 ) * sin1 + static_cast<float64>( _b2 ) * sin2 );
        const float64 denomReal     = 1.0 + static_cast<float64>( _a1 ) * cos1 + static_cast<float64>( _a2 ) * cos2;
        const float64 denomImag     = -( static_cast<float64>( _a1 ) * sin1 + static_cast<float64>( _a2 ) * sin2 );
        const float64 numerator     = std::sqrt( numeratorReal * numeratorReal + numeratorImag * numeratorImag );
        const float64 denominator   = std::sqrt( denomReal * denomReal + denomImag * denomImag );
        return denominator <= 0.0 ? 0.0f : static_cast<float32>( numerator / denominator );
    }

    void AudioBiquadStereo::reset()
    {
        _arrState1[0] = 0.0f;
        _arrState1[1] = 0.0f;
        _arrState2[0] = 0.0f;
        _arrState2[1] = 0.0f;
    }

    void AudioBiquadStereo::process( float32* pInterleaved, uint32 frameCount )
    {
        const AudioBiquadCoefficients coefficients = _coefficients;
        for ( uint32 channel = 0; channel < 2; ++channel )
        {
            float32 state1 = _arrState1[channel];
            float32 state2 = _arrState2[channel];
            for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
            {
                float32&      sample = pInterleaved[frameIndex * 2 + channel];
                const float32 input  = sample;
                const float32 output = coefficients._b0 * input + state1;
                state1               = coefficients._b1 * input - coefficients._a1 * output + state2;
                state2               = coefficients._b2 * input - coefficients._a2 * output;
                sample               = output;
            }
            // 아주 작은 값(디노멀)이 남아 CPU 를 잡아먹지 않게 0 으로 민다.
            _arrState1[channel] = MathUtil::abs( state1 ) < 1e-20f ? 0.0f : state1;
            _arrState2[channel] = MathUtil::abs( state2 ) < 1e-20f ? 0.0f : state2;
        }
    }
} // namespace sw
