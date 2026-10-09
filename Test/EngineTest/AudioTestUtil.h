/**
 * @file AudioTestUtil.h
 * @brief 오디오 시험의 합성 클립(직류 · 사인 · 임펄스)과 장치 없는 렌더 · 측정(RMS · 피크 · 한 주파수의 진폭)입니다.
 */
#pragma once
#include "Core/Math/MathUtil.h"

#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioTypes.h"

namespace test
{
    /** @brief 오디오 시험 도우미입니다. */
    struct AudioTestUtil
    {
        /** @brief 모든 샘플이 @p value 인 클립(직류)입니다. */
        static sw::shared_ptr<const sw::AudioClipData> makeConstantClip( float32 value, uint32 frameCount, uint16 channelCount = 2, uint32 sampleRate = sw::audio::kSampleRate )
        {
            sw::shared_ptr<sw::AudioClipData> pClip = sw::make_shared<sw::AudioClipData>();
            pClip->_channelCount                    = channelCount;
            pClip->_sampleRate                      = sampleRate;
            pClip->_frameCount                      = frameCount;
            pClip->_listSample.assign( static_cast<size_t>( frameCount ) * channelCount, value );
            return pClip;
        }

        /** @brief 진폭 @p amplitude · 주파수 @p frequencyHz 의 사인 클립(모노)입니다. */
        static sw::shared_ptr<const sw::AudioClipData> makeSineClip( float32 frequencyHz, float32 amplitude, uint32 frameCount, uint32 sampleRate = sw::audio::kSampleRate )
        {
            sw::shared_ptr<sw::AudioClipData> pClip = sw::make_shared<sw::AudioClipData>();
            pClip->_channelCount                    = 1;
            pClip->_sampleRate                      = sampleRate;
            pClip->_frameCount                      = frameCount;
            pClip->_listSample.resize( frameCount );
            for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
            {
                const float64 phase            = 2.0 * 3.14159265358979323846 * static_cast<float64>( frequencyHz ) * static_cast<float64>( frameIndex ) / static_cast<float64>( sampleRate );
                pClip->_listSample[frameIndex] = amplitude * static_cast<float32>( std::sin( phase ) );
            }
            return pClip;
        }

        /** @brief 엔진을 @p frameCount 프레임 렌더해 스테레오 교차 버퍼로 돌려줍니다. */
        static sw::vector<float32> render( sw::AudioEngine& engine, uint32 frameCount )
        {
            sw::vector<float32> listSample( static_cast<size_t>( frameCount ) * sw::audio::kChannelCount, 0.0f );
            engine.render( listSample.data(), frameCount );
            return listSample;
        }

        /** @brief 한 채널(0 왼쪽 · 1 오른쪽)의 [first, first + count) 프레임 평균입니다. */
        static float32 computeMean( const sw::vector<float32>& listSample, uint32 channel, uint32 firstFrame, uint32 frameCount )
        {
            float64 sum = 0.0;
            for ( uint32 frameIndex = firstFrame; frameIndex < firstFrame + frameCount; ++frameIndex )
            {
                sum += static_cast<float64>( listSample[static_cast<size_t>( frameIndex ) * 2 + channel] );
            }
            return static_cast<float32>( sum / static_cast<float64>( frameCount ) );
        }

        /** @brief 한 채널의 [first, first + count) 프레임 RMS 입니다. */
        static float32 computeRms( const sw::vector<float32>& listSample, uint32 channel, uint32 firstFrame, uint32 frameCount )
        {
            float64 sum = 0.0;
            for ( uint32 frameIndex = firstFrame; frameIndex < firstFrame + frameCount; ++frameIndex )
            {
                const float64 value = static_cast<float64>( listSample[static_cast<size_t>( frameIndex ) * 2 + channel] );
                sum += value * value;
            }
            return static_cast<float32>( std::sqrt( sum / static_cast<float64>( frameCount ) ) );
        }

        /** @brief 한 채널의 [first, first + count) 프레임 절댓값 최대입니다. */
        static float32 computePeak( const sw::vector<float32>& listSample, uint32 channel, uint32 firstFrame, uint32 frameCount )
        {
            float32 peak = 0.0f;
            for ( uint32 frameIndex = firstFrame; frameIndex < firstFrame + frameCount; ++frameIndex )
            {
                peak = sw::MathUtil::max( peak, sw::MathUtil::abs( listSample[static_cast<size_t>( frameIndex ) * 2 + channel] ) );
            }
            return peak;
        }

        /** @brief 첫 프레임부터 보아 절댓값이 @p threshold 를 넘는 첫 프레임입니다. 없으면 -1 입니다. */
        static int64 findFirstFrameAbove( const sw::vector<float32>& listSample, uint32 channel, float32 threshold )
        {
            const size_t frameCount = listSample.size() / 2;
            for ( size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
            {
                if ( sw::MathUtil::abs( listSample[frameIndex * 2 + channel] ) > threshold )
                    return static_cast<int64>( frameIndex );
            }
            return -1;
        }

        /**
         * @brief 한 채널의 [first, first + count) 프레임에서 @p frequencyHz 성분의 진폭입니다(단일 빈 DFT — 정수 주기를 담으면 정확합니다).
         */
        static float32 computeToneAmplitude( const sw::vector<float32>& listSample, uint32 channel, uint32 firstFrame, uint32 frameCount, float32 frequencyHz )
        {
            float64 real      = 0.0;
            float64 imaginary = 0.0;
            for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
            {
                const float64 phase = 2.0 * 3.14159265358979323846 * static_cast<float64>( frequencyHz ) * static_cast<float64>( frameIndex ) / static_cast<float64>( sw::audio::kSampleRate );
                const float64 value = static_cast<float64>( listSample[static_cast<size_t>( firstFrame + frameIndex ) * 2 + channel] );
                real += value * std::cos( phase );
                imaginary += value * std::sin( phase );
            }
            return static_cast<float32>( 2.0 * std::sqrt( real * real + imaginary * imaginary ) / static_cast<float64>( frameCount ) );
        }
    };
} // namespace test
