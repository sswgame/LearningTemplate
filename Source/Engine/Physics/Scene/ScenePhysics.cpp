#include "pch.h"

#include "Engine/Physics/Scene/ScenePhysics.h"

#include "Core/Concurrency/DevCounter.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/WorkerReadFence.h"
#include "Engine/Graphics/DebugDraw/DebugDrawQueue.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneMoveDispatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Object/GameObject/Service/ScenePreviewPolicy.h"
#include "Engine/Object/GameObject/Service/SceneServiceRegistry.h"
#include "Engine/Object/GameObject/Tick/FixedStepContext.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/Component/BoxCollider2DComponent.h"
#include "Engine/Physics/Component/PhysicsComponent.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Physics/Scene/PhysicsPoseStore.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

namespace sw
{
    namespace
    {
        struct ScenePhysicsInternal
        {
            static constexpr uint32 kPhaseCount = static_cast<uint32>( PhysicsComponentPhase::Count );

            /**
             * @brief 바닥 스윕의 몸을 줄이는 두께(m)입니다(언리얼 바닥 스윕의 캡슐 반지름 줄임).
             * @details 옆 벽에 닿거나 바닥에 살짝 파고든(접촉 여유) 몸이 시작부터 겹쳐, 받쳐 주지 않는 벽을 바닥으로 보거나 바닥을 놓치지 않게 한다.
             */
            static constexpr float32 kFloorSweepSkin = 0.03f;
            /** @brief 줄인 몸이 원래의 이 비율 아래로는 작아지지 않습니다(작은 파편). */
            static constexpr float32 kFloorSweepMinRatio = 0.5f;
            /** @brief 닿은 면의 법선이 위로 이만큼(cos)은 향해야 받쳐 주는 것으로 봅니다 — 벽(수평 법선)은 받쳐 주지 않는다. */
            static constexpr float32 kFloorSupportMinNormalY = 0.15f;

            /** @brief 몸 모양 @p inoutShape 를 축마다 `kFloorSweepSkin` 만큼 줄입니다(자리는 그대로). */
            static void shrinkForFloorSweep( PhysicsShapeDesc3D& inoutShape )
            {
                const auto shrink = []( float32 extent )
                { return MathUtil::max( extent - kFloorSweepSkin, extent * kFloorSweepMinRatio ); };
                inoutShape._halfExtents = float3{ shrink( inoutShape._halfExtents._x ), shrink( inoutShape._halfExtents._y ), shrink( inoutShape._halfExtents._z ) };
                inoutShape._radius      = shrink( inoutShape._radius );
                if ( inoutShape._listPoint.empty() )
                    return;
                float3 center{};
                for ( const float3& point : inoutShape._listPoint )
                {
                    center = center + point;
                }
                center        = center * ( 1.0f / static_cast<float32>( inoutShape._listPoint.size() ) );
                float32 reach = 0.0f;
                for ( const float3& point : inoutShape._listPoint )
                {
                    reach = MathUtil::max( reach, float3::getDistance( point, center ) );
                }
                const float32 ratio = reach > 0.0f ? shrink( reach ) / reach : 1.0f;
                for ( float3& point : inoutShape._listPoint )
                {
                    point = center + ( point - center ) * ratio;
                }
            }

            /** @brief @p component 의 몸(@p listShape)을 아래로 @p distance 쓸어 받쳐 줄 충돌(위를 보는 면)이 닿으면 true 입니다. */
            [[nodiscard]] static bool hasFloorBelow( const PhysicsComponent& component, const vector<PhysicsShapeDesc3D>& listShape, const float3& position, const quaternion& rotation,
                                                     const IPhysicsQueryScene3D& scene, uint32 layerMask, float32 distance )
            {
                PhysicsQueryFilter filter{};
                filter._ignoreUserData = component.getOwner() != nullptr ? component.getOwner()->getObjectID() : 0; // 제 몸(바디 · 히트 박스)은 바닥이 아니다
                filter._layerMask      = layerMask;
                vector<PhysicsCastHit3D> listHit;
                for ( PhysicsShapeDesc3D shape : listShape )
                {
                    shrinkForFloorSweep( shape );
                    listHit.clear();
                    // 처음 닿는 것 하나가 아니라 바디마다 본다 — 옆 벽에 먼저 닿아도 그 아래 바닥을 찾는다.
                    (void)scene.shapeCastAll( shape, position, rotation, float3{ 0.0f, -1.0f, 0.0f }, distance + kFloorSweepSkin, filter, listHit );
                    for ( const PhysicsCastHit3D& hit : listHit )
                    {
                        if ( hit._normal._y >= kFloorSupportMinNormalY )
                            return true;
                    }
                }
                return false;
            }

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

