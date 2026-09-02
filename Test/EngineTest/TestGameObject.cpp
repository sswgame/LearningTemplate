#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectPtr.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionTypes.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 0) 헬퍼 — 모의 컴포넌트 TypeInfo·팩토리
// ------------------------------------------------------------------------------

namespace sw
{
    namespace
    {
        /** @brief 테스트 전용 TypeInfo 를 만들거나 캐시에서 반환합니다. */
        const TypeInfo* makeMockComponentTypeInfo( hashed_string shortName, hashed_string fqn, size_t size, hashed_string parentFqn = hashed_string{} )
        {
            // 테스트 로컬 TypeInfo (RTTI 없음). 키는 ComponentManager 팩토리 등록과 일치합니다.
            static mutex                                  s_mutex;
            std::lock_guard<mutex>                        lock( s_mutex );
            static unordered_map<hashed_string, TypeInfo> s_types;
            auto                                          it = s_types.find( shortName );
            if ( it != s_types.end() )
                return &it->second;

            TypeInfo info{};
            info._name               = shortName;
            info._typeId             = static_cast<uint32>( shortName.getHash() );
            info._fullyQualifiedName = fqn;
            info._parentFQN          = parentFqn;
            info._size               = size;
            it                       = s_types.emplace( shortName, std::move( info ) ).first;
            if ( engine::areEngineServicesBound() )
                engine::getTypeRegistry().registerClass( it->second );
            return &it->second;
        }

    } // namespace

    class MockMeshComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 테스트 전용 StaticType 을 반환합니다. */
        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        string _meshName = "CubeMesh";
        int32  _tickCount{ 0 };

        GameObjectManager* _pTickDestroyManager{ nullptr };
        GameObject*        _pTickDestroyObject{ nullptr };
        GameObject*        _pTickRemoveOwner{ nullptr };
        Component*         _pTickRemoveComp{ nullptr };
        SceneComponent*    _pTickAttachChild{ nullptr };
        SceneComponent*    _pTickAttachParent{ nullptr };
        SceneComponent*    _pTickMoveComp{ nullptr };
        float3             _tickMovePos{};

        /** @brief 틱마다 _tickCount 를 증가시키고, 설정된 틱 액션을 실행합니다. */
        virtual void onTick( float32 deltaTime ) override
        {
            Component::onTick( deltaTime );
            _tickCount++;
            if ( _pTickMoveComp != nullptr )
                _pTickMoveComp->setLocalPosition( _tickMovePos );
            if ( _pTickDestroyManager != nullptr && _pTickDestroyObject != nullptr )
                _pTickDestroyManager->destroyObject( _pTickDestroyObject );
            if ( _pTickRemoveOwner != nullptr && _pTickRemoveComp != nullptr )
                _pTickRemoveOwner->removeComponent( _pTickRemoveComp );
            if ( _pTickAttachChild != nullptr && _pTickAttachParent != nullptr )
                _pTickAttachChild->attachToComponent( _pTickAttachParent );
        }
    };

    /** @brief MockMeshComponent 의 정적 TypeInfo 를 반환합니다. */
    const TypeInfo* MockMeshComponent::StaticType()
    {
        return makeMockComponentTypeInfo( hashed_string( "MockMeshComponent" ),
                                          hashed_string( "sw::MockMeshComponent" ), sizeof( MockMeshComponent ) );
    }

    class MockAudioComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 테스트 전용 StaticType 을 반환합니다. */
        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        float32 _volume{ 1.0f };
        int32   _playCount{ 0 };

        /** @brief 틱마다 _playCount 를 증가시킵니다. */
        virtual void onTick( float32 deltaTime ) override
        {
            Component::onTick( deltaTime );
            _playCount++;
        }
    };

    /** @brief MockAudioComponent 의 정적 TypeInfo 를 반환합니다. */
    const TypeInfo* MockAudioComponent::StaticType()
    {
        return makeMockComponentTypeInfo( hashed_string( "MockAudioComponent" ),
                                          hashed_string( "sw::MockAudioComponent" ), sizeof( MockAudioComponent ) );
    }

    class MockCallbackComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 테스트 전용 StaticType 을 반환합니다. */
        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        hashed_string _lastChangedProperty;

        /** @brief 변경된 프로퍼티 이름을 기록합니다. */
        virtual void onPropertyChanged( hashed_string propertyName ) override
        {
            Component::onPropertyChanged( propertyName );
            _lastChangedProperty = propertyName;
        }
    };

    /** @brief MockCallbackComponent 의 정적 TypeInfo 를 반환합니다. */
    const TypeInfo* MockCallbackComponent::StaticType()
    {
        return makeMockComponentTypeInfo( hashed_string( "MockCallbackComponent" ),
                                          hashed_string( "sw::MockCallbackComponent" ),
                                          sizeof( MockCallbackComponent ) );
    }

    class MockTickSceneComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        float3*                _pObservedWorld;
        float3                 _tickLocalPos;
        uint8                  _bWriteLocalOnTick : 1;
        [[maybe_unused]] uint8 _reserved          : 7;

        MockTickSceneComponent()
            : _pObservedWorld{ nullptr }
            , _tickLocalPos{}
            , _bWriteLocalOnTick{ SW_FALSE }
            , _reserved{ 0 }
        {
            setCanEverTick( true );
        }

        /** @brief 테스트 전용 StaticType 을 반환합니다. */
        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onTick( float32 deltaTime ) override
        {
            SceneComponent::onTick( deltaTime );
            if ( _pObservedWorld != nullptr )
                *_pObservedWorld = getWorldPosition();
            if ( _bWriteLocalOnTick == SW_TRUE )
                setLocalPosition( _tickLocalPos );
        }
    };

    /** @brief MockTickSceneComponent 의 정적 TypeInfo 를 반환합니다. */
    const TypeInfo* MockTickSceneComponent::StaticType()
    {
        return makeMockComponentTypeInfo( hashed_string( "MockTickSceneComponent" ),
                                          hashed_string( "sw::MockTickSceneComponent" ),
                                          sizeof( MockTickSceneComponent ),
                                          hashed_string( "sw::SceneComponent" ) );
    }

    // ------------------------------------------------------------------------------
    // 다단계 컴포넌트 상속 계층 정의:
    // Component -> SceneComponent -> MockRootComponent -> MockBasePawnComponent -> MockVehicleComponent -> MockFlyingVehicleComponent
    // ------------------------------------------------------------------------------
    class MockRootComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();

        vector<string>* _pTickOrderLog{ nullptr };
        string          _componentTag{ "Root" };

        MockRootComponent()
        {
            setCanEverTick( true );
        }

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onTick( float32 deltaTime ) override
        {
            SceneComponent::onTick( deltaTime );
            if ( _pTickOrderLog != nullptr )
            {
                _pTickOrderLog->push_back( _componentTag );
            }
        }

        void onSubTick( uint32 subTickId, float32 deltaTime ) override
        {
            SceneComponent::onSubTick( subTickId, deltaTime );
            if ( _pTickOrderLog != nullptr )
            {
                string entry = _componentTag;
                entry += "_SubTick_";
                entry += std::to_string( subTickId ).c_str();
                _pTickOrderLog->push_back( entry );
            }
        }
    };

    const TypeInfo* MockRootComponent::StaticType()
    {
        return makeMockComponentTypeInfo( hashed_string( "MockRootComponent" ),
                                          hashed_string( "sw::MockRootComponent" ),
                                          sizeof( MockRootComponent ),
                                          hashed_string( "sw::SceneComponent" ) );
    }

    class MockBasePawnComponent : public MockRootComponent
    {
    public:
        REFLECT_BODY();

        int32 _pawnHealth{ 100 };
        int32 _pawnTickCount{ 0 };

        MockBasePawnComponent()
        {
            setCanEverTick( true );
        }

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onTick( float32 deltaTime ) override
        {
            MockRootComponent::onTick( deltaTime );
            ++_pawnTickCount;
        }
    };

    const TypeInfo* MockBasePawnComponent::StaticType()
    {
        return makeMockComponentTypeInfo( hashed_string( "MockBasePawnComponent" ),
                                          hashed_string( "sw::MockBasePawnComponent" ),
                                          sizeof( MockBasePawnComponent ),
                                          hashed_string( "sw::MockRootComponent" ) );
    }

    class MockVehicleComponent : public MockBasePawnComponent
    {
    public:
        REFLECT_BODY();

        float32 _maxSpeed{ 120.0f };
        int32   _vehicleTickCount{ 0 };

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onTick( float32 deltaTime ) override
        {
            MockBasePawnComponent::onTick( deltaTime );
            ++_vehicleTickCount;
        }
    };

    const TypeInfo* MockVehicleComponent::StaticType()
    {
        return makeMockComponentTypeInfo( hashed_string( "MockVehicleComponent" ),
                                          hashed_string( "sw::MockVehicleComponent" ),
                                          sizeof( MockVehicleComponent ),
                                          hashed_string( "sw::MockBasePawnComponent" ) );
    }

    class MockFlyingVehicleComponent : public MockVehicleComponent
    {
    public:
        REFLECT_BODY();

        float32 _maxAltitude{ 5000.0f };
        int32   _flyingTickCount{ 0 };

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onTick( float32 deltaTime ) override
        {
            MockVehicleComponent::onTick( deltaTime );
            ++_flyingTickCount;
        }
    };

    const TypeInfo* MockFlyingVehicleComponent::StaticType()
    {
        return makeMockComponentTypeInfo( hashed_string( "MockFlyingVehicleComponent" ),
                                          hashed_string( "sw::MockFlyingVehicleComponent" ),
                                          sizeof( MockFlyingVehicleComponent ),
                                          hashed_string( "sw::MockVehicleComponent" ) );
    }

    class MockMidTickDeactivatorComponent : public Component
    {
    public:
        REFLECT_BODY();

        Component*      _pTargetComp{ nullptr };
        vector<string>* _pTickOrderLog{ nullptr };
        string          _componentTag{ "Deactivator" };
        uint32          _targetSubTickId{ 0 };
        uint32          _selfSubTickToUnregister{ 0 };
        int32           _subTickCount{ 0 };

        MockMidTickDeactivatorComponent()
            : _pTargetComp{ nullptr }
            , _pTickOrderLog{ nullptr }
            , _componentTag{ "Deactivator" }
            , _targetSubTickId{ 0 }
            , _selfSubTickToUnregister{ 0 }
            , _subTickCount{ 0 }
        {
            setCanEverTick( false );
        }

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onSubTick( uint32 subTickId, float32 deltaTime ) override
        {
            Component::onSubTick( subTickId, deltaTime );
            ++_subTickCount;
            if ( _pTickOrderLog != nullptr )
            {
                string entry = _componentTag;
                entry += "_SubTick_";
                entry += std::to_string( subTickId ).c_str();
                _pTickOrderLog->push_back( entry );
            }

            if ( _pTargetComp != nullptr && _targetSubTickId != 0 )
            {
                _pTargetComp->setSubTickActive( _targetSubTickId, false );
            }

            if ( _selfSubTickToUnregister != 0 )
            {
                unregisterSubTick( _selfSubTickToUnregister );
            }
        }
    };

    const TypeInfo* MockMidTickDeactivatorComponent::StaticType()
    {
        return makeMockComponentTypeInfo( hashed_string( "MockMidTickDeactivatorComponent" ),
                                          hashed_string( "sw::MockMidTickDeactivatorComponent" ),
                                          sizeof( MockMidTickDeactivatorComponent ),
                                          hashed_string{} );
    }

    class MockSubTickStressComponent : public Component
    {
    public:
        REFLECT_BODY();

        std::atomic<uint32>* _pGlobalTickSequence{ nullptr };
        std::atomic<uint32>* _pExecutionOrderArray{ nullptr };
        std::atomic<uint32>  _tickCount{ 0 };
        std::atomic<uint32>  _subTickCount{ 0 };
        uint32               _subTickGlobalIdOffset{ 0 };

        MockSubTickStressComponent()
            : _pGlobalTickSequence{ nullptr }
            , _pExecutionOrderArray{ nullptr }
            , _tickCount{ 0 }
            , _subTickCount{ 0 }
            , _subTickGlobalIdOffset{ 0 }
        {
            setCanEverTick( true );
        }

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }

        void onTick( float32 deltaTime ) override
        {
            Component::onTick( deltaTime );
            _tickCount.fetch_add( 1, std::memory_order_relaxed );
        }

        void onSubTick( uint32 subTickId, float32 deltaTime ) override
        {
            Component::onSubTick( subTickId, deltaTime );
            _subTickCount.fetch_add( 1, std::memory_order_relaxed );
            if ( _pGlobalTickSequence != nullptr && _pExecutionOrderArray != nullptr )
            {
                const uint32 order    = _pGlobalTickSequence->fetch_add( 1, std::memory_order_relaxed );
                const uint32 globalId = _subTickGlobalIdOffset + subTickId;
                _pExecutionOrderArray[globalId].store( order, std::memory_order_release );
            }
        }
    };

    const TypeInfo* MockSubTickStressComponent::StaticType()
    {
        return makeMockComponentTypeInfo( hashed_string( "MockSubTickStressComponent" ),
                                          hashed_string( "sw::MockSubTickStressComponent" ),
                                          sizeof( MockSubTickStressComponent ),
                                          hashed_string{} );
    }

    class MockPoolLifecycleComponent : public Component
    {
    public:
        REFLECT_BODY();

        static atomic<int32> s_ctorCount;
        static atomic<int32> s_dtorCount;

        int32 _customData{ 42 };

        MockPoolLifecycleComponent()
        {
            s_ctorCount.fetch_add( 1, std::memory_order_relaxed );
        }

        ~MockPoolLifecycleComponent() override
        {
            s_dtorCount.fetch_add( 1, std::memory_order_relaxed );
        }

        const TypeInfo* getTypeInfo() const override
        {
            return StaticType();
        }
    };

    atomic<int32> MockPoolLifecycleComponent::s_ctorCount{ 0 };
    atomic<int32> MockPoolLifecycleComponent::s_dtorCount{ 0 };

    const TypeInfo* MockPoolLifecycleComponent::StaticType()
    {
        return makeMockComponentTypeInfo( hashed_string( "MockPoolLifecycleComponent" ),
                                          hashed_string( "sw::MockPoolLifecycleComponent" ),
                                          sizeof( MockPoolLifecycleComponent ) );
    }

    /** @brief 모의 컴포넌트 TypeInfo 와 팩토리를 등록합니다. */
    static void RegisterMockComponents( GameObjectManager& manager )
    {
        MockMeshComponent::StaticType();
        MockAudioComponent::StaticType();
        MockCallbackComponent::StaticType();
        MockTickSceneComponent::StaticType();
        MockRootComponent::StaticType();
        MockBasePawnComponent::StaticType();
        MockVehicleComponent::StaticType();
        MockFlyingVehicleComponent::StaticType();
        MockMidTickDeactivatorComponent::StaticType();
        MockSubTickStressComponent::StaticType();
        MockPoolLifecycleComponent::StaticType();

        manager.registerComponentType<MockMeshComponent>( hashed_string( "MockMeshComponent" ) );
        manager.registerComponentType<MockAudioComponent>( hashed_string( "MockAudioComponent" ) );
        manager.registerComponentType<MockCallbackComponent>( hashed_string( "MockCallbackComponent" ) );
        manager.registerComponentType<MockTickSceneComponent>( hashed_string( "MockTickSceneComponent" ) );
        manager.registerComponentType<MockRootComponent>( hashed_string( "MockRootComponent" ) );
        manager.registerComponentType<MockBasePawnComponent>( hashed_string( "MockBasePawnComponent" ) );
        manager.registerComponentType<MockVehicleComponent>( hashed_string( "MockVehicleComponent" ) );
        manager.registerComponentType<MockFlyingVehicleComponent>( hashed_string( "MockFlyingVehicleComponent" ) );
        manager.registerComponentType<MockMidTickDeactivatorComponent>( hashed_string( "MockMidTickDeactivatorComponent" ) );
        manager.registerComponentType<MockSubTickStressComponent>( hashed_string( "MockSubTickStressComponent" ) );
        manager.registerComponentType<MockPoolLifecycleComponent>( hashed_string( "MockPoolLifecycleComponent" ) );
    }
} // namespace sw

// ------------------------------------------------------------------------------
// 1) GameObjectTest — 부착·틱·리플렉션·태그
// ------------------------------------------------------------------------------
/**
 * @brief [GameObjectTest] 동일 타입 컴포넌트 다중 부착
 */
SW_TEST_CASE( GameObjectTest, MultiSameComponentAttachment )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* actorPtr = manager.createGameObject( sw::hashed_string( "TestPlayer" ) );

    sw::GameObject& actor = *actorPtr;
    SW_EXPECT_EQUAL( sw::string( "TestPlayer" ), sw::string( actor.getName().c_str() ) );

    sw::MockMeshComponent* mesh1 = actor.addComponent<sw::MockMeshComponent>();
    mesh1->_meshName             = "HeadMesh";

    sw::MockMeshComponent* mesh2 = actor.addComponent<sw::MockMeshComponent>();
    mesh2->_meshName             = "BodyMesh";

    sw::MockMeshComponent* mesh3 = actor.addComponent<sw::MockMeshComponent>();
    mesh3->_meshName             = "WeaponMesh";

    SW_EXPECT_EQUAL( 3u, actor.getComponentCount() );

    sw::MockMeshComponent* firstMesh = actor.getComponent<sw::MockMeshComponent>();
    SW_ASSERT_NOT_NULL( firstMesh );
    SW_EXPECT_EQUAL( sw::string( "HeadMesh" ), firstMesh->_meshName );
    SW_EXPECT_EQUAL( mesh1, firstMesh );

    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 1, mesh1->_tickCount );
    SW_EXPECT_EQUAL( 1, mesh2->_tickCount );
    SW_EXPECT_EQUAL( 1, mesh3->_tickCount );

    SW_EXPECT_TRUE( actor.removeComponent( firstMesh ) );
    SW_EXPECT_EQUAL( 2u, actor.getComponentCount() );
}

/**
 * @brief [GameObjectTest] 지연 컴포넌트 삭제가 조회에서 빠진다
 */
SW_TEST_CASE( GameObjectTest, DeferredComponentDestructionRemovesFromObject )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject*        actor = manager.createGameObject( sw::hashed_string( "DeferredCompActor" ) );
    sw::MockMeshComponent* mesh  = actor->addComponent<sw::MockMeshComponent>();
    SW_ASSERT_NOT_NULL( mesh );
    SW_EXPECT_EQUAL( 1u, actor->getComponentCount() );

    manager.destroyComponent( mesh );
    SW_EXPECT_TRUE( mesh->isPendingKill() );
    SW_EXPECT_TRUE( actor->getComponent<sw::MockMeshComponent>() == nullptr );
    SW_EXPECT_EQUAL( 0u, actor->getComponentCount() );
    SW_EXPECT_EQUAL( 0u, actor->getComponentCount() );
}

