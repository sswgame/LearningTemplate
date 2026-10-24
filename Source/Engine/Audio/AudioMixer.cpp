#include "pch.h"

#include "Engine/Audio/AudioMixer.h"

#include "Core/Memory/Memory.h"

#include "Engine/Audio/AudioTypes.h"

namespace sw
{
    SW_LOG_CALLER( "AudioMixer" );

    namespace
    {
        /** @brief 버스 데이터의 채널 구성을 출력 레이아웃 아래의 실제 레이아웃으로 풉니다. */
        AudioChannelLayout resolveAudioMixerBusLayout( AudioBusChannels channels, AudioChannelLayout outputLayout )
        {
            switch ( channels )
            {
                case AudioBusChannels::Output:
                    return outputLayout;
                case AudioBusChannels::Stereo:
                    return AudioChannelLayout::Stereo;
            }
            return outputLayout;
        }
    } // namespace

    AudioMixer::AudioMixer()
        : _desc{}
        , _listBusState{}
        , _listOrder{}
        , _masterIndex{ 0 }
        , _blockFrameCount{ 0 }
        , _layout{ AudioChannelLayout::Stereo }
    {
    }

    AudioMixer::~AudioMixer() = default;

    bool AudioMixer::initialize( const AudioMixerDesc& desc, AudioChannelLayout layout )
    {
        _listBusState.clear();
        _listOrder.clear();
        if ( desc.validate( "mixer" ) == false || desc.makeProcessingOrder( _listOrder ) == false )
            return false;

        _desc   = desc;
        _layout = layout;
        _listBusState.resize( desc._listBus.size() );
        for ( size_t busIndex = 0; busIndex < desc._listBus.size(); ++busIndex )
        {
            const AudioBusDesc& busDesc = desc._listBus[busIndex];
            Bus&                bus     = _listBusState[busIndex];
            bus._name                   = busDesc._name;
            bus._parentIndex            = desc.findBusIndex( busDesc._parent );
            bus._volumeDb               = busDesc._volumeDb;
            bus._bMuted                 = busDesc._bMuted ? SW_TRUE : SW_FALSE;
            bus._bSolo                  = busDesc._bSolo ? SW_TRUE : SW_FALSE;
            bus._layout                 = resolveAudioMixerBusLayout( busDesc._channels, layout );
            bus._input.allocate( bus._layout );
            // 넓은 버스가 두 채널 버스(부모 · 센드)로 갈 때 쓰는 BS.775 행렬이다. 레이아웃의 마스크는 늘 아는 스피커라 실패하지 않는다.
            const uint32 busChannelCount = AudioChannelLayoutUtil::getChannelCount( bus._layout );
            const bool   bWideBus        = busChannelCount > audio::kStereoChannelCount;
            if ( bWideBus && AudioDownmix::makeForWaveMask( busChannelCount, AudioChannelLayoutUtil::getChannelMask( bus._layout ), bus._downmix ) == false )
            {
                SW_LOG_ERROR( "Bus '%#' has no downmix for %# channels", busDesc._name.c_str(), busChannelCount );
                _listBusState.clear();
                return false;
            }
            for ( const AudioSendDesc& sendDesc : busDesc._listSend )
            {
                Send send;
                send._targetIndex = static_cast<uint32>( desc.findBusIndex( sendDesc._bus ) );
                send._levelDb     = sendDesc._levelDb;
                send._gain        = AudioMath::dbToLinear( sendDesc._levelDb );
                send._bPreFader   = sendDesc._bPreFader;
                bus._listSend.push_back( send );
            }
            for ( const AudioDuckDesc& duckDesc : busDesc._listDuck )
            {
                Duck duck;
                duck._sourceIndex    = static_cast<uint32>( desc.findBusIndex( duckDesc._sourceBus ) );
                duck._thresholdDb    = duckDesc._thresholdDb;
                duck._depthDb        = duckDesc._depthDb;
                duck._attackSeconds  = duckDesc._attackSeconds;
                duck._releaseSeconds = duckDesc._releaseSeconds;
                bus._listDuck.push_back( duck );
            }
            for ( const AudioEffectDesc& effectDesc : busDesc._listEffect )
            {
                // 지원하지 않는 레이아웃 · 읽지 못한 자원(임펄스 응답)은 조용히 건너뛰지 않고 그래프 전체를 오류로 멈춘다(오류는 createEffect 가 남긴다).
                unique_ptr<IAudioEffect> pEffect = AudioEffectRegistry::createEffect( effectDesc, bus._layout );
                if ( pEffect == nullptr )
                {
                    SW_LOG_ERROR( "Bus '%#' effect '%#' could not be created for %# bus channels", busDesc._name.c_str(), effectDesc._type.c_str(), busChannelCount );
                    _listBusState.clear();
                    return false;
                }
                bus._listEffect.push_back( std::move( pEffect ) );
                bus._listEffectName.push_back( effectDesc.getEffectName() );
            }
            if ( bus._parentIndex < 0 )
                _masterIndex = static_cast<uint32>( busIndex );
        }
        refreshSoloPaths();
        return true;
    }

