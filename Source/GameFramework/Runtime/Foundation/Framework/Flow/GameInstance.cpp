#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Map/InputMapBundle.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Net/NetMessageChannel.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstanceClientHook.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IContentConfiguration.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IGameDevPart.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureComponents.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/GameLocalStore.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/GameSaveSlots.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettingsOverride.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDataWatcher.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Progression/PlayerStatsSubsystem.h"
#include "GameFramework/Runtime/Online/HTTP/TelemetryUploadLink.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"

namespace sw
{
    namespace
    {
        struct GameInstanceInternal
        {
            /** @brief 세이브 찍기(`captureSnapshot( SaveGame )`)의 프로파일러 구간 이름입니다 — 감시 값 `Save.CaptureMs` · `Save.CapturePeakMs` 의 재료(배포에는 재는 일도 칸도 없다). */
            static constexpr const utf8* kSaveCaptureScopeName = "GT.Save.capture";
            /** @brief `Save.CapturePeakMs` 가 읽는 백분위입니다 — p100(창 안 최대). */
            static constexpr uint32 kPeakPercent = 100;

            /**
             * @brief 감시 값 `Save.CaptureMs` · `Save.CapturePeakMs` 입니다 — 세이브 찍기 구간의 마지막 프레임 시간 · 측정 창 p100(밀리초).
             * @details 프로파일러가 없거나 꺼져 있으면 값이 없다(`Scene.ComponentTickMs` 와 같은 길). 마지막 프레임 값은 찍은 프레임이 지나면 0 이다.
             */
            template <bool bPeak>
            [[nodiscard]] static bool readCaptureMilliseconds( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                FrameProfiler* const pProfiler = game::findSubsystem<FrameProfiler>();
                if ( pProfiler == nullptr || pProfiler->isEnabled() == false )
                    return false;
                const uint32 slot  = pProfiler->registerScope( kSaveCaptureScopeName );
                const uint64 nanos = bPeak ? pProfiler->getPercentileNanos( slot, kPeakPercent ) : pProfiler->getLastFrameNanos( slot );
                outValue           = static_cast<float64>( nanos ) / static_cast<float64>( constant::time::kNanosecondsPerMillisecond );
                return true;
            }
        };
    } // namespace

    GameInstance* GameInstance::findRunningInstance()
    {
        return game::findSubsystem<GameInstance>();
    }

    GameInstance::GameInstance()
        : GameInstance( make_unique<GameLocalStore>() )
    {
    }

    GameInstance::GameInstance( unique_ptr<GameLocalStore> localStore )
        : _packConfig{}
        , _pWindow{ nullptr }
        , _stateSnapshot{}
        , _saveLock{}
        , _gameScope{}
        , _pItemCatalog{}
        , _pAppearanceDatabase{}
        , _pAppearanceWatcher{}
        , _pAutosave{}
        , _pGameFeatureScope{}
        , _pDevPart{}
        , _loadedAudioEvents{}
        , _listBoundCatalog{}
        , _listFollowedService{}
        , _bStarted{ SW_FALSE }
        , _bDerivedEntered{ SW_FALSE }
        , _bInputMapLoaded{ SW_FALSE }
        , _bSecuritySet{ SW_FALSE }
    {
        // 인스턴스 서브시스템 — initialize 가 onInitialize 앞에 초기화하고 shutdown 이 onShutdown 뒤에 역순으로 종료한다.
        ScreenTransitionManager& screenTransition = _gameScope.emplaceSubsystem<ScreenTransitionManager>();
        // 로딩 화면은 클라이언트 전용 부분이 세운다 — 그 소스를 짓지 않는 전용 서버에는 없다(`IGameInstanceClientHook`).
        IGameInstanceClientHook* const pClientHook    = IGameInstanceClientHook::find();
        ILoadingScreen* const          pLoadingScreen = pClientHook != nullptr ? pClientHook->emplaceLoadingScreen( _gameScope ) : nullptr;
        // 씬 흐름은 이 인스턴스의 gamesettings(`_packConfig` — 범위보다 먼저 선 멤버)와 두 형제를 빌려 든다.
        _gameScope.emplaceSubsystem<GameSceneFlow>( _packConfig._data, screenTransition, pLoadingScreen, _saveLock );
        // 로컬 저장소(세이브 슬롯)는 게임 범위 하나다 - 게임 상태 세이브 · 자동 저장 · 계정 장치 비밀이 함께 쓴다.
        (void)_gameScope.addSubsystem( std::move( localStore ) );
        // 게임 상태 세이브 슬롯(저장 · 불러오기 · 깨진 복원의 세이브 막기)은 저장소 · 씬 흐름 위에 선다.
        _gameScope.emplaceSubsystem<GameSaveSlots>( *this, _packConfig._data, _saveLock );
        // 온라인 세션은 키트 클라이언트를 묶을 뿐 연결은 게임이 연다 - 온라인을 쓰지 않는 게임에는 비용이 없다.
        _gameScope.emplaceSubsystem<OnlineSession>( _packConfig._data );
        // 텔레메트리 · 크래시 업로드는 gamesettings 에 끝점이 있을 때만 엔진 업로더에 HTTP 클라이언트를 끼운다 - 없으면 아무 일도 하지 않는다.
        _gameScope.emplaceSubsystem<TelemetryUploadLink>( _packConfig._data );
        // 플레이어 통계 · 업적은 gamesettings 에 통계 카탈로그가 있을 때만 선다 - 플랫폼 서비스(지금은 로컬 구현)까지 잇는다.
        _gameScope.emplaceSubsystem<PlayerStatsSubsystem>( _packConfig._data );
        // 게임 기능이 붙이는 컴포넌트의 규칙 — 기능 범위(게임 · 미니게임)보다 먼저 서고 늦게 사라진다.
        _gameScope.emplaceSubsystem<GameFeatureComponents>();
        // 미니게임 흐름은 늘 선다(미니게임을 쓰지 않는 게임에서는 아무 일도 안 한다) — 들어갈 때 게임 기능을 이 게임 범위에 걸고 나올 때 거두며, 종료가 `onShutdown` 뒤에 켜진 것을 끈다.
        _gameScope.emplaceSubsystem<MinigameFlow>( _gameScope, _packConfig, *this );
    }

