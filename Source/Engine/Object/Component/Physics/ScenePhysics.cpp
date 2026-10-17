#include "pch.h"

#include "Engine/Object/Component/Physics/ScenePhysics.h"

#include "Core/Memory/Memory.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Physics/PhysicsSystem.h"

namespace sw
{
    SW_LOG_CALLER( "ScenePhysics" );

    namespace
    {
        struct ScenePhysicsInternal
        {
            static constexpr uint32 kPhaseCount = static_cast<uint32>( PhysicsComponentPhase::Count );
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_SCENE_SERVICE( ScenePhysics, Physics )

    ScenePhysics::ScenePhysics( GameObjectManager& manager )
        : _manager{ manager }
        , _pPhysicsSystem{ manager.getSceneEngineServices()._pPhysicsSystem }
        , _listCollider2D{}
        , _listSweptOverlap2D{}
        , _pScene3D{}
        , _pScene2D{}
        , _accumulator{}
        , _arrListComponent{}
        , _listFrameEvent3D{}
        , _listFrameEvent2D{}
        , _stepCount{ 0 }
        , _debrisBodyCount{ 0 }
        , _bConfigured{ false }
    {
    }

    void ScenePhysics::changeDebrisBodyCount( int32 delta )
    {
        const int64 count = static_cast<int64>( _debrisBodyCount ) + delta;
        _debrisBodyCount  = count > 0 ? static_cast<uint32>( count ) : 0u;
    }

    ScenePhysics::~ScenePhysics()
    {
        onSceneCleared();
    }

    void ScenePhysics::update( float32 deltaTime )
    {
        SW_MEMORY_SCOPE( Physics );
        step( _manager, deltaTime );
    }

    void ScenePhysics::onSceneCleared()
    {
        // 남은 컴포넌트가 들고 있는 핸들은 씬과 함께 무효가 된다 — 먼저 놓게 한다.
        for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
        {
            for ( PhysicsComponent* pComponent : _arrListComponent[phase] )
            {
                pComponent->releasePhysics( *this );
            }
        }
        _pScene3D.reset();
        _pScene2D.reset();
        _listFrameEvent3D.clear();
        _listFrameEvent2D.clear();
        _listSweptOverlap2D.clear();
        _accumulator.reset();
    }

    void ScenePhysics::registerCollider2D( BoxCollider2DComponent* pCollider )
    {
        if ( pCollider == nullptr || pCollider->_colliderIndex != BoxCollider2DComponent::kNotRegistered )
            return;
        pCollider->_colliderIndex = static_cast<uint32>( _listCollider2D.size() );
        _listCollider2D.push_back( pCollider );
    }

    void ScenePhysics::unregisterCollider2D( BoxCollider2DComponent* pCollider )
    {
        const bool bNotRegistered = pCollider == nullptr || pCollider->_colliderIndex >= _listCollider2D.size() || _listCollider2D[pCollider->_colliderIndex] != pCollider;
        if ( bNotRegistered )
            return;
        BoxCollider2DComponent* pMoved             = _listCollider2D.back();
        _listCollider2D[pCollider->_colliderIndex] = pMoved;
        pMoved->_colliderIndex                     = pCollider->_colliderIndex;
        _listCollider2D.pop_back();
        pCollider->_colliderIndex = BoxCollider2DComponent::kNotRegistered;
    }

    PhysicsSystem* ScenePhysics::findInitializedPhysicsSystem() const
    {
        // 포인터는 생성할 때 받은 것이고, 백엔드가 올라와 있는지는 부를 때마다 본다(물리 단계가 씬보다 늦게 서고 먼저 내려간다).
        if ( _pPhysicsSystem == nullptr || _pPhysicsSystem->isInitialized() == false )
            return nullptr;
        return _pPhysicsSystem;
    }

    const PhysicsSettings* ScenePhysics::findSettings() const
    {
        const PhysicsSystem* pSystem = findInitializedPhysicsSystem();
        return pSystem != nullptr ? &pSystem->getSettings() : nullptr;
    }

