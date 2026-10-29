#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Base/Foundation/Framework/Flow/LoadingScreenController.h"
#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Base/Foundation/GameData/GamePackDataUtil.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

namespace sw
{
    namespace
    {
        struct GameInstanceInternal
        {
            /** @brief 게임 창구로 찾은 씬 매니저의 활성 씬 오브젝트 매니저입니다. 씬 매니저가 없거나 활성 씬이 없으면 nullptr 입니다. */
            static GameObjectManager* findActiveObjectManager()
            {
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                return pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameInstance" );

    GameInstance::GameInstance()
        : _packConfig{}
        , _pWindow{ nullptr }
        , _pRHIDevice{ nullptr }
        , _listPendingSceneLoad{}
        , _stateSnapshot{}
        , _gameScope{}
        , _loadedAudioEvents{}
        , _bResumingWorld{ SW_FALSE }
    {
        // 인스턴스 서브시스템 — initialize 가 onInitialize 앞에 초기화하고 shutdown 이 onShutdown 뒤에 역순으로 종료한다.
        _gameScope.addSubsystem( make_unique<ScreenTransitionManager>() );
        _gameScope.addSubsystem( make_unique<LoadingScreenController>() );
    }

    GameInstance::~GameInstance()
    {
        // shutdown 을 거치지 않고 지워져도 지금 범위가 지운 범위를 가리키지 않게 한다.
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
            // 리로드가 먼저 세운 새 인스턴스가 이 범위를 상위로 이었다 — 지워질 이 범위 대신 이 범위의 상위를 잇게 한다.
            pCurrent->setParentScope( _gameScope.getParentScope() );
        }
        _gameScope.setParentScope( nullptr );
    }

    void GameInstance::reportScopedServiceConflict( uint64 typeHash ) const
    {
        SW_LOG_ERROR( "Game scoped service %# is already bound to another instance - two kits share one service type", typeHash );
    }

    bool GameInstance::checkEngineServices( const IWindow* pWindow )
    {
        // 네트워크 세션은 필수 엔진 서비스다(`EngineServiceList.xxx` 의 `Required` 행). 없으면 세션 없이 조용히 도는 대신 아무것도 걸기 전에 멈춘다.
        if ( game::findSubsystem<GameNetSession>() == nullptr )
        {
            SW_LOG_ERROR( "Game instance cannot start - the required engine service GameNetSession is not bound to the game services" );
            return false;
        }
        // UI · 사용자 설정은 Client 호스트 서비스다. 창이 있는 호스트(클라이언트)에 없으면 호스트의 결함이라 멈춘다 — 타이틀 명령 · 설정 재적용을 조용히 건너뛰지 않는다.
        if ( pWindow != nullptr && ( game::findSubsystem<UISystem>() == nullptr || game::findSubsystem<UserSettingsManager>() == nullptr ) )
        {
            SW_LOG_ERROR( "Game instance cannot start - a windowed (client) host must bind the engine services UISystem and UserSettingsManager to the game services" );
            return false;
        }
        return true;
    }

