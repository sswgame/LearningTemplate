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
    {
    }

    AudioMixer::~AudioMixer() = default;

    bool AudioMixer::initialize( const AudioMixerDesc& desc )
    {
        _listBusState.clear();
        _listOrder.clear();
        if ( desc.validate( "mixer" ) == false || desc.makeProcessingOrder( _listOrder ) == false )
            return false;

        _desc = desc;
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
            bus._listInput.assign( static_cast<size_t>( audio::kBlockFrameCount ) * audio::kChannelCount, 0.0f );
            for ( const AudioSendDesc& sendDesc : busDesc._listSend )
            {
                Send send;
                send._targetIndex = static_cast<uint32>( desc.findBusIndex( sendDesc._bus ) );
                send._levelDb     = sendDesc._levelDb;
                send._gain        = AudioMath::dbToLinear( sendDesc._levelDb );
                send._bPreFader   = sendDesc._bPreFader;
                bus._listSend.push_back( send );
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

    float32 AudioMixer::getSendLevelDb( uint32 busIndex, uint32 targetBusIndex ) const
    {
        for ( const Send& send : _listBusState[busIndex]._listSend )
        {
            if ( send._targetIndex == targetBusIndex )
                return send._levelDb;
        }
        return audio::kSilenceDb;
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

        // 솔로 버스의 조상(신호가 지나가는 길)과 자손(솔로 버스로 들어오는 것)은 소리를 낸다. DAW · Wwise 의 솔로와 같다.
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
        return AudioMath::dbToLinear( bus._volumeDb + bus._volumeOffsetDb ) * bus._userVolume;
    }

    void AudioMixer::beginBlock( uint32 frameCount )
    {
        _blockFrameCount         = MathUtil::min( frameCount, audio::kBlockFrameCount );
        const size_t sampleCount = static_cast<size_t>( _blockFrameCount ) * audio::kChannelCount;
        for ( Bus& bus : _listBusState )
        {
            Memory::set( bus._listInput.data(), 0, sampleCount * sizeof( float32 ) );
        }
    }

    void AudioMixer::process( float32* pOutput, uint32 frameCount )
    {
        const uint32  blockFrames = MathUtil::min( frameCount, _blockFrameCount );
        const float32 invFrames   = blockFrames > 0 ? 1.0f / static_cast<float32>( blockFrames ) : 0.0f;
        for ( const uint32 busIndex : _listOrder )
        {
            Bus&     bus    = _listBusState[busIndex];
            float32* pInput = bus._listInput.data();

            // 인서트 이펙트 — 센드 · 페이더 앞(채널 스트립과 같은 자리).
            for ( const unique_ptr<IAudioEffect>& pEffect : bus._listEffect )
            {
                pEffect->process( pInput, blockFrames );
            }

            // 프리 페이더 센드는 페이더 전의 신호를 보낸다.
            for ( Send& send : bus._listSend )
            {
                if ( send._bPreFader == false )
                    continue;
                const float32 targetGain = AudioMath::dbToLinear( send._levelDb );
                const float32 step       = ( targetGain - send._gain ) * invFrames;
                float32*      pTarget    = _listBusState[send._targetIndex]._listInput.data();
                float32       gain       = send._gain;
                for ( uint32 frameIndex = 0; frameIndex < blockFrames; ++frameIndex )
                {
                    gain += step;
                    pTarget[frameIndex * 2] += pInput[frameIndex * 2] * gain;
                    pTarget[frameIndex * 2 + 1] += pInput[frameIndex * 2 + 1] * gain;
                }
                send._gain = targetGain;
            }

            // 페이더: 지난 블록의 게인에서 이번 목표까지 샘플마다 램프한다(지퍼 잡음 없음).
            const float32 targetGain = computeBusGain( busIndex );
            const float32 startGain  = bus._bFirstBlock == SW_TRUE ? targetGain : bus._gain;
            const float32 gainStep   = ( targetGain - startGain ) * invFrames;
            float32       gain       = startGain;
            float32       peak       = 0.0f;
            for ( uint32 frameIndex = 0; frameIndex < blockFrames; ++frameIndex )
            {
                gain += gainStep;
                pInput[frameIndex * 2] *= gain;
                pInput[frameIndex * 2 + 1] *= gain;
                peak = MathUtil::max( peak, MathUtil::max( MathUtil::abs( pInput[frameIndex * 2] ), MathUtil::abs( pInput[frameIndex * 2 + 1] ) ) );
            }
            bus._gain        = targetGain;
            bus._peak        = peak;
            bus._bFirstBlock = SW_FALSE;

            for ( Send& send : bus._listSend )
            {
                if ( send._bPreFader )
                    continue;
                const float32 sendTarget = AudioMath::dbToLinear( send._levelDb );
                const float32 step       = ( sendTarget - send._gain ) * invFrames;
                float32*      pTarget    = _listBusState[send._targetIndex]._listInput.data();
                float32       sendGain   = send._gain;
                for ( uint32 frameIndex = 0; frameIndex < blockFrames; ++frameIndex )
                {
                    sendGain += step;
                    pTarget[frameIndex * 2] += pInput[frameIndex * 2] * sendGain;
                    pTarget[frameIndex * 2 + 1] += pInput[frameIndex * 2 + 1] * sendGain;
                }
                send._gain = sendTarget;
            }

            if ( bus._parentIndex >= 0 )
            {
                float32* pParent = _listBusState[static_cast<size_t>( bus._parentIndex )]._listInput.data();
                for ( uint32 sampleIndex = 0; sampleIndex < blockFrames * 2; ++sampleIndex )
                {
                    pParent[sampleIndex] += pInput[sampleIndex];
                }
            }
        }

        const float32* pMaster = _listBusState[_masterIndex]._listInput.data();
        Memory::copy( pOutput, pMaster, static_cast<size_t>( blockFrames ) * audio::kChannelCount * sizeof( float32 ) );
    }
} // namespace sw
