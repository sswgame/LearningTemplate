#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Map/InputMapBundle.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstanceClientHook.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IContentConfiguration.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IGameDevPart.h"
#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/GameLocalStore.h"
#include "GameFramework/Runtime/Foundation/GameData/GamePackDataUtil.h"
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
            /** @brief 게임 상태 세이브 슬롯의 형식 버전입니다(봉투에 적힌다). 스냅샷 봉투(`GameStateSnapshot`)의 바이트 형식을 바꾸면 올린다. */
            static constexpr uint32 kSaveSlotFormatVersion = 1;

            /** @brief `game.save [슬롯]` · `game.load [슬롯]` 의 몸통입니다. 슬롯을 비우면 기본 슬롯입니다. 맡기기만 하고 끝은 감시 값 `Save.Written` · `Save.Loaded` 로 본다. */
            template <bool bSave>
            static bool runSlotCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                GameInstance* const pInstance = GameInstance::findRunningInstance();
                const string_view   slot      = listArgument.empty() ? string_view{} : string_view( listArgument[0] );
                const utf8* const   pVerb     = bSave ? "game.save" : "game.load";
                if ( pInstance == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "%#: no game is running", pVerb );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                const bool                               bQueued = bSave ? pInstance->saveGameToSlot( slot ) : pInstance->loadGameFromSlot( slot );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "%#: %# '%#'", pVerb, bQueued ? "queued" : "refused", slot.empty() ? string_view( "<default>" ) : slot );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief 활성 씬의 리소스 id(`Scene::getSourcePath`)입니다. 씬 매니저나 활성 씬이 없으면 빈 글입니다 — 세이브 머리의 씬 정체와 같은 뜻이다. */
            static string findActiveSceneID()
            {
                const SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                const Scene* const        pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
                return pScene != nullptr ? pScene->getSourcePath() : string{};
            }

            template <bool bWritten>
            [[nodiscard]] static bool readSlotCount( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const GameInstance* const pInstance = GameInstance::findRunningInstance();
                if ( pInstance == nullptr )
                    return false;
                const SaveSlotCounts& counts = pInstance->getSaveSlotCounts();
                outValue                     = static_cast<float64>( bWritten ? counts._written : counts._loaded );
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
        , _saveGate{}
        , _gameScope{}
        , _pItemCatalog{}
        , _pAppearanceDatabase{}
        , _pAppearanceWatcher{}
        , _pAutosave{}
        , _pDevPart{}
        , _loadedAudioEvents{}
        , _listSlotRequest{}
        , _saveSlotCounts{}
        , _pendingSlotRestore{}
        , _brokenRestoreBlock{}
        , _bStarted{ SW_FALSE }
        , _bDerivedEntered{ SW_FALSE }
        , _bSecuritySet{ SW_FALSE }
    {
        // 인스턴스 서브시스템 — initialize 가 onInitialize 앞에 초기화하고 shutdown 이 onShutdown 뒤에 역순으로 종료한다.
        ScreenTransitionManager& screenTransition = _gameScope.emplaceSubsystem<ScreenTransitionManager>();
        // 로딩 화면은 클라이언트 전용 부분이 세운다 — 그 소스를 짓지 않는 전용 서버에는 없다(`IGameInstanceClientHook`).
        IGameInstanceClientHook* const pClientHook    = IGameInstanceClientHook::find();
        ILoadingScreen* const          pLoadingScreen = pClientHook != nullptr ? pClientHook->emplaceLoadingScreen( _gameScope ) : nullptr;
        // 씬 흐름은 이 인스턴스의 gamesettings(`_packConfig` — 범위보다 먼저 선 멤버)와 두 형제를 빌려 든다.
        _gameScope.emplaceSubsystem<GameSceneFlow>( _packConfig._data, screenTransition, pLoadingScreen, _saveGate );
        // 로컬 저장소(세이브 슬롯)는 게임 범위 하나다 - 게임 상태 세이브 · 자동 저장 · 계정 장치 비밀이 함께 쓴다.
        (void)_gameScope.addSubsystem( std::move( localStore ) );
        // 온라인 세션은 키트 클라이언트를 묶을 뿐 연결은 게임이 연다 - 온라인을 쓰지 않는 게임에는 비용이 없다.
        _gameScope.emplaceSubsystem<OnlineSession>( _packConfig._data );
        // 텔레메트리 · 크래시 업로드는 gamesettings 에 끝점이 있을 때만 엔진 업로더에 HTTP 클라이언트를 끼운다 - 없으면 아무 일도 하지 않는다.
        _gameScope.emplaceSubsystem<TelemetryUploadLink>( _packConfig._data );
        // 플레이어 통계 · 업적은 gamesettings 에 통계 카탈로그가 있을 때만 선다 - 플랫폼 서비스(지금은 로컬 구현)까지 잇는다.
        _gameScope.emplaceSubsystem<PlayerStatsSubsystem>( _packConfig._data );
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

    bool GameInstance::checkEngineServices( const IWindow* pWindow )
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

    bool GameInstance::initialize( IWindow* pWindow )
    {
        // 프로세스당 하나다 — 다른 인스턴스가 돌면 아무것도 걸지 않고 멈춘다(앞 인스턴스의 지금 범위 · 서비스를 덮지 않는다).
        const GameInstance* const pRunning = findRunningInstance();
        if ( pRunning != nullptr && pRunning != this )
        {
            SW_LOG_ERROR_ID( "GameInstance.SecondGameInstance", "Game instance cannot start - another game instance is running in this process (one per process; the host shuts the old one down first)" );
            return false;
        }
        // 필수 엔진 서비스가 없으면 아무것도 걸기 전에 멈춘다. 쓰는 곳(입력 맵 · 세션 명령 · 설정 재적용)은 그 자리에서 게임 창구로 찾는다.
        if ( checkEngineServices( pWindow ) == false )
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
        if ( bindScopedService( this ) == false || bindScopedService( &_packConfig._data ) == false || bindScopedService( &_saveGate ) == false || applyPackConfig() == false )
            return false;
        // 인스턴스 서브시스템을 onInitialize 앞에 세운다(의존 순서: 화면 전환 · 로딩 화면 → 씬 흐름) — 씬 흐름이 씬 로드 받아들임 알림을 걸어 첫 씬 요청(onInitialize 안)이
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
        if ( onPrepareContent() == false )
        {
            SW_LOG_ERROR_ID( "GameInstance.ContentNotPrepared", "Game instance cannot start - the content configuration (mods) could not be prepared (see the error above)" );
            return false;
        }
        const IContentConfiguration* const pContent          = game::findSubsystem<IContentConfiguration>();
        const uint64                       contentConfigHash = pContent != nullptr ? pContent->getContentConfigHash() : 0;
        GameNetSession&                    netSession        = game::getSubsystem<GameNetSession>(); // `checkEngineServices` 가 있음을 확인했다
        netSession.getConnectSecurity().setProvider( &game::getSubsystem<INetSecurityProvider>() );
        _bSecuritySet = SW_TRUE;
        if ( netSession.openFromLaunch( contentConfigHash ) == false )
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
        // 개발 전용 부분은 게임이 다 선 뒤에 붙는다(게임 `Dev/` 의 공장 — 배포 구성에는 없다).
        if ( IGameDevPartFactory* const pDevFactory = game::findSubsystem<IGameDevPartFactory>() )
            _pDevPart = pDevFactory->createDevPart();
        return true;
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
        // 제공자는 GameFramework 의 것이라 이 인스턴스가 내려가면 거둔다(다시 서는 인스턴스가 다시 넣는다). 열린 세션은 이미 받은 포인터로 계속 돈다.
        GameNetSession* const pNetSession = game::findSubsystem<GameNetSession>();
        if ( _bSecuritySet == SW_TRUE && pNetSession != nullptr )
            pNetSession->getConnectSecurity().setProvider( nullptr );
        _bSecuritySet = SW_FALSE;
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
        if ( _pAutosave->initialize( settings, getSubsystem<GameLocalStore>(), _saveGate,
                                     AutosaveManager::SaveDelegate::create( [this]( vector<uint8>& outBytes )
        { return captureSnapshot( SerializePurpose::SaveGame, outBytes ); } ),
                                     AutosaveManager::LoadDelegate::create( [this]( const vector<uint8>& bytes )
        { return restoreSnapshot( SerializePurpose::SaveGame, bytes ); } ) ) == false )
            return false; // 까닭은 관리자가 남겼다
        return bindScopedService( _pAutosave.get() );
    }

    bool GameInstance::loadDeclaredCharacterData()
    {
        const map<string, string>& mapRequired = _packConfig._data._mapRequiredDataPack;
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
        // 공용 팩에서 읽는 종류는 gamesettings 의 <RequiredData pack> 이 팩을 정한다 — 자리 규칙은 어느 팩이든 등록 줄 하나다.
        const auto           itShared = _packConfig._data._mapRequiredDataPack.find( string( kindLabel ) );
        const string&        pack     = itShared != _packConfig._data._mapRequiredDataPack.end() ? itShared->second : _packConfig._packRoot.get();
        const expected<void> path     = GamePackDataUtil::tryMakeKindPath( pack, kindLabel, outPath );
        if ( path.has_value() )
            return true;
        SW_LOG_ERROR_ID( "GameInstance.RequiredGameDataNo", "Required game data '%#' has no path in pack '%#': %# (%#) - the game does not start", kindLabel, pack.c_str(), path.error().message(),
                         path.error().detail() );
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
        // 프레임 순서: 씬 흐름(페이드 · 로딩 화면 · 끝난 로드 알림) → 넷 세션 → 파생 프레임 훅.
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
        if ( purpose == SerializePurpose::SaveGame && _saveGate.isBlocked() )
        {
            SW_LOG_WARNING_ID( "GameInstance.SaveBlocked", "Saving is blocked ('%#') - nothing is written", _saveGate.findBlockReason()->c_str() );
            return false;
        }
        // 순서: 올린 타입의 상태를 싣고 걷기 → 파생 훅(그 밖의 상태를 싣는다) → 봉투(파생의 리플렉션 상태 포함).
        _stateSnapshot.beginCapture();
        onBeforeStateSerialize();
        return _stateSnapshot.writeEnvelope( purpose, getStateTypeInfo(), static_cast<const GameInstance*>( this )->getStateInstance(), outBytes );
    }

    expected<void, SnapshotRestoreFailure> GameInstance::readStateEnvelope( SerializePurpose purpose, const void* pInBuffer, uint32 size )
    {
        // 순서: 봉투(검증 → 씬 · 리플렉션 상태 · 컴포넌트 상태 교체, 올린 타입에 상태 돌려주기) → 파생 훅(그 밖의 상태를 넘긴다) → 저장소 비우기.
        expected<void, SnapshotRestoreFailure> result = _stateSnapshot.readEnvelope( purpose, getStateTypeInfo(), getStateInstance(), pInBuffer, size );
        if ( result.has_value() == false )
        {
            // 교체 중에 졌다 — 일부만 선 씬을 세이브에 쓰지 않는다. 검증에서 진 것(씬이 그대로)은 막지 않는다 — 간격 저장이 계속 마지막 좋은 상태를 남긴다.
            if ( result.error() == SnapshotRestoreFailure::SceneBroken )
            {
                SW_LOG_ERROR_ID( "GameInstance.RestoreBrokeScene", "The restore failed after the scene was cleared - saving is blocked until a restore succeeds or the scene is reopened" );
                _brokenRestoreBlock = _saveGate.block( "restore.broken" );
                // 세이브 불러오기(슬롯 · 자동 저장)는 씬을 디스크에서 다시 연다 — 핫 리로드 스냅샷은 저장하지 않은 편집이 든 씬이라 다시 열지 않고 막기만 둔다.
                if ( purpose == SerializePurpose::SaveGame )
                    reopenBrokenScene();
            }
            return result;
        }
        _brokenRestoreBlock.reset(); // 성공한 복원이 깨진 씬을 덮었다
        onAfterStateDeserialize();
        _stateSnapshot.endRestore();
        return result;
    }

    void GameInstance::reopenBrokenScene()
    {
        const string scenePath = GameInstanceInternal::findActiveSceneID();
        if ( scenePath.empty() )
        {
            SW_LOG_ERROR_ID( "GameInstance.BrokenSceneNotReopened", "The broken scene has no source to reopen - saving stays blocked until a restore succeeds" );
            return;
        }
        if ( getSubsystem<GameSceneFlow>().requestSceneThen( scenePath, GameSceneFlow::SceneLoadedFunction::create<&GameInstance::onBrokenSceneReopened>( this ) ) == false )
            SW_LOG_ERROR_ID( "GameInstance.BrokenSceneNotRequested", "The broken scene '%#' could not be requested again - saving stays blocked until a restore succeeds", scenePath.c_str() );
    }

    void GameInstance::onBrokenSceneReopened( Scene* pScene )
    {
        if ( pScene == nullptr )
        {
            SW_LOG_ERROR_ID( "GameInstance.BrokenSceneReopenFailed", "Reopening the broken scene failed - saving stays blocked until a restore succeeds" );
            return;
        }
        _brokenRestoreBlock.reset(); // 디스크에서 다시 선 씬이다 — 일부만 선 씬이 아니다
        SW_LOG_INFO_ID( "GameInstance.BrokenSceneReopened", "The broken scene '%#' was reopened - saving is allowed again", pScene->getSourcePath().c_str() );
    }

    bool GameInstance::restoreSnapshot( SerializePurpose purpose, const vector<uint8>& inBytes )
    {
        if ( inBytes.empty() )
            return false;
        return readStateEnvelope( purpose, inBytes.data(), static_cast<uint32>( inBytes.size() ) ).has_value();
    }

    bool GameInstance::resolveSaveSlot( string_view slot, string& outSlot ) const
    {
        const string& defaultSaveSlot = _packConfig._data._defaultSaveSlot;
        if ( slot.empty() && defaultSaveSlot.empty() )
        {
            SW_LOG_ERROR_ID( "GameInstance.NoSaveSlotGiven", "No save slot was given and GameSettings has no defaultSaveSlot - nothing is saved or loaded" );
            return false;
        }
        // 세이브는 로컬 저장 슬롯이다 - 자리(사용자 데이터 폴더)는 `GameLocalStore` 가 정하고 여기는 이름만 고른다(언리얼 `SaveGameToSlot` 의 슬롯 이름).
        const string_view chosenSlot = slot.empty() ? string_view( defaultSaveSlot ) : slot;
        if ( ILocalStore::isValidSlotName( chosenSlot ) == false )
        {
            SW_LOG_ERROR_ID( "GameInstance.SaveSlotBreaksSlot", "Save slot '%#' breaks the slot name rule ([0-9a-z_.-], at most one '/' group, 64 characters)", chosenSlot );
            return false;
        }
        outSlot = string( chosenSlot );
        return true;
    }

    void GameInstance::holdSlotRequest( Subscription request )
    {
        (void)VectorUtil::removeIf( _listSlotRequest, []( const Subscription& held )
        { return held.isActive() == false; } );
        _listSlotRequest.push_back( std::move( request ) );
    }

    bool GameInstance::saveGameToSlot( string_view slot )
    {
        string slotName;
        if ( resolveSaveSlot( slot, slotName ) == false )
            return false;
        vector<uint8> snapshotBytes;
        if ( captureSnapshot( SerializePurpose::SaveGame, snapshotBytes ) == false )
        {
            SaveGameSavedEvent event{};
            event._slot     = slotName;
            event._bSuccess = false;
            GameEventUtil::send( event );
            return false;
        }
        LocalStoreWriteOptions options{};
        options._formatVersion = GameInstanceInternal::kSaveSlotFormatVersion;
        holdSlotRequest( getSubsystem<GameLocalStore>().submitWrite( slotName, std::move( snapshotBytes ), options,
                                                                     GameLocalStore::CompletionFunction::create<&GameInstance::onSaveSlotWritten>( this ) ) );
        return true;
    }

    void GameInstance::onSaveSlotWritten( const LocalStoreCompletion& completion )
    {
        if ( completion._result != LocalStoreResult::Ok )
            SW_LOG_ERROR_ID( "GameInstance.SaveSlotCouldNot", "Save slot '%#' could not be written (local store result %#)", completion._slot.c_str(), static_cast<int32>( completion._result ) );
        else
            ++_saveSlotCounts._written;
        SaveGameSavedEvent event{};
        event._slot     = completion._slot;
        event._bSuccess = completion._result == LocalStoreResult::Ok;
        GameEventUtil::send( event );
    }

    bool GameInstance::loadGameFromSlot( string_view slot )
    {
        if ( _pendingSlotRestore.has_value() )
        {
            SW_LOG_WARNING_ID( "GameInstance.LoadWhileTravelling", "A save is already waiting for its scene - this load is refused (slot '%#')", _pendingSlotRestore->_slot.c_str() );
            return false;
        }
        string slotName;
        if ( resolveSaveSlot( slot, slotName ) == false )
            return false;
        holdSlotRequest( getSubsystem<GameLocalStore>().submitRead( slotName, GameLocalStore::CompletionFunction::create<&GameInstance::onSaveSlotRead>( this ) ) );
        return true;
    }

    void GameInstance::onSaveSlotRead( const LocalStoreCompletion& completion )
    {
        if ( completion._result != LocalStoreResult::Ok )
        {
            SW_LOG_ERROR_ID( "GameInstance.SaveSlotCouldNot2", "Save slot '%#' could not be read (local store result %#)", completion._slot.c_str(), static_cast<int32>( completion._result ) );
            publishSlotLoaded( completion._slot, false );
            return;
        }
        if ( completion._formatVersion != GameInstanceInternal::kSaveSlotFormatVersion )
        {
            // 옛 형식 리더는 두지 않는다 - 다른 판의 세이브는 거절한다.
            SW_LOG_ERROR_ID( "GameInstance.SaveSlotFormatVersion", "Save slot '%#' has format version %#, this build reads %#", completion._slot.c_str(), completion._formatVersion,
                             GameInstanceInternal::kSaveSlotFormatVersion );
            publishSlotLoaded( completion._slot, false );
            return;
        }
        if ( _pendingSlotRestore.has_value() )
        {
            // 읽기 둘이 겹쳤다 — 앞의 것이 씬을 옮기는 중이라 이 세이브를 곧 내려갈 씬에 풀지 않는다.
            SW_LOG_WARNING_ID( "GameInstance.SlotReadWhileTravelling", "Save slot '%#' was read while another save waits for its scene - it is not applied", completion._slot.c_str() );
            publishSlotLoaded( completion._slot, false );
            return;
        }
        // 머리만 읽어 찍은 씬을 본다. 같은 씬(또는 씬 없이 찍은 세이브)이면 바로 적용하고, 아니면 그 씬을 연 뒤 적용한다 —
        // 언리얼 게임이 세이브의 맵 이름으로 `OpenLevel` 한 뒤 적용하는 것과 같다(유니티는 `LoadSceneAsync` 의 완료에서 적용).
        const optional<string> savedSceneID = GameStateSnapshot::readSceneID( completion._bytes.data(), static_cast<uint32>( completion._bytes.size() ) );
        if ( savedSceneID.has_value() == false )
        {
            publishSlotLoaded( completion._slot, false ); // 머리가 틀린 까닭은 읽기가 남겼다
            return;
        }
        if ( savedSceneID->empty() || *savedSceneID == GameInstanceInternal::findActiveSceneID() )
        {
            publishSlotLoaded( completion._slot, restoreSnapshot( SerializePurpose::SaveGame, completion._bytes ) );
            return;
        }
        _pendingSlotRestore = PendingSlotRestore{ completion._slot, completion._bytes };
        if ( getSubsystem<GameSceneFlow>().requestSceneThen( *savedSceneID, GameSceneFlow::SceneLoadedFunction::create<&GameInstance::onSaveSceneLoaded>( this ) ) == false )
        {
            SW_LOG_ERROR_ID( "GameInstance.SaveSceneNotRequested", "Save slot '%#' was made in scene '%#', which could not be requested - nothing is loaded", completion._slot.c_str(),
                             savedSceneID->c_str() );
            _pendingSlotRestore = nullopt;
            publishSlotLoaded( completion._slot, false );
        }
    }

    void GameInstance::onSaveSceneLoaded( Scene* pScene )
    {
        if ( _pendingSlotRestore.has_value() == false )
            return; // 종료가 거뒀다
        const PendingSlotRestore pending = std::move( *_pendingSlotRestore );
        _pendingSlotRestore              = nullopt;
        if ( pScene == nullptr )
        {
            SW_LOG_ERROR_ID( "GameInstance.SaveSceneNotOpened", "The scene of save slot '%#' did not open (failed or replaced by a later request) - nothing is loaded", pending._slot.c_str() );
            publishSlotLoaded( pending._slot, false );
            return;
        }
        publishSlotLoaded( pending._slot, restoreSnapshot( SerializePurpose::SaveGame, pending._bytes ) );
    }

    void GameInstance::publishSlotLoaded( const string& slot, bool bLoaded )
    {
        if ( bLoaded )
            ++_saveSlotCounts._loaded;
        SaveGameLoadedEvent event{};
        event._slot     = slot;
        event._bSuccess = bLoaded;
        GameEventUtil::send( event );
    }

    // 세이브 메뉴 자리의 개발 명령 — 시나리오가 플레이어의 "저장 · 불러오기" 를 같은 창구(`saveGameToSlot` · `loadGameFromSlot`)로 부른다. 메뉴 화면은 아직 없다.
    SW_DEV_COMMAND( GameSave, "game.save", "game.save [slot]", "Save the scene and game state to a save slot (empty: the default slot of gamesettings)",
                    &GameInstanceInternal::runSlotCommand<true> );
    SW_DEV_COMMAND( GameLoad, "game.load", "game.load [slot]", "Load the scene and game state from a save slot (empty: the default slot of gamesettings)",
                    &GameInstanceInternal::runSlotCommand<false> );

    SW_AUTOMATION_WATCH( saveWritten, "Save.Written", "Game state save slots written successfully by the running game", &GameInstanceInternal::readSlotCount<true> );
    SW_AUTOMATION_WATCH( saveLoaded, "Save.Loaded", "Game state save slots read and restored successfully by the running game", &GameInstanceInternal::readSlotCount<false> );
} // namespace sw
