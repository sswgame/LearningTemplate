#include "pch.h"

#include "Engine/Audio/AudioEngine.h"

#include "Engine/Audio/AudioMixer.h"
#include "Engine/Audio/AudioMixerDesc.h"

namespace sw
{
    SW_LOG_CALLER( "AudioEngine" );

    AudioEngine::AudioEngine()
        : _clipStore{}
        , _listPendingCommand{}
        , _listApplyingCommand{}
        , _mapGameLibrary{}
        , _pGameMixerDesc{ nullptr }
        , _commandMutex{}
        , _nextPlayingId{ 1 }
        , _pMixer{ nullptr }
        , _listVoice{}
        , _listVoiceOrder{}
        , _listInstance{}
        , _listBusUserState{}
        , _listBlockOutput{}
        , _listVoiceScratch{}
        , _listAttenuation{}
        , _listSnapshot{}
        , _pMusic{ nullptr }
        , _musicPlayingId{ 0 }
        , _musicSegmentStart{ 0 }
        , _musicPendingFrame{ 0 }
        , _musicSegment{ -1 }
        , _musicPendingSegment{ -1 }
        , _mapLibrary{}
        , _mapEventState{}
        , _mapParameter{}
        , _mapEmitter{}
        , _arrListener{}
        , _random{}
        , _renderedFrameCount{ 0 }
        , _appliedPlayingId{ 0 }
        , _blockCursor{ audio::kBlockFrameCount }
        , _listPublishedPlaying{}
        , _publishedStats{}
        , _publishedMusic{}
        , _publishedAppliedId{ 0 }
        , _publishMutex{}
        , _bInitialized{ false }
    {
    }

    AudioEngine::~AudioEngine()
    {
        shutdown();
    }

    bool AudioEngine::initialize( const AudioMixerDesc& mixerDesc )
    {
        shared_ptr<AudioMixer> pMixer = make_shared<AudioMixer>();
        if ( pMixer->initialize( mixerDesc ) == false )
            return false;
        _pMixer         = std::move( pMixer );
        _pGameMixerDesc = make_shared<AudioMixerDesc>( mixerDesc );
        _listVoice.clear();
        _listVoice.resize( mixerDesc._maxVoiceCount );
        _listVoiceOrder.reserve( mixerDesc._maxVoiceCount );
        _listInstance.clear();
        _listInstance.resize( mixerDesc._maxVoiceCount );
        _listBlockOutput.assign( static_cast<size_t>( audio::kBlockFrameCount ) * audio::kChannelCount, 0.0f );
        _listVoiceScratch.assign( static_cast<size_t>( audio::kBlockFrameCount ) * audio::kChannelCount, 0.0f );
        _blockCursor        = audio::kBlockFrameCount;
        _renderedFrameCount = 0;
        rebuildAttenuationTable();
        _bInitialized.store( true, std::memory_order_release );
        return true;
    }

    void AudioEngine::shutdown()
    {
        if ( _bInitialized.exchange( false, std::memory_order_acq_rel ) == false )
            return;
        {
            std::scoped_lock<mutex> lock{ _commandMutex };
            _listPendingCommand.clear();
        }
        _listApplyingCommand.clear();
        _listVoice.clear();
        _listInstance.clear();
        _listSnapshot.clear();
        _mapLibrary.clear();
        _mapEventState.clear();
        _mapGameLibrary.clear();
        _mapEmitter.clear();
        _pMusic.reset();
        _musicSegment        = -1;
        _musicPendingSegment = -1;
        _pMixer.reset();
        {
            std::scoped_lock<mutex> lock{ _publishMutex };
            _listPublishedPlaying.clear();
            _publishedStats = AudioEngineStats{};
        }
    }

    bool AudioEngine::loadMixer( const AudioMixerDesc& mixerDesc )
    {
        shared_ptr<AudioMixer> pMixer = make_shared<AudioMixer>();
        if ( pMixer->initialize( mixerDesc ) == false )
            return false;
        _pGameMixerDesc = make_shared<AudioMixerDesc>( mixerDesc );
        Command command;
        command._type   = CommandType::SwapMixer;
        command._pMixer = std::move( pMixer );
        pushCommand( std::move( command ) );
        return true;
    }

