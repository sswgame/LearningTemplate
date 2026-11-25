#include "pch.h"

#include "Engine/Audio/AudioService.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Registry/RegistrationChain.h"
#include "Core/String/NamedValue.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixerDef.h"
#include "Engine/Audio/AudioMusic.h"
#include "Engine/Audio/AudioRenderListener.h"
#include "Engine/Audio/Graph/AudioGraphDef.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw
{
    namespace
    {
        struct AudioServiceInternal
        {
            /** @brief 곡을 바꿀 때의 크로스페이드 길이(초)입니다. */
            static constexpr float32 kMusicCrossfadeSeconds = 0.25f;
            /** @brief 장치 없이 한 번의 update 가 렌더할 최대 시간(초)입니다. 멈춘 프레임 뒤에 몇 분을 몰아서 렌더하지 않게 합니다. */
            static constexpr float64 kMaxOfflineSecondsPerUpdate = 0.25;
            /** @brief 스피커 모드의 설정 값입니다(이름 → 값 — 열거자 순서와 묶이지 않는다). */
            static constexpr NamedValue<AudioSpeakerMode> kArrSpeakerMode[] = {
                {      "auto",       AudioSpeakerMode::Auto},
                {    "stereo",     AudioSpeakerMode::Stereo},
                {"surround51", AudioSpeakerMode::Surround51},
                {"surround71", AudioSpeakerMode::Surround71},
                {"headphones", AudioSpeakerMode::Headphones},
            };

            /** @brief 장치 백엔드 등록자 사슬입니다(상수 초기화 — 정적 초기화 순서와 무관하다). */
            static SW_DEFINE_REGISTRATION_CHAIN( getRegistrarChain, AudioBackendRegistrar )
        };
    } // namespace
} // namespace sw

namespace sw
{
    AudioBackendRegistrar::AudioBackendRegistrar( const utf8* pName, AudioOutputDeviceCreateFunction pCreate )
        : _pName{ pName }
        , _pCreate{ pCreate }
        , _pNext{ AudioServiceInternal::getRegistrarChain().pushFront( this ) }
    {
    }

    const AudioBackendRegistrar* AudioBackendRegistrar::getHead()
    {
        return AudioServiceInternal::getRegistrarChain().getHead();
    }

    unique_ptr<AudioService> AudioService::create()
    {
        // 플랫폼 클라이언트 빌드는 백엔드 하나를 담는다 — 구성 단계가 먼저 막는다(Source/Engine/CMakeLists.txt). 여기 닿으면 링크가 등록자를 버린 것이다(장치 없는 오디오로 몰래 바꾸지 않는다).
        const AudioBackendRegistrar* pRegistrar = AudioBackendRegistrar::getHead();
        if ( pRegistrar == nullptr )
            reportFatalError( "No audio device backend is registered in this build (%#) - a client build links Audio/Windows, Audio/Linux, Audio/Android or Audio/Web", build::kTargetName );
        if ( pRegistrar->_pNext != nullptr )
            reportFatalError( "Two audio device backends are registered (%#, %#) - one platform build links one", pRegistrar->_pName, pRegistrar->_pNext->_pName );
        return make_unique<AudioService>( pRegistrar->_pCreate );
    }

    unique_ptr<AudioService> AudioService::createNull()
    {
        return make_unique<AudioService>( nullptr ); // 장치 없음 — 소리 요청은 받아 오프라인으로 렌더한다(5.1 · 7.1 결정적 렌더 · 리스너가 이 길을 쓴다)
    }

