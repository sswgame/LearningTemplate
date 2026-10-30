#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"

#include "Core/File/FileUtil.h"
#include "Core/File/UserDataPath.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Config/GameSelection.h"
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

#include "GameFramework/Base/Foundation/Framework/Flow/GameSceneFlow.h"
#include "GameFramework/Base/Foundation/Framework/Flow/LoadingScreenController.h"
#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Base/Foundation/GameData/GamePackDataUtil.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

namespace sw
{
    SW_LOG_CALLER( "GameInstance" );

    GameInstance::GameInstance()
        : _packConfig{}
        , _pWindow{ nullptr }
        , _pRHIDevice{ nullptr }
        , _stateSnapshot{}
        , _gameScope{}
        , _loadedAudioEvents{}
    {
        // 인스턴스 서브시스템 — initialize 가 onInitialize 앞에 초기화하고 shutdown 이 onShutdown 뒤에 역순으로 종료한다.
        ScreenTransitionManager& screenTransition = _gameScope.addSubsystem( make_unique<ScreenTransitionManager>() );
        LoadingScreenController& loadingScreen    = _gameScope.addSubsystem( make_unique<LoadingScreenController>() );
        // 씬 흐름은 이 인스턴스의 gamesettings(`_packConfig` — 범위보다 먼저 선 멤버)와 두 형제를 빌려 든다.
        _gameScope.addSubsystem( make_unique<GameSceneFlow>( _packConfig._data, screenTransition, loadingScreen ) );
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
        _pWindow    = pWindow;
        _pRHIDevice = pRHIDevice;
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
        // 인스턴스 서브시스템을 onInitialize 앞에 세운다(의존 순서: 화면 전환 · 로딩 화면 → 씬 흐름) — 씬 흐름이 씬 로드 받아들임 알림을 걸어 첫 씬 요청(onInitialize 안)이
        // 로딩 화면을 띄우고, 파생이 네트워크 역할을 보고 키트 처리기를 단다. 하나라도 서지 않으면(로딩 팁 파일을 못 읽었다) 게임을 띄우지 않는다 — 파생 초기화도 부르지 않는다.
        const bool bSubsystemsStarted = _gameScope.getCollection().initializeAll( SubsystemHostMask::All );
        if ( bSubsystemsStarted == false )
            SW_LOG_ERROR( "Game instance subsystems did not all initialize: %#", _gameScope.getCollection().getError().c_str() );
        // 네트워크 세션은 엔진 수명이다 — 처음이면 실행 인자로 열고, 리로드로 다시 선 인스턴스는 열린 세션을 이어받는다(클라이언트가 끊기지 않는다).
        // 서버 · 클라이언트로 띄웠는데 열지 못하면(틀린 인자 · 포트 사용 중) 혼자 하는 게임으로 서지 않는다.
        // 티켓 게임 포트가 쓸 암호 제공자(OpenSSL)는 GameFramework 에 있어 Engine 이 모른다 — 세션을 열기 전에 넣는다.
        GameNetSession& netSession = game::getSubsystem<GameNetSession>(); // `checkEngineServices` 가 있음을 확인했다
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
        const bool bInitialized = bSubsystemsStarted && bPackConfigApplied && bNetSessionOpened && loadAudioEvents() && onInitialize();
        // 창이 없는 호스트에는 UI 가 없다 — UI 전역 명령 표도 쓰지 않는다. 파생 초기화 뒤에 건다("타이틀로" 는 첫 씬을 요청한 뒤의 일이다).
        if ( game::findSubsystem<UISystem>() != nullptr )
            getSubsystem<GameSceneFlow>().registerSessionCommands( game::getSubsystem<UIGlobalCommandRegistry>() );

        // 플레이어 설정을 다시 넣는다. 언어 팩과 입력 맵(키 바인딩, 누르기/토글)은 위에서 막 생겼고, 엔진 기동 때의 적용은 그 대상이 없을 때 한 것이다.
        // 액션을 더 늦게(컴포넌트 시작 때) 만드는 게임은 그 뒤에 `UserSettingsManager::reapplyAll` 을 부른다. 창이 없는 호스트는 사용자 설정이 없다.
        if ( UserSettingsManager* const pUserSettings = game::findSubsystem<UserSettingsManager>() )
            pUserSettings->reapplyAll();
        return bInitialized;
    }

    void GameInstance::shutdown()
    {
        getSubsystem<GameSceneFlow>().unregisterSessionCommands(); // 걸지 않았으면(창이 없는 호스트 · 타이틀 씬 없음) 빈 구독이다
        onShutdown();
        if ( _loadedAudioEvents.empty() == false )
        {
            GameSound::unloadEvents( _loadedAudioEvents );
            _loadedAudioEvents.clear();
        }
        // 인스턴스 서브시스템(씬 흐름 → 로딩 화면 · 전환)을 onShutdown 뒤에 내린다 — 씬 흐름이 받아들임 알림을 풀고 맡긴 로드를 잊는다.
        // 네트워크 세션은 엔진 수명이라 닫지 않는다(리로드를 넘어 연결을 지킨다).
        _gameScope.getCollection().shutdownAll();
        // 제공자는 GameFramework 의 것이라 이 인스턴스가 내려가면 거둔다(다시 서는 인스턴스가 다시 넣는다). 열린 세션은 이미 받은 포인터로 계속 돈다.
        if ( GameNetSession* pNetSession = game::findSubsystem<GameNetSession>() )
            pNetSession->getConnectSecurity().setProvider( nullptr );
        detachGameScope();
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

    void GameInstance::update( float32 deltaTime )
    {
        // 프레임 순서: 씬 흐름(페이드 · 로딩 화면 · 끝난 로드 알림) → 넷 세션 → 파생 프레임 훅.
        getSubsystem<GameSceneFlow>().update();
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
        // 상대 경로는 그 게임의 사용자 데이터 폴더 아래다(로컬 저장소와 같은 규칙 - 작업 폴더가 아니다).
        const string  path = UserDataPath::resolve( GameSelection::getName(), filePath.empty() ? string_view( defaultSavePath.get() ) : filePath );
        vector<uint8> snapshotBytes;
        const bool    bSaved = captureSnapshot( SerializePurpose::SaveGame, snapshotBytes ) && FileUtil::writeFile( path, snapshotBytes.data(), snapshotBytes.size() );

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
        const string  path = UserDataPath::resolve( GameSelection::getName(), filePath.empty() ? string_view( defaultSavePath.get() ) : filePath );
        vector<uint8> snapshotBytes;
        const bool    bLoaded = FileUtil::readFile( path, snapshotBytes ) && restoreSnapshot( SerializePurpose::SaveGame, snapshotBytes );

        SaveGameLoadedEvent event{};
        event._savePath = string( path );
        event._bSuccess = bLoaded;
        GameEventUtil::send( event );
        return bLoaded;
    }
} // namespace sw