    bool AudioEngine::loadEventLibrary( const hashed_string& libraryName, const AudioEventLibrary& library )
    {
        const string_view sourceName = libraryName.view();
        if ( library.validate( sourceName ) == false )
            return false;

        bool bValid = true;
        for ( const AudioEventDesc& event : library._listEvent )
        {
            if ( _pGameMixerDesc == nullptr || _pGameMixerDesc->findBusIndex( event._bus ) < 0 )
            {
                SW_LOG_ERROR( "%#: event '%#' sends to unknown bus '%#'", sourceName, event._name.c_str(), event._bus.c_str() );
                bValid = false;
            }
            if ( event._attenuation.empty() == false )
            {
                bool bFound = ( _pGameMixerDesc != nullptr && _pGameMixerDesc->findAttenuation( event._attenuation ) != nullptr );
                for ( const AudioAttenuationDesc& attenuation : library._listAttenuation )
                    bFound = bFound || attenuation._name == event._attenuation;
                for ( const auto& entry : _mapGameLibrary )
                {
                    for ( const AudioAttenuationDesc& attenuation : entry.second->_listAttenuation )
                        bFound = bFound || attenuation._name == event._attenuation;
                }
                if ( bFound == false )
                {
                    SW_LOG_ERROR( "%#: event '%#' uses unknown attenuation '%#'", sourceName, event._name.c_str(), event._attenuation.c_str() );
                    bValid = false;
                }
            }
        }
        for ( const auto& entry : _mapGameLibrary )
        {
            if ( entry.first == libraryName )
                continue;
            for ( const AudioEventDesc& event : library._listEvent )
            {
                if ( entry.second->findEvent( event._name ) != nullptr )
                {
                    SW_LOG_ERROR( "%#: event '%#' is already declared by '%#'", sourceName, event._name.c_str(), entry.first.c_str() );
                    bValid = false;
                }
            }
            for ( const AudioParameterDesc& parameter : library._listParameter )
            {
                if ( entry.second->findParameter( parameter._name ) != nullptr )
                {
                    SW_LOG_ERROR( "%#: parameter '%#' is already declared by '%#'", sourceName, parameter._name.c_str(), entry.first.c_str() );
                    bValid = false;
                }
            }
        }
        if ( bValid == false )
            return false;

        shared_ptr<const AudioEventLibrary> pLibrary = make_shared<AudioEventLibrary>( library );
        _mapGameLibrary[libraryName]                 = pLibrary;
        // 뱅크를 올리듯 클립 디코드를 미리 걸어 둔다 — 첫 재생이 디코드를 기다리지 않게.
        for ( const AudioEventDesc& event : pLibrary->_listEvent )
        {
            for ( const AudioClipEntry& clip : event._listClip )
                _clipStore.requestClip( clip._path );
        }
        Command command;
        command._type     = CommandType::SetLibrary;
        command._name     = libraryName;
        command._pLibrary = std::move( pLibrary );
        pushCommand( std::move( command ) );
        return true;
    }

    void AudioEngine::unloadEventLibrary( const hashed_string& libraryName )
    {
        if ( _mapGameLibrary.erase( libraryName ) == 0 )
            return;
        Command command;
        command._type = CommandType::SetLibrary;
        command._name = libraryName;
        pushCommand( std::move( command ) );
    }

    bool AudioEngine::hasEvent( const hashed_string& eventName ) const
    {
        for ( const auto& entry : _mapGameLibrary )
        {
            if ( entry.second->findEvent( eventName ) != nullptr )
                return true;
        }
        return false;
    }

    void AudioEngine::setRandomSeed( uint64 seed )
    {
        Command command;
        command._type = CommandType::SetRandomSeed;
        command._seed = seed;
        pushCommand( std::move( command ) );
    }

    AudioPlayingId AudioEngine::postEvent( const hashed_string& eventName, AudioEmitterId emitterId )
    {
        const AudioEventDesc* pEvent = nullptr;
        for ( const auto& entry : _mapGameLibrary )
        {
            pEvent = entry.second->findEvent( eventName );
            if ( pEvent != nullptr )
                break;
        }
        if ( pEvent == nullptr )
        {
            SW_LOG_WARNING( "Unknown audio event '%#'", eventName.c_str() );
            return 0;
        }
        for ( const AudioClipEntry& clip : pEvent->_listClip )
            _clipStore.requestClip( clip._path );

        Command command;
        command._type      = CommandType::PostEvent;
        command._name      = eventName;
        command._emitterId = emitterId;
        // id 는 명령을 쌓을 때(잠금 안에서) 매긴다 — 큐 순서와 id 순서가 같아야 "아직 처리되지 않은 id" 판정이 맞다.
        std::scoped_lock<mutex> lock{ _commandMutex };
        command._playingId             = _nextPlayingId.fetch_add( 1 );
        const AudioPlayingId playingId = command._playingId;
        _listPendingCommand.push_back( std::move( command ) );
        return playingId;
    }

