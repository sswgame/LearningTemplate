#include "pch.h"

#include "Engine/Audio/AudioEngine.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Audio/AudioMixer.h"
#include "Engine/Audio/AudioMixerDesc.h"

namespace sw
{
    SW_LOG_CALLER( "AudioEngine" );

    AudioEngine::AudioEngine()
        : _clipStore{}
        , _listPendingCommand{}
        , _listApplyingCommand{}
        , _commandMutex{}
        , _nextPlayingId{ 1 }
        , _pMixer{ nullptr }
        , _listVoice{}
        , _listBusUserState{}
        , _listBlockOutput{}
        , _listVoiceScratch{}
        , _mapEmitter{}
        , _arrListener{}
        , _random{}
        , _renderedFrameCount{ 0 }
        , _appliedPlayingId{ 0 }
        , _blockCursor{ audio::kBlockFrameCount }
        , _listPublishedPlaying{}
        , _publishedStats{}
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
        _pMixer = std::move( pMixer );
        _listVoice.clear();
        _listVoice.resize( mixerDesc._maxVoiceCount );
        _listBlockOutput.assign( static_cast<size_t>( audio::kBlockFrameCount ) * audio::kChannelCount, 0.0f );
        _listVoiceScratch.assign( static_cast<size_t>( audio::kBlockFrameCount ) * audio::kChannelCount, 0.0f );
        _blockCursor        = audio::kBlockFrameCount;
        _renderedFrameCount = 0;
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
        Command command;
        command._type   = CommandType::SwapMixer;
        command._pMixer = std::move( pMixer );
        pushCommand( std::move( command ) );
        return true;
    }

    void AudioEngine::setRandomSeed( uint64 seed )
    {
        Command command;
        command._type = CommandType::SetRandomSeed;
        command._seed = seed;
        pushCommand( std::move( command ) );
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
        // id 는 명령을 쌓을 때(잠금 안에서) 매긴다 — 큐 순서와 id 순서가 같아야 "아직 처리되지 않은 id" 판정이 맞다.
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

    void AudioEngine::setPaused( AudioPlayingId playingId, bool bPaused )
    {
        Command command;
        command._type      = CommandType::SetPaused;
        command._playingId = playingId;
        command._bFlag     = bPaused;
        pushCommand( std::move( command ) );
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
            Memory::copy( pOutput + static_cast<size_t>( written ) * audio::kChannelCount, _listBlockOutput.data() + static_cast<size_t>( _blockCursor ) * audio::kChannelCount,
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
                pSlot->_bWaitingForClip  = true;
                startWaitingVoice( *pSlot );
                break;
            }
            case CommandType::Stop:
            {
                const uint32 fadeFrames = static_cast<uint32>( MathUtil::max( 0.0f, command._value ) * static_cast<float32>( audio::kSampleRate ) );
                for ( VoiceSlot& slot : _listVoice )
                {
                    if ( slot._bInUse == false || slot._playingId != command._playingId )
                        continue;
                    if ( slot._bWaitingForClip || fadeFrames == 0 )
                        freeVoice( slot );
                    else
                        slot._voice.setFade( 0.0f, fadeFrames, 0, true );
                }
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
                    {
                        slot._busIndex         = resolveBusIndex( slot._busName );
                        slot._attenuationIndex = resolveAttenuationIndex( slot._attenuationName );
                    }
                }
                if ( _listVoice.size() < _pMixer->getDesc()._maxVoiceCount )
                    _listVoice.resize( _pMixer->getDesc()._maxVoiceCount );
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
                AudioEmitterState& emitter = _mapEmitter[command._emitterId];
                emitter._position          = command._position;
                emitter._velocity          = command._velocity;
                break;
            }
            case CommandType::SetEmitterOcclusion:
            {
                _mapEmitter[command._emitterId]._occlusionTarget = command._value;
                break;
            }
            case CommandType::RemoveEmitter:
            {
                _mapEmitter.erase( command._emitterId );
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

        const float64 rate = static_cast<float64>( pClip->_sampleRate ) / static_cast<float64>( audio::kSampleRate ) * static_cast<float64>( slot._pitchRatio );
        slot._voice.start( std::move( pClip ), slot._bLoop, 0 );
        slot._voice.setPlaybackRate( rate );
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

    int32 AudioEngine::resolveAttenuationIndex( const hashed_string& name ) const
    {
        if ( name.empty() )
            return -1;
        const vector<AudioAttenuationDesc>& listAttenuation = _pMixer->getDesc()._listAttenuation;
        for ( size_t attenuationIndex = 0; attenuationIndex < listAttenuation.size(); ++attenuationIndex )
        {
            if ( listAttenuation[attenuationIndex]._name == name )
                return static_cast<int32>( attenuationIndex );
        }
        return -1;
    }

    void AudioEngine::updateEmitters()
    {
        // 한 극 추종: 블록마다 남은 차이의 (1 − e^(−블록/τ)) 만큼 다가간다.
        const float32 smoothing    = _pMixer->getDesc()._occlusion._smoothingSeconds;
        const float32 blockSeconds = static_cast<float32>( audio::kBlockFrameCount ) / static_cast<float32>( audio::kSampleRate );
        const float32 step         = smoothing <= 0.0f ? 1.0f : 1.0f - MathUtil::pow( 2.718281828f, -blockSeconds / smoothing );
        for ( auto& entry : _mapEmitter )
        {
            AudioEmitterState& emitter = entry.second;
            emitter._occlusion += ( emitter._occlusionTarget - emitter._occlusion ) * step;
        }
    }

    void AudioEngine::updateVoiceTargets( VoiceSlot& slot )
    {
        float32 volume    = slot._volume;
        float32 pan       = slot._pan;
        float32 pitch     = slot._pitchRatio;
        float32 lowPassHz = audio::kFilterOpenHz;

        const bool bSpatial = slot._attenuationIndex >= 0 && slot._emitterId != 0;
        if ( bSpatial )
        {
            const auto it = _mapEmitter.find( slot._emitterId );
            if ( it != _mapEmitter.end() )
            {
                const AudioAttenuationDesc& attenuation = _pMixer->getDesc()._listAttenuation[static_cast<size_t>( slot._attenuationIndex )];
                // 리스너가 여럿이면 가장 크게 들리는 리스너로 잰다(언리얼 · Wwise 의 "가장 가까운 리스너").
                bool               bHaveListener = false;
                AudioSpatialResult best;
                for ( const AudioListenerState& listener : _arrListener )
                {
                    if ( listener._bActive == false )
                        continue;
                    const AudioSpatialResult result = AudioSpatializer::compute( listener, it->second, attenuation, _pMixer->getDesc()._occlusion );
                    if ( bHaveListener == false || result._gain > best._gain )
                        best = result;
                    bHaveListener = true;
                }
                if ( bHaveListener )
                {
                    volume *= best._gain;
                    pan = MathUtil::clamp( pan + best._pan, -1.0f, 1.0f );
                    pitch *= best._pitchRatio;
                    lowPassHz = best._lowPassHz;
                }
            }
        }

        const AudioClipData* pClip = slot._voice.getClip();
        if ( pClip != nullptr )
            slot._voice.setPlaybackRate( static_cast<float64>( pClip->_sampleRate ) / static_cast<float64>( audio::kSampleRate ) * static_cast<float64>( pitch ) );
        slot._voice.setTarget( volume, pan );
        slot._voice.setLowPass( lowPassHz );
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

    void AudioEngine::renderBlock()
    {
        applyCommands();

        AudioMixer& mixer = *_pMixer;
        mixer.beginBlock( audio::kBlockFrameCount );
        updateEmitters();

        uint32 realCount = 0;
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
            slot._voice.mix( mixer.getBusInput( slot._busIndex ), audio::kBlockFrameCount, _listVoiceScratch.data() );
            ++realCount;
            if ( slot._voice.isFinished() )
                freeVoice( slot );
        }

        mixer.process( _listBlockOutput.data(), audio::kBlockFrameCount );
        // 장치는 [-1, 1] 밖을 감아 돌릴 수 있다 — 마지막에 자른다(리미터가 master 에 있으면 여기 닿지 않는다).
        for ( float32& sample : _listBlockOutput )
            sample = MathUtil::clamp( sample, -1.0f, 1.0f );

        _renderedFrameCount += audio::kBlockFrameCount;
        publishState();
        (void)realCount;
    }

    void AudioEngine::publishState()
    {
        AudioEngineStats stats;
        stats._renderedFrameCount = _renderedFrameCount;
        std::scoped_lock<mutex> lock{ _publishMutex };
        _listPublishedPlaying.clear();
        for ( const VoiceSlot& slot : _listVoice )
        {
            if ( slot._bInUse == false )
                continue;
            ++stats._voiceCount;
            if ( slot._bWaitingForClip == false )
                ++stats._realVoiceCount;
            _listPublishedPlaying.push_back( slot._playingId );
        }
        std::sort( _listPublishedPlaying.begin(), _listPublishedPlaying.end() );
        _publishedStats = stats;
        _publishedAppliedId.store( _appliedPlayingId, std::memory_order_release );
    }
} // namespace sw
