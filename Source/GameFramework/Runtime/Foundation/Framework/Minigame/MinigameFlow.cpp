#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"

#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneSubLevelSubsystem.h"
#include "Engine/Serialization/Serializer/SerializePurpose.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstanceClientHook.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Autosave.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/SaveLock.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"

#include <type_traits>

namespace sw
{
    /**
     * @brief `"-gv_firstMinigame=<이름>"` 인자입니다. 첫 씬(`gv_firstScene` > 타이틀 > 시작 맵) 대신 이 미니게임(`<팩>/minigames/<이름>.minigame.xml`)으로 들어갑니다.
     * @details 언리얼 Lyra 의 URL 옵션 `?Experience=` 자리다. 전역 변수라 시나리오 `<Launch>` 가 그대로 쓰고 체인 단계마다 되돌려진다(따로 둔 실행 인자는 단계마다 바꿀 길이 없었다).
     *          `gv_firstScene` 처럼 배포에도 있다 — 배포본의 시나리오 · 전용 서버도 미니게임으로 바로 들어간다. 돌아올 씬이 없으므로 나가면 입구 씬으로 간다.
     */
    SW_GLOBAL_VARIABLE( sw::GlobalString, gv_firstMinigame, "", "첫 씬 대신 처음 들어갈 미니게임 이름 (비우면 사용 안 함)" );

    namespace
    {
        struct MinigameFlowInternal
        {
            /** @brief 활성 씬(오브젝트가 선 것)입니다. 없으면 nullptr 입니다. */
            static Scene* findActivePlayScene()
            {
                const SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                Scene* const              pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
                return ( pScene != nullptr && pScene->getObjectManager() != nullptr ) ? pScene : nullptr;
            }

            /** @brief 넷 세션이 열렸으면 true 입니다(엔진 서비스가 없는 범위는 혼자 하는 게임으로 본다). */
            static bool isNetSessionActive()
            {
                const GameNetSession* const pNetSession = game::findSubsystem<GameNetSession>();
                return pNetSession != nullptr && pNetSession->isActive();
            }

            /** @brief 활성 씬의 사물 @p objectName 의 변환을 하위 레벨 자리로 만듭니다. 없으면 nullopt 입니다. */
            static optional<SubLevelPlacement> findAnchorPlacement( string_view objectName )
            {
                Scene* const      pScene  = findActivePlayScene();
                GameObject* const pAnchor = pScene != nullptr ? pScene->getObjectManager()->findGameObjectByName( hashed_string( objectName ) ) : nullptr;
                if ( pAnchor == nullptr )
                    return nullopt;
                return MinigameFlow::makeAnchorPlacement( *pAnchor );
            }

            /** @brief `minigame.enter <이름> [얹을 사물]` 의 몸통입니다. 맡기기만 하고 켜졌는지는 로그 · 감시 값으로 본다(맵 로드를 기다린다). */
            static bool runEnterCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() || listArgument.size() > 2 )
                    return false;
                MinigameFlow* const                      pFlow = game::findSubsystem<MinigameFlow>();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( pFlow == nullptr )
                    replyBuilder.append( "minigame.enter: no game is running" );
                else
                {
                    optional<SubLevelPlacement> placement = nullopt;
                    if ( listArgument.size() == 2 )
                        placement = findAnchorPlacement( listArgument[1] );
                    replyBuilder.appendFormat( "minigame.enter: %# '%#'", pFlow->enter( listArgument[0], placement ) ? "entering" : "refused", listArgument[0].c_str() );
                }
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `minigame.switch <이름>` 의 몸통입니다 — 맨 위 미니게임을 바꾼다(돌아올 자리는 그대로). */
            static bool runSwitchCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                MinigameFlow* const                      pFlow = game::findSubsystem<MinigameFlow>();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( pFlow == nullptr )
                    replyBuilder.append( "minigame.switch: no game is running" );
                else
                    replyBuilder.appendFormat( "minigame.switch: %# '%#'", pFlow->switchTo( listArgument[0] ) ? "switching" : "refused", listArgument[0].c_str() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `minigame.leave [결과]` 의 몸통입니다 — 맨 위 미니게임을 끄고 돌아올 자리로 간다(돌아왔는지는 감시 값 `Minigame.Active` · `Minigame.RestoreCount` · `Minigame.LastResult`). */
            static bool runLeaveCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    outReply = "minigame.leave: no game is running";
                else
                    outReply = pFlow->leave( listArgument.empty() ? hashed_string{} : hashed_string( listArgument[0] ) ) ? "minigame.leave: leaving" : "minigame.leave: refused";
                return true;
            }

            /** @brief 감시 값 `Minigame.Active` 입니다. 켜진 미니게임 이름이고, 없으면 `none` 입니다(맵을 기다리는 중도 `none`). */
            [[nodiscard]] static bool readActive( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                {
                    outValue.setText( "no game is running" );
                    return false;
                }
                const MinigameDefinition* const pActive = pFlow->isEntering() ? nullptr : pFlow->findActiveDefinition();
                outValue.setText( pActive != nullptr ? pActive->_name.c_str() : "none" );
                return true;
            }