/**
 * @brief [GameObjectTest] 에디터 동적 컴포넌트 부착
 */
SW_TEST_CASE( GameObjectTest, EditorDynamicComponentAttachment )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );
    const uint32 typeCountBefore = static_cast<uint32>( manager.getRegisteredComponentTypeNames().size() );

    const sw::vector<sw::hashed_string> factories = manager.getRegisteredComponentTypeNames();
    SW_EXPECT_EQUAL( typeCountBefore + 0u, static_cast<uint32>( factories.size() ) );

    {
        sw::GameObject*         actorPtr = manager.createGameObject( sw::hashed_string( "EditorActor" ) );
        sw::GameObject&         actor    = *actorPtr;
        sw::MockMeshComponent*  comp1    = actor.addComponent<sw::MockMeshComponent>();
        sw::MockAudioComponent* comp2    = actor.addComponent<sw::MockAudioComponent>();

        SW_EXPECT_TRUE( comp1 != nullptr );
        SW_EXPECT_TRUE( comp2 != nullptr );
        SW_EXPECT_EQUAL( 2u, actor.getComponentCount() );

        SW_EXPECT_EQUAL( &actor, comp1->getOwner() );
        SW_EXPECT_EQUAL( &actor, comp2->getOwner() );
        SW_EXPECT_EQUAL( comp1, actor.findComponentByTypeName( sw::hashed_string( "MockMeshComponent" ) ) );
        SW_EXPECT_EQUAL( comp2, actor.findComponentByTypeName( sw::hashed_string( "MockAudioComponent" ) ) );
        SW_EXPECT_NULL( actor.findComponentByTypeName( sw::hashed_string( "MissingComponent" ) ) );
    }

    // getComponentManager().shutdown() 호출 제거
}

/**
 * @brief [GameObjectTest] 병렬 컴포넌트 틱
 */
SW_TEST_CASE( GameObjectTest, ParallelComponentTicking )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    {
        sw::vector<sw::GameObject*> actors;

        for ( int32 objectIndex = 0; objectIndex < 100; ++objectIndex )
        {
            sw::fixed_string<32> nameBuf{};
            sw::formatstring( nameBuf.data(), nameBuf.capacity(), "TickActor_%#", objectIndex );
            sw::GameObject* actorPtr = manager.createGameObject( sw::hashed_string( nameBuf.c_str() ) );
            actorPtr->addComponent<sw::MockMeshComponent>();
            actors.push_back( actorPtr );
        }

        manager.tick( 0.016f );

        for ( sw::GameObject* actor : actors )
        {
            sw::MockMeshComponent* meshComp = actor->getComponent<sw::MockMeshComponent>();
            SW_EXPECT_NOT_NULL( meshComp );
            if ( meshComp != nullptr )
            {
                SW_EXPECT_EQUAL( 1, meshComp->_tickCount );
            }
        }
    }
}

/**
 * @brief [GameObjectTest] 리플렉션 지원
 */
SW_TEST_CASE( GameObjectTest, ReflectionSupport )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );
    sw::GameObject*        actorPtr = manager.createGameObject( sw::hashed_string( "ReflectedActor" ) );
    sw::GameObject&        actor    = *actorPtr;
    sw::MockMeshComponent* meshComp = actor.addComponent<sw::MockMeshComponent>();

    SW_EXPECT_TRUE( actor.getObjectId() != 0 );
    SW_EXPECT_TRUE( meshComp->getComponentId() != 0 );
}

using namespace sw;

// ------------------------------------------------------------------------------
// 2) TagSystemTest — 계층 리터럴·매칭
// ------------------------------------------------------------------------------
/**
 * @brief [TagSystemTest] 정수 리터럴과 계층 포함
 */
SW_TEST_CASE( TagSystemTest, IntegerLiteralAndHierarchicalSubsumption )
{
    sw::GameObjectManager manager;
    constexpr TagID       tagAttacking = "State.Combat.Attacking"_tag;
    constexpr TagID       tagCombat    = "State.Combat"_tag;
    constexpr TagID       tagState     = "State"_tag;

    SW_EXPECT_TRUE( tagAttacking.isValid() );
    SW_EXPECT_TRUE( tagCombat.isValid() );

    SW_EXPECT_TRUE( tagAttacking.isSubtagOf( tagCombat ) );
    SW_EXPECT_TRUE( tagAttacking.isSubtagOf( tagState ) ); // 전체 부모 체인
    SW_EXPECT_TRUE( tagCombat.isSubtagOf( tagState ) );
    SW_EXPECT_FALSE( tagState.isSubtagOf( tagAttacking ) );

    TagContainer container{ tagAttacking };
    SW_EXPECT_TRUE( container.hasTag( tagAttacking, true ) );
    SW_EXPECT_TRUE( container.hasTag( tagState, false ) );

    TagContainer required{ tagAttacking };
    TagContainer forbidden{ "State.Dead"_tag };
    SW_EXPECT_TRUE( container.matchTags( required, forbidden ) );
}

// ------------------------------------------------------------------------------
// 3) GameObjectTest — 부착·틱·리플렉션·태그
// ------------------------------------------------------------------------------
/**
 * @brief [GameObjectTest] 태그 관리
 */
SW_TEST_CASE( GameObjectTest, TagManagement )
{
    sw::GameObjectManager manager;
    sw::GameObject*       actorPtr = manager.createGameObject( sw::hashed_string( "TaggedHero" ) );
    sw::GameObject&       actor    = *actorPtr;

    constexpr TagID tagInvincible = "Status.Invincible"_tag;
    constexpr TagID tagFlying     = "Status.Flying"_tag;

    actor.addTag( tagInvincible );
    actor.addTag( tagFlying );

    SW_EXPECT_TRUE( actor.hasTag( tagInvincible ) );
    SW_EXPECT_TRUE( actor.hasTag( tagFlying ) );
    SW_EXPECT_FALSE( actor.hasTag( "Status.Poisoned"_tag ) );

    actor.removeTag( tagFlying );
    SW_EXPECT_FALSE( actor.hasTag( tagFlying ) );
}

// ------------------------------------------------------------------------------
// 4) SceneComponentTest — 계층·더티·카메라 상대
// ------------------------------------------------------------------------------
/**
 * @brief [SceneComponentTest] 부모-자식 계층과 dirty 전파
 */
SW_TEST_CASE( SceneComponentTest, ParentChildHierarchyAndDirtyPropagation )
{
    sw::GameObjectManager manager;
    sw::GameObject*       parentActorPtr = manager.createGameObject( sw::hashed_string( "ParentActor" ) );
    sw::GameObject&       parentActor    = *parentActorPtr;
    sw::GameObject*       childActorPtr  = manager.createGameObject( sw::hashed_string( "ChildActor" ) );
    sw::GameObject&       childActor     = *childActorPtr;

    parentActor.addComponent<SceneComponent>();
    childActor.addComponent<SceneComponent>();

    SceneComponent* parentComp = parentActor.getComponent<SceneComponent>();
    SceneComponent* childComp  = childActor.getComponent<SceneComponent>();

    parentComp->setLocalPosition( float3( 10.0f, 0.0f, 0.0f ) );
    childComp->setLocalPosition( float3( 5.0f, 2.0f, 0.0f ) );

    bool attachOk = childComp->attachToComponent( parentComp );
    SW_EXPECT_TRUE( attachOk );

    float3 childWorldPos = childComp->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 15.0f, childWorldPos._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, childWorldPos._y, 1e-4f );

    float4x4 childWorldMat = childComp->getWorldMatrix();
    SW_EXPECT_NEAR_EQUAL( 15.0f, childWorldMat._41, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, childWorldMat._42, 1e-4f );

    parentComp->setLocalPosition( float3( 20.0f, 0.0f, 0.0f ) );
    childWorldPos = childComp->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 25.0f, childWorldPos._x, 1e-4f );

    childWorldMat = childComp->getWorldMatrix();
    SW_EXPECT_NEAR_EQUAL( 25.0f, childWorldMat._41, 1e-4f );
}

/**
 * @brief [SceneComponentTest] 부모 회전·스케일이 자식에 전파
 */
SW_TEST_CASE( SceneComponentTest, ParentRotationScalePropagatesToChild )
{
    sw::GameObjectManager manager;
    sw::GameObject*       parentActorPtr = manager.createGameObject( sw::hashed_string( "RotatedScaledParent" ) );
    sw::GameObject&       parentActor    = *parentActorPtr;
    sw::GameObject*       childActorPtr  = manager.createGameObject( sw::hashed_string( "RotatedScaledChild" ) );
    sw::GameObject&       childActor     = *childActorPtr;

    parentActor.addComponent<SceneComponent>();
    childActor.addComponent<SceneComponent>();

    SceneComponent* parentComp = parentActor.getComponent<SceneComponent>();
    SceneComponent* childComp  = childActor.getComponent<SceneComponent>();

    // yaw 90도: 로컬 +X 가 월드 -Z 가 된다(오른손 Y-up).
    parentComp->setLocalPosition( float3( 10.0f, 0.0f, 0.0f ) );
    parentComp->setLocalRotation( float3( 0.0f, MathUtil::HalfPi, 0.0f ) );
    parentComp->setLocalScale( float3( 2.0f, 2.0f, 2.0f ) );

    childComp->setLocalPosition( float3( 1.0f, 0.0f, 0.0f ) );
    SW_ASSERT_TRUE( childComp->attachToComponent( parentComp ) );

    const float3 childWorldPos = childComp->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 10.0f, childWorldPos._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, childWorldPos._y, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, childWorldPos._z, 1e-3f ); // 스케일 1*2 후 yaw 90

    const float4x4 childWorldMat = childComp->getWorldMatrix();
    SW_EXPECT_NEAR_EQUAL( childWorldPos._x, childWorldMat._41, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( childWorldPos._y, childWorldMat._42, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( childWorldPos._z, childWorldMat._43, 1e-3f );

    const double3  cameraPos( 10.0, 0.0, 0.0 );
    const float4x4 cameraRel = childComp->getCameraRelativeWorldMatrix( cameraPos );
    SW_EXPECT_NEAR_EQUAL( 0.0f, cameraRel._41, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, cameraRel._42, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( -2.0f, cameraRel._43, 1e-3f );
    // 계층 스케일은 카메라 상대 행렬에 남아야 한다(상단 3x3 이 단위행렬이면 안 됨).
    const float3 camScale = cameraRel.getScale();
    SW_EXPECT_NEAR_EQUAL( 2.0f, camScale._x, 1e-3f );
}

/**
 * @brief [SceneComponentTest] 대형 월드 좌표와 카메라 상대 렌더링
 */
SW_TEST_CASE( SceneComponentTest, LargeWorldCoordinatesAndCameraRelativeRendering )
{
    sw::GameObjectManager manager;
    sw::GameObject*       actorPtr = manager.createGameObject( sw::hashed_string( "LWCActor" ) );
    sw::GameObject&       actor    = *actorPtr;

    SceneComponent* comp = actor.addComponent<SceneComponent>();

    comp->setLocalPosition( float3( 1000000.5f, 500000.25f, 0.0f ) );

    double3 lwcPos = comp->getWorldPositionLWC();
    SW_EXPECT_NEAR_EQUAL( 1000000.5, lwcPos._x, 1e-6 );
    SW_EXPECT_NEAR_EQUAL( 500000.25, lwcPos._y, 1e-6 );

    double3  cameraWorldPos( 1000000.0, 500000.0, 0.0 );
    float4x4 cameraRelMat = comp->getCameraRelativeWorldMatrix( cameraWorldPos );

    SW_EXPECT_NEAR_EQUAL( 0.5f, cameraRelMat._41, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, cameraRelMat._42, 1e-4f );
}

// ------------------------------------------------------------------------------
// 5) MathTest — Double3 벡터
// ------------------------------------------------------------------------------
/**
 * @brief [MathTest] double3 벡터 연산
 */
SW_TEST_CASE( MathTest, Double3VectorOperations )
{
    sw::GameObjectManager manager;
    double3               v1( 3.0, 4.0, 0.0 );
    SW_EXPECT_NEAR_EQUAL( 5.0, v1.getLength(), 1e-6 );

    v1.normalize();
    SW_EXPECT_NEAR_EQUAL( 0.6, v1._x, 1e-6 );
    SW_EXPECT_NEAR_EQUAL( 0.8, v1._y, 1e-6 );

    float3  f3( 10.0f, 20.0f, 30.0f );
    double3 d3FromF3( f3 );
    SW_EXPECT_NEAR_EQUAL( 10.0, d3FromF3._x, 1e-6 );
    SW_EXPECT_NEAR_EQUAL( 20.0, d3FromF3._y, 1e-6 );
    SW_EXPECT_NEAR_EQUAL( 30.0, d3FromF3._z, 1e-6 );

    float3 convertedF3 = d3FromF3.toFloat3();
    SW_EXPECT_NEAR_EQUAL( 10.0f, convertedF3._x, 1e-4f );
}

// ------------------------------------------------------------------------------
// 6) GameObjectManagerTest — 생성·검색·지연 파괴
// ------------------------------------------------------------------------------
/**
 * @brief [GameObjectManagerTest] 생성·검색·지연 파괴
 */
SW_TEST_CASE( GameObjectManagerTest, CreationSearchAndDeferredDestruction )
{
    sw::GameObjectManager manager;

    GameObject* hero  = manager.createGameObject( hashed_string( "Hero" ) );
    GameObject* enemy = manager.createGameObject( hashed_string( "Enemy" ) );
    SW_ASSERT_NOT_NULL( hero );
    SW_ASSERT_NOT_NULL( enemy );

    manager.tick( 0.0f );
    SW_EXPECT_EQUAL( size_t( 2 ), manager.getAllGameObjects().size() );
    SW_EXPECT_EQUAL( hero, manager.findGameObjectByName( hashed_string( "Hero" ) ) );
    SW_EXPECT_EQUAL( enemy, manager.findGameObjectById( enemy->getObjectId() ) );
    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Missing" ) ) );

    hero->setName( hashed_string( "HeroRenamed" ) );
    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Hero" ) ) );
    SW_EXPECT_EQUAL( hero, manager.findGameObjectByName( hashed_string( "HeroRenamed" ) ) );

    manager.destroyObject( enemy );
    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Enemy" ) ) );
    SW_EXPECT_NULL( manager.findGameObjectById( enemy->getObjectId() ) );
    SW_EXPECT_EQUAL( size_t( 2 ), manager.getAllGameObjects().size() );

    manager.tick( 0.0f );

    SW_EXPECT_EQUAL( size_t( 1 ), manager.getAllGameObjects().size() );
    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Enemy" ) ) );
    SW_EXPECT_NOT_NULL( manager.findGameObjectByName( hashed_string( "HeroRenamed" ) ) );

    manager.clear();
    SW_EXPECT_EQUAL( size_t( 0 ), manager.getAllGameObjects().size() );
}

/**
 * @brief [GameObjectManagerTest] 지연 파괴 후 이름·ID 조회가 비고 같은 이름을 다시 쓸 수 있다
 */
SW_TEST_CASE( GameObjectManagerTest, OwnershipReleasedAfterDeferredDestroy )
{
    sw::GameObjectManager manager;
    GameObject*           first = manager.createGameObject( hashed_string( "Owned" ) );
    SW_ASSERT_NOT_NULL( first );
    const uint64 firstId = first->getObjectId();

    manager.destroyObject( first );

    manager.tick( 0.0f );

    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Owned" ) ) );
    SW_EXPECT_NULL( manager.findGameObjectById( firstId ) );
    SW_EXPECT_EQUAL( size_t( 0 ), manager.getAllGameObjects().size() );

    GameObject* second = manager.createGameObject( hashed_string( "Owned" ) );
    SW_ASSERT_NOT_NULL( second );
    SW_EXPECT_EQUAL( second, manager.findGameObjectByName( hashed_string( "Owned" ) ) );
    SW_EXPECT_NOT_EQUAL( firstId, second->getObjectId() );
}

/**
 * @brief [GameObjectManagerTest] 순차·병렬 틱
 */
SW_TEST_CASE( GameObjectManagerTest, SequentialAndParallelTick )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* a = manager.createGameObject( hashed_string( "A" ) );
    GameObject* b = manager.createGameObject( hashed_string( "B" ) );
    a->addComponent<MockMeshComponent>();
    b->addComponent<MockMeshComponent>();

    MockMeshComponent* aMesh = a->getComponent<MockMeshComponent>();
    MockMeshComponent* bMesh = b->getComponent<MockMeshComponent>();

    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 1, aMesh->_tickCount );
    SW_EXPECT_EQUAL( 1, bMesh->_tickCount );

    manager.tick( 0.016f );
    aMesh = a->getComponent<MockMeshComponent>();
    bMesh = b->getComponent<MockMeshComponent>();
    SW_EXPECT_EQUAL( 2, aMesh->_tickCount );
    SW_EXPECT_EQUAL( 2, bMesh->_tickCount );

    manager.clear();
}

/**
 * @brief [GameObjectManagerTest] 병렬 틱이 안정된 계층 트랜스폼을 읽음
 */
SW_TEST_CASE( GameObjectManagerTest, ParallelTickReadsStableHierarchyTransforms )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* parentObj = manager.createGameObject( hashed_string( "Parent" ) );
    GameObject* childObj  = manager.createGameObject( hashed_string( "Child" ) );

    parentObj->addComponent<MockTickSceneComponent>();
    childObj->addComponent<MockTickSceneComponent>();

    MockTickSceneComponent* parentComp = parentObj->getComponent<MockTickSceneComponent>();
    MockTickSceneComponent* childComp  = childObj->getComponent<MockTickSceneComponent>();

    parentComp->setLocalPosition( float3( 10.0f, 0.0f, 0.0f ) );
    childComp->setLocalPosition( float3( 5.0f, 0.0f, 0.0f ) );
    SW_ASSERT_TRUE( childComp->attachToComponent( parentComp ) );

    float3 observedDuringTick{ 0.0f, 0.0f, 0.0f };
    childComp->_pObservedWorld = &observedDuringTick;

    manager.tick( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, observedDuringTick._x, 1e-4f );

    // 틱 중 로컬 쓰기는 post-flush 이후에 보이며, 틱 중간 스냅샷에는 없을 수 있다.
    parentComp->_tickLocalPos      = float3( 20.0f, 0.0f, 0.0f );
    parentComp->_bWriteLocalOnTick = SW_TRUE;
    manager.tick( 0.016f );
    SW_EXPECT_NEAR_EQUAL( 25.0f, childComp->getWorldPosition()._x, 1e-4f );

    manager.clear();
}