    GameInstance::~GameInstance()
    {
        // shutdown 을 거치지 않고 지워져도 지금 범위가 지운 범위를 가리키지 않게 한다(범위에 건 자신도 함께 떨어진다).
        detachGameScope();
    }

    void GameInstance::detachGameScope()
    {
        SubsystemScope* pCurrent = game::findGameScope();
        if ( pCurrent == &_gameScope )
        {
            // 이어 둔 앞 범위로 되돌린다(테스트가 건 범위). 게임 범위만 상위로 잇므로 그 범위는 SubsystemScope 다.
            game::setGameScope( static_cast<SubsystemScope*>( const_cast<ISubsystemLocator*>( _gameScope.getParentScope() ) ) );
        }
        else if ( pCurrent != nullptr && pCurrent->getParentScope() == &_gameScope )
        {
            // 이 인스턴스 위에 테스트 범위(`ScopedGameScope`)가 이어졌다 — 지워질 이 범위 대신 이 범위의 상위를 잇게 한다. 인스턴스는 프로세스에 하나라 새 인스턴스가 잇는 일은 없다.
            pCurrent->setParentScope( _gameScope.getParentScope() );
        }
        _gameScope.setParentScope( nullptr );
    }

    void GameInstance::reportScopedServiceConflict( uint64 typeHash ) const
    {
        SW_LOG_ERROR_ID( "GameInstance.GameScopedServiceAlready", "Game scoped service %# is already bound to another instance - two kits share one service type", typeHash );
    }

    bool GameInstance::validateEngineServices( const GenericWindow* pWindow )
    {
        // 네트워크 세션은 필수 엔진 서비스다(`SW_SERVICE_SLOT( GameNetSession, Required )`, 게임 노출). 없으면 세션 없이 조용히 도는 대신 아무것도 걸기 전에 멈춘다.
        if ( game::findSubsystem<GameNetSession>() == nullptr )
        {
            SW_LOG_ERROR_ID( "GameInstance.GameInstanceCannotStart", "Game instance cannot start - the required engine service GameNetSession is not bound to the game services" );
            return false;
        }
        // UI(입력 포획 창구로 본다) · 사용자 설정은 Client 호스트 서비스다. 창이 있는 호스트(클라이언트)에 없으면 호스트의 결함이라 멈춘다 — 타이틀 명령 · 설정 재적용을 조용히 건너뛰지 않는다.
        if ( pWindow != nullptr && ( game::findSubsystem<IInputCaptureQuery>() == nullptr || game::findSubsystem<UserSettingsManager>() == nullptr ) )
        {
            SW_LOG_ERROR_ID( "GameInstance.GameInstanceCannotStart2", "Game instance cannot start - a windowed (client) host must bind the engine services UISystem and UserSettingsManager to the game services" );
            return false;
        }
        return true;
    }