    int32 AudioMixer::findBusIndex( const hashed_string& name ) const
    {
        for ( size_t busIndex = 0; busIndex < _listBusState.size(); ++busIndex )
        {
            if ( _listBusState[busIndex]._name == name )
                return static_cast<int32>( busIndex );
        }
        return -1;
    }

    void AudioMixer::setBusUserVolume( uint32 busIndex, float32 volume )
    {
        _listBusState[busIndex]._userVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
    }

    void AudioMixer::setBusMuted( uint32 busIndex, bool bMuted )
    {
        _listBusState[busIndex]._bMuted = bMuted ? SW_TRUE : SW_FALSE;
    }

    void AudioMixer::setBusSolo( uint32 busIndex, bool bSolo )
    {
        _listBusState[busIndex]._bSolo = bSolo ? SW_TRUE : SW_FALSE;
        refreshSoloPaths();
    }

    bool AudioMixer::setSendLevelDb( uint32 busIndex, uint32 targetBusIndex, float32 levelDb )
    {
        for ( Send& send : _listBusState[busIndex]._listSend )
        {
            if ( send._targetIndex == targetBusIndex )
            {
                send._levelDb = levelDb;
                return true;
            }
        }
        return false;
    }

    IAudioEffect* AudioMixer::findEffect( uint32 busIndex, const hashed_string& effectName ) const
    {
        const Bus& bus = _listBusState[busIndex];
        for ( size_t effectIndex = 0; effectIndex < bus._listEffect.size(); ++effectIndex )
        {
            if ( bus._listEffectName[effectIndex] == effectName )
                return bus._listEffect[effectIndex].get();
        }
        return nullptr;
    }

    void AudioMixer::refreshSoloPaths()
    {
        bool bAnySolo = false;
        for ( const Bus& bus : _listBusState )
        {
            bAnySolo = bAnySolo || bus._bSolo == SW_TRUE;
        }
        for ( Bus& bus : _listBusState )
        {
            bus._bAudible = bAnySolo ? SW_FALSE : SW_TRUE;
        }
        if ( bAnySolo == false )
            return;

        // 솔로 버스의 조상(신호가 지나가는 경로)과 자손(솔로 버스로 들어오는 것)은 소리를 낸다. DAW 와 Wwise 의 솔로와 같다.
        for ( size_t busIndex = 0; busIndex < _listBusState.size(); ++busIndex )
        {
            if ( _listBusState[busIndex]._bSolo == SW_FALSE )
                continue;
            for ( int32 ancestor = static_cast<int32>( busIndex ); ancestor >= 0; ancestor = _listBusState[static_cast<size_t>( ancestor )]._parentIndex )
            {
                _listBusState[static_cast<size_t>( ancestor )]._bAudible = SW_TRUE;
            }
            for ( size_t candidate = 0; candidate < _listBusState.size(); ++candidate )
            {
                for ( int32 walk = _listBusState[candidate]._parentIndex; walk >= 0; walk = _listBusState[static_cast<size_t>( walk )]._parentIndex )
                {
                    if ( walk == static_cast<int32>( busIndex ) )
                    {
                        _listBusState[candidate]._bAudible = SW_TRUE;
                        break;
                    }
                }
            }
        }
    }

