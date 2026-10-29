#include "pch.h"

#include "Engine/Audio/AudioSnapshotMixer.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Audio/AudioMixer.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/DSP/AudioEffect.h"

namespace sw
{
    namespace
    {
        struct AudioSnapshotMixerInternal
        {
            /** @brief 데이터에 적힌(없으면 종류 기본) 이펙트 파라미터 값입니다. */
            static float32 findBaseEffectValue( const AudioEffectDesc& effect, const AudioEffectTypeInfo& typeInfo, uint32 parameterIndex )
            {
                const hashed_string parameterName( typeInfo._pParameter[parameterIndex]._pName );
                for ( const AudioEffectParameterDesc& parameter : effect._listParameter )
                {
                    if ( parameter._name == parameterName )
                        return parameter._value;
                }
                return typeInfo._pParameter[parameterIndex]._defaultValue;
            }

            /** @brief 데이터에 적힌 센드 레벨입니다. 없으면 바닥 값입니다. */
            static float32 findBaseSendLevel( const AudioMixerDesc& desc, uint32 busIndex, const hashed_string& target )
            {
                for ( const AudioSendDesc& send : desc._listBus[busIndex]._listSend )
                {
                    if ( send._bus == target )
                        return send._levelDb;
                }
                return audio::kSilenceDb;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "AudioSnapshotMixer" );

    void AudioSnapshotMixer::startSnapshot( const AudioMixer& mixer, const hashed_string& name )
    {
        SnapshotState* pSnapshot = getOrCreateSnapshot( mixer, name );
        if ( pSnapshot == nullptr )
            return;
        pSnapshot->_target  = 1.0f;
        pSnapshot->_bDriven = false;
        // 가장 나중에 켠 것이 마지막에 적용된다.
        const SnapshotState moved = *pSnapshot;
        _listSnapshot.erase( _listSnapshot.begin() + ( pSnapshot - _listSnapshot.data() ) );
        _listSnapshot.push_back( moved );
    }

    void AudioSnapshotMixer::stopSnapshot( const hashed_string& name )
    {
        for ( SnapshotState& snapshot : _listSnapshot )
        {
            if ( snapshot._name == name )
            {
                snapshot._target  = 0.0f;
                snapshot._bDriven = false;
            }
        }
    }

    void AudioSnapshotMixer::setSnapshotIntensity( const AudioMixer& mixer, const hashed_string& name, float32 intensity )
    {
        SnapshotState* pSnapshot = getOrCreateSnapshot( mixer, name );
        if ( pSnapshot == nullptr )
            return;
        pSnapshot->_intensity = intensity;
        pSnapshot->_target    = intensity;
        pSnapshot->_bDriven   = true;
    }

    float32 AudioSnapshotMixer::getSnapshotIntensity( const hashed_string& name ) const
    {
        for ( const SnapshotState& snapshot : _listSnapshot )
        {
            if ( snapshot._name == name )
                return snapshot._intensity;
        }
        return 0.0f;
    }

    void AudioSnapshotMixer::setBusUserVolume( AudioMixer& mixer, const hashed_string& bus, float32 volume )
    {
        BusUserState& state = getOrCreateBusUserState( bus );
        state._volume       = MathUtil::clamp( volume, 0.0f, 1.0f );
        applyBusUserState( mixer, state );
    }

    void AudioSnapshotMixer::setBusMuted( AudioMixer& mixer, const hashed_string& bus, bool bMuted )
    {
        BusUserState& state = getOrCreateBusUserState( bus );
        state._bMuted       = bMuted;
        applyBusUserState( mixer, state );
    }

    void AudioSnapshotMixer::setBusSolo( AudioMixer& mixer, const hashed_string& bus, bool bSolo )
    {
        BusUserState& state = getOrCreateBusUserState( bus );
        state._bSolo        = bSolo;
        applyBusUserState( mixer, state );
    }

