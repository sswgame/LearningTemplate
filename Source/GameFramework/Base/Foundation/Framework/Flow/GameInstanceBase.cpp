#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstanceBase.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Base/Foundation/Data/GamePackDataUtil.h"
#include "GameFramework/Base/Foundation/Framework/Flow/LoadingScreenController.h"
#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Base/Foundation/Framework/Save/ComponentStateStore.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

namespace sw
{
    namespace
    {
        struct StateEnvelopeInternal
        {
            static constexpr uint32 kMagic = FourCcUtil::make( "SWST" );
            /**
             * @brief 스냅샷 봉투의 버전입니다. 읽을 때도 이 버전만 받습니다.
             * @details 봉투 머리에 프로세스 토큰이 있고, 씬 섹션의 오브젝트마다 런타임 id 가 상태 앞에 실린다.
             *          섹션은 씬 오브젝트, 파생의 리플렉션 상태, 컴포넌트 상태(`ComponentStateStore`) 셋이다.
             *          토큰이 지금 프로세스와 같으면(핫 리로드) id 를 복원하고, 다르면(다른 실행의 세이브 파일) id 를 읽고 버린다.
             *          다른 실행에서 나간 id 를 복원하면 이 실행에서 이미 나간 id 와 겹칠 수 있기 때문이다.
             */
            static constexpr uint32 kVersion = 4; // 4: 오브젝트 id 묶음에 컴포넌트 안정 키가 실린다
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameInstanceBase" );

    GameInstanceBase::GameInstanceBase()
        : _bootstrap{}
        , _pWindow{ nullptr }
        , _pRHIDevice{ nullptr }
        , _listPendingSceneLoad{}
        , _listStatefulType{}
        , _pComponentStateStore{ make_unique<ComponentStateStore>() }
        , _gameScope{}
        , _loadedAudioEvents{}
        , _pUISystem{ nullptr }
        , _pUserSettings{ nullptr }
        , _pInput{ nullptr }
        , _bResumingWorld{ SW_FALSE }
    {
        // 인스턴스 서브시스템 — initialize 가 onInitialize 앞에 초기화하고 shutdown 이 onShutdown 뒤에 역순으로 종료한다.
        _gameScope.addSubsystem( make_unique<ScreenTransitionManager>() );
        _gameScope.addSubsystem( make_unique<LoadingScreenController>() );
        // 엔진 컴포넌트 중 PROPERTY 밖의 편집 상태를 가진 것 — 지형의 저장하지 않은 높이 · 스플랫이 모듈 리로드와 백엔드 교체를 넘어간다.
        registerStatefulComponent<TerrainComponent>();
        // 프레임워크 컴포넌트(카메라 디렉터 등)는 정의한 쪽이 정적으로 등록한다 — 게임마다 올리지 않아도 모든 게임의 스냅샷에 실린다.
        for ( const StatefulComponentRegistration* pRegistration : StatefulComponentRegistration::getRegistrations() )
        {
            StatefulComponentType type;
            type._pCapture = pRegistration->getCapture();
            type._pRestore = pRegistration->getRestore();
            _listStatefulType.push_back( type );
        }
    }

    GameInstanceBase::~GameInstanceBase()
    {
        // shutdown 을 거치지 않고 지워져도 지금 범위가 지운 범위를 가리키지 않게 한다.
        detachGameScope();
    }

    void GameInstanceBase::detachGameScope()
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

    void GameInstanceBase::reportScopedServiceConflict( uint64 typeHash ) const
    {
        SW_LOG_ERROR( "Game scoped service %# is already bound to another instance - two kits share one service type", typeHash );
    }

    GameObjectManager* GameInstanceBase::findActiveObjectManager()
    {
        // `findSubsystem<SceneManager>()` 가 바로 이 서비스를 보므로 아래 널 검사에는 사실상 닿지 않는다.
        // 그래도 `game::findSubsystem<T>()` 는 nullptr 을 반환할 수 있어서 `CheckNullableServiceUse` 린트가 요구하는 모양을 지킨다.
        if ( game::findSubsystem<SceneManager>() == nullptr )
            return nullptr;
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
            return nullptr;
        return pSceneManager->findActiveObjectManager();
    }