    bool GameInstance::initialize( IWindow* pWindow, IRHIDevice* pRHIDevice )
    {
        // 필수 엔진 서비스가 없으면 아무것도 걸기 전에 멈춘다. 쓰는 곳(입력 맵 · 세션 명령 · 설정 재적용)은 그 자리에서 게임 창구로 찾는다.
        if ( checkEngineServices( pWindow ) == false )
            return false;
        _pWindow        = pWindow;
        _pRHIDevice     = pRHIDevice;
        _bResumingWorld = GameInstanceInternal::findActiveObjectManager() != nullptr ? SW_TRUE : SW_FALSE;
        configurePackConfig( _packConfig );
        const GameConfig* const pGameConfig = game::findSubsystem<const GameConfig>();
        if ( pGameConfig == nullptr )
        {
            SW_LOG_ERROR( "Game instance cannot start - the required engine service GameConfig is missing from the game services" );
            return false;
        }
        _packConfig._packRoot = pGameConfig->_packRoot;
        // gamesettings.xml 은 필수다. 못 읽으면 기본값으로 서지 않고 게임 초기화를 멈춘다.
        if ( loadGameSettings() == false )
        {
            SW_LOG_ERROR( "Game instance cannot start - gamesettings could not be loaded from pack '%#'", _packConfig._packRoot.c_str() );
            return false;
        }
        // 이 인스턴스의 범위를 지금 게임 범위로 건다 — 파생의 onInitialize 가 거는 카탈로그와 이 설정을 게임 서비스가 찾는다.
        // 앞 범위(테스트가 건 범위 · 리로드에서 아직 안 내려간 옛 인스턴스의 범위)를 상위로 이어, 거기 걸린 것도 찾힌다.
        if ( game::findGameScope() != &_gameScope )
            _gameScope.setParentScope( game::findGameScope() );
        game::setGameScope( &_gameScope );
        bindScopedService( &_packConfig._data );
        const bool bPackConfigApplied = applyPackConfig();
        // 씬 로드를 누가 요청했든 받아들임 알림으로 로딩 화면을 띄운다(언리얼 로딩 화면 모듈이 PreLoadMap 을 구독하는 것과 같다). 첫 씬 요청(onInitialize) 앞에 건다.
        if ( SceneManager* pLoadSceneManager = game::findSubsystem<SceneManager>() )
            _sceneLoadRequestedSubscription = pLoadSceneManager->onSceneLoadRequested().add(
                SW_DELEGATE_METHOD( SceneLoadRequestedHandler, &GameInstance::onSceneLoadRequested, this ) );
        // 인스턴스 서브시스템을 onInitialize 앞에 세운다 — 첫 씬 요청(onInitialize 안)이 로딩 화면을 띄우고, 파생이 네트워크 역할을 보고 키트 처리기를 단다.
        if ( _gameScope.getCollection().initializeAll( SubsystemHostMask::All ) == false )
            SW_LOG_ERROR( "Game instance subsystems did not all initialize: %#", _gameScope.getCollection().getError().c_str() );
        // 네트워크 세션은 엔진 수명이다 — 처음이면 실행 인자로 열고, 리로드로 다시 선 인스턴스는 열린 세션을 이어받는다(클라이언트가 끊기지 않는다).
        // 서버 · 클라이언트로 띄웠는데 열지 못하면(틀린 인자 · 포트 사용 중) 혼자 하는 게임으로 서지 않는다.
        // 티켓 게임 포트가 쓸 암호 제공자(OpenSSL)는 GameFramework 에 있어 Engine 이 모른다 — 세션을 열기 전에 넣는다.
        GameNetSession& netSession = game::getSubsystem<GameNetSession>(); // `bindEngineServices` 가 있음을 확인했다
        netSession.getConnectSecurity().setProvider( &game::getSubsystem<INetSecurityProvider>() );
        const bool bNetSessionOpened = netSession.openFromLaunch();
        if ( bNetSessionOpened == false )
            SW_LOG_ERROR( "Game instance cannot start - the net session asked for by the launch arguments or the server config did not open" );
        // 열린 세션의 창구 · 라우터 · 역할을 씬 매니저에 건다 — 이 뒤에 받아들이는 씬(월드)마다 넷 링크가 올라 컴포넌트가 자기 씬에서 찾는다. 세션이 없으면 걸지 않는다(혼자 하는 게임).
        if ( bNetSessionOpened && netSession.isActive() )
        {
            SceneManager* pLinkSceneManager = game::findSubsystem<SceneManager>();
            if ( pLinkSceneManager != nullptr )
                pLinkSceneManager->setNetLink( netSession.makeNetLink() );
        }
        // 사운드 이벤트는 gamesettings 가 기준이다. 적었는데 못 읽으면 게임을 띄우지 않는다(소리 없이 진행하지 않는다). 입력 맵을 적었는데 입력 서비스가 없어도 같다.
        const bool bInitialized = bPackConfigApplied && bNetSessionOpened && loadAudioEvents() && onInitialize();
        // 창이 없는 호스트에는 UI 가 없다 — UI 전역 명령 표도 쓰지 않는다.
        if ( game::findSubsystem<UISystem>() != nullptr )
            registerSessionCommands( game::getSubsystem<UIGlobalCommandRegistry>() );

        // 플레이어 설정을 다시 넣는다. 언어 팩과 입력 맵(키 바인딩, 누르기/토글)은 위에서 막 생겼고, 엔진 기동 때의 적용은 그 대상이 없을 때 한 것이다.
        // 액션을 더 늦게(컴포넌트 시작 때) 만드는 게임은 그 뒤에 `UserSettingsManager::reapplyAll` 을 부른다. 창이 없는 호스트는 사용자 설정이 없다.
        if ( UserSettingsManager* const pUserSettings = game::findSubsystem<UserSettingsManager>() )
            pUserSettings->reapplyAll();
        return bInitialized;
    }

