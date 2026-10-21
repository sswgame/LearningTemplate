#include "pch.h"

#include "Engine/Audio/AudioVoice.h"

#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/Graph/AudioGraph.h"

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
        , _binaural{}
        , _stream{}
        , _pGraph{ nullptr }
        , _firstFrameLeft{ 0.0f }
        , _firstFrameRight{ 0.0f }
        , _bHaveFirstFrame{ false }
        , _position{ 0.0 }
        , _rate{ 1.0 }
        , _arrGain{}
        , _arrTargetGain{}
        , _gainChannelCount{ 0 }
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
        , _bBinaural{ SW_FALSE }
        , _reservedVoice{ 0 }
    {
    }

    void AudioVoice::start( shared_ptr<const AudioClipData> pClip, bool bLoop, uint32 startDelayFrames, bool bRampIn, uint64 graphSeed )
    {
        reset();
        _bRampIn          = bRampIn ? SW_TRUE : SW_FALSE;
        _pClip            = std::move( pClip );
        _bLoop            = bLoop ? SW_TRUE : SW_FALSE;
        _startDelayFrames = startDelayFrames;
        _bFinished        = ( _pClip == nullptr || _pClip->_frameCount == 0 ) ? SW_TRUE : SW_FALSE;
        if ( _bFinished == SW_TRUE )
            return;
        // 소리 그래프는 보이스마다 실행 상태를 만들어 블록 길이 덩어리의 창으로 읽는다(게임 파라미터 늦음이 한 블록 안이다).
        if ( _pClip->isGraph() )
        {
            unique_ptr<AudioGraphInstance> pInstance = make_unique<AudioGraphInstance>( _pClip->_pGraph, graphSeed );
            _pGraph                                  = pInstance.get();
            if ( _stream.openDecoder( std::move( pInstance ), 1, audio::kBlockFrameCount ) == false )
            {
                _pGraph    = nullptr;
                _bFinished = SW_TRUE;
            }
            return;
        }
        // 스트리밍 클립은 보이스마다 디코더를 연다(압축 바이트는 클립이 한 벌만 든다). 열지 못하면 소리 없이 끝난다.
        const bool bStreamOpenFailed = _pClip->isStreamed() && _stream.open( *_pClip->_pStream ) == false;
        if ( bStreamOpenFailed )
            _bFinished = SW_TRUE;
    }

    void AudioVoice::setGraphParameter( uint32 inputIndex, float32 value )
    {
        SW_ASSERT( _pGraph != nullptr );
        _pGraph->setParameterValue( inputIndex, value );
    }

    void AudioVoice::reset()
    {
        *this = AudioVoice{};
    }

    void AudioVoice::setTarget( float32 volume, const AudioPanDirection& direction, AudioChannelLayout layout )
    {
        _gainChannelCount        = AudioChannelLayoutUtil::getChannelCount( layout );
        const bool bStereoSource = _pClip != nullptr && _pClip->_channelCount >= audio::kStereoChannelCount;
        const bool bBinaural     = layout == AudioChannelLayout::Binaural && bStereoSource == false && direction._mode == AudioPanMode::Surround;
        if ( bBinaural && _bBinaural == SW_FALSE )
            _binaural.reset(); // 처음 바이노럴로 바뀌면 지난 지연선이 섞이지 않게 비운다
        _bBinaural = bBinaural ? SW_TRUE : SW_FALSE;
        if ( bBinaural )
        {
            // 방향은 HRTF 가 내고, 게인은 볼륨만 두 귀에 같다.
            _binaural.setDirection( direction );
            for ( float32& gain : _arrTargetGain )
            {
                gain = 0.0f;
            }
            _arrTargetGain[0] = volume;
            _arrTargetGain[1] = volume;
        }
        else if ( bStereoSource )
        {
            // 밸런스: 가운데에서 두 채널 그대로, 한쪽으로 가면 반대쪽만 줄인다. 앞 두 채널(FL FR) 밖은 0 이다.
            const float32 clampedPan = MathUtil::clamp( direction._right, -1.0f, 1.0f );
            for ( float32& gain : _arrTargetGain )
            {
                gain = 0.0f;
            }
            _arrTargetGain[0] = volume * MathUtil::min( 1.0f, 1.0f - clampedPan );
            _arrTargetGain[1] = volume * MathUtil::min( 1.0f, 1.0f + clampedPan );
        }
        else
        {
            float32 arrPanGain[audio::kMaxChannelCount]{};
            AudioPanner::computeGains( layout, direction, arrPanGain );
            for ( uint32 channelIndex = 0; channelIndex < audio::kMaxChannelCount; ++channelIndex )
            {
                _arrTargetGain[channelIndex] = volume * arrPanGain[channelIndex];
            }
        }
        if ( _bFirstBlock == SW_TRUE )
        {
            for ( uint32 channelIndex = 0; channelIndex < audio::kMaxChannelCount; ++channelIndex )
            {
                _arrGain[channelIndex] = _bRampIn == SW_TRUE ? 0.0f : _arrTargetGain[channelIndex];
            }
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

    void AudioVoice::mix( const AudioBlockView& bus )
    {
        if ( isActive() == false || _bPaused == SW_TRUE )
            return;
        SW_ASSERT( bus._channelCount == _gainChannelCount );
        SW_ASSERT( bus._frameCount <= audio::kBlockFrameCount );

        // 1 단계: 소스(왼쪽 · 오른쪽 평면)와 프레임별 페이드를 한 번만 만든다. 출력 채널 수와 상관없는 일이다.
        float32        arrSource[audio::kStereoChannelCount * audio::kBlockFrameCount];
        float32        arrFade[audio::kBlockFrameCount];
        AudioBlockView source;
        source._pData        = arrSource;
        source._channelCount = audio::kStereoChannelCount;
        source._frameCount   = bus._frameCount;
        uint32 activeBegin{ 0 };
        uint32 activeEnd{ 0 };
        // 스트리밍 클립과 소리 그래프는 같은 창으로 읽는다.
        if ( _pClip->isStreamed() || _pClip->isGraph() )
        {
            AudioVoiceStreamSampler sampler{ _stream, _firstFrameLeft, _firstFrameRight, _bHaveFirstFrame };
            renderSource( sampler, source, arrFade, activeBegin, activeEnd );
        }
        else
        {
            AudioVoiceMemorySampler sampler{ _pClip->_listSample.data(), _pClip->_channelCount >= audio::kStereoChannelCount };
            renderSource( sampler, source, arrFade, activeBegin, activeEnd );
        }

        // 2 단계: 로우패스는 패닝 전에 소스 채널에만 건다(모노 클립은 한 채널). 소리가 나지 않은 자리는 0 으로 채워 필터 상태가 이어지게 한다.
        const bool   bStereoSource      = _pClip->_channelCount >= audio::kStereoChannelCount;
        const uint32 sourceChannelCount = bStereoSource ? audio::kStereoChannelCount : 1u;
        const bool   bFiltered          = _lowPassHz < audio::kFilterOpenHz;
        if ( bFiltered || _bBinaural == SW_TRUE )
        {
            for ( uint32 channelIndex = 0; channelIndex < sourceChannelCount; ++channelIndex )
            {
                float32* pChannel = source.getChannel( channelIndex );
                for ( uint32 frameIndex = 0; frameIndex < activeBegin; ++frameIndex )
                {
                    pChannel[frameIndex] = 0.0f;
                }
                for ( uint32 frameIndex = activeEnd; frameIndex < source._frameCount; ++frameIndex )
                {
                    pChannel[frameIndex] = 0.0f;
                }
            }
        }
        if ( bFiltered )
        {
            AudioBlockView filtered = source;
            filtered._channelCount  = sourceChannelCount;
            _lowPass.process( filtered );
        }

        // 바이노럴: 모노 소스를 두 귀 신호로 바꿔 소스의 왼쪽 · 오른쪽 채널로 둔다. 아래 단계는 스테레오 소스처럼 FL ← 왼쪽, FR ← 오른쪽으로 더한다.
        float32    arrEar[audio::kStereoChannelCount * audio::kBlockFrameCount];
        const bool bBinaural = _bBinaural == SW_TRUE && bStereoSource == false;
        if ( bBinaural )
        {
            _binaural.process( source.getChannel( 0 ), source._frameCount, arrEar, arrEar + audio::kBlockFrameCount );
            source._pData = arrEar;
        }

        // 3 단계: 출력 채널마다 게인을 램프하며 더한다. 스테레오 소스는 FL ← 왼쪽, FR ← 오른쪽이고 나머지 채널은 게인이 0 이다.
        const float32 invFrames = 1.0f / static_cast<float32>( bus._frameCount );
        for ( uint32 channelIndex = 0; channelIndex < bus._channelCount; ++channelIndex )
        {
            const bool bSilentChannel = _arrGain[channelIndex] == 0.0f && _arrTargetGain[channelIndex] == 0.0f;
            if ( bSilentChannel )
                continue;
            const bool     bTwoSource   = bStereoSource || bBinaural;
            const bool     bRightSource = bTwoSource && channelIndex == 1;
            const float32* pSource      = source.getChannel( bRightSource ? 1u : 0u );
            float32*       pTarget      = bus.getChannel( channelIndex );
            const float32  step         = ( _arrTargetGain[channelIndex] - _arrGain[channelIndex] ) * invFrames;
            float32        gain         = _arrGain[channelIndex];
            for ( uint32 frameIndex = 0; frameIndex < activeBegin; ++frameIndex )
            {
                gain += step;
            }
            for ( uint32 frameIndex = activeBegin; frameIndex < activeEnd; ++frameIndex )
            {
                gain += step;
                pTarget[frameIndex] += pSource[frameIndex] * gain * arrFade[frameIndex];
            }
        }
        for ( uint32 channelIndex = 0; channelIndex < audio::kMaxChannelCount; ++channelIndex )
        {
            _arrGain[channelIndex] = _arrTargetGain[channelIndex];
        }
        _bFirstBlock = SW_FALSE;
    }

    template <typename TSampler>
    void AudioVoice::renderSource( TSampler& sampler, const AudioBlockView& source, float32* pFade, uint32& outActiveBegin, uint32& outActiveEnd )
    {
        const uint32         frameCount   = source._frameCount;
        float32*             pLeft        = source.getChannel( 0 );
        float32*             pRight       = source.getChannel( 1 );
        const AudioClipData& clip         = *_pClip;
        const uint32         clipFrames   = clip._frameCount;
        float64              position     = _position;
        const float64        rate         = _rate;
        const float64        clipFrames64 = static_cast<float64>( clipFrames );

        // 시작 지연은 블록 앞쪽에만, 끝남은 블록 뒤쪽에만 있으므로 소리가 난 프레임은 한 구간이다.
        outActiveBegin = frameCount;
        outActiveEnd   = frameCount;
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            if ( _startDelayFrames > 0 )
            {
                --_startDelayFrames;
                continue;
            }
            if ( outActiveBegin == frameCount )
                outActiveBegin = frameIndex;
            if ( stepFade() == false )
            {
                _bFinished   = SW_TRUE;
                outActiveEnd = frameIndex;
                break;
            }

            const uint32  index    = static_cast<uint32>( position );
            const float32 fraction = static_cast<float32>( position - static_cast<float64>( index ) );
            uint32        next     = index + 1;
            if ( next >= clipFrames )
                next = _bLoop == SW_TRUE ? 0u : index;

            float32 left{ 0.0f };
            float32 right{ 0.0f };
            sampler.sample( index, next, fraction, left, right );
            pLeft[frameIndex]  = left;
            pRight[frameIndex] = right;
            pFade[frameIndex]  = _fadeGain;

            position += rate;
            if ( position >= clipFrames64 )
            {
                if ( _bLoop == SW_FALSE )
                {
                    _bFinished   = SW_TRUE;
                    outActiveEnd = frameIndex + 1;
                    break;
                }
                position -= clipFrames64 * MathUtil::floor( position / clipFrames64 );
            }
        }
        _position = position;
    }

    void AudioVoice::advanceVirtual( uint32 frameCount )
    {
        if ( isActive() == false || _bPaused == SW_TRUE )
            return;
        // 다시 섞일 때 0 에서 오르게 둔다. 가상이 되는 동안은 들리지 않았기 때문이다.
        for ( float32& gain : _arrGain )
        {
            gain = 0.0f;
        }
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
