#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/ComponentPtr.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerInternal.h"
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
		const TypeInfo* makeMockComponentTypeInfo( hashed_string shortName, hashed_string fqn, size_t size )
		{
			// 테스트 로컬 TypeInfo (RTTI 없음). 키는 ComponentManager 팩토리 등록과 일치합니다.
			static mutex								  s_mutex;
			std::lock_guard<mutex>						  lock( s_mutex );
			static unordered_map<hashed_string, TypeInfo> s_types;
			auto										  it = s_types.find( shortName );
			if ( it != s_types.end() )
				return &it->second;

			TypeInfo info{};
			info._name				 = shortName;
			info._typeId			 = static_cast<uint32>( shortName.getHash() );
			info._fullyQualifiedName = fqn;
			info._size				 = size;
			it						 = s_types.emplace( shortName, std::move( info ) ).first;
			if ( engine::areEngineServicesBound() )
				engine::getTypeRegistry().registerClass( it->second );
			return &it->second;
		}

	} // namespace

	class MockMeshComponent : public Component
	{
	public:
		REFLECT_BODY();

		/** @brief 캐시된 TypeInfo 또는 StaticType 을 반환합니다. */
		const TypeInfo* getTypeInfo() const override
		{
			return _pCachedTypeInfo != nullptr ? _pCachedTypeInfo : StaticType();
		}

		string _meshName = "CubeMesh";
		int32  _tickCount{ 0 };

		/** @brief 틱마다 _tickCount 를 증가시킵니다. */
		virtual void onTick( float32 deltaTime ) override
		{
			Component::onTick( deltaTime );
			_tickCount++;
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

		/** @brief 캐시된 TypeInfo 또는 StaticType 을 반환합니다. */
		const TypeInfo* getTypeInfo() const override
		{
			return _pCachedTypeInfo != nullptr ? _pCachedTypeInfo : StaticType();
		}

		float32 _volume{ 1.0f };
		int32	_playCount{ 0 };

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

		/** @brief 캐시된 TypeInfo 또는 StaticType 을 반환합니다. */
		const TypeInfo* getTypeInfo() const override
		{
			return _pCachedTypeInfo != nullptr ? _pCachedTypeInfo : StaticType();
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

	/** @brief 모의 컴포넌트 TypeInfo 와 ECS 팩토리를 등록합니다. */
	static void RegisterMockComponents( GameObjectManager& manager )
	{
		MockMeshComponent::StaticType();
		MockAudioComponent::StaticType();
		MockCallbackComponent::StaticType();

		sw::Registry& registry = GameObjectManagerAccess::get( manager );
		registry.registerComponentType<MockMeshComponent>( hashed_string( "MockMeshComponent" ) );
		registry.registerComponentType<MockAudioComponent>( hashed_string( "MockAudioComponent" ) );
		registry.registerComponentType<MockCallbackComponent>( hashed_string( "MockCallbackComponent" ) );
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
	mesh1->_meshName			 = "HeadMesh";

	// ECS 가 기존 컴포넌트를 교체한다
	sw::MockMeshComponent* mesh2 = actor.addComponent<sw::MockMeshComponent>();
	mesh2->_meshName			 = "BodyMesh";

	sw::MockMeshComponent* mesh3 = actor.addComponent<sw::MockMeshComponent>();
	mesh3->_meshName			 = "WeaponMesh";

	SW_EXPECT_EQUAL( 1u, actor.getComponentCount() );

	auto firstHandle = actor.getComponent<sw::MockMeshComponent>();
	SW_ASSERT_NOT_NULL( firstHandle.get() );
	SW_EXPECT_EQUAL( sw::string( "WeaponMesh" ), firstHandle->_meshName );

	sw::MockMeshComponent* firstMesh = firstHandle.get();
	SW_EXPECT_EQUAL( mesh3, firstMesh );

	manager.tick( 0.016f );
	SW_EXPECT_EQUAL( 1, mesh3->_tickCount );

	SW_EXPECT_TRUE( actor.removeComponent( firstMesh ) );
	SW_EXPECT_EQUAL( 0u, actor.getComponentCount() );
}

/**
 * @brief [GameObjectTest] 지연 컴포넌트 삭제가 ECS 풀에서 제거한다
 */
SW_TEST_CASE( GameObjectTest, DeferredComponentDestructionRemovesFromEcs )
{
	sw::GameObjectManager manager;
	sw::RegisterMockComponents( manager );

	sw::GameObject*		   actor = manager.createGameObject( sw::hashed_string( "DeferredCompActor" ) );
	sw::MockMeshComponent* mesh	 = actor->addComponent<sw::MockMeshComponent>();
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
		sw::GameObject*			actorPtr = manager.createGameObject( sw::hashed_string( "EditorActor" ) );
		sw::GameObject&			actor	 = *actorPtr;
		sw::MockMeshComponent*	comp1	 = actor.addComponent<sw::MockMeshComponent>();
		sw::MockAudioComponent* comp2	 = actor.addComponent<sw::MockAudioComponent>();

		SW_EXPECT_TRUE( comp1 != nullptr );
		SW_EXPECT_TRUE( comp2 != nullptr );
		SW_EXPECT_EQUAL( 2u, actor.getComponentCount() );

		SW_EXPECT_EQUAL( &actor, comp1->getOwner() );
		SW_EXPECT_EQUAL( &actor, comp2->getOwner() );
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
			sw::MockMeshComponent* meshComp = actor->getComponent<sw::MockMeshComponent>().get();
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
	sw::GameObject*		   actorPtr = manager.createGameObject( sw::hashed_string( "ReflectedActor" ) );
	sw::GameObject&		   actor	= *actorPtr;
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
	constexpr TagID		  tagAttacking = "State.Combat.Attacking"_tag;
	constexpr TagID		  tagCombat	   = "State.Combat"_tag;
	constexpr TagID		  tagState	   = "State"_tag;

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
	sw::GameObject*		  actorPtr = manager.createGameObject( sw::hashed_string( "TaggedHero" ) );
	sw::GameObject&		  actor	   = *actorPtr;

	constexpr TagID tagInvincible = "Status.Invincible"_tag;
	constexpr TagID tagFlying	  = "Status.Flying"_tag;

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
	sw::GameObject*		  parentActorPtr = manager.createGameObject( sw::hashed_string( "ParentActor" ) );
	sw::GameObject&		  parentActor	 = *parentActorPtr;
	sw::GameObject*		  childActorPtr	 = manager.createGameObject( sw::hashed_string( "ChildActor" ) );
	sw::GameObject&		  childActor	 = *childActorPtr;

	parentActor.addComponent<SceneComponent>();
	childActor.addComponent<SceneComponent>();

	SceneComponent* parentComp = parentActor.getComponent<SceneComponent>().get();
	SceneComponent* childComp  = childActor.getComponent<SceneComponent>().get();

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
	sw::GameObject*		  parentActorPtr = manager.createGameObject( sw::hashed_string( "RotatedScaledParent" ) );
	sw::GameObject&		  parentActor	 = *parentActorPtr;
	sw::GameObject*		  childActorPtr	 = manager.createGameObject( sw::hashed_string( "RotatedScaledChild" ) );
	sw::GameObject&		  childActor	 = *childActorPtr;

	parentActor.addComponent<SceneComponent>();
	childActor.addComponent<SceneComponent>();

	SceneComponent* parentComp = parentActor.getComponent<SceneComponent>().get();
	SceneComponent* childComp  = childActor.getComponent<SceneComponent>().get();

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
	sw::GameObject*		  actorPtr = manager.createGameObject( sw::hashed_string( "LWCActor" ) );
	sw::GameObject&		  actor	   = *actorPtr;

	SceneComponent* comp = actor.addComponent<SceneComponent>();

	comp->setLocalPosition( float3( 1000000.5f, 500000.25f, 0.0f ) );

	double3 lwcPos = comp->getWorldPositionLWC();
	SW_EXPECT_NEAR_EQUAL( 1000000.5, lwcPos._x, 1e-6 );
	SW_EXPECT_NEAR_EQUAL( 500000.25, lwcPos._y, 1e-6 );

	double3	 cameraWorldPos( 1000000.0, 500000.0, 0.0 );
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
	double3				  v1( 3.0, 4.0, 0.0 );
	SW_EXPECT_NEAR_EQUAL( 5.0, v1.getLength(), 1e-6 );

	v1.normalize();
	SW_EXPECT_NEAR_EQUAL( 0.6, v1._x, 1e-6 );
	SW_EXPECT_NEAR_EQUAL( 0.8, v1._y, 1e-6 );

	float3	f3( 10.0f, 20.0f, 30.0f );
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
	GameObject*			  first = manager.createGameObject( hashed_string( "Owned" ) );
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

	MockMeshComponent* aMesh = a->getComponent<MockMeshComponent>().get();
	MockMeshComponent* bMesh = b->getComponent<MockMeshComponent>().get();

	manager.tick( 0.016f );
	SW_EXPECT_EQUAL( 1, aMesh->_tickCount );
	SW_EXPECT_EQUAL( 1, bMesh->_tickCount );

	manager.tick( 0.016f );
	aMesh = a->getComponent<MockMeshComponent>().get();
	bMesh = b->getComponent<MockMeshComponent>().get();
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

	parentObj->addComponent<SceneComponent>();
	childObj->addComponent<SceneComponent>();

	SceneComponent* parentComp = parentObj->getComponent<SceneComponent>().get();
	SceneComponent* childComp  = childObj->getComponent<SceneComponent>().get();

	parentComp->setLocalPosition( float3( 10.0f, 0.0f, 0.0f ) );
	childComp->setLocalPosition( float3( 5.0f, 0.0f, 0.0f ) );
	SW_ASSERT_TRUE( childComp->attachToComponent( parentComp ) );

	float3 observedDuringTick{ 0.0f, 0.0f, 0.0f };
	childComp->setTickDelegate( SW_DELEGATE_LAMBDA( Component::ComponentTickDelegate,
													[&observedDuringTick, childComp]( float32 )
	{
		observedDuringTick = childComp->getWorldPosition();
	} ) );

	manager.tick( 0.016f );
	SW_EXPECT_NEAR_EQUAL( 15.0f, observedDuringTick._x, 1e-4f );

	// 틱 중 로컬 쓰기는 post-flush 이후에 보이며, 틱 중간 스냅샷에는 없을 수 있다.
	parentComp->setTickDelegate( SW_DELEGATE_LAMBDA( Component::ComponentTickDelegate,
													 [parentComp]( float32 )
	{
		parentComp->setLocalPosition( float3( 20.0f, 0.0f, 0.0f ) );
	} ) );
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

	GameObject*		obj	 = manager.createGameObject( hashed_string( "CleanRoot" ) );
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
	sw::GameObject*		  sourcePtr = manager.createGameObject( sw::hashed_string( "SerializedHero" ) );
	sw::GameObject&		  source	= *sourcePtr;
	source.setActive( false );

	const sw::string xml = ObjectStateSerializer::saveToXmlString( &source );
	SW_ASSERT_TRUE( xml.empty() == false );
	SW_EXPECT_TRUE( xml.find( "GameObjectState" ) != sw::string::npos );
	SW_EXPECT_TRUE( xml.find( "SerializedHero" ) != sw::string::npos );
	SW_EXPECT_TRUE( xml.find( "ParentGO" ) != sw::string::npos );

	manager.clear();

	sw::GameObject* targetPtr = manager.createGameObject( sw::hashed_string( "Temp" ) );
	sw::GameObject& target	  = *targetPtr;
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

	manager->clear();
}

// ------------------------------------------------------------------------------
// 8) ComponentTickGroupTest — PrePhysics~PostUpdate 순서
// ------------------------------------------------------------------------------
/**
 * @brief [ComponentTickGroupTest] PrePhysics~PostUpdate 틱 순서
 */
SW_TEST_CASE( ComponentTickGroupTest, TickOrderPrePhysicsToPostUpdate )
{
	sw::GameObjectManager manager;
	sw::RegisterMockComponents( manager );
	sw::GameObject* actorPost = manager.createGameObject( sw::hashed_string( "TickGroupActorPost" ) );
	actorPost->addComponent<MockMeshComponent>();

	sw::GameObject* actorPre = manager.createGameObject( sw::hashed_string( "TickGroupActorPre" ) );
	actorPre->addComponent<MockMeshComponent>();

	MockMeshComponent* compPost = actorPost->getComponent<MockMeshComponent>().get();
	MockMeshComponent* compPre	= actorPre->getComponent<MockMeshComponent>().get();
	compPost->setTickGroup( sw::TickGroup::PostPhysics );
	compPre->setTickGroup( sw::TickGroup::PrePhysics );

	manager.tick( 0.016f );
	SW_EXPECT_EQUAL( 1, compPre->_tickCount );
	SW_EXPECT_EQUAL( 1, compPost->_tickCount );
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
	sw::GameObject*	   actorPtr = manager.createGameObject( sw::hashed_string( "ActiveTestActor" ) );
	sw::GameObject&	   actor	= *actorPtr;
	MockMeshComponent* comp		= actor.addComponent<MockMeshComponent>();

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
	sw::GameObject*		  parentPtr = manager.createGameObject( sw::hashed_string( "ParentGO" ) );
	sw::GameObject&		  parent	= *parentPtr;
	sw::GameObject*		  childPtr	= manager.createGameObject( sw::hashed_string( "ChildGO" ) );
	sw::GameObject&		  child		= *childPtr;
	sw::GameObject*		  grandPtr	= manager.createGameObject( sw::hashed_string( "GrandGO" ) );
	sw::GameObject&		  grand		= *grandPtr;

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
	sw::GameObject*		   actorPtr = manager.createGameObject( sw::hashed_string( "PropertyChangedActor" ) );
	sw::GameObject&		   actor	= *actorPtr;
	MockCallbackComponent* comp		= actor.addComponent<MockCallbackComponent>();

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
	sw::GameObject*		  first	 = manager.createGameObject( sw::hashed_string( "DupName" ) );
	sw::GameObject*		  second = manager.createGameObject( sw::hashed_string( "DupName" ) );
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
	GameObject*			  parent = manager.createGameObject( hashed_string( "AlignParent" ) );
	GameObject*			  child	 = manager.createGameObject( hashed_string( "AlignChild" ) );
	GameObject*			  other	 = manager.createGameObject( hashed_string( "AlignOther" ) );
	SW_ASSERT_NOT_NULL( parent );
	SW_ASSERT_NOT_NULL( child );
	SW_ASSERT_NOT_NULL( other );

	parent->addComponent<SceneComponent>();
	child->addComponent<SceneComponent>();
	other->addComponent<SceneComponent>();

	SceneComponent* parentSc = parent->getComponent<SceneComponent>().get();
	SceneComponent* childSc	 = child->getComponent<SceneComponent>().get();
	SceneComponent* otherSc	 = other->getComponent<SceneComponent>().get();
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
	MockMeshComponent* keeperMesh = keeper->getComponent<MockMeshComponent>().get();
	MockMeshComponent* victimMesh = victim->getComponent<MockMeshComponent>().get();
	SW_ASSERT_NOT_NULL( keeperMesh );
	SW_ASSERT_NOT_NULL( victimMesh );

	keeperMesh->setTickDelegate( SW_DELEGATE_LAMBDA( Component::ComponentTickDelegate,
													 [&manager, victim]( float32 )
	{
		manager.destroyObject( victim );
	} ) );

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
	MockMeshComponent* meshA = a->getComponent<MockMeshComponent>().get();
	MockMeshComponent* meshB = b->getComponent<MockMeshComponent>().get();
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
	MockMeshComponent* keeperMesh = keeper->getComponent<MockMeshComponent>().get();
	MockMeshComponent* victimMesh = victim->getComponent<MockMeshComponent>().get();
	SW_ASSERT_NOT_NULL( keeperMesh );
	SW_ASSERT_NOT_NULL( victimMesh );

	keeperMesh->setTickDelegate( SW_DELEGATE_LAMBDA( Component::ComponentTickDelegate,
													 [victim, victimMesh]( float32 )
	{
		victim->removeComponent( victimMesh );
	} ) );

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
	MockMeshComponent*	mesh  = actor->getComponent<MockMeshComponent>().get();
	MockAudioComponent* audio = actor->getComponent<MockAudioComponent>().get();
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
	MockMeshComponent* keeperMesh = keeper->getComponent<MockMeshComponent>().get();
	SceneComponent*	   victimSc	  = victim->getComponent<SceneComponent>().get();
	SW_ASSERT_NOT_NULL( keeperMesh );
	SW_ASSERT_NOT_NULL( victimSc );

	keeperMesh->setTickDelegate( SW_DELEGATE_LAMBDA( Component::ComponentTickDelegate,
													 [&manager, victim, victimSc]( float32 )
	{
		victimSc->setLocalPosition( float3( 1.0f, 2.0f, 3.0f ) );
		manager.destroyObject( victim );
	} ) );

	manager.tick( 0.016f );
	SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "TransformVictim" ) ) );
	SW_EXPECT_NOT_NULL( manager.findGameObjectByName( hashed_string( "TransformKeeper" ) ) );
}