    AudioService::AudioService( AudioOutputDeviceCreateFunction pCreateDevice )
        : _pEngine{ make_unique<AudioEngine>() }
        , _pOutputDevice{ pCreateDevice != nullptr ? pCreateDevice( *this ) : nullptr }
        , _soundPlayer{ *this }
        , _engineAudioDriver{ *this }
        , _listBusVolume{}
        , _listOfflineBlock{}
        , _musicPath{}
        , _mixerPath{}
        , _musicPlayingID{ 0 }
        , _offlineFrameDebt{ 0.0 }
        , _reopenSecondsLeft{ 0.0 }
        , _reopenBackoff{ ExponentialBackoff::createWithRandomSeed( 0 ) } // `initialize` 가 설정의 첫 간격으로 바꾼다 — 그 전에는 `update` 가 돌지 않는다
        , _reopenMaxDelayMs{ 0 }
        , _bOutputLost{ false }
        , _device{}
        , _masterVolume{ 1.0f }
        , _musicVolume{ 1.0f }
        , _sfxVolume{ 1.0f }
        , _speakerMode{ AudioSpeakerMode::Auto }
        , _bMuted{ SW_FALSE }
        , _bGamePaused{ SW_FALSE }
        , _bDeviceOpen{ SW_FALSE }
        , _bOutputOpen{ SW_FALSE }
        , _bInitialized{ SW_FALSE }
        , _bOutputSuspended{ SW_FALSE }
    {
    }

    AudioService::~AudioService()
    {
        shutdown();
    }

    SubsystemInitResult AudioService::initializeSubsystem()
    {
        return initialize( engine::getSubsystem<EngineConfig>()._audio ) ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
    }