    bool GameInstance::initialize( GenericWindow* pWindow )
    {
        // 프로세스당 하나다 — 다른 인스턴스가 돌면 아무것도 걸지 않고 멈춘다(앞 인스턴스의 지금 범위 · 서비스를 덮지 않는다).
        const GameInstance* const pRunning = findRunningInstance();
        if ( pRunning != nullptr && pRunning != this )
        {
            SW_LOG_ERROR_ID( "GameInstance.SecondGameInstance", "Game instance cannot start - another game instance is running in this process (one per process; the host shuts the old one down first)" );
            return false;
        }
        // 필수 엔진 서비스가 없으면 아무것도 걸기 전에 멈춘다. 쓰는 곳(입력 맵 · 세션 명령 · 설정 재적용)은 그 자리에서 게임 창구로 찾는다.
        if ( validateEngineServices( pWindow ) == false )
            return false;
        _pWindow = pWindow;
        if ( startInstance() == false )
        {
            // 되감기는 한 길이다 — 세운 만큼만 거두는 종료가 같은 일을 한다. 호스트는 이 인스턴스를 부순다(`ModuleInstanceUtil::createInstance`).
            SW_LOG_ERROR_ID( "GameInstance.GameInstanceRolledBack", "Game instance did not start - everything it set up is rolled back (see the error above)" );
            shutdown();
            return false;
        }
        _bStarted = SW_TRUE;
        // 창이 없는 호스트에는 UI 가 없다 — UI 전역 명령 표도 쓰지 않는다. 파생 초기화 뒤에 건다("타이틀로" 는 첫 씬을 요청한 뒤의 일이다).
        if ( IGameInstanceClientHook* const pClientHook = IGameInstanceClientHook::find() )
            pClientHook->bindSessionCommands( getSubsystem<GameSceneFlow>() );

        // 플레이어 설정을 다시 넣는다. 언어 팩과 입력 맵(키 바인딩, 누르기/토글)은 위에서 막 생겼고, 엔진 기동 때의 적용은 그 대상이 없을 때 한 것이다.
        // 액션을 더 늦게(컴포넌트 시작 때) 만드는 게임은 그 뒤에 `UserSettingsManager::reapplyAll` 을 부른다. 창이 없는 호스트(전용 서버 · 테스트 하네스)는 플레이어 설정을
        // 다시 넣을 대상이 없다 — 하네스가 개발자의 저장된 설정을 테스트 프로세스의 변수에 넣지 않게 창으로 가른다.
        UserSettingsManager* const pUserSettings = game::findSubsystem<UserSettingsManager>();
        if ( _pWindow != nullptr && pUserSettings != nullptr )
            pUserSettings->reapplyAll();
        return true;
    }

