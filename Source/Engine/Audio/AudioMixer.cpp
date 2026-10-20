#include "pch.h"

#include "Engine/Audio/AudioMixer.h"

#include "Core/Memory/Memory.h"

#include "Engine/Audio/AudioTypes.h"

namespace sw
{
    SW_LOG_CALLER( "AudioMixer" );

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

        // 이펙트는 스테레오만 처리한다. 맞지 않는 조합은 조용히 건너뛰지 않고 여기서 멈춘다(채널별 이펙트는 U3).
        const bool bStereo = AudioChannelLayoutUtil::getChannelCount( layout ) == audio::kStereoChannelCount;
        for ( const AudioBusDesc& busDesc : desc._listBus )
        {
            if ( bStereo == false && busDesc._listEffect.empty() == false )
            {
                SW_LOG_ERROR( "Bus '%#' has effects, which only process stereo, but the layout has %# channels", busDesc._name.c_str(),
                              AudioChannelLayoutUtil::getChannelCount( layout ) );
                return false;
            }
        }

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
            bus._input.allocate( layout );
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
                unique_ptr<IAudioEffect> pEffect = AudioEffectRegistry::createEffect( effectDesc._type );
                for ( const AudioEffectParameterDesc& parameter : effectDesc._listParameter )
                {
                    const int32 parameterIndex = pEffect->findParameterIndex( parameter._name );
                    if ( parameterIndex >= 0 )
                        pEffect->setParameter( static_cast<uint32>( parameterIndex ), parameter._value );
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
        const uint32  blockFrames  = MathUtil::min( output._frameCount, _blockFrameCount );
        const uint32  channelCount = AudioChannelLayoutUtil::getChannelCount( _layout );
        const float32 invFrames    = blockFrames > 0 ? 1.0f / static_cast<float32>( blockFrames ) : 0.0f;
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
                const float32        targetGain = AudioMath::dbToLinear( send._levelDb );
                const float32        step       = ( targetGain - send._gain ) * invFrames;
                const AudioBlockView target     = _listBusState[send._targetIndex]._input.makeView( blockFrames );
                for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
                {
                    const float32* pSource = input.getChannel( channelIndex );
                    float32*       pTarget = target.getChannel( channelIndex );
                    float32        gain    = send._gain;
                    for ( uint32 frameIndex = 0; frameIndex < blockFrames; ++frameIndex )
                    {
                        gain += step;
                        pTarget[frameIndex] += pSource[frameIndex] * gain;
                    }
                }
                send._gain = targetGain;
            }

            // 페이더: 지난 블록의 게인에서 이번 목표까지 샘플마다 램프한다(지퍼 잡음 없음).
            const float32 targetGain = computeBusGain( busIndex );
            const float32 startGain  = bus._bFirstBlock == SW_TRUE ? targetGain : bus._gain;
            const float32 gainStep   = ( targetGain - startGain ) * invFrames;
            float32       peak       = 0.0f;
            for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
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
                const float32        sendTarget = AudioMath::dbToLinear( send._levelDb );
                const float32        step       = ( sendTarget - send._gain ) * invFrames;
                const AudioBlockView target     = _listBusState[send._targetIndex]._input.makeView( blockFrames );
                for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
                {
                    const float32* pSource  = input.getChannel( channelIndex );
                    float32*       pTarget  = target.getChannel( channelIndex );
                    float32        sendGain = send._gain;
                    for ( uint32 frameIndex = 0; frameIndex < blockFrames; ++frameIndex )
                    {
                        sendGain += step;
                        pTarget[frameIndex] += pSource[frameIndex] * sendGain;
                    }
                }
                send._gain = sendTarget;
            }

            if ( bus._parentIndex >= 0 )
            {
                const AudioBlockView parent = _listBusState[static_cast<size_t>( bus._parentIndex )]._input.makeView( blockFrames );
                for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
                {
                    const float32* pSource = input.getChannel( channelIndex );
                    float32*       pParent = parent.getChannel( channelIndex );
                    for ( uint32 frameIndex = 0; frameIndex < blockFrames; ++frameIndex )
                    {
                        pParent[frameIndex] += pSource[frameIndex];
                    }
                }
            }
        }

        const AudioBlockView master = _listBusState[_masterIndex]._input.makeView( blockFrames );
        for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
        {
            Memory::copy( output.getChannel( channelIndex ), master.getChannel( channelIndex ), static_cast<size_t>( blockFrames ) * sizeof( float32 ) );
        }
    }
} // namespace sw