    bool AudioService::initialize( const AudioConfig& config )
    {
        if ( _bInitialized )
            return true;

        // 다시 열기 간격은 설정 파일이 정한다(엔진 설정 바탕 층 — 코드 기본값으로 대신하지 않는다).
        if ( config._reopenFirstDelayMs == 0 || config._reopenMaxDelayMs < config._reopenFirstDelayMs )
        {
            SW_LOG_ERROR_ID( "AudioService.InvalidReopenDelay", "EngineConfig _audio: reopen delays must be first > 0 and max >= first (first %# ms, max %# ms)", config._reopenFirstDelayMs,
                             config._reopenMaxDelayMs );
            return false;
        }
        // 클립 캐시 예산도 설정 파일이 정한다 — 없으면 캐시가 끝없이 자라므로 기동을 멈춘다.
        if ( config._clipCacheMegabytes == 0 )
        {
            SW_LOG_ERROR_ID( "AudioService.MissingClipCacheBudget", "EngineConfig _audio: _clipCacheMegabytes must be > 0" );
            return false;
        }
        _pEngine->getClipStore().setBudgetBytes( static_cast<uint64>( config._clipCacheMegabytes ) * constant::memory::kBytesPerMegabyte );
        _reopenBackoff     = ExponentialBackoff::createWithRandomSeed( static_cast<int64>( config._reopenFirstDelayMs ) );
        _reopenMaxDelayMs  = static_cast<int64>( config._reopenMaxDelayMs );
        _reopenSecondsLeft = 0.0; // 장치 없이 시작하면 첫 update 가 바로 다시 열어 본다

        // 믹서 그래프는 엔진 데이터가 기준이다. 없거나 틀리면 코드 그래프로 대신하지 않고 기동이 실패한다.
        AudioMixerDef mixerDesc;
        if ( mixerDesc.loadFromAsset( AssetID{ kDefaultMixerPath } ) == false )
        {
            SW_LOG_ERROR_ID( "AudioService.AudioMixerGraphCould", "Audio mixer graph '%#' could not be loaded", kDefaultMixerPath );
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
            SW_LOG_ERROR_ID( "AudioService.OutputDeviceChannelsMask", "The output device has %# channels (mask 0x%#), fewer than the requested speaker mode needs", _device._channelCount,
                             Fmt( _device._channelMask, Format().hex() ) );
            if ( _bDeviceOpen )
                closeDevice();
            _bDeviceOpen = SW_FALSE;
            return false;
        }
        if ( _pEngine->initialize( mixerDesc, layout ) == false )
        {
            SW_LOG_ERROR_ID( "AudioService.AudioEngineRejectedMixer", "Audio engine rejected the mixer graph '%#' for %# output channels", kDefaultMixerPath, AudioChannelLayoutUtil::getChannelCount( layout ) );
            if ( _bDeviceOpen )
                closeDevice();
            _bDeviceOpen = SW_FALSE;
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
        _pEngine->releaseAudioThread(); // 렌더를 부를 스레드가 바뀐다(오프라인 → 장치 스레드)
        _bOutputOpen = _bDeviceOpen && startOutput( layout );
        if ( _bDeviceOpen && _bOutputOpen == false )
        {
            closeDevice();
            _bDeviceOpen = SW_FALSE;
            _device      = AudioOutputDevice{};
        }
        _bInitialized = SW_TRUE;
        SW_LOG_INFO_ID( "AudioService.InitializedChannels", "Initialized (%#, %# channels).", _bOutputOpen ? "device output" : "offline - no device", AudioChannelLayoutUtil::getChannelCount( layout ) );
        return true;
    }

    bool AudioService::tryParseSpeakerMode( string_view text, AudioSpeakerMode& outMode )
    {
        return NamedValueUtil::find( AudioServiceInternal::kArrSpeakerMode, text, outMode );
    }

    bool AudioService::resolveLayout( AudioSpeakerMode mode, const AudioOutputDevice& device, AudioChannelLayout& outLayout )
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

    bool AudioService::setSpeakerMode( AudioSpeakerMode mode )
    {
        if ( _bInitialized == false )
        {
            _speakerMode = mode;
            return true;
        }
        AudioChannelLayout layout{ AudioChannelLayout::Stereo };
        if ( resolveLayout( mode, _device, layout ) == false )
        {
            SW_LOG_ERROR_ID( "AudioService.OutputDeviceChannelsFewer", "The output device has %# channels, fewer than the requested speaker mode needs - keeping the current mode", _device._channelCount );
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
        _bOutputOpen = SW_FALSE;
        _pEngine->releaseAudioThread(); // 장치 스레드가 끝났다 — 다음 렌더 스레드(오프라인 · 종료)가 받는다
        const bool               bChanged = applyChannelLayout( layout );
        const AudioChannelLayout current  = _pEngine->getChannelLayout();
        if ( bChanged )
            _speakerMode = mode;
        if ( _bDeviceOpen )
        {
            _pEngine->releaseAudioThread(); // 레이아웃을 바꾼 게임 스레드에서 새 장치 스레드로 넘긴다
            _bOutputOpen = startOutput( current );
            if ( _bOutputOpen == false )
            {
                closeDevice();
                _bDeviceOpen = SW_FALSE;
                _device      = AudioOutputDevice{};
                SW_LOG_WARNING_ID( "AudioService.CouldNotReopenOutput", "Could not reopen the output stream - rendering offline." );
            }
        }
        return bChanged;
    }

    void AudioService::shutdown()
    {
        if ( _bInitialized == false )
            return;
        // 장치를 먼저 닫는다. 닫힌 뒤에는 장치 스레드가 render 를 호출하지 않으므로 엔진을 해제해도 된다.
        [[maybe_unused]] const AudioEngineStats stats = _pEngine->getStats();
        SW_LOG_INFO_ID( "AudioService.PlayedEventsDroppedCooldown", "Played %# events (%# dropped by cooldown / limits), peak %# mixed voices, %# voices stolen / %# dropped by a full pool, %# s rendered.",
                        stats._playedEventCount, stats._droppedEventCount, stats._peakRealVoiceCount, stats._stolenVoiceCount, stats._droppedVoiceCount,
                        stats._renderedFrameCount / audio::kSampleRate );
        if ( _bOutputOpen )
            stopOutput();
        _bOutputOpen = SW_FALSE;
        _pEngine->releaseAudioThread(); // 장치 스레드가 끝났다 — 다음 렌더 스레드(오프라인 · 종료)가 받는다
        if ( _bDeviceOpen )
            closeDevice();
        _bDeviceOpen = SW_FALSE;
        _device      = AudioOutputDevice{};
        AudioRenderListenerRegistry::notifyStreamEnded( _pEngine->getChannelLayout() );
        _pEngine->shutdown();
        _musicPath.clear();
        _musicPlayingID = 0;
        _bGamePaused    = SW_FALSE; // 다시 세운 엔진은 멈추지 않은 채 선다 — 다음 프레임이 지금 값을 넘긴다
        _bInitialized   = SW_FALSE;
        SW_LOG_INFO_ID( "AudioService.ShutDown", "Shut down." );
    }

    bool AudioService::applyChannelLayout( AudioChannelLayout layout )
    {
        if ( layout == _pEngine->getChannelLayout() )
            return true;
        // 리스너(덤프 파일)는 채널 수가 하나다. 옛 레이아웃의 스트림을 먼저 끝낸다.
        AudioRenderListenerRegistry::notifyStreamEnded( _pEngine->getChannelLayout() );
        const bool bChanged = _pEngine->setChannelLayout( layout );
        _listOfflineBlock.assign( static_cast<size_t>( audio::kBlockFrameCount ) * AudioChannelLayoutUtil::getChannelCount( _pEngine->getChannelLayout() ), 0.0f );
        return bChanged;
    }

    void AudioService::closeOutput()
    {
        if ( _bOutputOpen )
            stopOutput();
        _bOutputOpen = SW_FALSE;
        _pEngine->releaseAudioThread(); // 장치 스레드가 끝났다 — 오프라인 렌더(게임 스레드)가 받는다
        if ( _bDeviceOpen )
            closeDevice();
        _bDeviceOpen      = SW_FALSE;
        _device           = AudioOutputDevice{};
        _offlineFrameDebt = 0.0;
    }

    void AudioService::closeLostOutput()
    {
        _bOutputLost.store( false, std::memory_order_release );
        closeOutput();
        _reopenSecondsLeft = static_cast<float64>( _reopenBackoff.takeDelayMs( _reopenMaxDelayMs ) ) / static_cast<float64>( constant::time::kMillisecondsPerSecond );
        SW_LOG_WARNING_ID( "AudioService.OutputLost", "The audio output device was lost - rendering offline and reopening the default device" );
    }

    void AudioService::suspendOutput()
    {
        if ( _bInitialized == false || _bOutputSuspended != 0 )
            return;
        _bOutputSuspended = SW_TRUE;
        closeOutput();
        SW_LOG_INFO_ID( "AudioService.OutputSuspended", "Audio output closed while the app is suspended" );
    }

    void AudioService::resumeOutput()
    {
        if ( _bOutputSuspended == 0 )
            return;
        _bOutputSuspended  = SW_FALSE;
        _reopenSecondsLeft = 0.0; // 다음 update 가 바로 연다 — 유실이 아니라 멈춤이었으니 백오프를 기다리지 않는다
        _reopenBackoff.reset();
    }

    void AudioService::retryOpenOutput( float32 deltaSeconds )
    {
        _reopenSecondsLeft -= static_cast<float64>( deltaSeconds );
        if ( _reopenSecondsLeft > 0.0 )
            return;
        _reopenSecondsLeft = static_cast<float64>( _reopenBackoff.takeDelayMs( _reopenMaxDelayMs ) ) / static_cast<float64>( constant::time::kMillisecondsPerSecond );
        AudioOutputDevice device{};
        if ( openDevice( device ) == false )
            return;
        // 처음 열 때와 같은 규칙(`resolveLayout`)으로 새 장치에 맞춘다 — 자동이면 5.1 로 돌다 꽂은 스테레오 헤드폰으로 내려가고, 명시한 모드를 못 받는 장치면 열지 않고 다시 시도한다
        // (언리얼 오디오 믹서의 장치 바꾸기도 새 장치의 채널 수로 다시 선다). 예전에는 지금 레이아웃보다 채널이 적은 장치를 끝없이 거절해 소리가 돌아오지 않았다.
        AudioChannelLayout layout{ AudioChannelLayout::Stereo };
        const bool         bResolved = resolveLayout( _speakerMode, device, layout );
        if ( bResolved )
            (void)applyChannelLayout( layout ); // 그래프가 거절하면 지금 레이아웃으로 이어 연다(이유는 엔진이 남긴다)
        layout = _pEngine->getChannelLayout();
        _pEngine->releaseAudioThread(); // 렌더를 부를 스레드가 바뀐다(오프라인 → 장치 스레드)
        if ( bResolved == false || startOutput( layout ) == false )
        {
            _pEngine->releaseAudioThread();
            closeDevice();
            return;
        }
        _device      = device;
        _bDeviceOpen = SW_TRUE;
        _bOutputOpen = SW_TRUE;
        _reopenBackoff.reset();
        SW_LOG_INFO_ID( "AudioService.OutputReopened", "Audio output reopened (%# channels)", AudioChannelLayoutUtil::getChannelCount( layout ) );
    }

    void AudioService::setGamePaused( bool bPaused )
    {
        const uint8 value = bPaused ? SW_TRUE : SW_FALSE;
        if ( _bInitialized == false || value == _bGamePaused )
            return;
        _bGamePaused = value;
        _pEngine->setGamePaused( bPaused );
    }

    void AudioService::update( float32 deltaSeconds )
    {
        const ScopedMemoryTag memoryScope{ kSubsystemMemoryTag };
        if ( _bInitialized == false )
            return;
        if ( _bOutputLost.load( std::memory_order_acquire ) )
            closeLostOutput();
        // 워커(병렬 틱)가 이번 프레임에 낸 요청을 큐에 넣고, 오디오 스레드가 돌려보낸 객체를 게임 스레드에서 놓는다.
        _pEngine->flushCommands();
        if ( _bOutputOpen || _bOutputSuspended != 0 )
            return; // 멈춘 동안은 오프라인으로도 렌더하지 않는다 — 게임 시간이 서 있다
        // 장치가 없으면 흐른 시간만큼 바로 렌더한다. 끝남, 박자, 가상 보이스가 장치가 있을 때와 같이 진행한다.
        const float64 seconds = MathUtil::clamp( static_cast<float64>( deltaSeconds ), 0.0, AudioServiceInternal::kMaxOfflineSecondsPerUpdate );
        _offlineFrameDebt += seconds * static_cast<float64>( audio::kSampleRate );
        const bool               bListened = AudioRenderListenerRegistry::getCount() > 0; // 배포본에는 리스너 등록이 없다
        const AudioChannelLayout layout    = _pEngine->getChannelLayout();
        while ( _offlineFrameDebt >= static_cast<float64>( audio::kBlockFrameCount ) )
        {
            _pEngine->render( _listOfflineBlock.data(), audio::kBlockFrameCount );
            _offlineFrameDebt -= static_cast<float64>( audio::kBlockFrameCount );
            if ( bListened )
                AudioRenderListenerRegistry::notifyRenderedBlock( _listOfflineBlock.data(), audio::kBlockFrameCount, layout );
        }
        _pEngine->flushCommands(); // 방금 이 스레드가 렌더하며 적용한 명령의 반납 객체도 이 프레임에 놓는다
        retryOpenOutput( deltaSeconds );
    }

    bool AudioService::play( const AssetID& assetID, const hashed_string& bus )
    {
        const string_view     path = assetID.view();
        const ScopedMemoryTag memoryScope{ kSubsystemMemoryTag };
        if ( _bInitialized == false || path.empty() )
            return false;
        if ( AssetIO::hasAsset( assetID ) == false )
        {
            SW_LOG_ERROR_ID( "AudioService.AudioResourceNotFound", "Audio resource not found: %#", path );
            return false;
        }
        return _pEngine->playClip( hashed_string( path ), bus, AudioClipPlayParams{} ) != 0;
    }

    bool AudioService::preload( string_view path )
    {
        const ScopedMemoryTag memoryScope{ kSubsystemMemoryTag };
        if ( path.empty() )
            return false;
        return _pEngine->getClipStore().loadClip( hashed_string( path ) ) != nullptr;
    }

    bool AudioService::playMusic( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        if ( _bInitialized == false || path.empty() )
            return false;
        if ( _musicPath == path && _pEngine->isPlaying( _musicPlayingID ) )
            return true;
        // 있는 곡인지 **멈추기 전에** 확인한다. 없는 곡을 요청했다가 재생 중이던 BGM 만 꺼지면 안 된다.
        if ( AssetIO::hasAsset( assetID ) == false )
        {
            SW_LOG_ERROR_ID( "AudioService.AudioResourceNotFound", "Audio resource not found: %#", path );
            return false;
        }
        const bool bCrossfade = _musicPlayingID != 0;
        if ( bCrossfade )
        {
            _pEngine->stop( _musicPlayingID, AudioServiceInternal::kMusicCrossfadeSeconds );
            _pEngine->stopMusic( AudioServiceInternal::kMusicCrossfadeSeconds );
        }

        AudioClipPlayParams params;
        params._bLoop         = SW_TRUE;
        params._fadeInSeconds = bCrossfade ? AudioServiceInternal::kMusicCrossfadeSeconds : 0.0f;
        _musicPath            = string( path );
        _musicPlayingID       = _pEngine->playClip( hashed_string( path ), hashed_string( AudioBusNames::kMusic ), params );
        return _musicPlayingID != 0;
    }

    bool AudioService::playAdaptiveMusic( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        if ( _bInitialized == false || path.empty() )
            return false;
        if ( _musicPath == path && _pEngine->isPlaying( _musicPlayingID ) )
            return true;
        shared_ptr<AudioMusicDef> pMusic = make_shared<AudioMusicDef>();
        if ( pMusic->loadFromAsset( assetID ) == false )
            return false;
        if ( _musicPlayingID != 0 )
            _pEngine->stop( _musicPlayingID, AudioServiceInternal::kMusicCrossfadeSeconds );
        _musicPath      = string( path );
        _musicPlayingID = _pEngine->startMusic( std::move( pMusic ), AudioServiceInternal::kMusicCrossfadeSeconds );
        return _musicPlayingID != 0;
    }

    void AudioService::stopMusic()
    {
        if ( _musicPlayingID != 0 )
            _pEngine->stop( _musicPlayingID, 0.0f );
        _pEngine->stopMusic( 0.0f );
        _musicPlayingID = 0;
        _musicPath.clear();
    }

    void AudioService::resetGameInstanceState( GameInstanceBoundary boundary )
    {
        if ( boundary != GameInstanceBoundary::Ended || _pEngine == nullptr )
            return;
        stopMusic();
        _pEngine->stopAllPlayback();
    }

    const utf8* AudioService::findGameInstanceState() const
    {
        return _musicPath.empty() ? nullptr : "music"; // 보이스는 오디오 스레드가 멈춘다(`stopAllPlayback`) — 게임 스레드의 게시 값은 블록 하나 늦다
    }

    void AudioService::pauseMusic()
    {
        if ( _musicPlayingID != 0 )
            _pEngine->setPaused( _musicPlayingID, true );
    }

    void AudioService::resumeMusic()
    {
        if ( _musicPlayingID != 0 )
            _pEngine->setPaused( _musicPlayingID, false );
    }

    bool AudioService::loadMixer( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        AudioMixerDef     mixerDesc;
        if ( mixerDesc.loadFromAsset( assetID ) == false )
            return false;
        if ( _pEngine->loadMixer( mixerDesc ) == false )
            return false;
        _mixerPath = string( assetPath );
        return true;
    }

    void AudioService::setMasterVolume( float32 volume )
    {
        _masterVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kMaster ), _masterVolume );
    }