    AudioPlayingId AudioEngine::playClip( const hashed_string& path, const hashed_string& bus, const AudioClipPlayParams& params )
    {
        if ( path.empty() )
            return 0;
        _clipStore.requestClip( path );
        Command command;
        command._type       = CommandType::PlayClip;
        command._path       = path;
        command._name       = bus;
        command._clipParams = params;
        std::scoped_lock<mutex> lock{ _commandMutex };
        command._playingId             = _nextPlayingId.fetch_add( 1 );
        const AudioPlayingId playingId = command._playingId;
        _listPendingCommand.push_back( std::move( command ) );
        return playingId;
    }

    void AudioEngine::stop( AudioPlayingId playingId, float32 fadeSeconds )
    {
        Command command;
        command._type      = CommandType::Stop;
        command._playingId = playingId;
        command._value     = fadeSeconds;
        pushCommand( std::move( command ) );
    }

    void AudioEngine::stopEmitter( AudioEmitterId emitterId, float32 fadeSeconds )
    {
        Command command;
        command._type      = CommandType::StopEmitter;
        command._emitterId = emitterId;
        command._value     = fadeSeconds;
        pushCommand( std::move( command ) );
    }

    void AudioEngine::setPaused( AudioPlayingId playingId, bool bPaused )
    {
        Command command;
        command._type      = CommandType::SetPaused;
        command._playingId = playingId;
        command._bFlag     = bPaused;
        pushCommand( std::move( command ) );
    }

    AudioPlayingId AudioEngine::startMusic( shared_ptr<const AudioMusicDesc> pMusic, float32 fadeSeconds )
    {
        if ( pMusic == nullptr )
            return 0;
        for ( const AudioMusicSegmentDesc& segment : pMusic->_listSegment )
        {
            for ( const AudioMusicLayerDesc& layer : segment._listLayer )
                _clipStore.requestClip( layer._path );
        }
        for ( const AudioMusicTransitionDesc& transition : pMusic->_listTransition )
            _clipStore.requestClip( transition._stinger );
        Command command;
        command._type   = CommandType::StartMusic;
        command._pMusic = std::move( pMusic );
        command._value  = fadeSeconds;
        std::scoped_lock<mutex> lock{ _commandMutex };
        command._playingId             = _nextPlayingId.fetch_add( 1 );
        const AudioPlayingId playingId = command._playingId;
        _listPendingCommand.push_back( std::move( command ) );
        return playingId;
    }

    void AudioEngine::setMusicSegment( const hashed_string& segment )
    {
        Command command;
        command._type = CommandType::SetMusicSegment;
        command._name = segment;
        pushCommand( std::move( command ) );
    }

    void AudioEngine::stopMusic( float32 fadeSeconds )
    {
        Command command;
        command._type  = CommandType::StopMusic;
        command._value = fadeSeconds;
        pushCommand( std::move( command ) );
    }

    AudioMusicStatus AudioEngine::getMusicStatus() const
    {
        std::scoped_lock<mutex> lock{ _publishMutex };
        return _publishedMusic;
    }

    void AudioEngine::setBusUserVolume( const hashed_string& bus, float32 volume )
    {
        Command command;
        command._type  = CommandType::SetBusUserVolume;
        command._name  = bus;
        command._value = volume;
        pushCommand( std::move( command ) );
    }

    void AudioEngine::setBusMuted( const hashed_string& bus, bool bMuted )
    {
        Command command;
        command._type  = CommandType::SetBusMuted;
        command._name  = bus;
        command._bFlag = bMuted;
        pushCommand( std::move( command ) );
    }

    void AudioEngine::setBusSolo( const hashed_string& bus, bool bSolo )
    {
        Command command;
        command._type  = CommandType::SetBusSolo;
        command._name  = bus;
        command._bFlag = bSolo;
        pushCommand( std::move( command ) );
    }

    void AudioEngine::setParameter( const hashed_string& name, float32 value )
    {
        Command command;
        command._type  = CommandType::SetParameter;
        command._name  = name;
        command._value = value;
        pushCommand( std::move( command ) );
    }

    void AudioEngine::setEmitterParameter( AudioEmitterId emitterId, const hashed_string& name, float32 value )
    {
        if ( emitterId == 0 )
            return;
        Command command;
        command._type      = CommandType::SetEmitterParameter;
        command._emitterId = emitterId;
        command._name      = name;
        command._value     = value;
        pushCommand( std::move( command ) );
    }

    void AudioEngine::startSnapshot( const hashed_string& name )
    {
        Command command;
        command._type = CommandType::StartSnapshot;
        command._name = name;
        pushCommand( std::move( command ) );
    }

    void AudioEngine::stopSnapshot( const hashed_string& name )
    {
        Command command;
        command._type = CommandType::StopSnapshot;
        command._name = name;
        pushCommand( std::move( command ) );
    }

    void AudioEngine::setSnapshotIntensity( const hashed_string& name, float32 intensity )
    {
        Command command;
        command._type  = CommandType::SetSnapshotIntensity;
        command._name  = name;
        command._value = MathUtil::saturate( intensity );
        pushCommand( std::move( command ) );
    }