    void GameInstance::shutdown()
    {
        unregisterSessionCommands(); // 걸지 않았으면(창이 없는 호스트 · 타이틀 씬 없음) 빈 구독이다
        onShutdown();
        if ( _loadedAudioEvents.empty() == false )
        {
            GameSound::unloadEvents( _loadedAudioEvents );
            _loadedAudioEvents.clear();
        }
        // 인스턴스 서브시스템(로딩 화면 · 전환)을 onShutdown 뒤에 내린다. 네트워크 세션은 엔진 수명이라 닫지 않는다(리로드를 넘어 연결을 지킨다).
        _gameScope.getCollection().shutdownAll();
        // 제공자는 GameFramework 의 것이라 이 인스턴스가 내려가면 거둔다(다시 서는 인스턴스가 다시 넣는다). 열린 세션은 이미 받은 포인터로 계속 돈다.
        if ( GameNetSession* pNetSession = game::findSubsystem<GameNetSession>() )
            pNetSession->getConnectSecurity().setProvider( nullptr );
        detachGameScope();
        _sceneLoadRequestedSubscription.reset();
        _listPendingSceneLoad.clear();
        _pWindow    = nullptr;
        _pRHIDevice = nullptr;
    }

    bool GameInstance::loadGameSettings()
    {
        // 컴포넌트 기본값 경로의 주인은 엔진 범위의 `ComponentDefaults` 다(정적 칸을 두지 않는다). 선택 조회: 엔진 서비스 없이 도는 하네스에서는 기본값을 쓰지 않는다.
        ComponentDefaults* const pComponentDefaults = game::findSubsystem<ComponentDefaults>();
        if ( pComponentDefaults != nullptr )
            pComponentDefaults->setPath( _packConfig.resolve( path::kGameSettingsFile ) );
        return _packConfig.load( path::kGameSettingsFile );
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
                SW_LOG_ERROR( "GameSettings localization project '%#' needs the engine service LocalizationManager, which is not bound to the game services",
                              settings._localizationProject.c_str() );
            }
            else if ( pLocalization->initialize( AssetID{ settings._localizationProject.get() }, settings._defaultLanguage, settings._fallbackLanguage ) == false )
            {
                SW_LOG_WARNING( "GameSettings localization project '%#' could not be loaded - strings show their keys", settings._localizationProject.c_str() );
            }
        }

        // 게임플레이 입력 맵은 `InputManager::beginFrame` 이 프레임마다 갱신하는 통합 맵이며, `PlayerController` 가 읽는다.
        if ( settings._inputMap.isSet() == false )
            return true;
        InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
        {
            SW_LOG_ERROR( "GameSettings input map '%#' needs the engine service InputManager, which is not bound to the game services - the game does not start",
                          settings._inputMap.c_str() );
            return false;
        }
        if ( pInput->getInputMap().loadFromAsset( AssetID{ settings._inputMap.get() } ) == false )
            SW_LOG_WARNING( "GameSettings input map '%#' could not be loaded - gameplay actions keep their current bindings", settings._inputMap.c_str() );
        return true;
    }