    void AudioService::setMusicVolume( float32 volume )
    {
        _musicVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kMusic ), _musicVolume );
    }

    void AudioService::setSFXVolume( float32 volume )
    {
        _sfxVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
        _pEngine->setBusUserVolume( hashed_string( AudioBusNames::kSFX ), _sfxVolume );
    }

    void AudioService::setBusVolume( const hashed_string& bus, float32 volume )
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

    float32 AudioService::getBusVolume( const hashed_string& bus ) const
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

    void AudioService::setMute( bool bMute )
    {
        _bMuted = bMute;
        _pEngine->setBusMuted( hashed_string( AudioBusNames::kMaster ), bMute );
    }
} // namespace sw

namespace sw
{
    span<const utf8* const> AudioService::getSubsystemDependencies() const
    {
        // 다시 열기 간격을 엔진 설정에서 읽고, 믹서 그래프를 에셋으로 읽는다(팩 매니저 — 에셋 매니저가 마운트한 뒤라야 한다).
        static constexpr const utf8* kArrDependency[] = { TaskManager::kSubsystemName, ConfigManager::kSubsystemName, AssetManager::kSubsystemName };
        return kArrDependency;
    }

    span<const utf8* const> AudioService::getSubsystemDependents() const
    {
        static constexpr const utf8* kArrDependent[] = { SceneManager::kSubsystemName, UserSettingsManager::kSubsystemName };
        return kArrDependent;
    }

