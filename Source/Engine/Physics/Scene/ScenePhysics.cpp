#include "pch.h"

#include "Engine/Physics/Scene/ScenePhysics.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Object/GameObject/Service/ScenePreviewPolicy.h"
#include "Engine/Physics/Component/BoxCollider2DComponent.h"
#include "Engine/Physics/Component/PhysicsComponent.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Physics/Scene/PhysicsPoseStore.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    namespace
    {
        struct ScenePhysicsInternal
        {
            static constexpr uint32 kPhaseCount = static_cast<uint32>( PhysicsComponentPhase::Count );
            static constexpr uint32 kHookCount  = static_cast<uint32>( PhysicsHookSlot::Count );

            /** @brief @p owner 의 바디 칸에 그 강체 칸 번호를 적습니다(바디가 없으면 아무것도 하지 않는다). */
            template <typename TOwner>
            static void bindOwner( vector<uint32>& listPoseSlot, const TOwner& owner )
            {
                const PhysicsBodyHandle body = owner.getBodyHandle();
                if ( body.isValid() == false )
                    return;
                const uint32 index = body.getSlot().index();
                if ( index >= listPoseSlot.size() )
                    listPoseSlot.resize( static_cast<size_t>( index ) + 1, PhysicsPoseStore::kInvalidSlot );
                listPoseSlot[index] = owner.getPoseSlot();
            }

            /** @brief @p owner 가 자기 바디 칸의 주인이면 지웁니다. */
            template <typename TOwner>
            static void unbindOwner( vector<uint32>& listPoseSlot, const TOwner& owner )
            {
                const uint32 index = owner.getBodyHandle().getSlot().index();
                if ( index < listPoseSlot.size() && listPoseSlot[index] == owner.getPoseSlot() )
                    listPoseSlot[index] = PhysicsPoseStore::kInvalidSlot;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_SCENE_SERVICE( ScenePhysics, Physics, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::Transform }, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure } ) )
    SW_SCENE_SERVICE( ScenePhysicsEvents, FlushPostPhysics, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform, SceneDataKind::AnimatorState, SceneDataKind::Navigation }, SceneDataKindSet{}, SceneDataKindSet{} ) )

    /** @brief 활성 씬의 강체 물리(바디 셰이프 · 캐릭터 캡슐)를 디버그 선으로 그립니다(`ScenePhysics::drawDebug` → `DebugDrawQueue`). */
    SW_GLOBAL_VARIABLE( bool, gv_physicsDebugDraw, false, "강체 물리 바디 · 캐릭터를 디버그 선으로 그린다" );

    ScenePhysics::ScenePhysics( GameObjectManager& manager )
        : _manager{ manager }
        , _pPhysicsSystem{ locateSubsystem<PhysicsSystem>( manager.getSceneEngineServices()._pEngineSubsystems ) }
        , _listCollider2D{}
        , _listMovedCollider2D{}
        , _listSweptOverlap2D{}
        , _pScene3D{}
        , _pScene2D{}
        , _stepTimer{}
        , _arrListComponent{}
        , _arrListFrameHook{}
        , _arrListFrameHookRun{}
        , _mapHookEntry{}
        , _pHookRegistry{ locateSubsystem<PhysicsHookRegistry>( manager.getSceneEngineServices()._pEngineSubsystems ) }
        , _hookGeneration{ 0 }
        , _listBodyPoseSlot3D{}
        , _listBodyPoseSlot2D{}
        , _listFrameEvent3D{}
        , _listFrameEvent2D{}
        , _poseRead3D{}
        , _poseRead2D{}
        , _listPoseWrite{}
        , _listTransformWrite{}
        , _listDispatchEvent3D{}
        , _listDispatchEvent2D{}
        , _listDispatchSwept{}
        , _mapStayListener{}
        , _listDeliverTarget{}
        , _listStayTarget{}
        , _arrContactLog{}
        , _contactLogCount{ 0 }
        , _stepCount{ 0 }
        , _debrisBodyCount{ 0 }
        , _fixedStepOverride{ 0.0f }
        , _bConfigured{ SW_FALSE }
        , _bFrameHookListStale{ SW_TRUE }
        , _bHookBatchEnabled{ SW_TRUE }
        , _bFrameEventsPending{ SW_FALSE }
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
        // 틱만 도는 미리보기 씬은 물리를 끈다(`ScenePreviewPolicy::_bPhysics`).
        if ( _manager.isPhysicsStepEnabled() == false )
            return;
        SW_PROFILE_SCOPE( "GT.Scene.tick.physics" );
        SW_MEMORY_SCOPE( Physics );
        step( deltaTime );
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
        _listBodyPoseSlot3D.clear();
        _listBodyPoseSlot2D.clear();
        _listFrameEvent3D.clear();
        _listFrameEvent2D.clear();
        _listSweptOverlap2D.clear();
        _listMovedCollider2D.clear();
        _listPoseWrite.clear();
        _bFrameEventsPending = SW_FALSE;
        _stepTimer.reset();
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
        SW_LOG_ERROR_ID( "ScenePhysics.UnknownPhysicsLayerUsing", "Unknown physics layer '%#' - using layer 0", layerName.c_str() );
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
        // 미리보기 씬 정책이 덮어쓰면 그 스텝이다(`setFixedStepOverride`).
        const EngineConfig& engineConfig = engine::getSubsystem<EngineConfig>();
        const uint32        subStepCount = pSettings->_subStepCount;
        const float32       fixedStep    = _fixedStepOverride > 0.0f ? _fixedStepOverride : engineConfig._fixedDeltaTime;
        // 쌓인 시간은 그대로 두고 스텝과 프레임당 상한(스텝 수 × 스텝 길이)만 바꾼다.
        _stepTimer._step         = fixedStep / static_cast<float32>( subStepCount );
        _stepTimer._maxFrameTime = _stepTimer._step * static_cast<float32>( engineConfig._maxFixedStepPerFrame * subStepCount );
        _bConfigured             = SW_TRUE;
    }

    void ScenePhysics::setFixedStepOverride( float32 seconds )
    {
        if ( seconds < 0.0f )
        {
            SW_LOG_ERROR_ID( "ScenePhysics.FixedStepOverrideNegative", "Fixed step override %# is negative - the scene keeps its step", seconds );
            return;
        }
        _fixedStepOverride = seconds;
        _bConfigured       = SW_FALSE; // 다음 스텝 전에 누적기를 이 스텝으로 다시 맞춘다
    }

    IPhysicsScene3D* ScenePhysics::getScene3D()
    {
        if ( _pScene3D == nullptr )
        {
            const PhysicsSystem* pSystem = findInitializedPhysicsSystem();
            if ( pSystem != nullptr )
                _pScene3D = pSystem->createScene3D();
            if ( _pScene3D != nullptr )
            {
                for ( const auto& [objectID, listListener] : _mapStayListener )
                {
                    _pScene3D->setContactStayListener( objectID, true ); // 씬보다 먼저 올린 듣는 쪽
                }
            }
        }
        return _pScene3D.get();
    }

    unique_ptr<IPhysicsScene3D> ScenePhysics::createStaticScene3D() const
    {
        const PhysicsSystem* pSystem = findInitializedPhysicsSystem();
        if ( pSystem == nullptr )
            return nullptr;
        vector<PhysicsBodyDesc3D> listDesc;
        for ( const vector<PhysicsComponent*>& listComponent : _arrListComponent )
        {
            for ( const PhysicsComponent* pComponent : listComponent )
            {
                if ( pComponent->isActive() && pComponent->isPendingDestroy() == false )
                    pComponent->appendStaticBodies3D( *this, listDesc );
            }
        }
        unique_ptr<IPhysicsScene3D> pScene = pSystem->createScene3D();
        if ( pScene != nullptr && listDesc.empty() == false )
        {
            vector<PhysicsBodyHandle> listBody;
            pScene->createBodies( listDesc, listBody );
        }
        return pScene;
    }

    IPhysicsScene2D* ScenePhysics::getScene2D()
    {
        if ( _pScene2D == nullptr )
        {
            const PhysicsSystem* pSystem = findInitializedPhysicsSystem();
            if ( pSystem != nullptr )
                _pScene2D = pSystem->createScene2D();
            if ( _pScene2D != nullptr )
            {
                for ( const auto& [objectID, listListener] : _mapStayListener )
                {
                    _pScene2D->setContactStayListener( objectID, true ); // 씬보다 먼저 올린 듣는 쪽
                }
            }
        }
        return _pScene2D.get();
    }

    void ScenePhysics::addContactStayListener( Component& component, const ContactStayListener& listener )
    {
        const GameObject* pOwner = component.getOwner();
        if ( pOwner == nullptr || pOwner->getObjectID() == 0 )
        {
            SW_LOG_ERROR_ID( "ScenePhysics.StayListenerWithoutObject", "A contact stay listener has no owner object - it would never receive Stay" );
            return;
        }
        if ( listener._pOnCollisionStay == nullptr && listener._pOnOverlapStay == nullptr )
        {
            SW_LOG_ERROR_ID( "ScenePhysics.StayListenerWithoutFunction", "'%#': a contact stay listener has neither a collision nor an overlap function", pOwner->getName().c_str() );
            return;
        }
        vector<ContactStayListenerEntry>& listListener = _mapStayListener[pOwner->getObjectID()];
        for ( const ContactStayListenerEntry& entry : listListener )
        {
            if ( entry._pComponent == &component )
            {
                SW_LOG_ERROR_ID( "ScenePhysics.StayListenerTwice", "'%#': the same component added a contact stay listener twice", pOwner->getName().c_str() );
                return;
            }
        }
        listListener.push_back( ContactStayListenerEntry{ &component, listener } );
        if ( listListener.size() == 1 )
            applyContactStayListener( pOwner->getObjectID(), true );
    }

    void ScenePhysics::removeContactStayListener( Component& component )
    {
        const GameObject* pOwner = component.getOwner();
        if ( pOwner == nullptr )
            return;
        auto iter = _mapStayListener.find( pOwner->getObjectID() );
        if ( iter == _mapStayListener.end() )
            return;
        vector<ContactStayListenerEntry>& listListener = iter->second;
        for ( size_t index = 0; index < listListener.size(); ++index )
        {
            if ( listListener[index]._pComponent != &component )
                continue;
            listListener.erase( listListener.begin() + static_cast<std::ptrdiff_t>( index ) );
            break;
        }
        if ( listListener.empty() == false )
            return;
        const uint64 objectID = iter->first;
        applyContactStayListener( objectID, false );
        _mapStayListener.erase( objectID );
    }

    uint32 ScenePhysics::getContactStayListenerCount( uint64 objectID ) const
    {
        auto iter = _mapStayListener.find( objectID );
        return iter != _mapStayListener.end() ? static_cast<uint32>( iter->second.size() ) : 0u;
    }

    void ScenePhysics::applyContactStayListener( uint64 objectID, bool bListen )
    {
        // 씬은 처음 쓸 때 만든다 — 없으면 만들 때 `_mapStayListener` 를 다시 건다(`getScene3D` · `getScene2D`).
        if ( _pScene3D != nullptr )
            _pScene3D->setContactStayListener( objectID, bListen );
        if ( _pScene2D != nullptr )
            _pScene2D->setContactStayListener( objectID, bListen );
    }

    void ScenePhysics::registerComponent( PhysicsComponent* pComponent )
    {
        if ( pComponent == nullptr || pComponent->_physicsIndex != PhysicsComponent::kNotRegistered )
            return;
        vector<PhysicsComponent*>& listComponent = _arrListComponent[static_cast<uint32>( pComponent->_phase )];
        pComponent->_physicsIndex                = static_cast<uint32>( listComponent.size() );
        listComponent.push_back( pComponent );
        _bFrameHookListStale = SW_TRUE;
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
        _bFrameHookListStale      = SW_TRUE; // 스텝 훅 목록이 지운 컴포넌트를 가리키지 않게 다음 사용 전에 다시 거른다
    }

    void ScenePhysics::setHookBatchEnabled( bool bEnabled )
    {
        _bHookBatchEnabled   = bEnabled ? SW_TRUE : SW_FALSE;
        _bFrameHookListStale = SW_TRUE;
    }

    const PhysicsHookRegistry::Entry* ScenePhysics::findHookEntry( const TypeInfo* pType )
    {
        if ( _pHookRegistry == nullptr || pType == nullptr )
            return nullptr;
        const auto found = _mapHookEntry.find( pType );
        if ( found != _mapHookEntry.end() )
            return found->second;
        const PhysicsHookRegistry::Entry* pEntry = _pHookRegistry->findEntry( pType );
        _mapHookEntry.insert_or_assign( pType, pEntry );
        return pEntry;
    }

    PhysicsHookBatchFunction ScenePhysics::getVirtualHook( PhysicsHookSlot slot )
    {
        // 등록이 없는 타입은 가상 호출이다(기본 구현은 아무것도 하지 않는다). 람다는 `ScenePhysics` 의 멤버 안이라 컴포넌트의 보호된 훅을 부를 수 있다(친구).
        switch ( slot )
        {
            case PhysicsHookSlot::BeginFrame:
                return []( ScenePhysics& physics, PhysicsComponent* const* ppComponent, uint32 count, const PhysicsHookArgs& )
                {
                    for ( uint32 index = 0; index < count; ++index )
                        ppComponent[index]->beginPhysicsFrame( physics );
                };
            case PhysicsHookSlot::PreStep:
                return []( ScenePhysics& physics, PhysicsComponent* const* ppComponent, uint32 count, const PhysicsHookArgs& args )
                {
                    for ( uint32 index = 0; index < count; ++index )
                        ppComponent[index]->prePhysicsStep( physics, args._fixedDeltaTime, args._stepIndex, args._stepCount );
                };
            case PhysicsHookSlot::PostStep:
                return []( ScenePhysics& physics, PhysicsComponent* const* ppComponent, uint32 count, const PhysicsHookArgs& )
                {
                    for ( uint32 index = 0; index < count; ++index )
                        ppComponent[index]->postPhysicsStep( physics );
                };
            case PhysicsHookSlot::EndFrame:
                return []( ScenePhysics& physics, PhysicsComponent* const* ppComponent, uint32 count, const PhysicsHookArgs& args )
                {
                    for ( uint32 index = 0; index < count; ++index )
                        ppComponent[index]->endPhysicsFrame( physics, args._alpha );
                };
            case PhysicsHookSlot::Count:
                break;
        }
        SW_FATAL( "ScenePhysics: unknown physics hook slot %#", static_cast<uint32>( slot ) );
        return nullptr;
    }

    void ScenePhysics::refreshFrameHookLists()
    {
        // 훅 표가 바뀌었으면(모듈 적재 · 해제) 기억한 줄을 버린다 — 내려간 모듈의 함수를 들고 있으면 안 된다.
        const uint32 generation = _pHookRegistry != nullptr ? _pHookRegistry->getGeneration() : 0;
        if ( generation != _hookGeneration )
        {
            _hookGeneration = generation;
            _mapHookEntry.clear();
            _bFrameHookListStale = SW_TRUE;
        }
        if ( _bFrameHookListStale == SW_FALSE )
            return;
        _bFrameHookListStale = SW_FALSE;
        for ( uint32 hookIndex = 0; hookIndex < ScenePhysicsInternal::kHookCount; ++hookIndex )
        {
            const PhysicsHookBatchFunction pVirtualHook = getVirtualHook( static_cast<PhysicsHookSlot>( hookIndex ) );
            for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
            {
                vector<PhysicsComponent*>& listHook = _arrListFrameHook[hookIndex][phase];
                vector<FrameHookRun>&      listRun  = _arrListFrameHookRun[hookIndex][phase];
                listHook.clear();
                listRun.clear();
                const TypeInfo* pRunType = nullptr;
                for ( PhysicsComponent* pComponent : _arrListComponent[phase] )
                {
                    const TypeInfo*                   pType     = pComponent->findCachedTypeInfo();
                    const PhysicsHookRegistry::Entry* pEntry    = _bHookBatchEnabled ? findHookEntry( pType ) : nullptr;
                    const PhysicsHookBatchFunction    pFunction = pEntry != nullptr ? pEntry->_arrFunction[hookIndex] : pVirtualHook;
                    if ( pFunction == nullptr )
                        continue; // 그 타입은 이 훅을 재정의하지 않았다(`PhysicsHooksOf`)
                    // 단계 목록에서 이어진 같은 타입만 한 구간이다(차례를 지킨다). 가상 호출 구간은 타입이 섞여도 된다.
                    const bool bContiguous = listRun.empty() == false && listRun.back()._start + listRun.back()._count == static_cast<uint32>( listHook.size() );
                    const bool bExtend     = bContiguous && listRun.back()._pFunction == pFunction && ( pFunction == pVirtualHook || pRunType == pType );
                    if ( bExtend )
                        ++listRun.back()._count;
                    else
                        listRun.push_back( FrameHookRun{ pFunction, static_cast<uint32>( listHook.size() ), 1 } );
                    pRunType = pType;
                    listHook.push_back( pComponent );
                }
            }
        }
    }

    void ScenePhysics::runFrameHook( PhysicsHookSlot slot, uint32 phase, const PhysicsHookArgs& args )
    {
        const uint32                     hookIndex = static_cast<uint32>( slot );
        const vector<PhysicsComponent*>& listHook  = _arrListFrameHook[hookIndex][phase];
        for ( const FrameHookRun& run : _arrListFrameHookRun[hookIndex][phase] )
        {
            run._pFunction( *this, listHook.data() + run._start, run._count, args );
        }
    }

    void ScenePhysics::bindBodyOwner( RigidBodyComponent& owner )
    {
        ScenePhysicsInternal::bindOwner( _listBodyPoseSlot3D, owner );
    }

    void ScenePhysics::bindBodyOwner( RigidBody2DComponent& owner )
    {
        ScenePhysicsInternal::bindOwner( _listBodyPoseSlot2D, owner );
    }

    void ScenePhysics::unbindBodyOwner( const RigidBodyComponent& owner )
    {
        ScenePhysicsInternal::unbindOwner( _listBodyPoseSlot3D, owner );
    }

    void ScenePhysics::unbindBodyOwner( const RigidBody2DComponent& owner )
    {
        ScenePhysicsInternal::unbindOwner( _listBodyPoseSlot2D, owner );
    }

    template <typename TDimension>
    void ScenePhysics::readMovedBodies( const vector<uint32>& listPoseSlot, const IPhysicsBodyScene<TDimension>& scene, PoseReadBuffer<TDimension>& buffer )
    {
        using Store  = PhysicsPoseStore::Store<TDimension>;
        using Page   = PhysicsBodyPage<TDimension>;
        Store& store = engine::getSubsystem<PhysicsPoseStore>().getStore<TDimension>();
        buffer._listBody.clear();
        buffer._listPoseSlot.clear();
        for ( const PhysicsBodyHandle body : scene.getMovedBodies() )
        {
            const uint32 index    = body.getSlot().index();
            const uint32 poseSlot = index < listPoseSlot.size() ? listPoseSlot[index] : PhysicsPoseStore::kInvalidSlot;
            if ( poseSlot == PhysicsPoseStore::kInvalidSlot )
                continue;
            // 칸 번호가 같아도 세대가 다르면 다른 바디다(파괴 · 래그돌이 만든 주인 없는 바디도 이 목록에 든다).
            const Page* pPage = store.findPage( poseSlot );
            if ( pPage != nullptr && pPage->_arrBody[poseSlot & Page::kSlotMask] == body )
            {
                buffer._listBody.push_back( body );
                buffer._listPoseSlot.push_back( poseSlot );
            }
        }
        const uint32 count = static_cast<uint32>( buffer._listBody.size() );
        if ( count == 0 )
            return;
        buffer._listState.resize( count );

        // 구간마다 백엔드 한 번(잠그지 않는 일괄 읽기) + 강체 칸 쓰기다 — 워커는 컨테이너를 만지지 않고 포인터만 받고, 컴포넌트를 거치지 않는다.
        struct PoseReadJob
        {
            const IPhysicsBodyScene<TDimension>* _pScene{ nullptr };
            const Store*                         _pStore{ nullptr };
            const PhysicsBodyHandle*             _pBody{ nullptr };
            const uint32*                        _pPoseSlot{ nullptr };
            PhysicsBodyState<TDimension>*        _pState{ nullptr };
            uint64                               _stepCount{ 0 };

            void readRange( uint32 start, uint32 end )
            {
                const size_t rangeCount = static_cast<size_t>( end - start );
                _pScene->readBodyStates( span<const PhysicsBodyHandle>{ _pBody + start, rangeCount }, span<PhysicsBodyState<TDimension>>{ _pState + start, rangeCount } );
                for ( uint32 index = start; index < end; ++index )
                {
                    const uint32 poseSlot = _pPoseSlot[index];
                    _pStore->findPage( poseSlot )->applySteppedPose( poseSlot & Page::kSlotMask, _pState[index], _stepCount );
                }
            }
        };
        PoseReadJob job{};
        job._pScene    = &scene;
        job._pStore    = &store;
        job._pBody     = buffer._listBody.data();
        job._pPoseSlot = buffer._listPoseSlot.data();
        job._pState    = buffer._listState.data();
        job._stepCount = _stepCount;
        engine::runParallelByCost( count, SW_DELEGATE_METHOD( ParallelBlockDelegate, &PoseReadJob::readRange, &job ) );
    }

    void ScenePhysics::queuePoseWrite( PhysicsComponent& writer, const float3& position, const quaternion& rotation, const float3& scale )
    {
        _listPoseWrite.push_back( PoseWrite{ &writer, position, rotation, scale } );
    }

    void ScenePhysics::applyPoseWrites()
    {
        if ( _listPoseWrite.empty() )
            return;
        // 부모 없는 쓰기(동적 바디 대부분)는 월드가 곧 로컬이다 — 한 번에 적용한다(`writeWorldPose` 의 루트 길과 같은 값: 자리 · 오일러 각 · 배율).
        _listTransformWrite.clear();
        for ( const PoseWrite& poseWrite : _listPoseWrite )
        {
            if ( poseWrite._pWriter->getParent() != nullptr )
                continue;
            SceneTransformWrite write{};
            write._handle        = poseWrite._pWriter->getHandle();
            write._localPosition = poseWrite._position;
            write._localRotation = poseWrite._rotation.getEulerAngles();
            write._localScale    = poseWrite._scale;
            write._bSetPosition  = SW_TRUE;
            write._bSetRotation  = SW_TRUE;
            write._bSetScale     = SW_TRUE;
            _listTransformWrite.push_back( write );
        }
        if ( _listTransformWrite.empty() == false )
            (void)_manager.applyTransformBatch( _listTransformWrite.data(), static_cast<uint32>( _listTransformWrite.size() ) );
        // 펼친 뒤에 부모 있는 쓰기를 푼다(부모 바디의 이번 자리 위) — 변경 번호도 펼친 뒤 읽어야 컴포넌트마다 월드를 따로 합성하지 않는다.
        if ( _manager.hasDirtySceneTransforms() )
            _manager.flushSceneTransforms();
        for ( const PoseWrite& poseWrite : _listPoseWrite )
        {
            if ( poseWrite._pWriter->getParent() != nullptr )
                PhysicsComponent::writeWorldPose( *poseWrite._pWriter, poseWrite._position, poseWrite._rotation, poseWrite._scale );
            poseWrite._pWriter->markTransformSynced();
        }
        _listPoseWrite.clear();
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

    void ScenePhysics::step( float32 deltaTime )
    {
        _listFrameEvent3D.clear();
        _listFrameEvent2D.clear();
        _listSweptOverlap2D.clear();
        // 물리 컴포넌트도 바디도 없는 씬은 아무것도 하지 않는다(시간도 쌓지 않는다 — 처음 바디가 생긴 프레임이 따라잡느라 여러 스텝을 돌지 않게).
        const bool bHasJobs = getComponentCount() != 0 || _pScene3D != nullptr || _pScene2D != nullptr;
        if ( bHasJobs == false )
            return;
        SW_MEMORY_SCOPE( Physics );
        configureOnce();

        // 바디 → 관절 → 캐릭터. 콜백이 컴포넌트를 더하거나 뺄 수는 없다(구조 변경은 이 밖에서 난다). 훅 사이마다 구간이 낡았는지 본다.
        // 훅은 그 훅을 재정의한 타입만 타입별 일괄 함수로 부른다(`PhysicsHookRegistry` — 차례는 단계 목록 그대로).
        refreshFrameHookLists();
        PhysicsHookArgs args{};
        for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
        {
            runFrameHook( PhysicsHookSlot::BeginFrame, phase, args );
        }
        sweepContinuousColliders2D();

        const uint32  stepCount = static_cast<uint32>( _stepTimer.consume( deltaTime ) );
        const float32 fixedStep = _stepTimer.getStep();
        for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            refreshFrameHookLists();
            args._fixedDeltaTime = fixedStep;
            args._stepIndex      = stepIndex;
            args._stepCount      = stepCount;
            for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
            {
                runFrameHook( PhysicsHookSlot::PreStep, phase, args );
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
            // 자세는 이 스텝에 깨어 있던 바디의 주인만 읽는다 — 정적 · 잠든 바디마다 백엔드 세 번(자세 · 두 속도)을 부르지 않는다.
            if ( _pScene3D != nullptr )
                readMovedBodies( _listBodyPoseSlot3D, static_cast<const IPhysicsBodyScene3D&>( *_pScene3D ), _poseRead3D );
            if ( _pScene2D != nullptr )
                readMovedBodies( _listBodyPoseSlot2D, static_cast<const IPhysicsBodyScene2D&>( *_pScene2D ), _poseRead2D );
            refreshFrameHookLists();
            for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
            {
                runFrameHook( PhysicsHookSlot::PostStep, phase, args );
            }
        }

        args._alpha = _stepTimer.getAlpha();
        refreshFrameHookLists();
        for ( uint32 phase = 0; phase < ScenePhysicsInternal::kPhaseCount; ++phase )
        {
            runFrameHook( PhysicsHookSlot::EndFrame, phase, args );
        }
        applyPoseWrites();
        // 이벤트는 여기서 나눠 주지 않는다 — `FlushPostPhysics` 단계의 `ScenePhysicsEvents` 가 부른다(파일 머리말).
        _bFrameEventsPending = SW_TRUE;
    }

    void ScenePhysics::deliverFrameEvents()
    {
        if ( _bFrameEventsPending == SW_FALSE )
            return;
        _bFrameEventsPending = SW_FALSE;
        dispatchEvents( _manager );
        // 처리가 옮긴 트랜스폼을 PostPhysics 틱 전에 펼친다 — 병렬 틱은 더티 월드를 합성하지 않는다.
        if ( _manager.hasDirtySceneTransforms() )
            _manager.flushSceneTransforms();
    }

    ScenePhysicsEvents::ScenePhysicsEvents( GameObjectManager& manager )
        : _manager{ manager }
    {
    }

    span<const utf8* const> ScenePhysicsEvents::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { ScenePhysics::kSubsystemName };
        return kArrDependency;
    }

    void ScenePhysicsEvents::update( float32 deltaTime )
    {
        (void)deltaTime;
        _manager.getSubsystem<ScenePhysics>().deliverFrameEvents();
    }

    void ScenePhysics::sweepContinuousColliders2D()
    {
        _listMovedCollider2D.clear();
        for ( BoxCollider2DComponent* pCollider : _listCollider2D )
        {
            if ( pCollider->hasMovedThisFrame() )
                _listMovedCollider2D.push_back( pCollider );
        }
        // 쓸기는 겹침을 목록에 더할 뿐 콜라이더를 더하거나 빼지 않는다 — 움직인 목록을 그대로 돈다.
        for ( BoxCollider2DComponent* pCollider : _listMovedCollider2D )
        {
            if ( pCollider->isContinuous() )
                pCollider->sweepPassedBodies( *this, _listMovedCollider2D );
        }
    }

    void ScenePhysics::dispatchEvents( GameObjectManager& manager )
    {
        const bool bNoEvent = _listFrameEvent3D.empty() && _listFrameEvent2D.empty() && _listSweptOverlap2D.empty();
        if ( bNoEvent )
            return;
        // 처리가 바디를 만들거나 지워도(다음 프레임의 이벤트가 된다) 이 프레임의 목록은 그대로 돈다 — 베껴 둔다.
        // 멤버에 베껴 용량을 재사용한다(접촉이 있는 프레임마다 지난다). 이 함수는 `step` 끝에서만 불려 다시 들어오지 않는다.
        _listDispatchEvent3D                             = _listFrameEvent3D;
        _listDispatchEvent2D                             = _listFrameEvent2D;
        _listDispatchSwept                               = _listSweptOverlap2D;
        const vector<PhysicsContactEvent3D>& listEvent3D = _listDispatchEvent3D;
        const vector<PhysicsContactEvent2D>& listEvent2D = _listDispatchEvent2D;
        const vector<SweptOverlap2D>&        listSwept   = _listDispatchSwept;
        // 접촉 기록 — 시작 · 끝만 고정 용량 고리에(진단, 유지는 스텝마다 쏟아져 뺀다).
        const auto recordLog = [this]( uint64 objectA, uint64 objectB, const float3& point, const float3& normal, float32 impulse, PhysicsContactPhase phase, bool bOverlap, bool bIs2D )
        {
            if ( phase == PhysicsContactPhase::Stay )
                return;
            _arrContactLog[_contactLogCount % kContactLogCapacity] =
                PhysicsContactLogEntry{ _stepCount, objectA, objectB, point, normal, impulse, phase, static_cast<uint8>( bOverlap ? SW_TRUE : SW_FALSE ), static_cast<uint8>( bIs2D ? SW_TRUE : SW_FALSE ) };
            ++_contactLogCount;
        };
        for ( const PhysicsContactEvent3D& event : listEvent3D )
        {
            recordLog( event._userDataA, event._userDataB, event._point, event._normal, event._impulse, event._phase, event.isOverlap(), false );
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
            recordLog( event._userDataA, event._userDataB, point, normal, event._impulse, event._phase, event.isOverlap(), true );
            const bool bTriggerA = event._bTriggerA == SW_TRUE;
            const bool bTriggerB = event._bTriggerB == SW_TRUE;
            deliverEvent( manager, event._userDataA, event._userDataB, event._bodyA, event._bodyB, point, normal, event._impulse, event._phase, bTriggerA, bTriggerB,
                          event.isOverlap(), true, 1.0f );
            deliverEvent( manager, event._userDataB, event._userDataA, event._bodyB, event._bodyA, point, -normal, event._impulse, event._phase, bTriggerB, bTriggerA,
                          event.isOverlap(), true, 1.0f );
        }
    }

    void ScenePhysics::collectContactLog( vector<PhysicsContactLogEntry>& outListEntry ) const
    {
        outListEntry.clear();
        const uint64 count = _contactLogCount < kContactLogCapacity ? _contactLogCount : kContactLogCapacity;
        for ( uint64 index = _contactLogCount - count; index < _contactLogCount; ++index )
        {
            outListEntry.push_back( _arrContactLog[index % kContactLogCapacity] );
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
        if ( phase == PhysicsContactPhase::Stay )
        {
            deliverStay( selfUserData, pOther, selfBody, otherBody, point, normal, impulse, bSelfTrigger, bOtherTrigger, bOverlap, bIs2D );
            return;
        }
        // 처리가 컴포넌트를 붙이고 뗄 수 있으므로 목록을 베껴 돈다(멤버에 — 이벤트마다 새로 할당하지 않는다, 이 함수는 다시 들어오지 않는다).
        _listDeliverTarget.assign( pSelf->getComponents().begin(), pSelf->getComponents().end() );
        const vector<Component*>& listTarget = _listDeliverTarget;
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
                    case PhysicsContactPhase::End:
                    {
                        pComponent->onOverlapEnd( overlap );
                        break;
                    }
                    case PhysicsContactPhase::Stay:
                    {
                        break; // 유지는 위에서 `deliverStay` 로 갔다
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
                case PhysicsContactPhase::End:
                {
                    pComponent->onCollisionEnd( collision );
                    break;
                }
                case PhysicsContactPhase::Stay:
                {
                    break; // 유지는 위에서 `deliverStay` 로 갔다
                }
            }
        }
    }

    void ScenePhysics::deliverStay( uint64 selfUserData, GameObject* pOther, PhysicsBodyHandle selfBody, PhysicsBodyHandle otherBody, const float3& point,
                                    const float3& normal, float32 impulse, bool bSelfTrigger, bool bOtherTrigger, bool bOverlap, bool bIs2D )
    {
        // 유지는 올린 듣는 쪽만 받는다 — 쌍의 다른 쪽이 들어서 나온 유지면 이 쪽은 없다(감사 S-1).
        auto iter = _mapStayListener.find( selfUserData );
        if ( iter == _mapStayListener.end() )
            return;
        _listStayTarget = iter->second; // 처리가 듣는 쪽을 올리고 내릴 수 있다
        OverlapInfo overlap;
        overlap._pOther        = pOther;
        overlap._selfBody      = selfBody;
        overlap._otherBody     = otherBody;
        overlap._bSelfTrigger  = bSelfTrigger ? SW_TRUE : SW_FALSE;
        overlap._bOtherTrigger = bOtherTrigger ? SW_TRUE : SW_FALSE;
        CollisionInfo collision;
        collision._pOther    = pOther;
        collision._selfBody  = selfBody;
        collision._otherBody = otherBody;
        collision._point     = point;
        collision._normal    = normal;
        collision._impulse   = impulse;
        collision._bIs2D     = bIs2D;
        for ( const ContactStayListenerEntry& entry : _listStayTarget )
        {
            const bool bInactive = entry._pComponent->isPendingDestroy() || entry._pComponent->isSelfActive() == false;
            if ( bInactive )
                continue;
            if ( bOverlap && entry._listener._pOnOverlapStay != nullptr )
                entry._listener._pOnOverlapStay( *entry._pComponent, overlap );
            else if ( bOverlap == false && entry._listener._pOnCollisionStay != nullptr )
                entry._listener._pOnCollisionStay( *entry._pComponent, collision );
        }
    }

    void ScenePhysics::drawDebug( IPhysicsDebugRenderer& renderer ) const
    {
        if ( _pScene3D != nullptr )
            _pScene3D->drawDebug( renderer );
        if ( _pScene2D != nullptr )
            _pScene2D->drawDebug( renderer );
    }

    void ScenePhysics::applyPreviewPolicy( const ScenePreviewPolicy& policy )
    {
        setFixedStepOverride( policy._fixedStepOverride );
    }

    void ScenePhysics::drawDebug( DebugDrawQueue& queue )
    {
        if ( gv_physicsDebugDraw == false )
            return;
        PhysicsDebugQueueRenderer renderer{ queue };
        drawDebug( static_cast<IPhysicsDebugRenderer&>( renderer ) );
    }
} // namespace sw