    bool GameInstance::loadAudioEvents()
    {
        const DataPath& audioEvents = _packConfig._data._audioEvents;
        if ( audioEvents.isSet() == false )
            return true; // 적지 않은 게임은 사운드 이벤트가 없다
        const string& path = audioEvents.get();
        if ( GameSound::loadEvents( AssetID{ path } ) == false )
        {
            SW_LOG_ERROR( "GameSettings audioEvents '%#' could not be loaded (missing or invalid - see the messages above) - the game does not start", path.c_str() );
            return false;
        }
        _loadedAudioEvents = path;
        return true;
    }

    bool GameInstance::makeRequiredDataPath( string_view kindLabel, string& outPath ) const
    {
        string error;
        if ( GamePackDataUtil::tryMakeKindPath( _packConfig._packRoot.get(), kindLabel, outPath, error ) )
            return true;
        SW_LOG_ERROR( "Required game data '%#' has no path in pack '%#': %# - the game does not start", kindLabel, _packConfig._packRoot.c_str(), error.c_str() );
        return false;
    }

    void GameInstance::reportRequiredDataFailure( string_view kindLabel, const AssetID& assetID ) const
    {
        const string& path    = assetID.str();
        const utf8*   pReason = AssetIO::hasAsset( assetID ) ? "the loader rejected it (see the messages above)" : "it is not in the pack or the loader rejected it (see the messages above)";
        SW_LOG_ERROR( "Required game data '%#' (%#) could not be loaded: %# - the game does not start", kindLabel, path.c_str(), pReason );
    }