    bool AudioService::isDataFile( string_view assetPath ) const
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

    bool AudioService::findLoadedGraphPath( string_view assetPath, hashed_string& outGraphPath ) const
    {
        // 소리 그래프는 이벤트가 클립처럼 고르므로 클립 캐시가 읽어 둔 것만 다시 읽는다(아직 재생하지 않은 그래프는 처음 재생할 때 새로 읽힌다).
        return StringUtil::endsWith( assetPath, AudioGraphDef::kExtension, true ) && _pEngine->getClipStore().findReadyPath( assetPath, outGraphPath );
    }

    bool AudioService::reloadDataFile( const AssetID& assetID )
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
                SW_LOG_INFO_ID( "AudioService.AudioGraphReloadedPlaying", "Audio graph reloaded (playing voices finish with the previous graph): %#", assetPath );
            return bReloaded;
        }
        hashed_string libraryName;
        if ( bMixer == false && _pEngine->findEventLibraryName( assetPath, libraryName ) == false )
            return false;
        if ( bMixer )
        {
            const bool bReloaded = loadMixer( AssetID{ _mixerPath } );
            if ( bReloaded )
                SW_LOG_INFO_ID( "AudioService.MixerGraphReloaded", "Mixer graph reloaded: %#", assetPath );
            return bReloaded;
        }
        AudioEventLibrary library;
        if ( library.loadFromAsset( assetID ) == false )
            return false;
        // 이름은 라이브러리를 처음 올린 이름 그대로여야 같은 라이브러리를 바꾼다.
        if ( _pEngine->loadEventLibrary( libraryName, library ) == false )
            return false;
        SW_LOG_INFO_ID( "AudioService.EventLibraryReloaded", "Event library reloaded: %#", assetPath );
        return true;
    }
} // namespace sw