/**
 * @brief [GameObjectManagerTest] 틱이 트랜스폼을 깨끗이 두면 tick 두 번째 flush 를 건너뛴다
 */
SW_TEST_CASE( GameObjectManagerTest, ParallelTickLeavesTransformsCleanWhenUnchanged )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject*     obj  = manager.createGameObject( hashed_string( "CleanRoot" ) );
    SceneComponent* root = obj->addComponent<SceneComponent>();
    root->setLocalPosition( float3( 1.0f, 2.0f, 3.0f ) );

    manager.tick( 0.016f );
    SW_EXPECT_TRUE( manager.hasDirtySceneTransforms() == false );
    SW_EXPECT_NEAR_EQUAL( 1.0f, root->getWorldPosition()._x, 1e-4f );

    manager.tick( 0.016f );
    SW_EXPECT_TRUE( manager.hasDirtySceneTransforms() == false );

    manager.clear();
}

// ------------------------------------------------------------------------------
// 7) ObjectStateXmlSerializerTest — XML 저장·계층 라운드트립
// ------------------------------------------------------------------------------
/**
 * @brief [ObjectStateXmlSerializerTest] XML 문자열 저장·로드
 */
SW_TEST_CASE( ObjectStateXmlSerializerTest, SaveAndLoadXmlString )
{
    sw::GameObjectManager manager;
    sw::GameObject*       sourcePtr = manager.createGameObject( sw::hashed_string( "SerializedHero" ) );
    sw::GameObject&       source    = *sourcePtr;
    source.setActive( false );

    const sw::string xml = ObjectStateSerializer::saveToXmlString( &source );
    SW_ASSERT_TRUE( xml.empty() == false );
    SW_EXPECT_TRUE( xml.find( "GameObject" ) != sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "SerializedHero" ) != sw::string::npos );
    // 컨테이너는 프로퍼티 이름 요소로 직접 나간다(<vector _name=..> 래핑 없음).
    // 비어 있으면 self-closing(<_listComponent />)이라 여는 태그만으로 찾는다.
    SW_EXPECT_TRUE( xml.find( "<_listComponent" ) != sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "_name=\"_listComponent\"" ) == sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "SceneTransforms" ) == sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "_parentGO" ) == sw::string::npos );
    SW_EXPECT_TRUE( xml.find( "ParentGO" ) == sw::string::npos );

    const sw::string json = ObjectStateSerializer::saveToJsonString( &source );
    SW_ASSERT_TRUE( json.empty() == false );
    // 컨테이너는 프로퍼티 이름 아래 배열로 직접 나간다("vector"/"_name" 래핑 없음).
    SW_EXPECT_TRUE( json.find( "\"_listComponent\":[" ) != sw::string::npos );
    SW_EXPECT_TRUE( json.find( "\"vector\"" ) == sw::string::npos );
    SW_EXPECT_TRUE( json.find( "\"Components\"" ) == sw::string::npos );
    SW_EXPECT_TRUE( json.find( "ParentGO" ) == sw::string::npos );

    manager.clear();

    sw::GameObject* targetPtr = manager.createGameObject( sw::hashed_string( "Temp" ) );
    sw::GameObject& target    = *targetPtr;
    target.setActive( true );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( &target, xml ) );
    SW_EXPECT_STREQ( "SerializedHero", target.getName().c_str() );
    SW_EXPECT_FALSE( target.isActive() );

    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromXmlString( nullptr, xml ) );
    SW_EXPECT_FALSE( ObjectStateSerializer::loadFromXmlString( &target, "" ) );
    SW_EXPECT_EMPTY( ObjectStateSerializer::saveToXmlString( nullptr ) );
}

/**
 * @brief [ObjectStateXmlSerializerTest] 부모-자식 계층 라운드트립
 */
SW_TEST_CASE( ObjectStateXmlSerializerTest, ParentChildHierarchyRoundtrip )
{
    Scene* scene = engine::getSceneManager().getActiveScene();
    if ( scene == nullptr )
        scene = engine::getSceneManager().createScene( "GOHierarchySerializer" );
    SW_ASSERT_NOT_NULL( scene );
    SW_ASSERT_NOT_NULL( scene->getObjectManager() );

    GameObjectManager* manager = scene->getObjectManager();
    manager->clear();

    GameObject* parent = manager->createGameObject( hashed_string( "ParentGO" ) );
    GameObject* child  = manager->createGameObject( hashed_string( "ChildGO" ) );
    GameObject* grand  = manager->createGameObject( hashed_string( "GrandGO" ) );
    SW_ASSERT_NOT_NULL( parent );
    SW_ASSERT_NOT_NULL( child );
    SW_ASSERT_NOT_NULL( grand );

    parent->addComponent<sw::SceneComponent>();
    child->addComponent<sw::SceneComponent>();
    grand->addComponent<sw::SceneComponent>();

    parent->getComponent<sw::SceneComponent>()->setLocalPosition( sw::float3( 1.0f, 2.0f, 3.0f ) );
    child->getComponent<sw::SceneComponent>()->setLocalPosition( sw::float3( 4.0f, 5.0f, 6.0f ) );

    SW_ASSERT_TRUE( child->attachToParent( parent ) );
    SW_ASSERT_TRUE( grand->attachToParent( child ) );

    const sw::string parentXml = ObjectStateSerializer::saveToXmlString( parent );
    const sw::string childXml  = ObjectStateSerializer::saveToXmlString( child );
    const sw::string grandXml  = ObjectStateSerializer::saveToXmlString( grand );
    SW_ASSERT_TRUE( parentXml.empty() == false );
    SW_ASSERT_TRUE( childXml.empty() == false );
    SW_ASSERT_TRUE( grandXml.empty() == false );

    SW_EXPECT_TRUE( childXml.find( "ParentGO" ) != sw::string::npos );
    SW_EXPECT_TRUE( grandXml.find( "ChildGO" ) != sw::string::npos );

    // 계층을 해체하고 빈 GO 를 다시 만든 뒤 로드·리바인드한다(Play 스냅샷 순서).
    manager->clear();
    parent = manager->createGameObject( hashed_string( "TempParent" ) );
    child  = manager->createGameObject( hashed_string( "TempChild" ) );
    grand  = manager->createGameObject( hashed_string( "TempGrand" ) );
    SW_ASSERT_NOT_NULL( parent );
    SW_ASSERT_NOT_NULL( child );
    SW_ASSERT_NOT_NULL( grand );

    // 자식을 부모보다 먼저 로드해 두 번째 패스 리바인드를 강제한다(비순서 스냅샷 복원).
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( child, childXml ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( grand, grandXml ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( parent, parentXml ) );

    SW_ASSERT_TRUE( ObjectStateSerializer::rebindSceneHierarchy( parent, parentXml ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::rebindSceneHierarchy( child, childXml ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::rebindSceneHierarchy( grand, grandXml ) );

    parent = manager->findGameObjectByName( hashed_string( "ParentGO" ) );
    child  = manager->findGameObjectByName( hashed_string( "ChildGO" ) );
    grand  = manager->findGameObjectByName( hashed_string( "GrandGO" ) );
    SW_ASSERT_NOT_NULL( parent );
    SW_ASSERT_NOT_NULL( child );
    SW_ASSERT_NOT_NULL( grand );

    SW_EXPECT_NULL( parent->getParent() );
    SW_EXPECT_EQUAL( parent, child->getParent() );
    SW_EXPECT_EQUAL( child, grand->getParent() );
    SW_EXPECT_EQUAL( size_t( 1 ), parent->getChildren().size() );
    SW_EXPECT_EQUAL( child, parent->getChildren()[0] );
    SW_EXPECT_EQUAL( size_t( 1 ), child->getChildren().size() );
    SW_EXPECT_EQUAL( grand, child->getChildren()[0] );

    const sw::float3 parentPos = parent->getComponent<sw::SceneComponent>()->getLocalPosition();
    const sw::float3 childPos  = child->getComponent<sw::SceneComponent>()->getLocalPosition();
    SW_EXPECT_NEAR_EQUAL( 1.0f, parentPos._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, parentPos._y, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, parentPos._z, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, childPos._x, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, childPos._y, 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 6.0f, childPos._z, 0.0001f );

    manager->clear();
}

// ------------------------------------------------------------------------------
// 8) ComponentTickGroupTest — PrePhysics~PostUpdate 순서 및 상속 계층 연동 검증
// ------------------------------------------------------------------------------
/**
 * @brief [ComponentTickGroupTest] 다단계 상속 계층 컴포넌트들의 4단계 TickGroup(PrePhysics~PostUpdate) 시간순 실행 검증
 */
SW_TEST_CASE( ComponentTickGroupTest, MultiLevelInheritanceTickGroupChronologicalSequence )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::vector<sw::string> listTickOrder;

    // 역순(PostUpdate -> PostPhysics -> DuringPhysics -> PrePhysics)으로 액터 및 컴포넌트를 생성하여
    // 생성 순서와 무관하게 TickGroup 순서대로 틱이 정렬·디스패치되는지 검증
    sw::GameObject*                 pActorPostUpdate = manager.createGameObject( sw::hashed_string( "Actor_PostUpdate" ) );
    sw::MockFlyingVehicleComponent* pCompFlying      = pActorPostUpdate->addComponent<sw::MockFlyingVehicleComponent>();
    pCompFlying->setTickGroup( sw::TickGroup::PostUpdate );
    pCompFlying->_pTickOrderLog = &listTickOrder;
    pCompFlying->_componentTag  = "3_PostUpdate_Flying";

    sw::GameObject*           pActorPostPhysics = manager.createGameObject( sw::hashed_string( "Actor_PostPhysics" ) );
    sw::MockVehicleComponent* pCompVehicle      = pActorPostPhysics->addComponent<sw::MockVehicleComponent>();
    pCompVehicle->setTickGroup( sw::TickGroup::PostPhysics );
    pCompVehicle->_pTickOrderLog = &listTickOrder;
    pCompVehicle->_componentTag  = "2_PostPhysics_Vehicle";

    sw::GameObject*            pActorDuringPhysics = manager.createGameObject( sw::hashed_string( "Actor_DuringPhysics" ) );
    sw::MockBasePawnComponent* pCompPawn           = pActorDuringPhysics->addComponent<sw::MockBasePawnComponent>();
    pCompPawn->setTickGroup( sw::TickGroup::DuringPhysics );
    pCompPawn->_pTickOrderLog = &listTickOrder;
    pCompPawn->_componentTag  = "1_DuringPhysics_Pawn";

    sw::GameObject*        pActorPrePhysics = manager.createGameObject( sw::hashed_string( "Actor_PrePhysics" ) );
    sw::MockRootComponent* pCompRoot        = pActorPrePhysics->addComponent<sw::MockRootComponent>();
    pCompRoot->setTickGroup( sw::TickGroup::PrePhysics );
    pCompRoot->_pTickOrderLog = &listTickOrder;
    pCompRoot->_componentTag  = "0_PrePhysics_Root";

    // 틱 1회 실행
    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), listTickOrder.size() );
    if ( listTickOrder.size() == 4 )
    {
        SW_EXPECT_EQUAL( "0_PrePhysics_Root", listTickOrder[0] );
        SW_EXPECT_EQUAL( "1_DuringPhysics_Pawn", listTickOrder[1] );
        SW_EXPECT_EQUAL( "2_PostPhysics_Vehicle", listTickOrder[2] );
        SW_EXPECT_EQUAL( "3_PostUpdate_Flying", listTickOrder[3] );
    }
}

/**
 * @brief [ComponentTickGroupTest] 부모-자식 트리 계층에서 서로 다른 TickGroup을 가진 컴포넌트들의 프레임 내 단방향 데이터 파이프라인 검증
 */
SW_TEST_CASE( ComponentTickGroupTest, ParentChildHierarchyHeterogeneousTickGroupDataPipeline )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    // 1) 루트 부모 (PrePhysics 단계에서 기본 체력 연산)
    sw::GameObject*            pParentObj = manager.createGameObject( sw::hashed_string( "PipelineParent" ) );
    sw::MockBasePawnComponent* pPawn      = pParentObj->addComponent<sw::MockBasePawnComponent>();
    pPawn->setTickGroup( sw::TickGroup::PrePhysics );
    pPawn->_pawnHealth = 100;

    // 2) 자식 (DuringPhysics 단계에서 부모 체력에 기반하여 최대 속도 산출)
    sw::GameObject* pChildObj = manager.createGameObject( sw::hashed_string( "PipelineChild" ) );
    pChildObj->attachToParent( pParentObj );
    sw::MockVehicleComponent* pVehicle = pChildObj->addComponent<sw::MockVehicleComponent>();
    pVehicle->setTickGroup( sw::TickGroup::DuringPhysics );
    pVehicle->_maxSpeed = 0.0f;

    // 3) 손자 (PostUpdate 단계에서 자식 속도에 기반하여 비행 고도 산출)
    sw::GameObject* pGrandObj = manager.createGameObject( sw::hashed_string( "PipelineGrand" ) );
    pGrandObj->attachToParent( pChildObj );
    sw::MockFlyingVehicleComponent* pFlying = pGrandObj->addComponent<sw::MockFlyingVehicleComponent>();
    pFlying->setTickGroup( sw::TickGroup::PostUpdate );
    pFlying->_maxAltitude = 0.0f;

    // 커스텀 파이프라인 로깅 연결
    sw::vector<sw::string> listTickOrder;
    pPawn->_pTickOrderLog    = &listTickOrder;
    pPawn->_componentTag     = "Stage1_ParentPrePhysics";
    pVehicle->_pTickOrderLog = &listTickOrder;
    pVehicle->_componentTag  = "Stage2_ChildDuringPhysics";
    pFlying->_pTickOrderLog  = &listTickOrder;
    pFlying->_componentTag   = "Stage3_GrandPostUpdate";

    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), listTickOrder.size() );
    if ( listTickOrder.size() == 3 )
    {
        SW_EXPECT_EQUAL( "Stage1_ParentPrePhysics", listTickOrder[0] );
        SW_EXPECT_EQUAL( "Stage2_ChildDuringPhysics", listTickOrder[1] );
        SW_EXPECT_EQUAL( "Stage3_GrandPostUpdate", listTickOrder[2] );
    }

    // 3개 계층 컴포넌트 모두 누락 없이 1회씩 틱을 완료했는지 확인
    SW_EXPECT_EQUAL( 1, pPawn->_pawnTickCount );
    SW_EXPECT_EQUAL( 1, pVehicle->_vehicleTickCount );
    SW_EXPECT_EQUAL( 1, pFlying->_flyingTickCount );
}

/**
 * @brief [ComponentTickGroupTest] 런타임에 TickGroup이 동적으로 변경(Migration)되었을 때 틱 웨이브 재구성 및 순서 역전 검증
 */
SW_TEST_CASE( ComponentTickGroupTest, DynamicTickGroupRuntimeMigration )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::vector<sw::string> listTickOrder;

    sw::GameObject*        pActorA = manager.createGameObject( sw::hashed_string( "ActorA" ) );
    sw::MockRootComponent* pCompA  = pActorA->addComponent<sw::MockRootComponent>();
    pCompA->setTickGroup( sw::TickGroup::PostUpdate ); // A는 처음엔 PostUpdate (나중에 실행)
    pCompA->_pTickOrderLog = &listTickOrder;
    pCompA->_componentTag  = "ActorA";

    sw::GameObject*                 pActorB = manager.createGameObject( sw::hashed_string( "ActorB" ) );
    sw::MockFlyingVehicleComponent* pCompB  = pActorB->addComponent<sw::MockFlyingVehicleComponent>();
    pCompB->setTickGroup( sw::TickGroup::PrePhysics ); // B는 처음엔 PrePhysics (먼저 실행)
    pCompB->_pTickOrderLog = &listTickOrder;
    pCompB->_componentTag  = "ActorB";

    // Frame 1: B -> A 순서로 실행되어야 함
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
    if ( listTickOrder.size() == 2 )
    {
        SW_EXPECT_EQUAL( "ActorB", listTickOrder[0] );
        SW_EXPECT_EQUAL( "ActorA", listTickOrder[1] );
    }

    // 런타임 동적 TickGroup 변경 (A -> PrePhysics, B -> PostUpdate)
    listTickOrder.clear();
    pCompA->setTickGroup( sw::TickGroup::PrePhysics );
    pCompB->setTickGroup( sw::TickGroup::PostUpdate );
    pActorA->markTickOrderDirty();
    pActorB->markTickOrderDirty();

    // Frame 2: 즉시 A -> B 순서로 역전되어 실행되어야 함
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
    if ( listTickOrder.size() == 2 )
    {
        SW_EXPECT_EQUAL( "ActorA", listTickOrder[0] );
        SW_EXPECT_EQUAL( "ActorB", listTickOrder[1] );
    }
}

// ------------------------------------------------------------------------------
// ComponentSubTickHybridTest — 하이브리드 서브틱 (TickPhase + Prerequisite DAG)
// ------------------------------------------------------------------------------