    void AudioEngine::setListener( uint32 listenerIndex, const AudioListenerState& state )
    {
        if ( listenerIndex >= kMaxListenerCount )
            return;
        Command command;
        command._type     = CommandType::SetListener;
        command._index    = listenerIndex;
        command._listener = state;
        pushCommand( std::move( command ) );
    }

    void AudioEngine::setEmitter( AudioEmitterId emitterId, const float3& position, const float3& velocity )
    {
        if ( emitterId == 0 )
            return;
        Command command;
        command._type      = CommandType::SetEmitter;
        command._emitterId = emitterId;
        command._position  = position;
        command._velocity  = velocity;
        pushCommand( std::move( command ) );
    }

    void AudioEngine::setEmitterOcclusion( AudioEmitterId emitterId, float32 occlusion )
    {
        if ( emitterId == 0 )
            return;
        Command command;
        command._type      = CommandType::SetEmitterOcclusion;
        command._emitterId = emitterId;
        command._value     = MathUtil::saturate( occlusion );
        pushCommand( std::move( command ) );
    }

    void AudioEngine::removeEmitter( AudioEmitterId emitterId )
    {
        Command command;
        command._type      = CommandType::RemoveEmitter;
        command._emitterId = emitterId;
        pushCommand( std::move( command ) );
    }

    bool AudioEngine::isPlaying( AudioPlayingId playingId ) const
    {
        if ( playingId == 0 )
            return false;
        // 아직 오디오 스레드가 보지 못한 재생은 살아 있는 것으로 친다.
        if ( playingId > _publishedAppliedId.load( std::memory_order_acquire ) )
            return playingId < _nextPlayingId.load( std::memory_order_acquire );
        std::scoped_lock<mutex> lock{ _publishMutex };
        return std::binary_search( _listPublishedPlaying.begin(), _listPublishedPlaying.end(), playingId );
    }

    AudioEngineStats AudioEngine::getStats() const
    {
        std::scoped_lock<mutex> lock{ _publishMutex };
        return _publishedStats;
    }

    float32 AudioEngine::getBusPeak( const hashed_string& bus ) const
    {
        if ( _pMixer == nullptr )
            return 0.0f;
        const int32 busIndex = _pMixer->findBusIndex( bus );
        return busIndex < 0 ? 0.0f : _pMixer->getBusPeak( static_cast<uint32>( busIndex ) );
    }

    void AudioEngine::pushCommand( Command&& command )
    {
        std::scoped_lock<mutex> lock{ _commandMutex };
        _listPendingCommand.push_back( std::move( command ) );
    }

    void AudioEngine::render( float32* pOutput, uint32 frameCount )
    {
        if ( pOutput == nullptr )
            return;
        if ( isInitialized() == false || _pMixer == nullptr )
        {
            Memory::set( pOutput, 0, static_cast<size_t>( frameCount ) * audio::kChannelCount * sizeof( float32 ) );
            return;
        }

        uint32 written = 0;
        while ( written < frameCount )
        {
            if ( _blockCursor >= audio::kBlockFrameCount )
            {
                renderBlock();
                _blockCursor = 0;
            }
            const uint32 copyFrames = MathUtil::min( frameCount - written, audio::kBlockFrameCount - _blockCursor );
            Memory::copy( pOutput + static_cast<size_t>( written ) * audio::kChannelCount,
                          _listBlockOutput.data() + static_cast<size_t>( _blockCursor ) * audio::kChannelCount,
                          static_cast<size_t>( copyFrames ) * audio::kChannelCount * sizeof( float32 ) );
            written += copyFrames;
            _blockCursor += copyFrames;
        }
    }

    void AudioEngine::applyCommands()
    {
        {
            std::scoped_lock<mutex> lock{ _commandMutex };
            _listApplyingCommand.swap( _listPendingCommand );
        }
        for ( Command& command : _listApplyingCommand )
            applyCommand( command );
        _listApplyingCommand.clear();
    }