    bool GameInstance::startInstance()
    {
        configurePackConfig( _packConfig );
        const GameConfig* const pGameConfig = game::findSubsystem<const GameConfig>();
        if ( pGameConfig == nullptr )
        {
            SW_LOG_ERROR_ID( "GameInstance.GameInstanceCannotStart3", "Game instance cannot start - the required engine service GameConfig is missing from the game services" );
            return false;
        }
        _packConfig._packRoot = pGameConfig->_packRoot;
        // gamesettings.xml 은 필수다. 못 읽으면 기본값으로 서지 않고 게임 초기화를 멈춘다.
        if ( loadGameSettings() == false )
        {
            SW_LOG_ERROR_ID( "GameInstance.GameInstanceCannotStart4", "Game instance cannot start - gamesettings could not be loaded from pack '%#'", _packConfig._packRoot.c_str() );
            return false;
        }
        // 이 인스턴스의 범위를 지금 게임 범위로 건다 — 파생의 onInitialize 가 거는 카탈로그와 이 설정을 게임 서비스가 찾는다.
        // 앞 범위(테스트가 건 범위 · 리로드에서 아직 안 내려간 옛 인스턴스의 범위)를 상위로 이어, 거기 걸린 것도 찾힌다.
        if ( game::findGameScope() != &_gameScope )
            _gameScope.setParentScope( game::findGameScope() );
        game::setGameScope( &_gameScope );
        // 자신도 건다 — 도는 인스턴스(`findRunningInstance`)는 지금 게임 범위에서 찾는다.
        if ( bindScopedService( this ) == false || bindScopedService( &_packConfig._data ) == false || bindScopedService( &_saveLock ) == false || applyPackConfig() == false )
            return false;
        // 인스턴스 서브시스템을 onInitialize 앞에 세운다(의존 순서: 화면 전환 · 로딩 화면 → 씬 흐름) — 씬 흐름이 씬 로드 받아들임 알림을 걸어 첫 씬 요청(`startFirstScene`)이
        // 로딩 화면을 띄우고, 파생이 네트워크 역할을 보고 키트 처리기를 단다. 하나라도 서지 않으면(로딩 팁 파일을 못 읽었다) 게임을 띄우지 않는다 — 파생 초기화도 부르지 않는다.
        if ( _gameScope.getCollection().initializeAll( SubsystemHostMask::All ) == false )
        {
            SW_LOG_ERROR_ID( "GameInstance.GameInstanceSubsystemsDid", "Game instance subsystems did not all initialize: %#", _gameScope.getCollection().getError().c_str() );
            return false;
        }
        // 네트워크 세션은 엔진 수명이다 — 처음이면 실행 인자로 열고, 리로드로 다시 선 인스턴스는 열린 세션을 이어받는다(클라이언트가 끊기지 않는다).
        // 서버 · 클라이언트로 띄웠는데 열지 못하면(틀린 인자 · 포트 사용 중) 혼자 하는 게임으로 서지 않는다.
        // 티켓 게임 포트가 쓸 암호 제공자(OpenSSL)는 GameFramework 에 있어 Engine 이 모른다 — 세션을 열기 전에 넣는다.
        // 콘텐츠 구성(켠 모드)은 세션을 열기 전에 정한다 — 그 해시가 와이어 버전에 섞여 구성이 다른 상대가 핸드셰이크에서 갈린다.
        if ( onPrepareContent( ContentPreparationToken{} ) == false )
        {
            SW_LOG_ERROR_ID( "GameInstance.ContentNotPrepared", "Game instance cannot start - the content configuration (mods) could not be prepared (see the error above)" );
            return false;
        }
        // 복제 타입도 세션을 열기 전에 정한다 — 그 스키마 해시가 같은 함수로 와이어 버전에 섞인다(`GameNetSession::combineWireContent`).
        NetMessageChannel replicatedChannel;
        if ( onRegisterReplicatedTypes( replicatedChannel ) == false )
        {
            SW_LOG_ERROR_ID( "GameInstance.ReplicatedTypesNotRegistered", "Game instance cannot start - a replicated type could not be registered (see the error above)" );
            return false;
        }
        const IContentConfiguration* const pContent = game::findSubsystem<IContentConfiguration>();
        GameNetWireContent                 wireContent;
        wireContent._contentConfigHash    = pContent != nullptr ? pContent->getContentConfigHash() : 0;
        wireContent._replicatedSchemaHash = replicatedChannel.computeSchemaHash();
        GameNetSession& netSession        = game::getSubsystem<GameNetSession>(); // `validateEngineServices` 가 있음을 확인했다
        netSession.getConnectSecurity().setProvider( &game::getSubsystem<INetSecurityProvider>() );
        _bSecuritySet = SW_TRUE;
        if ( netSession.openFromLaunch( wireContent ) == false )
        {
            SW_LOG_ERROR_ID( "GameInstance.GameInstanceCannotStart5", "Game instance cannot start - the net session asked for by the launch arguments or the server config did not open" );
            return false;
        }
        // 열린 세션의 창구 · 라우터 · 역할을 씬 매니저에 건다 — 이 뒤에 받아들이는 씬(월드)마다 넷 링크가 올라 컴포넌트가 자기 씬에서 찾는다. 세션이 없으면 걸지 않는다(혼자 하는 게임).
        if ( netSession.isActive() )
        {
            SceneManager* pLinkSceneManager = game::findSubsystem<SceneManager>();
            if ( pLinkSceneManager != nullptr )
                pLinkSceneManager->setNetLink( netSession.makeNetLink() );
        }
        // 사운드 이벤트는 gamesettings 가 기준이다. 적었는데 못 읽으면 게임을 띄우지 않는다(소리 없이 진행하지 않는다). 입력 맵을 적었는데 입력 서비스가 없어도 같다.
        if ( loadAudioEvents() == false || loadDeclaredCharacterData() == false || startAutosave() == false )
            return false;
        _bDerivedEntered = SW_TRUE;
        if ( onInitialize() == false )
            return false;
        // 게임 범위 기능(gamesettings `gameFeatures`)은 파생 초기화 뒤 · 첫 씬 전에 켠다 — 첫 씬부터 그 카탈로그를 읽는다. 못 켜면 필수 데이터처럼 게임을 띄우지 않는다.
        if ( activateGameFeatures() == false )
            return false;
        startFirstScene();
        // 개발 전용 부분은 게임이 다 선 뒤에 붙는다(게임 `Dev/` 의 공장 — 배포 구성에는 없다).
        if ( IGameDevPartFactory* const pDevFactory = game::findSubsystem<IGameDevPartFactory>() )
            _pDevPart = pDevFactory->createDevPart();
        return true;
    }

    void GameInstance::startFirstScene()
    {
        // 실행 전역 변수 `gv_firstMinigame` 는 첫 씬(`gv_firstScene` > 타이틀 > 시작 맵)보다 위다 — 그 미니게임의 맵으로 들어간다. 지면 씬 없이 오류로 남는다(다른 씬으로 대신하지 않는다).
        if ( getSubsystem<MinigameFlow>().enterFirstMinigame() )
            return;
        (void)getSubsystem<GameSceneFlow>().requestFirstScene(); // 실패는 씬 흐름이 알리고, 다시 선 인스턴스는 살아 있는 씬을 쓴다
    }

