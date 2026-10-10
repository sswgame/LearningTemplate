#include "pch.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixer.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Audio/DSP/AudioEffect.h"

namespace sw
{
    namespace
    {
        struct AudioEngineEventsInternal
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
    SW_LOG_CALLER( "AudioEngine" );

    float32 AudioEngine::getParameterValue( const hashed_string& name ) const
    {
        const auto it = _mapParameter.find( name );
        return it == _mapParameter.end() ? 0.0f : it->second._value;
    }

    float32 AudioEngine::getSnapshotIntensity( const hashed_string& name ) const
    {
        for ( const SnapshotState& snapshot : _listSnapshot )
        {
            if ( snapshot._name == name )
                return snapshot._intensity;
        }
        return 0.0f;
    }

    void AudioEngine::applySetLibrary( const Command& command )
    {
        if ( command._pLibrary == nullptr )
            _mapLibrary.erase( command._name );
        else
            _mapLibrary[command._name] = command._pLibrary;

        if ( command._pLibrary != nullptr )
        {
            for ( const AudioParameterDesc& desc : command._pLibrary->_listParameter )
            {
                const bool      bNew      = _mapParameter.find( desc._name ) == _mapParameter.end();
                ParameterState& parameter = _mapParameter[desc._name];
                parameter._minValue       = desc._minValue;
                parameter._maxValue       = desc._maxValue;
                parameter._seekSpeed      = desc._seekSpeed;
                if ( bNew )
                {
                    parameter._value  = desc._defaultValue;
                    parameter._target = desc._defaultValue;
                }
                parameter._value  = MathUtil::clamp( parameter._value, desc._minValue, desc._maxValue );
                parameter._target = MathUtil::clamp( parameter._target, desc._minValue, desc._maxValue );
            }
        }
        rebuildEventTable();
        rebuildAttenuationTable();
    }

    void AudioEngine::rebuildEventTable()
    {
        for ( auto& entry : _mapEventState )
        {
            entry.second._pDesc = nullptr;
            entry.second._pLibrary.reset();
        }
        for ( const auto& libraryEntry : _mapLibrary )
        {
            for ( const AudioEventDesc& event : libraryEntry.second->_listEvent )
            {
                EventState& state = _mapEventState[event._name];
                state._pDesc      = &event;
                state._pLibrary   = libraryEntry.second;
            }
        }
    }

    void AudioEngine::rebuildAttenuationTable()
    {
        _listAttenuation = _pMixer->getDesc()._listAttenuation;
        for ( const auto& libraryEntry : _mapLibrary )
        {
            for ( const AudioAttenuationDesc& attenuation : libraryEntry.second->_listAttenuation )
            {
                _listAttenuation.push_back( attenuation );
            }
        }
        for ( VoiceSlot& slot : _listVoice )
        {
            if ( slot._bInUse )
                slot._attenuationIndex = resolveAttenuationIndex( slot._attenuationName );
        }
    }

    int32 AudioEngine::resolveAttenuationIndex( const hashed_string& name ) const
    {
        if ( name.empty() )
            return -1;
        for ( size_t attenuationIndex = 0; attenuationIndex < _listAttenuation.size(); ++attenuationIndex )
        {
            if ( _listAttenuation[attenuationIndex]._name == name )
                return static_cast<int32>( attenuationIndex );
        }
        return -1;
    }

    int32 AudioEngine::allocateInstance()
    {
        for ( size_t instanceIndex = 0; instanceIndex < _listInstance.size(); ++instanceIndex )
        {
            if ( _listInstance[instanceIndex]._bInUse == false )
            {
                _listInstance[instanceIndex]         = EventInstance{};
                _listInstance[instanceIndex]._bInUse = true;
                return static_cast<int32>( instanceIndex );
            }
        }
        return -1;
    }

