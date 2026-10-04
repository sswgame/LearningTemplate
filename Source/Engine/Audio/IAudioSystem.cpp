#include "pch.h"

#include "Engine/Audio/IAudioSystem.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioMusic.h"
#include "Engine/Audio/NullAudioSystem.h"
#include "Engine/Resource/ResourceUtil.h"

// NullAudioSystem 은 Windows 에서 쓰이지 않지만 **항상 컴파일한다.** 헤더 전용이라 비용이 없고,
// `#else` 안에만 두었을 때는 Windows 에서 한 번도 컴파일되지 않아 조용히 썩는다(FileWatcher 의
// macOS 구현이 그랬다).

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Audio/Windows/XAudio2System.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "AudioSystem" );

    namespace
    {
        struct IAudioSystemInternal
        {
            /** @brief 곡을 바꿀 때의 크로스페이드 길이(초)입니다. */
            static constexpr float32 kMusicCrossfadeSeconds = 0.25f;
            /** @brief 장치 없이 한 번의 update 가 렌더할 최대 시간(초)입니다 — 멈춘 프레임 뒤에 몇 분을 몰아 렌더하지 않게. */
            static constexpr float64 kMaxOfflineSecondsPerUpdate = 0.25;
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<IAudioSystem> IAudioSystem::create()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return make_unique<XAudio2System>();
#else
        return make_unique<NullAudioSystem>();
#endif
    }

    IAudioSystem::IAudioSystem()
        : _pEngine{ make_unique<AudioEngine>() }
        , _listBusVolume{}
        , _listOfflineBlock{}
        , _musicPath{}
        , _musicPlayingId{ 0 }
        , _offlineFrameDebt{ 0.0 }
        , _masterVolume{ 1.0f }
        , _musicVolume{ 1.0f }
        , _sfxVolume{ 1.0f }
        , _bMuted{ false }
        , _bOutputOpen{ false }
        , _bInitialized{ false }
    {
    }

    IAudioSystem::~IAudioSystem() = default;

    bool IAudioSystem::initialize()
    {
        if ( _bInitialized )
            return true;

        AudioMixerDesc mixerDesc;
        if ( ResourceUtil::hasResource( kDefaultMixerPath ) == false || mixerDesc.loadFromResource( kDefaultMixerPath ) == false )
            mixerDesc = AudioMixerDesc::makeDefault();
        if ( _pEngine->initialize( mixerDesc ) == false && _pEngine->initialize( AudioMixerDesc::makeDefault() ) == false )
            return false;

        // 설정 메뉴가 엔진보다 먼저 볼륨을 넣었을 수 있다 — 들고 있는 값을 다시 건다.
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kMaster ), _masterVolume );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kMusic ), _musicVolume );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kSfx ), _sfxVolume );
        _pEngine->setBusMuted( hashed_string( AudioBusNames::kMaster ), _bMuted );
        for ( const AudioBusVolume& busVolume : _listBusVolume )
            _pEngine->setBusUserVolume( busVolume._bus, busVolume._volume );

        _listOfflineBlock.assign( static_cast<size_t>( audio::kBlockFrameCount ) * audio::kChannelCount, 0.0f );
        _bOutputOpen  = openOutput();
        _bInitialized = true;
        SW_LOG_INFO( "Initialized (%#).", _bOutputOpen ? "device output" : "offline - no device" );
        return true;
    }

    void IAudioSystem::shutdown()
    {
        if ( _bInitialized == false )
            return;
        // 장치를 먼저 닫는다 — 닫힌 뒤에는 장치 스레드가 render 를 부르지 않으므로 엔진을 내려도 된다.
        if ( _bOutputOpen )
            closeOutput();
        _bOutputOpen = false;
        _pEngine->shutdown();
        _musicPath.clear();
        _musicPlayingId = 0;
        _bInitialized   = false;
        SW_LOG_INFO( "Shut down." );
    }

    void IAudioSystem::update( float32 deltaSeconds )
    {
        SW_MEMORY_SCOPE( Audio );
        if ( _bInitialized == false || _bOutputOpen )
            return;
        // 장치가 없으면 흐른 시간만큼 렌더해 버린다. 끝남 · 박자 · 가상 보이스가 장치가 있을 때와 같이 흐른다.
        const float64 seconds = MathUtil::clamp( static_cast<float64>( deltaSeconds ), 0.0, IAudioSystemInternal::kMaxOfflineSecondsPerUpdate );
        _offlineFrameDebt += seconds * static_cast<float64>( audio::kSampleRate );
        while ( _offlineFrameDebt >= static_cast<float64>( audio::kBlockFrameCount ) )
        {
            _pEngine->render( _listOfflineBlock.data(), audio::kBlockFrameCount );
            _offlineFrameDebt -= static_cast<float64>( audio::kBlockFrameCount );
        }
    }

    bool IAudioSystem::play( string_view path, const hashed_string& bus )
    {
        SW_MEMORY_SCOPE( Audio );
        if ( _bInitialized == false || path.empty() )
            return false;
        if ( ResourceUtil::hasResource( path ) == false )
        {
            SW_LOG_WARNING( "Audio resource not found: %#", path );
            return false;
        }
        return _pEngine->playClip( hashed_string( path ), bus, AudioClipPlayParams{} ) != 0;
    }

    bool IAudioSystem::preload( string_view path )
    {
        SW_MEMORY_SCOPE( Audio );
        if ( path.empty() )
            return false;
        return _pEngine->getClipStore().loadClip( hashed_string( path ) ) != nullptr;
    }

    bool IAudioSystem::playMusic( string_view path )
    {
        if ( _bInitialized == false || path.empty() )
            return false;
        if ( _musicPath == path && _pEngine->isPlaying( _musicPlayingId ) )
            return true;
        // 있는 곡인지 **멈추기 전에** 본다 — 없는 곡을 요청해 틀어져 있던 BGM 만 꺼지면 안 된다.
        if ( ResourceUtil::hasResource( path ) == false )
        {
            SW_LOG_WARNING( "Audio resource not found: %#", path );
            return false;
        }
        const bool bCrossfade = _musicPlayingId != 0;
        if ( bCrossfade )
        {
            _pEngine->stop( _musicPlayingId, IAudioSystemInternal::kMusicCrossfadeSeconds );
            _pEngine->stopMusic( IAudioSystemInternal::kMusicCrossfadeSeconds );
        }

        AudioClipPlayParams params;
        params._bLoop         = true;
        params._fadeInSeconds = bCrossfade ? IAudioSystemInternal::kMusicCrossfadeSeconds : 0.0f;
        _musicPath            = string( path );
        _musicPlayingId       = _pEngine->playClip( hashed_string( path ), hashed_string( AudioBusNames::kMusic ), params );
        return _musicPlayingId != 0;
    }

    bool IAudioSystem::playAdaptiveMusic( string_view path )
    {
        if ( _bInitialized == false || path.empty() )
            return false;
        if ( _musicPath == path && _pEngine->isPlaying( _musicPlayingId ) )
            return true;
        shared_ptr<AudioMusicDesc> pMusic = make_shared<AudioMusicDesc>();
        if ( pMusic->loadFromResource( path ) == false )
            return false;
        if ( _musicPlayingId != 0 )
            _pEngine->stop( _musicPlayingId, IAudioSystemInternal::kMusicCrossfadeSeconds );
        _musicPath      = string( path );
        _musicPlayingId = _pEngine->startMusic( std::move( pMusic ), IAudioSystemInternal::kMusicCrossfadeSeconds );
        return _musicPlayingId != 0;
    }

    void IAudioSystem::stopMusic()
    {
        if ( _musicPlayingId != 0 )
            _pEngine->stop( _musicPlayingId, 0.0f );
        _pEngine->stopMusic( 0.0f );
        _musicPlayingId = 0;
        _musicPath.clear();
    }

    void IAudioSystem::pauseMusic()
    {
        if ( _musicPlayingId != 0 )
            _pEngine->setPaused( _musicPlayingId, true );
    }

    void IAudioSystem::resumeMusic()
    {
        if ( _musicPlayingId != 0 )
            _pEngine->setPaused( _musicPlayingId, false );
    }

    bool IAudioSystem::loadMixer( string_view resourcePath )
    {
        AudioMixerDesc mixerDesc;
        if ( mixerDesc.loadFromResource( resourcePath ) == false )
            return false;
        return _pEngine->loadMixer( mixerDesc );
    }

    void IAudioSystem::setMasterVolume( float32 volume )
    {
        _masterVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kMaster ), _masterVolume );
    }

    void IAudioSystem::setMusicVolume( float32 volume )
    {
        _musicVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kMusic ), _musicVolume );
    }

    void IAudioSystem::setSfxVolume( float32 volume )
    {
        _sfxVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kSfx ), _sfxVolume );
    }

    void IAudioSystem::setBusVolume( const hashed_string& bus, float32 volume )
    {
        if ( bus == hashed_string( AudioBusNames::kMaster ) )
        {
            setMasterVolume( volume );
            return;
        }
        if ( bus == hashed_string( AudioBusNames::kMusic ) )
        {
            setMusicVolume( volume );
            return;
        }
        if ( bus == hashed_string( AudioBusNames::kSfx ) )
        {
            setSfxVolume( volume );
            return;
        }

        const float32 clamped = MathUtil::clamp( volume, 0.0f, 1.0f );
        _pEngine->setBusUserVolume( bus, clamped );
        for ( AudioBusVolume& busVolume : _listBusVolume )
        {
            if ( busVolume._bus == bus )
            {
                busVolume._volume = clamped;
                return;
            }
        }
        _listBusVolume.push_back( AudioBusVolume{ bus, clamped } );
    }

    float32 IAudioSystem::getBusVolume( const hashed_string& bus ) const
    {
        if ( bus == hashed_string( AudioBusNames::kMaster ) )
            return _masterVolume;
        if ( bus == hashed_string( AudioBusNames::kMusic ) )
            return _musicVolume;
        if ( bus == hashed_string( AudioBusNames::kSfx ) )
            return _sfxVolume;
        for ( const AudioBusVolume& busVolume : _listBusVolume )
        {
            if ( busVolume._bus == bus )
                return busVolume._volume;
        }
        return 1.0f;
    }

    void IAudioSystem::setMute( bool bMute )
    {
        _bMuted = bMute;
        _pEngine->setBusMuted( hashed_string( AudioBusNames::kMaster ), bMute );
    }
} // namespace sw
