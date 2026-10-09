#include "pch.h"

#include "Engine/Audio/AudioVoice.h"

#include "Engine/Audio/AudioTypes.h"

namespace sw
{
    AudioVoice::AudioVoice()
        : _pClip{ nullptr }
        , _lowPass{}
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

    void AudioVoice::mix( float32* pBusInput, uint32 frameCount, float32* pScratch )
    {
        if ( isActive() == false || _bPaused == SW_TRUE )
            return;
        if ( _lowPassHz >= audio::kFilterOpenHz || pScratch == nullptr )
        {
            mixInto( pBusInput, frameCount );
            return;
        }
        const size_t sampleCount = static_cast<size_t>( frameCount ) * 2;
        Memory::set( pScratch, 0, sampleCount * sizeof( float32 ) );
        mixInto( pScratch, frameCount );
        _lowPass.process( pScratch, frameCount );
        for ( size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
        {
            pBusInput[sampleIndex] += pScratch[sampleIndex];
        }
    }

    void AudioVoice::mixInto( float32* pBusInput, uint32 frameCount )
    {

        const AudioClipData& clip         = *_pClip;
        const float32*       pSample      = clip._listSample.data();
        const uint32         clipFrames   = clip._frameCount;
        const bool           bStereo      = clip._channelCount >= 2;
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
            if ( bStereo )
            {
                const float32 left  = pSample[index * 2] + ( pSample[next * 2] - pSample[index * 2] ) * fraction;
                const float32 right = pSample[index * 2 + 1] + ( pSample[next * 2 + 1] - pSample[index * 2 + 1] ) * fraction;
                pBusInput[frameIndex * 2] += left * gainLeft * fade;
                pBusInput[frameIndex * 2 + 1] += right * gainRight * fade;
            }
            else
            {
                const float32 mono = pSample[index] + ( pSample[next] - pSample[index] ) * fraction;
                pBusInput[frameIndex * 2] += mono * gainLeft * fade;
                pBusInput[frameIndex * 2 + 1] += mono * gainRight * fade;
            }

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
        // 다시 섞일 때 0 에서 오르게 둔다 — 가상이 되는 동안 들리지 않았다.
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