    bool GameInstanceBase::bindEngineServices( const IWindow* pWindow )
    {
        _pUISystem     = nullptr;
        _pUserSettings = nullptr;
        _pInput        = nullptr;
        // 네트워크 세션은 필수 엔진 서비스다(`EngineServiceList.xxx` 의 `Required` 행). 없으면 세션 없이 조용히 도는 대신 아무것도 걸기 전에 멈춘다.
        if ( game::findSubsystem<GameNetSession>() == nullptr )
        {
            SW_LOG_ERROR( "Game instance cannot start - the required engine service GameNetSession is not bound to the game services" );
            return false;
        }
        // UI · 사용자 설정은 Client 호스트 서비스다. 창이 있는 호스트(클라이언트)에 없으면 호스트의 결함이라 멈춘다 — 타이틀 명령 · 설정 재적용을 조용히 건너뛰지 않는다.
        UISystem*            pUISystem     = game::findSubsystem<UISystem>();
        UserSettingsManager* pUserSettings = game::findSubsystem<UserSettingsManager>();
        if ( pWindow != nullptr && ( pUISystem == nullptr || pUserSettings == nullptr ) )
        {
            SW_LOG_ERROR( "Game instance cannot start - a windowed (client) host must bind the engine services UISystem and UserSettingsManager to the game services" );
            return false;
        }
        _pUISystem     = pUISystem;
        _pUserSettings = pUserSettings;
        _pInput        = game::findSubsystem<InputManager>();
        return true;
    }

    bool GameInstanceBase::initialize( IWindow* pWindow, IRHIDevice* pRHIDevice )
    {
        // 엔진 서비스는 여기서 한 번 받아 든다 — 아래에서 쓰는 곳(입력 맵 · 세션 명령 · 설정 재적용)은 다시 찾지 않는다. 필수 서비스가 없으면 아무것도 걸기 전에 멈춘다.
        if ( bindEngineServices( pWindow ) == false )
            return false;
        _pWindow        = pWindow;
        _pRHIDevice     = pRHIDevice;
        _bResumingWorld = findActiveObjectManager() != nullptr ? SW_TRUE : SW_FALSE;
        configureBootstrap( _bootstrap );
        const GameConfig* const pGameConfig = game::findSubsystem<const GameConfig>();
        if ( pGameConfig == nullptr )
        {
            SW_LOG_ERROR( "Game instance cannot start - the required engine service GameConfig is missing from the game services" );
            return false;
        }
        _bootstrap._packRoot = pGameConfig->_packRoot;
        // gamesettings.xml 은 필수다. 못 읽으면 기본값으로 서지 않고 게임 초기화를 멈춘다.
        if ( loadGameSettings() == false )
        {
            SW_LOG_ERROR( "Game instance cannot start - gamesettings could not be loaded from pack '%#'", _bootstrap._packRoot.c_str() );
            return false;
        }
        // 이 인스턴스의 범위를 지금 게임 범위로 건다 — 파생의 onInitialize 가 거는 카탈로그와 이 설정을 게임 서비스가 찾는다.
        // 앞 범위(테스트가 건 범위 · 리로드에서 아직 안 내려간 옛 인스턴스의 범위)를 상위로 이어, 거기 걸린 것도 찾힌다.
        if ( game::findGameScope() != &_gameScope )
            _gameScope.setParentScope( game::findGameScope() );
        game::setGameScope( &_gameScope );
        bindScopedService( &_bootstrap._data );
        const bool bBootstrapApplied = applyBootstrap();
        // 인스턴스 서브시스템을 onInitialize 앞에 세운다 — 첫 씬 요청(onInitialize 안)이 로딩 화면을 띄우고, 파생이 네트워크 역할을 보고 키트 처리기를 단다.
        if ( _gameScope.getCollection().initializeAll( SubsystemHostMask::All ) == false )
            SW_LOG_ERROR( "Game instance subsystems did not all initialize: %#", _gameScope.getCollection().getError().c_str() );
        // 네트워크 세션은 엔진 수명이다 — 처음이면 실행 인자로 열고, 리로드로 다시 선 인스턴스는 열린 세션을 이어받는다(클라이언트가 끊기지 않는다).
        // 서버 · 클라이언트로 띄웠는데 열지 못하면(틀린 인자 · 포트 사용 중) 혼자 하는 게임으로 서지 않는다.
        // 티켓 게임 포트가 쓸 암호 제공자(OpenSSL)는 GameFramework 에 있어 Engine 이 모른다 — 세션을 열기 전에 넣는다.
        GameNetSession& netSession = game::getSubsystem<GameNetSession>(); // `bindEngineServices` 가 있음을 확인했다
        netSession.getConnectSecurity().setProvider( &NetSecurity::getProvider() );
        const bool bNetSessionOpened = netSession.openFromLaunch();
        if ( bNetSessionOpened == false )
            SW_LOG_ERROR( "Game instance cannot start - the net session asked for by the launch arguments or the server config did not open" );
        // 사운드 이벤트는 gamesettings 가 기준이다. 적었는데 못 읽으면 게임을 띄우지 않는다(소리 없이 진행하지 않는다). 입력 맵을 적었는데 입력 서비스가 없어도 같다.
        const bool bInitialized = bBootstrapApplied && bNetSessionOpened && loadAudioEvents() && onInitialize();
        if ( _pUISystem != nullptr )
            registerSessionCommands( *_pUISystem );

        // 플레이어 설정을 다시 넣는다. 언어 팩과 입력 맵(키 바인딩, 누르기/토글)은 위에서 막 생겼고, 엔진 기동 때의 적용은 그 대상이 없을 때 한 것이다.
        // 액션을 더 늦게(컴포넌트 시작 때) 만드는 게임은 그 뒤에 `UserSettingsManager::reapplyAll` 을 부른다. 창이 없는 호스트는 사용자 설정이 없다.
        if ( _pUserSettings != nullptr )
            _pUserSettings->reapplyAll();
        return bInitialized;
    }