/**
 * @brief [ComponentSubTickHybridTest] 동일 컴포넌트 내의 복수 서브틱이 Phase 및 우선순위 순서대로 완벽히 정렬되는지 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, IntraComponentMultiSubTickPhaseAndPriorityOrder )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pActor = manager.createGameObject( hashed_string( "SkeletalActor" ) );

    auto* pComp          = pActor->addComponent<MockRootComponent>();
    pComp->_componentTag = "Skeletal";

    vector<string> listTickOrder;
    pComp->_pTickOrderLog = &listTickOrder;

    // 메인 틱은 PrePhysics (애니메이션 평가)
    pComp->setTickGroup( sw::TickGroup::PrePhysics );

    // PostPhysics 단계에 3개의 서브틱을 '역순(Finalize -> Normal -> Early)'으로 등록
    constexpr uint32 kSubTickEarly    = 1;
    constexpr uint32 kSubTickNormal   = 2;
    constexpr uint32 kSubTickFinalize = 3;

    pComp->registerSubTick( sw::TickGroup::PostPhysics, kSubTickFinalize, sw::TickPhase::Finalize );
    pComp->registerSubTick( sw::TickGroup::PostPhysics, kSubTickNormal, sw::TickPhase::Normal );
    pComp->registerSubTick( sw::TickGroup::PostPhysics, kSubTickEarly, sw::TickPhase::Early );

    manager.tick( 0.016f );

    // 검증:
    // 1. PrePhysics: 메인 틱 (Skeletal)
    // 2. PostPhysics: Early(1) -> Normal(2) -> Finalize(3) 순서로 정확히 정렬되어야 함!
    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), listTickOrder.size() );
    if ( listTickOrder.size() == 4 )
    {
        SW_EXPECT_EQUAL( "Skeletal", listTickOrder[0] );
        SW_EXPECT_EQUAL( "Skeletal_SubTick_1", listTickOrder[1] );
        SW_EXPECT_EQUAL( "Skeletal_SubTick_2", listTickOrder[2] );
        SW_EXPECT_EQUAL( "Skeletal_SubTick_3", listTickOrder[3] );
    }
}

/**
 * @brief [ComponentSubTickHybridTest] 서로 다른 액터/컴포넌트 간 Prerequisite DAG에 의해 선행 서브틱이 후행보다 먼저 실행되도록 위상 승격되는지 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, InterComponentPrerequisiteDAGDependencyElevation )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pHorseActor = manager.createGameObject( hashed_string( "HorseActor" ) );
    sw::GameObject*       pRiderActor = manager.createGameObject( hashed_string( "RiderActor" ) );

    auto* pHorseComp          = pHorseActor->addComponent<MockRootComponent>();
    pHorseComp->_componentTag = "Horse";
    pHorseComp->setCanEverTick( false ); // 메인 틱 제외

    auto* pRiderComp          = pRiderActor->addComponent<MockRootComponent>();
    pRiderComp->_componentTag = "Rider";
    pRiderComp->setCanEverTick( false ); // 메인 틱 제외

    vector<string> listTickOrder;
    pHorseComp->_pTickOrderLog = &listTickOrder;
    pRiderComp->_pTickOrderLog = &listTickOrder;

    // 말: PostPhysics의 Normal Phase (64)
    constexpr uint32  kHorseTick  = 10;
    sw::SubTickHandle horseHandle = pHorseComp->registerSubTick( sw::TickGroup::PostPhysics, kHorseTick, sw::TickPhase::Normal );

    // 기수: PostPhysics의 Early Phase (0)
    // 일반적인 Phase 정렬만으로는 Early(기수)가 Normal(말)보다 먼저 실행되게 됨
    constexpr uint32 kRiderTick = 20;
    pRiderComp->registerSubTick( sw::TickGroup::PostPhysics, kRiderTick, sw::TickPhase::Early );

    // 하지만 기수가 말의 틱에 종속성(Prerequisite)을 추가함!
    const bool bAdded = pRiderComp->addSubTickPrerequisite( kRiderTick, horseHandle );
    SW_EXPECT_EQUAL( true, bAdded );

    manager.tick( 0.016f );

    // Prerequisite DAG 위상 정렬에 의해 반드시 말(Horse)이 먼저 돌고 기수(Rider)가 돌아야 함!
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
    if ( listTickOrder.size() == 2 )
    {
        SW_EXPECT_EQUAL( "Horse_SubTick_10", listTickOrder[0] );
        SW_EXPECT_EQUAL( "Rider_SubTick_20", listTickOrder[1] );
    }
}

/**
 * @brief [ComponentSubTickHybridTest] 서브틱 동적 활성화/비활성화(setSubTickActive) 및 등록 해제(unregisterSubTick) 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, SubTickDynamicLifecycleAndActiveToggle )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pActor = manager.createGameObject( hashed_string( "DynamicActor" ) );

    auto* pComp          = pActor->addComponent<MockRootComponent>();
    pComp->_componentTag = "Actor";
    pComp->setCanEverTick( true );

    vector<string> listTickOrder;
    pComp->_pTickOrderLog = &listTickOrder;

    constexpr uint32 kSubTick1 = 1;
    constexpr uint32 kSubTick2 = 2;

    pComp->registerSubTick( sw::TickGroup::DuringPhysics, kSubTick1, sw::TickPhase::Normal );
    pComp->registerSubTick( sw::TickGroup::PostPhysics, kSubTick2, sw::TickPhase::Normal );

    // Frame 1: 메인 틱 + SubTick 1 + SubTick 2 실행
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), listTickOrder.size() );

    // 동적 제어: SubTick 1 비활성화, SubTick 2 등록 해제
    listTickOrder.clear();
    pComp->setSubTickActive( kSubTick1, false );
    const bool bUnregistered = pComp->unregisterSubTick( kSubTick2 );
    SW_EXPECT_EQUAL( true, bUnregistered );

    // Frame 2: 메인 틱만 실행되어야 함
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), listTickOrder.size() );
    if ( listTickOrder.size() == 1 )
    {
        SW_EXPECT_EQUAL( "Actor", listTickOrder[0] );
    }

    // 동적 제어: SubTick 1 다시 활성화
    listTickOrder.clear();
    pComp->setSubTickActive( kSubTick1, true );

    // Frame 3: 메인 틱 + SubTick 1 실행
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
    if ( listTickOrder.size() == 2 )
    {
        SW_EXPECT_EQUAL( "Actor", listTickOrder[0] );
        SW_EXPECT_EQUAL( "Actor_SubTick_1", listTickOrder[1] );
    }
}

/**
 * @brief [ComponentSubTickHybridTest] 순환 종속성(Circular Dependency) 발생 시 데드락/크래시 없이 방어 및 안전 실행 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, CircularPrerequisiteDependencyCycleResilience )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pActorA = manager.createGameObject( hashed_string( "ActorA" ) );
    sw::GameObject*       pActorB = manager.createGameObject( hashed_string( "ActorB" ) );

    auto* pCompA          = pActorA->addComponent<MockRootComponent>();
    pCompA->_componentTag = "ActorA";
    pCompA->setCanEverTick( false );

    auto* pCompB          = pActorB->addComponent<MockRootComponent>();
    pCompB->_componentTag = "ActorB";
    pCompB->setCanEverTick( false );

    vector<string> listTickOrder;
    pCompA->_pTickOrderLog = &listTickOrder;
    pCompB->_pTickOrderLog = &listTickOrder;

    sw::SubTickHandle handleA = pCompA->registerSubTick( sw::TickGroup::DuringPhysics, 1, sw::TickPhase::Early );
    sw::SubTickHandle handleB = pCompB->registerSubTick( sw::TickGroup::DuringPhysics, 2, sw::TickPhase::Late );

    // A는 B에 의존하고, B는 A에 의존하는 상호 순환 참조(Cycle) 형성
    pCompA->addSubTickPrerequisite( 1, handleB );
    pCompB->addSubTickPrerequisite( 2, handleA );

    // 틱 실행: 무한루프나 크래시 없이 안전하게 실행 완료되어야 함
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
}

/**
 * @brief [ComponentSubTickHybridTest] 다단계 부모-자식 계층(Grandparent->Parent->Child), 다중 컴포넌트, 다중 서브틱 및 계층을 넘나드는 Prerequisite DAG 위상 정렬 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, DeepHierarchyMultiComponentMultiSubTickDAGOrder )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* pGrandparent = manager.createGameObject( sw::hashed_string( "Grandparent" ) );
    sw::GameObject* pParent      = manager.createGameObject( sw::hashed_string( "Parent" ) );
    sw::GameObject* pChild       = manager.createGameObject( sw::hashed_string( "Child" ) );

    pParent->attachToParent( pGrandparent );
    pChild->attachToParent( pParent );

    vector<string> listTickOrder;

    // 1. Grandparent 컴포넌트들
    auto* pCompGP1           = pGrandparent->addComponent<MockRootComponent>();
    pCompGP1->_componentTag  = "GP1";
    pCompGP1->_pTickOrderLog = &listTickOrder;
    pCompGP1->setTickGroup( sw::TickGroup::PrePhysics ); // Main: PrePhysics
    const sw::SubTickHandle hGP1_PostPhysLate = pCompGP1->registerSubTick( sw::TickGroup::PostPhysics, 1, sw::TickPhase::Late );
    pCompGP1->registerSubTick( sw::TickGroup::PostUpdate, 2, sw::TickPhase::Finalize );

    auto* pCompGP2           = pGrandparent->addComponent<MockRootComponent>();
    pCompGP2->_componentTag  = "GP2";
    pCompGP2->_pTickOrderLog = &listTickOrder;
    pCompGP2->setCanEverTick( false ); // 메인 틱 끔
    pCompGP2->registerSubTick( sw::TickGroup::DuringPhysics, 10, sw::TickPhase::Normal );

    // 2. Parent 컴포넌트들
    auto* pCompP1           = pParent->addComponent<MockRootComponent>();
    pCompP1->_componentTag  = "P1";
    pCompP1->_pTickOrderLog = &listTickOrder;
    pCompP1->setTickGroup( sw::TickGroup::DuringPhysics ); // Main: DuringPhysics
    pCompP1->registerSubTick( sw::TickGroup::PrePhysics, 1, sw::TickPhase::Early );
    pCompP1->registerSubTick( sw::TickGroup::PostPhysics, 2, sw::TickPhase::Early );

    auto* pCompP2           = pParent->addComponent<MockRootComponent>();
    pCompP2->_componentTag  = "P2";
    pCompP2->_pTickOrderLog = &listTickOrder;
    pCompP2->setTickGroup( sw::TickGroup::PostPhysics ); // Main: PostPhysics (Normal Phase)
    pCompP2->registerSubTick( sw::TickGroup::DuringPhysics, 20, sw::TickPhase::Late );

    // 3. Child 컴포넌트들
    auto* pCompC1           = pChild->addComponent<MockRootComponent>();
    pCompC1->_componentTag  = "C1";
    pCompC1->_pTickOrderLog = &listTickOrder;
    pCompC1->setCanEverTick( false ); // 메인 틱 끔
    const sw::SubTickHandle hC1_PrePhysNormal  = pCompC1->registerSubTick( sw::TickGroup::PrePhysics, 100, sw::TickPhase::Normal );
    const sw::SubTickHandle hC1_PostPhysNormal = pCompC1->registerSubTick( sw::TickGroup::PostPhysics, 101, sw::TickPhase::Normal );

    auto* pCompC2           = pChild->addComponent<MockRootComponent>();
    pCompC2->_componentTag  = "C2";
    pCompC2->_pTickOrderLog = &listTickOrder;
    pCompC2->setTickGroup( sw::TickGroup::PostUpdate ); // Main: PostUpdate (Normal Phase)
    pCompC2->registerSubTick( sw::TickGroup::PostPhysics, 200, sw::TickPhase::Early );

    // 계층을 넘나드는 선행 종속성 (Cross-Hierarchy DAG Prerequisites) 설정:
    // A) PrePhysics: Child(C1_100, Normal)이 Parent(P1_1, Early)보다 먼저 돌도록 Parent에 선행 조건 등록
    pCompP1->addSubTickPrerequisite( 1, hC1_PrePhysNormal );

    // B) PostPhysics: 체인 의존성
    //    GP1_1(Late) -> C1_101(Normal) -> P1_2(Early)
    //    (Late가 먼저 실행되도록 위상 승격)
    pCompC1->addSubTickPrerequisite( 101, hGP1_PostPhysLate );
    pCompP1->addSubTickPrerequisite( 2, hC1_PostPhysNormal );

    manager.tick( 0.016f );

    // 총 13개 틱 아이템 실행 검증 (메인틱 4개 + 서브틱 9개)
    SW_EXPECT_EQUAL( static_cast<size_t>( 13 ), listTickOrder.size() );

    // 헬퍼: 틱 로그에서 특정 항목의 인덱스 검색
    auto findIndex = [&listTickOrder]( const string& tag ) -> size_t
    {
        for ( size_t index = 0; index < listTickOrder.size(); ++index )
        {
            if ( listTickOrder[index] == tag )
                return index;
        }
        return static_cast<size_t>( -1 );
    };

    const size_t idxC1_PrePhys100 = findIndex( "C1_SubTick_100" );
    const size_t idxP1_PrePhys1   = findIndex( "P1_SubTick_1" );
    const size_t idxGP1_MainPre   = findIndex( "GP1" );

    const size_t idxGP2_DurPhys10  = findIndex( "GP2_SubTick_10" );
    const size_t idxP1_DurPhysMain = findIndex( "P1" );
    const size_t idxP2_DurPhys20   = findIndex( "P2_SubTick_20" );

    const size_t idxGP1_PostPhysLate = findIndex( "GP1_SubTick_1" );
    const size_t idxC1_PostPhysNorm  = findIndex( "C1_SubTick_101" );
    const size_t idxP1_PostPhysEarly = findIndex( "P1_SubTick_2" );

    const size_t idxC2_PostUpdateMain = findIndex( "C2" );
    const size_t idxGP1_PostUpFin     = findIndex( "GP1_SubTick_2" );

    // 1) PrePhysics 그룹 내 위상 승격 검증: C1_100 -> P1_1
    SW_EXPECT_TRUE( idxC1_PrePhys100 < idxP1_PrePhys1 );
    // PrePhysics 항목들은 모두 DuringPhysics 항목들보다 먼저 실행되어야 함
    SW_EXPECT_TRUE( idxP1_PrePhys1 < idxGP2_DurPhys10 );
    SW_EXPECT_TRUE( idxGP1_MainPre < idxP1_DurPhysMain );

    // 2) DuringPhysics 항목들은 모두 PostPhysics 항목들보다 먼저 실행되어야 함
    SW_EXPECT_TRUE( idxP2_DurPhys20 < idxGP1_PostPhysLate );

    // 3) PostPhysics 체인 의존성 검증: GP1_1 (Late) -> C1_101 (Normal) -> P1_2 (Early)
    SW_EXPECT_TRUE( idxGP1_PostPhysLate < idxC1_PostPhysNorm );
    SW_EXPECT_TRUE( idxC1_PostPhysNorm < idxP1_PostPhysEarly );

    // 4) PostPhysics 항목들은 모두 PostUpdate 항목들보다 먼저 실행되어야 함
    SW_EXPECT_TRUE( idxP1_PostPhysEarly < idxC2_PostUpdateMain );
    SW_EXPECT_TRUE( idxC2_PostUpdateMain < idxGP1_PostUpFin );
}

/**
 * @brief [ComponentSubTickHybridTest] 틱 실행 도중(Mid-Tick) 서브틱이 동적으로 비활성화되거나 등록 해제되는 경우 즉시 스킵되는지 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, MidTickSubTickDeactivationAndCancellation )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* pActorA = manager.createGameObject( sw::hashed_string( "ActorA" ) );
    sw::GameObject* pActorB = manager.createGameObject( sw::hashed_string( "ActorB" ) );

    auto* pCompA          = pActorA->addComponent<MockMidTickDeactivatorComponent>();
    pCompA->_componentTag = "A";

    auto* pCompB          = pActorB->addComponent<MockRootComponent>();
    pCompB->_componentTag = "B";
    pCompB->setCanEverTick( false );

    vector<string> listTickOrder;
    pCompA->_pTickOrderLog = &listTickOrder;
    pCompB->_pTickOrderLog = &listTickOrder;

    // PostPhysics 단계 설정
    // A: SubTick 1 (Early) - 실행 시 B의 SubTick 20을 비활성화하고 자신의 SubTick 2를 unregister
    pCompA->registerSubTick( sw::TickGroup::PostPhysics, 1, sw::TickPhase::Early );
    pCompA->registerSubTick( sw::TickGroup::PostPhysics, 2, sw::TickPhase::Late );

    // B: SubTick 20 (Normal), SubTick 21 (Finalize)
    pCompB->registerSubTick( sw::TickGroup::PostPhysics, 20, sw::TickPhase::Normal );
    pCompB->registerSubTick( sw::TickGroup::PostPhysics, 21, sw::TickPhase::Finalize );

    pCompA->_pTargetComp             = pCompB;
    pCompA->_targetSubTickId         = 20;
    pCompA->_selfSubTickToUnregister = 2;

    // Frame 1: A_1(Early) 실행 시 B_20과 A_2를 끔 -> B_20과 A_2는 스킵되고 B_21(Finalize)만 실행
    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
    if ( listTickOrder.size() == 2 )
    {
        SW_EXPECT_EQUAL( "A_SubTick_1", listTickOrder[0] );
        SW_EXPECT_EQUAL( "B_SubTick_21", listTickOrder[1] );
    }

    // Frame 2: B의 SubTick 20을 다시 켜고 A의 동적 비활성화 트리거 해제
    listTickOrder.clear();
    pCompA->_pTargetComp     = nullptr;
    pCompA->_targetSubTickId = 0;
    pCompB->setSubTickActive( 20, true );

    manager.tick( 0.016f );

    // Frame 2에서는 A_1, B_20, B_21 세 개가 모두 정상 실행되어야 함 (A_2는 unregister되었으므로 미실행)
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), listTickOrder.size() );
    if ( listTickOrder.size() == 3 )
    {
        SW_EXPECT_EQUAL( "A_SubTick_1", listTickOrder[0] );
        SW_EXPECT_EQUAL( "B_SubTick_20", listTickOrder[1] );
        SW_EXPECT_EQUAL( "B_SubTick_21", listTickOrder[2] );
    }
}

/**
 * @brief [ComponentSubTickHybridTest] 부모-자식 계층에서 서브트리 비활성화, 재부모화(Reparenting), 연쇄 파괴 시 서브틱 라이프사이클 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, HierarchySubtreeDeactivationAndReparentingWithSubTicks )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* pRoot    = manager.createGameObject( sw::hashed_string( "Root" ) );
    sw::GameObject* pBranch1 = manager.createGameObject( sw::hashed_string( "Branch1" ) );
    sw::GameObject* pLeaf1   = manager.createGameObject( sw::hashed_string( "Leaf1" ) );
    sw::GameObject* pBranch2 = manager.createGameObject( sw::hashed_string( "Branch2" ) );
    sw::GameObject* pLeaf2   = manager.createGameObject( sw::hashed_string( "Leaf2" ) );

    vector<string> listTickOrder;

    auto setupActor = [&listTickOrder]( sw::GameObject* pObj, const string& tag )
    {
        auto* pComp           = pObj->addComponent<MockRootComponent>();
        pComp->_componentTag  = tag;
        pComp->_pTickOrderLog = &listTickOrder;
        pComp->setCanEverTick( false ); // 메인 틱 제외
        pComp->registerSubTick( sw::TickGroup::DuringPhysics, 1, sw::TickPhase::Early );
        pComp->registerSubTick( sw::TickGroup::PostPhysics, 2, sw::TickPhase::Normal );
    };

    // 계층 부착 전 컴포넌트(SceneComponent)를 먼저 생성
    setupActor( pRoot, "Root" );
    setupActor( pBranch1, "Branch1" );
    setupActor( pLeaf1, "Leaf1" );
    setupActor( pBranch2, "Branch2" );
    setupActor( pLeaf2, "Leaf2" );

    pBranch1->attachToParent( pRoot );
    pLeaf1->attachToParent( pBranch1 );
    pBranch2->attachToParent( pRoot );
    pLeaf2->attachToParent( pBranch2 );

    // Frame 1: 5개 액터 전체 활성 (각 2개 서브틱 = 총 10개)
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 10 ), listTickOrder.size() );

    // Frame 2: Branch1 비활성화 -> Branch1 및 Leaf1 서브트리 전체 틱 스킵 (Root, Branch2, Leaf2 = 총 6개)
    listTickOrder.clear();
    pBranch1->setActive( false );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 6 ), listTickOrder.size() );

    // Frame 3: Leaf1을 비활성화된 Branch1에서 활성화된 Branch2 밑으로 Reparent
    listTickOrder.clear();
    pLeaf1->attachToParent( pBranch2 );
    SW_EXPECT_TRUE( pLeaf1->isActiveInHierarchy() );
    manager.tick( 0.016f );
    // Root, Branch2, Leaf2, Leaf1 = 총 8개 실행 (Branch1만 스킵)
    SW_EXPECT_EQUAL( static_cast<size_t>( 8 ), listTickOrder.size() );

    // Frame 4: Branch2 연쇄 삭제 (Branch2, Leaf2, Leaf1 삭제) -> Root만 남음 (2개)
    listTickOrder.clear();
    manager.destroyObject( pBranch2, true );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
}

/**
 * @brief [ComponentSubTickHybridTest] 100개 이상의 액터, 300개 컴포넌트, 600개 서브틱의 고밀도 다이아몬드 DAG 및 체인 종속성 멀티스레드 스트레스 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, MassiveSubTickStressAndMultiThreadedDAGValidation )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    constexpr size_t kActorCount    = 100;
    constexpr size_t kTotalSubTicks = kActorCount * 6; // 600개 서브틱

    std::atomic<uint32> globalTickSeq{ 1 };
    std::atomic<uint32> arrExecutionOrder[kTotalSubTicks + 16];
    for ( size_t index = 0; index < kTotalSubTicks + 16; ++index )
        arrExecutionOrder[index].store( 0, std::memory_order_relaxed );

    vector<sw::GameObject*>             listActor;
    vector<MockSubTickStressComponent*> listCompA;
    vector<MockSubTickStressComponent*> listCompB;
    listActor.reserve( kActorCount );
    listCompA.reserve( kActorCount );
    listCompB.reserve( kActorCount );

    struct DagEdge
    {
        uint32 _prereqGlobalId;
        uint32 _dependentGlobalId;
    };
    vector<DagEdge> listDagEdge;

    for ( size_t actorIdx = 0; actorIdx < kActorCount; ++actorIdx )
    {
        sw::fixed_string<64> nameBuf{};
        sw::formatstring( nameBuf.data(), nameBuf.capacity(), "StressActor_%#", actorIdx );
        sw::GameObject* pActor = manager.createGameObject( sw::hashed_string( nameBuf.c_str() ) );
        listActor.push_back( pActor );

        // 컴포넌트 2개 부착
        auto* pCompA = pActor->addComponent<MockSubTickStressComponent>();
        auto* pCompB = pActor->addComponent<MockSubTickStressComponent>();

        pCompA->setCanEverTick( false );
        pCompB->setCanEverTick( false );

        pCompA->_pGlobalTickSequence   = &globalTickSeq;
        pCompA->_pExecutionOrderArray  = arrExecutionOrder;
        pCompA->_subTickGlobalIdOffset = static_cast<uint32>( actorIdx * 6 );

        pCompB->_pGlobalTickSequence   = &globalTickSeq;
        pCompB->_pExecutionOrderArray  = arrExecutionOrder;
        pCompB->_subTickGlobalIdOffset = static_cast<uint32>( actorIdx * 6 + 3 );

        // SubTick 등록 (CompA: 1, 2 / CompB: 1, 2)
        pCompA->registerSubTick( sw::TickGroup::DuringPhysics, 1, sw::TickPhase::Early );
        pCompA->registerSubTick( sw::TickGroup::PostPhysics, 2, sw::TickPhase::Normal );

        pCompB->registerSubTick( sw::TickGroup::DuringPhysics, 1, sw::TickPhase::Normal );
        pCompB->registerSubTick( sw::TickGroup::PostPhysics, 2, sw::TickPhase::Late );
        listCompA.push_back( pCompA );
        listCompB.push_back( pCompB );
    }

    // 1. 다이아몬드 DAG 종속성 30개 생성:
    // Node A(DuringPhysics, CompA_1) -> Node B(DuringPhysics, CompB_1)
    // Node A(DuringPhysics, CompA_1) -> Node C(DuringPhysics, nextActor CompA_1)
    // Node B, C -> Node D(DuringPhysics, nextActor CompB_1)
    for ( size_t diamondIdx = 0; diamondIdx < 30; ++diamondIdx )
    {
        const size_t actorAIdx = diamondIdx * 2;
        const size_t actorBIdx = diamondIdx * 2 + 1;

        auto* pCompA1 = listCompA[actorAIdx];
        auto* pCompB1 = listCompB[actorBIdx];

        const sw::SubTickHandle hA = sw::SubTickHandle{ pCompA1->getComponentId(), 1 };

        pCompB1->addSubTickPrerequisite( 1, hA );

        const uint32 gIdA = static_cast<uint32>( actorAIdx * 6 + 1 );
        const uint32 gIdB = static_cast<uint32>( actorBIdx * 6 + 3 + 1 );
        listDagEdge.push_back( { gIdA, gIdB } );
    }

    // 2. 10단계 긴 의존성 체인 생성 (PostPhysics)
    // T0 -> T1 -> T2 -> ... -> T9
    for ( size_t chainIdx = 0; chainIdx < 9; ++chainIdx )
    {
        auto* pCompSrc = listCompA[chainIdx];
        auto* pCompDst = listCompA[chainIdx + 1];

        const sw::SubTickHandle hSrc = sw::SubTickHandle{ pCompSrc->getComponentId(), 2 };
        pCompDst->addSubTickPrerequisite( 2, hSrc );

        const uint32 gIdSrc = static_cast<uint32>( chainIdx * 6 + 2 );
        const uint32 gIdDst = static_cast<uint32>( ( chainIdx + 1 ) * 6 + 2 );
        listDagEdge.push_back( { gIdSrc, gIdDst } );
    }

    // 5 프레임 동안 멀티스레드 스트레스 틱 실행 및 매 프레임 DAG 위상 정렬 정밀 검증
    for ( int32 frame = 0; frame < 5; ++frame )
    {
        globalTickSeq.store( 1, std::memory_order_relaxed );

        manager.tick( 0.016f );

        // 모든 등록된 선행 의존성 엣지에 대해 선행 노드가 후행 노드보다 먼저 실행되었는지 검증
        for ( const DagEdge& edge : listDagEdge )
        {
            const uint32 orderPrereq    = arrExecutionOrder[edge._prereqGlobalId].load( std::memory_order_acquire );
            const uint32 orderDependent = arrExecutionOrder[edge._dependentGlobalId].load( std::memory_order_acquire );

            SW_EXPECT_TRUE( orderPrereq != 0 );
            SW_EXPECT_TRUE( orderDependent != 0 );
            SW_EXPECT_TRUE( orderPrereq < orderDependent );
        }
    }
}

// ------------------------------------------------------------------------------
// 9) HierarchicalActiveStateTest — 비활성 서브트리 틱 스킵
// ------------------------------------------------------------------------------
/**
 * @brief [HierarchicalActiveStateTest] 비활성 서브트리 틱 스킵
 */