    float32 AudioMixer::computeBusGain( uint32 busIndex ) const
    {
        const Bus& bus = _listBusState[busIndex];
        if ( bus._bMuted == SW_TRUE || bus._bAudible == SW_FALSE )
            return 0.0f;
        return AudioMath::dbToLinear( bus._volumeDb + bus._volumeOffsetDb + bus._duckDb ) * bus._userVolume;
    }

    void AudioMixer::updateDucking( uint32 blockFrames )
    {
        const float32 blockSeconds = static_cast<float32>( blockFrames ) / static_cast<float32>( audio::kSampleRate );
        for ( Bus& bus : _listBusState )
        {
            if ( bus._listDuck.empty() )
                continue;
            float32 deepestDb = 0.0f;
            for ( Duck& duck : bus._listDuck )
            {
                const float32 sourceDb = AudioMath::linearToDb( _listBusState[duck._sourceIndex]._peak );
                const float32 targetDb = duck._thresholdDb <= sourceDb ? duck._depthDb : 0.0f;
                // 더 깊어지는 방향이 공격, 되돌아오는 방향이 릴리스이다. 시간 0 은 한 블록에 끝까지 간다.
                const float32 seconds     = targetDb < duck._currentDb ? duck._attackSeconds : duck._releaseSeconds;
                const float32 coefficient = seconds <= 0.0f ? 1.0f : 1.0f - MathUtil::exp( -blockSeconds / seconds );
                duck._currentDb += ( targetDb - duck._currentDb ) * coefficient;
                deepestDb = MathUtil::min( deepestDb, duck._currentDb );
            }
            bus._duckDb = deepestDb;
        }
    }

    void AudioMixer::beginBlock( uint32 frameCount )
    {
        _blockFrameCount = MathUtil::min( frameCount, audio::kBlockFrameCount );
        for ( Bus& bus : _listBusState )
        {
            bus._input.clear( _blockFrameCount );
        }
    }

    void AudioMixer::process( const AudioBlockView& output )
    {
        SW_ASSERT( output._channelCount == AudioChannelLayoutUtil::getChannelCount( _layout ) );
        const uint32  blockFrames = MathUtil::min( output._frameCount, _blockFrameCount );
        const float32 invFrames   = blockFrames > 0 ? 1.0f / static_cast<float32>( blockFrames ) : 0.0f;
        updateDucking( blockFrames );
        for ( const uint32 busIndex : _listOrder )
        {
            Bus&                 bus   = _listBusState[busIndex];
            const AudioBlockView input = bus._input.makeView( blockFrames );

            // 인서트 이펙트는 센드와 페이더 앞에서 처리한다(채널 스트립과 같은 위치).
            for ( const unique_ptr<IAudioEffect>& pEffect : bus._listEffect )
            {
                pEffect->process( input );
            }

            // 프리 페이더 센드는 페이더 전의 신호를 보낸다.
            for ( Send& send : bus._listSend )
            {
                if ( send._bPreFader == false )
                    continue;
                const float32 targetGain = AudioMath::dbToLinear( send._levelDb );
                accumulateBus( busIndex, input, send._targetIndex, send._gain, targetGain );
                send._gain = targetGain;
            }

            // 페이더: 지난 블록의 게인에서 이번 목표까지 샘플마다 램프한다(지퍼 잡음 없음).
            const float32 targetGain = computeBusGain( busIndex );
            const float32 startGain  = bus._bFirstBlock == SW_TRUE ? targetGain : bus._gain;
            const float32 gainStep   = ( targetGain - startGain ) * invFrames;
            float32       peak       = 0.0f;
            for ( uint32 channelIndex = 0; channelIndex < input._channelCount; ++channelIndex )
            {
                float32* pChannel = input.getChannel( channelIndex );
                float32  gain     = startGain;
                for ( uint32 frameIndex = 0; frameIndex < blockFrames; ++frameIndex )
                {
                    gain += gainStep;
                    pChannel[frameIndex] *= gain;
                    peak = MathUtil::max( peak, MathUtil::abs( pChannel[frameIndex] ) );
                }
            }
            bus._gain        = targetGain;
            bus._peak        = peak;
            bus._bFirstBlock = SW_FALSE;

            for ( Send& send : bus._listSend )
            {
                if ( send._bPreFader )
                    continue;
                const float32 sendTarget = AudioMath::dbToLinear( send._levelDb );
                accumulateBus( busIndex, input, send._targetIndex, send._gain, sendTarget );
                send._gain = sendTarget;
            }

            if ( bus._parentIndex >= 0 )
                accumulateBus( busIndex, input, static_cast<uint32>( bus._parentIndex ), 1.0f, 1.0f );
        }

        // master 는 출력 레이아웃이다(데이터 검사가 막는다). 그래서 채널 수가 출력과 같다.
        const AudioBlockView master = _listBusState[_masterIndex]._input.makeView( blockFrames );
        for ( uint32 channelIndex = 0; channelIndex < output._channelCount; ++channelIndex )
        {
            Memory::copy( output.getChannel( channelIndex ), master.getChannel( channelIndex ), static_cast<size_t>( blockFrames ) * sizeof( float32 ) );
        }
    }