    void GameInstanceBase::shutdown()
    {
        if ( _pUISystem != nullptr )
            unregisterSessionCommands( *_pUISystem );
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
        _listPendingSceneLoad.clear();
        _pWindow       = nullptr;
        _pRHIDevice    = nullptr;
        _pUISystem     = nullptr;
        _pUserSettings = nullptr;
        _pInput        = nullptr;
    }

    bool GameInstanceBase::loadGameSettings()
    {
        // 컴포넌트 기본값 경로의 주인은 엔진 범위의 `ComponentDefaults` 다(정적 칸을 두지 않는다). 선택 조회: 엔진 서비스 없이 도는 하네스에서는 기본값을 쓰지 않는다.
        ComponentDefaults* const pComponentDefaults = game::findSubsystem<ComponentDefaults>();
        if ( pComponentDefaults != nullptr )
            pComponentDefaults->setPath( _bootstrap.resolve( path::kGameSettingsFile ) );
        return _bootstrap.load( path::kGameSettingsFile );
    }

    bool GameInstanceBase::applyBootstrap()
    {
        const GameSettings& settings = _bootstrap._data;

        // 게임의 로컬라이제이션 프로젝트를 올린다. 엔진 프로젝트는 엔진이 기동 때 올렸다.
        if ( settings._localizationProject.isSet() )
        {
            LocalizationManager* pLocalization = game::findSubsystem<LocalizationManager>();
            if ( pLocalization == nullptr )
            {
                SW_LOG_ERROR( "GameSettings localization project '%#' needs the engine service LocalizationManager, which is not bound to the game services",
                              settings._localizationProject.c_str() );
            }
            else if ( pLocalization->initialize( settings._localizationProject.get(), settings._defaultLanguage, settings._fallbackLanguage ) == false )
            {
                SW_LOG_WARNING( "GameSettings localization project '%#' could not be loaded - strings show their keys", settings._localizationProject.c_str() );
            }
        }

        // 게임플레이 입력 맵은 `InputManager::beginFrame` 이 프레임마다 갱신하는 통합 맵이며, `PlayerController` 가 읽는다.
        if ( settings._inputMap.isSet() == false )
            return true;
        if ( _pInput == nullptr )
        {
            SW_LOG_ERROR( "GameSettings input map '%#' needs the engine service InputManager, which is not bound to the game services - the game does not start",
                          settings._inputMap.c_str() );
            return false;
        }
        if ( _pInput->getInputMap().loadFromResource( ResourceID{ settings._inputMap.get() } ) == false )
            SW_LOG_WARNING( "GameSettings input map '%#' could not be loaded - gameplay actions keep their current bindings", settings._inputMap.c_str() );
        return true;
    }