    uint8 ScenePhysics::resolveLayer( const hashed_string& layerName ) const
    {
        if ( layerName.empty() )
            return 0;
        const PhysicsSettings* pSettings = findSettings();
        uint8                  layer     = 0;
        if ( pSettings != nullptr && pSettings->findLayerIndex( layerName, layer ) )
            return layer;
        SW_LOG_ERROR( "Unknown physics layer '%#' - using layer 0", layerName.c_str() );
        return 0;
    }

    void ScenePhysics::configureOnce()
    {
        if ( _bConfigured )
            return;
        const PhysicsSettings* pSettings = findSettings();
        if ( pSettings == nullptr )
            return;
        // 고정 스텝의 출처는 엔진 설정 하나다 — 물리는 그 스텝을 서브스텝 수로 나눈다(상한도 같은 배수).
        const EngineConfig& engineConfig = EngineConfig::getActive();
        const uint32        subStepCount = pSettings->_subStepCount;
        _accumulator.configure( engineConfig._fixedDeltaTime / static_cast<float32>( subStepCount ), engineConfig._maxFixedStepPerFrame * subStepCount );
        _bConfigured = true;
    }

    IPhysicsScene3D* ScenePhysics::getScene3D()
    {
        if ( _pScene3D == nullptr )
        {
            const PhysicsSystem* pSystem = findInitializedPhysicsSystem();
            if ( pSystem != nullptr )
                _pScene3D = pSystem->createScene3D();
        }
        return _pScene3D.get();
    }

    IPhysicsScene2D* ScenePhysics::getScene2D()
    {
        if ( _pScene2D == nullptr )
        {
            const PhysicsSystem* pSystem = findInitializedPhysicsSystem();
            if ( pSystem != nullptr )
                _pScene2D = pSystem->createScene2D();
        }
        return _pScene2D.get();
    }

    void ScenePhysics::registerComponent( PhysicsComponent* pComponent )
    {
        if ( pComponent == nullptr || pComponent->_physicsIndex != PhysicsComponent::kNotRegistered )
            return;
        vector<PhysicsComponent*>& listComponent = _arrListComponent[static_cast<uint32>( pComponent->_phase )];
        pComponent->_physicsIndex                = static_cast<uint32>( listComponent.size() );
        listComponent.push_back( pComponent );
    }

    void ScenePhysics::unregisterComponent( PhysicsComponent* pComponent )
    {
        if ( pComponent == nullptr )
            return;
        vector<PhysicsComponent*>& listComponent = _arrListComponent[static_cast<uint32>( pComponent->_phase )];
        const uint32               index         = pComponent->_physicsIndex;
        if ( index >= listComponent.size() || listComponent[index] != pComponent )
            return;
        PhysicsComponent* pMoved = listComponent.back();
        listComponent[index]     = pMoved;
        pMoved->_physicsIndex    = index;
        listComponent.pop_back();
        pComponent->_physicsIndex = PhysicsComponent::kNotRegistered;
    }

    uint32 ScenePhysics::getComponentCount() const
    {
        uint32 count = 0;
        for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
        {
            count += static_cast<uint32>( _arrListComponent[phase].size() );
        }
        return count;
    }