SW_TEST_CASE( HierarchicalActiveStateTest, SubtreeTickSkip )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );
    sw::GameObject*    actorPtr = manager.createGameObject( sw::hashed_string( "ActiveTestActor" ) );
    sw::GameObject&    actor    = *actorPtr;
    MockMeshComponent* comp     = actor.addComponent<MockMeshComponent>();

    actor.setActive( false );
    SW_EXPECT_FALSE( actor.isActiveInHierarchy() );

    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( 0, comp->_tickCount );
}

// ------------------------------------------------------------------------------
// 10) GameObjectHierarchyTest — attach 와 active 전파
// ------------------------------------------------------------------------------
/**
 * @brief [GameObjectHierarchyTest] 부모-자식 attach 와 active 전파
 */
SW_TEST_CASE( GameObjectHierarchyTest, ParentChildAttachAndActivePropagation )
{
    sw::GameObjectManager manager;
    sw::GameObject*       parentPtr = manager.createGameObject( sw::hashed_string( "ParentGO" ) );
    sw::GameObject&       parent    = *parentPtr;
    sw::GameObject*       childPtr  = manager.createGameObject( sw::hashed_string( "ChildGO" ) );
    sw::GameObject&       child     = *childPtr;
    sw::GameObject*       grandPtr  = manager.createGameObject( sw::hashed_string( "GrandGO" ) );
    sw::GameObject&       grand     = *grandPtr;

    parent.addComponent<sw::SceneComponent>();
    child.addComponent<sw::SceneComponent>();
    grand.addComponent<sw::SceneComponent>();

    SW_EXPECT_TRUE( child.attachToParent( &parent ) );
    SW_EXPECT_TRUE( grand.attachToParent( &child ) );
    SW_EXPECT_EQUAL( &parent, child.getParent() );
    SW_EXPECT_EQUAL( &child, grand.getParent() );
    SW_EXPECT_EQUAL( size_t( 1 ), parent.getChildren().size() );
    SW_EXPECT_EQUAL( &child, parent.getChildren()[0] );

    // 순환 attach 는 실패해야 한다.
    SW_EXPECT_FALSE( parent.attachToParent( &grand ) );

    parent.setActive( false );
    SW_EXPECT_FALSE( parent.isActiveInHierarchy() );
    SW_EXPECT_FALSE( child.isActiveInHierarchy() );
    SW_EXPECT_FALSE( grand.isActiveInHierarchy() );

    parent.setActive( true );
    SW_EXPECT_TRUE( child.isActiveInHierarchy() );
    SW_EXPECT_TRUE( grand.isActiveInHierarchy() );

    child.setActive( false );
    SW_EXPECT_TRUE( parent.isActiveInHierarchy() );
    SW_EXPECT_FALSE( child.isActiveInHierarchy() );
    SW_EXPECT_FALSE( grand.isActiveInHierarchy() );

    grand.detachFromParent();
    SW_EXPECT_NULL( grand.getParent() );
    SW_EXPECT_EQUAL( size_t( 0 ), child.getChildren().size() );
    // 분리된 grand 는 다시 루트이고, 자체 active 는 true 이다.
    SW_EXPECT_TRUE( grand.isActive() );
    SW_EXPECT_TRUE( grand.isActiveInHierarchy() );
}

/**
 * @brief [GameObjectHierarchyTest] isDescendantOf는 자신과 조상 체인을 포함한다
 */
SW_TEST_CASE( GameObjectHierarchyTest, IsDescendantOfWalksParentChain )
{
    sw::GameObjectManager manager;
    sw::GameObject*       parentPtr = manager.createGameObject( sw::hashed_string( "AncestorGO" ) );
    sw::GameObject&       parent    = *parentPtr;
    sw::GameObject*       childPtr  = manager.createGameObject( sw::hashed_string( "ChildGO" ) );
    sw::GameObject&       child     = *childPtr;
    sw::GameObject*       grandPtr  = manager.createGameObject( sw::hashed_string( "GrandGO" ) );
    sw::GameObject&       grand     = *grandPtr;
    sw::GameObject*       otherPtr  = manager.createGameObject( sw::hashed_string( "OtherGO" ) );
    sw::GameObject&       other     = *otherPtr;

    parent.addComponent<sw::SceneComponent>();
    child.addComponent<sw::SceneComponent>();
    grand.addComponent<sw::SceneComponent>();
    other.addComponent<sw::SceneComponent>();

    SW_ASSERT_TRUE( child.attachToParent( &parent ) );
    SW_ASSERT_TRUE( grand.attachToParent( &child ) );

    SW_EXPECT_TRUE( parent.isDescendantOf( &parent ) );
    SW_EXPECT_TRUE( child.isDescendantOf( &parent ) );
    SW_EXPECT_TRUE( grand.isDescendantOf( &parent ) );
    SW_EXPECT_TRUE( grand.isDescendantOf( &child ) );
    SW_EXPECT_FALSE( parent.isDescendantOf( &child ) );
    SW_EXPECT_FALSE( other.isDescendantOf( &parent ) );
    SW_EXPECT_FALSE( parent.isDescendantOf( nullptr ) );
}

// ------------------------------------------------------------------------------
// 11) PostEditChangePropertyTest — 프로퍼티 변경 콜백
// ------------------------------------------------------------------------------
/**
 * @brief [PostEditChangePropertyTest] 프로퍼티 변경 콜백
 */
SW_TEST_CASE( PostEditChangePropertyTest, CallbackOnPropertyChanged )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );
    sw::GameObject*        actorPtr = manager.createGameObject( sw::hashed_string( "PropertyChangedActor" ) );
    sw::GameObject&        actor    = *actorPtr;
    MockCallbackComponent* comp     = actor.addComponent<MockCallbackComponent>();

    comp->setActive( false );
    // 프로퍼티 변경 알림은 동작해야 하지만, 정확한 이름은 리플렉션 시스템에 따라 달라질 수 있다
    SW_EXPECT_TRUE( comp->_lastChangedProperty.getHash() != 0 );
}

// ------------------------------------------------------------------------------
// 12) GameObjectTest — 부착·틱·리플렉션·태그
// ------------------------------------------------------------------------------
/**
 * @brief [GameObjectTest] 중복 GameObject 이름은 조회 맵을 덮어쓰지 않고 고유화한다
 */
SW_TEST_CASE( GameObjectTest, DuplicateNameUniquifies )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing duplicate GameObject name auto-uniquification" );
    sw::GameObjectManager manager;
    sw::GameObject*       first  = manager.createGameObject( sw::hashed_string( "DupName" ) );
    sw::GameObject*       second = manager.createGameObject( sw::hashed_string( "DupName" ) );
    SW_ASSERT_NOT_NULL( first );
    SW_ASSERT_NOT_NULL( second );
    SW_EXPECT_TRUE( first != second );
    SW_EXPECT_STREQ( "DupName", first->getName().c_str() );
    SW_EXPECT_TRUE( first->getName() != second->getName() );
    SW_EXPECT_TRUE( manager.findGameObjectByName( first->getName() ) == first );
    SW_EXPECT_TRUE( manager.findGameObjectByName( second->getName() ) == second );
}

/**
 * @brief [GameObjectTest] attachToParent가 어긋난 SceneComponent 부모를 Unreal처럼 재부모한다
 */
SW_TEST_CASE( GameObjectTest, AttachToParentReparentsPrimarySceneComponent )
{
    sw::GameObjectManager manager;
    GameObject*           parent = manager.createGameObject( hashed_string( "AlignParent" ) );
    GameObject*           child  = manager.createGameObject( hashed_string( "AlignChild" ) );
    GameObject*           other  = manager.createGameObject( hashed_string( "AlignOther" ) );
    SW_ASSERT_NOT_NULL( parent );
    SW_ASSERT_NOT_NULL( child );
    SW_ASSERT_NOT_NULL( other );

    parent->addComponent<SceneComponent>();
    child->addComponent<SceneComponent>();
    other->addComponent<SceneComponent>();

    SceneComponent* parentSc = parent->getComponent<SceneComponent>();
    SceneComponent* childSc  = child->getComponent<SceneComponent>();
    SceneComponent* otherSc  = other->getComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( parentSc );
    SW_ASSERT_NOT_NULL( childSc );
    SW_ASSERT_NOT_NULL( otherSc );

    SW_ASSERT_TRUE( childSc->attachToComponent( otherSc ) );
    SW_EXPECT_EQUAL( otherSc, childSc->getParent() );

    SW_ASSERT_TRUE( child->attachToParent( parent ) );
    SW_EXPECT_EQUAL( parent, child->getParent() );
    SW_EXPECT_EQUAL( parentSc, childSc->getParent() );
}

/**
 * @brief [GameObjectManagerTest] 틱 중 destroy는 같은 프레임에 pending-kill 후 제거된다
 */
SW_TEST_CASE( GameObjectManagerTest, DestroyDuringTickIsDeferredThenApplied )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* keeper = manager.createGameObject( hashed_string( "TickKeeper" ) );
    GameObject* victim = manager.createGameObject( hashed_string( "TickVictim" ) );
    SW_ASSERT_NOT_NULL( keeper );
    SW_ASSERT_NOT_NULL( victim );

    keeper->addComponent<MockMeshComponent>();
    victim->addComponent<MockMeshComponent>();
    MockMeshComponent* keeperMesh = keeper->getComponent<MockMeshComponent>();
    MockMeshComponent* victimMesh = victim->getComponent<MockMeshComponent>();
    SW_ASSERT_NOT_NULL( keeperMesh );
    SW_ASSERT_NOT_NULL( victimMesh );

    keeperMesh->_pTickDestroyManager = &manager;
    keeperMesh->_pTickDestroyObject  = victim;

    manager.tick( 0.016f );
    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "TickVictim" ) ) );
    SW_EXPECT_NOT_NULL( manager.findGameObjectByName( hashed_string( "TickKeeper" ) ) );
    SW_EXPECT_EQUAL( 1, keeperMesh->_tickCount );
}

/**
 * @brief [GameObjectTest] 같은 타입 두 컴포넌트 지연 삭제가 포인터가 아니라 핸들로 처리된다
 */
SW_TEST_CASE( GameObjectTest, DeferredDestroyTwoSameTypeComponents )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* a = manager.createGameObject( hashed_string( "DeferredA" ) );
    GameObject* b = manager.createGameObject( hashed_string( "DeferredB" ) );
    a->addComponent<MockMeshComponent>();
    b->addComponent<MockMeshComponent>();
    MockMeshComponent* meshA = a->getComponent<MockMeshComponent>();
    MockMeshComponent* meshB = b->getComponent<MockMeshComponent>();
    SW_ASSERT_NOT_NULL( meshA );
    SW_ASSERT_NOT_NULL( meshB );

    manager.destroyComponent( meshA );
    manager.destroyComponent( meshB );

    SW_EXPECT_EQUAL( 0u, a->getComponentCount() );
    SW_EXPECT_EQUAL( 0u, b->getComponentCount() );
}

/**
 * @brief [GameObjectTest] 틱 중 다른 같은 타입 컴포넌트 제거는 프레임 끝에 적용된다
 */