    void GameInstance::shutdown()
    {
        // 다 선 인스턴스만 종료 저장을 맡긴다 - 씬이 살아 있는 동안 맡기고 쓰기는 저장소가 끝낸다. 정책이 끄면(`quit`) 맡기지 않는다. 되감기(서다 멈춤)는 저장할 게임이 없다.
        if ( _pAutosave != nullptr && _bStarted == SW_TRUE )
            (void)_pAutosave->notifyQuit();
        getSubsystem<GameSceneFlow>().unregisterSessionCommands(); // 걸지 않았으면(창이 없는 호스트 · 타이틀 씬 없음 · 되감기) 빈 구독이다
        _pDevPart.reset();                                         // 게임이 세운 것을 보던 부분이라 파생보다 먼저 진다(남길 기록은 소멸자가 쓴다)
        if ( _bDerivedEntered == SW_TRUE )
            onShutdown();
        _bDerivedEntered = SW_FALSE;
        _bStarted        = SW_FALSE;
        if ( _loadedAudioEvents.empty() == false )
        {
            GameSound::unloadEvents( _loadedAudioEvents );
            _loadedAudioEvents.clear();
        }
        // 인스턴스 서브시스템(씬 흐름 → 로딩 화면 · 전환)을 onShutdown 뒤에 내린다 — 씬 흐름이 받아들임 알림을 풀고 맡긴 로드를 잊는다.
        // 네트워크 세션은 엔진 수명이라 닫지 않는다(리로드를 넘어 연결을 지킨다).
        _gameScope.getCollection().shutdownAll();
        // 게임 범위 기능은 미니게임 흐름(서브시스템)이 미니게임을 끈 뒤에 끈다 — 미니게임의 해제 검사는 이것이 켜진 채로 찍었다.
        if ( _pGameFeatureScope != nullptr )
            (void)_pGameFeatureScope->deactivate(); // 남은 것은 해제 검사가 오류로 알렸다
        _pGameFeatureScope.reset();
        // 제공자는 GameFramework 의 것이라 이 인스턴스가 내려가면 거둔다(다시 서는 인스턴스가 다시 넣는다). 열린 세션은 이미 받은 포인터로 계속 돈다.
        GameNetSession* const pNetSession = game::findSubsystem<GameNetSession>();
        if ( _bSecuritySet == SW_TRUE && pNetSession != nullptr )
            pNetSession->getConnectSecurity().setProvider( nullptr );
        _bSecuritySet              = SW_FALSE;
        InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( _bInputMapLoaded == SW_TRUE && pInput != nullptr )
            pInput->getInputMap().clear();
        _bInputMapLoaded = SW_FALSE;
        detachGameScope();
        // 범위를 뗀 뒤에 놓는다 — 걸어 둔 서비스(외형 · 아이템)를 찾는 쪽이 이제 없다.
        _pAppearanceWatcher.reset();
        _pAppearanceDatabase.reset();
        _pItemCatalog.reset();
        _pAutosave.reset();
        _pWindow = nullptr;
    }

    bool GameInstance::loadGameSettings()
    {
        // 컴포넌트 기본값 경로의 주인은 엔진 범위의 `ComponentDefaults` 다(정적 칸을 두지 않는다). 선택 조회: 엔진 서비스 없이 도는 하네스에서는 기본값을 쓰지 않는다.
        ComponentDefaults* const pComponentDefaults = game::findSubsystem<ComponentDefaults>();
        if ( pComponentDefaults != nullptr )
            pComponentDefaults->setPath( _packConfig.resolve( constant::path::kGameSettingsFile ) );
        if ( _packConfig.load( constant::path::kGameSettingsFile ) == false )
            return false;
        // 개발 구성의 실행 인자 덮어쓰기(로컬 수신기 · 로컬 서버로 끝점을 돌린다)가 서비스로 걸기 전에 고친다. 배포에는 창구가 없다.
        if ( IGameSettingsOverride* const pOverride = IGameSettingsOverride::find() )
            pOverride->apply( _packConfig._data );
        return true;
    }