    void ScenePhysics::step( GameObjectManager& manager, float32 deltaTime )
    {
        _listFrameEvent3D.clear();
        _listFrameEvent2D.clear();
        _listSweptOverlap2D.clear();
        // 물리 컴포넌트도 바디도 없는 씬은 아무것도 하지 않는다(시간도 쌓지 않는다 — 처음 바디가 생긴 프레임이 따라잡느라 여러 스텝을 돌지 않게).
        const bool bHasWork = getComponentCount() != 0 || _pScene3D != nullptr || _pScene2D != nullptr;
        if ( bHasWork == false )
            return;
        SW_MEMORY_SCOPE( Physics );
        configureOnce();

        // 바디 → 관절 → 캐릭터. 콜백이 컴포넌트를 더하거나 뺄 수는 없다(구조 변경은 이 밖에서 난다) — 그래도 자리로 돌아 목록이 줄어도 넘지 않는다.
        for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
        {
            vector<PhysicsComponent*>& listComponent = _arrListComponent[phase];
            for ( size_t index = 0; index < listComponent.size(); ++index )
            {
                listComponent[index]->beginPhysicsFrame( *this );
            }
        }

        const uint32  stepCount = _accumulator.advance( deltaTime );
        const float32 fixedStep = _accumulator.getFixedTimeStep();
        for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
            {
                vector<PhysicsComponent*>& listComponent = _arrListComponent[phase];
                for ( size_t index = 0; index < listComponent.size(); ++index )
                {
                    listComponent[index]->prePhysicsStep( *this, fixedStep, stepIndex, stepCount );
                }
            }
            if ( _pScene3D != nullptr )
            {
                _pScene3D->step( fixedStep );
                const vector<PhysicsContactEvent3D>& listEvent = _pScene3D->getContactEvents();
                _listFrameEvent3D.insert( _listFrameEvent3D.end(), listEvent.begin(), listEvent.end() );
            }
            if ( _pScene2D != nullptr )
            {
                _pScene2D->step( fixedStep );
                const vector<PhysicsContactEvent2D>& listEvent = _pScene2D->getContactEvents();
                _listFrameEvent2D.insert( _listFrameEvent2D.end(), listEvent.begin(), listEvent.end() );
            }
            ++_stepCount;
            for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
            {
                vector<PhysicsComponent*>& listComponent = _arrListComponent[phase];
                for ( size_t index = 0; index < listComponent.size(); ++index )
                {
                    listComponent[index]->postPhysicsStep( *this );
                }
            }
        }

        const float32 alpha = _accumulator.getAlpha();
        for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
        {
            vector<PhysicsComponent*>& listComponent = _arrListComponent[phase];
            for ( size_t index = 0; index < listComponent.size(); ++index )
            {
                listComponent[index]->endPhysicsFrame( *this, alpha );
            }
        }

        dispatchEvents( manager );
    }

    void ScenePhysics::dispatchEvents( GameObjectManager& manager )
    {
        const bool bNoEvent = _listFrameEvent3D.empty() && _listFrameEvent2D.empty() && _listSweptOverlap2D.empty();
        if ( bNoEvent )
            return;
        // 처리가 바디를 만들거나 지워도(다음 프레임의 이벤트가 된다) 이 프레임의 목록은 그대로 돈다 — 베껴 둔다.
        const vector<PhysicsContactEvent3D> listEvent3D = _listFrameEvent3D;
        const vector<PhysicsContactEvent2D> listEvent2D = _listFrameEvent2D;
        const vector<SweptOverlap2D>        listSwept   = _listSweptOverlap2D;
        for ( const PhysicsContactEvent3D& event : listEvent3D )
        {
            const bool bTriggerA = event._bTriggerA == SW_TRUE;
            const bool bTriggerB = event._bTriggerB == SW_TRUE;
            deliverEvent( manager, event._userDataA, event._userDataB, event._bodyA, event._bodyB, event._point, event._normal, event._impulse, event._phase,
                          bTriggerA, bTriggerB, event.isOverlap(), false, 1.0f );
            deliverEvent( manager, event._userDataB, event._userDataA, event._bodyB, event._bodyA, event._point, -event._normal, event._impulse, event._phase,
                          bTriggerB, bTriggerA, event.isOverlap(), false, 1.0f );
        }
        // 쓸고 지나간 겹침이 먼저다 — 이번 프레임 길 위에서 닿았으므로 지금 자리의 겹침(센서 시작)보다 이르다. 지나쳤으므로 시작과 끝을 함께 낸다.
        // 이 목록은 콜라이더 쪽에서만 낸다(상대는 쓸리지 않았다) — 양쪽에 나눠 준다.
        for ( const SweptOverlap2D& swept : listSwept )
        {
            for ( const PhysicsContactPhase phase : { PhysicsContactPhase::Begin, PhysicsContactPhase::End } )
            {
                deliverEvent( manager, swept._selfUserData, swept._otherUserData, swept._selfBody, swept._otherBody, float3{}, float3{}, 0.0f, phase,
                              swept._bSelfTrigger, swept._bOtherTrigger, true, true, swept._hitFraction );
                deliverEvent( manager, swept._otherUserData, swept._selfUserData, swept._otherBody, swept._selfBody, float3{}, float3{}, 0.0f, phase,
                              swept._bOtherTrigger, swept._bSelfTrigger, true, true, swept._hitFraction );
            }
        }
        for ( const PhysicsContactEvent2D& event : listEvent2D )
        {
            const float3 point{ event._point._x, event._point._y, 0.0f };
            const float3 normal{ event._normal._x, event._normal._y, 0.0f };
            const bool   bTriggerA = event._bTriggerA == SW_TRUE;
            const bool   bTriggerB = event._bTriggerB == SW_TRUE;
            deliverEvent( manager, event._userDataA, event._userDataB, event._bodyA, event._bodyB, point, normal, event._impulse, event._phase, bTriggerA, bTriggerB,
                          event.isOverlap(), true, 1.0f );
            deliverEvent( manager, event._userDataB, event._userDataA, event._bodyB, event._bodyA, point, -normal, event._impulse, event._phase, bTriggerB, bTriggerA,
                          event.isOverlap(), true, 1.0f );
        }
    }