    void AudioEngine::applyPostEvent( const Command& command )
    {
        const auto stateIt = _mapEventState.find( command._name );
        if ( stateIt == _mapEventState.end() || stateIt->second._pDesc == nullptr )
            return;
        EventState&           state = stateIt->second;
        const AudioEventDesc& event = *state._pDesc;

        // 쿨다운 — 오디오 시각(렌더한 프레임)으로 잰다.
        const uint64 cooldownFrames = static_cast<uint64>( event._cooldownSeconds * static_cast<float32>( audio::kSampleRate ) );
        if ( cooldownFrames > 0 && state._bPostedOnce && _renderedFrameCount - state._lastPostFrame < cooldownFrames )
        {
            ++_droppedEventCount;
            return;
        }

        // 동시 재생 상한과 뺏기(Wwise Playback Limit · 언리얼 Sound Concurrency).
        if ( event._maxInstances > 0 )
        {
            uint32 liveCount   = 0;
            int32  victimIndex = -1;
            for ( size_t instanceIndex = 0; instanceIndex < _listInstance.size(); ++instanceIndex )
            {
                const EventInstance& instance = _listInstance[instanceIndex];
                if ( instance._bInUse == false || instance._bStopping || instance._pDesc == nullptr || instance._pDesc->_name != event._name )
                    continue;
                ++liveCount;
                if ( victimIndex < 0 )
                {
                    victimIndex = static_cast<int32>( instanceIndex );
                    continue;
                }
                const EventInstance& victim  = _listInstance[static_cast<size_t>( victimIndex )];
                bool                 bBetter = false;
                switch ( event._steal )
                {
                    case AudioStealPolicy::Reject:
                    {
                        break;
                    }
                    case AudioStealPolicy::Oldest:
                    {
                        bBetter = instance._startFrame < victim._startFrame ||
                                  ( instance._startFrame == victim._startFrame && instance._playingID < victim._playingID );
                        break;
                    }
                    case AudioStealPolicy::Quietest:
                    {
                        bBetter = instance._audibility < victim._audibility;
                        break;
                    }
                    case AudioStealPolicy::Farthest:
                    {
                        bBetter = instance._distance > victim._distance;
                        break;
                    }
                }
                if ( bBetter )
                    victimIndex = static_cast<int32>( instanceIndex );
            }
            if ( liveCount >= event._maxInstances )
            {
                if ( event._steal == AudioStealPolicy::Reject || victimIndex < 0 )
                {
                    ++_droppedEventCount;
                    return;
                }
                EventInstance& victim = _listInstance[static_cast<size_t>( victimIndex )];
                victim._bStopping     = true;
                stopVoices( victim._playingID, 0, event._fadeOutSeconds );
            }
        }

        const int32 instanceIndex = allocateInstance();
        if ( instanceIndex < 0 )
        {
            ++_droppedEventCount;
            return;
        }
        ++_playedEventCount;
        state._bPostedOnce   = true;
        state._lastPostFrame = _renderedFrameCount;

        EventInstance& instance = _listInstance[static_cast<size_t>( instanceIndex )];
        instance._pLibrary      = state._pLibrary;
        instance._pDesc         = &event;
        instance._playingID     = command._playingID;
        instance._emitterID     = command._emitterID;
        instance._startFrame    = _renderedFrameCount;

        // 재생 한 번의 볼륨 · 피치(레이어 보이스는 같은 값을 나눈다).
        const float32 volumeDb  = _random.nextRange( event._volumeDbMin, event._volumeDbMax );
        const float32 semitones = _random.nextRange( event._pitchMin, event._pitchMax );

        const uint32 clipCount = static_cast<uint32>( event._listClip.size() );
        uint32       firstClip = 0;
        uint32       pickCount = 1;
        switch ( event._container )
        {
            case AudioContainerType::Random:
            {
                // 가중치로 하나 — 클립이 둘 이상이면 바로 앞의 것을 뺀다.
                const bool bSkipLast = event._bAvoidRepeat && clipCount > 1 && state._lastRandomIndex >= 0;
                float32    total     = 0.0f;
                for ( uint32 clipIndex = 0; clipIndex < clipCount; ++clipIndex )
                {
                    if ( bSkipLast && static_cast<int32>( clipIndex ) == state._lastRandomIndex )
                        continue;
                    total += MathUtil::max( 0.0f, event._listClip[clipIndex]._weight );
                }
                float32 pick = _random.nextUnit() * total;
                firstClip    = 0;
                for ( uint32 clipIndex = 0; clipIndex < clipCount; ++clipIndex )
                {
                    if ( bSkipLast && static_cast<int32>( clipIndex ) == state._lastRandomIndex )
                        continue;
                    firstClip = clipIndex;
                    pick -= MathUtil::max( 0.0f, event._listClip[clipIndex]._weight );
                    if ( pick < 0.0f )
                        break;
                }
                state._lastRandomIndex = static_cast<int32>( firstClip );
                break;
            }
            case AudioContainerType::Sequence:
            {
                firstClip             = state._sequenceCursor % clipCount;
                state._sequenceCursor = ( firstClip + 1 ) % clipCount;
                break;
            }
            case AudioContainerType::Layer:
            {
                firstClip = 0;
                pickCount = clipCount;
                break;
            }
        }

        const uint32 busIndex         = resolveBusIndex( event._bus );
        const int32  attenuationIndex = resolveAttenuationIndex( event._attenuation );
        for ( uint32 pickIndex = 0; pickIndex < pickCount; ++pickIndex )
        {
            const AudioClipEntry& clip  = event._listClip[firstClip + pickIndex];
            VoiceSlot*            pSlot = allocateVoice();
            if ( pSlot == nullptr )
            {
                SW_LOG_WARNING( "Voice pool exhausted - dropped event '%#'", event._name.c_str() );
                break;
            }
            pSlot->_playingID        = command._playingID;
            pSlot->_emitterID        = command._emitterID;
            pSlot->_clipPath         = clip._path;
            pSlot->_busName          = event._bus;
            pSlot->_busIndex         = busIndex;
            pSlot->_attenuationName  = event._attenuation;
            pSlot->_attenuationIndex = attenuationIndex;
            pSlot->_instanceIndex    = instanceIndex;
            pSlot->_volume           = AudioMath::dbToLinear( volumeDb + clip._volumeDb );
            pSlot->_pitchRatio       = AudioMath::semitonesToRatio( semitones );
            pSlot->_fadeInSeconds    = event._fadeInSeconds;
            pSlot->_priority         = event._priority;
            pSlot->_virtualMode      = event._virtual;
            pSlot->_bLoop            = event._bLoop;
            pSlot->_bWaitingForClip  = true;
            startWaitingVoice( *pSlot );
        }
    }