namespace sw
{
    // 전용 서버 역할(클라이언트 · 게임 구성의 `-server`)과 헤드리스 작업(단위 테스트 · 쿠킹 · 도구)은 장치를 열지 않는다 — 들을 사람이 없고, 소리 요청은 장치 없이 받는다. 전용 서버 구성은 이 폴더를 짓지 않는다(`SW_EXCLUDED_ENGINE_FOLDERS`).
    SW_ENGINE_SERVICE_SUBSYSTEM( AudioService, GameVisible, GameInstance, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        const bool    bNoDevice = build._pStartupSetup->_bDedicatedServer || build._pStartupSetup->_bHeadlessRequested;
        AudioService& audio     = scope.addSubsystem( bNoDevice ? AudioService::createNull() : AudioService::create() );
        const bool    bExposed  = scope.exposeMember<IEngineAudioDriver>( audio, audio.getEngineAudioDriver() ) && scope.exposeMember<ISoundPlayer>( audio, audio.getSoundPlayer() );
        SW_ASSERT( bExposed );
        (void)bExposed;
    } );
} // namespace sw

// 기본 믹서 그래프는 상수로 읽는다 - 쓰는 것만 담는 팩 쿠킹의 뿌리다(`AssetCookRoot.h`).
SW_ASSET_COOK_ROOT( DefaultAudioMixer, ::sw::AudioService::kDefaultMixerPath );
