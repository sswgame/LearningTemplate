#include "pch.h"

#include "Engine/Object/GameObject/ScenePhysics.h"

#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Physics/PhysicsSystem.h"

namespace sw
{
    SW_LOG_CALLER( "ScenePhysics" );

    namespace
    {
        struct ScenePhysicsInternal
        {
            static PhysicsSystem* findPhysicsSystem()
            {
                if ( engine::areEngineServicesBound() == false )
                    return nullptr;
                PhysicsSystem& system = engine::getPhysicsSystem();
                return system.isInitialized() ? &system : nullptr;
            }

            static constexpr uint32 kPhaseCount = static_cast<uint32>( PhysicsComponentPhase::Count );
        };
    } // namespace
} // namespace sw

namespace sw
{
    ScenePhysics::ScenePhysics()
        : _pScene3D{}
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
        shutdown();
    }

    void ScenePhysics::shutdown()
    {
        // 남은 컴포넌트가 들고 있는 핸들은 씬과 함께 무효가 된다 — 먼저 놓게 한다.
        for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
        {
            for ( PhysicsComponent* pComponent : _arrListComponent[phase] )
                pComponent->releasePhysics( *this );
        }
        _pScene3D.reset();
        _pScene2D.reset();
        _listFrameEvent3D.clear();
        _listFrameEvent2D.clear();
        _accumulator.reset();
    }

    const PhysicsSettings* ScenePhysics::findSettings() const
    {
        const PhysicsSystem* pSystem = ScenePhysicsInternal::findPhysicsSystem();
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
            const PhysicsSystem* pSystem = ScenePhysicsInternal::findPhysicsSystem();
            if ( pSystem != nullptr )
                _pScene3D = pSystem->createScene3D();
        }
        return _pScene3D.get();
    }

    IPhysicsScene2D* ScenePhysics::getScene2D()
    {
        if ( _pScene2D == nullptr )
        {
            const PhysicsSystem* pSystem = ScenePhysicsInternal::findPhysicsSystem();
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
            count += static_cast<uint32>( _arrListComponent[phase].size() );
        return count;
    }

    void ScenePhysics::step( GameObjectManager& manager, float32 deltaTime )
    {
        _listFrameEvent3D.clear();
        _listFrameEvent2D.clear();
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
                listComponent[index]->beginPhysicsFrame( *this );
        }

        const uint32  stepCount = _accumulator.advance( deltaTime );
        const float32 fixedStep = _accumulator.getFixedTimeStep();
        for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
            {
                vector<PhysicsComponent*>& listComponent = _arrListComponent[phase];
                for ( size_t index = 0; index < listComponent.size(); ++index )
                    listComponent[index]->prePhysicsStep( *this, fixedStep, stepIndex, stepCount );
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
                    listComponent[index]->postPhysicsStep( *this );
            }
        }

        const float32 alpha = _accumulator.getAlpha();
        for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
        {
            vector<PhysicsComponent*>& listComponent = _arrListComponent[phase];
            for ( size_t index = 0; index < listComponent.size(); ++index )
                listComponent[index]->endPhysicsFrame( *this, alpha );
        }

        dispatchEvents( manager );
    }

    void ScenePhysics::dispatchEvents( GameObjectManager& manager )
    {
        if ( _listFrameEvent3D.empty() && _listFrameEvent2D.empty() )
            return;
        // 처리가 바디를 만들거나 지워도(다음 프레임의 이벤트가 된다) 이 프레임의 목록은 그대로 돈다 — 베껴 둔다.
        const vector<PhysicsContactEvent3D> listEvent3D = _listFrameEvent3D;
        const vector<PhysicsContactEvent2D> listEvent2D = _listFrameEvent2D;
        for ( const PhysicsContactEvent3D& event : listEvent3D )
        {
            const bool bTriggerA = event._bTriggerA == SW_TRUE;
            const bool bTriggerB = event._bTriggerB == SW_TRUE;
            deliverEvent( manager, event._userDataA, event._userDataB, event._bodyA, event._bodyB, event._point, event._normal, event._impulse, event._phase,
                          bTriggerA, bTriggerB, false );
            deliverEvent( manager, event._userDataB, event._userDataA, event._bodyB, event._bodyA, event._point, -event._normal, event._impulse, event._phase,
                          bTriggerB, bTriggerA, false );
        }
        for ( const PhysicsContactEvent2D& event : listEvent2D )
        {
            const float3 point{ event._point._x, event._point._y, 0.0f };
            const float3 normal{ event._normal._x, event._normal._y, 0.0f };
            const bool   bTriggerA = event._bTriggerA == SW_TRUE;
            const bool   bTriggerB = event._bTriggerB == SW_TRUE;
            deliverEvent( manager, event._userDataA, event._userDataB, event._bodyA, event._bodyB, point, normal, event._impulse, event._phase, bTriggerA, bTriggerB,
                          true );
            deliverEvent( manager, event._userDataB, event._userDataA, event._bodyB, event._bodyA, point, -normal, event._impulse, event._phase, bTriggerB, bTriggerA,
                          true );
        }
    }

    void ScenePhysics::deliverEvent( GameObjectManager& manager, uint64 selfUserData, uint64 otherUserData, PhysicsBodyHandle selfBody, PhysicsBodyHandle otherBody,
                                     const float3& point, const float3& normal, float32 impulse, PhysicsContactPhase phase, bool bSelfTrigger, bool bOtherTrigger,
                                     bool bIs2D )
    {
        GameObject* pSelf = selfUserData != 0 ? manager.findGameObjectById( selfUserData ) : nullptr;
        if ( pSelf == nullptr || pSelf->isActiveInHierarchy() == false )
            return;
        GameObject* pOther = otherUserData != 0 ? manager.findGameObjectById( otherUserData ) : nullptr;
        // 처리가 컴포넌트를 붙이고 뗄 수 있으므로 목록을 베껴 돈다.
        const vector<Component*> listTarget( pSelf->getComponents().begin(), pSelf->getComponents().end() );
        if ( bSelfTrigger || bOtherTrigger )
        {
            OverlapInfo overlap;
            overlap._pOther        = pOther;
            overlap._selfBody      = selfBody;
            overlap._otherBody     = otherBody;
            overlap._bSelfTrigger  = bSelfTrigger ? SW_TRUE : SW_FALSE;
            overlap._bOtherTrigger = bOtherTrigger ? SW_TRUE : SW_FALSE;
            for ( Component* pComponent : listTarget )
            {
                if ( pComponent == nullptr || pComponent->isPendingDestroy() || pComponent->isSelfActive() == false )
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
            if ( pComponent == nullptr || pComponent->isPendingDestroy() || pComponent->isSelfActive() == false )
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