    void AudioEngine::applyCommand( Command& command )
    {
        switch ( command._type )
        {
            case CommandType::PlayClip:
            {
                _appliedPlayingId = MathUtil::max( _appliedPlayingId, command._playingId );
                VoiceSlot* pSlot  = allocateVoice();
                if ( pSlot == nullptr )
                {
                    SW_LOG_WARNING( "Voice pool exhausted - dropped %#", command._path.c_str() );
                    break;
                }
                pSlot->_playingId        = command._playingId;
                pSlot->_clipPath         = command._path;
                pSlot->_busName          = command._name;
                pSlot->_busIndex         = resolveBusIndex( command._name );
                pSlot->_volume           = AudioMath::dbToLinear( command._clipParams._volumeDb );
                pSlot->_pan              = command._clipParams._pan;
                pSlot->_pitchRatio       = AudioMath::semitonesToRatio( command._clipParams._pitchSemitones );
                pSlot->_fadeInSeconds    = command._clipParams._fadeInSeconds;
                pSlot->_bLoop            = command._clipParams._bLoop;
                pSlot->_emitterId        = command._clipParams._emitterId;
                pSlot->_attenuationName  = command._clipParams._attenuation;
                pSlot->_attenuationIndex = resolveAttenuationIndex( command._clipParams._attenuation );
                pSlot->_priority         = command._clipParams._priority;
                pSlot->_virtualMode      = command._clipParams._virtual;
                pSlot->_bWaitingForClip  = true;
                startWaitingVoice( *pSlot );
                break;
            }
            case CommandType::PostEvent:
            {
                _appliedPlayingId = MathUtil::max( _appliedPlayingId, command._playingId );
                applyPostEvent( command );
                break;
            }
            case CommandType::Stop:
            {
                stopVoices( command._playingId, 0, command._value );
                break;
            }
            case CommandType::StopEmitter:
            {
                stopVoices( 0, command._emitterId, command._value );
                break;
            }
            case CommandType::SetPaused:
            {
                for ( VoiceSlot& slot : _listVoice )
                {
                    if ( slot._bInUse == false || slot._playingId != command._playingId )
                        continue;
                    slot._bPausedRequest = command._bFlag;
                    slot._voice.setPaused( command._bFlag );
                }
                break;
            }
            case CommandType::SetBusUserVolume:
            {
                BusUserState& state = findOrAddBusUserState( command._name );
                state._volume       = MathUtil::clamp( command._value, 0.0f, 1.0f );
                applyBusUserState( state );
                break;
            }
            case CommandType::SetBusMuted:
            {
                BusUserState& state = findOrAddBusUserState( command._name );
                state._bMuted       = command._bFlag;
                applyBusUserState( state );
                break;
            }
            case CommandType::SetBusSolo:
            {
                BusUserState& state = findOrAddBusUserState( command._name );
                state._bSolo        = command._bFlag;
                applyBusUserState( state );
                break;
            }
            case CommandType::SwapMixer:
            {
                _pMixer = std::move( command._pMixer );
                for ( const BusUserState& state : _listBusUserState )
                    applyBusUserState( state );
                // 살아 있는 보이스는 같은 이름의 버스로 옮긴다(없으면 sfx · master).
                for ( VoiceSlot& slot : _listVoice )
                {
                    if ( slot._bInUse )
                        slot._busIndex = resolveBusIndex( slot._busName );
                }
                if ( _listVoice.size() < _pMixer->getDesc()._maxVoiceCount )
                {
                    _listVoice.resize( _pMixer->getDesc()._maxVoiceCount );
                    _listInstance.resize( _pMixer->getDesc()._maxVoiceCount );
                }
                rebuildAttenuationTable();
                rebindSnapshots();
                break;
            }
            case CommandType::SetLibrary:
            {
                applySetLibrary( command );
                break;
            }
            case CommandType::SetRandomSeed:
            {
                _random._state = command._seed;
                break;
            }
            case CommandType::SetListener:
            {
                _arrListener[command._index] = command._listener;
                break;
            }
            case CommandType::SetEmitter:
            {
                AudioEmitterState& emitter = _mapEmitter[command._emitterId]._state;
                emitter._position          = command._position;
                emitter._velocity          = command._velocity;
                break;
            }
            case CommandType::SetEmitterOcclusion:
            {
                _mapEmitter[command._emitterId]._state._occlusionTarget = command._value;
                break;
            }
            case CommandType::SetEmitterParameter:
            {
                vector<EmitterParameter>& listParameter = _mapEmitter[command._emitterId]._listParameter;
                bool                      bFound        = false;
                for ( EmitterParameter& parameter : listParameter )
                {
                    if ( parameter._name == command._name )
                    {
                        parameter._value = command._value;
                        bFound           = true;
                    }
                }
                if ( bFound == false )
                    listParameter.push_back( EmitterParameter{ command._name, command._value } );
                break;
            }
            case CommandType::RemoveEmitter:
            {
                _mapEmitter.erase( command._emitterId );
                break;
            }
            case CommandType::SetParameter:
            {
                ParameterState& parameter = _mapParameter[command._name];
                parameter._target         = MathUtil::clamp( command._value, parameter._minValue, parameter._maxValue );
                if ( parameter._seekSpeed <= 0.0f )
                    parameter._value = parameter._target;
                break;
            }
            case CommandType::StartSnapshot:
            {
                SnapshotState* pSnapshot = findOrAddSnapshot( command._name );
                if ( pSnapshot == nullptr )
                    break;
                pSnapshot->_target  = 1.0f;
                pSnapshot->_bDriven = false;
                // 가장 나중에 켠 것이 마지막에 얹힌다.
                const SnapshotState moved = *pSnapshot;
                _listSnapshot.erase( _listSnapshot.begin() + ( pSnapshot - _listSnapshot.data() ) );
                _listSnapshot.push_back( moved );
                break;
            }
            case CommandType::StopSnapshot:
            {
                for ( SnapshotState& snapshot : _listSnapshot )
                {
                    if ( snapshot._name == command._name )
                    {
                        snapshot._target  = 0.0f;
                        snapshot._bDriven = false;
                    }
                }
                break;
            }
            case CommandType::StartMusic:
            {
                _appliedPlayingId = MathUtil::max( _appliedPlayingId, command._playingId );
                applyStartMusic( command );
                break;
            }
            case CommandType::SetMusicSegment:
            {
                applySetMusicSegment( command._name, false );
                break;
            }
            case CommandType::StopMusic:
            {
                applyStopMusic( command._value );
                break;
            }
            case CommandType::SetSnapshotIntensity:
            {
                SnapshotState* pSnapshot = findOrAddSnapshot( command._name );
                if ( pSnapshot == nullptr )
                    break;
                pSnapshot->_intensity = command._value;
                pSnapshot->_target    = command._value;
                pSnapshot->_bDriven   = true;
                break;
            }
        }
    }