    struct ScenePhysics::FloorTracking
    {
        SceneMoveConsumer         _moves;         ///< 바닥을 물을 바디 · 캐릭터의 움직임(스트리밍하는 동안만 듣는다)
        vector<SceneComponent*>   _listMoved{};   ///< 이번 프레임에 움직인 것(재사용)
        vector<PhysicsComponent*> _listMissing{}; ///< 바닥 없는 자리에 있는 것 — 컴포넌트가 자기 자리(`_floorMissingIndex`)를 든다
        uint64                    _serial{ 0 };   ///< 마지막으로 본 바닥 번호(`SceneStreamingSubsystem::getFloorSerial`)
        DevCounter                _checkCount;    ///< `getFloorCheckCount`

        explicit FloorTracking( GameObjectManager& manager )
            : _moves{ manager }
        {
        }
    };

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
        , _listBodyPoseSlot3D{}
        , _listBodyPoseSlot2D{}
        , _listFrameEvent3D{}
        , _listFrameEvent2D{}
        , _listStepEventEnd3D{}
        , _listStepEventEnd2D{}
        , _poseRead3D{}
        , _poseRead2D{}
        , _listPoseWrite{}
        , _listCharacterMove3D{}
        , _listTransformWrite{}
        , _listDispatchEvent3D{}
        , _listDispatchEvent2D{}
        , _listDispatchSwept{}
        , _overlapTracker{}
        , _listSpawnedObject{}
        , _listOverlapTransition{}
        , _listOverlapStay{}
        , _mapStayListener{}
        , _mapStayListenerObject{}
        , _listStayTarget{}
        , _arrContactLog{}
        , _contactLogCount{ 0 }
        , _stepCount{ 0 }
        , _pFloor{ make_unique<FloorTracking>( manager ) }
        , _openReadScopeCount{ 0 }
        , _debrisBudget{}
        , _fixedStepOverride{ 0.0f }
        , _bConfigured{ SW_FALSE }
        , _bFrameEventsPending{ SW_FALSE }
        , _bStreamingHolds{ SW_FALSE }
        , _lastStayListenerID{ 0 }
        , _stayListenerSubscriptions{ make_unique<SubscriptionList>( SW_DELEGATE_METHOD( SubscriptionList::ReleaseFunction, &ScenePhysics::releaseContactStayListener, this ) ) }
    {
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
        updateStreamingHolds();
        step( deltaTime );
    }

