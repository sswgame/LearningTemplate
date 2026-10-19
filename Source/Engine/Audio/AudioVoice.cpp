#include "pch.h"

#include "Engine/Audio/AudioVoice.h"

#include "Engine/Audio/AudioTypes.h"

namespace sw
{
    namespace
    {
        /** @brief 메모리에 통째로 푼 클립에서 보간 샘플을 읽습니다. */
        struct AudioVoiceMemorySampler
        {
            const float32* _pSample;
            bool           _bStereo;

            void sample( uint32 index, uint32 next, float32 fraction, float32& outLeft, float32& outRight ) const
            {
                if ( _bStereo )
                {
                    // 클립은 교차 저장이다(스테레오 클립의 프레임 간격).
                    constexpr uint32 kStride = audio::kStereoChannelCount;
                    outLeft                  = _pSample[index * kStride] + ( _pSample[next * kStride] - _pSample[index * kStride] ) * fraction;
                    outRight                 = _pSample[index * kStride + 1] + ( _pSample[next * kStride + 1] - _pSample[index * kStride + 1] ) * fraction;
                }
                else
                {
                    outLeft  = _pSample[index] + ( _pSample[next] - _pSample[index] ) * fraction;
                    outRight = outLeft;
                }
            }
        };

        /**
         * @brief 스트리밍 창에서 보간 샘플을 읽습니다.
         * @details 루프 이음매(`index` 가 마지막 프레임이고 `next` 가 0)는 창을 처음으로 되감지 않고 처음 읽어 둔 첫 프레임으로 보간한다.
         *          첫 프레임을 읽은 적이 없으면(중간에서 시작) 보간 없이 `index` 프레임을 쓴다.
         */
        struct AudioVoiceStreamSampler
        {
            AudioStreamWindow& _window;
            float32&           _firstLeft;
            float32&           _firstRight;
            bool&              _bHaveFirst;

            void sample( uint32 index, uint32 next, float32 fraction, float32& outLeft, float32& outRight ) const
            {
                float32 leftA{ 0.0f };
                float32 rightA{ 0.0f };
                (void)_window.readFrame( index, leftA, rightA ); // 끝을 넘으면 0 이다(무음)
                if ( index == 0 && _bHaveFirst == false )
                {
                    _firstLeft  = leftA;
                    _firstRight = rightA;
                    _bHaveFirst = true;
                }
                float32 leftB{ leftA };
                float32 rightB{ rightA };
                if ( next == 0 && index != 0 )
                {
                    if ( _bHaveFirst )
                    {
                        leftB  = _firstLeft;
                        rightB = _firstRight;
                    }
                }
                else if ( next != index )
                {
                    (void)_window.readFrame( next, leftB, rightB ); // 끝을 넘으면 0 이라 무음이다(보간 상대가 없다)
                }
                outLeft  = leftA + ( leftB - leftA ) * fraction;
                outRight = rightA + ( rightB - rightA ) * fraction;
            }
        };
    } // namespace

    AudioVoice::AudioVoice()
        : _pClip{ nullptr }
        , _lowPass{}
        , _stream{}
        , _firstFrameLeft{ 0.0f }
        , _firstFrameRight{ 0.0f }
        , _bHaveFirstFrame{ false }
        , _position{ 0.0 }
        , _rate{ 1.0 }
        , _gainLeft{ 0.0f }
        , _gainRight{ 0.0f }
        , _targetGainLeft{ 0.0f }
        , _targetGainRight{ 0.0f }
        , _lowPassHz{ audio::kFilterOpenHz }
        , _fadeGain{ 1.0f }
        , _fadeTarget{ 1.0f }
        , _fadeStep{ 0.0f }
        , _fadeDelayFrames{ 0 }
        , _startDelayFrames{ 0 }
        , _bLoop{ SW_FALSE }
        , _bFinished{ SW_FALSE }
        , _bPaused{ SW_FALSE }
        , _bStopAtFade{ SW_FALSE }
        , _bFirstBlock{ SW_TRUE }
        , _bRampIn{ SW_TRUE }
        , _reservedVoice{ 0 }
    {
    }

    void AudioVoice::start( shared_ptr<const AudioClipData> pClip, bool bLoop, uint32 startDelayFrames, bool bRampIn )
    {
        reset();
        _bRampIn          = bRampIn ? SW_TRUE : SW_FALSE;
        _pClip            = std::move( pClip );
        _bLoop            = bLoop ? SW_TRUE : SW_FALSE;
        _startDelayFrames = startDelayFrames;
        _bFinished        = ( _pClip == nullptr || _pClip->_frameCount == 0 ) ? SW_TRUE : SW_FALSE;
        // 스트리밍 클립은 보이스마다 디코더를 연다(압축 바이트는 클립이 한 벌만 든다). 열지 못하면 소리 없이 끝난다.
        const bool bStreamOpenFailed = _bFinished == SW_FALSE && _pClip->isStreamed() && _stream.open( *_pClip->_pStream ) == false;
        if ( bStreamOpenFailed )
            _bFinished = SW_TRUE;
    }