    bool GameInstanceBase::loadAudioEvents()
    {
        const DataPath& audioEvents = _bootstrap._data._audioEvents;
        if ( audioEvents.isSet() == false )
            return true; // 적지 않은 게임은 사운드 이벤트가 없다
        const string& path = audioEvents.get();
        if ( GameSound::loadEvents( ResourceID{ path } ) == false )
        {
            SW_LOG_ERROR( "GameSettings audioEvents '%#' could not be loaded (missing or invalid - see the messages above) - the game does not start", path.c_str() );
            return false;
        }
        _loadedAudioEvents = path;
        return true;
    }

    bool GameInstanceBase::makeRequiredDataPath( string_view kindLabel, string& outPath ) const
    {
        string error;
        if ( GamePackDataUtil::tryMakeKindPath( _bootstrap._packRoot.get(), kindLabel, outPath, error ) )
            return true;
        SW_LOG_ERROR( "Required game data '%#' has no path in pack '%#': %# - the game does not start", kindLabel, _bootstrap._packRoot.c_str(), error.c_str() );
        return false;
    }

    void GameInstanceBase::reportRequiredDataFailure( string_view kindLabel, const ResourceID& resourceID ) const
    {
        const string& path    = resourceID.str();
        const utf8*   pReason = ResourceIO::hasResource( resourceID ) ? "the loader rejected it (see the messages above)" : "it is not in the pack or the loader rejected it (see the messages above)";
        SW_LOG_ERROR( "Required game data '%#' (%#) could not be loaded: %# - the game does not start", kindLabel, path.c_str(), pReason );
    }

    /**
     * @brief `"-gv_firstScene=<리소스 경로>"` 인자입니다. 팩 설정(gamesettings)의 시작 씬 대신 이 씬을 처음 엽니다.
     * @details 에디터 없이 다른 씬(환경 쇼케이스 등)을 띄워 볼 때 쓴다.
     * @note PowerShell 은 점이 든 인자를 쪼개므로 따옴표로 감싸야 한다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_firstScene, "", "팩 설정의 시작 씬 대신 처음 열 씬의 리소스 경로 (비우면 사용 안 함)" );

    const string& GameInstanceBase::getFirstScene() const
    {
        if ( gv_firstScene.empty() == false )
            return gv_firstScene;
        // 시작 맵은 필수 칸이다 — 읽기(`GameSettings::loadRoot`)가 빠진 것을 오류로 멈춘다.
        return _bootstrap._data._titleScene.isSet() ? _bootstrap._data._titleScene.get() : _bootstrap._data._startMap.get();
    }

    const string& GameInstanceBase::getEntranceScene() const
    {
        return _bootstrap._data._entranceScene.isSet() ? _bootstrap._data._entranceScene.get() : _bootstrap._data._startMap.get();
    }

    bool GameInstanceBase::requestFirstScene()
    {
        if ( _bResumingWorld == SW_TRUE )
        {
            SW_LOG_INFO( "The game instance was recreated over a live scene - keeping it instead of loading the first scene" );
            return false;
        }
        return requestLoadScene( getFirstScene(), "first" );
    }

    bool GameInstanceBase::requestEntranceScene()
    {
        return requestLoadScene( getEntranceScene(), "entrance" );
    }

    bool GameInstanceBase::requestTitleScene()
    {
        const DataPath& titleScene = _bootstrap._data._titleScene;
        if ( titleScene.isSet() == false )
            return false;
        return requestLoadScene( titleScene.get(), "title" );
    }

    void GameInstanceBase::registerSessionCommands( UISystem& ui )
    {
        // 타이틀 씬이 없는 게임은 "타이틀로" 를 걸지 않는다(창이 없는 호스트는 `initialize` 가 이 함수를 부르지 않는다 — UI 가 없다).
        if ( _bootstrap._data._titleScene.isSet() == false )
            return;
        _returnToTitleRegistration = ui.getGlobalCommands().registerCommand( hashed_string( UIGlobalCommandRegistry::kReturnToTitle ),
                                                                             SW_DELEGATE_METHOD( UICommandDelegate, &GameInstanceBase::onReturnToTitleCommand, this ) );
    }

    void GameInstanceBase::unregisterSessionCommands( UISystem& /*ui*/ )
    {
        _returnToTitleRegistration.reset(); // 핫 리로드의 새 인스턴스가 그 사이 교체했으면 그 등록은 지우지 않는다
    }

