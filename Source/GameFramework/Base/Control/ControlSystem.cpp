#include "pch.h"

#include "GameFramework/Base/Control/ControlSystem.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"

#include "GameFramework/Base/Control/AiControllerComponent.h"
#include "GameFramework/Base/Control/ControlAutomationSteps.h"
#include "GameFramework/Base/Control/ControlIntent.h"
#include "GameFramework/Base/Control/PawnComponent.h"
#include "GameFramework/Base/Control/PlayerControllerComponent.h"
#include "GameFramework/Base/Framework/GameService.h"
#include "GameFramework/Base/Utility/OrientationUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ControlSystem" );

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
            static AiControllerComponent* createAiController( GameObjectManager& manager, const PawnComponent& pawn )
            {
                const string& prefabPath = pawn.getAiControllerPrefab();
                if ( prefabPath.empty() )
                {
                    GameObject* pObject = manager.createGameObject( hashed_string( "AiController" ) );
                    return pObject != nullptr ? pObject->addComponent<AiControllerComponent>() : nullptr;
                }
                AssetManager*          pAssetManager = game::getService<AssetManager>();
                GameObject*            pObject       = pAssetManager != nullptr ? pAssetManager->getPrefabCache().spawn( &manager, prefabPath, "AiController" ) : nullptr;
                AiControllerComponent* pAi           = pObject != nullptr ? pObject->getComponent<AiControllerComponent>() : nullptr;
                if ( pAi == nullptr )
                {
                    const utf8* pPawnName = pawn.getOwner() != nullptr ? pawn.getOwner()->getName().c_str() : "?";
                    SW_LOG_WARNING( "Pawn '%#' auto-possess prefab '%#' has no AI controller - the pawn stays unpossessed", pPawnName, prefabPath.c_str() );
                }
                return pAi;
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
        , _tick{ 0 }
        , _bRecording{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ControlSystem& ControlSystem::ensureFor( GameObjectManager& manager )
    {
        ControlSystem* pSystem = find( manager );
        if ( pSystem != nullptr )
            return *pSystem;
        // 시나리오 단계(Intent · Possess) 등록자가 정적 링크에서 빠지지 않게 그 .cpp 를 붙든다.
        ControlAutomationSteps::ensureLinked();
        unique_ptr<ControlSystem> pNew = make_unique<ControlSystem>();
        pSystem                        = pNew.get();
        (void)manager.addFrameSystem( ControlSystemInternal::getFrameSystemKey(), std::move( pNew ) );
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

    InputManager* ControlSystem::findInputManager() const
    {
        return _pInputOverride != nullptr ? _pInputOverride : game::getService<InputManager>();
    }

    void ControlSystem::setRecording( bool bRecording, int32 capacityTicks )
    {
        if ( bRecording && _bRecording == SW_FALSE )
            _history.initialize( capacityTicks );
        _bRecording = bRecording ? SW_TRUE : SW_FALSE;
    }

    void ControlSystem::queuePossess( const ComponentHandle& controller, const ComponentHandle& pawn )
    {
        QueuedPossess& queued = _listQueuedPossess.emplace_back();
        queued._controller    = controller;
        queued._pawn          = pawn;
    }

    void ControlSystem::runBeforeTick( GameObjectManager& manager, float32 deltaTime )
    {
        applyQueuedPossess( manager );
        autoPossess( manager );

        ControlFrameContext context{};
        context._pInput    = findInputManager();
        context._deltaTime = deltaTime;
        context._tick      = _tick;

        // 등록부의 창은 등록 · 해제가 일어나면 버려야 한다 — 조종자가 판단 중에 오브젝트를 만들 수 있으므로 자리 번호로 돌며 매번 크기를 다시 본다.
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
            pController->setControlRotation( OrientationUtil::wrapAngle( pController->getControlYaw() + offset._x ), pitch );
            ControlIntent intent{};
            pController->produceIntent( context, *pPawn, intent );
            intent._controlYaw   = pController->getControlYaw();
            intent._controlPitch = pController->getControlPitch();
            intent.quantize();
            pPawn->applyIntent( intent );
        }
        if ( _bRecording == SW_TRUE )
            recordIntents( manager );
        ++_tick;
    }

    void ControlSystem::applyQueuedPossess( GameObjectManager& manager )
    {
        for ( size_t queuedIndex = 0; queuedIndex < _listQueuedPossess.size(); ++queuedIndex )
        {
            const QueuedPossess  queued      = _listQueuedPossess[queuedIndex];
            ControllerComponent* pController = static_cast<ControllerComponent*>( manager.resolveComponent( queued._controller ) );
            if ( pController == nullptr )
                continue;
            PawnComponent* pPawn = queued._pawn.isValid() ? static_cast<PawnComponent*>( manager.resolveComponent( queued._pawn ) ) : nullptr;
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
                _history.record( _tick, pPawn->getHandle(), pOwner->getName(), pPawn->getIntent() );
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
                _listAutoPossessPawn.push_back( pPawn->getHandle() );
        }
        // 조종자를 세우면 등록부가 바뀐다 — 다 모은 뒤에 세운다.
        for ( const ComponentHandle& handle : _listAutoPossessPawn )
        {
            PawnComponent* pPawn = static_cast<PawnComponent*>( manager.resolveComponent( handle ) );
            if ( pPawn == nullptr || pPawn->isPossessed() )
                continue;
            ControllerComponent* pController = nullptr;
            if ( pPawn->getAutoPossess() == PawnAutoPossess::Player0 )
                pController = findOrCreatePlayerController( manager, 0 );
            else if ( pPawn->getAutoPossess() == PawnAutoPossess::Ai )
                pController = ControlSystemInternal::createAiController( manager, *pPawn );
            if ( pController != nullptr )
                pController->possess( *pPawn );
        }
        _listAutoPossessPawn.clear();
    }
} // namespace sw