    void AudioMixer::accumulateBus( uint32 sourceIndex, const AudioBlockView& source, uint32 targetIndex, float32 startGain, float32 targetGain )
    {
        const AudioBlockView target    = _listBusState[targetIndex]._input.makeView( source._frameCount );
        const float32        invFrames = source._frameCount > 0 ? 1.0f / static_cast<float32>( source._frameCount ) : 0.0f;
        const float32        step      = ( targetGain - startGain ) * invFrames;
        const bool           bFoldDown = source._channelCount > target._channelCount;
        if ( bFoldDown )
        {
            // 넓은 쪽 → 두 채널: 소스 채널마다 (왼쪽, 오른쪽) 계수로 더한다(LFE 계수는 0 이다).
            const AudioDownmix& downmix = _listBusState[sourceIndex]._downmix;
            SW_ASSERT( target._channelCount == audio::kStereoChannelCount && downmix._channelCount == source._channelCount );
            float32* pLeft  = target.getChannel( 0 );
            float32* pRight = target.getChannel( 1 );
            for ( uint32 channelIndex = 0; channelIndex < source._channelCount; ++channelIndex )
            {
                const float32  leftWeight  = downmix._arrLeft[channelIndex];
                const float32  rightWeight = downmix._arrRight[channelIndex];
                const float32* pSource     = source.getChannel( channelIndex );
                float32        gain        = startGain;
                for ( uint32 frameIndex = 0; frameIndex < source._frameCount; ++frameIndex )
                {
                    gain += step;
                    const float32 scaledSample = pSource[frameIndex] * gain;
                    pLeft[frameIndex] += scaledSample * leftWeight;
                    pRight[frameIndex] += scaledSample * rightWeight;
                }
            }
            return;
        }
        // 같은 채널 수이거나 두 채널 → 넓은 쪽: 소스 채널을 같은 번호(FL FR …)에 더한다. 넓은 쪽의 나머지 채널은 그대로다.
        for ( uint32 channelIndex = 0; channelIndex < source._channelCount; ++channelIndex )
        {
            const float32* pSource = source.getChannel( channelIndex );
            float32*       pTarget = target.getChannel( channelIndex );
            float32        gain    = startGain;
            for ( uint32 frameIndex = 0; frameIndex < source._frameCount; ++frameIndex )
            {
                gain += step;
                pTarget[frameIndex] += pSource[frameIndex] * gain;
            }
        }
    }
} // namespace sw