    void GameInstanceBase::onReturnToTitleCommand( [[maybe_unused]] const hashed_string& command, [[maybe_unused]] Widget& source )
    {
        if ( requestTitleScene() == false )
            SW_LOG_ERROR( "Return to title: the title scene '%#' was not requested", _bootstrap._data._titleScene.c_str() );
    }

    bool GameInstanceBase::requestLoadScene( const string& scenePath, const utf8* pWhich )
    {
        if ( scenePath.empty() )
            return false;
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
        {
            SW_LOG_WARNING( "The %# scene '%#' is not opened - no SceneManager service", pWhich, scenePath.c_str() );
            return false;
        }
        TaskFuture<Scene*> future = pSceneManager->requestLoadFuture( scenePath );
        if ( future.isValid() == false )
        {
            SW_LOG_ERROR( "The %# scene '%#' could not be requested", pWhich, scenePath.c_str() );
            return false;
        }
        _listPendingSceneLoad.push_back( PendingSceneLoad{ scenePath, std::move( future ) } );
        LoadingScreenController& loadingScreen = getSubsystem<LoadingScreenController>();
        if ( loadingScreen.getSettings()._documentPath.isSet() )
            (void)loadingScreen.beginLoading();

        SceneLoadRequestedEvent event{};
        event._levelName = scenePath;
        GameEventUtil::send( event );
        return true;
    }

    void GameInstanceBase::publishFinishedSceneLoads()
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

    void GameInstanceBase::update( float32 deltaTime )
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
        const float32            realDeltaTime    = GameTimeScale::getUnscaledDeltaTime();
        ScreenTransitionManager& screenTransition = getSubsystem<ScreenTransitionManager>();
        screenTransition.update( realDeltaTime );
        getSubsystem<LoadingScreenController>().update( realDeltaTime, bLoading, loadProgress, screenTransition.fade() );
        if ( _listPendingSceneLoad.empty() == false )
            publishFinishedSceneLoads();
        // 받은 이벤트와 메시지를 게임 프레임 앞에서 처리기에 나눠 준다. 세션을 열지 않았으면 바로 돌아온다. 필수 서비스라 없으면 멈춘다.
        game::getSubsystem<GameNetSession>().update();
        onUpdate( deltaTime );
    }

    bool GameInstanceBase::serializeSceneObjects( SerializePurpose purpose, vector<uint8>& outBytes )
    {
        GameObjectManager* pObjectManager = findActiveObjectManager();
        if ( pObjectManager == nullptr )
            return false;

        // 살아 있는 것만 담는다. `forEachGameObject` 가 파괴 대기 오브젝트를 이미 건너뛰므로 목록을 받아 다시 거를 일이 없다.
        vector<GameObject*> listValidObject;
        pObjectManager->forEachGameObject( [&listValidObject]( GameObject* pObj )
        { listValidObject.push_back( pObj ); } );

        outBytes.clear();

        // 게임오브젝트 개수를 맨 앞에 쓴다.
        const uint32 count   = static_cast<uint32>( listValidObject.size() );
        const size_t oldSize = outBytes.size();
        outBytes.resize( oldSize + sizeof( uint32 ) );
        Memory::copy( outBytes.data() + oldSize, &count, sizeof( uint32 ) );

        for ( GameObject* pObj : listValidObject )
        {
            ObjectStateSerializer::writeIdentity( ObjectStateSerializer::captureIdentity( pObj ), outBytes );
            // 개수를 이미 앞에 적었으므로 하나라도 못 쓰면 개수와 본문이 어긋난다. 그래서 통째로 실패로 처리한다.
            if ( ObjectStateSerializer::saveToBinaryBuffer( pObj, outBytes, ObjectSaveOptions{ purpose } ) == false )
            {
                SW_LOG_ERROR( "Scene object '%#' could not be serialized - the snapshot is not taken", pObj->getName().c_str() );
                outBytes.clear();
                return false;
            }
        }

        return true;
    }