    /**
     * @brief `"-gv_firstScene=<리소스 경로>"` 인자입니다. 팩 설정(gamesettings)의 시작 씬 대신 이 씬을 처음 엽니다.
     * @details 에디터 없이 다른 씬(환경 쇼케이스 등)을 띄워 볼 때 쓴다.
     * @note PowerShell 은 점이 든 인자를 쪼개므로 따옴표로 감싸야 한다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_firstScene, "", "팩 설정의 시작 씬 대신 처음 열 씬의 리소스 경로 (비우면 사용 안 함)" );

    const string& GameInstance::getFirstScene() const
    {
        if ( gv_firstScene.empty() == false )
            return gv_firstScene;
        // 시작 맵은 필수 칸이다 — 읽기(`GameSettings::loadRoot`)가 빠진 것을 오류로 멈춘다.
        return _packConfig._data._titleScene.isSet() ? _packConfig._data._titleScene.get() : _packConfig._data._startMap.get();
    }

    const string& GameInstance::getEntranceScene() const
    {
        return _packConfig._data._entranceScene.isSet() ? _packConfig._data._entranceScene.get() : _packConfig._data._startMap.get();
    }

    bool GameInstance::requestFirstScene()
    {
        if ( _bResumingWorld == SW_TRUE )
        {
            SW_LOG_INFO( "The game instance was recreated over a live scene - keeping it instead of loading the first scene" );
            return false;
        }
        return requestLoadScene( getFirstScene(), "first" );
    }

    bool GameInstance::requestEntranceScene()
    {
        return requestLoadScene( getEntranceScene(), "entrance" );
    }

    bool GameInstance::requestTitleScene()
    {
        const DataPath& titleScene = _packConfig._data._titleScene;
        if ( titleScene.isSet() == false )
            return false;
        return requestLoadScene( titleScene.get(), "title" );
    }

    void GameInstance::registerSessionCommands( UIGlobalCommandRegistry& commands )
    {
        // 타이틀 씬이 없는 게임은 "타이틀로" 를 걸지 않는다(창이 없는 호스트는 `initialize` 가 이 함수를 부르지 않는다 — UI 가 없다).
        if ( _packConfig._data._titleScene.isSet() == false )
            return;
        _returnToTitleRegistration = commands.registerCommand( hashed_string( UIGlobalCommandRegistry::kReturnToTitle ),
                                                               SW_DELEGATE_METHOD( UICommandDelegate, &GameInstance::onReturnToTitleCommand, this ) );
    }

    void GameInstance::unregisterSessionCommands()
    {
        _returnToTitleRegistration.reset(); // 핫 리로드의 새 인스턴스가 그 사이 교체했으면 그 등록은 지우지 않는다
    }

    void GameInstance::onReturnToTitleCommand( [[maybe_unused]] const hashed_string& command, [[maybe_unused]] Widget& source )
    {
        if ( requestTitleScene() == false )
            SW_LOG_ERROR( "Return to title: the title scene '%#' was not requested", _packConfig._data._titleScene.c_str() );
    }

    bool GameInstance::requestLoadScene( const string& scenePath, const utf8* pWhich )
    {
        if ( scenePath.empty() )
            return false;
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
        {
            SW_LOG_WARNING( "The %# scene '%#' is not opened - no SceneManager service", pWhich, scenePath.c_str() );
            return false;
        }
        // 로딩 화면 · 로드 이벤트는 받아들임 알림(`onSceneLoadRequested`)이 붙인다 — 시나리오 · 게임 코드가 연 씬도 같은 길이다.
        if ( pSceneManager->requestLoadFuture( scenePath, SceneLoadKind::Travel ).isValid() == false )
        {
            SW_LOG_ERROR( "The %# scene '%#' could not be requested", pWhich, scenePath.c_str() );
            return false;
        }
        return true;
    }

    void GameInstance::onSceneLoadRequested( const string& scenePath, SceneLoadKind kind, const TaskFuture<Scene*>& future )
    {
        if ( kind != SceneLoadKind::Travel )
            return; // 도구가 연 씬(편집기 · 진단)은 게임 흐름이 아니다
        _listPendingSceneLoad.push_back( PendingSceneLoad{ scenePath, future } );
        LoadingScreenController& loadingScreen = getSubsystem<LoadingScreenController>();
        if ( loadingScreen.getSettings()._documentPath.isSet() )
            (void)loadingScreen.beginLoading();

        SceneLoadRequestedEvent event{};
        event._levelName = scenePath;
        GameEventUtil::send( event );
    }

    void GameInstance::publishFinishedSceneLoads()
    {
        // 끝난 것만 순서를 지키며 뺀다. 대기열에서 밀린 요청은 nullptr 로 끝나므로 실패로 알린다.
        size_t keptCount = 0;
        for ( size_t index = 0; index < _listPendingSceneLoad.size(); ++index )
        {
            PendingSceneLoad& pending = _listPendingSceneLoad[index];
            if ( pending._future.isReady() == false )
            {
                if ( keptCount != index )
                    _listPendingSceneLoad[keptCount] = std::move( pending );
                ++keptCount;
                continue;
            }
            SceneLoadCompletedEvent event{};
            event._levelName = pending._scenePath;
            event._bSuccess  = ( pending._future.get() != nullptr );
            GameEventUtil::send( event );
        }
        _listPendingSceneLoad.resize( keptCount );
    }

    void GameInstance::update( float32 deltaTime )
    {
        // 맡긴 씬 로드가 남았거나 씬 매니저가 전환 중이면 로딩 중이다. 페이드를 먼저 진행하고, 로딩 화면이 닫히면 페이드 인을 건다.
        const SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        const bool          bLoading      = _listPendingSceneLoad.empty() == false || ( pSceneManager != nullptr && pSceneManager->isTransitioning() );
        // 로드 진행은 씬 매니저의 프리로드가 안다(쿠킹이 구운 목록이 없으면 모른다 — 로딩 화면은 진행 막대를 접는다).
        std::optional<float32> loadProgress;
        float32                progressValue = 0.0f;
        if ( pSceneManager != nullptr && pSceneManager->tryGetLoadProgress( progressValue ) )
            loadProgress = progressValue;
        // 페이드와 로딩 화면은 실제 시간으로 돌린다. 게임 시간으로 돌리면 정지 메뉴(게임 시간 0)가 뜬 동안 페이드가 반쯤 검은 채로 멈춘다.
        const float32            realDeltaTime    = game::getSubsystem<GameTimeScale>().getUnscaledDeltaTime();
        ScreenTransitionManager& screenTransition = getSubsystem<ScreenTransitionManager>();
        screenTransition.update( realDeltaTime );
        getSubsystem<LoadingScreenController>().update( realDeltaTime, bLoading, loadProgress, screenTransition.fade() );
        if ( _listPendingSceneLoad.empty() == false )
            publishFinishedSceneLoads();
        // 받은 이벤트와 메시지를 게임 프레임 앞에서 처리기에 나눠 준다. 세션을 열지 않았으면 바로 돌아온다. 필수 서비스라 없으면 멈춘다.
        game::getSubsystem<GameNetSession>().update();
        onUpdate( deltaTime );
    }

    bool GameInstance::serializeState( vector<uint8>& outBytes )
    {
        return captureSnapshot( SerializePurpose::Snapshot, outBytes );
    }

    bool GameInstance::deserializeState( const void* pInBuffer, uint32 size )
    {
        return readStateEnvelope( SerializePurpose::Snapshot, pInBuffer, size );
    }

    bool GameInstance::captureSnapshot( SerializePurpose purpose, vector<uint8>& outBytes )
    {
        // 순서: 올린 타입의 상태를 싣고 걷기 → 파생 훅(그 밖의 상태를 싣는다) → 봉투(파생의 리플렉션 상태 포함).
        _stateSnapshot.beginCapture();
        onBeforeStateSerialize();
        return _stateSnapshot.writeEnvelope( purpose, getStateTypeInfo(), static_cast<const GameInstance*>( this )->getStateInstance(), outBytes );
    }

    bool GameInstance::readStateEnvelope( SerializePurpose purpose, const void* pInBuffer, uint32 size )
    {
        // 순서: 봉투(씬 · 리플렉션 상태 · 컴포넌트 상태, 올린 타입에 상태 돌려주기) → 파생 훅(그 밖의 상태를 넘긴다) → 저장소 비우기.
        if ( _stateSnapshot.readEnvelope( purpose, getStateTypeInfo(), getStateInstance(), pInBuffer, size ) == false )
            return false;
        onAfterStateDeserialize();
        _stateSnapshot.endRestore();
        return true;
    }

    bool GameInstance::restoreSnapshot( SerializePurpose purpose, const vector<uint8>& inBytes )
    {
        if ( inBytes.empty() )
            return false;
        return readStateEnvelope( purpose, inBytes.data(), static_cast<uint32>( inBytes.size() ) );
    }

    bool GameInstance::saveStateToFile( string_view filePath )
    {
        const DataPath& defaultSavePath = _packConfig._data._defaultSavePath;
        if ( filePath.empty() && defaultSavePath.isSet() == false )
        {
            SW_LOG_ERROR( "No save path was given and GameSettings has no defaultSavePath - nothing is saved" );
            return false;
        }
        const string_view path = filePath.empty() ? string_view( defaultSavePath.get() ) : filePath;
        vector<uint8>     snapshotBytes;
        const bool        bSaved = captureSnapshot( SerializePurpose::SaveGame, snapshotBytes ) && FileUtil::writeFile( path, snapshotBytes.data(), snapshotBytes.size() );

        SaveGameSavedEvent event{};
        event._savePath = string( path );
        event._bSuccess = bSaved;
        GameEventUtil::send( event );
        return bSaved;
    }

    bool GameInstance::loadStateFromFile( string_view filePath )
    {
        const DataPath& defaultSavePath = _packConfig._data._defaultSavePath;
        if ( filePath.empty() && defaultSavePath.isSet() == false )
        {
            SW_LOG_ERROR( "No save path was given and GameSettings has no defaultSavePath - nothing is loaded" );
            return false;
        }
        const string_view path = filePath.empty() ? string_view( defaultSavePath.get() ) : filePath;
        vector<uint8>     snapshotBytes;
        const bool        bLoaded = FileUtil::readFile( path, snapshotBytes ) && restoreSnapshot( SerializePurpose::SaveGame, snapshotBytes );

        SaveGameLoadedEvent event{};
        event._savePath = string( path );
        event._bSuccess = bLoaded;
        GameEventUtil::send( event );
        return bLoaded;
    }
} // namespace sw