    AudioEngine::VoiceSlot* AudioEngine::allocateVoice()
    {
        for ( VoiceSlot& slot : _listVoice )
        {
            if ( slot._bInUse == false )
            {
                slot         = VoiceSlot{};
                slot._bInUse = true;
                return &slot;
            }
        }
        return nullptr;
    }

    void AudioEngine::freeVoice( VoiceSlot& slot )
    {
        slot._voice.reset();
        slot._bInUse          = false;
        slot._bWaitingForClip = false;
        slot._playingId       = 0;
        slot._instanceIndex   = -1;
    }

    void AudioEngine::stopVoices( AudioPlayingId playingId, AudioEmitterId emitterId, float32 fadeSeconds )
    {
        for ( VoiceSlot& slot : _listVoice )
        {
            if ( slot._bInUse == false )
                continue;
            const bool bMatches = ( playingId != 0 && slot._playingId == playingId ) || ( emitterId != 0 && slot._emitterId == emitterId );
            if ( bMatches == false )
                continue;
            float32 seconds = fadeSeconds;
            if ( seconds < 0.0f )
            {
                const bool bEvent = slot._instanceIndex >= 0 && _listInstance[static_cast<size_t>( slot._instanceIndex )]._pDesc != nullptr;
                seconds           = bEvent ? _listInstance[static_cast<size_t>( slot._instanceIndex )]._pDesc->_fadeOutSeconds : 0.0f;
            }
            const uint32 fadeFrames = static_cast<uint32>( seconds * static_cast<float32>( audio::kSampleRate ) );
            if ( slot._bWaitingForClip || fadeFrames == 0 || slot._bVirtual )
                freeVoice( slot );
            else
                slot._voice.setFade( 0.0f, fadeFrames, 0, true );
        }
    }

    void AudioEngine::startWaitingVoice( VoiceSlot& slot )
    {
        const AudioClipStatus status = _clipStore.getStatus( slot._clipPath );
        if ( status == AudioClipStatus::Failed || status == AudioClipStatus::Missing )
        {
            freeVoice( slot );
            return;
        }
        shared_ptr<const AudioClipData> pClip = _clipStore.findClip( slot._clipPath );
        if ( pClip == nullptr )
            return;

        // 음악 레이어 · 스팅어는 정해진 렌더 프레임에 샘플 단위로 시작한다 — 아직 이르면 그만큼 늦추고, 클립이 늦게 왔으면 그만큼 건너뛰어 박을 지킨다.
        const bool bScheduled = slot._musicSegment != -1;
        uint32     delay      = 0;
        float64    skipFrames = 0.0;
        if ( bScheduled )
        {
            if ( slot._startFrame >= _renderedFrameCount )
                delay = static_cast<uint32>( slot._startFrame - _renderedFrameCount );
            else
                skipFrames = static_cast<float64>( _renderedFrameCount - slot._startFrame ) * static_cast<float64>( pClip->_sampleRate ) / static_cast<float64>( audio::kSampleRate );
        }
        slot._voice.start( std::move( pClip ), slot._bLoop, delay, bScheduled == false );
        if ( skipFrames > 0.0 )
            slot._voice.setPosition( skipFrames );
        slot._voice.setPaused( slot._bPausedRequest );
        if ( slot._fadeInSeconds > 0.0f )
        {
            slot._voice.setFade( 0.0f, 0, 0, false );
            slot._voice.setFade( 1.0f, static_cast<uint32>( slot._fadeInSeconds * static_cast<float32>( audio::kSampleRate ) ), 0, false );
        }
        slot._bWaitingForClip = false;
    }