    bool GameInstance::applyPackConfig()
    {
        const GameSettings& settings = _packConfig._data;

        // 게임의 로컬라이제이션 프로젝트를 올린다. 엔진 프로젝트는 엔진이 기동 때 올렸다.
        if ( settings._localizationProject.isSet() )
        {
            LocalizationManager* pLocalization = game::findSubsystem<LocalizationManager>();
            if ( pLocalization == nullptr )
            {
                SW_LOG_ERROR_ID( "GameInstance.GameSettingsLocalizationProjectNeeds", "GameSettings localization project '%#' needs the engine service LocalizationManager, which is not bound to the game services",
                                 settings._localizationProject.c_str() );
            }
            else if ( pLocalization->initialize( AssetID{ settings._localizationProject.get() }, settings._defaultLanguage, settings._fallbackLanguage ) == false )
            {
                SW_LOG_WARNING_ID( "GameInstance.GameSettingsLocalizationProjectCould", "GameSettings localization project '%#' could not be loaded - strings show their keys", settings._localizationProject.c_str() );
            }
        }

        // 게임플레이 입력 맵은 `InputManager::beginFrame` 이 프레임마다 갱신하는 통합 맵이며, `PlayerController` 가 읽는다. 공통 맵 · 키트 맵 · 게임 차이의 묶음이다(`InputMapBundle`).
        // 묶음이 깨졌으면 게임을 띄우지 않는다 — 앞 게임의 바인딩을 그대로 들고 도는 것이 가장 찾기 어렵다. 겹침 검사가 여기서 게임 시작 전에 돈다.
        const AssetID   settingsAssetID{ _packConfig.resolve( constant::path::kGameSettingsFile ) };
        vector<AssetID> listInputMap;
        if ( InputMapBundle::collectAssets( settingsAssetID, listInputMap ) == false )
            return false;
        if ( listInputMap.empty() )
            return true;
        InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
        {
            SW_LOG_ERROR_ID( "GameInstance.GameSettingsInputMapNeeds", "GameSettings input maps of '%#' need the engine service InputManager, which is not bound to the game services - the game does not start",
                             settingsAssetID.c_str() );
            return false;
        }
        _bInputMapLoaded = SW_TRUE; // 읽다 만 맵도 이 인스턴스 것이다
        return pInput->getInputMap().loadBundle( listInputMap );
    }

    bool GameInstance::loadAudioEvents()
    {
        const DataPath& audioEvents = _packConfig._data._audioEvents;
        if ( audioEvents.isSet() == false )
            return true; // 적지 않은 게임은 사운드 이벤트가 없다
        const string& path = audioEvents.get();
        if ( GameSound::loadEvents( AssetID{ path } ) == false )
        {
            SW_LOG_ERROR_ID( "GameInstance.GameSettingsAudioEventsCouldNot", "GameSettings audioEvents '%#' could not be loaded (missing or invalid - see the messages above) - the game does not start", path.c_str() );
            return false;
        }
        _loadedAudioEvents = path;
        return true;
    }

    bool GameInstance::startAutosave()
    {
        const DataPath& settingsPath = _packConfig._data._autosaveSettings;
        if ( settingsPath.isSet() == false )
            return true; // 적지 않은 게임은 자동 저장이 없다
        AutosaveSettings settings;
        if ( settings.loadFromAsset( AssetID{ settingsPath.get() } ) == false )
        {
            SW_LOG_ERROR_ID( "GameInstance.GameSettingsAutosaveCouldNot", "GameSettings autosaveSettings '%#' could not be loaded (missing or invalid - see the messages above) - the game does not start", settingsPath.c_str() );
            return false;
        }
        _pAutosave = make_unique<AutosaveManager>();
        if ( _pAutosave->initialize( settings, getSubsystem<GameLocalStore>(), _saveLock,
                                     SW_DELEGATE_LAMBDA( AutosaveManager::SaveDelegate, [this]( vector<uint8>& outBytes )
        { return captureSnapshot( SerializePurpose::SaveGame, outBytes ); } ),
                                     SW_DELEGATE_LAMBDA( AutosaveManager::LoadDelegate, [this]( const string& slot, const vector<uint8>& bytes, const AutosaveManager::RestoreCompletedFunction& onCompleted )
        {
            getSubsystem<GameSaveSlots>().applySlot( slot, bytes, onCompleted ); // 슬롯 불러오기와 같은 길 — 찍은 씬으로 옮긴 뒤 적용한다(감사 G-7)
            return true;
        } ) ) == false )
            return false; // 까닭은 관리자가 남겼다
        return bindScopedService( _pAutosave.get() );
    }

    bool GameInstance::activateGameFeatures()
    {
        // 게임 모듈의 기능(`SW_GAME_FEATURE`)은 인스턴스를 만들기 전에 붙는다(`GameModuleManager::bindGameAPI` → `registerModuleTypes` → 등록자 붙이기 → `createGameInstance`).
        // 그래도 없으면 `activateFeatures` 가 모르는 기능으로 알리고 여기서 진다 — 게임을 띄우지 않는다(기능 없이 켜지 않는다).
        InputManager* const pInput = game::findSubsystem<InputManager>(); // 입력 서비스가 없는 호스트(전용 서버)는 입력 레이어를 걸지 않는다
        _pGameFeatureScope         = make_unique<GameFeatureScope>( _gameScope, game::getSubsystem<GlobalVariableManager>(), _packConfig, pInput != nullptr ? &pInput->getInputMap() : nullptr,
                                                                    hashed_string( "gameFeatures" ), GameFeatureScope::CatalogRule::PackData, vector<hashed_string>{} );
        if ( _pGameFeatureScope->activateFeatures( _packConfig._data._listGameFeature ) == false )
        {
            SW_LOG_ERROR_ID( "GameInstance.GameFeaturesNotActivated", "The game features (GameSettings gameFeatures) could not be activated (see the messages above) - the game does not start" );
            _pGameFeatureScope.reset(); // 그때까지 건 것을 거둔다(해제 검사 포함)
            return false;
        }
        _pGameFeatureScope->finishActivation();
        return true;
    }

