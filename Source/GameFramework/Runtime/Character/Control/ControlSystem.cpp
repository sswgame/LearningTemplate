#include "pch.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/Prefab/PrefabCache.h"

#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct ControlSystemInternal
        {
            /** @brief 매니저에 붙는 이름입니다. */
            static const hashed_string& getFrameSystemKey()
            {
                static const hashed_string kKey{ "ControlSystem" };
                return kKey;
            }

            /** @brief @p pawn 을 쥘 AI 조종자를 세웁니다 — 폰의 프리팹이 있으면 그것(그 안의 AI 조종자), 없으면 기본 AI 조종자 하나. */
            static AIControllerComponent* createAIController( GameObjectManager& manager, const PawnComponent& pawn )
            {
                const string& prefabPath = pawn.getAIControllerPrefab();
                if ( prefabPath.empty() )
                {
                    GameObject* pObject = manager.createGameObject( hashed_string( "AIController" ) );
                    return pObject != nullptr ? pObject->addComponent<AIControllerComponent>() : nullptr;
                }
                AssetManager*          pAssetManager = game::findSubsystem<AssetManager>();
                GameObject*            pObject       = pAssetManager != nullptr ? pAssetManager->getAssetCache<PrefabCache>().spawn( &manager, prefabPath, "AIController" ) : nullptr;
                AIControllerComponent* pAI           = pObject != nullptr ? pObject->getComponent<AIControllerComponent>() : nullptr;
                if ( pAI == nullptr )
                {
                    const utf8* pPawnName = pawn.getOwner() != nullptr ? pawn.getOwner()->getName().c_str() : "?";
                    SW_LOG_WARNING_ID( "ControlSystem.PawnAutoPossessPrefab", "Pawn '%#' auto-possess prefab '%#' has no AI controller - the pawn stays unpossessed", pPawnName, prefabPath.c_str() );
                }
                return pAI;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ControlSystem::ControlSystem()
        : _history{}
        , _listAutoPossessPawn{}
        , _listQueuedPossess{}
        , _pInputOverride{ nullptr }
        , _pInputCaptureOverride{ nullptr }
        , _serverTickSource{}
        , _serverTickOffset{ 0 }
        , _tick{ 0 }
        , _bRecording{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ControlSystem& ControlSystem::findOrCreate( GameObjectManager& manager )
    {
        ControlSystem* pSystem = find( manager );
        if ( pSystem != nullptr )
            return *pSystem;
        unique_ptr<ControlSystem> pNew = make_unique<ControlSystem>();
        pSystem                        = pNew.get();
        const bool bAdded              = manager.addFrameSystem( ControlSystemInternal::getFrameSystemKey(), std::move( pNew ) );
        SW_ASSERT( bAdded ); // 같은 키가 없음을 바로 위에서 보았다. 거절되면 접근 선언과 붙인 순서가 어긋난 것이고(오류 로그), 시스템은 이미 지워졌다
        (void)bAdded;
        return *pSystem;
    }

    ControlSystem* ControlSystem::find( const GameObjectManager& manager )
    {
        return static_cast<ControlSystem*>( manager.findFrameSystem( ControlSystemInternal::getFrameSystemKey() ) );
    }

    void ControlSystem::releaseIfUnused( GameObjectManager& manager )
    {
        const ComponentRegistry& registry = manager.getComponentRegistry();
        if ( registry.getAll<PawnComponent>().empty() && registry.getAll<ControllerComponent>().empty() )
            manager.removeFrameSystem( ControlSystemInternal::getFrameSystemKey() );
    }

    PlayerControllerComponent* ControlSystem::findOrCreatePlayerController( GameObjectManager& manager, uint32 playerIndex )
    {
        for ( PlayerControllerComponent* pPlayer : manager.getComponentRegistry().getAll<PlayerControllerComponent>() )
        {
            if ( pPlayer != nullptr && pPlayer->getPlayerIndex() == playerIndex )
                return pPlayer;
        }
        // 언리얼 GameMode 가 플레이어마다 PlayerController 를 세우는 것과 같다 — 씬에 없으면 만든다.
        GameObject*                pObject = manager.createGameObject( hashed_string( "PlayerController" ) );
        PlayerControllerComponent* pPlayer = pObject != nullptr ? pObject->addComponent<PlayerControllerComponent>() : nullptr;
        if ( pPlayer != nullptr )
            pPlayer->setPlayerIndex( playerIndex );
        return pPlayer;
    }

    bool ControlSystem::isPawnControlledBy( const GameObjectManager& manager, const PawnComponent& pawn, GameObjectHandle controllerObject )
    {
        const GameObject*          pObject     = manager.resolveGameObject( controllerObject );
        const ControllerComponent* pController = pObject != nullptr ? manager.resolveComponent( pawn.getController() ) : nullptr;
        return pController != nullptr && pController->getOwner() == pObject;
    }

    bool ControlSystem::returnPawnToPlayer( GameObjectManager& manager, PawnComponent& pawn, GameObjectHandle autoPlayObject )
    {
        if ( pawn.isPossessed() && isPawnControlledBy( manager, pawn, autoPlayObject ) == false )
            return false;
        PlayerControllerComponent* pPlayer = findOrCreatePlayerController( manager, 0 );
        if ( pPlayer != nullptr && pPlayer->getPawn() != pawn.getHandle() )
            pPlayer->possess( pawn );
        return true;
    }

    InputManager* ControlSystem::findInputManager() const
    {
        return _pInputOverride != nullptr ? _pInputOverride : game::findSubsystem<InputManager>();
    }

    const IInputCaptureQuery* ControlSystem::findInputCapture() const
    {
        return _pInputCaptureOverride != nullptr ? _pInputCaptureOverride : game::findSubsystem<IInputCaptureQuery>();
    }

    void ControlSystem::setRecording( bool bRecording, int32 capacityTicks )
    {
        if ( bRecording && _bRecording == SW_FALSE )
            _history.initialize( capacityTicks );
        _bRecording = bRecording ? SW_TRUE : SW_FALSE;
    }

    void ControlSystem::queuePossess( const TComponentHandle<ControllerComponent>& controller, const TComponentHandle<PawnComponent>& pawn )
    {
        QueuedPossess& queued = _listQueuedPossess.emplace_back();
        queued._controller    = controller;
        queued._pawn          = pawn;
    }

    SceneFrameAccess ControlSystem::getFrameAccess() const
    {
        SceneFrameAccess access{};
        access._writes        = { SceneDataKind::ControlIntent, SceneDataKind::Navigation, SceneDataKind::Structure };
        access._reads         = { SceneDataKind::ControlIntent };
        access._readsPrevious = { SceneDataKind::Navigation, SceneDataKind::Structure, SceneDataKind::Transform };
        // 감사(레인 불안전): 조종자를 만들고(`createGameObject` — 게임 스레드 전용), 입력 · UI 싱글턴을 읽고, 조종자의 `produceIntent`(AI · 게임 코드)를 부른다. 게임 스레드 시스템(`ISceneMainFrameSystem`)이다(레인 시스템이 Structure 를 쓰면 등록이 거절된다).
        return access;
    }

    void ControlSystem::runMain( GameObjectManager& manager, float32 deltaTime )
    {
        applyQueuedPossess( manager );
        autoPossess( manager );

        ControlFrameContext context{};
        context._pInput        = findInputManager();
        context._pInputCapture = findInputCapture();
        context._deltaTime     = deltaTime;
        context._tick          = _tick;

        // 레지스트리의 창은 등록 · 해제가 일어나면 버려야 한다 — 조종자가 판단 중에 오브젝트를 만들 수 있으므로 자리 번호로 돌며 매번 크기를 다시 본다.
        const ComponentRegistry::View<PawnComponent> pawnView = manager.getComponentRegistry().getAll<PawnComponent>();
        for ( size_t pawnIndex = 0; pawnIndex < pawnView.size(); ++pawnIndex )
        {
            PawnComponent* pPawn = pawnView[pawnIndex];
            if ( pPawn != nullptr && pPawn->isPossessed() == false )
            {
                pPawn->clearMotion();
                pPawn->applyPendingControlRotation();
            }
        }
        const ComponentRegistry::View<ControllerComponent> controllerView = manager.getComponentRegistry().getAll<ControllerComponent>();
        for ( size_t controllerIndex = 0; controllerIndex < controllerView.size(); ++controllerIndex )
        {
            ControllerComponent* pController = controllerView[controllerIndex];
            PawnComponent*       pPawn       = pController != nullptr && pController->isActive() ? pController->findPawn() : nullptr;
            if ( pPawn == nullptr )
                continue;
            // 코드가 정한 시선(요청)이 먼저, 그 뒤에 쌓인 반동(오프셋) — 요청은 그 앞의 오프셋을 이미 버렸다.
            float2 requested{};
            if ( pPawn->consumeControlRotationRequest( requested ) )
                pController->setControlRotation( requested._x, requested._y );
            const float2  offset = pPawn->consumeControlRotationOffset();
            const float32 pitch  = MathUtil::clamp( pController->getControlPitch() + offset._y, -pPawn->getMaxPitch(), pPawn->getMaxPitch() );
            pController->setControlRotation( MathUtil::wrapAngle( pController->getControlYaw() + offset._x ), pitch );
            ControlIntent intent{};
            pController->produceIntent( context, *pPawn, intent );
            intent._controlYaw   = pController->getControlYaw();
            intent._controlPitch = pController->getControlPitch();
            intent.quantize();
            pPawn->applyIntent( intent );
        }
        if ( _bRecording == SW_TRUE )
            recordIntents( manager );
        // 틱은 시뮬레이션 시간이다 — 게임 시간이 멈춘 프레임(정지 · 시나리오 시작 전 붙잡기, deltaTime 0)에는 세지 않는다. 물리 · 캐릭터는 멈춰 있는데
        // 틱만 오르면 틱의 함수로 자리를 내는 바닥(`TickedPathMoverComponent`)이 혼자 앞서 가 위의 캐릭터가 가장자리에 내려앉았다(붙잡힌 프레임 수에 따라 갈렸다).
        // 언리얼 GameState 의 서버 월드 시각도 일시 정지 동안 흐르지 않는다.
        if ( deltaTime > 0.0f )
            ++_tick;
    }

    void ControlSystem::applyQueuedPossess( GameObjectManager& manager )
    {
        for ( size_t queuedIndex = 0; queuedIndex < _listQueuedPossess.size(); ++queuedIndex )
        {
            const QueuedPossess  queued      = _listQueuedPossess[queuedIndex];
            ControllerComponent* pController = manager.resolveComponent( queued._controller );
            if ( pController == nullptr )
                continue;
            PawnComponent* pPawn = queued._pawn.isValid() ? manager.resolveComponent( queued._pawn ) : nullptr;
            if ( pPawn != nullptr )
                pController->possess( *pPawn );
            else
                pController->unpossess();
        }
        _listQueuedPossess.clear();
    }

    void ControlSystem::recordIntents( GameObjectManager& manager )
    {
        for ( PawnComponent* pPawn : manager.getComponentRegistry().getAll<PawnComponent>() )
        {
            const GameObject* pOwner = pPawn != nullptr ? pPawn->getOwner() : nullptr;
            if ( pOwner != nullptr )
                _history.record( _tick, TComponentHandle<PawnComponent>{ pPawn }, pOwner->getName(), pPawn->getIntent() );
        }
    }

    void ControlSystem::autoPossess( GameObjectManager& manager )
    {
        // 플레이 전(에디터)에는 쥐지 않는다 — 시작한 뒤 첫 프레임에 쥔다.
        if ( manager.hasBegunPlay() == false )
            return;
        _listAutoPossessPawn.clear();
        for ( PawnComponent* pPawn : manager.getComponentRegistry().getAll<PawnComponent>() )
        {
            const bool bWantsController = pPawn != nullptr && pPawn->getAutoPossess() != PawnAutoPossess::None && pPawn->_bAutoPossessDone == SW_FALSE;
            if ( bWantsController == false )
                continue;
            pPawn->_bAutoPossessDone = SW_TRUE;
            if ( pPawn->isPossessed() == false )
                _listAutoPossessPawn.push_back( TComponentHandle<PawnComponent>{ pPawn } );
        }
        // 조종자를 세우면 레지스트리가 바뀐다 — 다 모은 뒤에 세운다.
        for ( const TComponentHandle<PawnComponent>& handle : _listAutoPossessPawn )
        {
            PawnComponent* pPawn = manager.resolveComponent( handle );
            if ( pPawn == nullptr || pPawn->isPossessed() )
                continue;
            ControllerComponent* pController = nullptr;
            if ( pPawn->getAutoPossess() == PawnAutoPossess::Player0 )
                pController = findOrCreatePlayerController( manager, 0 );
            else if ( pPawn->getAutoPossess() == PawnAutoPossess::AI )
                pController = ControlSystemInternal::createAIController( manager, *pPawn );
            if ( pController == nullptr )
                continue;
            pController->setSpawnedForPawn( pPawn->getAutoPossess() == PawnAutoPossess::AI );
            pController->possess( *pPawn );
        }
        _listAutoPossessPawn.clear();
    }
} // namespace sw
