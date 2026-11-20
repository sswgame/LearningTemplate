#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceManager.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneSubLevelSubsystem.h"
#include "Engine/Serialization/Serializer/SerializePurpose.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceKitRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceScope.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstanceClientHook.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

namespace sw
{
    /**
     * @brief `"-gv_firstExperience=<이름>"` 인자입니다. 첫 씬(`gv_firstScene` > 타이틀 > 시작 맵) 대신 이 Experience(`<팩>/experiences/<이름>.experience.xml`)로 들어갑니다.
     * @details 언리얼 Lyra 의 URL 옵션 `?Experience=` 자리다. 전역 변수라 시나리오 `<Launch>` 가 그대로 쓰고 체인 단계마다 되돌려진다(따로 둔 실행 인자는 단계마다 바꿀 길이 없었다).
     *          `gv_firstScene` 처럼 배포에도 있다 — 배포본의 시나리오 · 전용 서버도 미니게임으로 바로 들어간다. 허브 이름이면 허브 맵을 연다(허브 입장).
     */
    SW_GLOBAL_VARIABLE( sw::GlobalString, gv_firstExperience, "", "첫 씬 대신 처음 들어갈 Experience 이름 (비우면 사용 안 함)" );

    namespace
    {
        struct ExperienceManagerInternal
        {
            /** @brief `experience.enter <이름>` 의 몸통입니다. 맡기기만 하고 켜졌는지는 로그 · 감시 값으로 본다(맵 로드를 기다린다). */
            static bool runEnterCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                ExperienceManager* const                 pManager = game::findSubsystem<ExperienceManager>();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( pManager == nullptr )
                    replyBuilder.append( "experience.enter: no game is running" );
                else
                    replyBuilder.appendFormat( "experience.enter: %# '%#'", pManager->enter( listArgument[0] ) ? "entering" : "refused", listArgument[0].c_str() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `experience.leave` 의 몸통입니다 — 미니게임을 끄고 허브로 돌아간다(돌아왔는지는 감시 값 `Experience.Active` · `Experience.HubRestoreCount`). */
            static bool runLeaveCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                ExperienceManager* const pManager = game::findSubsystem<ExperienceManager>();
                if ( pManager == nullptr )
                    outReply = "experience.leave: no game is running";
                else
                    outReply = pManager->returnToHub() ? "experience.leave: returning to the hub" : "experience.leave: refused";
                return true;
            }

            /** @brief 감시 값 `Experience.Active` 입니다. 켜진 미니게임 이름이고, 없으면 `none` 입니다(맵을 기다리는 중도 `none`, 허브만 켜졌어도 `none`). */
            [[nodiscard]] static bool readActive( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const ExperienceManager* const pManager = game::findSubsystem<ExperienceManager>();
                if ( pManager == nullptr )
                {
                    outValue.setText( "no game is running" );
                    return false;
                }
                const ExperienceDefinition* const pActive = pManager->isEntering() ? nullptr : pManager->findActiveDefinition();
                outValue.setText( pActive != nullptr ? pActive->_name.c_str() : "none" );
                return true;
            }

            /** @brief 감시 값 `Experience.Hub` 입니다. 켜진 허브 이름이고, 허브가 없는 게임은 `none` 입니다. */
            [[nodiscard]] static bool readHub( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const ExperienceManager* const pManager = game::findSubsystem<ExperienceManager>();
                if ( pManager == nullptr )
                {
                    outValue.setText( "no game is running" );
                    return false;
                }
                const ExperienceDefinition* const pHub = pManager->findHubDefinition();
                outValue.setText( pHub != nullptr ? pHub->_name.c_str() : "none" );
                return true;
            }

            /** @brief 감시 값 `Experience.LeakCount` 입니다 — 이 인스턴스에서 끈 Experience 범위가 남긴 것의 합(0 이 아니면 `ExperienceScope.Leaked*` 오류도 남았다). */
            [[nodiscard]] static bool readLeakCount( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const ExperienceManager* const pManager = game::findSubsystem<ExperienceManager>();
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getLeakCount() );
                return true;
            }