    uint32 AudioEngine::resolveBusIndex( const hashed_string& bus ) const
    {
        int32 busIndex = bus.empty() ? -1 : _pMixer->findBusIndex( bus );
        if ( busIndex < 0 )
            busIndex = _pMixer->findBusIndex( hashed_string( AudioBusNames::kSfx ) );
        if ( busIndex < 0 )
            busIndex = _pMixer->findBusIndex( hashed_string( AudioBusNames::kMaster ) );
        return busIndex < 0 ? 0u : static_cast<uint32>( busIndex );
    }

    AudioEngine::BusUserState& AudioEngine::findOrAddBusUserState( const hashed_string& bus )
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

    void AudioEngine::applyBusUserState( const BusUserState& state )
    {
        const int32 busIndex = _pMixer->findBusIndex( state._bus );
        if ( busIndex < 0 )
            return;
        const uint32 index = static_cast<uint32>( busIndex );
        _pMixer->setBusUserVolume( index, state._volume );
        _pMixer->setBusMuted( index, state._bMuted || _pMixer->getDesc()._listBus[index]._bMuted );
        _pMixer->setBusSolo( index, state._bSolo || _pMixer->getDesc()._listBus[index]._bSolo );
    }

    void AudioEngine::updateEmitters()
    {
        // 한 극 추종: 블록마다 남은 차이의 (1 − e^(−블록/τ)) 만큼 다가간다.
        const float32 smoothing    = _pMixer->getDesc()._occlusion._smoothingSeconds;
        const float32 blockSeconds = static_cast<float32>( audio::kBlockFrameCount ) / static_cast<float32>( audio::kSampleRate );
        const float32 step         = smoothing <= 0.0f ? 1.0f : 1.0f - MathUtil::pow( 2.718281828f, -blockSeconds / smoothing );
        for ( auto& entry : _mapEmitter )
        {
            AudioEmitterState& emitter = entry.second._state;
            emitter._occlusion += ( emitter._occlusionTarget - emitter._occlusion ) * step;
        }
    }

    void AudioEngine::updateVoiceTargets( VoiceSlot& slot )
    {
        float32 volume    = slot._volume * slot._musicGain;
        float32 pan       = slot._pan;
        float32 pitch     = slot._pitchRatio;
        float32 lowPassHz = audio::kFilterOpenHz;
        slot._distance    = 0.0f;

        const bool bSpatial = slot._attenuationIndex >= 0 && slot._emitterId != 0;
        if ( bSpatial )
        {
            const auto it = _mapEmitter.find( slot._emitterId );
            if ( it != _mapEmitter.end() )
            {
                const AudioAttenuationDesc& attenuation = _listAttenuation[static_cast<size_t>( slot._attenuationIndex )];
                // 리스너가 여럿이면 가장 크게 들리는 리스너로 잰다(언리얼 · Wwise 의 "가장 가까운 리스너").
                bool               bHaveListener = false;
                AudioSpatialResult best;
                for ( const AudioListenerState& listener : _arrListener )
                {
                    if ( listener._bActive == false )
                        continue;
                    const AudioSpatialResult result = AudioSpatializer::compute( listener, it->second._state, attenuation, _pMixer->getDesc()._occlusion );
                    if ( bHaveListener == false || result._gain > best._gain )
                        best = result;
                    bHaveListener = true;
                }
                if ( bHaveListener )
                {
                    volume *= best._gain;
                    pan = MathUtil::clamp( pan + best._pan, -1.0f, 1.0f );
                    pitch *= best._pitchRatio;
                    lowPassHz      = best._lowPassHz;
                    slot._distance = best._distance;
                }
            }
        }

        // 이벤트의 파라미터 곡선(RTPC) — 볼륨 dB · 피치 반음은 더하고, 로우패스는 낮은 쪽.
        if ( slot._instanceIndex >= 0 )
        {
            const AudioEventDesc* pEvent = _listInstance[static_cast<size_t>( slot._instanceIndex )]._pDesc;
            if ( pEvent != nullptr )
            {
                for ( const AudioParameterMapping& mapping : pEvent->_listParameterMap )
                {
                    const float32 output = AudioCurvePoint::evaluate( mapping._listPoint, findParameterValue( slot._emitterId, mapping._parameter ), 0.0f );
                    switch ( mapping._target )
                    {
                        case AudioParameterTarget::Volume:
                        {
                            volume *= AudioMath::dbToLinear( output );
                            break;
                        }
                        case AudioParameterTarget::Pitch:
                        {
                            pitch *= AudioMath::semitonesToRatio( output );
                            break;
                        }
                        case AudioParameterTarget::LowPass:
                        {
                            lowPassHz = MathUtil::min( lowPassHz, output );
                            break;
                        }
                    }
                }
            }
        }

        const AudioClipData* pClip = slot._voice.getClip();
        if ( pClip != nullptr )
            slot._voice.setPlaybackRate( static_cast<float64>( pClip->_sampleRate ) / static_cast<float64>( audio::kSampleRate ) * static_cast<float64>( pitch ) );
        slot._voice.setTarget( volume, pan );
        slot._voice.setLowPass( lowPassHz );
        // 들림은 보이스 자신의 게인(거리 · 가림 · 파라미터 · 페이드 목표)이다 — 버스 페이더는 넣지 않는다(음소거된 버스의 프리 페이더 센드가 살아 있어야 한다).
        slot._audibility = volume * slot._voice.getFadeTarget();
    }