    bool GameInstanceBase::deserializeSceneObjects( SerializePurpose purpose, const uint8* pData, size_t size, SceneObjectFormat format,
                                                    vector<ComponentStateStore::SavedComponentID>& outListRemap )
    {
        outListRemap.clear();
        if ( pData == nullptr || size < sizeof( uint32 ) )
            return false;

        GameObjectManager* pObjectManager = findActiveObjectManager();
        if ( pObjectManager == nullptr )
            return false;

        // 기존 오브젝트를 정리한다.
        pObjectManager->clear();

        size_t offset = 0;
        uint32 count  = 0;
        Memory::copy( &count, pData + offset, sizeof( uint32 ) );
        offset += sizeof( uint32 );

        vector<GameObject*> listRestoredObject;

        // **파일이 말한 개수를 그대로 잡아 두지 않는다.** 오브젝트 하나는 적어도 길이 4바이트를
        // 쓰므로, 남은 바이트 / 4 보다 많은 오브젝트는 어떤 스냅샷에도 있을 수 없다. 아래 읽기는
        // 잘린 데이터에서 어차피 멈추지만, **그 전에 이 `reserve` 가 먼저 터진다.** `count` 가
        // 40억이면 이 한 줄이 수십 기가를 요구한다. `SceneDocument::loadBinary` 가 같은 이유로
        // 같은 계산을 한다.
        constexpr size_t kMinBytesPerObject = sizeof( uint32 );
        const size_t     maxPossibleObject  = ( size - offset ) / kMinBytesPerObject;
        listRestoredObject.reserve( MathUtil::min( static_cast<size_t>( count ), maxPossibleObject ) );

        // 모든 게임오브젝트를 만들고 읽은 뒤에 묶음이 계층을 한 번에 잇는다.
        // 같은 프로세스의 스냅샷이면 원래 id 로 만들고 컴포넌트 id 도 복원한다.
        // 부착은 상태의 부착 필드(부모의 **id** 와 컴포넌트 키)로 잇는다. 부모 이름으로 찾아 부모의 primary 에 붙이면
        // 부모보다 먼저 읽힌 소켓 자식(손에 든 무기)이 몸통으로 옮겨 가므로 깨진다.
        // 다른 실행의 세이브는 id 를 복원하지 않고, 스트림에 적힌 그때의 id 로 묶음 안에서 부모를 찾는다.
        // **하나라도 못 읽으면 실패다.** 읽기를 멈추고도 true 를 돌려주면 핫 리로드가 스냅샷을 버리고 저장 막기를 풀어 버린다.
        // 씬은 이미 비운 뒤라서, 못 읽은 오브젝트부터 뒤가 사라진 채 저장될 수 있다.
        const bool       bRestoreIdentity = ( format == SceneObjectFormat::RestoreIdentity );
        bool             bComplete        = true;
        ObjectStateBatch batch( bRestoreIdentity ? ObjectIDSpace::Live : ObjectIDSpace::Saved );
        for ( uint32 objectIndex = 0; objectIndex < count; ++objectIndex )
        {
            ObjectIdentity identity;
            const size_t   identityBytes = ObjectStateSerializer::readIdentity( pData + offset, size - offset, identity );
            if ( identityBytes == 0 )
            {
                SW_LOG_ERROR( "Failed to read object identity at index %u", objectIndex );
                bComplete = false;
                break;
            }
            offset += identityBytes;

            GameObject*       pObj = bRestoreIdentity ? pObjectManager->createGameObjectWithID( hashed_string( GameObject::kDefaultName ), identity._objectID )
                                                      : pObjectManager->createGameObject();
            ObjectLoadContext context{ purpose, bRestoreIdentity ? &identity : nullptr };
            context._pBatch        = &batch;
            context._savedID       = identity._objectID;
            const size_t readBytes = ObjectStateSerializer::loadFromBinaryBuffer( pObj, pData + offset, size - offset, context );
            if ( readBytes == 0 )
            {
                SW_LOG_ERROR( "Failed to load binary object state at index %u", objectIndex );
                pObjectManager->destroyObject( pObj ); // 읽지 못한 자리에 빈 오브젝트를 남기지 않는다.
                bComplete = false;
                break;
            }
            listRestoredObject.push_back( pObj );
            offset += readBytes;
            if ( bRestoreIdentity == false )
                ComponentStateStore::appendComponentIDRemap( identity, *pObj, outListRemap );
        }

        // 씬 계층 구조(부모와 소켓)를 한 번에 잇는다. 자식이 부모보다 먼저 읽혔어도 부모를 찾을 수 있다.
        // 옮기지 못한 핸들 PROPERTY(다른 실행의 세이브)는 로드 오류다 — 비우고 이어 가면 값이 조용히 사라진다.
        bComplete = batch.finish() && bComplete;

        // 복원된 모든 오브젝트의 월드 매트릭스를 강제로 동기화한다.
        pObjectManager->flushSceneTransforms();

        if ( bComplete == false )
        {
            SW_LOG_ERROR( "Scene restore stopped after %# of %# objects - the snapshot is kept and saving stays blocked",
                          static_cast<uint32>( listRestoredObject.size() ), count );
            return false;
        }
        return true;
    }