            /** @brief 감시 값 `Experience.HubRestoreCount` 입니다 — 씬 전환 미니게임에서 돌아와 허브 스냅숏을 복원한 수(돌아가기가 끝났는지 기다리는 값). */
            [[nodiscard]] static bool readHubRestoreCount( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const ExperienceManager* const pManager = game::findSubsystem<ExperienceManager>();
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getHubRestoreCount() );
                return true;
            }
        };
    } // namespace

    ExperienceManager::ExperienceManager( SubsystemScope& gameScope, const GamePackConfig& packConfig, GameInstance& owner )
        : _gameScope( gameScope )
        , _packConfig( packConfig )
        , _owner( owner )
        , _pHub{ nullptr }
        , _pActive{ nullptr }
        , _hubSnapshot{}
        , _pendingEnter{}
        , _leakCount{ 0 }
        , _hubRestoreCount{ 0 }
        , _bEntering{ SW_FALSE }
        , _bReturning{ SW_FALSE }
    {
    }

    ExperienceManager::~ExperienceManager() = default;

    void ExperienceManager::shutdownSubsystem()
    {
        _pendingEnter.clear();
        _hubSnapshot.clear();
        _bReturning = SW_FALSE;
        deactivateActive();
        deactivateScope( _pHub ); // 허브는 바탕이라 마지막에 끈다(미니게임의 해제 검사는 허브가 켜진 채로 찍었다)
    }

    bool ExperienceManager::activateHub()
    {
        const string& hubName = _packConfig._data._hubExperience;
        if ( hubName.empty() )
            return true;
        ExperienceDefinition definition;
        if ( loadDefinition( hubName, definition ) == false )
            return false;
        if ( definition._entry != ExperienceEntry::SceneSwitch )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.HubNotSceneSwitch", "Hub experience '%#' must have entry=\"SceneSwitch\" - the hub is a map you travel to, not a sub-level", hubName.c_str() );
            return false;
        }
        _pHub = activate( definition );
        if ( _pHub == nullptr )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.HubNotActivated", "Hub experience '%#' (GameSettings hubExperience) could not be activated (see the messages above) - the game does not start",
                             hubName.c_str() );
            return false;
        }
        return true;
    }

    bool ExperienceManager::enterFirstExperience()
    {
        const string name = gv_firstExperience.get();
        if ( name.empty() )
            return false;
        SW_LOG_INFO_ID( "ExperienceManager.FirstExperience", "First experience %# (gv_firstExperience)", name.c_str() );
        (void)enter( name ); // 실패는 위에서 알렸다 — 다른 씬으로 대신하지 않는다
        return true;
    }

    string ExperienceManager::makeAssetPath( const GamePackConfig& packConfig, string_view name )
    {
        return packConfig.resolve( string( kExperienceFolder ) + string( name ) + kExperienceSuffix );
    }

    bool ExperienceManager::loadDefinition( string_view name, ExperienceDefinition& outDefinition ) const
    {
        const string assetPath = makeAssetPath( _packConfig, name );
        if ( ExperienceDefinition::loadFromAsset( AssetID{ assetPath }, outDefinition ) == false )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.NotLoaded", "Experience '%#' (%#) could not be read (see the messages above)", name, assetPath.c_str() );
            return false;
        }
        if ( outDefinition._name != hashed_string( name ) )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.NameMismatch", "%#: name=\"%#\" differs from the file name '%#'", assetPath.c_str(), outDefinition._name.c_str(), name );
            return false;
        }
        // 등록부는 GameFramework 의 모듈 서브시스템이다 — 없으면 호스트가 모듈을 올리지 않은 것이라 멈춘다.
        return outDefinition.validate( game::getSubsystem<ExperienceKitRegistry>(), game::getSubsystem<TypeRegistry>(), assetPath );
    }

    bool ExperienceManager::refuseWhileLoading( string_view name ) const
    {
        if ( isEntering() == false && isReturning() == false )
            return false;
        const utf8* const pWaiting = isEntering() ? _pActive->getDefinition()._name.c_str() : _pHub->getDefinition()._name.c_str();
        SW_LOG_ERROR_ID( "ExperienceManager.EnterBusy", "Experience '%#' is refused - experience '%#' is still waiting for its map", name, pWaiting );
        return true;
    }

    bool ExperienceManager::enter( string_view name )
    {
        if ( refuseWhileLoading( name ) )
            return false;
        if ( _pHub != nullptr && _pHub->getDefinition()._name == hashed_string( name ) )
            return returnToHub();
        ExperienceDefinition definition;
        if ( loadDefinition( name, definition ) == false )
            return false;
        Scene* const pHubScene = findActiveHubScene();
        if ( definition._entry == ExperienceEntry::SubLevel && pHubScene == nullptr )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.SubLevelNeedsHub", "Experience '%#' (entry=\"SubLevel\") is added to the hub scene - enter it from inside the hub (hub experience: %#)", name,
                             _pHub != nullptr ? _pHub->getDefinition()._name.c_str() : "none - GameSettings has no hubExperience" );
            return false;
        }
        // 앞 미니게임을 끄고 새 것을 켠 뒤 맵을 요청한다 — 맵의 컴포넌트가 씬 적용(`beginPlay`) 때 키트 서비스 · 카탈로그를 찾는다. 앞 맵의 컴포넌트가 아직 산 동안 칸이 비어도
        // 두 Experience 가 섞이지는 않는다(같은 서비스를 둘이 걸면 `ExperienceScope::bindService` 가 거절한다).
        deactivateActive();
        // 허브 안에서 씬을 바꾸면 허브를 찍어 두고 돌아올 때 복원한다. 허브 밖에서 바로 들어간 미니게임(`gv_firstExperience`)은 돌아올 때 허브 맵을 새로 연다.
        if ( definition._entry == ExperienceEntry::SceneSwitch && pHubScene != nullptr )
        {
            _hubSnapshot.clear();
            if ( _owner.captureSnapshot( SerializePurpose::Snapshot, _hubSnapshot ) == false )
            {
                SW_LOG_ERROR_ID( "ExperienceManager.HubNotCaptured", "Experience '%#': the hub could not be captured before switching scenes - nothing is entered", name );
                _hubSnapshot.clear();
                return false;
            }
        }
        _pActive = activate( definition );
        if ( _pActive == nullptr )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.NotActivated", "Experience '%#' could not be activated (see the messages above) - nothing of it stays bound", name );
            return false;
        }
        const string mapPath = definition._map.get();
        if ( definition._entry == ExperienceEntry::SubLevel )
        {
            // 하위 레벨 이름은 Experience 이름이다 — 나올 때 그 이름으로 뺀다. 자리는 원점이다(맵을 허브 좌표로 놓는다 — 언리얼 레벨 인스턴스를 원점에 두는 꼴).
            SceneSubLevelSubsystem& subLevels = pHubScene->getObjectManager()->getSubsystem<SceneSubLevelSubsystem>();
            if ( subLevels.addSubLevel( *pHubScene, name, mapPath, SubLevelPlacement{} ) == false )
            {
                SW_LOG_ERROR_ID( "ExperienceManager.SubLevelRefused", "Experience '%#': the map '%#' could not be added to the hub scene (see the messages above)", name, mapPath.c_str() );
                deactivateActive();
                return false;
            }
            return true;
        }
        if ( game::getSubsystem<GameSceneFlow>().requestSceneThen( mapPath, GameSceneFlow::SceneLoadedFunction::create<&ExperienceManager::onMapLoaded>( this ) ) == false )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.MapRequestRefused", "Experience '%#': the map '%#' could not be requested", name, mapPath.c_str() );
            deactivateActive();
            return false;
        }
        _bEntering = SW_TRUE;
        return true;
    }

    bool ExperienceManager::returnToHub()
    {
        if ( _pHub == nullptr )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.NoHub", "There is no hub to return to - GameSettings has no hubExperience" );
            return false;
        }
        const ExperienceDefinition& hub = _pHub->getDefinition();
        if ( refuseWhileLoading( hub._name.view() ) )
            return false;
        deactivateActive();
        if ( findActiveHubScene() != nullptr )
            return true; // 하위 레벨 미니게임에서 나왔거나 이미 허브에 있다 — 허브는 그대로 섰다
        const string mapPath = hub._map.get();
        if ( game::getSubsystem<GameSceneFlow>().requestSceneThen( mapPath, GameSceneFlow::SceneLoadedFunction::create<&ExperienceManager::onHubLoaded>( this ) ) == false )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.HubMapRequestRefused", "The hub map '%#' could not be requested", mapPath.c_str() );
            return false;
        }
        _bReturning = SW_TRUE;
        return true;
    }

    void ExperienceManager::requestEnter( string_view name )
    {
        if ( _pendingEnter.empty() == false )
        {
            SW_LOG_WARNING_ID( "ExperienceManager.EnterAlreadyRequested", "Experience '%#' is dropped - '%#' was requested first in this frame", name, _pendingEnter.c_str() );
            return;
        }
        _pendingEnter = string( name );
    }

    void ExperienceManager::update()
    {
        if ( _pendingEnter.empty() )
            return;
        const string name = std::move( _pendingEnter );
        _pendingEnter.clear();
        (void)enter( name ); // 실패는 enter 가 알렸다
    }

    const ExperienceDefinition* ExperienceManager::findActiveDefinition() const
    {
        return _pActive != nullptr ? &_pActive->getDefinition() : nullptr;
    }

    const ExperienceDefinition* ExperienceManager::findHubDefinition() const
    {
        return _pHub != nullptr ? &_pHub->getDefinition() : nullptr;
    }

    Scene* ExperienceManager::findActiveHubScene() const
    {
        if ( _pHub == nullptr )
            return nullptr;
        const SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
        Scene* const              pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr || pScene->getSourcePath() != _pHub->getDefinition()._map.get() )
            return nullptr;
        return pScene;
    }

    void ExperienceManager::deactivateActive()
    {
        _bEntering = SW_FALSE; // 기다리던 맵의 완료 알림이 와도 할 일이 없다
        if ( _pActive == nullptr )
            return;
        const ExperienceDefinition& definition = _pActive->getDefinition();
        // 하위 레벨은 허브 씬에 아직 있을 때만 뺀다 — 씬이 바뀌었으면(불러오기 · 다른 씬) 씬 끝내기가 이미 버렸다.
        if ( definition._entry == ExperienceEntry::SubLevel )
        {
            Scene* const pHubScene = findActiveHubScene();
            if ( pHubScene != nullptr )
            {
                SceneSubLevelSubsystem& subLevels = pHubScene->getObjectManager()->getSubsystem<SceneSubLevelSubsystem>();
                SubLevelState           state{ SubLevelState::Queued };
                if ( subLevels.findSubLevelState( definition._name.view(), state ) )
                    (void)subLevels.removeSubLevel( definition._name.view() ); // 방금 찾은 이름이다
            }
        }
        deactivateScope( _pActive );
    }

    void ExperienceManager::deactivateScope( unique_ptr<ExperienceScope>& pScope )
    {
        if ( pScope == nullptr )
            return;
        const uint32 leakCount = pScope->deactivate(); // 범위가 건 역순으로 거두고 남은 것을 알린다
        _leakCount += leakCount;
        SW_LOG_INFO_ID( "ExperienceManager.Left", "Experience '%#' is deactivated (%# left behind)", pScope->getDefinition()._name.c_str(), leakCount );
        pScope.reset();
    }

    void ExperienceManager::onMapLoaded( Scene* pScene )
    {
        if ( isEntering() == false || _pActive == nullptr )
            return;
        _bEntering = SW_FALSE;
        if ( pScene != nullptr )
            return;
        const ExperienceDefinition& definition = _pActive->getDefinition();
        SW_LOG_ERROR_ID( "ExperienceManager.MapNotOpened", "Experience '%#': the map '%#' did not open (failed or replaced by a later request) - the experience is deactivated", definition._name.c_str(),
                         definition._map.c_str() );
        deactivateActive();
    }

    void ExperienceManager::onHubLoaded( Scene* pScene )
    {
        if ( isReturning() == false )
            return;
        _bReturning                 = SW_FALSE;
        const vector<uint8> capture = std::move( _hubSnapshot );
        _hubSnapshot.clear();
        if ( pScene == nullptr )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.HubMapNotOpened", "The hub map did not open (failed or replaced by a later request) - the captured hub is dropped" );
            return;
        }
        if ( capture.empty() )
            return; // 허브 밖에서 바로 들어간 미니게임이었다 — 허브 맵을 새로 열었다
        // 세이브 불러오기와 같은 길이다 — 찍은 씬을 연 뒤 같은 목적의 봉투를 읽는다(`GameInstance::onSaveSceneLoaded`).
        if ( _owner.restoreSnapshot( SerializePurpose::Snapshot, capture ) == false )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.HubNotRestored", "The captured hub could not be restored (see the messages above) - the hub map stays as it opened" );
            return;
        }
        ++_hubRestoreCount;
        SW_LOG_INFO_ID( "ExperienceManager.HubRestored", "Returned to hub '%#' - the captured hub is restored", _pHub->getDefinition()._name.c_str() );
    }

    unique_ptr<ExperienceScope> ExperienceManager::activate( const ExperienceDefinition& definition )
    {
        InputManager* const          pInput = game::findSubsystem<InputManager>(); // 입력 서비스가 없는 호스트(전용 서버)는 입력 레이어를 걸지 않는다
        unique_ptr<ExperienceScope>  scope  = make_unique<ExperienceScope>( _gameScope, game::getSubsystem<GlobalVariableManager>(), _packConfig, pInput != nullptr ? &pInput->getInputMap() : nullptr,
                                                                            definition );
        const ExperienceKitRegistry& kits   = game::getSubsystem<ExperienceKitRegistry>();
        for ( const hashed_string& kitName : definition._listKit )
        {
            const ExperienceKitRegistration* const pKit = kits.findKit( kitName.view() ); // 검사 뒤에 키트 모듈이 내려갔을 수 있다 — 다시 찾는다
            if ( pKit == nullptr )
            {
                SW_LOG_ERROR_ID( "ExperienceManager.KitGone", "Experience '%#': kit '%#' is no longer registered", definition._name.c_str(), kitName.c_str() );
                return nullptr;
            }
            if ( pKit->_pActivate( *scope ) == false )
            {
                SW_LOG_ERROR_ID( "ExperienceManager.KitNotActivated", "Experience '%#': kit '%#' could not be activated (see the messages above)", definition._name.c_str(), kitName.c_str() );
                return nullptr;
            }
        }
        // 적은 카탈로그를 아무 키트도 읽지 않았으면 데이터와 키트가 어긋났다 — 적지 않은 것을 읽는 쪽은 `ExperienceScope::loadCatalog` 가 막았다.
        bool bCatalogsLoaded = true;
        for ( const hashed_string& catalog : definition._listCatalog )
        {
            if ( VectorUtil::contains( scope->getLoadedCatalogs(), catalog ) )
                continue;
            SW_LOG_ERROR_ID( "ExperienceManager.CatalogNotLoaded", "Experience '%#': catalog '%#' is declared but no kit in kits=\"...\" loaded it", definition._name.c_str(), catalog.c_str() );
            bCatalogsLoaded = false;
        }
        if ( bCatalogsLoaded == false || pushInputLayers( *scope ) == false )
            return nullptr;
        // UI 는 클라이언트 전용 부분이 연다 — 전용 서버(창구가 없다)는 화면이 없다.
        IGameInstanceClientHook* const pClientHook = IGameInstanceClientHook::find();
        if ( pClientHook != nullptr && pClientHook->openExperienceUI( *scope ) == false )
            return nullptr;
        scope->finishActivation();
        SW_LOG_INFO_ID( "ExperienceManager.Activated", "Experience '%#' is active (%# kits)", definition._name.c_str(), static_cast<uint32>( definition._listKit.size() ) );
        return scope;
    }

    bool ExperienceManager::pushInputLayers( ExperienceScope& scope )
    {
        const ExperienceDefinition& definition = scope.getDefinition();
        if ( definition._listInputContext.empty() )
            return true;
        InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
        {
            SW_LOG_ERROR_ID( "ExperienceManager.InputContextNeedsInput", "Experience '%#' lists input contexts but the engine service InputManager is not bound to the game services",
                             definition._name.c_str() );
            return false;
        }
        InputMap& inputMap = pInput->getInputMap();
        for ( const hashed_string& layer : definition._listInputContext )
        {
            if ( inputMap.hasLayer( layer ) == false )
            {
                SW_LOG_ERROR_ID( "ExperienceManager.InputContextMissing", "Experience '%#': input context '%#' is not a layer of the game input map bundle", definition._name.c_str(), layer.c_str() );
                return false;
            }
            inputMap.pushLayer( layer );
            // 같은 입력 맵을 끌 때 찾는다 — 입력 서비스는 엔진 수명이라 Experience 보다 오래 산다.
            scope.addCleanup( Delegate<void()>::create( [pInput, layer]()
            { pInput->getInputMap().popLayer( layer ); } ) );
        }
        return true;
    }

    // 시나리오 · 개발용 입구 — 허브의 진입 자리는 `ExperienceTriggerComponent` 다.
    SW_DEV_COMMAND( ExperienceEnter, "experience.enter", "experience.enter <name>", "Enter an experience (<pack>/experiences/<name>.experience.xml); the hub's name returns to the hub",
                    &ExperienceManagerInternal::runEnterCommand );
    SW_DEV_COMMAND( ExperienceLeave, "experience.leave", "experience.leave", "Leave the active mini-game experience and return to the hub (restores the hub captured on a scene switch)",
                    &ExperienceManagerInternal::runLeaveCommand );
    SW_AUTOMATION_QUERY_WATCH( experienceActive, "Experience.Active", "Name of the active mini-game experience; none without one (also while it waits for its map, and with only the hub active)",
                               &ExperienceManagerInternal::readActive, false, false );
    SW_AUTOMATION_QUERY_WATCH( experienceHub, "Experience.Hub", "Name of the hub experience (GameSettings hubExperience); none in a game without a hub", &ExperienceManagerInternal::readHub,
                               false, false );
    SW_AUTOMATION_WATCH( experienceLeakCount, "Experience.LeakCount", "Bindings left behind by deactivated experience scopes in this game instance (leak check; 0 when clean)",
                         &ExperienceManagerInternal::readLeakCount );
    SW_AUTOMATION_WATCH( experienceHubRestoreCount, "Experience.HubRestoreCount", "Times the hub captured before a scene-switch mini-game was restored on return",
                         &ExperienceManagerInternal::readHubRestoreCount );
} // namespace sw