    void AudioSnapshotMixer::adoptMixer( AudioMixer& mixer )
    {
        for ( const BusUserState& state : _listBusUserState )
        {
            applyBusUserState( mixer, state );
        }
        for ( size_t snapshotIndex = 0; snapshotIndex < _listSnapshot.size(); )
        {
            SnapshotState& snapshot = _listSnapshot[snapshotIndex];
            snapshot._pDesc         = mixer.getDesc().findSnapshot( snapshot._name );
            if ( snapshot._pDesc == nullptr )
            {
                _listSnapshot.erase( _listSnapshot.begin() + static_cast<ptrdiff_t>( snapshotIndex ) );
                continue;
            }
            ++snapshotIndex;
        }
    }

    void AudioSnapshotMixer::update( AudioMixer& mixer )
    {
        // 세기를 옮기고 다 빠진 것을 지운다.
        for ( size_t snapshotIndex = 0; snapshotIndex < _listSnapshot.size(); )
        {
            SnapshotState& snapshot = _listSnapshot[snapshotIndex];
            if ( snapshot._bDriven == false && snapshot._intensity != snapshot._target )
            {
                const bool    bRising = snapshot._target > snapshot._intensity;
                const float32 seconds = bRising ? snapshot._pDesc->_fadeInSeconds : snapshot._pDesc->_fadeOutSeconds;
                const float32 move    = seconds <= 0.0f ? 1.0f : audio::kBlockSeconds / seconds;
                snapshot._intensity   = bRising ? MathUtil::min( snapshot._target, snapshot._intensity + move )
                                                : MathUtil::max( snapshot._target, snapshot._intensity - move );
            }
            const bool bSnapshotFinished = snapshot._intensity <= 0.0f && snapshot._target <= 0.0f && snapshot._bDriven == false;
            if ( bSnapshotFinished )
            {
                _listSnapshot.erase( _listSnapshot.begin() + static_cast<ptrdiff_t>( snapshotIndex ) );
                continue;
            }
            ++snapshotIndex;
        }

        const AudioMixerDesc& desc = mixer.getDesc();

        // 버스 오프셋: 세기 × dB 를 더한다.
        for ( uint32 busIndex = 0; busIndex < mixer.getBusCount(); ++busIndex )
        {
            mixer.setBusVolumeOffsetDb( busIndex, 0.0f );
        }
        for ( const SnapshotState& snapshot : _listSnapshot )
        {
            for ( const AudioSnapshotBusDesc& busVolume : snapshot._pDesc->_listBusVolume )
            {
                const int32 busIndex = mixer.findBusIndex( busVolume._bus );
                if ( busIndex >= 0 )
                {
                    const uint32 index = static_cast<uint32>( busIndex );
                    mixer.setBusVolumeOffsetDb( index, mixer.getBusVolumeOffsetDb( index ) + busVolume._volumeDb * snapshot._intensity );
                }
            }
        }

        // 센드와 이펙트 파라미터: 어느 스냅샷이든 가리키는 슬롯은 데이터 값에서 시작해 켠 순서대로 세기만큼 보간한다(모두 빠지면 데이터 값으로 돌아온다).
        for ( const AudioSnapshotDesc& anySnapshot : desc._listSnapshot )
        {
            for ( const AudioSnapshotSendDesc& sendItem : anySnapshot._listSend )
            {
                const int32 busIndex    = mixer.findBusIndex( sendItem._bus );
                const int32 targetIndex = mixer.findBusIndex( sendItem._target );
                if ( busIndex < 0 || targetIndex < 0 )
                    continue;
                float32 levelDb = AudioSnapshotMixerInternal::findBaseSendLevel( desc, static_cast<uint32>( busIndex ), sendItem._target );
                for ( const SnapshotState& snapshot : _listSnapshot )
                {
                    for ( const AudioSnapshotSendDesc& activeSend : snapshot._pDesc->_listSend )
                    {
                        if ( activeSend._bus == sendItem._bus && activeSend._target == sendItem._target )
                            levelDb += ( activeSend._levelDb - levelDb ) * snapshot._intensity;
                    }
                }
                (void)mixer.setSendLevelDb( static_cast<uint32>( busIndex ), static_cast<uint32>( targetIndex ), levelDb );
            }
            for ( const AudioSnapshotEffectDesc& effectItem : anySnapshot._listEffectParameter )
            {
                const int32 busIndex = mixer.findBusIndex( effectItem._bus );
                if ( busIndex < 0 )
                    continue;
                IAudioEffect*          pEffect     = mixer.findEffect( static_cast<uint32>( busIndex ), effectItem._effect );
                const AudioEffectDesc* pEffectDesc = desc.findEffect( static_cast<uint32>( busIndex ), effectItem._effect );
                if ( pEffect == nullptr || pEffectDesc == nullptr )
                    continue;
                const int32 parameterIndex = pEffect->findParameterIndex( effectItem._parameter );
                if ( parameterIndex < 0 )
                    continue;
                const AudioEffectTypeInfo&      typeInfo = pEffect->getTypeInfo();
                const AudioEffectParameterInfo& info     = typeInfo._pParameter[parameterIndex];
                float32                         value    = AudioSnapshotMixerInternal::findBaseEffectValue( *pEffectDesc, typeInfo, static_cast<uint32>( parameterIndex ) );
                for ( const SnapshotState& snapshot : _listSnapshot )
                {
                    for ( const AudioSnapshotEffectDesc& activeEffect : snapshot._pDesc->_listEffectParameter )
                    {
                        const bool bSame = activeEffect._bus == effectItem._bus && activeEffect._effect == effectItem._effect &&
                                           activeEffect._parameter == effectItem._parameter;
                        if ( bSame == false )
                            continue;
                        value = info._bLogarithmic ? AudioMath::lerpFrequency( value, activeEffect._value, snapshot._intensity )
                                                   : value + ( activeEffect._value - value ) * snapshot._intensity;
                    }
                }
                pEffect->setParameter( static_cast<uint32>( parameterIndex ), value );
            }
        }
    }