    bool GameInstance::loadDeclaredCharacterData()
    {
        const flat_map<string, string>& mapRequired = _packConfig._data._mapRequiredDataPack;
        if ( mapRequired.find( ItemCatalog::kDataKind ) != mapRequired.end() )
        {
            if ( loadRequiredCatalog( _pItemCatalog ) == false )
                return false;
        }
        if ( mapRequired.find( AppearanceDatabase::kDataKind ) == mapRequired.end() )
            return true;
        _pAppearanceDatabase = make_unique<AppearanceDatabase>();
        _pAppearanceWatcher  = make_unique<AppearanceDataWatcher>();
        const bool bLoaded   = loadRequiredData( AppearanceDatabase::kDataKind, [this]( const string& folder )
          {
            if ( _pAppearanceDatabase->loadFromFolder( folder, _pItemCatalog.get() ) == false )
            {
                SW_LOG_ERROR_ID( "GameInstance.AppearanceDataReport", "Appearance data report - %#", _pAppearanceDatabase->getReport().joined().c_str() );
                return false;
            }
            // 고친 외형 파일을 실행 중에 다시 읽는다. 팩 데이터면 디스크 폴더가 없어 감시하지 않는다(그 까닭은 감시자가 알린다).
            (void)_pAppearanceWatcher->initialize( folder );
            return true;
        } );
        if ( bLoaded == false )
            return false;
        return bindScopedService( _pAppearanceDatabase.get() );
    }

    bool GameInstance::makeRequiredDataPath( string_view kindLabel, string& outPath ) const
    {
        // 공용 팩에서 읽는 종류는 gamesettings 의 <RequiredData pack> 이 팩을 정한다 — 자리 규칙은 어느 팩이든 등록 줄 하나다(`GamePackConfig::makeRequiredDataPath`).
        const expected<void> path = _packConfig.makeRequiredDataPath( kindLabel, outPath );
        if ( path.has_value() )
            return true;
        SW_LOG_ERROR_ID( "GameInstance.RequiredGameDataNo", "Required game data '%#' has no path in pack '%#': %# (%#) - the game does not start", kindLabel, _packConfig._packRoot.c_str(),
                         path.error().message(), path.error().detail() );
        return false;
    }

    void GameInstance::reportRequiredDataFailure( string_view kindLabel, const AssetID& assetID ) const
    {
        const string& path    = assetID.str();
        const utf8*   pReason = AssetIO::hasAsset( assetID ) ? "the loader rejected it (see the messages above)" : "it is not in the pack or the loader rejected it (see the messages above)";
        SW_LOG_ERROR_ID( "GameInstance.RequiredGameDataCould", "Required game data '%#' (%#) could not be loaded: %# - the game does not start", kindLabel, path.c_str(), pReason );
    }

    void GameInstance::update( float32 deltaTime )
    {
        // 프레임 순서(계약 테스트 `GameInstanceTest.UpdateOrder`): 다시 읽힌 카탈로그 칸(게임 범위 기능 포함) → 맡긴 미니게임 진입 → 씬 흐름(페이드 · 로딩 화면 · 끝난 로드 알림) → 넷 세션 → 파생 프레임 훅.
        for ( const Delegate<void()>& follow : _listFollowedService )
        {
            follow();
        }
        if ( _pGameFeatureScope != nullptr )
            _pGameFeatureScope->rebindReloadedCatalogs();
        getSubsystem<MinigameFlow>().update(); // 트리거가 틱 뒤에 맡긴 진입 — 돌아올 씬의 스냅숏은 틱 밖에서 찍는다
        getSubsystem<GameSceneFlow>().update();
        getSubsystem<GameLocalStore>().update(); // 끝난 세이브 · 자동 저장 요청의 콜백(세이브 이벤트)
        // 받은 이벤트와 메시지를 게임 프레임 앞에서 처리기에 나눠 준다. 세션을 열지 않았으면 바로 돌아온다. 필수 서비스라 없으면 멈춘다.
        game::getSubsystem<GameNetSession>().update();
        getSubsystem<OnlineSession>().update();        // 키트 응답 · 알림을 게임 프레임 앞에서 처리기에 넘긴다
        getSubsystem<PlayerStatsSubsystem>().update(); // 플랫폼에 모아 둔 업적 · 통계 쓰기를 내보낸다(통계가 없는 게임은 바로 돌아온다)
        if ( _pAppearanceWatcher != nullptr )
            _pAppearanceWatcher->update( *_pAppearanceDatabase, _pItemCatalog.get() );
        onUpdate( deltaTime );
        if ( _pDevPart != nullptr )
            _pDevPart->update( deltaTime );
        if ( _pAutosave != nullptr )
            _pAutosave->update( deltaTime );
    }

