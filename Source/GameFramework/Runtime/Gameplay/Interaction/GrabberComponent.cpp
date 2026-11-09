#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/GrabberComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    bool TransformGrabPhysics::attach( GameObject& holder, GameObject& target, const float3& localOffset )
    {
        SceneComponent* pHolderScene  = holder.getPrimarySceneComponent();
        SceneComponent* pTargetScene  = target.getPrimarySceneComponent();
        const bool      bCannotAttach = pHolderScene == nullptr || pTargetScene == nullptr || pTargetScene->attachToComponent( pHolderScene, AttachRule::KeepWorld ) == false;
        if ( bCannotAttach )
            return false;
        pTargetScene->setLocalPosition( localOffset );
        return true;
    }

    void TransformGrabPhysics::release( GameObject& holder, GameObject& target, const float3& velocity )
    {
        (void)holder;
        (void)velocity; // 강체가 없으면 속도를 받을 곳이 없다 - 놓은 자리에 남는다. 던지려면 대상에 강체를 둔다(RigidBodyGrabPhysics)
        SceneComponent* pTargetScene = target.getPrimarySceneComponent();
        if ( pTargetScene != nullptr )
            pTargetScene->detachFromComponent( AttachRule::KeepWorld );
    }

    bool RigidBodyGrabPhysics::attach( GameObject& holder, GameObject& target, const float3& localOffset )
    {
        if ( _transform.attach( holder, target, localOffset ) == false )
            return false;
        // 강체는 손을 따르는 키네마틱으로 둔다. 동적으로 남겨 두면 중력과 접촉이 계층 구조와 충돌한다.
        RigidBodyComponent* pBody3D = target.getComponent<RigidBodyComponent>();
        if ( pBody3D != nullptr )
            pBody3D->setBodyType( PhysicsBodyType::Kinematic );
        RigidBody2DComponent* pBody2D = target.getComponent<RigidBody2DComponent>();
        if ( pBody2D != nullptr )
            pBody2D->setBodyType( PhysicsBodyType::Kinematic );
        return true;
    }

    void RigidBodyGrabPhysics::release( GameObject& holder, GameObject& target, const float3& velocity )
    {
        RigidBodyComponent*   pBody3D = target.getComponent<RigidBodyComponent>();
        RigidBody2DComponent* pBody2D = target.getComponent<RigidBody2DComponent>();
        if ( pBody3D == nullptr && pBody2D == nullptr )
        {
            _transform.release( holder, target, velocity );
            return;
        }
        SceneComponent* pTargetScene = target.getPrimarySceneComponent();
        if ( pTargetScene != nullptr )
            pTargetScene->detachFromComponent( AttachRule::KeepWorld );
        if ( pBody3D != nullptr )
        {
            pBody3D->setBodyType( PhysicsBodyType::Dynamic );
            pBody3D->setLinearVelocity( velocity );
            pBody3D->wake();
        }
        if ( pBody2D != nullptr )
        {
            pBody2D->setBodyType( PhysicsBodyType::Dynamic );
            pBody2D->setLinearVelocity( float2{ velocity._x, velocity._y } );
            pBody2D->wake();
        }
    }

    GrabberComponent::GrabberComponent()
        : _holdOffset{ 0.0f, 1.0f, 0.6f }
        , _held{}
        , _grabButton{}
        , _grabbableTags{}
        , _reach{ 1.5f }
        , _throwSpeed{ 0.0f }
        , _fallback{}
        , _grabIndex{}
        , _listTagged{}
    {
        setCanEverTick( true );
    }

    void GrabberComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr && _grabButton.empty() == false ? pOwner->getComponent<PawnComponent>() : nullptr;
        _grabIndex                  = pPawn != nullptr ? pPawn->findButton( _grabButton ) : nullopt;
        if ( _grabButton.empty() == false && _grabIndex.has_value() == false )
            SW_LOG_ERROR_ID( "GrabberComponent.GrabButtonNotOnPawn", "Grabber on '%#' listens to button '%#' but its object has no pawn with that button - the button does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?", _grabButton.c_str() );
    }

    void GrabberComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _grabIndex.has_value() == false )
            return;
        GameObject*          pOwner   = getOwner();
        const PawnComponent* pPawn    = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        GameObjectManager*   pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const bool           bNoPress = pPawn == nullptr || pManager == nullptr || pPawn->wasButtonTriggered( _grabIndex ) == false;
        if ( bNoPress )
            return;
        pManager->executeOrDeferPostTickOnComponent( *this, []( GrabberComponent& component )
        { (void)component.toggleGrab(); } ); // 집을 것이 없으면 아무 일도 없다
    }

    GameObject* GrabberComponent::findNearestGrabbable()
    {
        GameObject*           pOwner   = getOwner();
        GameObjectManager*    pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const SceneComponent* pSelf    = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pSelf == nullptr )
            return nullptr;
        // 후보는 태그 색인에서만 모은다(씬을 훑지 않는다 — 포탑 · 상호작용 고르기와 같다).
        pManager->getTagIndex().findObjectsWithTags( _grabbableTags, _listTagged );
        GameObject* pNearest     = nullptr;
        float32     bestDistance = _reach;
        for ( GameObject* pObject : _listTagged )
        {
            const SceneComponent* pOther = pObject != pOwner && pObject->isActiveInHierarchy() ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pOther == nullptr )
                continue;
            const float32 distance = float3::getDistance( pSelf->getWorldPosition(), pOther->getWorldPosition() );
            if ( distance <= bestDistance )
            {
                bestDistance = distance;
                pNearest     = pObject;
            }
        }
        return pNearest;
    }

    bool GrabberComponent::toggleGrab()
    {
        if ( isHolding() )
        {
            const GameObject*     pOwner  = getOwner();
            const SceneComponent* pSelf   = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
            const float3          forward = pSelf != nullptr ? float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pSelf->getWorldMatrix() ).normalize() : float3{};
            return throwHeld( forward * _throwSpeed );
        }
        GameObject* pTarget = findNearestGrabbable();
        return pTarget != nullptr && grab( *pTarget );
    }

    IGrabPhysics& GrabberComponent::getBackend()
    {
        IGrabPhysics* pService = game::findSubsystem<IGrabPhysics>();
        if ( pService == nullptr )
            return _fallback;
        return *pService;
    }

    bool GrabberComponent::grab( GameObject& target )
    {
        GameObject* pOwner      = getOwner();
        const bool  bCannotGrab = pOwner == nullptr || &target == pOwner || isHolding();
        if ( bCannotGrab )
            return false;
        if ( getBackend().attach( *pOwner, target, _holdOffset ) == false )
            return false;
        _held = target.getHandle();
        return true;
    }

    bool GrabberComponent::throwHeld( const float3& velocity )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*        pHeld    = pManager != nullptr ? pManager->resolveGameObject( _held ) : nullptr;
        _held                       = GameObjectHandle{};
        if ( pHeld == nullptr )
            return false;
        getBackend().release( *pOwner, *pHeld, velocity );
        return true;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct GrabberCommandsInternal
        {
            /** @brief 활성 씬에서 이름 붙은 오브젝트의 집기 컴포넌트입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static GrabberComponent* findGrabber( const string& name, string& outReply )
            {
                GameObject*       pObject  = engine::findActiveObject( hashed_string( name ) );
                GrabberComponent* pGrabber = pObject != nullptr ? pObject->getComponent<GrabberComponent>() : nullptr;
                if ( pGrabber == nullptr )
                    outReply = "no object named '" + name + "' with a GrabberComponent in the active scene";
                return pGrabber;
            }

            /** @brief `grab.pick <holder> <target>` — 든 쪽이 대상을 집습니다(게임의 집기 입력과 같은 함수). */
            static bool runPick( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                GrabberComponent* pGrabber = findGrabber( listArgument[0], outReply );
                GameObject*       pTarget  = engine::findActiveObject( hashed_string( listArgument[1] ) );
                if ( pGrabber == nullptr || pTarget == nullptr )
                {
                    if ( pTarget == nullptr )
                        outReply = "no object named '" + listArgument[1] + "' in the active scene";
                    return false;
                }
                if ( pGrabber->grab( *pTarget ) == false )
                {
                    outReply = "could not grab (already holding, the holder itself, or no scene component)";
                    return false;
                }
                outReply = "holding " + listArgument[1];
                return true;
            }

            /** @brief `grab.throw <holder> <vx> <vy> <vz>` — 든 것을 그 속도(m/s, 월드)로 던집니다. 0 0 0 이면 내려놓습니다. */
            static bool runThrow( const vector<string>& listArgument, string& outReply )
            {
                float3     velocity{};
                const bool bBadArguments = listArgument.size() != 4 || StringUtil::parseFloat( listArgument[1], velocity._x ) == false ||
                                           StringUtil::parseFloat( listArgument[2], velocity._y ) == false || StringUtil::parseFloat( listArgument[3], velocity._z ) == false;
                if ( bBadArguments )
                    return false;
                GrabberComponent* pGrabber = findGrabber( listArgument[0], outReply );
                if ( pGrabber == nullptr )
                    return false;
                if ( pGrabber->throwHeld( velocity ) == false )
                {
                    outReply = "nothing held";
                    return false;
                }
                outReply = "released";
                return true;
            }

            /** @brief 감시 값 `Grab.Held` 입니다. 대상의 집기 컴포넌트가 든 오브젝트 이름이고, 든 것이 없으면 `none` 입니다. */
            [[nodiscard]] static bool readHeld( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*       pTarget  = query.findTargetObject();
                const GrabberComponent* pGrabber = pTarget != nullptr ? pTarget->getComponent<GrabberComponent>() : nullptr;
                if ( pGrabber == nullptr )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "grabber" ) );
                    return false;
                }
                const GameObject* pHeld = query._pManager != nullptr ? query._pManager->resolveGameObject( pGrabber->getHeld() ) : nullptr;
                outValue.setText( pHeld != nullptr ? pHeld->getName().c_str() : "none" );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( GrabPick, "grab.pick", "grab.pick <holder> <target>", "Make the named holder's GrabberComponent pick up the named object of the active scene", &GrabberCommandsInternal::runPick );
    SW_DEV_COMMAND( GrabThrow, "grab.throw", "grab.throw <holder> <vx> <vy> <vz>", "Throw what the named holder carries at a world velocity (0 0 0 drops it)", &GrabberCommandsInternal::runThrow );
    SW_AUTOMATION_QUERY_WATCH( grabHeld, "Grab.Held", "Name of the object the target's GrabberComponent holds; none when it holds nothing", &GrabberCommandsInternal::readHeld, true, false );
} // namespace sw