SW_TEST_CASE( GameObjectTest, TickRemoveOtherSameTypeComponent )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* keeper = manager.createGameObject( hashed_string( "TickKeeperComp" ) );
    GameObject* victim = manager.createGameObject( hashed_string( "TickVictimComp" ) );
    keeper->addComponent<MockMeshComponent>();
    victim->addComponent<MockMeshComponent>();
    MockMeshComponent* keeperMesh = keeper->getComponent<MockMeshComponent>();
    MockMeshComponent* victimMesh = victim->getComponent<MockMeshComponent>();
    SW_ASSERT_NOT_NULL( keeperMesh );
    SW_ASSERT_NOT_NULL( victimMesh );

    keeperMesh->_pTickRemoveOwner = victim;
    keeperMesh->_pTickRemoveComp  = victimMesh;

    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 0u, victim->getComponentCount() );
    SW_EXPECT_EQUAL( 1u, keeper->getComponentCount() );
    SW_EXPECT_EQUAL( 1, keeperMesh->_tickCount );
}

/**
 * @brief [GameObjectTest] 같은 엔티티의 서로 다른 컴포넌트도 모두 tick된다
 */
SW_TEST_CASE( GameObjectTest, SameEntityComponentsBothTick )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* actor = manager.createGameObject( hashed_string( "SameEntityTick" ) );
    actor->addComponent<MockMeshComponent>();
    actor->addComponent<MockAudioComponent>();
    MockMeshComponent*  mesh  = actor->getComponent<MockMeshComponent>();
    MockAudioComponent* audio = actor->getComponent<MockAudioComponent>();
    SW_ASSERT_NOT_NULL( mesh );
    SW_ASSERT_NOT_NULL( audio );

    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 1, mesh->_tickCount );
    SW_EXPECT_EQUAL( 1, audio->_playCount );
}

/**
 * @brief [GameObjectManagerTest] 틱 중 트랜스폼 지연과 destroy가 겹쳐도 resolve가 실패하면 건너뛴다
 */
SW_TEST_CASE( GameObjectManagerTest, DeferredTransformSkipsDestroyedComponent )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* keeper = manager.createGameObject( hashed_string( "TransformKeeper" ) );
    GameObject* victim = manager.createGameObject( hashed_string( "TransformVictim" ) );
    keeper->addComponent<MockMeshComponent>();
    victim->addComponent<SceneComponent>();
    MockMeshComponent* keeperMesh = keeper->getComponent<MockMeshComponent>();
    SceneComponent*    victimSc   = victim->getComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( keeperMesh );
    SW_ASSERT_NOT_NULL( victimSc );

    keeperMesh->_pTickMoveComp       = victimSc;
    keeperMesh->_tickMovePos         = float3( 1.0f, 2.0f, 3.0f );
    keeperMesh->_pTickDestroyManager = &manager;
    keeperMesh->_pTickDestroyObject  = victim;

    manager.tick( 0.016f );
    SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "TransformVictim" ) ) );
    SW_EXPECT_NOT_NULL( manager.findGameObjectByName( hashed_string( "TransformKeeper" ) ) );
}

/**
 * @brief [GameObjectManagerTest] 틱 중 attach는 지연된 뒤 부모-자식 포인터를 연결한다
 */
SW_TEST_CASE( GameObjectManagerTest, DeferredAttachDuringTickApplies )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* parent = manager.createGameObject( hashed_string( "AttachParent" ) );
    GameObject* child  = manager.createGameObject( hashed_string( "AttachChild" ) );
    GameObject* ticker = manager.createGameObject( hashed_string( "AttachTicker" ) );
    parent->addComponent<SceneComponent>();
    child->addComponent<SceneComponent>();
    ticker->addComponent<MockMeshComponent>();
    SceneComponent*    parentSc   = parent->getComponent<SceneComponent>();
    SceneComponent*    childSc    = child->getComponent<SceneComponent>();
    MockMeshComponent* tickerMesh = ticker->getComponent<MockMeshComponent>();
    SW_ASSERT_NOT_NULL( parentSc );
    SW_ASSERT_NOT_NULL( childSc );
    SW_ASSERT_NOT_NULL( tickerMesh );

    tickerMesh->_pTickAttachChild  = childSc;
    tickerMesh->_pTickAttachParent = parentSc;

    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( parentSc, childSc->getParent() );
}

/**
 * @brief [SceneComponentTest] 64비트 정밀도 대규모 월드 좌표계 (LWC) 및 부모-자식 합성 검증
 */
SW_TEST_CASE( SceneComponentTest, LargeWorldCoordinatesHierarchy )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    GameObject* root       = manager.createGameObject( hashed_string( "LWCRoot" ) );
    GameObject* child      = manager.createGameObject( hashed_string( "LWCChild" ) );
    GameObject* grandChild = manager.createGameObject( hashed_string( "LWCGandChild" ) );

    root->addComponent<SceneComponent>();
    child->addComponent<SceneComponent>();
    grandChild->addComponent<SceneComponent>();

    SceneComponent* rootSc       = root->getComponent<SceneComponent>();
    SceneComponent* childSc      = child->getComponent<SceneComponent>();
    SceneComponent* grandChildSc = grandChild->getComponent<SceneComponent>();

    SW_ASSERT_NOT_NULL( rootSc );
    SW_ASSERT_NOT_NULL( childSc );
    SW_ASSERT_NOT_NULL( grandChildSc );

    childSc->attachToComponent( rootSc );
    grandChildSc->attachToComponent( childSc );

    // 로컬 좌표 설정
    rootSc->setLocalPosition( float3( 100.0f, 200.0f, 300.0f ) );
    childSc->setLocalPosition( float3( 10.0f, 20.0f, 30.0f ) );
    grandChildSc->setLocalPosition( float3( 1.0f, 2.0f, 3.0f ) );

    manager.tick( 0.016f );

    const double3 rootLWC = rootSc->getWorldPositionLWC();
    SW_EXPECT_NEAR_EQUAL( 100.0, rootLWC._x, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 200.0, rootLWC._y, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 300.0, rootLWC._z, 0.0001 );

    const double3 childLWC = childSc->getWorldPositionLWC();
    SW_EXPECT_NEAR_EQUAL( 110.0, childLWC._x, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 220.0, childLWC._y, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 330.0, childLWC._z, 0.0001 );

    const double3 grandChildLWC = grandChildSc->getWorldPositionLWC();
    SW_EXPECT_NEAR_EQUAL( 111.0, grandChildLWC._x, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 222.0, grandChildLWC._y, 0.0001 );
    SW_EXPECT_NEAR_EQUAL( 333.0, grandChildLWC._z, 0.0001 );

    // 카메라 상대 4x4 월드 변환 행렬 검증
    const double3  cameraPos( 110.0, 220.0, 330.0 );
    const float4x4 relMatrix = grandChildSc->getCameraRelativeWorldMatrix( cameraPos );
    // 상대 위치는 (111-110, 222-220, 333-330) = (1, 2, 3)
    SW_EXPECT_NEAR_EQUAL( 1.0f, relMatrix._41, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, relMatrix._42, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, relMatrix._43, 0.001f );
}

SW_TEST_CASE( SoftPointerTest, SafeDestruction )
{
    sw::GameObjectManager  manager;
    sw::GameObject*        obj  = manager.createGameObject( sw::hashed_string( "TargetObj" ) );
    sw::MockMeshComponent* mesh = obj->addComponent<sw::MockMeshComponent>();

    sw::GameObjectPtr objPtr( obj );
    sw::ComponentPtr  compPtr( mesh );

    SW_EXPECT_TRUE( objPtr.isValid() );
    SW_EXPECT_TRUE( compPtr.isValid() );

    // Destroy component only
    manager.destroyComponent( mesh );
    manager.tick( 0.016f );

    SW_EXPECT_TRUE( objPtr.isValid() );   // Object is still alive
    SW_EXPECT_FALSE( compPtr.isValid() ); // Component pointer should be safely nullified!

    // Destroy object
    manager.destroyObject( obj );
    manager.tick( 0.016f );

    SW_EXPECT_FALSE( objPtr.isValid() ); // Object pointer should be safely nullified!
}

/**
 * @brief [SceneHierarchyTest] 트랜스폼 더티 전파: 자식 이동 시 부모의 bIsTransformDirty는 0 유지, bHasDirtyDescendant는 1
 */
SW_TEST_CASE( SceneHierarchyTest, TransformDirtyPropagationAndEarlyOut )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pParentObj = manager.createGameObject( sw::hashed_string( "Parent" ) );
    sw::GameObject*       pChildObj  = manager.createGameObject( sw::hashed_string( "Child" ) );

    sw::SceneComponent* pParentSc = pParentObj->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pChildSc  = pChildObj->addComponent<sw::SceneComponent>();

    SW_ASSERT_NOT_NULL( pParentSc );
    SW_ASSERT_NOT_NULL( pChildSc );

    pChildSc->attachToComponent( pParentSc );

    pParentSc->setLocalPosition( sw::float3( 10.0f, 0.0f, 0.0f ) );
    pChildSc->setLocalPosition( sw::float3( 5.0f, 0.0f, 0.0f ) );

    // Force update world matrix to clear dirty flags
    pParentSc->getWorldMatrix();
    pChildSc->getWorldMatrix();

    SW_EXPECT_EQUAL( 0, static_cast<int32>( pParentSc->isTransformDirty() ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( pChildSc->isTransformDirty() ) );

    // Move child only
    pChildSc->setLocalPosition( sw::float3( 20.0f, 0.0f, 0.0f ) );

    // Child must be dirty
    SW_EXPECT_EQUAL( 1, static_cast<int32>( pChildSc->isTransformDirty() ) );
    // Parent transform must NOT be dirty (child move does not dirty parent)
    SW_EXPECT_EQUAL( 0, static_cast<int32>( pParentSc->isTransformDirty() ) );
    // Parent must have bHasDirtyDescendant == 1
    SW_EXPECT_EQUAL( 1, static_cast<int32>( pParentSc->hasDirtyDescendant() ) );

    // When child world matrix is queried, it composes correctly from parent
    const sw::float3 childPos = pChildSc->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 30.0f, childPos._x, 0.001f );
}

/**
 * @brief [GameObjectTest] 틱 도중 지연 컴포넌트 추가 및 틱 종료 후 완전 바인딩 검증
 */
SW_TEST_CASE( GameObjectTest, DeferredComponentAdditionDuringTick )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "DeferredTestObj" ) );
    SW_ASSERT_NOT_NULL( pObj );

    // 틱 실행 구간에서 addComponent 호출 시 deferPostTick에 등록되어 지연 실행
    manager.deferPostTick( [pObj]()
    {
        sw::MockAudioComponent* pAudio = pObj->addComponent<sw::MockAudioComponent>();
        SW_ASSERT_NOT_NULL( pAudio );
        pAudio->_volume = 0.75f;
    } );

    manager.tick( 0.016f );

    sw::MockAudioComponent* pAudioComp = pObj->getComponent<sw::MockAudioComponent>();
    SW_ASSERT_NOT_NULL( pAudioComp );
    SW_EXPECT_EQUAL( pObj, pAudioComp->getOwner() );
    SW_EXPECT_NEAR_EQUAL( 0.75f, pAudioComp->_volume, 0.001f );
}

/**
 * @brief [GameObjectTest] 부모 GameObject 지연 삭제 시 자식 계층 연쇄 삭제 검증
 */
SW_TEST_CASE( GameObjectTest, CascadingChildDestruction )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pParent = manager.createGameObject( sw::hashed_string( "CascadeParent" ) );
    sw::GameObject*       pChild1 = manager.createGameObject( sw::hashed_string( "CascadeChild1" ) );
    sw::GameObject*       pChild2 = manager.createGameObject( sw::hashed_string( "CascadeChild2" ) );

    SW_ASSERT_NOT_NULL( pParent );
    SW_ASSERT_NOT_NULL( pChild1 );
    SW_ASSERT_NOT_NULL( pChild2 );

    pParent->addComponent<sw::SceneComponent>();
    pChild1->addComponent<sw::SceneComponent>();
    pChild2->addComponent<sw::SceneComponent>();

    SW_EXPECT_TRUE( pChild1->attachToParent( pParent ) );
    SW_EXPECT_TRUE( pChild2->attachToParent( pChild1 ) );

    const sw::GameObjectPtr parentPtr( pParent );
    const sw::GameObjectPtr child1Ptr( pChild1 );
    const sw::GameObjectPtr child2Ptr( pChild2 );

    SW_EXPECT_TRUE( parentPtr.isValid() );
    SW_EXPECT_TRUE( child1Ptr.isValid() );
    SW_EXPECT_TRUE( child2Ptr.isValid() );

    // Destroy parent with bDestroyChildren = true
    manager.destroyObject( pParent, true );

    // Before tick, all are marked pending kill
    SW_EXPECT_TRUE( pParent->isPendingKill() );
    SW_EXPECT_TRUE( pChild1->isPendingKill() );
    SW_EXPECT_TRUE( pChild2->isPendingKill() );

    manager.tick( 0.016f );

    // After tick, all pointers should be safely nullified/invalid
    SW_EXPECT_FALSE( parentPtr.isValid() );
    SW_EXPECT_FALSE( child1Ptr.isValid() );
    SW_EXPECT_FALSE( child2Ptr.isValid() );
}

/**
 * @brief [GameObjectTest] 동일 위치/회전/스케일 설정 시 Transform Dirty 플래그가 불필요하게 켜지지 않는지 검증 (No-Op 가드)
 */
SW_TEST_CASE( GameObjectTest, NoOpTransformDoesNotMarkDirty )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "NoOpTransformTest" ) );
    SW_ASSERT_NOT_NULL( pObj );

    sw::SceneComponent* pSceneComp = pObj->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pSceneComp );

    pSceneComp->setLocalPosition( sw::float3( 10.0f, 20.0f, 30.0f ) );
    pSceneComp->setLocalRotation( sw::float3( 0.0f, 90.0f, 0.0f ) );
    pSceneComp->setLocalScale( sw::float3( 2.0f, 2.0f, 2.0f ) );

    // Flush scene transforms to clear dirty flags
    manager.flushSceneTransforms();
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    // Setting identical values should NOT dirty the transform
    pSceneComp->setLocalPosition( sw::float3( 10.0f, 20.0f, 30.0f ) );
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    pSceneComp->setLocalRotation( sw::float3( 0.0f, 90.0f, 0.0f ) );
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    pSceneComp->setLocalScale( sw::float3( 2.0f, 2.0f, 2.0f ) );
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    // Setting a new value should dirty the transform
    pSceneComp->setLocalPosition( sw::float3( 15.0f, 20.0f, 30.0f ) );
    SW_EXPECT_TRUE( manager.hasDirtySceneTransforms() );
}

/**
 * @brief [GameObjectTest] Transform Dirty Generation 세대 카운터 및 O(1) 조기 탈출 검증
 */
SW_TEST_CASE( GameObjectTest, TransformDirtyGenerationEarlyExit )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "GenerationTestObj" ) );
    SW_ASSERT_NOT_NULL( pObj );

    sw::SceneComponent* pSceneComp = pObj->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pSceneComp );

    const uint64 initialGen = manager.getTransformGeneration();

    pSceneComp->setLocalPosition( sw::float3( 5.0f, 10.0f, 15.0f ) );
    const uint64 updatedGen = manager.getTransformGeneration();
    SW_EXPECT_TRUE( updatedGen > initialGen );

    SW_EXPECT_TRUE( manager.hasDirtySceneTransforms() );
    manager.flushSceneTransforms();
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    // 변경 없는 경우 hasDirtySceneTransforms()는 서브트리 순회 없이 O(1)로 false 반환
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );
}

/**
 * @brief [GameObjectTest] MeshComponent만 있어도 월드 행렬 flush가 SceneComponent 풀을 요구하지 않는지 검증
 */
SW_TEST_CASE( GameObjectTest, MeshComponentOnlyTransformFlush )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "MeshOnlyFlush" ) );
    SW_ASSERT_NOT_NULL( pObj );

    sw::MeshComponent* pMesh = pObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    SW_EXPECT_TRUE( pObj->getComponent<sw::SceneComponent>() != nullptr );

    pMesh->setLocalPosition( sw::float3( 10.0f, 0.0f, 0.0f ) );
    SW_EXPECT_TRUE( manager.hasDirtySceneTransforms() );
    manager.flushSceneTransforms();
    SW_EXPECT_FALSE( manager.hasDirtySceneTransforms() );

    SW_EXPECT_EQUAL( 0, static_cast<int32>( pMesh->isTransformDirty() ) );
    SW_EXPECT_NEAR_EQUAL( 10.0f, pMesh->getWorldPosition()._x, 0.001f );
}

/**
 * @brief [GameObjectTest] MeshComponent 계층에서 getParent/getChildren와 자식 월드 합성이 동작하는지 검증
 */
SW_TEST_CASE( GameObjectTest, MeshComponentHierarchyLookupAndWorldCompose )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pParentObj = manager.createGameObject( sw::hashed_string( "MeshParent" ) );
    sw::GameObject*       pChildObj  = manager.createGameObject( sw::hashed_string( "MeshChild" ) );
    SW_ASSERT_NOT_NULL( pParentObj );
    SW_ASSERT_NOT_NULL( pChildObj );

    sw::MeshComponent* pParentMesh = pParentObj->addComponent<sw::MeshComponent>();
    sw::MeshComponent* pChildMesh  = pChildObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pParentMesh );
    SW_ASSERT_NOT_NULL( pChildMesh );

    SW_EXPECT_TRUE( pChildObj->attachToParent( pParentObj ) );
    SW_EXPECT_EQUAL( pParentMesh, pChildMesh->getParent() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), pParentMesh->getChildren().size() );
    SW_EXPECT_EQUAL( pChildMesh, pParentMesh->getChildren()[0] );

    pParentMesh->setLocalPosition( sw::float3( 10.0f, 0.0f, 0.0f ) );
    pChildMesh->setLocalPosition( sw::float3( 5.0f, 0.0f, 0.0f ) );
    manager.flushSceneTransforms();

    const sw::float3 childWorld = pChildMesh->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 15.0f, childWorld._x, 0.001f );
}

/**
 * @brief [GameObjectTest] 부모 비활성이 자식의 isActiveInHierarchy에 반영되는지 검증
 */