    bool GameInstance::serializeState( vector<uint8>& outBytes )
    {
        return captureSnapshot( SerializePurpose::Snapshot, outBytes );
    }

    bool GameInstance::deserializeState( const void* pInBuffer, uint32 size )
    {
        return readStateEnvelope( SerializePurpose::Snapshot, pInBuffer, size ).has_value();
    }

    bool GameInstance::captureSnapshot( SerializePurpose purpose, vector<uint8>& outBytes )
    {
        // 세이브는 어느 미니게임 안인지(칸 스택)를 머리에 담는다 — 불러오기가 그 미니게임을 켠 뒤 그 맵을 연다. 같은 실행 안의 스냅숏은 흐름이 살아 있어 담지 않는다.
        vector<uint8> minigameBytes;
        if ( purpose == SerializePurpose::SaveGame )
            getSubsystem<MinigameFlow>().writeSaveState( minigameBytes );
        return captureEnvelope( purpose, minigameBytes, outBytes );
    }

    bool GameInstance::captureSceneForSave( vector<uint8>& outBytes )
    {
        return captureEnvelope( SerializePurpose::SaveGame, vector<uint8>{}, outBytes );
    }

    bool GameInstance::captureEnvelope( SerializePurpose purpose, const vector<uint8>& minigameBytes, vector<uint8>& outBytes )
    {
        if ( purpose == SerializePurpose::SaveGame && _saveLock.isBlocked() )
        {
            SW_LOG_WARNING_ID( "GameInstance.SaveBlocked", "Saving is blocked ('%#') - nothing is written", _saveLock.findBlockReason()->c_str() );
            return false;
        }
        // 순서: 올린 타입의 상태를 싣고 걷기 → 파생 훅(그 밖의 상태를 싣는다) → 봉투(파생의 리플렉션 상태 포함).
        const auto capture = [this, purpose, &minigameBytes, &outBytes]()
        {
            _stateSnapshot.beginCapture();
            onBeforeStateSerialize();
            return _stateSnapshot.writeEnvelope( purpose, getStateTypeInfo(), static_cast<const GameInstance*>( this )->getStateInstance(), minigameBytes, outBytes );
        };
        if ( purpose == SerializePurpose::SaveGame )
        {
            // 세이브 찍기만 잰다 — 되감기 · 미니게임 스냅샷(`Snapshot`)은 프레임마다 돌 수 있어 구간을 흐린다.
            SW_PROFILE_SCOPE( GameInstanceInternal::kSaveCaptureScopeName );
            return capture();
        }
        return capture();
    }

    expected<void, SnapshotRestoreFailure> GameInstance::readStateEnvelope( SerializePurpose purpose, const void* pInBuffer, uint32 size )
    {
        // 순서: 봉투(검증 → 씬 · 리플렉션 상태 · 컴포넌트 상태 교체, 올린 타입에 상태 돌려주기) → 파생 훅(그 밖의 상태를 넘긴다) → 저장소 비우기.
        expected<void, SnapshotRestoreFailure> result = _stateSnapshot.readEnvelope( purpose, getStateTypeInfo(), getStateInstance(), pInBuffer, size );
        if ( result.has_value() == false )
        {
            // 교체 중에 졌다 — 일부만 선 씬을 세이브에 쓰지 않는다. 검증에서 진 것(씬이 그대로)은 막지 않는다 — 간격 저장이 계속 마지막 좋은 상태를 남긴다.
            if ( result.error() == SnapshotRestoreFailure::SceneBroken )
                getSubsystem<GameSaveSlots>().notifySceneBroken( purpose );
            return result;
        }
        getSubsystem<GameSaveSlots>().notifySceneRestored(); // 성공한 복원이 깨진 씬을 덮었다
        onAfterStateDeserialize();
        _stateSnapshot.endRestore();
        return result;
    }

    bool GameInstance::restoreSnapshot( SerializePurpose purpose, const vector<uint8>& inBytes )
    {
        if ( inBytes.empty() )
            return false;
        return readStateEnvelope( purpose, inBytes.data(), static_cast<uint32>( inBytes.size() ) ).has_value();
    }

    // 측정 문턱(Android 4 ms)을 재는 자리 — 넘기 전에는 감시 값만 둔다(`docs/NEXT.md` 측정 게임 프레임워크).
    SW_AUTOMATION_WATCH( saveCaptureMs, "Save.CaptureMs", "Game-thread milliseconds of the save-game capture in the last profiled frame (profiler scope GT.Save.capture; no value while the profiler is off)",
                         &GameInstanceInternal::readCaptureMilliseconds<false> );
    SW_AUTOMATION_WATCH( saveCapturePeakMs, "Save.CapturePeakMs", "Longest save-game capture in game-thread milliseconds over the profiler window (p100 of GT.Save.capture; no value while the profiler is off)",
                         &GameInstanceInternal::readCaptureMilliseconds<true> );
} // namespace sw