    void AudioVoice::reset()
    {
        *this = AudioVoice{};
    }

    void AudioVoice::setTarget( float32 volume, float32 pan )
    {
        const float32 clampedPan = MathUtil::clamp( pan, -1.0f, 1.0f );
        if ( _pClip != nullptr && _pClip->_channelCount >= 2 )
        {
            // 밸런스: 가운데에서 두 채널 그대로, 한쪽으로 가면 반대쪽만 줄인다.
            _targetGainLeft  = volume * MathUtil::min( 1.0f, 1.0f - clampedPan );
            _targetGainRight = volume * MathUtil::min( 1.0f, 1.0f + clampedPan );
        }
        else
        {
            // 등전력: θ = (pan + 1)·π/4, L = cos θ, R = sin θ.
            const float32 theta = ( clampedPan + 1.0f ) * MathUtil::kPi * 0.25f;
            _targetGainLeft     = volume * MathUtil::cos( theta );
            _targetGainRight    = volume * MathUtil::sin( theta );
        }
        if ( _bFirstBlock == SW_TRUE )
        {
            _gainLeft  = _bRampIn == SW_TRUE ? 0.0f : _targetGainLeft;
            _gainRight = _bRampIn == SW_TRUE ? 0.0f : _targetGainRight;
        }
    }

    void AudioVoice::setFade( float32 targetGain, uint32 durationFrames, uint32 delayFrames, bool bStopAtEnd )
    {
        _fadeTarget      = MathUtil::max( 0.0f, targetGain );
        _fadeDelayFrames = delayFrames;
        _bStopAtFade     = bStopAtEnd ? SW_TRUE : SW_FALSE;
        if ( durationFrames == 0 )
        {
            _fadeStep = MathUtil::kMaxFloat;
            return;
        }
        _fadeStep = MathUtil::abs( _fadeTarget - _fadeGain ) / static_cast<float32>( durationFrames );
        if ( _fadeStep <= 0.0f )
            _fadeStep = MathUtil::kMaxFloat;
    }

    bool AudioVoice::stepFade()
    {
        if ( _fadeDelayFrames > 0 )
        {
            --_fadeDelayFrames;
            return true;
        }
        if ( _fadeGain != _fadeTarget )
        {
            if ( _fadeGain < _fadeTarget )
                _fadeGain = ( _fadeTarget - _fadeGain <= _fadeStep ) ? _fadeTarget : _fadeGain + _fadeStep;
            else
                _fadeGain = ( _fadeGain - _fadeTarget <= _fadeStep ) ? _fadeTarget : _fadeGain - _fadeStep;
        }
        const bool bFadedOut = _bStopAtFade == SW_TRUE && _fadeGain == _fadeTarget;
        return bFadedOut == false;
    }

    void AudioVoice::setLowPass( float32 cutoffHz )
    {
        const float32 cutoff = MathUtil::clamp( cutoffHz, 20.0f, audio::kFilterOpenHz );
        if ( MathUtil::abs( cutoff - _lowPassHz ) <= _lowPassHz * 0.005f )
            return;
        // 열려 있다가 처음 걸리면 상태를 비운다(오래된 상태가 튀지 않게).
        if ( _lowPassHz >= audio::kFilterOpenHz )
            _lowPass.reset();
        _lowPassHz = cutoff;
        if ( cutoff < audio::kFilterOpenHz )
            _lowPass._coefficients = AudioBiquadCoefficients::make( AudioBiquadType::LowPass, cutoff, 0.7071f, 0.0f, static_cast<float32>( audio::kSampleRate ) );
    }

    void AudioVoice::mix( const AudioBlockView& bus, const AudioBlockView& scratch )
    {
        if ( isActive() == false || _bPaused == SW_TRUE )
            return;
        // 보이스는 아직 앞 두 채널(FL FR)에만 더한다. 그 밖의 레이아웃은 엔진이 초기화에서 거절한다(방향 패너는 U2).
        SW_ASSERT( bus._channelCount == audio::kStereoChannelCount );
        if ( _lowPassHz >= audio::kFilterOpenHz || scratch.isEmpty() )
        {
            mixInto( bus );
            return;
        }
        AudioBlockView work = scratch;
        work._channelCount  = bus._channelCount;
        work._frameCount    = bus._frameCount;
        for ( uint32 channelIndex = 0; channelIndex < audio::kStereoChannelCount; ++channelIndex )
        {
            Memory::set( work.getChannel( channelIndex ), 0, static_cast<size_t>( work._frameCount ) * sizeof( float32 ) );
        }
        mixInto( work );
        _lowPass.process( work );
        for ( uint32 channelIndex = 0; channelIndex < audio::kStereoChannelCount; ++channelIndex )
        {
            float32*       pTarget = bus.getChannel( channelIndex );
            const float32* pSource = work.getChannel( channelIndex );
            for ( uint32 frameIndex = 0; frameIndex < work._frameCount; ++frameIndex )
            {
                pTarget[frameIndex] += pSource[frameIndex];
            }
        }
    }