    void ScenePhysics::updateStreamingHolds()
    {
        const SceneStreamingSubsystem& streaming  = _manager.getSubsystem<SceneStreamingSubsystem>();
        const bool                     bStreaming = streaming.isStreaming();
        FloorTracking&                 floor      = *_pFloor;
        if ( bStreaming == false )
        {
            if ( _bStreamingHolds == SW_FALSE )
                return;
            // 스트리밍이 끝났다 — 쥔 것을 모두 푼다.
            _bStreamingHolds = SW_FALSE;
            trackFloorMoves( false );
            while ( floor._listMissing.empty() == false )
            {
                removeFloorMissing( *floor._listMissing.back() );
            }
            return;
        }
        // 1. 자리마다의 답을 맞춘다 — 움직인 것만, 셀이 오르내렸으면 모두(처음 듣기 시작한 것은 움직인 것에 든다).
        const uint64 floorSerial = streaming.getFloorSerial();
        const bool   bAll        = _bStreamingHolds == SW_TRUE && floorSerial != floor._serial;
        if ( _bStreamingHolds == SW_FALSE )
            trackFloorMoves( true );
        _bStreamingHolds = SW_TRUE;
        floor._serial    = floorSerial;
        floor._moves.takeMoved( floor._listMoved );
        if ( bAll )
        {
            for ( const PhysicsComponentPhase phase : { PhysicsComponentPhase::Body, PhysicsComponentPhase::Character } )
            {
                for ( PhysicsComponent* pComponent : _arrListComponent[static_cast<uint32>( phase )] )
                {
                    refreshFloorMissing( *pComponent, streaming );
                }
            }
        }
        else
        {
            for ( SceneComponent* pComponent : floor._listMoved )
            {
                refreshFloorMissing( static_cast<PhysicsComponent&>( *pComponent ), streaming ); // 듣는 것은 물리 컴포넌트뿐이다
            }
        }
        if ( floor._listMissing.empty() )
            return;
        // 2. 바닥 없는 자리의 것만 받침을 본다.
        // 바닥 셀이 내려간 후보만 몸 모양을 아래로 쓴다(비탈 · 가장자리에 걸친 큰 바디도 자기 몸으로 본다) — 받쳐 줄 충돌(늘 올라 있는 지형 · 정적 바디)이 몸 아래 가까이 있으면 쥐지 않는다(순간이동으로 남겨진 캐릭터가 땅 위에서 서지 않게).
        // 설정이나 3D 씬이 없으면 받쳐 줄 것을 알 수 없다 — 쥔다(떨어지는 것보다 안전하다).
        const PhysicsSettings* const      pSettings = findSettings();
        const IPhysicsQueryScene3D* const pQuery    = findScene3D();
        vector<PhysicsShapeDesc3D>        listShape;
        for ( PhysicsComponent* pComponent : floor._listMissing )
        {
            const bool bCandidate = pComponent->needsStreamingFloor(); // 강체의 종류는 실행 중에 바뀐다 — 목록은 자리만 본다
            bool       bSupported = false;
            if ( bCandidate && pSettings != nullptr && pQuery != nullptr )
            {
                float3     bodyPosition{};
                quaternion bodyRotation{};
                float3     bodyScale{};
                if ( PhysicsComponent::readWorldPose( *pComponent, bodyPosition, bodyRotation, bodyScale ) )
                {
                    listShape.clear();
                    pComponent->collectStreamingFloorShapes( bodyScale, listShape );
                    bSupported = ScenePhysicsInternal::hasFloorBelow( *pComponent, listShape, bodyPosition, bodyRotation, *pQuery, pSettings->makeGroundQueryLayerMask(),
                                                                      pSettings->_streamingFloorProbeDistance );
                }
            }
            pComponent->_bStreamingHeld = bCandidate && bSupported == false;
        }
    }

    void ScenePhysics::refreshFloorMissing( PhysicsComponent& component, const SceneStreamingSubsystem& streaming )
    {
        _pFloor->_checkCount.add();
        if ( streaming.isFloorMissing( component.getWorldPosition() ) == false )
        {
            removeFloorMissing( component );
            return;
        }
        if ( component._floorMissingIndex != PhysicsComponent::kNotRegistered )
            return;
        component._floorMissingIndex = static_cast<uint32>( _pFloor->_listMissing.size() );
        _pFloor->_listMissing.push_back( &component );
    }

    void ScenePhysics::removeFloorMissing( PhysicsComponent& component )
    {
        component._bStreamingHeld = false;
        const uint32 index        = component._floorMissingIndex;
        if ( index == PhysicsComponent::kNotRegistered )
            return;
        PhysicsComponent* pMoved     = _pFloor->_listMissing.back();
        _pFloor->_listMissing[index] = pMoved;
        pMoved->_floorMissingIndex   = index;
        component._floorMissingIndex = PhysicsComponent::kNotRegistered;
        _pFloor->_listMissing.pop_back();
    }

    void ScenePhysics::trackFloorMoves( bool bTrack )
    {
        for ( const PhysicsComponentPhase phase : { PhysicsComponentPhase::Body, PhysicsComponentPhase::Character } )
        {
            for ( const PhysicsComponent* pComponent : _arrListComponent[static_cast<uint32>( phase )] )
            {
                if ( bTrack )
                    _pFloor->_moves.track( *pComponent );
                else
                    _pFloor->_moves.untrack( pComponent->getTransformSlot(), pComponent );
            }
        }
    }

    const DevCounter& ScenePhysics::getFloorCheckCount() const
    {
        return _pFloor->_checkCount;
    }

