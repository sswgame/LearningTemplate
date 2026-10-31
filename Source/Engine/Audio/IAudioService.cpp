#include "pch.h"

#include "Engine/Audio/IAudioService.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioMusic.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/Scene/SceneManager.h"

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
            /** @brief 오프라인 렌더 덤프가 쌓는 최대 길이(초)입니다. 7.1 이면 약 110 MB 이며, 넘는 부분은 쌓지 않습니다. */
            static constexpr uint32 kMaxDumpSeconds = 60;

            /** @brief 장치 백엔드 등록자 목록의 머리입니다(함수 정적 — 정적 초기화 순서와 무관하다). */
            static const AudioBackendRegistrar*& getRegistrarHead()
            {
                static const AudioBackendRegistrar* s_pHead = nullptr;
                return s_pHead;
            }
        };

        /**
         * @class NullAudioBackend
         * @brief 출력 장치를 열지 않는 오디오 시스템입니다(전용 서버, 테스트). 엔진은 그대로 돌고, `update` 가 흐른 시간만큼 버퍼에 렌더합니다.
         * @details 장치를 열지 않으므로(`openDevice` 가 false) 재생 상태는 `update` 의 오프라인 렌더로 진행한다. 명시 스피커 모드(`setSpeakerMode`)는 그 레이아웃 그대로 렌더한다.
         *          5.1 · 7.1 의 결정적 오프라인 렌더와 덤프(`gv_audioDumpPath`)가 이 길을 쓴다. `createNull` 만 만들어 이 파일에 둔다.
         *          클라이언트(Windows XAudio2 · 리눅스 ALSA)에서도 `createNull` 이 쓰므로 **항상 컴파일한다** — 한 번도 컴파일되지 않는 구현은 조용히 썩는다.
         */
        class NullAudioBackend final : public IAudioService
        {
        public:
            /** @brief 출력 장치 없이 만듭니다. */
            NullAudioBackend() = default;
            /** @brief 내립니다. */
            ~NullAudioBackend() override { shutdown(); }
        };
    } // namespace

    /** @brief `-gv_audioDumpPath=<파일>.wav`: 장치 없는 오프라인 렌더를 서비스를 내릴 때(또는 레이아웃을 바꿀 때) 이 WAV(채널 마스크 포함, 최대 60 초)로 남깁니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalDiskPath, gv_audioDumpPath, "", "오프라인 오디오 렌더를 내릴 때 이 WAV 로 남김(비면 끄기)" );
} // namespace sw

namespace sw
{
    AudioBackendRegistrar::AudioBackendRegistrar( const utf8* pName, CreateFunction pCreate )
        : _pName{ pName }
        , _pCreate{ pCreate }
        , _pNext{ nullptr }
    {
        _pNext                                    = IAudioServiceInternal::getRegistrarHead();
        IAudioServiceInternal::getRegistrarHead() = this;
    }

    const AudioBackendRegistrar* AudioBackendRegistrar::getHead()
    {
        return IAudioServiceInternal::getRegistrarHead();
    }

    unique_ptr<IAudioService> IAudioService::create()
    {
        // 플랫폼 클라이언트 빌드는 백엔드 하나를 담는다 — 없거나 둘이면 빌드 구성의 결함이다(장치 없는 오디오로 몰래 바꾸지 않는다).
        const AudioBackendRegistrar* pRegistrar = AudioBackendRegistrar::getHead();
        if ( pRegistrar == nullptr )
            SW_FATAL( "No audio device backend is registered in this build (%#) - a client build links Audio/Windows or Audio/Linux", build::kTargetName );
        if ( pRegistrar->_pNext != nullptr )
            SW_FATAL( "Two audio device backends are registered (%#, %#) - one platform build links one", pRegistrar->_pName, pRegistrar->_pNext->_pName );
        return pRegistrar->_pCreate();
    }

    unique_ptr<IAudioService> IAudioService::createNull()
    {
        return make_unique<NullAudioBackend>();
    }

    IAudioService::IAudioService()
        : _pEngine{ make_unique<AudioEngine>() }
        , _listBusVolume{}
        , _listOfflineBlock{}
        , _listDumpSample{}
        , _musicPath{}
        , _mixerPath{}
        , _musicPlayingID{ 0 }
        , _offlineFrameDebt{ 0.0 }
        , _device{}
        , _masterVolume{ 1.0f }
        , _musicVolume{ 1.0f }
        , _sfxVolume{ 1.0f }
        , _speakerMode{ AudioSpeakerMode::Auto }
        , _bMuted{ false }
        , _bDeviceOpen{ false }
        , _bOutputOpen{ false }
        , _bInitialized{ false }
    {
    }

    IAudioService::~IAudioService() = default;

    bool IAudioService::initialize()
    {
        if ( _bInitialized )
            return true;

        // 믹서 그래프는 엔진 데이터가 기준이다. 없거나 틀리면 코드 그래프로 대신하지 않고 기동이 실패한다.
        AudioMixerDesc mixerDesc;
        if ( mixerDesc.loadFromAsset( AssetID{ kDefaultMixerPath } ) == false )
        {
            SW_LOG_ERROR( "Audio mixer graph '%#' could not be loaded", kDefaultMixerPath );
            return false;
        }
        _mixerPath = kDefaultMixerPath;

        // 협상: 장치를 열어 형식을 알고, 스피커 모드로 레이아웃을 정한 뒤 엔진을 그 레이아웃으로 만든다. 스트림은 엔진이 선 뒤에 연다.
        _device      = AudioOutputDevice{};
        _bDeviceOpen = openDevice( _device );
        if ( _bDeviceOpen == false )
            _device = AudioOutputDevice{};
        AudioChannelLayout layout{ AudioChannelLayout::Stereo };
        if ( resolveLayout( _speakerMode, _device, layout ) == false )
        {
            SW_LOG_ERROR( "The output device has %# channels (mask 0x%#), fewer than the requested speaker mode needs", _device._channelCount,
                          Fmt( _device._channelMask, Format().hex() ) );
            if ( _bDeviceOpen )
                closeDevice();
            _bDeviceOpen = false;
            return false;
        }
        if ( _pEngine->initialize( mixerDesc, layout ) == false )
        {
            SW_LOG_ERROR( "Audio engine rejected the mixer graph '%#' for %# output channels", kDefaultMixerPath, AudioChannelLayoutUtil::getChannelCount( layout ) );
            if ( _bDeviceOpen )
                closeDevice();
            _bDeviceOpen = false;
            return false;
        }

        // 설정 메뉴가 엔진보다 먼저 볼륨을 넣었을 수 있다. 가지고 있던 값을 다시 적용한다.
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kMaster ), _masterVolume );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kMusic ), _musicVolume );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kSFX ), _sfxVolume );
        _pEngine->setBusMuted( hashed_string( AudioBusNames::kMaster ), _bMuted );
        for ( const AudioBusVolume& busVolume : _listBusVolume )
        {
            _pEngine->setBusUserVolume( busVolume._bus, busVolume._volume );
        }

        _listOfflineBlock.assign( static_cast<size_t>( audio::kBlockFrameCount ) * AudioChannelLayoutUtil::getChannelCount( layout ), 0.0f );
        _listDumpSample.clear();
        _pEngine->releaseAudioThread(); // 렌더를 부를 스레드가 바뀐다(오프라인 → 장치 스레드)
        _bOutputOpen = _bDeviceOpen && startOutput( layout );
        if ( _bDeviceOpen && _bOutputOpen == false )
        {
            closeDevice();
            _bDeviceOpen = false;
            _device      = AudioOutputDevice{};
        }
        _bInitialized = true;
        SW_LOG_INFO( "Initialized (%#, %# channels).", _bOutputOpen ? "device output" : "offline - no device", AudioChannelLayoutUtil::getChannelCount( layout ) );
        return true;
    }

    bool IAudioService::resolveLayout( AudioSpeakerMode mode, const AudioOutputDevice& device, AudioChannelLayout& outLayout )
    {
        using Util                   = AudioChannelLayoutUtil;
        const bool       bHasDevice  = device._channelCount > 0;
        constexpr uint32 k71Mask     = Util::getChannelMask( AudioChannelLayout::Surround71 );
        constexpr uint32 k51BackMask = Util::getChannelMask( AudioChannelLayout::Surround51 );
        constexpr uint32 k51SideMask = Util::kSpeakerFrontLeft | Util::kSpeakerFrontRight | Util::kSpeakerFrontCenter | Util::kSpeakerLowFrequency | Util::kSpeakerSideLeft |
                                       Util::kSpeakerSideRight;
        AudioChannelLayout requested{ AudioChannelLayout::Stereo };
        switch ( mode )
        {
            case AudioSpeakerMode::Auto:
            {
                // 선언된 모드다: 장치 마스크가 다 가진 가장 큰 레이아웃, 아니면 스테레오(그보다 적은 장치는 장치가 아래로 섞는다).
                const uint32 mask = device._channelMask;
                const bool   b71  = bHasDevice && ( mask & k71Mask ) == k71Mask;
                const bool   b51  = bHasDevice && ( ( mask & k51BackMask ) == k51BackMask || ( mask & k51SideMask ) == k51SideMask );
                if ( b71 )
                    outLayout = AudioChannelLayout::Surround71;
                else if ( b51 )
                    outLayout = AudioChannelLayout::Surround51;
                else
                    outLayout = AudioChannelLayout::Stereo;
                return true;
            }
            case AudioSpeakerMode::Stereo:
            {
                requested = AudioChannelLayout::Stereo;
                break;
            }
            case AudioSpeakerMode::Surround51:
            {
                requested = AudioChannelLayout::Surround51;
                break;
            }
            case AudioSpeakerMode::Surround71:
            {
                requested = AudioChannelLayout::Surround71;
                break;
            }
            case AudioSpeakerMode::Headphones:
            {
                requested = AudioChannelLayout::Binaural;
                break;
            }
        }
        // 명시값: 장치가 없으면 그대로(오프라인 렌더), 있으면 채널이 모자랄 때 거절한다. 몰래 낮추지 않는다.
        if ( bHasDevice && device._channelCount < Util::getChannelCount( requested ) )
            return false;
        outLayout = requested;
        return true;
    }

    bool IAudioService::setSpeakerMode( AudioSpeakerMode mode )
    {
        if ( _bInitialized == false )
        {
            _speakerMode = mode;
            return true;
        }
        AudioChannelLayout layout{ AudioChannelLayout::Stereo };
        if ( resolveLayout( mode, _device, layout ) == false )
        {
            SW_LOG_ERROR( "The output device has %# channels, fewer than the requested speaker mode needs - keeping the current mode", _device._channelCount );
            return false;
        }
        if ( layout == _pEngine->getChannelLayout() )
        {
            _speakerMode = mode;
            return true;
        }
        // 레이아웃은 장치가 엔진을 당기지 않을 때만 바꾼다. 스트림을 닫고, 바꾸고, 엔진의 (새 또는 그대로인) 레이아웃으로 다시 연다.
        if ( _bOutputOpen )
            stopOutput();
        _bOutputOpen = false;
        _pEngine->releaseAudioThread(); // 장치 스레드가 끝났다 — 다음 렌더 스레드(오프라인 · 종료)가 받는다
        // 덤프 파일은 채널 수가 하나다. 옛 레이아웃으로 쌓은 것을 먼저 쓴다.
        writeOfflineDump();
        const bool               bChanged = _pEngine->setChannelLayout( layout );
        const AudioChannelLayout current  = _pEngine->getChannelLayout();
        _listOfflineBlock.assign( static_cast<size_t>( audio::kBlockFrameCount ) * AudioChannelLayoutUtil::getChannelCount( current ), 0.0f );
        if ( bChanged )
            _speakerMode = mode;
        if ( _bDeviceOpen )
        {
            _pEngine->releaseAudioThread(); // 레이아웃을 바꾼 게임 스레드에서 새 장치 스레드로 넘긴다
            _bOutputOpen = startOutput( current );
            if ( _bOutputOpen == false )
            {
                closeDevice();
                _bDeviceOpen = false;
                _device      = AudioOutputDevice{};
                SW_LOG_WARNING( "Could not reopen the output stream - rendering offline." );
            }
        }
        return bChanged;
    }

    void IAudioService::writeOfflineDump()
    {
        if ( _listDumpSample.empty() )
            return;
        const string             path         = gv_audioDumpPath;
        const AudioChannelLayout layout       = _pEngine->getChannelLayout();
        const uint32             channelCount = AudioChannelLayoutUtil::getChannelCount( layout );
        vector<uint8>            bytes;
        AudioWAVEncoder::encodeFloat32( _listDumpSample.data(), static_cast<uint32>( _listDumpSample.size() / channelCount ), channelCount,
                                        AudioChannelLayoutUtil::getChannelMask( layout ), audio::kSampleRate, bytes );
        _listDumpSample.clear();
        if ( FileUtil::writeFile( path, bytes.data(), bytes.size() ) == false )
        {
            SW_LOG_ERROR( "Could not write the audio dump '%#'", path );
            return;
        }
        SW_LOG_INFO( "Audio dump written: %# (%# channels)", path, channelCount );
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
            stopOutput();
        _bOutputOpen = false;
        _pEngine->releaseAudioThread(); // 장치 스레드가 끝났다 — 다음 렌더 스레드(오프라인 · 종료)가 받는다
        if ( _bDeviceOpen )
            closeDevice();
        _bDeviceOpen = false;
        _device      = AudioOutputDevice{};
        writeOfflineDump();
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
        const bool   bDumping  = gv_audioDumpPath.empty() == false;
        const size_t dumpLimit = static_cast<size_t>( IAudioServiceInternal::kMaxDumpSeconds ) * audio::kSampleRate * _listOfflineBlock.size() / audio::kBlockFrameCount;
        while ( _offlineFrameDebt >= static_cast<float64>( audio::kBlockFrameCount ) )
        {
            _pEngine->render( _listOfflineBlock.data(), audio::kBlockFrameCount );
            _offlineFrameDebt -= static_cast<float64>( audio::kBlockFrameCount );
            const bool bDumpFull = _listDumpSample.size() >= dumpLimit;
            if ( bDumping && bDumpFull == false )
                _listDumpSample.insert( _listDumpSample.end(), _listOfflineBlock.begin(), _listOfflineBlock.end() );
        }
    }

    bool IAudioService::play( const AssetID& assetID, const hashed_string& bus )
    {
        const string_view path = assetID.view();
        SW_MEMORY_SCOPE( Audio );
        if ( _bInitialized == false || path.empty() )
            return false;
        if ( AssetIO::hasAsset( assetID ) == false )
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

    bool IAudioService::playMusic( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        if ( _bInitialized == false || path.empty() )
            return false;
        if ( _musicPath == path && _pEngine->isPlaying( _musicPlayingID ) )
            return true;
        // 있는 곡인지 **멈추기 전에** 확인한다. 없는 곡을 요청했다가 재생 중이던 BGM 만 꺼지면 안 된다.
        if ( AssetIO::hasAsset( assetID ) == false )
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

    bool IAudioService::playAdaptiveMusic( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        if ( _bInitialized == false || path.empty() )
            return false;
        if ( _musicPath == path && _pEngine->isPlaying( _musicPlayingID ) )
            return true;
        shared_ptr<AudioMusicDesc> pMusic = make_shared<AudioMusicDesc>();
        if ( pMusic->loadFromAsset( assetID ) == false )
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

    bool IAudioService::loadMixer( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        AudioMixerDesc    mixerDesc;
        if ( mixerDesc.loadFromAsset( assetID ) == false )
            return false;
        if ( _pEngine->loadMixer( mixerDesc ) == false )
            return false;
        _mixerPath = string( assetPath );
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

    void IAudioService::setSFXVolume( float32 volume )
    {
        _sfxVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kSFX ), _sfxVolume );
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
        if ( bus == hashed_string( AudioBusNames::kSFX ) )
        {
            setSFXVolume( volume );
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
        if ( bus == hashed_string( AudioBusNames::kSFX ) )
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

    span<const utf8* const> IAudioService::getSubsystemDependents() const
    {
        static constexpr const utf8* kArrDependent[] = { SceneManager::kSubsystemName };
        return kArrDependent;
    }

    bool IAudioService::isDataFile( string_view assetPath ) const
    {
        if ( _bInitialized == false || assetPath.empty() )
            return false;
        const bool    bMixer = _mixerPath.empty() == false && StringUtil::equals( FileUtil::normalizeSeparators( _mixerPath ), FileUtil::normalizeSeparators( assetPath ), true );
        hashed_string libraryName;
        hashed_string graphPath;
        const bool    bLibrary = _pEngine->findEventLibraryName( assetPath, libraryName );
        const bool    bGraph   = findLoadedGraphPath( assetPath, graphPath );
        return bMixer || bLibrary || bGraph;
    }

    bool IAudioService::findLoadedGraphPath( string_view assetPath, hashed_string& outGraphPath ) const
    {
        // 소리 그래프는 이벤트가 클립처럼 고르므로 클립 캐시가 읽어 둔 것만 다시 읽는다(아직 재생하지 않은 그래프는 처음 재생할 때 새로 읽힌다).
        return StringUtil::endsWith( assetPath, AudioGraphDef::kExtension, true ) && _pEngine->getClipStore().findReadyPath( assetPath, outGraphPath );
    }

    bool IAudioService::reloadDataFile( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        if ( _bInitialized == false || assetPath.empty() )
            return false;
        const bool    bMixer = _mixerPath.empty() == false && StringUtil::equals( FileUtil::normalizeSeparators( _mixerPath ), FileUtil::normalizeSeparators( assetPath ), true );
        hashed_string graphPath;
        if ( findLoadedGraphPath( assetPath, graphPath ) )
        {
            const bool bReloaded = _pEngine->getClipStore().reloadClip( graphPath );
            if ( bReloaded )
                SW_LOG_INFO( "Audio graph reloaded (playing voices finish with the previous graph): %#", assetPath );
            return bReloaded;
        }
        hashed_string libraryName;
        if ( bMixer == false && _pEngine->findEventLibraryName( assetPath, libraryName ) == false )
            return false;
        if ( bMixer )
        {
            const bool bReloaded = loadMixer( AssetID{ _mixerPath } );
            if ( bReloaded )
                SW_LOG_INFO( "Mixer graph reloaded: %#", assetPath );
            return bReloaded;
        }
        AudioEventLibrary library;
        if ( library.loadFromAsset( assetID ) == false )
            return false;
        // 이름은 라이브러리를 처음 올린 이름 그대로여야 같은 라이브러리를 바꾼다.
        if ( _pEngine->loadEventLibrary( libraryName, library ) == false )
            return false;
        SW_LOG_INFO( "Event library reloaded: %#", assetPath );
        return true;
    }
} // namespace sw