    void AudioVoice::mixInto( const AudioBlockView& target )
    {
        if ( _pClip->isStreamed() )
        {
            AudioVoiceStreamSampler sampler{ _stream, _firstFrameLeft, _firstFrameRight, _bHaveFirstFrame };
            mixLoop( target, sampler );
            return;
        }
        AudioVoiceMemorySampler sampler{ _pClip->_listSample.data(), _pClip->_channelCount >= audio::kStereoChannelCount };
        mixLoop( target, sampler );
    }

    template <typename TSampler>
    void AudioVoice::mixLoop( const AudioBlockView& target, TSampler& sampler )
    {
        const uint32         frameCount   = target._frameCount;
        float32*             pTargetLeft  = target.getChannel( 0 );
        float32*             pTargetRight = target.getChannel( 1 );
        const AudioClipData& clip         = *_pClip;
        const uint32         clipFrames   = clip._frameCount;
        const float32        invFrames    = 1.0f / static_cast<float32>( frameCount );
        const float32        stepLeft     = ( _targetGainLeft - _gainLeft ) * invFrames;
        const float32        stepRight    = ( _targetGainRight - _gainRight ) * invFrames;
        float32              gainLeft     = _gainLeft;
        float32              gainRight    = _gainRight;
        float64              position     = _position;
        const float64        rate         = _rate;
        const float64        clipFrames64 = static_cast<float64>( clipFrames );

        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            gainLeft += stepLeft;
            gainRight += stepRight;
            if ( _startDelayFrames > 0 )
            {
                --_startDelayFrames;
                continue;
            }
            if ( stepFade() == false )
            {
                _bFinished = SW_TRUE;
                break;
            }

            const uint32  index    = static_cast<uint32>( position );
            const float32 fraction = static_cast<float32>( position - static_cast<float64>( index ) );
            uint32        next     = index + 1;
            if ( next >= clipFrames )
                next = _bLoop == SW_TRUE ? 0u : index;

            const float32 fade = _fadeGain;
            float32       left{ 0.0f };
            float32       right{ 0.0f };
            sampler.sample( index, next, fraction, left, right );
            pTargetLeft[frameIndex] += left * gainLeft * fade;
            pTargetRight[frameIndex] += right * gainRight * fade;

            position += rate;
            if ( position >= clipFrames64 )
            {
                if ( _bLoop == SW_FALSE )
                {
                    _bFinished = SW_TRUE;
                    break;
                }
                position -= clipFrames64 * MathUtil::floor( position / clipFrames64 );
            }
        }

        _position    = position;
        _gainLeft    = _targetGainLeft;
        _gainRight   = _targetGainRight;
        _bFirstBlock = SW_FALSE;
    }

    void AudioVoice::advanceVirtual( uint32 frameCount )
    {
        if ( isActive() == false || _bPaused == SW_TRUE )
            return;
        // 다시 섞일 때 0 에서 오르게 둔다. 가상이 되는 동안은 들리지 않았기 때문이다.
        _gainLeft    = 0.0f;
        _gainRight   = 0.0f;
        _bFirstBlock = SW_FALSE;

        uint32 remaining = frameCount;
        if ( _startDelayFrames > 0 )
        {
            const uint32 consumed = MathUtil::min( _startDelayFrames, remaining );
            _startDelayFrames -= consumed;
            remaining -= consumed;
        }
        for ( uint32 frameIndex = 0; frameIndex < remaining; ++frameIndex )
        {
            if ( stepFade() == false )
            {
                _bFinished = SW_TRUE;
                return;
            }
            // 페이드가 걸리지 않은 동안은 한 번에 진행해도 된다.
            const bool bFadeIdle = _fadeDelayFrames == 0 && _fadeGain == _fadeTarget;
            if ( bFadeIdle )
            {
                advancePosition( remaining - frameIndex );
                return;
            }
            advancePosition( 1 );
            if ( _bFinished == SW_TRUE )
                return;
        }
    }

    void AudioVoice::setPosition( float64 clipFrame )
    {
        if ( _pClip == nullptr )
            return;
        const float64 clipFrames = static_cast<float64>( _pClip->_frameCount );
        _position                = MathUtil::max( 0.0, clipFrame );
        if ( _position < clipFrames )
            return;
        if ( _bLoop == SW_FALSE )
        {
            _bFinished = SW_TRUE;
            return;
        }
        _position -= clipFrames * MathUtil::floor( _position / clipFrames );
    }

    void AudioVoice::advancePosition( uint32 frameCount )
    {
        if ( _pClip == nullptr )
            return;
        const float64 clipFrames = static_cast<float64>( _pClip->_frameCount );
        _position += _rate * static_cast<float64>( frameCount );
        if ( _position < clipFrames )
            return;
        if ( _bLoop == SW_FALSE )
        {
            _bFinished = SW_TRUE;
            return;
        }
        _position -= clipFrames * MathUtil::floor( _position / clipFrames );
    }
} // namespace sw
