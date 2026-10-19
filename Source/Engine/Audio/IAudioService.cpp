#include "pch.h"

#include "Engine/Audio/IAudioService.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioMusic.h"
#include "Engine/Audio/NullAudioBackend.h"
#include "Engine/Resource/ResourceIO.h"

// NullAudioBackend 은 Windows 에서 쓰이지 않지만 **항상 컴파일한다.** 헤더 전용이라 비용이 없고,
// `#else` 안에만 두었을 때는 Windows 에서 한 번도 컴파일되지 않아 조용히 썩는다(FileWatcher 의
// macOS 구현이 그랬다).

// 전용 서버 빌드(SW_WITH_CLIENT_CODE 없음)에는 XAudio2 구현과 링크가 없다. 서버는 장치를 열지 않는다.
#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_WITH_CLIENT_CODE )
    #include "Engine/Audio/Windows/XAudio2AudioBackend.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "AudioService" );

    namespace
    {
        struct IAudioServiceInternal
        {
            /** @brief 곡을 바꿀 때의 크로스페이드 길이(초)입니다. */
            static constexpr float32 kMusicCrossfadeSeconds = 0.25f;
            /** @brief 장치 없이 한 번의 update 가 렌더할 최대 시간(초)입니다. 멈춘 프레임 뒤에 몇 분을 몰아서 렌더하지 않게 합니다. */
            static constexpr float64 kMaxOfflineSecondsPerUpdate = 0.25;
            /** @brief 엔진을 여는 출력 레이아웃입니다. 장치 협상(`openOutput` 이 형식을 돌려줌)이 들어오기 전까지 장치도 스테레오로만 엽니다. */
            static constexpr AudioChannelLayout kOutputLayout = AudioChannelLayout::Stereo;
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<IAudioService> IAudioService::create()
    {
#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_WITH_CLIENT_CODE )
        return make_unique<XAudio2AudioBackend>();
#else
        return make_unique<NullAudioBackend>();
#endif
    }

    unique_ptr<IAudioService> IAudioService::createNull()
    {
        return make_unique<NullAudioBackend>();
    }

    IAudioService::IAudioService()
        : _pEngine{ make_unique<AudioEngine>() }
        , _listBusVolume{}
        , _listOfflineBlock{}
        , _musicPath{}
        , _mixerPath{}
        , _musicPlayingID{ 0 }
        , _offlineFrameDebt{ 0.0 }
        , _masterVolume{ 1.0f }
        , _musicVolume{ 1.0f }
        , _sfxVolume{ 1.0f }
        , _bMuted{ false }
        , _bOutputOpen{ false }
        , _bInitialized{ false }
    {
    }

    IAudioService::~IAudioService() = default;

    bool IAudioService::initialize()
    {
        if ( _bInitialized )
            return true;

        // 믹서 그래프는 엔진 데이터가 정본이다. 없거나 틀리면 코드 그래프로 대신하지 않고 기동이 실패한다.
        AudioMixerDesc mixerDesc;
        if ( mixerDesc.loadFromResource( kDefaultMixerPath ) == false )
        {
            SW_LOG_ERROR( "Audio mixer graph '%#' could not be loaded", kDefaultMixerPath );
            return false;
        }
        _mixerPath = kDefaultMixerPath;
        if ( _pEngine->initialize( mixerDesc, IAudioServiceInternal::kOutputLayout ) == false )
        {
            SW_LOG_ERROR( "Audio engine rejected the mixer graph '%#'", kDefaultMixerPath );
            return false;
        }

        // 설정 메뉴가 엔진보다 먼저 볼륨을 넣었을 수 있다. 가지고 있던 값을 다시 적용한다.
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kMaster ), _masterVolume );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kMusic ), _musicVolume );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kSfx ), _sfxVolume );
        _pEngine->setBusMuted( hashed_string( AudioBusNames::kMaster ), _bMuted );
        for ( const AudioBusVolume& busVolume : _listBusVolume )
        {
            _pEngine->setBusUserVolume( busVolume._bus, busVolume._volume );
        }

        _listOfflineBlock.assign( static_cast<size_t>( audio::kBlockFrameCount ) * AudioChannelLayoutUtil::getChannelCount( _pEngine->getChannelLayout() ), 0.0f );
        _bOutputOpen  = openOutput();
        _bInitialized = true;
        SW_LOG_INFO( "Initialized (%#).", _bOutputOpen ? "device output" : "offline - no device" );
        return true;
    }

    void IAudioService::shutdown()
    {
        if ( _bInitialized == false )
            return;
        // 장치를 먼저 닫는다. 닫힌 뒤에는 장치 스레드가 render 를 호출하지 않으므로 엔진을 해제해도 된다.
        [[maybe_unused]] const AudioEngineStats stats = _pEngine->getStats();
        SW_LOG_INFO( "Played %# events (%# dropped by cooldown / limits), peak %# mixed voices, %# s rendered.", stats._playedEventCount, stats._droppedEventCount,
                     stats._peakRealVoiceCount, stats._renderedFrameCount / audio::kSampleRate );
        if ( _bOutputOpen )
            closeOutput();
        _bOutputOpen = false;
        _pEngine->shutdown();
        _musicPath.clear();
        _musicPlayingID = 0;
        _bInitialized   = false;
        SW_LOG_INFO( "Shut down." );
    }

    void IAudioService::update( float32 deltaSeconds )
    {
        SW_MEMORY_SCOPE( Audio );
        if ( _bInitialized == false || _bOutputOpen )
            return;
        // 장치가 없으면 흐른 시간만큼 바로 렌더한다. 끝남, 박자, 가상 보이스가 장치가 있을 때와 같이 진행한다.
        const float64 seconds = MathUtil::clamp( static_cast<float64>( deltaSeconds ), 0.0, IAudioServiceInternal::kMaxOfflineSecondsPerUpdate );
        _offlineFrameDebt += seconds * static_cast<float64>( audio::kSampleRate );
        while ( _offlineFrameDebt >= static_cast<float64>( audio::kBlockFrameCount ) )
        {
            _pEngine->render( _listOfflineBlock.data(), audio::kBlockFrameCount );
            _offlineFrameDebt -= static_cast<float64>( audio::kBlockFrameCount );
        }
    }

    bool IAudioService::play( string_view path, const hashed_string& bus )
    {
        SW_MEMORY_SCOPE( Audio );
        if ( _bInitialized == false || path.empty() )
            return false;
        if ( ResourceIO::hasResource( path ) == false )
        {
            SW_LOG_ERROR( "Audio resource not found: %#", path );
            return false;
        }
        return _pEngine->playClip( hashed_string( path ), bus, AudioClipPlayParams{} ) != 0;
    }

    bool IAudioService::preload( string_view path )
    {
        SW_MEMORY_SCOPE( Audio );
        if ( path.empty() )
            return false;
        return _pEngine->getClipStore().loadClip( hashed_string( path ) ) != nullptr;
    }

    bool IAudioService::playMusic( string_view path )
    {
        if ( _bInitialized == false || path.empty() )
            return false;
        if ( _musicPath == path && _pEngine->isPlaying( _musicPlayingID ) )
            return true;
        // 있는 곡인지 **멈추기 전에** 확인한다. 없는 곡을 요청했다가 재생 중이던 BGM 만 꺼지면 안 된다.
        if ( ResourceIO::hasResource( path ) == false )
        {
            SW_LOG_ERROR( "Audio resource not found: %#", path );
            return false;
        }
        const bool bCrossfade = _musicPlayingID != 0;
        if ( bCrossfade )
        {
            _pEngine->stop( _musicPlayingID, IAudioServiceInternal::kMusicCrossfadeSeconds );
            _pEngine->stopMusic( IAudioServiceInternal::kMusicCrossfadeSeconds );
        }

        AudioClipPlayParams params;
        params._bLoop         = true;
        params._fadeInSeconds = bCrossfade ? IAudioServiceInternal::kMusicCrossfadeSeconds : 0.0f;
        _musicPath            = string( path );
        _musicPlayingID       = _pEngine->playClip( hashed_string( path ), hashed_string( AudioBusNames::kMusic ), params );
        return _musicPlayingID != 0;
    }

    bool IAudioService::playAdaptiveMusic( string_view path )
    {
        if ( _bInitialized == false || path.empty() )
            return false;
        if ( _musicPath == path && _pEngine->isPlaying( _musicPlayingID ) )
            return true;
        shared_ptr<AudioMusicDesc> pMusic = make_shared<AudioMusicDesc>();
        if ( pMusic->loadFromResource( path ) == false )
            return false;
        if ( _musicPlayingID != 0 )
            _pEngine->stop( _musicPlayingID, IAudioServiceInternal::kMusicCrossfadeSeconds );
        _musicPath      = string( path );
        _musicPlayingID = _pEngine->startMusic( std::move( pMusic ), IAudioServiceInternal::kMusicCrossfadeSeconds );
        return _musicPlayingID != 0;
    }

    void IAudioService::stopMusic()
    {
        if ( _musicPlayingID != 0 )
            _pEngine->stop( _musicPlayingID, 0.0f );
        _pEngine->stopMusic( 0.0f );
        _musicPlayingID = 0;
        _musicPath.clear();
    }

    void IAudioService::pauseMusic()
    {
        if ( _musicPlayingID != 0 )
            _pEngine->setPaused( _musicPlayingID, true );
    }

    void IAudioService::resumeMusic()
    {
        if ( _musicPlayingID != 0 )
            _pEngine->setPaused( _musicPlayingID, false );
    }

    bool IAudioService::loadMixer( string_view resourcePath )
    {
        AudioMixerDesc mixerDesc;
        if ( mixerDesc.loadFromResource( resourcePath ) == false )
            return false;
        if ( _pEngine->loadMixer( mixerDesc ) == false )
            return false;
        _mixerPath = string( resourcePath );
        return true;
    }

    bool IAudioService::isDataFile( string_view resourcePath ) const
    {
        if ( _bInitialized == false || resourcePath.empty() )
            return false;
        const bool    bMixer = _mixerPath.empty() == false && StringUtil::equals( FileUtil::normalizeSeparators( _mixerPath ), FileUtil::normalizeSeparators( resourcePath ), true );
        hashed_string libraryName;
        return bMixer || _pEngine->findEventLibraryName( resourcePath, libraryName );
    }

    bool IAudioService::reloadDataFile( string_view resourcePath )
    {
        if ( _bInitialized == false || resourcePath.empty() )
            return false;
        const bool    bMixer = _mixerPath.empty() == false && StringUtil::equals( FileUtil::normalizeSeparators( _mixerPath ), FileUtil::normalizeSeparators( resourcePath ), true );
        hashed_string libraryName;
        if ( bMixer == false && _pEngine->findEventLibraryName( resourcePath, libraryName ) == false )
            return false;
        if ( bMixer )
        {
            const bool bReloaded = loadMixer( _mixerPath );
            if ( bReloaded )
                SW_LOG_INFO( "Mixer graph reloaded: %#", resourcePath );
            return bReloaded;
        }
        AudioEventLibrary library;
        if ( library.loadFromResource( resourcePath ) == false )
            return false;
        // 이름은 라이브러리를 처음 올린 이름 그대로여야 같은 라이브러리를 바꾼다.
        if ( _pEngine->loadEventLibrary( libraryName, library ) == false )
            return false;
        SW_LOG_INFO( "Event library reloaded: %#", resourcePath );
        return true;
    }

    void IAudioService::setMasterVolume( float32 volume )
    {
        _masterVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kMaster ), _masterVolume );
    }

    void IAudioService::setMusicVolume( float32 volume )
    {
        _musicVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kMusic ), _musicVolume );
    }

    void IAudioService::setSfxVolume( float32 volume )
    {
        _sfxVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kSfx ), _sfxVolume );
    }

    void IAudioService::setBusVolume( const hashed_string& bus, float32 volume )
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

    float32 IAudioService::getBusVolume( const hashed_string& bus ) const
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

    void IAudioService::setMute( bool bMute )
    {
        _bMuted = bMute;
        _pEngine->setBusMuted( hashed_string( AudioBusNames::kMaster ), bMute );
    }
} // namespace sw

namespace sw
{
    span<const utf8* const> IAudioService::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { TaskManager::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw
