#include "pch.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixer.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioTypes.h"

namespace sw
{
    SW_LOG_CALLER( "AudioEngine" );

    float32 AudioEngine::getParameterValue( const hashed_string& name ) const
    {
        const auto it = _mapParameter.find( name );
        return it == _mapParameter.end() ? 0.0f : it->second._value;
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

    int32 AudioEngine::findStealVictim( const AudioEventDesc& event, uint32& outLiveCount ) const
    {
        outLiveCount      = 0;
        int32 victimIndex = -1;
        for ( size_t instanceIndex = 0; instanceIndex < _listInstance.size(); ++instanceIndex )
        {
            const EventInstance& instance                = _listInstance[instanceIndex];
            const bool           bNotLiveInstanceOfEvent = instance._bInUse == false || instance._bStopping || instance._pDesc == nullptr || instance._pDesc->_name != event._name;
            if ( bNotLiveInstanceOfEvent )
                continue;
            ++outLiveCount;
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
                    bBetter = instance._startFrame < victim._startFrame || ( instance._startFrame == victim._startFrame && instance._playingID < victim._playingID );
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
        return victimIndex;
    }

    bool AudioEngine::tryAdmitEvent( const EventState& state, const AudioEventDesc& event )
    {
        // 쿨다운은 오디오 시각(렌더한 프레임)으로 측정한다.
        const uint64 cooldownFrames = static_cast<uint64>( event._cooldownSeconds * static_cast<float32>( audio::kSampleRate ) );
        const bool   bInCooldown    = cooldownFrames > 0 && state._bPostedOnce && _renderedFrameCount - state._lastPostFrame < cooldownFrames;
        if ( bInCooldown )
        {
            ++_droppedEventCount;
            return false;
        }

        // 동시 재생 상한과 뺏기다(Wwise 의 Playback Limit, 언리얼의 Sound Concurrency).
        if ( event._maxInstances > 0 )
        {
            uint32      liveCount{ 0 };
            const int32 victimIndex = findStealVictim( event, liveCount );
            if ( liveCount >= event._maxInstances )
            {
                if ( event._steal == AudioStealPolicy::Reject || victimIndex < 0 )
                {
                    ++_droppedEventCount;
                    return false;
                }
                EventInstance& victim = _listInstance[static_cast<size_t>( victimIndex )];
                victim._bStopping     = true;
                stopVoices( victim._playingID, 0, event._fadeOutSeconds );
            }
        }
        return true;
    }

    void AudioEngine::pickEventClips( EventState& inoutState, const AudioEventDesc& event, uint32& outFirstClip, uint32& outPickCount )
    {
        const uint32 clipCount = static_cast<uint32>( event._listClip.size() );
        outFirstClip           = 0;
        outPickCount           = 1;
        switch ( event._container )
        {
            case AudioContainerType::Random:
            {
                // 가중치로 하나를 고른다. 클립이 둘 이상이면 바로 앞에 낸 것은 뺀다.
                const bool bSkipLast = event._bAvoidRepeat && clipCount > 1 && inoutState._lastRandomIndex >= 0;
                float32    total     = 0.0f;
                for ( uint32 clipIndex = 0; clipIndex < clipCount; ++clipIndex )
                {
                    if ( bSkipLast && static_cast<int32>( clipIndex ) == inoutState._lastRandomIndex )
                        continue;
                    total += MathUtil::max( 0.0f, event._listClip[clipIndex]._weight );
                }
                float32 pick = _random.nextFloat() * total;
                for ( uint32 clipIndex = 0; clipIndex < clipCount; ++clipIndex )
                {
                    if ( bSkipLast && static_cast<int32>( clipIndex ) == inoutState._lastRandomIndex )
                        continue;
                    outFirstClip = clipIndex;
                    pick -= MathUtil::max( 0.0f, event._listClip[clipIndex]._weight );
                    if ( pick < 0.0f )
                        break;
                }
                inoutState._lastRandomIndex = static_cast<int32>( outFirstClip );
                break;
            }
            case AudioContainerType::Sequence:
            {
                outFirstClip               = inoutState._sequenceCursor % clipCount;
                inoutState._sequenceCursor = ( outFirstClip + 1 ) % clipCount;
                break;
            }
            case AudioContainerType::Layer:
            {
                outFirstClip = 0;
                outPickCount = clipCount;
                break;
            }
        }
    }

    void AudioEngine::applyPostEvent( const Command& command )
    {
        const auto stateIt = _mapEventState.find( command._name );
        if ( stateIt == _mapEventState.end() || stateIt->second._pDesc == nullptr )
            return;
        EventState&           state = stateIt->second;
        const AudioEventDesc& event = *state._pDesc;
        if ( tryAdmitEvent( state, event ) == false )
            return;

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

        // 재생 한 번의 볼륨과 피치다(레이어 보이스는 같은 값을 나눠 쓴다).
        const float32 volumeDb  = _random.nextRange( event._volumeDbMin, event._volumeDbMax );
        const float32 semitones = _random.nextRange( event._pitchMin, event._pitchMax );

        uint32 firstClip = 0;
        uint32 pickCount = 1;
        pickEventClips( state, event, firstClip, pickCount );

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
            // 자막은 버려지지 않고 보이스를 얻은 클립만 낸다 — 쿨다운 · 상한으로 버린 재생은 위에서 이미 돌아갔다.
            if ( clip._subtitleKey.empty() == false )
                _published.addSubtitleCue( AudioSubtitleCue{ event._subtitleSpeakerKey, clip._subtitleKey, command._playingID } );
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
                // 들리지 않는다. 가상(시간만 흐름)으로 두거나, 짧은 원샷이면 멈춘다.
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
        VectorUtil::sort( _listVoiceOrder, [this]( uint32 lhsIndex, uint32 rhsIndex )
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
                // 들리는 원샷을 뺏을 때는 한 블록 동안 줄여서 끊는다(클릭 없음). 이 블록은 그대로 섞는다.
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