SW_TEST_CASE( GameObjectTest, ActiveInHierarchyFollowsParent )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pParent = manager.createGameObject( sw::hashed_string( "ActiveParent" ) );
    sw::GameObject*       pChild  = manager.createGameObject( sw::hashed_string( "ActiveChild" ) );
    SW_ASSERT_NOT_NULL( pParent );
    SW_ASSERT_NOT_NULL( pChild );

    pParent->addComponent<sw::SceneComponent>();
    pChild->addComponent<sw::SceneComponent>();
    SW_EXPECT_TRUE( pChild->attachToParent( pParent ) );

    pParent->setActive( false );
    SW_EXPECT_FALSE( pParent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pChild->isActiveInHierarchy() );

    pParent->setActive( true );
    SW_EXPECT_TRUE( pParent->isActiveInHierarchy() );
    SW_EXPECT_TRUE( pChild->isActiveInHierarchy() );

    pChild->setActive( false );
    SW_EXPECT_TRUE( pParent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pChild->isActiveInHierarchy() );
}

/**
 * @brief [GameObjectTest] ObjectStateSerializer 바이너리 버퍼 직렬화 및 복원 검증
 */
SW_TEST_CASE( GameObjectTest, ObjectStateBinaryBufferRoundtrip )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pSource = manager.createGameObject( sw::hashed_string( "BinaryHero" ) );
    SW_ASSERT_NOT_NULL( pSource );
    pSource->setActive( false );
    pSource->addComponent<sw::SceneComponent>();

    sw::vector<uint8> buffer;
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::saveToBinaryBuffer( pSource, buffer ) );
    SW_EXPECT_FALSE( buffer.empty() );

    manager.clear();

    sw::GameObject* pTarget = manager.createGameObject( sw::hashed_string( "TempTarget" ) );
    SW_ASSERT_NOT_NULL( pTarget );
    pTarget->setActive( true );

    sw::string   parentName;
    const size_t bytesRead = sw::ObjectStateSerializer::loadFromBinaryBuffer( pTarget, buffer.data(), buffer.size(), parentName );
    SW_EXPECT_EQUAL( buffer.size(), bytesRead );
    SW_EXPECT_STREQ( "BinaryHero", pTarget->getName().c_str() );
    SW_EXPECT_FALSE( pTarget->isActive() );
    SW_EXPECT_NOT_NULL( pTarget->getPrimarySceneComponent() );
}

/**
 * @brief [GameObjectTest] 5,000개 대규모 GameObject 생성, 컴포넌트 부착 및 지연 일괄 해제 스트레스 테스트
 */
SW_TEST_CASE( GameObjectTest, GameObjectMassiveCreationAndDestructionStressTest )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    constexpr uint32            kObjectCount = 5000;
    sw::vector<sw::GameObject*> listObject;
    listObject.reserve( kObjectCount );

    // 1) 5,000개 GameObject 및 컴포넌트 대량 생성
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( ( "StressActor_" + std::to_string( index ) ).c_str() ) );
        SW_ASSERT_NOT_NULL( pObj );

        pObj->addComponent<sw::SceneComponent>();
        sw::MockMeshComponent* pMesh = pObj->addComponent<sw::MockMeshComponent>();
        pMesh->_meshName             = "StressMesh";

        if ( index % 5 == 0 )
            pObj->addComponent<sw::MockAudioComponent>();

        listObject.push_back( pObj );
    }

    SW_EXPECT_EQUAL( static_cast<size_t>( kObjectCount ), manager.getAllGameObjects().size() );

    // 2) 틱 실행 및 상태 갱신
    manager.tick( 0.016f );

    // 3) 전수 지연 삭제 큐 등록 및 처리
    for ( sw::GameObject* pObj : listObject )
    {
        manager.destroyObject( pObj );
    }
    manager.processDeferredDestruction();

    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );
}

/**
 * @brief [GameObjectTest] 병렬 틱(Parallel Tick) 중 구조 변경 지연 큐(deferPostTick/deferTransformUpdate) 동시성 스트레스 테스트
 */
SW_TEST_CASE( GameObjectTest, ParallelTickStructuralMutationStressTest )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    constexpr uint32 kObjectCount = 1000;
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( ( "ParallelTickActor_" + std::to_string( index ) ).c_str() ) );
        pObj->addComponent<sw::SceneComponent>();
        pObj->addComponent<sw::MockMeshComponent>();
    }

    std::atomic<uint32> postTickExecutedCount{ 0 };
    std::atomic<uint32> transformUpdateExecutedCount{ 0 };

    // 틱 직전 다중 지연 람다 등록
    for ( uint32 index = 0; index < 50; ++index )
    {
        manager.deferPostTick( SW_DELEGATE_LAMBDA( sw::GameObjectManager::PostTickDelegate, [&postTickExecutedCount]()
        {
            postTickExecutedCount.fetch_add( 1, std::memory_order_relaxed );
        } ) );

        manager.deferTransformUpdate( SW_DELEGATE_LAMBDA( sw::GameObjectManager::TransformUpdateDelegate, [&transformUpdateExecutedCount]()
        {
            transformUpdateExecutedCount.fetch_add( 1, std::memory_order_relaxed );
        } ) );
    }

    // 틱 수행 시 모든 지연 큐가 정상 flush 되어야 함
    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( 50u, postTickExecutedCount.load() );
    SW_EXPECT_EQUAL( 50u, transformUpdateExecutedCount.load() );
}

/**
 * @brief [GameObjectTest] 64단계 심층 부모-자식 계층 트랜스폼 월드 합성 및 동적 분리 스트레스 테스트
 */
SW_TEST_CASE( GameObjectTest, DeepHierarchyMatrixCompositionStressTest )
{
    sw::GameObjectManager manager;

    constexpr uint32            kDepth = 64;
    sw::vector<sw::GameObject*> listNode;
    listNode.reserve( kDepth );

    // 64단계 체인 생성: Node_0 -> Node_1 -> ... -> Node_63
    for ( uint32 depthIndex = 0; depthIndex < kDepth; ++depthIndex )
    {
        sw::GameObject* pNode = manager.createGameObject( sw::hashed_string( ( "DeepNode_" + std::to_string( depthIndex ) ).c_str() ) );
        SW_ASSERT_NOT_NULL( pNode );

        sw::SceneComponent* pComp = pNode->addComponent<sw::SceneComponent>();
        pComp->setLocalPosition( sw::float3( 1.0f, 0.0f, 0.0f ) ); // 각 단계마다 +1 X 이동

        if ( depthIndex > 0 )
        {
            SW_EXPECT_TRUE( pNode->attachToParent( listNode[depthIndex - 1] ) );
        }

        listNode.push_back( pNode );
    }

    manager.flushSceneTransforms();

    // 1) 64번째(인덱스 63) 리프 노드의 월드 위치 = (64, 0, 0)
    sw::SceneComponent* pLeafComp = listNode[kDepth - 1]->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pLeafComp );
    SW_EXPECT_NEAR_EQUAL( static_cast<float32>( kDepth ), pLeafComp->getWorldPosition()._x, 1e-3f );

    // 2) 루트 노드(Node_0) 위치를 (100, 0, 0)으로 변경
    sw::SceneComponent* pRootComp = listNode[0]->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pRootComp );
    pRootComp->setLocalPosition( sw::float3( 100.0f, 0.0f, 0.0f ) );

    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 100.0f + static_cast<float32>( kDepth - 1 ), pLeafComp->getWorldPosition()._x, 1e-3f );

    // 3) 중간 노드(Node_32)를 부모로부터 분리(Detach)
    sw::GameObject* pMidNode = listNode[32];
    pMidNode->detachFromParent();
    SW_EXPECT_TRUE( pMidNode->getParent() == nullptr );

    manager.flushSceneTransforms();
    // Node_32부터 Node_63까지는 32개 체인이므로 리프 월드 X = 32.0f
    SW_EXPECT_NEAR_EQUAL( 32.0f, pLeafComp->getWorldPosition()._x, 1e-3f );
}

/**
 * @brief [GameObjectTest] 다중 컴포넌트 심층 계층에서의 동적 부모 재지정(Reparenting), 순환 방어 및 중간 노드 연쇄 삭제 검증
 */
SW_TEST_CASE( GameObjectTest, DeepMultiComponentCascadeDestructionAndReparenting )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    constexpr uint32            kDepth = 32;
    sw::vector<sw::GameObject*> listNode;
    listNode.reserve( kDepth );

    // 1) 32단계 심층 계층 생성 (각 노드마다 Scene, Mesh, Audio 3개 컴포넌트 부착)
    for ( uint32 depthIndex = 0; depthIndex < kDepth; ++depthIndex )
    {
        sw::GameObject* pNode = manager.createGameObject( sw::hashed_string( ( "MultiCompNode_" + std::to_string( depthIndex ) ).c_str() ) );
        SW_ASSERT_NOT_NULL( pNode );

        sw::SceneComponent* pSceneComp = pNode->addComponent<sw::SceneComponent>();
        pSceneComp->setLocalPosition( sw::float3( 2.0f, 0.0f, 0.0f ) ); // 각 단계마다 +2 X

        sw::MockMeshComponent* pMeshComp = pNode->addComponent<sw::MockMeshComponent>();
        pMeshComp->_meshName             = "Mesh_" + std::to_string( depthIndex );

        pNode->addComponent<sw::MockAudioComponent>();

        if ( depthIndex > 0 )
        {
            SW_EXPECT_TRUE( pNode->attachToParent( listNode[depthIndex - 1] ) );
        }

        listNode.push_back( pNode );
    }

    SW_EXPECT_EQUAL( static_cast<size_t>( kDepth ), manager.getAllGameObjects().size() );

    // 2) 틱 실행 시 모든 32개 노드의 MockMeshComponent가 1회씩 틱을 수행했는지 확인
    manager.tick( 0.016f );
    for ( uint32 depthIndex = 0; depthIndex < kDepth; ++depthIndex )
    {
        sw::MockMeshComponent* pMesh = listNode[depthIndex]->getComponent<sw::MockMeshComponent>();
        SW_ASSERT_NOT_NULL( pMesh );
        SW_EXPECT_EQUAL( 1, pMesh->_tickCount );
    }

    // 3) 동적 부모 변경 (Node_16을 Node_4의 직속 자식으로 재지정)
    sw::GameObject* pNode16 = listNode[16];
    sw::GameObject* pNode4  = listNode[4];
    SW_EXPECT_TRUE( pNode16->attachToParent( pNode4 ) );
    SW_EXPECT_EQUAL( pNode4, pNode16->getParent() );

    // 순환 참조 방어 (자식 Node_16을 부모 Node_4의 부모로 지정 시도 -> 실패해야 함)
    SW_EXPECT_FALSE( pNode4->attachToParent( pNode16 ) );

    manager.flushSceneTransforms();
    // Node_4 월드 X = 5 * 2 = 10. Node_16부터 Node_31까지 16개 단계이므로 리프(Node_31) 월드 X = 10 + (16 * 2) = 42.0f
    sw::SceneComponent* pLeafScene = listNode[kDepth - 1]->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pLeafScene );
    SW_EXPECT_NEAR_EQUAL( 42.0f, pLeafScene->getWorldPosition()._x, 1e-3f );

    // 4) 중간 계층 Node_16 연쇄 삭제 (Node_16과 그 하위 자식인 Node_17 ~ Node_31 총 16개 노드 동시 소멸)
    manager.destroyObject( pNode16, true );
    manager.processDeferredDestruction();

    // Node_0 ~ Node_15 총 16개 노드만 생존해야 함
    SW_EXPECT_EQUAL( static_cast<size_t>( 16 ), manager.getAllGameObjects().size() );

    for ( uint32 depthIndex = 0; depthIndex < 16; ++depthIndex )
    {
        SW_EXPECT_NOT_NULL( manager.findGameObjectByName( sw::hashed_string( ( "MultiCompNode_" + std::to_string( depthIndex ) ).c_str() ) ) );
    }
    for ( uint32 depthIndex = 16; depthIndex < kDepth; ++depthIndex )
    {
        SW_EXPECT_NULL( manager.findGameObjectByName( sw::hashed_string( ( "MultiCompNode_" + std::to_string( depthIndex ) ).c_str() ) ) );
    }

    // 5) 생존 노드들은 정상적으로 계속 틱 수행 가능
    manager.tick( 0.016f );
    for ( uint32 depthIndex = 0; depthIndex < 16; ++depthIndex )
    {
        sw::MockMeshComponent* pMesh = listNode[depthIndex]->getComponent<sw::MockMeshComponent>();
        SW_ASSERT_NOT_NULL( pMesh );
        SW_EXPECT_EQUAL( 2, pMesh->_tickCount );
    }
}

/**
 * @brief [GameObjectTest] 무작위 트리 재배치, 활성 토글 및 삭제가 난무하는 500회 카오스 계층 변이 스트레스 테스트
 */
SW_TEST_CASE( GameObjectTest, ChaoticHierarchyMutationAndActiveToggleStressTest )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    constexpr uint32            kInitialObjectCount = 100;
    sw::vector<sw::GameObject*> listAliveObject;
    listAliveObject.reserve( 500 );

    // 초기 100개 오브젝트 및 계층 생성
    for ( uint32 index = 0; index < kInitialObjectCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( ( "ChaosActor_" + std::to_string( index ) ).c_str() ) );
        SW_ASSERT_NOT_NULL( pObj );

        sw::SceneComponent* pScene = pObj->addComponent<sw::SceneComponent>();
        pScene->setLocalPosition( sw::float3( static_cast<float32>( index ), 0.0f, 0.0f ) );
        pObj->addComponent<sw::MockMeshComponent>();

        if ( index > 0 && ( index % 3 != 0 ) )
        {
            pObj->attachToParent( listAliveObject[index / 2] );
        }

        listAliveObject.push_back( pObj );
    }

    uint32           nextActorId  = kInitialObjectCount;
    constexpr uint32 kTotalRounds = 500;

    for ( uint32 roundIndex = 0; roundIndex < kTotalRounds; ++roundIndex )
    {
        const uint32 actionType = ( roundIndex * 37 + 13 ) % 5;

        if ( listAliveObject.empty() == false )
        {
            const size_t    targetIndexA = ( roundIndex * 17 ) % listAliveObject.size();
            const size_t    targetIndexB = ( roundIndex * 29 + 1 ) % listAliveObject.size();
            sw::GameObject* pObjA        = listAliveObject[targetIndexA];
            sw::GameObject* pObjB        = listAliveObject[targetIndexB];

            if ( actionType == 0 )
            {
                // 1) 부모 재지정 시도 (A -> B)
                if ( pObjA != pObjB )
                    pObjA->attachToParent( pObjB );
            }
            else if ( actionType == 1 )
            {
                // 2) 부모 분리 (Detach)
                pObjA->detachFromParent();
            }
            else if ( actionType == 2 )
            {
                // 3) Active 토글
                const bool bNewActive = ( roundIndex % 2 == 0 );
                pObjA->setActive( bNewActive );
                SW_EXPECT_EQUAL( bNewActive, pObjA->isActive() );
            }
            else if ( actionType == 3 )
            {
                // 4) 임의 오브젝트 삭제 (지연 큐 등록)
                manager.destroyObject( pObjA, true );
                listAliveObject.erase(
                    std::remove_if( listAliveObject.begin(), listAliveObject.end(), []( sw::GameObject* pObj )
                { return pObj == nullptr || pObj->isPendingKill(); } ),
                    listAliveObject.end() );
            }
            else
            {
                // 5) 신규 오브젝트 동적 생성 후 임의 부모 연결
                sw::GameObject* pNewObj = manager.createGameObject( sw::hashed_string( ( "ChaosActor_" + std::to_string( nextActorId++ ) ).c_str() ) );
                if ( pNewObj != nullptr )
                {
                    sw::SceneComponent* pScene = pNewObj->addComponent<sw::SceneComponent>();
                    pScene->setLocalPosition( sw::float3( 1.0f, 2.0f, 3.0f ) );
                    pNewObj->addComponent<sw::MockMeshComponent>();

                    if ( listAliveObject.empty() == false )
                    {
                        pNewObj->attachToParent( listAliveObject.back() );
                    }
                    listAliveObject.push_back( pNewObj );
                }
            }
        }

        // 10회마다 지연 삭제 처리 및 트랜스폼/틱 flush
        if ( roundIndex % 10 == 0 )
        {
            manager.processDeferredDestruction();
            manager.flushSceneTransforms();
            manager.tick( 0.016f );
        }
    }

    // 최종 상태 검증
    manager.processDeferredDestruction();
    manager.flushSceneTransforms();
    manager.tick( 0.016f );

    // 남아있는 모든 오브젝트의 월드 좌표가 유효(NaN/Inf 없음)한지 검증
    for ( sw::GameObject* pObj : manager.getAllGameObjects() )
    {
        sw::SceneComponent* pScene = pObj->getPrimarySceneComponent();
        if ( pScene != nullptr )
        {
            const sw::float3 worldPos = pScene->getWorldPosition();
            SW_EXPECT_FALSE( std::isnan( worldPos._x ) );
            SW_EXPECT_FALSE( std::isnan( worldPos._y ) );
            SW_EXPECT_FALSE( std::isnan( worldPos._z ) );
            SW_EXPECT_FALSE( std::isinf( worldPos._x ) );
        }
    }

    // 씬 클리어
    manager.clear();
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );
}

/**
 * @brief [GameObjectTest] 다단계 상속(4단계) 컴포넌트의 TypeInfo::isDerivedFrom 및 isA/castTo 다형성 검증
 */
