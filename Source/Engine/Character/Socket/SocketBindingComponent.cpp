#include "pch.h"

#include "Engine/Character/Socket/SocketBindingComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/Physics/SocketPhysicsBody.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_LOG_CALLER( "SocketBinding" );

    namespace
    {
        struct SocketBindingComponentInternal
        {
            /** @brief 같은 소켓 변환이면 로컬을 다시 쓰지 않는다(쉬는 단위는 비용이 없어야 한다). */
            static bool isSameTransform( const float4x4& lhs, const float4x4& rhs )
            {
                const float32* pLhs = lhs.data();
                const float32* pRhs = rhs.data();
                for ( uint32 element = 0; element < 16; ++element )
                {
                    if ( MathUtil::abs( pLhs[element] - pRhs[element] ) > 1.0e-6f )
                        return false;
                }
                return true;
            }

            static void setLocalTransform( SceneComponent& scene, const float4x4& localTransform )
            {
                float3     scale;
                quaternion rotation;
                float3     translation;
                (void)localTransform.decompose( scale, rotation, translation );
                scene.setLocalPosition( translation );
                scene.setLocalRotation( rotation.getEulerAngles() );
                scene.setLocalScale( scale );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SocketBindingComponent::SocketBindingComponent()
        : _returnBlend{}
        , _state{ SocketBindingState::ReleasedAnimated }
        , _socketInHolder{}
        , _returnFrom{}
        , _socketName{}
        , _holder{}
        , _physicsBodyComponent{}
        , _pPhysicsBody{ nullptr }
        , _returnElapsed{ 0.0f }
    {
        // 쉬는 단위는 비용이 없다 — 틱은 물리 · 되돌아가기 동안만 켠다(`enterState`).
        setCanEverTick( false );
    }

    void SocketBindingComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        SceneComponent* pScene = findOwnerScene();
        if ( pScene == nullptr )
            return;
        if ( _state == SocketBindingState::ReleasedPhysics )
        {
            const ISocketPhysicsBody* pBody = resolvePhysicsBody();
            float4x4                  bodyWorld;
            if ( pBody != nullptr && pBody->findBodyWorldTransform( bodyWorld ) )
                pScene->setWorldTransform( bodyWorld );
            return;
        }
        if ( _state != SocketBindingState::Returning )
            return;
        float4x4 target;
        if ( computeSocketWorld( target ) == false )
        {
            // holder 가 사라졌다 — 지금 자리에 남는다.
            enterState( SocketBindingState::ReleasedAnimated );
            return;
        }
        _returnElapsed += MathUtil::max( 0.0f, deltaTime );
        const float32 progress = getReturnProgress();
        if ( progress >= 1.0f )
        {
            if ( attachToHolder() == false )
                enterState( SocketBindingState::ReleasedAnimated );
            return;
        }
        const float32 weight = evaluateBlendWeight( _returnBlend, progress );
        pScene->setWorldTransform( CharacterGeometryUtil::blendTransforms( _returnFrom, target, weight ) );
    }

    void SocketBindingComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 같은 오브젝트의 강체가 물리 바디다 — 코드가 따로 정했으면 그것을 둔다.
        GameObject*         pOwner = getOwner();
        RigidBodyComponent* pBody  = pOwner != nullptr ? pOwner->getComponent<RigidBodyComponent>() : nullptr;
        if ( pBody != nullptr && resolvePhysicsBody() == nullptr )
            setPhysicsBody( pBody, &pBody->getSocketPhysicsBody() );
    }

    void SocketBindingComponent::onEndPlay()
    {
        endPhysicsIfRunning();
        Component::onEndPlay();
    }

    SceneComponent* SocketBindingComponent::findOwnerScene() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner == nullptr ? nullptr : pOwner->getPrimarySceneComponent();
    }

    GameObject* SocketBindingComponent::resolveHolder() const
    {
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || pOwner->getManager() == nullptr || _holder.isValid() == false )
            return nullptr;
        return pOwner->getManager()->resolveGameObject( _holder );
    }

    ISocketPhysicsBody* SocketBindingComponent::resolvePhysicsBody() const
    {
        // 인터페이스 포인터는 핸들이 살아 있다고 확인될 때만 쓴다 — 바디 컴포넌트가 먼저 사라질 수 있다.
        const GameObject* pOwner = getOwner();
        if ( _pPhysicsBody == nullptr || pOwner == nullptr || pOwner->getManager() == nullptr )
            return nullptr;
        return pOwner->getManager()->resolveComponent( _physicsBodyComponent ) != nullptr ? _pPhysicsBody : nullptr;
    }

    bool SocketBindingComponent::computeSocketWorld( float4x4& outWorldTransform ) const
    {
        const GameObject* pHolder = resolveHolder();
        if ( pHolder == nullptr || pHolder->getPrimarySceneComponent() == nullptr )
            return false;
        outWorldTransform = _socketInHolder * pHolder->getPrimarySceneComponent()->getWorldMatrix();
        return true;
    }

    bool SocketBindingComponent::attachToHolder()
    {
        SceneComponent*   pScene  = findOwnerScene();
        const GameObject* pHolder = resolveHolder();
        if ( pScene == nullptr || pHolder == nullptr || pHolder->getPrimarySceneComponent() == nullptr )
            return false;
        SceneComponent* pHolderScene = pHolder->getPrimarySceneComponent();
        if ( pScene->getParent() != pHolderScene && pScene->attachToComponent( pHolderScene, AttachRule::KeepRelative ) == false )
        {
            SW_LOG_WARNING( "'%#' cannot attach to socket '%#' of '%#'", getOwner()->getName().c_str(), _socketName.c_str(), pHolder->getName().c_str() );
            return false;
        }
        SocketBindingComponentInternal::setLocalTransform( *pScene, _socketInHolder );
        enterState( SocketBindingState::Bound );
        // 붙은 동안 강체는 손을 따르는 키네마틱이다(동적으로 남으면 계층과 싸운다).
        ISocketPhysicsBody* pBody = resolvePhysicsBody();
        if ( pBody != nullptr )
            pBody->endPhysics();
        return true;
    }

    void SocketBindingComponent::endPhysicsIfRunning()
    {
        if ( _state != SocketBindingState::ReleasedPhysics )
            return;
        ISocketPhysicsBody* pBody = resolvePhysicsBody();
        if ( pBody != nullptr )
            pBody->endPhysics();
    }

    void SocketBindingComponent::enterState( SocketBindingState state )
    {
        _state = state;
        if ( state != SocketBindingState::Returning )
            _returnElapsed = 0.0f;
        const bool bNeedsTick = state == SocketBindingState::ReleasedPhysics || state == SocketBindingState::Returning;
        if ( canEverTick() != bNeedsTick )
            setCanEverTick( bNeedsTick );
    }

    bool SocketBindingComponent::bindToSocket( GameObject* pHolder, const hashed_string& socketName, const float4x4& socketInHolder )
    {
        if ( pHolder == nullptr || pHolder->getPrimarySceneComponent() == nullptr || findOwnerScene() == nullptr )
            return false;
        endPhysicsIfRunning();
        _holder         = pHolder->getHandle();
        _socketName     = socketName;
        _socketInHolder = socketInHolder;
        return attachToHolder();
    }

    void SocketBindingComponent::updateSocketTransform( const float4x4& socketInHolder )
    {
        if ( SocketBindingComponentInternal::isSameTransform( socketInHolder, _socketInHolder ) )
            return;
        _socketInHolder = socketInHolder;
        if ( _state != SocketBindingState::Bound )
            return;
        SceneComponent* pScene = findOwnerScene();
        if ( pScene != nullptr )
            SocketBindingComponentInternal::setLocalTransform( *pScene, _socketInHolder );
    }

    bool SocketBindingComponent::release( SocketReleaseMode mode, const float3& linearVelocity )
    {
        SceneComponent* pScene = findOwnerScene();
        if ( pScene == nullptr || _state != SocketBindingState::Bound )
            return false;
        ISocketPhysicsBody* pBody = resolvePhysicsBody();
        if ( mode == SocketReleaseMode::Physics && pBody == nullptr )
            return false;
        // 월드 자리를 지키며 뗀다 — 뗀 뒤 첫 프레임이 붙어 있던 자리다.
        const float4x4 boundWorld = pScene->getWorldMatrix();
        pScene->detachFromComponent( AttachRule::KeepWorld );
        pScene->setWorldTransform( boundWorld );
        if ( mode == SocketReleaseMode::Physics )
        {
            pBody->beginPhysics( boundWorld, linearVelocity );
            enterState( SocketBindingState::ReleasedPhysics );
            return true;
        }
        enterState( SocketBindingState::ReleasedAnimated );
        return true;
    }

    bool SocketBindingComponent::returnToSocket()
    {
        SceneComponent* pScene = findOwnerScene();
        float4x4        target;
        if ( pScene == nullptr || computeSocketWorld( target ) == false )
            return false;
        if ( _state == SocketBindingState::Bound )
            return true;
        endPhysicsIfRunning();
        _returnFrom = pScene->getWorldMatrix();
        if ( _returnBlend._duration <= 0.0f || _returnBlend._curve == BlendCurve::Cut )
            return attachToHolder();
        _returnElapsed = 0.0f;
        enterState( SocketBindingState::Returning );
        return true;
    }

    bool SocketBindingComponent::transferTo( GameObject* pNewHolder, const hashed_string& socketName, const float4x4& socketInHolder, bool bBlend )
    {
        SceneComponent* pScene = findOwnerScene();
        if ( pScene == nullptr || pNewHolder == nullptr || pNewHolder->getPrimarySceneComponent() == nullptr )
            return false;
        if ( pScene->canAttachTo( pNewHolder->getPrimarySceneComponent() ) == false )
            return false;
        const float4x4 currentWorld = pScene->getWorldMatrix();
        endPhysicsIfRunning();
        if ( pScene->getParent() != nullptr )
        {
            pScene->detachFromComponent( AttachRule::KeepWorld );
            pScene->setWorldTransform( currentWorld );
        }
        _holder         = pNewHolder->getHandle();
        _socketName     = socketName;
        _socketInHolder = socketInHolder;
        if ( bBlend == false )
            return attachToHolder();
        enterState( SocketBindingState::ReleasedAnimated );
        return returnToSocket();
    }

    void SocketBindingComponent::setPhysicsBody( Component* pBodyComponent, ISocketPhysicsBody* pBody )
    {
        if ( pBodyComponent == nullptr || pBody == nullptr )
        {
            endPhysicsIfRunning();
            _physicsBodyComponent = ComponentHandle{};
            _pPhysicsBody         = nullptr;
            if ( _state == SocketBindingState::ReleasedPhysics )
                enterState( SocketBindingState::ReleasedAnimated );
            return;
        }
        _physicsBodyComponent = pBodyComponent->getHandle();
        _pPhysicsBody         = pBody;
    }

    float32 SocketBindingComponent::getReturnProgress() const
    {
        if ( _state != SocketBindingState::Returning || _returnBlend._duration <= 0.0f )
            return 0.0f;
        return MathUtil::saturate( _returnElapsed / _returnBlend._duration );
    }
} // namespace sw