    AudioSnapshotMixer::SnapshotState* AudioSnapshotMixer::getOrCreateSnapshot( const AudioMixer& mixer, const hashed_string& name )
    {
        for ( SnapshotState& snapshot : _listSnapshot )
        {
            if ( snapshot._name == name )
                return &snapshot;
        }
        const AudioSnapshotDesc* pDesc = mixer.getDesc().findSnapshot( name );
        if ( pDesc == nullptr )
        {
            SW_LOG_ERROR( "Unknown audio snapshot '%#'", name.c_str() );
            return nullptr;
        }
        SnapshotState snapshot;
        snapshot._name  = name;
        snapshot._pDesc = pDesc;
        _listSnapshot.push_back( snapshot );
        return &_listSnapshot.back();
    }

    AudioSnapshotMixer::BusUserState& AudioSnapshotMixer::getOrCreateBusUserState( const hashed_string& bus )
    {
        for ( BusUserState& state : _listBusUserState )
        {
            if ( state._bus == bus )
                return state;
        }
        BusUserState state;
        state._bus = bus;
        _listBusUserState.push_back( state );
        return _listBusUserState.back();
    }

    void AudioSnapshotMixer::applyBusUserState( AudioMixer& mixer, const BusUserState& state )
    {
        const int32 busIndex = mixer.findBusIndex( state._bus );
        if ( busIndex < 0 )
            return;
        const uint32 index = static_cast<uint32>( busIndex );
        mixer.setBusUserVolume( index, state._volume );
        mixer.setBusMuted( index, state._bMuted || mixer.getDesc()._listBus[index]._bMuted );
        mixer.setBusSolo( index, state._bSolo || mixer.getDesc()._listBus[index]._bSolo );
    }
} // namespace sw