    void GameInstanceBase::captureStatefulComponents()
    {
        GameObjectManager* pManager = findActiveObjectManager();
        if ( pManager == nullptr )
            return;
        // 모두 싣고 나서 걷는다. 걷기가 다른 타입의 상태(플레이어가 읽는 디렉터)를 바꾸지 않게 하려는 순서다.
        for ( const StatefulComponentType& type : _listStatefulType )
        {
            if ( type._pCapture != nullptr )
                type._pCapture( *_pComponentStateStore, *pManager );
        }
        for ( const StatefulComponentType& type : _listStatefulType )
        {
            if ( type._pDespawn != nullptr )
                type._pDespawn( *pManager );
        }
    }

    void GameInstanceBase::restoreStatefulComponents()
    {
        GameObjectManager* pManager = findActiveObjectManager();
        if ( pManager == nullptr )
            return;
        for ( const StatefulComponentType& type : _listStatefulType )
        {
            if ( type._pRestore != nullptr )
                type._pRestore( *_pComponentStateStore, *pManager );
        }
    }

    bool GameInstanceBase::serializeState( vector<uint8>& outBytes )
    {
        return captureSnapshot( SerializePurpose::Snapshot, outBytes );
    }

    bool GameInstanceBase::deserializeState( const void* pInBuffer, uint32 size )
    {
        return readStateEnvelope( SerializePurpose::Snapshot, pInBuffer, size );
    }

    bool GameInstanceBase::captureSnapshot( SerializePurpose purpose, vector<uint8>& outBytes )
    {
        _pComponentStateStore->clear();
        captureStatefulComponents();
        onBeforeStateSerialize();

        Archive arch;
        arch << StateEnvelopeInternal::kMagic;
        arch << StateEnvelopeInternal::kVersion;
        arch << ObjectStateSerializer::getProcessToken();

        // 1) 씬 오브젝트 바이너리 스냅샷
        //
        // **씬이 없는 것은 실패가 아니다.** 씬 없이 커스텀 상태만 스냅샷하는 것은 지원하는 사용법이다
        // (GameFrameworkTest.GameInstanceBaseSnapshotAndFileRoundTrip 이 그렇게 쓴다). 그러니 여기서 끊으면 안 된다.
        // 다만 반환값을 흔적 없이 버리면, 씬이 있어야 할 상황에서 오브젝트가 하나도 없는 세이브가 나와도
        // 로드할 때까지 아무도 모른다. 빈 섹션은 그대로 쓰되 단서는 남긴다(지원하는 사용법이라 Warning 이 아니라 Info).
        vector<uint8> bytesScene;
        if ( serializeSceneObjects( purpose, bytesScene ) == false )
            SW_LOG_INFO( "The scene object snapshot is empty - there is no active scene or the game service is not bound. Only the custom state is saved." );
        arch.writeSection( bytesScene.data(), static_cast<uint32>( bytesScene.size() ) );

        // 2) 파생 클래스 커스텀 리플렉션 상태 스냅샷
        const TypeInfo* pStateTypeInfo = getStateTypeInfo();
        const void*     pStateInstance = getStateInstance();
        if ( pStateTypeInfo != nullptr && pStateInstance != nullptr )
        {
            // 값 하나라도 싣지 못하면 세이브 전체를 실패로 돌려준다. 써 두면 로드에서 그 값이 빠진다. 이유는 직렬화기가 알린다.
            vector<uint8> bytesState;
            if ( BinarySerializer::serialize( pStateInstance, *pStateTypeInfo, bytesState, SerializeContext::getDefault( purpose ) ) == false )
            {
                _pComponentStateStore->clear();
                return false;
            }
            arch.writeSection( bytesState.data(), static_cast<uint32>( bytesState.size() ) );
        }
        else
        {
            arch.writeSection( nullptr, 0 );
        }

        // 3) PROPERTY 가 아닌 컴포넌트 상태(디렉터의 시뮬레이션). 훅이 실어 둔 것을 쓴다.
        Archive componentArch;
        _pComponentStateStore->write( componentArch );
        arch.writeSection( componentArch.getData(), static_cast<uint32>( componentArch.getSize() ) );
        _pComponentStateStore->clear();

        // 봉투는 한 번 써서 그대로 넘긴다.
        outBytes.assign( arch.getData(), arch.getData() + arch.getSize() );
        return true;
    }