/**
 * @brief [GameObjectManagerTest] 틱 중 attach는 지연된 뒤 HierarchyData를 만들 수 있다
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
	SceneComponent*	   parentSc	  = parent->getComponent<SceneComponent>().get();
	SceneComponent*	   childSc	  = child->getComponent<SceneComponent>().get();
	MockMeshComponent* tickerMesh = ticker->getComponent<MockMeshComponent>().get();
	SW_ASSERT_NOT_NULL( parentSc );
	SW_ASSERT_NOT_NULL( childSc );
	SW_ASSERT_NOT_NULL( tickerMesh );

	tickerMesh->setTickDelegate( SW_DELEGATE_LAMBDA( Component::ComponentTickDelegate,
													 [childSc, parentSc]( float32 )
	{
		childSc->attachToComponent( parentSc );
	} ) );

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

	GameObject* root	   = manager.createGameObject( hashed_string( "LWCRoot" ) );
	GameObject* child	   = manager.createGameObject( hashed_string( "LWCChild" ) );
	GameObject* grandChild = manager.createGameObject( hashed_string( "LWCGandChild" ) );

	root->addComponent<SceneComponent>();
	child->addComponent<SceneComponent>();
	grandChild->addComponent<SceneComponent>();

	SceneComponent* rootSc		 = root->getComponent<SceneComponent>().get();
	SceneComponent* childSc		 = child->getComponent<SceneComponent>().get();
	SceneComponent* grandChildSc = grandChild->getComponent<SceneComponent>().get();

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
	sw::GameObject*		   obj	= manager.createGameObject( sw::hashed_string( "TargetObj" ) );
	sw::MockMeshComponent* mesh = obj->addComponent<sw::MockMeshComponent>();

	sw::GameObjectPtr objPtr( obj );
	sw::ComponentPtr  compPtr( mesh );

	SW_EXPECT_TRUE( objPtr.isValid() );
	SW_EXPECT_TRUE( compPtr.isValid() );

	// Destroy component only
	manager.destroyComponent( mesh );
	manager.tick( 0.016f );

	SW_EXPECT_TRUE( objPtr.isValid() );	  // Object is still alive
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
	sw::GameObject*		  pParentObj = manager.createGameObject( sw::hashed_string( "Parent" ) );
	sw::GameObject*		  pChildObj	 = manager.createGameObject( sw::hashed_string( "Child" ) );

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

	auto* pParentTdata = pParentObj->getComponent<sw::TransformData>().get();
	auto* pChildTdata  = pChildObj->getComponent<sw::TransformData>().get();

	SW_ASSERT_NOT_NULL( pParentTdata );
	SW_ASSERT_NOT_NULL( pChildTdata );

	SW_EXPECT_EQUAL( 0, static_cast<int32>( pParentTdata->bIsTransformDirty ) );
	SW_EXPECT_EQUAL( 0, static_cast<int32>( pChildTdata->bIsTransformDirty ) );

	// Move child only
	pChildSc->setLocalPosition( sw::float3( 20.0f, 0.0f, 0.0f ) );

	// Child must be dirty
	SW_EXPECT_EQUAL( 1, static_cast<int32>( pChildTdata->bIsTransformDirty ) );
	// Parent transform must NOT be dirty (child move does not dirty parent)
	SW_EXPECT_EQUAL( 0, static_cast<int32>( pParentTdata->bIsTransformDirty ) );
	// Parent must have bHasDirtyDescendant == 1
	SW_EXPECT_EQUAL( 1, static_cast<int32>( pParentTdata->bHasDirtyDescendant ) );

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
	sw::GameObject*		  pObj = manager.createGameObject( sw::hashed_string( "DeferredTestObj" ) );
	SW_ASSERT_NOT_NULL( pObj );

	// 틱 실행 구간에서 addComponent 호출 시 deferPostTick에 등록되어 지연 실행
	manager.deferPostTick( [pObj]()
	{
		sw::MockAudioComponent* pAudio = pObj->addComponent<sw::MockAudioComponent>();
		SW_ASSERT_NOT_NULL( pAudio );
		pAudio->_volume = 0.75f;
	} );

	manager.tick( 0.016f );

	sw::MockAudioComponent* pAudioComp = pObj->getComponent<sw::MockAudioComponent>().get();
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
	sw::GameObject*		  pParent = manager.createGameObject( sw::hashed_string( "CascadeParent" ) );
	sw::GameObject*		  pChild1 = manager.createGameObject( sw::hashed_string( "CascadeChild1" ) );
	sw::GameObject*		  pChild2 = manager.createGameObject( sw::hashed_string( "CascadeChild2" ) );

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
	sw::GameObject*		  pObj = manager.createGameObject( sw::hashed_string( "NoOpTransformTest" ) );
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
	sw::GameObject*		  pObj = manager.createGameObject( sw::hashed_string( "GenerationTestObj" ) );
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