    void AudioEngine::updateParameters()
    {
        const float32 step = audio::kBlockSeconds;
        for ( auto& entry : _mapParameter )
        {
            ParameterState& parameter = entry.second;
            if ( parameter._value == parameter._target )
                continue;
            const float32 maxMove = parameter._seekSpeed * step;
            const float32 delta   = parameter._target - parameter._value;
            if ( parameter._seekSpeed <= 0.0f || MathUtil::abs( delta ) <= maxMove )
                parameter._value = parameter._target;
            else
                parameter._value += delta > 0.0f ? maxMove : -maxMove;
        }
    }

    float32 AudioEngine::findParameterValue( AudioEmitterID emitterID, const hashed_string& name ) const
    {
        if ( emitterID != 0 )
        {
            const auto emitterIt = _mapEmitter.find( emitterID );
            if ( emitterIt != _mapEmitter.end() )
            {
                for ( const EmitterParameter& parameter : emitterIt->second._listParameter )
                {
                    if ( parameter._name == name )
                        return parameter._value;
                }
            }
        }
        return getParameterValue( name );
    }

    AudioEngine::SnapshotState* AudioEngine::findOrAddSnapshot( const hashed_string& name )
    {
        for ( SnapshotState& snapshot : _listSnapshot )
        {
            if ( snapshot._name == name )
                return &snapshot;
        }
        const AudioSnapshotDesc* pDesc = _pMixer->getDesc().findSnapshot( name );
        if ( pDesc == nullptr )
        {
            SW_LOG_WARNING( "Unknown audio snapshot '%#'", name.c_str() );
            return nullptr;
        }
        SnapshotState snapshot;
        snapshot._name  = name;
        snapshot._pDesc = pDesc;
        _listSnapshot.push_back( snapshot );
        return &_listSnapshot.back();
    }

    void AudioEngine::rebindSnapshots()
    {
        for ( size_t snapshotIndex = 0; snapshotIndex < _listSnapshot.size(); )
        {
            SnapshotState& snapshot = _listSnapshot[snapshotIndex];
            snapshot._pDesc         = _pMixer->getDesc().findSnapshot( snapshot._name );
            if ( snapshot._pDesc == nullptr )
            {
                _listSnapshot.erase( _listSnapshot.begin() + static_cast<ptrdiff_t>( snapshotIndex ) );
                continue;
            }
            ++snapshotIndex;
        }
    }

    void AudioEngine::updateSnapshots()
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
            if ( snapshot._intensity <= 0.0f && snapshot._target <= 0.0f && snapshot._bDriven == false )
            {
                _listSnapshot.erase( _listSnapshot.begin() + static_cast<ptrdiff_t>( snapshotIndex ) );
                continue;
            }
            ++snapshotIndex;
        }