    void ScenePhysics::deliverEvent( GameObjectManager& manager, uint64 selfUserData, uint64 otherUserData, PhysicsBodyHandle selfBody, PhysicsBodyHandle otherBody,
                                     const float3& point, const float3& normal, float32 impulse, PhysicsContactPhase phase, bool bSelfTrigger, bool bOtherTrigger,
                                     bool bOverlap, bool bIs2D, float32 hitFraction )
    {
        GameObject* pSelf = selfUserData != 0 ? manager.findGameObjectByID( selfUserData ) : nullptr;
        if ( pSelf == nullptr || pSelf->isActiveInHierarchy() == false )
            return;
        GameObject* pOther = otherUserData != 0 ? manager.findGameObjectByID( otherUserData ) : nullptr;
        // 처리가 컴포넌트를 붙이고 뗄 수 있으므로 목록을 베껴 돈다.
        const vector<Component*> listTarget( pSelf->getComponents().begin(), pSelf->getComponents().end() );
        if ( bOverlap )
        {
            OverlapInfo overlap;
            overlap._pOther        = pOther;
            overlap._selfBody      = selfBody;
            overlap._otherBody     = otherBody;
            overlap._hitFraction   = hitFraction;
            overlap._bSelfTrigger  = bSelfTrigger ? SW_TRUE : SW_FALSE;
            overlap._bOtherTrigger = bOtherTrigger ? SW_TRUE : SW_FALSE;
            for ( Component* pComponent : listTarget )
            {
                const bool bInactiveTickTarget = pComponent == nullptr || pComponent->isPendingDestroy() || pComponent->isSelfActive() == false;
                if ( bInactiveTickTarget )
                    continue;
                switch ( phase )
                {
                    case PhysicsContactPhase::Begin:
                    {
                        pComponent->onOverlapBegin( overlap );
                        break;
                    }
                    case PhysicsContactPhase::Stay:
                    {
                        pComponent->onOverlapStay( overlap );
                        break;
                    }
                    case PhysicsContactPhase::End:
                    {
                        pComponent->onOverlapEnd( overlap );
                        break;
                    }
                }
            }
            return;
        }
        CollisionInfo collision;
        collision._pOther    = pOther;
        collision._selfBody  = selfBody;
        collision._otherBody = otherBody;
        collision._point     = point;
        collision._normal    = normal;
        collision._impulse   = impulse;
        collision._bIs2D     = bIs2D;
        for ( Component* pComponent : listTarget )
        {
            const bool bInactivePhysicsTarget = pComponent == nullptr || pComponent->isPendingDestroy() || pComponent->isSelfActive() == false;
            if ( bInactivePhysicsTarget )
                continue;
            switch ( phase )
            {
                case PhysicsContactPhase::Begin:
                {
                    pComponent->onCollisionBegin( collision );
                    break;
                }
                case PhysicsContactPhase::Stay:
                {
                    pComponent->onCollisionStay( collision );
                    break;
                }
                case PhysicsContactPhase::End:
                {
                    pComponent->onCollisionEnd( collision );
                    break;
                }
            }
        }
    }

    void ScenePhysics::drawDebug( IPhysicsDebugRenderer& renderer ) const
    {
        if ( _pScene3D != nullptr )
            _pScene3D->drawDebug( renderer );
        if ( _pScene2D != nullptr )
            _pScene2D->drawDebug( renderer );
    }
} // namespace sw