    void AudioEngine::renderBlock()
    {
        applyCommands();
        updateParameters();
        updateSnapshots();
        updateEmitters();
        updateMusic();

        AudioMixer& mixer = *_pMixer;
        mixer.beginBlock( audio::kBlockFrameCount );

        for ( VoiceSlot& slot : _listVoice )
        {
            if ( slot._bInUse == false )
                continue;
            if ( slot._bWaitingForClip )
            {
                startWaitingVoice( slot );
                if ( slot._bInUse == false || slot._bWaitingForClip )
                    continue;
            }
            updateVoiceTargets( slot );
        }
        selectRealVoices();

        uint32 realCount    = 0;
        uint32 virtualCount = 0;
        for ( VoiceSlot& slot : _listVoice )
        {
            if ( slot._bInUse == false || slot._bWaitingForClip )
                continue;
            if ( slot._bVirtual )
            {
                slot._voice.advanceVirtual( audio::kBlockFrameCount );
                ++virtualCount;
            }
            else
            {
                slot._voice.mix( mixer.getBusInput( slot._busIndex ), audio::kBlockFrameCount, _listVoiceScratch.data() );
                ++realCount;
            }
            if ( slot._voice.isFinished() )
                freeVoice( slot );
        }
        updateInstances();

        mixer.process( _listBlockOutput.data(), audio::kBlockFrameCount );
        // 장치는 [-1, 1] 밖을 감아 돌릴 수 있다 — 마지막에 자른다(리미터가 master 에 있으면 여기 닿지 않는다).
        for ( float32& sample : _listBlockOutput )
            sample = MathUtil::clamp( sample, -1.0f, 1.0f );

        _renderedFrameCount += audio::kBlockFrameCount;
        publishState( realCount, virtualCount );
    }

    void AudioEngine::publishState( uint32 realCount, uint32 virtualCount )
    {
        AudioEngineStats stats;
        stats._renderedFrameCount = _renderedFrameCount;
        stats._realVoiceCount     = realCount;
        stats._virtualVoiceCount  = virtualCount;
        for ( const EventInstance& instance : _listInstance )
            stats._instanceCount += instance._bInUse ? 1u : 0u;

        std::scoped_lock<mutex> lock{ _publishMutex };
        _listPublishedPlaying.clear();
        for ( const VoiceSlot& slot : _listVoice )
        {
            if ( slot._bInUse == false )
                continue;
            ++stats._voiceCount;
            _listPublishedPlaying.push_back( slot._playingId );
        }
        std::sort( _listPublishedPlaying.begin(), _listPublishedPlaying.end() );
        _publishedStats = stats;
        _publishedMusic = AudioMusicStatus{};
        if ( _pMusic != nullptr && _musicSegment >= 0 )
        {
            const uint32  segmentIndex  = static_cast<uint32>( _musicSegment );
            const float64 framesPerBeat = _pMusic->computeFramesPerBeat( segmentIndex );
            const float64 elapsed       = static_cast<float64>( _renderedFrameCount ) - static_cast<float64>( _musicSegmentStart );
            _publishedMusic._segment    = _pMusic->_listSegment[segmentIndex]._name;
            _publishedMusic._beat       = elapsed / framesPerBeat;
            _publishedMusic._bar        = elapsed <= 0.0 ? 0u : static_cast<uint64>( _publishedMusic._beat ) / _pMusic->getBeatsPerBar( segmentIndex );
            _publishedMusic._tempo      = static_cast<float32>( 60.0 * static_cast<float64>( audio::kSampleRate ) / framesPerBeat );
            _publishedMusic._bPlaying   = true;
        }
        _publishedAppliedId.store( _appliedPlayingId, std::memory_order_release );
    }
} // namespace sw