    bool GameInstanceBase::readStateEnvelope( SerializePurpose purpose, const void* pInBuffer, uint32 size )
    {
        if ( pInBuffer == nullptr || size < sizeof( uint32 ) )
            return false;

        uint32 magic = 0;
        Memory::copy( &magic, pInBuffer, sizeof( uint32 ) );

        if ( magic != StateEnvelopeInternal::kMagic )
        {
            SW_LOG_ERROR( "State snapshot has no SWST header - not written by GameInstanceBase::serializeState" );
            return false;
        }

        if ( size < sizeof( uint32 ) * 4 )
            return false;

        Archive arch( static_cast<const uint8*>( pInBuffer ), size );
        arch >> magic;

        uint32 version = 0;
        arch >> version;
        if ( version != StateEnvelopeInternal::kVersion )
        {
            SW_LOG_ERROR( "State snapshot version %# is not this build's %#", version, StateEnvelopeInternal::kVersion );
            return false;
        }

        uint64 processToken = 0;
        arch >> processToken;
        const bool              bSameProcess = ( processToken == ObjectStateSerializer::getProcessToken() );
        const SceneObjectFormat format       = bSameProcess ? SceneObjectFormat::RestoreIdentity : SceneObjectFormat::WithIdentity;

        // 1) 씬 오브젝트 복원
        vector<uint8> bytesScene;
        if ( arch.readSection( bytesScene ) == false )
            return false;

        vector<ComponentStateStore::SavedComponentID> listComponentIDRemap;
        if ( bytesScene.empty() == false && deserializeSceneObjects( purpose, bytesScene.data(), bytesScene.size(), format, listComponentIDRemap ) == false )
            return false;

        // 2) 파생 클래스 커스텀 리플렉션 상태 복원
        vector<uint8> bytesState;
        if ( arch.readSection( bytesState ) == false )
            return false;

        if ( bytesState.empty() == false )
        {
            const TypeInfo* pStateTypeInfo = getStateTypeInfo();
            void*           pStateInstance = getStateInstance();
            if ( pStateTypeInfo != nullptr && pStateInstance != nullptr )
            {
                if ( BinarySerializer::deserialize( pStateInstance, *pStateTypeInfo, bytesState.data(), bytesState.size(), SerializeContext::getDefault( purpose ) ) == false )
                    return false;
            }
        }

        // 3) 컴포넌트 상태는 훅이 다시 만든 컴포넌트에 넘긴다. 못 읽으면 알리고 넘어간다. 씬은 이미 섰고, 디렉터는 새로 시작한다.
        vector<uint8> bytesComponent;
        if ( arch.readSection( bytesComponent ) == false )
            return false;
        _pComponentStateStore->clear();
        if ( bytesComponent.empty() == false )
        {
            Archive componentArch( bytesComponent.data(), bytesComponent.size() );
            if ( _pComponentStateStore->read( componentArch ) == false )
                SW_LOG_WARNING( "Component state section could not be read - components start fresh" );
            // 다른 실행의 세이브는 컴포넌트 id 가 새로 나갔다. 씬 섹션이 만든 표로 저장된 id 를 이번 id 로 바꿔 id 로만 짝짓는다.
            if ( format == SceneObjectFormat::WithIdentity )
                _pComponentStateStore->remapComponentIDs( listComponentIDRemap );
        }

        if ( arch.isError() )
            return false;

        restoreStatefulComponents();
        onAfterStateDeserialize();
        _pComponentStateStore->clear();
        return true;
    }

    bool GameInstanceBase::restoreSnapshot( SerializePurpose purpose, const vector<uint8>& inBytes )
    {
        if ( inBytes.empty() )
            return false;
        return readStateEnvelope( purpose, inBytes.data(), static_cast<uint32>( inBytes.size() ) );
    }

    bool GameInstanceBase::saveStateToFile( string_view filePath )
    {
        const DataPath& defaultSavePath = _bootstrap._data._defaultSavePath;
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

    bool GameInstanceBase::loadStateFromFile( string_view filePath )
    {
        const DataPath& defaultSavePath = _bootstrap._data._defaultSavePath;
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