            /** @brief 감시 값 `Minigame.LastResult` 입니다. 마지막으로 넘긴 결과 이름이고, 없으면 `none` 입니다(결과 없이 나왔으면 빈 글). */
            [[nodiscard]] static bool readLastResult( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                {
                    outValue.setText( "no game is running" );
                    return false;
                }
                const MinigameResult& result = pFlow->getLastResult();
                outValue.setText( result._minigame.empty() ? "none" : result._resultName.c_str() );
                return true;
            }

            /** @brief 감시 값 `Minigame.LeakCount` 입니다 — 이 인스턴스에서 끈 미니게임 범위가 남긴 것의 합(0 이 아니면 `GameFeatureScope.Leaked*` 오류도 남았다). */
            [[nodiscard]] static bool readLeakCount( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getLeakCount() );
                return true;
            }

            /** @brief 감시 값 `Minigame.RestoreCount` 입니다 — 씬 전환 미니게임에서 나와 돌아올 자리를 복원한 수(나가기가 끝났는지 기다리는 값). */
            [[nodiscard]] static bool readRestoreCount( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getRestoreCount() );
                return true;
            }

            /** @brief 감시 값 `Minigame.LeaveAssetCacheBytes` 입니다 — 마지막으로 나온 순간의 에셋 캐시 바이트 추정 합(되풀이해 느는지 본다). */
            [[nodiscard]] static bool readLeaveAssetCacheBytes( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getLeaveAssetCacheBytes() );
                return true;
            }

            /** @brief 감시 값 `Minigame.LeaveHeapBytes` 입니다 — 마지막으로 나온 순간의 힙 바이트(되풀이해 느는지 본다). */
            [[nodiscard]] static bool readLeaveHeapBytes( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getLeaveHeapBytes() );
                return true;
            }

            /** @brief 감시 값 `Minigame.Depth` 입니다 — 쌓인 칸 수(켜진 미니게임 + 덮인 미니게임). */
            [[nodiscard]] static bool readDepth( const GameObjectManager* pObjects, float64& outValue )
            {
                (void)pObjects;
                const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
                if ( pFlow == nullptr )
                    return false;
                outValue = static_cast<float64>( pFlow->getStack().getDepth() );
                return true;
            }
        };
    } // namespace

    MinigameFlow::MinigameFlow( SubsystemScope& gameScope, const GamePackConfig& packConfig, GameInstance& owner )
        : _gameScope( gameScope )
        , _packConfig( packConfig )
        , _owner( owner )
        , _stack{}
        , _pActive{ nullptr }
        , _pReturning{ nullptr }
        , _pendingEnter{}
        , _pendingPlacement{}
        , _pendingLeave{}
        , _onFinished{}
        , _lastResult{}
        , _sceneLoadRequested{}
        , _leakCount{ 0 }
        , _restoreCount{ 0 }
        , _leaveAssetCacheBytes{ 0 }
        , _leaveHeapBytes{ 0 }
        , _bEntering{ SW_FALSE }
        , _bRequestingScene{ SW_FALSE }
    {
    }

    MinigameFlow::~MinigameFlow() = default;

    SubsystemInitResult MinigameFlow::initializeSubsystem()
    {
        // 누가 요청했든 게임 이동을 듣는다 — 세이브 불러오기 · 타이틀로 · 시나리오의 씬 열기가 칸 스택을 낡게 만든다(씬 흐름 `GameSceneFlow` 와 같은 알림).
        SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager != nullptr )
            _sceneLoadRequested = pSceneManager->onSceneLoadRequested().add( SW_DELEGATE_METHOD( SceneLoadRequestedHandler, &MinigameFlow::onSceneLoadRequested, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void MinigameFlow::shutdownSubsystem()
    {
        _sceneLoadRequested.reset();
        _pendingEnter.clear();
        _pendingPlacement = nullopt;
        _pendingLeave     = nullopt;
        _pReturning.reset();
        deactivateScope(); // 게임 범위 기능은 게임 인스턴스가 이 뒤에 끈다(미니게임의 해제 검사는 그것이 켜진 채로 찍었다)
        _stack.clear();
    }

    bool MinigameFlow::enterFirstMinigame()
    {
        const string name = gv_firstMinigame.get();
        if ( name.empty() )
            return false;
        SW_LOG_INFO_ID( "MinigameFlow.FirstMinigame", "First minigame %# (gv_firstMinigame)", name.c_str() );
        (void)enter( name ); // 실패는 위에서 알렸다 — 다른 씬으로 대신하지 않는다
        return true;
    }

    string MinigameFlow::makeAssetPath( const GamePackConfig& packConfig, string_view name )
    {
        StringBuilder<constant::buffer::kMax256> relativeBuilder;
        relativeBuilder.appendFormat( "%#%#%#", kMinigameFolder, name, kMinigameSuffix );
        return packConfig.resolve( relativeBuilder.view() );
    }

    optional<SubLevelPlacement> MinigameFlow::makeAnchorPlacement( const GameObject& anchor )
    {
        const SceneComponent* const pRoot = anchor.getPrimarySceneComponent();
        if ( pRoot == nullptr )
            return nullopt;
        // shortcut: 회전은 주 장면 컴포넌트의 로컬 오일러다 — 부모 아래에 놓인 입구가 생기면 월드 회전(쿼터니언 → 오일러)으로 바꾼다.
        return SubLevelPlacement{ pRoot->getWorldPosition(), pRoot->getLocalRotation() };
    }

    bool MinigameFlow::validateEntry( const MinigameDefinition& definition, bool bNetSessionActive )
    {
        if ( definition._entry != MinigameEntry::SceneSwitch || bNetSessionActive == false )
            return true;
        // 세션의 피어가 모두 같은 씬을 돌아야 한다 — 한 피어만 씬을 바꾸면 복제가 끊긴다. 세션 이동(서버 이동)은 돌아올 자리 형식에 갈래를 더할 자리다.
        SW_LOG_ERROR_ID( "MinigameFlow.SceneSwitchInNetSession", "Minigame '%#' (entry=\"SceneSwitch\") is refused - a net session is open and a scene switch would split the session",
                         definition._name.c_str() );
        return false;
    }

    bool MinigameFlow::loadDefinition( string_view name, MinigameDefinition& outDefinition ) const
    {
        const string assetPath = makeAssetPath( _packConfig, name );
        if ( MinigameDefinition::loadFromAsset( AssetID{ assetPath }, outDefinition ) == false )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.NotLoaded", "Minigame '%#' (%#) could not be read (see the messages above)", name, assetPath.c_str() );
            return false;
        }
        if ( outDefinition._name != hashed_string( name ) )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.NameMismatch", "%#: name=\"%#\" differs from the file name '%#'", assetPath.c_str(), outDefinition._name.c_str(), name );
            return false;
        }
        // 등록부는 GameFramework 의 모듈 서브시스템이다 — 없으면 호스트가 모듈을 올리지 않은 것이라 멈춘다.
        return outDefinition.validate( game::getSubsystem<GameFeatureRegistry>(), assetPath );
    }

    bool MinigameFlow::validateDepth( string_view name ) const
    {
        // 정책 값은 엔진 설정이다 — 없으면(0) 기본값으로 메우지 않고 진입을 거절한다.
        const uint32 depthLimit = game::getSubsystem<const MinigameConfig>()._stackDepthLimit;
        if ( depthLimit == 0 )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.DepthLimitMissing", "Minigame '%#' is refused - EngineConfig _minigame._stackDepthLimit must be > 0 (Config/Engine/EngineConfig.json)", name );
            return false;
        }
        if ( _stack.canPush( depthLimit ) )
            return true;
        SW_LOG_ERROR_ID( "MinigameFlow.DepthLimitReached", "Minigame '%#' is refused - %# minigames are stacked (limit %#)", name, _stack.getDepth(), depthLimit );
        return false;
    }

    bool MinigameFlow::rejectIfLoadPending( string_view name ) const
    {
        if ( isEntering() == false && isLeaving() == false )
            return false;
        SW_LOG_ERROR_ID( "MinigameFlow.Busy", "Minigame request '%#' is refused - %# is still waiting for its map", name, isEntering() ? "an entry" : "leaving the minigame" );
        return true;
    }

    bool MinigameFlow::requestScene( const string& scenePath, const Delegate<void( Scene* pScene )>& onLoaded )
    {
        // 받아들임 알림은 요청 안에서 동기로 온다 — 그동안만 이 흐름의 요청으로 표시해 장치 밖 이동으로 보지 않게 한다.
        _bRequestingScene   = SW_TRUE;
        const bool bRequest = game::getSubsystem<GameSceneFlow>().requestSceneThen( scenePath, onLoaded );
        _bRequestingScene   = SW_FALSE;
        return bRequest;
    }

    bool MinigameFlow::makeSceneReturn( string_view name, MinigameReturnPoint& outReturnPoint )
    {
        Scene* const pScene = MinigameFlowInternal::findActivePlayScene();
        if ( pScene == nullptr )
        {
            outReturnPoint = MinigameEntranceReturn{}; // 첫 진입이 미니게임이다 — 나가면 입구 씬으로 간다
            return true;
        }
        if ( pScene->getSourcePath().empty() )
        {
            SW_LOG_WARNING_ID( "MinigameFlow.SceneNotSuspendable", "Minigame '%#': the active scene has no source path to reopen - leaving goes to the entrance scene", name );
            outReturnPoint = MinigameEntranceReturn{};
            return true;
        }
        // 들어가기 직전에 저장을 한 번 맡긴다 — 미니게임 동안은 막히므로 바깥 진행의 마지막 좋은 저장이 여기 남는다. 정책이 없는 게임은 자동 저장이 없다.
        if ( AutosaveManager* const pAutosave = game::findSubsystem<AutosaveManager>() )
            (void)pAutosave->saveNow( AutosaveTrigger::AreaChanged, hashed_string( name ) ); // 맡기지 못했으면(쓰는 중 · 막힘 — 쌓인 진입) 마지막 저장이 그대로 남는다
        MinigameSuspendedScene suspended;
        suspended._scenePath = pScene->getSourcePath();
        if ( _owner.captureSnapshot( SerializePurpose::Snapshot, suspended._snapshot ) == false )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.SceneNotCaptured", "Minigame '%#': the active scene could not be captured before switching scenes - nothing is entered", name );
            return false;
        }
        // 칸이 있는 동안 저장을 막는다 — 세이브가 미니게임 맵을 바깥 씬으로 적지 않게(컷씬 막기와 같은 길, 칸이 사라지면 풀린다).
        suspended._saveBlock = _owner.getSaveLock().block( hashed_string( kSaveBlockReason ) );
        outReturnPoint       = std::move( suspended );
        return true;
    }

    bool MinigameFlow::enter( string_view name, const optional<SubLevelPlacement>& placement )
    {
        if ( rejectIfLoadPending( name ) )
            return false;
        // 맨 위만 본다 — 같은 미니게임 위에 같은 것을 쌓는 것은 실수다(아래 칸의 같은 이름은 다른 판으로 친다).
        const MinigameFrame* const pTop = _stack.findTop();
        if ( pTop != nullptr && pTop->_definition._name == hashed_string( name ) )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.AlreadyActive", "Minigame '%#' is already active - leave it first", name );
            return false;
        }
        MinigameDefinition definition;
        if ( loadDefinition( name, definition ) == false || validateEntry( definition, MinigameFlowInternal::isNetSessionActive() ) == false || validateDepth( name ) == false )
            return false;
        Scene* const pScene = MinigameFlowInternal::findActivePlayScene();
        if ( definition._entry == MinigameEntry::SubLevel && ( pScene == nullptr || placement.has_value() == false ) )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.SubLevelNeedsAnchor", "Minigame '%#' (entry=\"SubLevel\") is added at the transform of the object that enters it - %#", name,
                             pScene == nullptr ? "there is no active scene" : "no anchor object was given (MinigameTriggerComponent, or minigame.enter <name> <object>)" );
            return false;
        }
        // 진입은 원자적이다 — 검사가 끝난 뒤 덮을 미니게임의 범위만 끄고(같은 서비스를 둘이 걸 수 없다), 새 것이 지면 다시 켠다.
        // 맵의 컴포넌트가 씬 적용(`beginPlay`) 때 기능 서비스 · 카탈로그를 찾으므로 켜기는 맵 요청 앞이다.
        deactivateScope();
        MinigameReturnPoint returnPoint{ MinigameEntranceReturn{} };
        if ( definition._entry == MinigameEntry::SubLevel )
            returnPoint = MinigameSubLevelReturn{ pScene->getSourcePath(), definition._name };
        else if ( makeSceneReturn( name, returnPoint ) == false )
        {
            activateTop();
            return false;
        }
        unique_ptr<GameFeatureScope> pScope = activate( definition );
        if ( pScope == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.NotActivated", "Minigame '%#' could not be activated (see the messages above) - the covered minigame is active again", name );
            activateTop(); // 만든 돌아올 자리(저장 막기 포함)는 여기서 버려진다
            return false;
        }
        const string mapPath = definition._map.get();
        const bool   bSub    = definition._entry == MinigameEntry::SubLevel;
        _stack.push( MinigameFrame{ std::move( definition ), std::move( returnPoint ) } );
        _pActive            = std::move( pScope );
        const auto rollback = [this]()
        {
            deactivateScope();
            MinigameFrame dropped;
            (void)_stack.pop( dropped ); // 방금 쌓은 칸이다 — 그 돌아올 자리(저장 막기)도 버린다
            activateTop();
        };
        if ( bSub )
        {
            // 하위 레벨 이름은 미니게임 이름이다 — 나올 때 그 이름으로 뺀다. 자리는 진입을 건 사물의 변환이다(언리얼 레벨 인스턴스 액터).
            SceneSubLevelSubsystem& subLevels = pScene->getObjectManager()->getSubsystem<SceneSubLevelSubsystem>();
            if ( subLevels.addSubLevel( *pScene, name, mapPath, *placement ) == false )
            {
                SW_LOG_ERROR_ID( "MinigameFlow.SubLevelRefused", "Minigame '%#': the map '%#' could not be added to the active scene (see the messages above)", name, mapPath.c_str() );
                rollback();
                return false;
            }
            return true;
        }
        if ( requestScene( mapPath, GameSceneFlow::SceneLoadedFunction::create<&MinigameFlow::onMapLoaded>( this ) ) == false )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.MapRequestRefused", "Minigame '%#': the map '%#' could not be requested", name, mapPath.c_str() );
            rollback();
            return false;
        }
        _bEntering = SW_TRUE;
        return true;
    }

    bool MinigameFlow::switchTo( string_view name )
    {
        MinigameFrame* const pTop = _stack.findTop();
        if ( pTop == nullptr )
            return enter( name );
        if ( rejectIfLoadPending( name ) )
            return false;
        MinigameDefinition definition;
        if ( loadDefinition( name, definition ) == false || validateEntry( definition, MinigameFlowInternal::isNetSessionActive() ) == false )
            return false;
        // 교체는 씬 전환 미니게임끼리다 — 하위 레벨은 얹은 씬이 돌아올 자리라 바꾸면 바탕이 어긋난다(나간 뒤 들어간다).
        if ( definition._entry != MinigameEntry::SceneSwitch || std::holds_alternative<MinigameSubLevelReturn>( pTop->_returnPoint ) )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.SwitchNeedsSceneSwitch", "Minigame switch to '%#' is refused - only scene-switch minigames replace each other (leave the sub-level minigame, then enter)", name );
            return false;
        }
        if ( pTop->_definition._name == definition._name )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.AlreadyActive", "Minigame '%#' is already active - leave it first", name );
            return false;
        }
        deactivateScope();
        unique_ptr<GameFeatureScope> pScope = activate( definition );
        if ( pScope == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.NotActivated", "Minigame '%#' could not be activated (see the messages above) - the previous minigame is active again", name );
            activateTop();
            return false;
        }
        MinigameDefinition previous = std::move( pTop->_definition ); // 돌아올 자리는 그대로 둔다
        pTop->_definition           = std::move( definition );
        _pActive                    = std::move( pScope );
        const string mapPath        = pTop->_definition._map.get();
        if ( requestScene( mapPath, GameSceneFlow::SceneLoadedFunction::create<&MinigameFlow::onMapLoaded>( this ) ) == false )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.MapRequestRefused", "Minigame '%#': the map '%#' could not be requested", name, mapPath.c_str() );
            deactivateScope();
            pTop->_definition = std::move( previous );
            activateTop();
            return false;
        }
        SW_LOG_INFO_ID( "MinigameFlow.Switched", "Minigame '%#' replaces '%#' (the scene to return to is kept)", name, previous._name.c_str() );
        _bEntering = SW_TRUE;
        return true;
    }

    bool MinigameFlow::leave( const hashed_string& resultName, vector<MinigameResultValue> listValue )
    {
        if ( rejectIfLoadPending( "leave" ) )
            return false;
        MinigameFrame frame;
        if ( _stack.pop( frame ) == false )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.NothingToLeave", "There is no minigame to leave" );
            return false;
        }
        deactivateScope();
        MinigameResult result{ frame._definition._name, resultName, std::move( listValue ) };
        // 하위 레벨은 그 자리에서 끝난다 — 빼고, 덮였던 미니게임을 다시 켜고, 결과를 넘긴다.
        if ( const MinigameSubLevelReturn* const pSubLevel = std::get_if<MinigameSubLevelReturn>( &frame._returnPoint ) )
        {
            removeSubLevel( *pSubLevel );
            activateTop();
            deliverResult( result );
            return true;
        }
        // 씬으로 돌아간다 — 찍은 씬(씬 전환) 또는 입구 씬(돌아올 씬 없음). 갈래가 늘면 아래 visit 이 빠진 갈래를 컴파일 오류로 알린다.
        const string scenePath = std::visit(
            []( const auto& returnPoint ) -> string
        {
            using T = std::decay_t<decltype( returnPoint )>;
            if constexpr ( std::is_same_v<T, MinigameSuspendedScene> )
                return returnPoint._scenePath;
            else if constexpr ( std::is_same_v<T, MinigameEntranceReturn> )
                return game::getSubsystem<GameSceneFlow>().getEntranceScene();
            else
            {
                static_assert( std::is_same_v<T, MinigameSubLevelReturn>, "every return point kind has a way back" );
                return returnPoint._scenePath; // 위에서 처리했다
            }
        },
            frame._returnPoint );
        if ( scenePath.empty() || requestScene( scenePath, GameSceneFlow::SceneLoadedFunction::create<&MinigameFlow::onReturnLoaded>( this ) ) == false )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.ReturnRequestRefused", "Leaving minigame '%#' is refused - the scene to return to '%#' could not be requested (an entry with no scene to return to needs GameSettings entranceScene or startMap)",
                             result._minigame.c_str(), scenePath.c_str() );
            _stack.push( std::move( frame ) ); // 칸은 그대로다
            activateTop();
            return false;
        }
        // 덮였던 미니게임은 씬 복원 전에 켠다 — 돌아간 씬의 컴포넌트가 씬 적용(`beginPlay`) 때 그 서비스를 찾는다. 결과는 복원 뒤에 넘긴다.
        activateTop();
        _pReturning = make_unique<PendingReturn>( PendingReturn{ std::move( frame._returnPoint ), std::move( result ) } );
        return true;
    }

    void MinigameFlow::requestLeave( const hashed_string& resultName, vector<MinigameResultValue> listValue )
    {
        if ( _pendingLeave.has_value() )
        {
            SW_LOG_WARNING_ID( "MinigameFlow.LeaveAlreadyRequested", "Minigame leave with result '%#' is dropped - a leave with result '%#' was requested first in this frame",
                               resultName.c_str(), _pendingLeave->_resultName.c_str() );
            return;
        }
        _pendingLeave = MinigameResult{ hashed_string{}, resultName, std::move( listValue ) };
    }

    void MinigameFlow::requestEnter( string_view name, const optional<SubLevelPlacement>& placement )
    {
        if ( _pendingEnter.empty() == false )
        {
            SW_LOG_WARNING_ID( "MinigameFlow.EnterAlreadyRequested", "Minigame '%#' is dropped - '%#' was requested first in this frame", name, _pendingEnter.c_str() );
            return;
        }
        _pendingEnter     = string( name );
        _pendingPlacement = placement;
    }

    void MinigameFlow::update()
    {
        // 다시 읽힌 카탈로그를 먼저 건다 — 이 프레임의 진입 · 컴포넌트가 새 판을 찾는다.
        if ( _pActive != nullptr )
            _pActive->rebindReloadedCatalogs();
        // 나가기가 먼저다 — 같은 프레임에 끝난 미니게임의 트리거가 다음 진입을 맡겨도 끝낸 칸 위에 쌓지 않는다(나가기가 맵을 기다리면 진입은 `rejectIfLoadPending` 이 거절한다).
        if ( _pendingLeave.has_value() )
        {
            MinigameResult pending = std::move( *_pendingLeave );
            _pendingLeave          = nullopt;
            (void)leave( pending._resultName, std::move( pending._listValue ) ); // 실패는 leave 가 알렸다
        }
        if ( _pendingEnter.empty() )
            return;
        const string                      name      = std::move( _pendingEnter );
        const optional<SubLevelPlacement> placement = _pendingPlacement;
        _pendingEnter.clear();
        _pendingPlacement = nullopt;
        (void)enter( name, placement ); // 실패는 enter 가 알렸다
    }

    const MinigameDefinition* MinigameFlow::findActiveDefinition() const
    {
        const MinigameFrame* const pTop = _stack.findTop();
        return ( _pActive != nullptr && pTop != nullptr ) ? &pTop->_definition : nullptr;
    }

    void MinigameFlow::activateTop()
    {
        const MinigameFrame* const pTop = _stack.findTop();
        if ( pTop == nullptr || _pActive != nullptr )
            return;
        // 같은 정의는 덮이기 전까지 켜져 있었다 — 다시 켜지 않으면(그 사이 기능 모듈이 내려갔다) 칸은 남기고 켜진 미니게임 없이 둔다(나가기는 그대로 된다).
        _pActive = activate( pTop->_definition );
        if ( _pActive == nullptr )
            SW_LOG_ERROR_ID( "MinigameFlow.CoveredNotActivated", "Minigame '%#' could not be activated again (see the messages above) - it stays stacked without its features",
                             pTop->_definition._name.c_str() );
    }

    void MinigameFlow::deactivateScope()
    {
        _bEntering = SW_FALSE; // 기다리던 맵의 완료 알림이 와도 할 일이 없다
        if ( _pActive == nullptr )
            return;
        const uint32 leakCount = _pActive->deactivate(); // 범위가 건 역순으로 거두고 남은 것을 알린다
        _leakCount += leakCount;
        SW_LOG_INFO_ID( "MinigameFlow.Deactivated", "Minigame '%#' is deactivated (%# left behind)", _pActive->getName().c_str(), leakCount );
        _pActive.reset();
    }

    void MinigameFlow::removeSubLevel( const MinigameSubLevelReturn& returnPoint )
    {
        // 하위 레벨은 바탕 씬이 아직 활성일 때만 뺀다 — 씬이 바뀌었으면 씬 끝내기가 이미 버렸다.
        Scene* const pScene = MinigameFlowInternal::findActivePlayScene();
        if ( pScene == nullptr || pScene->getSourcePath() != returnPoint._scenePath )
            return;
        SceneSubLevelSubsystem& subLevels = pScene->getObjectManager()->getSubsystem<SceneSubLevelSubsystem>();
        SubLevelState           state{ SubLevelState::Queued };
        if ( subLevels.findSubLevelState( returnPoint._subLevelName.view(), state ) )
            (void)subLevels.removeSubLevel( returnPoint._subLevelName.view() ); // 방금 찾은 이름이다
    }

    void MinigameFlow::sampleLeaveMemory()
    {
        // 나올 때마다 한 번 — 캐시마다 에셋 줄을 모아 더한다(프레임 길이 아니다).
        uint64              cacheBytes    = 0;
        const AssetManager* pAssetManager = game::findSubsystem<AssetManager>();
        if ( pAssetManager != nullptr )
        {
            vector<AssetMemoryEntry> listEntry;
            for ( const AssetCacheBase* pCache : pAssetManager->getAllAssetCache() )
            {
                pCache->collectMemoryEntries( listEntry );
            }
            for ( const AssetMemoryEntry& entry : listEntry )
            {
                cacheBytes += entry._cpuBytes + entry._gpuBytes;
            }
        }
        _leaveAssetCacheBytes           = cacheBytes;
        const MemoryProfiler* pProfiler = MemoryProfiler::getActive();
        _leaveHeapBytes                 = pProfiler != nullptr ? pProfiler->getLiveAllocatedBytes() : MemoryProfiler::getPlatformHeapBytes();
    }

    void MinigameFlow::deliverResult( const MinigameResult& result )
    {
        sampleLeaveMemory(); // 끈 범위 · 뺀 하위 레벨 · 돌아간 씬이 선 뒤다 — 모든 나오기 길이 여기를 지난다
        _lastResult = result;
        SW_LOG_INFO_ID( "MinigameFlow.Finished", "Minigame '%#' finished (result '%#')", result._minigame.c_str(), result._resultName.c_str() );
        _onFinished.broadcast( result );
    }

    void MinigameFlow::onSceneLoadRequested( const string& scenePath, SceneLoadKind kind, const TaskFuture<Scene*>& future )
    {
        (void)future;
        if ( kind != SceneLoadKind::Travel || _bRequestingScene == SW_TRUE )
            return; // 도구가 연 씬이거나 이 흐름의 요청이다
        if ( _stack.isEmpty() && isLeaving() == false )
            return;
        clearForOutsideTravel( scenePath );
    }

    void MinigameFlow::clearForOutsideTravel( const string& scenePath )
    {
        SW_LOG_INFO_ID( "MinigameFlow.ClearedByTravel", "Travel to '%#' was requested outside the minigame flow - the stacked minigames and the scenes to return to are dropped",
                        scenePath.c_str() );
        _pReturning.reset();     // 저장 막기도 풀린다 — 새 씬(세이브 · 타이틀)이 바깥 진행이다
        _pendingLeave = nullopt; // 맡긴 나가기의 칸은 방금 버렸다
        deactivateScope();
        _stack.clear();
    }

    void MinigameFlow::onMapLoaded( Scene* pScene )
    {
        if ( isEntering() == false || _pActive == nullptr )
            return;
        _bEntering = SW_FALSE;
        if ( pScene != nullptr )
            return;
        const MinigameFrame* const pTop = _stack.findTop();
        SW_LOG_ERROR_ID( "MinigameFlow.MapNotOpened", "Minigame '%#': the map '%#' did not open (failed or replaced by a later request) - the minigame is left without restoring",
                         pTop->_definition._name.c_str(), pTop->_definition._map.c_str() );
        deactivateScope();
        MinigameFrame dropped;
        (void)_stack.pop( dropped ); // 바탕 씬이 그대로 섰다 — 덮였던 미니게임을 다시 켠다
        activateTop();
    }

    void MinigameFlow::onReturnLoaded( Scene* pScene )
    {
        if ( isLeaving() == false )
            return;
        const unique_ptr<PendingReturn> pReturn = std::move( _pReturning ); // 끝나면 저장 막기가 풀린다
        if ( pScene == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.ReturnSceneNotOpened", "The scene to return to did not open (failed or replaced by a later request) - the captured scene and the result of '%#' are dropped",
                             pReturn->_result._minigame.c_str() );
            return;
        }
        // 세이브 불러오기와 같은 길이다 — 찍은 씬을 연 뒤 같은 목적의 봉투를 읽는다(`GameInstance::onSaveSceneLoaded`). 입구 씬은 새로 연 그대로다.
        if ( const MinigameSuspendedScene* const pSuspended = std::get_if<MinigameSuspendedScene>( &pReturn->_returnPoint ) )
        {
            if ( _owner.restoreSnapshot( SerializePurpose::Snapshot, pSuspended->_snapshot ) == false )
            {
                SW_LOG_ERROR_ID( "MinigameFlow.SceneNotRestored", "The captured scene could not be restored (see the messages above) - it stays as it opened" );
                deliverResult( pReturn->_result );
                return;
            }
            ++_restoreCount;
            SW_LOG_INFO_ID( "MinigameFlow.Restored", "Returned to '%#' - the captured scene is restored", pSuspended->_scenePath.c_str() );
        }
        // 복원이 파생 인스턴스 상태까지 되감았다 — 미니게임에서 번 것은 그 위에 얹는다.
        deliverResult( pReturn->_result );
    }

    unique_ptr<GameFeatureScope> MinigameFlow::activate( const MinigameDefinition& definition )
    {
        InputManager* const          pInput = game::findSubsystem<InputManager>(); // 입력 서비스가 없는 호스트(전용 서버)는 입력 레이어를 걸지 않는다
        unique_ptr<GameFeatureScope> scope  = make_unique<GameFeatureScope>( _gameScope, game::getSubsystem<GlobalVariableManager>(), _packConfig, pInput != nullptr ? &pInput->getInputMap() : nullptr,
                                                                             definition._name, GameFeatureScope::CatalogRule::Declared, definition._listCatalog );
        if ( scope->activateFeatures( definition._listFeature ) == false )
            return nullptr;
        // 적은 카탈로그를 아무 기능도 읽지 않았으면 데이터와 기능이 어긋났다 — 적지 않은 것을 읽는 쪽은 `GameFeatureScope::loadCatalog` 가 막았다.
        if ( const hashed_string* const pUnloaded = scope->findUnloadedCatalog() )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.CatalogNotLoaded", "Minigame '%#': catalog '%#' is declared but no feature in features=\"...\" loaded it", definition._name.c_str(), pUnloaded->c_str() );
            return nullptr;
        }
        if ( pushInputLayers( *scope, definition ) == false )
            return nullptr;
        // UI 는 클라이언트 전용 부분이 연다 — 전용 서버(창구가 없다)는 화면이 없다.
        IGameInstanceClientHook* const pClientHook = IGameInstanceClientHook::find();
        if ( pClientHook != nullptr && pClientHook->openMinigameUI( *scope, definition ) == false )
            return nullptr;
        scope->finishActivation();
        SW_LOG_INFO_ID( "MinigameFlow.Activated", "Minigame '%#' is active (%# features)", definition._name.c_str(), static_cast<uint32>( definition._listFeature.size() ) );
        return scope;
    }

    bool MinigameFlow::pushInputLayers( GameFeatureScope& scope, const MinigameDefinition& definition )
    {
        if ( definition._listInputContext.empty() )
            return true;
        InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameFlow.InputContextNeedsInput", "Minigame '%#' lists input contexts but the engine service InputManager is not bound to the game services", definition._name.c_str() );
            return false;
        }
        InputMap& inputMap = pInput->getInputMap();
        for ( const hashed_string& layer : definition._listInputContext )
        {
            if ( inputMap.hasLayer( layer ) == false )
            {
                SW_LOG_ERROR_ID( "MinigameFlow.InputContextMissing", "Minigame '%#': input context '%#' is not a layer of the game input map bundle", definition._name.c_str(), layer.c_str() );
                return false;
            }
            inputMap.pushLayer( layer );
            // 같은 입력 맵을 끌 때 찾는다 — 입력 서비스는 엔진 수명이라 미니게임보다 오래 산다.
            scope.addCleanup( Delegate<void()>::create( [pInput, layer]()
            { pInput->getInputMap().popLayer( layer ); } ) );
        }
        return true;
    }

    // 시나리오 · 개발용 입구 — 맵의 진입 자리는 `MinigameTriggerComponent` 다.
    SW_DEV_COMMAND( MinigameEnter, "minigame.enter", "minigame.enter <name> [anchor object]",
                    "Stack a minigame (<pack>/minigames/<name>.minigame.xml); a sub-level minigame is placed at the anchor object's transform", &MinigameFlowInternal::runEnterCommand );
    SW_DEV_COMMAND( MinigameSwitch, "minigame.switch", "minigame.switch <name>", "Replace the top scene-switch minigame and keep its scene to return to", &MinigameFlowInternal::runSwitchCommand );
    SW_DEV_COMMAND( MinigameLeave, "minigame.leave", "minigame.leave [result]",
                    "Leave the top minigame and return to the scene it was entered from (restored from the capture on a scene switch; the entrance scene without one), then hand over the result",
                    &MinigameFlowInternal::runLeaveCommand );
    SW_AUTOMATION_QUERY_WATCH( minigameActive, "Minigame.Active", "Name of the active (top) minigame; none without one (also while it waits for its map)", &MinigameFlowInternal::readActive, false,
                               false );
    SW_AUTOMATION_QUERY_WATCH( minigameLastResult, "Minigame.LastResult", "Result handed over by the last finished minigame (after its scene was restored); none before one finished",
                               &MinigameFlowInternal::readLastResult, false, false );
    SW_AUTOMATION_WATCH( minigameLeakCount, "Minigame.LeakCount", "Bindings left behind by deactivated minigame scopes in this game instance (leak check; 0 when clean)",
                         &MinigameFlowInternal::readLeakCount );
    SW_AUTOMATION_WATCH( minigameRestoreCount, "Minigame.RestoreCount", "Times the scene captured before a scene-switch minigame was restored on leaving", &MinigameFlowInternal::readRestoreCount );
    SW_AUTOMATION_WATCH( minigameDepth, "Minigame.Depth", "Stacked minigames (the active one and the ones it covers)", &MinigameFlowInternal::readDepth );
    SW_AUTOMATION_WATCH( minigameLeaveAssetCacheBytes, "Minigame.LeaveAssetCacheBytes",
                         "Estimated bytes held by the asset caches when the last minigame was left (repeat enter and leave to see whether it grows)", &MinigameFlowInternal::readLeaveAssetCacheBytes );
    SW_AUTOMATION_WATCH( minigameLeaveHeapBytes, "Minigame.LeaveHeapBytes",
                         "Heap bytes when the last minigame was left: live sw allocator bytes with the memory profiler, else the platform heap (0 where it cannot be measured)",
                         &MinigameFlowInternal::readLeaveHeapBytes );
} // namespace sw