    void ScenePhysics::onWorldOriginShifted( const float3& delta )
    {
        if ( _pScene3D != nullptr )
            _pScene3D->shiftOrigin( delta );
        if ( _pScene2D != nullptr )
            _pScene2D->shiftOrigin( float2{ delta._x, delta._y } );
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
        _overlapTracker.clear(); // 씬과 함께 모든 겹침이 사라진다 — 이탈 없이 잊는다(받을 오브젝트도 함께 내려간다)
        _listSpawnedObject.clear();
        _listMovedCollider2D.clear();
        _listPoseWrite.clear();
        _listCharacterMove3D.clear();
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
        const optional<uint8>  layer     = pSettings != nullptr ? pSettings->findLayerIndex( layerName ) : optional<uint8>{};
        if ( layer.has_value() )
            return *layer;
        SW_LOG_ERROR_ID( "ScenePhysics.UnknownPhysicsLayerUsing", "Unknown physics layer '%#' - using layer 0", layerName.c_str() );
        return 0;
    }

    optional<PhysicsQueryFilter> ScenePhysics::makeTraceFilter( const hashed_string& traceChannel ) const
    {
        const PhysicsSettings* pSettings = findSettings();
        if ( pSettings == nullptr )
        {
            SW_LOG_ERROR_ID( "ScenePhysics.TraceWithoutSettings", "Trace channel '%#' asked for but the scene has no physics settings", traceChannel.c_str() );
            return nullopt;
        }
        const optional<uint32> mask = pSettings->makeTraceLayerMask( traceChannel );
        if ( mask.has_value() == false )
            return nullopt;
        PhysicsQueryFilter filter;
        filter._layerMask = *mask;
        return filter;
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

    Subscription ScenePhysics::registerContactStayListener( Component& component, const ContactStayListener& listener )
    {
        const GameObject* pOwner = component.getOwner();
        if ( pOwner == nullptr || pOwner->getObjectID() == 0 )
        {
            SW_LOG_ERROR_ID( "ScenePhysics.StayListenerWithoutObject", "A contact stay listener has no owner object - it would never receive Stay" );
            return Subscription{};
        }
        if ( listener._pOnCollisionStay == nullptr && listener._pOnOverlapStay == nullptr )
        {
            SW_LOG_ERROR_ID( "ScenePhysics.StayListenerWithoutFunction", "'%#': a contact stay listener has neither a collision nor an overlap function", pOwner->getName().c_str() );
            return Subscription{};
        }
        vector<ContactStayListenerEntry>& listListener = _mapStayListener[pOwner->getObjectID()];
        for ( const ContactStayListenerEntry& entry : listListener )
        {
            if ( entry._pComponent == &component )
            {
                SW_LOG_ERROR_ID( "ScenePhysics.StayListenerTwice", "'%#': the same component added a contact stay listener twice", pOwner->getName().c_str() );
                return Subscription{};
            }
        }
        const uint64 listenerID = ++_lastStayListenerID;
        listListener.push_back( ContactStayListenerEntry{ &component, listener, listenerID } );
        _mapStayListenerObject[listenerID] = pOwner->getObjectID();
        if ( listListener.size() == 1 )
            applyContactStayListener( pOwner->getObjectID(), true );
        return _stayListenerSubscriptions->issue( listenerID );
    }

    void ScenePhysics::releaseContactStayListener( uint64 listenerID )
    {
        const auto objectIter = _mapStayListenerObject.find( listenerID );
        SW_ASSERT( objectIter != _mapStayListenerObject.end() ); // 구독이 살아 있는 동안 등록도 있다
        auto iter = _mapStayListener.find( objectIter->second );
        _mapStayListenerObject.erase( objectIter );
        SW_ASSERT( iter != _mapStayListener.end() );
        vector<ContactStayListenerEntry>& listListener = iter->second;
        for ( size_t index = 0; index < listListener.size(); ++index )
        {
            if ( listListener[index]._listenerID != listenerID )
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
        if ( _bStreamingHolds == SW_TRUE && pComponent->_phase != PhysicsComponentPhase::Joint )
            _pFloor->_moves.track( *pComponent );
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
        removeFloorMissing( *pComponent );
        _pFloor->_moves.untrack( pComponent->getTransformSlot(), pComponent );
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

        // 구간마다 백엔드 한 번(잠그지 않는 일괄 읽기) + 강체 칸 쓰기다 — 워커는 컨테이너를 만지지 않고, 입력은 const 포인터로 읽고, 상태 출력은
        // `runParallelRangeInto` 가 건넨 제 구간 조각에만 쓴다. 컴포넌트를 거치지 않는다.
        struct PoseReadJob
        {
            const IPhysicsBodyScene<TDimension>* _pScene{ nullptr };
            const Store*                         _pStore{ nullptr };
            const PhysicsBodyHandle*             _pBody{ nullptr };
            const uint32*                        _pPoseSlot{ nullptr };
            uint64                               _stepCount{ 0 };

            void operator()( uint32 start, uint32 end, span<PhysicsBodyState<TDimension>> outListState ) const
            {
                _pScene->readBodyStates( span<const PhysicsBodyHandle>{ _pBody + start, static_cast<size_t>( end - start ) }, outListState );
                for ( uint32 index = start; index < end; ++index )
                {
                    const uint32 poseSlot = _pPoseSlot[index];
                    _pStore->findPage( poseSlot )->applySteppedPose( poseSlot & Page::kSlotMask, outListState[index - start], _stepCount );
                }
            }
        };
        PoseReadJob job{};
        job._pScene    = &scene;
        job._pStore    = &store;
        job._pBody     = buffer._listBody.data();
        job._pPoseSlot = buffer._listPoseSlot.data();
        job._stepCount = _stepCount;
        engine::runParallelRangeIntoByCost( count, job, span<PhysicsBodyState<TDimension>>( buffer._listState ) );
    }

    void ScenePhysics::queuePoseWrite( PhysicsComponent& writer, const float3& position, const quaternion& rotation, const float3& scale )
    {
        _listPoseWrite.push_back( PoseWrite{ &writer, position, rotation, scale } );
    }

    void ScenePhysics::queueCharacterMove( PhysicsCharacterHandle character, const float3& velocity )
    {
        _listCharacterMove3D.push_back( PhysicsCharacterMove3D{ character, velocity } );
    }

    void ScenePhysics::flushCharacterMoves( float32 fixedDeltaTime )
    {
        if ( _listCharacterMove3D.empty() )
            return;
        SW_PROFILE_SCOPE( "GT.Physics.moveCharacters" ); // 부하 시나리오 `load/charactermove` 가 워커 수별로 잰다(감사 S-8)
        if ( _pScene3D != nullptr )
            _pScene3D->moveCharacters( _listCharacterMove3D, fixedDeltaTime );
        _listCharacterMove3D.clear();
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

    PhysicsReadScope::PhysicsReadScope( ScenePhysics& owner, const IPhysicsQueryScene3D* pQuery3D, const IPhysicsQueryScene2D* pQuery2D )
        : _pOwner{ &owner }
        , _pQuery3D{ pQuery3D }
        , _pQuery2D{ pQuery2D }
    {
    }

    PhysicsReadScope::PhysicsReadScope( PhysicsReadScope&& other ) noexcept
        : _pOwner{ other._pOwner }
        , _pQuery3D{ other._pQuery3D }
        , _pQuery2D{ other._pQuery2D }
    {
        other._pOwner   = nullptr;
        other._pQuery3D = nullptr;
        other._pQuery2D = nullptr;
    }

    PhysicsReadScope::~PhysicsReadScope()
    {
        SW_ASSERT( _pOwner == nullptr ); // 워커를 기다린 뒤 `ScenePhysics::closeReadScope` 로 닫는다 — 버리면 스텝이 영영 막힌다
    }

    PhysicsReadScope ScenePhysics::openReadScope()
    {
        ++_openReadScopeCount;
        return PhysicsReadScope{ *this, _pScene3D.get(), _pScene2D.get() };
    }

    WorkerReadFence<ScenePhysics> ScenePhysics::closeReadScope( PhysicsReadScope&& scope )
    {
        const bool bOwned = scope._pOwner == this && _openReadScopeCount > 0;
        SW_ASSERT( bOwned ); // 다른 씬의 표이거나 이미 닫았다
        if ( bOwned )
            --_openReadScopeCount;
        scope._pOwner   = nullptr;
        scope._pQuery3D = nullptr;
        scope._pQuery2D = nullptr;
        return WorkerReadFence<ScenePhysics>{};
    }

    WorkerReadFence<ScenePhysics> ScenePhysics::makeWriteFence() const
    {
        SW_ASSERT( _openReadScopeCount == 0 ); // 워커가 질의 창으로 읽는 동안 스텝을 돌리려 했다 — 구간을 먼저 닫는다
        return WorkerReadFence<ScenePhysics>{};
    }

    void ScenePhysics::stepWorlds( const WorkerReadFence<ScenePhysics>& /*fence*/, float32 fixedStep )
    {
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
        _listStepEventEnd3D.push_back( _listFrameEvent3D.size() );
        _listStepEventEnd2D.push_back( _listFrameEvent2D.size() );
    }

    void ScenePhysics::step( float32 deltaTime )
    {
        _listFrameEvent3D.clear();
        _listFrameEvent2D.clear();
        _listStepEventEnd3D.clear();
        _listStepEventEnd2D.clear();
        _listSweptOverlap2D.clear();
        // 물리 컴포넌트도 바디도 없는 씬은 아무것도 하지 않는다(시간도 쌓지 않는다 — 처음 바디가 생긴 프레임이 따라잡느라 여러 스텝을 돌지 않게).
        const bool bHasJobs = getComponentCount() != 0 || _pScene3D != nullptr || _pScene2D != nullptr;
        if ( bHasJobs == false )
            return;
        configureOnce();

        // 고정 스텝 훅은 틱 시스템의 고정 스텝 그룹이 부른다(`SceneTickScheduler::tickFixedStepGroup` — 컴포넌트가 onRegister 에서 등록한 메서드만 메서드별 일괄 함수, 차례는 단계 바디 → 관절 → 캐릭터).
        // 콜백이 컴포넌트를 더하거나 뺄 수는 없다(구조 변경은 이 밖에서 난다). 그룹마다 구간이 낡았는지 본다.
        (void)_manager.tickFixedStepGroup( FixedStepGroup::FrameBegin, FixedStepContext{ *this, deltaTime, 0.0f, 0, 0, 0.0f } );
        sweepContinuousColliders2D();

        const uint32  stepCount = static_cast<uint32>( _stepTimer.consume( deltaTime ) );
        const float32 fixedStep = _stepTimer.getStep();
        for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            const FixedStepContext stepContext{ *this, deltaTime, fixedStep, stepIndex, stepCount, 0.0f };
            (void)_manager.tickFixedStepGroup( FixedStepGroup::PreStep, stepContext );
            flushCharacterMoves( fixedStep ); // 캐릭터는 바디 · 관절이 이번 스텝 목표를 정한 뒤, 월드 스텝 앞에 움직인다(예전 차례 그대로)
            stepWorlds( makeWriteFence(), fixedStep );
            ++_stepCount;
            // 자세는 이 스텝에 깨어 있던 바디의 주인만 읽는다 — 정적 · 잠든 바디마다 백엔드 세 번(자세 · 두 속도)을 부르지 않는다.
            if ( _pScene3D != nullptr )
                readMovedBodies( _listBodyPoseSlot3D, static_cast<const IPhysicsBodyScene3D&>( *_pScene3D ), _poseRead3D );
            if ( _pScene2D != nullptr )
                readMovedBodies( _listBodyPoseSlot2D, static_cast<const IPhysicsBodyScene2D&>( *_pScene2D ), _poseRead2D );
            (void)_manager.tickFixedStepGroup( FixedStepGroup::PostStep, stepContext );
        }

        // 파편 예산은 씬 전체에서 한 번 고른다 — 프레임 끝 훅에서 주인마다 자기 몫만 지운다(먼저 틱한 오브젝트가 값을 치르지 않는다, 감사 S-11).
        if ( const PhysicsSettings* pSettings = findSettings(); pSettings != nullptr )
            _debrisBudget.planRemovals( pSettings->_maxDebrisBodyCount );
        (void)_manager.tickFixedStepGroup( FixedStepGroup::FrameEnd, FixedStepContext{ *this, deltaTime, fixedStep, 0, stepCount, _stepTimer.getAlpha() } );
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
        // 첫 스텝을 지난 오브젝트의 나타남 표시는 이 나눠 주기로 끝났다 — 스텝이 없던 프레임(높은 프레임률)은 표시를 그대로 둔다.
        VectorUtil::removeIf( _listSpawnedObject, [this]( const SpawnedObject& spawned )
        { return spawned._stepCount < _stepCount; } );
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
        const bool bNoEvent  = _listFrameEvent3D.empty() && _listFrameEvent2D.empty() && _listSweptOverlap2D.empty();
        const bool bWantStay = _mapStayListener.empty() == false; // 겹침 유지는 이벤트가 없는 프레임에도 안에 있는 쌍마다 난다
        if ( bNoEvent && bWantStay == false )
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
        // 겹침은 바디 쌍으로 나누지 않는다 — 추적기가 오브젝트 쌍으로 세어 아래에서 한 번에 낸다(트리거 계약: 몸이 여럿이어도 한 번, 이탈 먼저).
        const auto recordOverlap = [this]( uint64 objectA, uint64 objectB, PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB, bool bTriggerA, bool bTriggerB, bool bIs2D,
                                           float32 hitFraction, PhysicsContactPhase phase, uint64 step )
        { _overlapTracker.record( PhysicsOverlapEvent{ objectA, objectB, bodyA, bodyB, hitFraction, phase, bTriggerA, bTriggerB, bIs2D, step } ); };
        // 알림이 온 스텝 — 쌓은 차례가 스텝 차례라 경계 목록으로 가른다.
        const uint64 firstStep  = _stepCount + 1u - static_cast<uint64>( _listStepEventEnd3D.size() );
        size_t       stepIndex  = 0;
        size_t       eventIndex = 0;
        for ( const PhysicsContactEvent3D& event : listEvent3D )
        {
            while ( _listStepEventEnd3D[stepIndex] <= eventIndex )
                ++stepIndex;
            ++eventIndex;
            recordLog( event._userDataA, event._userDataB, event._point, event._normal, event._impulse, event._phase, event.isOverlap(), false );
            const bool bTriggerA = event._bTriggerA == SW_TRUE;
            const bool bTriggerB = event._bTriggerB == SW_TRUE;
            if ( event.isOverlap() )
            {
                recordOverlap( event._userDataA, event._userDataB, event._bodyA, event._bodyB, bTriggerA, bTriggerB, false, 1.0f, event._phase, firstStep + stepIndex );
                continue;
            }
            deliverEvent( manager, event._userDataA, event._userDataB, event._bodyA, event._bodyB, event._point, event._normal, event._impulse, event._phase,
                          bTriggerA, bTriggerB, false, false, 1.0f, false );
            deliverEvent( manager, event._userDataB, event._userDataA, event._bodyB, event._bodyA, event._point, -event._normal, event._impulse, event._phase,
                          bTriggerB, bTriggerA, false, false, 1.0f, false );
        }
        // 쓸고 지나간 겹침이 먼저다 — 이번 프레임 길 위에서 닿았으므로 지금 자리의 겹침(센서 시작)보다 이르다. 지나쳤으므로 시작과 끝을 함께 센다
        // (그 사이 센서가 같은 쌍의 시작을 내면 쌍은 안에 남아 진입만 난다).
        for ( const SweptOverlap2D& swept : listSwept )
        {
            // 길의 시작에서 이미 겹쳤으면 첫 스텝의 겹침이다 — 길 위에서 닿은 것은 걸어 든 것이라 스텝이 없다(나타남이 아니다).
            const uint64 sweptStep = swept._hitFraction <= 0.0f ? firstStep : 0u;
            for ( const PhysicsContactPhase phase : { PhysicsContactPhase::Begin, PhysicsContactPhase::End } )
            {
                recordOverlap( swept._selfUserData, swept._otherUserData, swept._selfBody, swept._otherBody, swept._bSelfTrigger, swept._bOtherTrigger, true, swept._hitFraction, phase, sweptStep );
            }
        }
        stepIndex  = 0;
        eventIndex = 0;
        for ( const PhysicsContactEvent2D& event : listEvent2D )
        {
            while ( _listStepEventEnd2D[stepIndex] <= eventIndex )
                ++stepIndex;
            ++eventIndex;
            const float3 point{ event._point._x, event._point._y, 0.0f };
            const float3 normal{ event._normal._x, event._normal._y, 0.0f };
            recordLog( event._userDataA, event._userDataB, point, normal, event._impulse, event._phase, event.isOverlap(), true );
            const bool bTriggerA = event._bTriggerA == SW_TRUE;
            const bool bTriggerB = event._bTriggerB == SW_TRUE;
            if ( event.isOverlap() )
            {
                recordOverlap( event._userDataA, event._userDataB, event._bodyA, event._bodyB, bTriggerA, bTriggerB, true, 1.0f, event._phase, firstStep + stepIndex );
                continue;
            }
            deliverEvent( manager, event._userDataA, event._userDataB, event._bodyA, event._bodyB, point, normal, event._impulse, event._phase, bTriggerA, bTriggerB, false,
                          true, 1.0f, false );
            deliverEvent( manager, event._userDataB, event._userDataA, event._bodyB, event._bodyA, point, -normal, event._impulse, event._phase, bTriggerB, bTriggerA, false,
                          true, 1.0f, false );
        }

        // 겹침 진입 · 이탈(이탈 먼저) → 유지. 처리가 오브젝트를 지우거나 만들어도 결과 목록은 멤버 사본이라 흔들리지 않는다(새 겹침은 다음 프레임 알림이 된다).
        _overlapTracker.finishFrame( _listOverlapTransition, _listOverlapStay, bWantStay );
        const auto deliverOverlap = [this, &manager]( const PhysicsOverlapEvent& event )
        {
            const float3 zero{};
            // 진입이 상대 첫 바디의 첫 스텝에서 왔으면 "안에서 생김" 이다(`markObjectSpawned`) — 받는 쪽이 프레임 수 창 없이 나타남을 가린다.
            const bool bBegin = event._phase == PhysicsContactPhase::Begin;
            deliverEvent( manager, event._objectA, event._objectB, event._bodyA, event._bodyB, zero, zero, 0.0f, event._phase, event._bTriggerA, event._bTriggerB, true,
                          event._bIs2D, event._hitFraction, bBegin && isSpawnedInStep( event._objectB, event._step ) );
            if ( event._objectA != event._objectB ) // 자기 몸과 자기 트리거의 겹침은 한 번만 받는다
                deliverEvent( manager, event._objectB, event._objectA, event._bodyB, event._bodyA, zero, zero, 0.0f, event._phase, event._bTriggerB, event._bTriggerA,
                              true, event._bIs2D, event._hitFraction, bBegin && isSpawnedInStep( event._objectA, event._step ) );
        };
        for ( const PhysicsOverlapEvent& event : _listOverlapTransition )
        {
            deliverOverlap( event );
        }
        for ( const PhysicsOverlapEvent& event : _listOverlapStay )
        {
            deliverOverlap( event );
        }
    }

    void ScenePhysics::markObjectSpawned( uint64 objectID )
    {
        _listSpawnedObject.push_back( SpawnedObject{ objectID, _stepCount } );
    }

    bool ScenePhysics::isSpawnedInStep( uint64 objectID, uint64 step ) const
    {
        for ( const SpawnedObject& spawned : _listSpawnedObject )
        {
            if ( spawned._objectID == objectID && spawned._stepCount + 1u == step && step <= _stepCount )
                return true;
        }
        return false;
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
                                     bool bOverlap, bool bIs2D, float32 hitFraction, bool bOtherSpawnedInside )
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
        // 듣는 컴포넌트만 받는다(`GameObjectEvents` — 구독 · 해지 · 스폰 · 파괴를 처리 안에서 해도 된다). 유지는 위에서 `deliverStay` 로 갔다.
        const bool bBegin = phase == PhysicsContactPhase::Begin;
        if ( bOverlap )
        {
            OverlapInfo overlap;
            overlap._pOther              = pOther;
            overlap._selfBody            = selfBody;
            overlap._otherBody           = otherBody;
            overlap._hitFraction         = hitFraction;
            overlap._bSelfTrigger        = bSelfTrigger ? SW_TRUE : SW_FALSE;
            overlap._bOtherTrigger       = bOtherTrigger ? SW_TRUE : SW_FALSE;
            overlap._bOtherSpawnedInside = bOtherSpawnedInside ? SW_TRUE : SW_FALSE;
            GameObjectEvents::broadcast( *pSelf, bBegin ? &GameObjectEvents::_overlapBegin : &GameObjectEvents::_overlapEnd, overlap );
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
        GameObjectEvents::broadcast( *pSelf, bBegin ? &GameObjectEvents::_collisionBegin : &GameObjectEvents::_collisionEnd, collision );
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
} // namespace sw