SW_TEST_CASE( GameObjectTest, MultiLevelComponentInheritanceTypeInfoAndIsA )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "InheritanceActor" ) );
    SW_ASSERT_NOT_NULL( pObj );

    sw::MockFlyingVehicleComponent* pFlying = pObj->addComponent<sw::MockFlyingVehicleComponent>();
    SW_ASSERT_NOT_NULL( pFlying );

    const sw::TypeInfo* pType = pFlying->getTypeInfo();
    SW_ASSERT_NOT_NULL( pType );

    // 1) TypeInfo::isDerivedFrom 4단계 상속 체인 전수 검증
    // MockFlyingVehicleComponent -> MockVehicleComponent -> MockBasePawnComponent -> MockRootComponent
    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "sw::MockFlyingVehicleComponent" ) ) );
    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "MockFlyingVehicleComponent" ) ) );

    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "sw::MockVehicleComponent" ) ) );
    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "MockVehicleComponent" ) ) );

    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "sw::MockBasePawnComponent" ) ) );
    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "MockBasePawnComponent" ) ) );

    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "sw::MockRootComponent" ) ) );
    SW_EXPECT_TRUE( pType->isDerivedFrom( sw::hashed_string( "MockRootComponent" ) ) );

    // 무관한 형제/타입에 대해서는 false
    SW_EXPECT_FALSE( pType->isDerivedFrom( sw::hashed_string( "sw::MockAudioComponent" ) ) );
    SW_EXPECT_FALSE( pType->isDerivedFrom( sw::hashed_string( "sw::MockMeshComponent" ) ) );

    // 2) isA<T>() 템플릿 다형성 검증
    SW_EXPECT_TRUE( sw::isA<sw::MockFlyingVehicleComponent>( pFlying ) );
    SW_EXPECT_TRUE( sw::isA<sw::MockVehicleComponent>( pFlying ) );
    SW_EXPECT_TRUE( sw::isA<sw::MockBasePawnComponent>( pFlying ) );
    SW_EXPECT_TRUE( sw::isA<sw::MockRootComponent>( pFlying ) );
    SW_EXPECT_FALSE( sw::isA<sw::MockAudioComponent>( pFlying ) );

    // 3) castTo<T>() 템플릿 안전 업캐스팅 검증
    sw::MockVehicleComponent*  pVehicle = sw::castTo<sw::MockVehicleComponent>( pFlying );
    sw::MockBasePawnComponent* pPawn    = sw::castTo<sw::MockBasePawnComponent>( pFlying );
    sw::MockRootComponent*     pRoot    = sw::castTo<sw::MockRootComponent>( pFlying );

    SW_ASSERT_NOT_NULL( pVehicle );
    SW_ASSERT_NOT_NULL( pPawn );
    SW_ASSERT_NOT_NULL( pRoot );

    SW_EXPECT_EQUAL( static_cast<void*>( pFlying ), static_cast<void*>( pVehicle ) );
    SW_EXPECT_EQUAL( static_cast<void*>( pFlying ), static_cast<void*>( pPawn ) );
    SW_EXPECT_EQUAL( static_cast<void*>( pFlying ), static_cast<void*>( pRoot ) );

    SW_EXPECT_NULL( sw::castTo<sw::MockAudioComponent>( pFlying ) );
}

/**
 * @brief [GameObjectTest] GameObject::getComponent<T>()의 다단계 상속 다형성 조회 및 가상 메서드 체인 검증
 */
SW_TEST_CASE( GameObjectTest, MultiLevelComponentGameObjectPolymorphicLookup )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "DroneActor" ) );
    SW_ASSERT_NOT_NULL( pObj );

    // 다단계 상속 컴포넌트 부착
    sw::MockFlyingVehicleComponent* pFlying = pObj->addComponent<sw::MockFlyingVehicleComponent>();
    SW_ASSERT_NOT_NULL( pFlying );
    pFlying->_pawnHealth  = 250;
    pFlying->_maxSpeed    = 180.0f;
    pFlying->_maxAltitude = 8000.0f;

    // 1) 임의의 부모 타입 T로 getComponent<T>() 호출 시 동일 인스턴스 조회 검증
    sw::MockFlyingVehicleComponent* pExactLookup   = pObj->getComponent<sw::MockFlyingVehicleComponent>();
    sw::MockVehicleComponent*       pVehicleLookup = pObj->getComponent<sw::MockVehicleComponent>();
    sw::MockBasePawnComponent*      pPawnLookup    = pObj->getComponent<sw::MockBasePawnComponent>();
    sw::MockRootComponent*          pRootLookup    = pObj->getComponent<sw::MockRootComponent>();

    SW_ASSERT_NOT_NULL( pExactLookup );
    SW_ASSERT_NOT_NULL( pVehicleLookup );
    SW_ASSERT_NOT_NULL( pPawnLookup );
    SW_ASSERT_NOT_NULL( pRootLookup );

    SW_EXPECT_EQUAL( pFlying, pExactLookup );
    SW_EXPECT_EQUAL( pFlying, pVehicleLookup );
    SW_EXPECT_EQUAL( pFlying, pPawnLookup );
    SW_EXPECT_EQUAL( pFlying, pRootLookup );

    // 무관한 컴포넌트 조회 시 nullptr
    SW_EXPECT_NULL( pObj->getComponent<sw::MockAudioComponent>() );
    SW_EXPECT_NULL( pObj->getComponent<sw::MockMeshComponent>() );

    // 2) 다단계 상속 계층의 가상 onTick 체인 연쇄 호출 검증
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 1, pFlying->_flyingTickCount );
    SW_EXPECT_EQUAL( 1, pFlying->_vehicleTickCount );
    SW_EXPECT_EQUAL( 1, pFlying->_pawnTickCount );

    // 3) 상속된 SceneComponent 트랜스폼 동작 검증
    pFlying->setLocalPosition( sw::float3( 10.0f, 20.0f, 30.0f ) );
    manager.flushSceneTransforms();

    const sw::float3 worldPos = pFlying->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 10.0f, worldPos._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, worldPos._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, worldPos._z, 1e-4f );
}

/**
 * @brief [GameObjectPoolTest] GameObject 파괴 후 재생성 시 TypedPoolAllocator 메모리 주소 재활용 및 내부 상태 초기화 검증
 */
SW_TEST_CASE( GameObjectPoolTest, GameObjectPoolMemoryReuseAndStateReset )
{
    sw::GameObjectManager manager;

    // 1) 3개 액터 생성 및 메모리 주소 기록
    sw::GameObject* pObj0 = manager.createGameObject( sw::hashed_string( "Actor_0" ) );
    sw::GameObject* pObj1 = manager.createGameObject( sw::hashed_string( "Actor_1" ) );
    sw::GameObject* pObj2 = manager.createGameObject( sw::hashed_string( "Actor_2" ) );

    SW_ASSERT_NOT_NULL( pObj0 );
    SW_ASSERT_NOT_NULL( pObj1 );
    SW_ASSERT_NOT_NULL( pObj2 );

    void* pAddr0 = static_cast<void*>( pObj0 );
    void* pAddr1 = static_cast<void*>( pObj1 );
    void* pAddr2 = static_cast<void*>( pObj2 );

    // 서로 다른 메모리 주소 할당 확인
    SW_EXPECT_NOT_EQUAL( pAddr0, pAddr1 );
    SW_EXPECT_NOT_EQUAL( pAddr1, pAddr2 );

    const uint64 id0 = pObj0->getObjectId();
    const uint64 id1 = pObj1->getObjectId();
    const uint64 id2 = pObj2->getObjectId();

    // 2) 액터 지연 삭제 처리
    manager.destroyObject( pObj2 );
    manager.destroyObject( pObj1 );
    manager.destroyObject( pObj0 );
    manager.processDeferredDestruction();

    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );

    // 3) 새로운 액터 3개 생성 시 풀의 LIFO 프리리스트에 의해 이전 주소가 재활용되는지 검증
    sw::GameObject* pNewObj0 = manager.createGameObject( sw::hashed_string( "NewActor_0" ) );
    sw::GameObject* pNewObj1 = manager.createGameObject( sw::hashed_string( "NewActor_1" ) );
    sw::GameObject* pNewObj2 = manager.createGameObject( sw::hashed_string( "NewActor_2" ) );

    SW_ASSERT_NOT_NULL( pNewObj0 );
    SW_ASSERT_NOT_NULL( pNewObj1 );
    SW_ASSERT_NOT_NULL( pNewObj2 );

    void* pNewAddr0 = static_cast<void*>( pNewObj0 );
    void* pNewAddr1 = static_cast<void*>( pNewObj1 );
    void* pNewAddr2 = static_cast<void*>( pNewObj2 );

    // LIFO 순서에 따라 마지막에 반환된 pAddr0, pAddr1, pAddr2가 재사용됨
    SW_EXPECT_EQUAL( pAddr0, pNewAddr0 );
    SW_EXPECT_EQUAL( pAddr1, pNewAddr1 );
    SW_EXPECT_EQUAL( pAddr2, pNewAddr2 );

    // 새로운 고유 ID가 발급되었는지 확인 (이전 ID와 다름)
    SW_EXPECT_NOT_EQUAL( id0, pNewObj0->getObjectId() );
    SW_EXPECT_NOT_EQUAL( id1, pNewObj1->getObjectId() );
    SW_EXPECT_NOT_EQUAL( id2, pNewObj2->getObjectId() );

    // 이름 및 기본 상태가 깨끗하게 초기화되었는지 확인
    SW_EXPECT_STREQ( "NewActor_0", pNewObj0->getName().c_str() );
    SW_EXPECT_TRUE( pNewObj0->isActive() );
    SW_EXPECT_FALSE( pNewObj0->isPendingKill() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), pNewObj0->getComponentCount() );
}

/**
 * @brief [ComponentPoolTest] 서로 다른 컴포넌트 타입별 전용 PoolAllocator 격리 및 주소 재활용 검증
 */
SW_TEST_CASE( ComponentPoolTest, PolymorphicComponentPoolIsolationAndAddressRecycle )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* pActor = manager.createGameObject( sw::hashed_string( "TestActor" ) );
    SW_ASSERT_NOT_NULL( pActor );

    // 1) 크기가 다른 서로 다른 타입의 컴포넌트 추가
    sw::SceneComponent*     pScene1 = pActor->addComponent<sw::SceneComponent>();
    sw::MockMeshComponent*  pMesh1  = pActor->addComponent<sw::MockMeshComponent>();
    sw::MockAudioComponent* pAudio1 = pActor->addComponent<sw::MockAudioComponent>();

    SW_ASSERT_NOT_NULL( pScene1 );
    SW_ASSERT_NOT_NULL( pMesh1 );
    SW_ASSERT_NOT_NULL( pAudio1 );

    void* pMeshAddr1  = static_cast<void*>( pMesh1 );
    void* pAudioAddr1 = static_cast<void*>( pAudio1 );

    // 2) MockMeshComponent 만 삭제
    pActor->removeComponent( pMesh1 );

    // 3) 새로운 MockMeshComponent 추가 시 동일한 메모리 주소가 재활용되는지 검증
    sw::MockMeshComponent* pMesh2 = pActor->addComponent<sw::MockMeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh2 );
    void* pMeshAddr2 = static_cast<void*>( pMesh2 );
    SW_EXPECT_EQUAL( pMeshAddr1, pMeshAddr2 );

    // MockAudioComponent 는 기존 인스턴스가 유지되고 영향받지 않음
    SW_EXPECT_EQUAL( pAudio1, pActor->getComponent<sw::MockAudioComponent>() );

    // 4) MockAudioComponent 삭제 후 재추가 시 Audio 풀 주소 재활용 검증
    pActor->removeComponent( pAudio1 );
    sw::MockAudioComponent* pAudio2 = pActor->addComponent<sw::MockAudioComponent>();
    SW_ASSERT_NOT_NULL( pAudio2 );
    void* pAudioAddr2 = static_cast<void*>( pAudio2 );
    SW_EXPECT_EQUAL( pAudioAddr1, pAudioAddr2 );
}

/**
 * @brief [ComponentPoolTest] 컴포넌트 풀 할당 및 해제 시 생성자/소멸자 호출 1:1 매칭 정확도 검증
 */
SW_TEST_CASE( ComponentPoolTest, ComponentPoolConstructorAndDestructorExactTracking )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::MockPoolLifecycleComponent::s_ctorCount.store( 0, std::memory_order_relaxed );
    sw::MockPoolLifecycleComponent::s_dtorCount.store( 0, std::memory_order_relaxed );

    // 1) 100개 오브젝트에 각각 MockPoolLifecycleComponent 추가
    constexpr uint32            kCount = 100;
    sw::vector<sw::GameObject*> listObject;
    listObject.reserve( kCount );

    for ( uint32 index = 0; index < kCount; ++index )
    {
        sw::GameObject* pObj  = manager.createGameObject( sw::hashed_string( ( "LifeActor_" + std::to_string( index ) ).c_str() ) );
        auto*           pComp = pObj->addComponent<sw::MockPoolLifecycleComponent>();
        pComp->_customData    = static_cast<int32>( index );
        listObject.push_back( pObj );
    }

    SW_EXPECT_EQUAL( static_cast<int32>( kCount ), sw::MockPoolLifecycleComponent::s_ctorCount.load( std::memory_order_relaxed ) );
    SW_EXPECT_EQUAL( 0, sw::MockPoolLifecycleComponent::s_dtorCount.load( std::memory_order_relaxed ) );

    // 2) 50개 오브젝트에서 removeComponent 로 직접 해제
    for ( uint32 index = 0; index < 50; ++index )
    {
        auto* pComp = listObject[index]->getComponent<sw::MockPoolLifecycleComponent>();
        SW_ASSERT_NOT_NULL( pComp );
        listObject[index]->removeComponent( pComp );
    }

    SW_EXPECT_EQUAL( 50, sw::MockPoolLifecycleComponent::s_dtorCount.load( std::memory_order_relaxed ) );

    // 3) 남은 50개 오브젝트는 GameObjectManager::clear 로 일괄 정리
    manager.clear();

    SW_EXPECT_EQUAL( static_cast<int32>( kCount ), sw::MockPoolLifecycleComponent::s_dtorCount.load( std::memory_order_relaxed ) );
    SW_EXPECT_EQUAL( sw::MockPoolLifecycleComponent::s_ctorCount.load( std::memory_order_relaxed ),
                     sw::MockPoolLifecycleComponent::s_dtorCount.load( std::memory_order_relaxed ) );
}

/**
 * @brief [ComponentPoolTest] 초고빈도(High-Frequency Churn) 컴포넌트 추가/삭제 스트레스 테스트
 */
SW_TEST_CASE( ComponentPoolTest, ComponentPoolHighFrequencyChurnStress )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::GameObject* pActor = manager.createGameObject( sw::hashed_string( "ChurnActor" ) );
    SW_ASSERT_NOT_NULL( pActor );

    constexpr uint32 kIterations = 10000;
    for ( uint32 iterIndex = 0; iterIndex < kIterations; ++iterIndex )
    {
        sw::MockMeshComponent* pMesh = pActor->addComponent<sw::MockMeshComponent>();
        pMesh->_meshName             = "DynamicMesh";
        SW_ASSERT_NOT_NULL( pMesh );

        sw::MockAudioComponent* pAudio = pActor->addComponent<sw::MockAudioComponent>();
        pAudio->_volume                = 0.5f;
        SW_ASSERT_NOT_NULL( pAudio );

        pActor->removeComponent( pMesh );
        pActor->removeComponent( pAudio );
    }

    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), pActor->getComponentCount() );
}

/**
 * @brief [GameObjectManagerPoolTest] Scene clear 후 풀 완전 초기화 및 재사용 라이프사이클 검증
 */
SW_TEST_CASE( GameObjectManagerPoolTest, SceneClearAndPoolReuseLifecycle )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    // 1) 씬 1 생성: 200개 액터 및 복합 컴포넌트 구성
    constexpr uint32 kObjectCount = 200;
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( ( "Scene1_Obj_" + std::to_string( index ) ).c_str() ) );
        pObj->addComponent<sw::SceneComponent>();
        pObj->addComponent<sw::MockMeshComponent>();
        pObj->addComponent<sw::MockAudioComponent>();
    }

    SW_EXPECT_EQUAL( static_cast<size_t>( kObjectCount ), manager.getAllGameObjects().size() );
    manager.tick( 0.016f );

    // 2) 씬 클리어 (풀 완전 초기화)
    manager.clear();
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );

    // 3) 씬 2 생성: 클리어된 매니저에서 다시 200개 액터 정상 생성 및 틱 검증
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( ( "Scene2_Obj_" + std::to_string( index ) ).c_str() ) );
        pObj->addComponent<sw::SceneComponent>();
        sw::MockMeshComponent* pMesh = pObj->addComponent<sw::MockMeshComponent>();
        pMesh->_meshName             = "Scene2Mesh";
    }

    SW_EXPECT_EQUAL( static_cast<size_t>( kObjectCount ), manager.getAllGameObjects().size() );
    manager.tick( 0.016f );

    // 씬 2의 모든 메시 컴포넌트가 1회 틱되었는지 검증
    for ( sw::GameObject* pObj : manager.getAllGameObjects() )
    {
        sw::MockMeshComponent* pMesh = pObj->getComponent<sw::MockMeshComponent>();
        SW_ASSERT_NOT_NULL( pMesh );
        SW_EXPECT_EQUAL( 1, pMesh->_tickCount );
    }

    manager.clear();
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );
}

/**
 * @brief [GameObjectHierarchy] refreshActiveInHierarchy 부모-자식-손자 다계층 합성 활성 상태 엣지 케이스 검증
 */
SW_TEST_CASE( GameObjectHierarchy, ActiveInHierarchyCompoundEvaluation )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pGrandparent = manager.createGameObject( sw::hashed_string( "Grandparent" ) );
    sw::GameObject*       pParent      = manager.createGameObject( sw::hashed_string( "Parent" ) );
    sw::GameObject*       pChild       = manager.createGameObject( sw::hashed_string( "Child" ) );

    sw::SceneComponent* pGrandparentSc = pGrandparent->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pParentSc      = pParent->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pChildSc       = pChild->addComponent<sw::SceneComponent>();

    pParentSc->attachToComponent( pGrandparentSc );
    pChildSc->attachToComponent( pParentSc );

    // 1) 초기 상태: 모두 활성
    SW_EXPECT_TRUE( pGrandparent->isActiveInHierarchy() );
    SW_EXPECT_TRUE( pParent->isActiveInHierarchy() );
    SW_EXPECT_TRUE( pChild->isActiveInHierarchy() );

    // 2) Child만 비활성화: Grandparent/Parent는 true, Child는 false
    pChild->setActive( false );
    SW_EXPECT_TRUE( pGrandparent->isActiveInHierarchy() );
    SW_EXPECT_TRUE( pParent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pChild->isActiveInHierarchy() );

    // 3) Parent 비활성화, Child 재활성화: Grandparent=true, Parent=false, Child=false (부모가 꺼져있으므로)
    pParent->setActive( false );
    pChild->setActive( true );
    SW_EXPECT_TRUE( pGrandparent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pParent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pChild->isActiveInHierarchy() );

    // 4) Grandparent 비활성화, Parent 활성화, Child 활성화: Grandparent=false, Parent=false, Child=false (조상이 꺼져있으므로)
    pGrandparent->setActive( false );
    pParent->setActive( true );
    pChild->setActive( true );
    SW_EXPECT_FALSE( pGrandparent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pParent->isActiveInHierarchy() );
    SW_EXPECT_FALSE( pChild->isActiveInHierarchy() );

    // 5) Grandparent 복구: 모두 true
    pGrandparent->setActive( true );
    SW_EXPECT_TRUE( pGrandparent->isActiveInHierarchy() );
    SW_EXPECT_TRUE( pParent->isActiveInHierarchy() );
    SW_EXPECT_TRUE( pChild->isActiveInHierarchy() );
}