        AudioMixer&           mixer = *_pMixer;
        const AudioMixerDesc& desc  = mixer.getDesc();

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

        // 센드 · 이펙트 파라미터: 어느 스냅샷이든 가리키는 칸은 데이터 값에서 시작해 켠 순서대로 세기만큼 보간한다(다 빠지면 데이터 값으로 돌아온다).
        for ( const AudioSnapshotDesc& anySnapshot : desc._listSnapshot )
        {
            for ( const AudioSnapshotSendDesc& sendItem : anySnapshot._listSend )
            {
                const int32 busIndex    = mixer.findBusIndex( sendItem._bus );
                const int32 targetIndex = mixer.findBusIndex( sendItem._target );
                if ( busIndex < 0 || targetIndex < 0 )
                    continue;
                float32 levelDb = AudioEngineEventsInternal::findBaseSendLevel( desc, static_cast<uint32>( busIndex ), sendItem._target );
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
                float32                         value    = AudioEngineEventsInternal::findBaseEffectValue( *pEffectDesc, typeInfo, static_cast<uint32>( parameterIndex ) );
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

    void AudioEngine::selectRealVoices()
    {
        const AudioMixerDesc& desc      = _pMixer->getDesc();
        const float32         inaudible = AudioMath::dbToLinear( desc._inaudibleDb );
        uint32                keepCount = 0;
        _listVoiceOrder.clear();
        for ( uint32 voiceIndex = 0; voiceIndex < _listVoice.size(); ++voiceIndex )
        {
            VoiceSlot& slot = _listVoice[voiceIndex];
            if ( slot._bInUse == false || slot._bWaitingForClip )
                continue;
            slot._bVirtual = false;
            if ( slot._voice.isPaused() )
                continue;
            if ( slot._virtualMode == AudioVirtualMode::KeepReal )
            {
                ++keepCount;
                continue;
            }
            if ( slot._audibility < inaudible )
            {
                // 들리지 않는다 — 가상(시간만 흐름)이거나, 짧은 원샷이면 멈춘다.
                if ( slot._virtualMode == AudioVirtualMode::Stop )
                    freeVoice( slot );
                else
                    slot._bVirtual = true;
                continue;
            }
            _listVoiceOrder.push_back( voiceIndex );
        }

        const uint32 budget = desc._maxRealVoiceCount > keepCount ? desc._maxRealVoiceCount - keepCount : 0u;
        if ( _listVoiceOrder.size() <= budget )
            return;

        // 우선순위 → 들림 → 먼저 낸 것 순서로 앞의 budget 개만 섞는다.
        std::sort( _listVoiceOrder.begin(), _listVoiceOrder.end(), [this]( uint32 lhsIndex, uint32 rhsIndex )
        {
            const VoiceSlot& lhs = _listVoice[lhsIndex];
            const VoiceSlot& rhs = _listVoice[rhsIndex];
            if ( lhs._priority != rhs._priority )
                return lhs._priority > rhs._priority;
            if ( lhs._audibility != rhs._audibility )
                return lhs._audibility > rhs._audibility;
            return lhs._playingID < rhs._playingID;
        } );
        for ( size_t orderIndex = budget; orderIndex < _listVoiceOrder.size(); ++orderIndex )
        {
            VoiceSlot& slot = _listVoice[_listVoiceOrder[orderIndex]];
            if ( slot._virtualMode == AudioVirtualMode::Stop )
            {
                // 들리는 원샷을 뺏을 때는 한 블록 동안 줄여 끊는다(클릭 없음) — 이 블록은 섞는다.
                slot._voice.setFade( 0.0f, audio::kBlockFrameCount, 0, true );
                continue;
            }
            slot._bVirtual = true;
        }
    }

    void AudioEngine::updateInstances()
    {
        for ( EventInstance& instance : _listInstance )
        {
            instance._audibility = -1.0f;
            instance._distance   = MathUtil::kMaxFloat;
        }
        for ( const VoiceSlot& slot : _listVoice )
        {
            if ( slot._bInUse == false || slot._instanceIndex < 0 )
                continue;
            EventInstance& instance = _listInstance[static_cast<size_t>( slot._instanceIndex )];
            instance._audibility    = MathUtil::max( instance._audibility, slot._audibility );
            instance._distance      = MathUtil::min( instance._distance, slot._distance );
        }
        for ( EventInstance& instance : _listInstance )
        {
            if ( instance._bInUse && instance._audibility < 0.0f )
                instance = EventInstance{};
        }
    }
} // namespace sw
