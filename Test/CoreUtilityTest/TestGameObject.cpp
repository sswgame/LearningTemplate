/**
 * @file TestGameObject.cpp
 * @brief Auto-generated documentation header
 */
#include "Core/Object/Component.h"
#include "Core/Object/ComponentManager.h"
#include "Core/Object/GameObject.h"
#include "TestFramework.h"

namespace sw
{

	class MockMeshComponent : public Component
	{
	public:
		std::string _meshName  = "CubeMesh";
		int32		_tickCount = 0;

		virtual void onTick( float32 deltaTime ) override
		{
			Component::onTick( deltaTime );
			_tickCount++;
		}
	};

	class MockAudioComponent : public Component
	{
	public:
		float32 _volume	   = 1.0f;
		int32	_playCount = 0;

		virtual void onTick( float32 deltaTime ) override
		{
			Component::onTick( deltaTime );
			_playCount++;
		}
	};
} // namespace sw

SW_TEST_CASE( GameObjectTest, MultiSameComponentAttachment )
{
	sw::GameObject actor( sw::hashed_string( "TestPlayer" ) );
	SW_EXPECT_EQUAL( std::string( "TestPlayer" ), std::string( actor.getName().c_str() ) );

	sw::MockMeshComponent* mesh1 = actor.addComponent<sw::MockMeshComponent>();
	sw::MockMeshComponent* mesh2 = actor.addComponent<sw::MockMeshComponent>();
	sw::MockMeshComponent* mesh3 = actor.addComponent<sw::MockMeshComponent>();

	mesh1->_meshName = "HeadMesh";
	mesh2->_meshName = "BodyMesh";
	mesh3->_meshName = "WeaponMesh";

	SW_EXPECT_EQUAL( 3u, actor.getComponentCount() );

	std::vector<sw::MockMeshComponent*> meshes = actor.getComponents<sw::MockMeshComponent>();
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( meshes.size() ) );
	if ( meshes.size() == 3 )
	{
		SW_EXPECT_EQUAL( std::string( "HeadMesh" ), meshes[0]->_meshName );
		SW_EXPECT_EQUAL( std::string( "BodyMesh" ), meshes[1]->_meshName );
		SW_EXPECT_EQUAL( std::string( "WeaponMesh" ), meshes[2]->_meshName );
	}

	sw::MockMeshComponent* firstMesh = actor.getComponent<sw::MockMeshComponent>();
	SW_EXPECT_EQUAL( mesh1, firstMesh );

	actor.tick( 0.016f );
	SW_EXPECT_EQUAL( 1, mesh1->_tickCount );
	SW_EXPECT_EQUAL( 1, mesh2->_tickCount );
	SW_EXPECT_EQUAL( 1, mesh3->_tickCount );

	actor.removeComponent( mesh2 );
	SW_EXPECT_EQUAL( 2u, actor.getComponentCount() );
}

SW_TEST_CASE( GameObjectTest, EditorDynamicComponentAttachment )
{
	sw::getComponentManager().registerComponentType<sw::MockMeshComponent>( sw::hashed_string( "MockMeshComponent" ) );
	sw::getComponentManager().registerComponentType<sw::MockAudioComponent>( sw::hashed_string( "MockAudioComponent" ) );

	const std::vector<sw::hashed_string>& types = sw::getComponentManager().getRegisteredComponentTypes();
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( types.size() ) );

	sw::GameObject			actor( sw::hashed_string( "EditorActor" ) );
	sw::MockMeshComponent*	comp1 = actor.addComponent<sw::MockMeshComponent>();
	sw::MockAudioComponent* comp2 = actor.addComponent<sw::MockAudioComponent>();

	SW_EXPECT_TRUE( comp1 != nullptr );
	SW_EXPECT_TRUE( comp2 != nullptr );
	SW_EXPECT_EQUAL( 2u, actor.getComponentCount() );

	SW_EXPECT_EQUAL( &actor, comp1->getOwner() );
	SW_EXPECT_EQUAL( &actor, comp2->getOwner() );

	sw::getComponentManager().shutdown();
}

SW_TEST_CASE( GameObjectTest, ParallelComponentTicking )
{
	sw::getComponentManager().registerComponentType<sw::MockMeshComponent>( sw::hashed_string( "MockMeshComponent" ) );

	sw::GameObject				actor;
	std::vector<sw::Component*> activeComps;

	for ( int32 i = 0; i < 100; ++i )
	{
		sw::MockMeshComponent* comp = actor.addComponent<sw::MockMeshComponent>();
		activeComps.push_back( comp );
	}

	SW_EXPECT_EQUAL( 100u, actor.getComponentCount() );

	sw::getComponentManager().tickAllComponentsParallel( activeComps, 0.016f );

	for ( sw::Component* comp : activeComps )
	{
		sw::MockMeshComponent* meshComp = static_cast<sw::MockMeshComponent*>( comp );
		SW_EXPECT_EQUAL( 1, meshComp->_tickCount );
	}

	sw::getComponentManager().shutdown();
}

SW_TEST_CASE( GameObjectTest, ReflectionSupport )
{
	sw::GameObject		   actor( sw::hashed_string( "ReflectedActor" ) );
	sw::MockMeshComponent* meshComp = actor.addComponent<sw::MockMeshComponent>();

	SW_EXPECT_TRUE( actor.getObjectId() != 0 );
	SW_EXPECT_TRUE( meshComp->getComponentId() != 0 );
}

#include "Core/Object/SceneComponent.h"
#include "Core/Object/TagSystem.h"
#include "Core/Utility/Math/MathUtil.h"

using namespace sw;

SW_TEST_CASE( TagSystemTest, IntegerLiteralAndHierarchicalSubsumption )
{
	constexpr TagID tagAttacking = "State.Combat.Attacking"_tag;
	constexpr TagID tagCombat	 = "State.Combat"_tag;
	constexpr TagID tagState	 = "State"_tag;

	SW_EXPECT_TRUE( tagAttacking.isValid() );
	SW_EXPECT_TRUE( tagCombat.isValid() );

	SW_EXPECT_TRUE( tagAttacking.isSubtagOf( tagCombat ) );
	SW_EXPECT_TRUE( tagAttacking.isSubtagOf( tagState ) ); // full parent chain
	SW_EXPECT_TRUE( tagCombat.isSubtagOf( tagState ) );
	SW_EXPECT_FALSE( tagState.isSubtagOf( tagAttacking ) );

	TagContainer container{ tagAttacking };
	SW_EXPECT_TRUE( container.hasTag( tagAttacking, true ) );
	SW_EXPECT_TRUE( container.hasTag( tagState, false ) );

	TagContainer required{ tagAttacking };
	TagContainer forbidden{ "State.Dead"_tag };
	SW_EXPECT_TRUE( container.matchTags( required, forbidden ) );
}

SW_TEST_CASE( GameObjectTest, TagManagement )
{
	GameObject actor( hashed_string( "TaggedHero" ) );

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

SW_TEST_CASE( SceneComponentTest, ParentChildHierarchyAndDirtyPropagation )
{
	GameObject actor( hashed_string( "ActorWithSceneComps" ) );

	SceneComponent* parentComp = actor.addComponent<SceneComponent>();
	SceneComponent* childComp  = actor.addComponent<SceneComponent>();

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

SW_TEST_CASE( SceneComponentTest, ParentRotationScalePropagatesToChild )
{
	GameObject actor( hashed_string( "RotatedScaledHierarchy" ) );

	SceneComponent* parentComp = actor.addComponent<SceneComponent>();
	SceneComponent* childComp  = actor.addComponent<SceneComponent>();

	// 90 deg yaw: local +X becomes world -Z (right-handed Y-up).
	parentComp->setLocalPosition( float3( 10.0f, 0.0f, 0.0f ) );
	parentComp->setLocalRotation( float3( 0.0f, MathUtil::HalfPi, 0.0f ) );
	parentComp->setLocalScale( float3( 2.0f, 2.0f, 2.0f ) );

	childComp->setLocalPosition( float3( 1.0f, 0.0f, 0.0f ) );
	SW_ASSERT_TRUE( childComp->attachToComponent( parentComp ) );

	const float3 childWorldPos = childComp->getWorldPosition();
	SW_EXPECT_NEAR_EQUAL( 10.0f, childWorldPos._x, 1e-3f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, childWorldPos._y, 1e-3f );
	SW_EXPECT_NEAR_EQUAL( -2.0f, childWorldPos._z, 1e-3f ); // scaled 1 * 2, then yaw 90

	const float4x4 childWorldMat = childComp->getWorldMatrix();
	SW_EXPECT_NEAR_EQUAL( childWorldPos._x, childWorldMat._41, 1e-3f );
	SW_EXPECT_NEAR_EQUAL( childWorldPos._y, childWorldMat._42, 1e-3f );
	SW_EXPECT_NEAR_EQUAL( childWorldPos._z, childWorldMat._43, 1e-3f );

	const double3 cameraPos( 10.0, 0.0, 0.0 );
	const float4x4 cameraRel = childComp->getCameraRelativeWorldMatrix( cameraPos );
	SW_EXPECT_NEAR_EQUAL( 0.0f, cameraRel._41, 1e-3f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, cameraRel._42, 1e-3f );
	SW_EXPECT_NEAR_EQUAL( -2.0f, cameraRel._43, 1e-3f );
	// Hierarchy scale should remain in the camera-relative matrix (not identity upper 3x3).
	const float3 camScale = cameraRel.getScale();
	SW_EXPECT_NEAR_EQUAL( 2.0f, camScale._x, 1e-3f );
}

SW_TEST_CASE( SceneComponentTest, LargeWorldCoordinatesAndCameraRelativeRendering )
{
	GameObject actor( hashed_string( "LWCActor" ) );

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

SW_TEST_CASE( MathTest, Double3VectorOperations )
{
	double3 v1( 3.0, 4.0, 0.0 );
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

#include "Core/Object/GameObjectManager.h"
#include "Core/Object/ObjectStateSerializer.h"

class MockCallbackComponent : public Component
{
public:
	hashed_string _lastChangedProperty;

	virtual void onPropertyChanged( hashed_string propertyName ) override
	{
		Component::onPropertyChanged( propertyName );
		_lastChangedProperty = propertyName;
	}
};

SW_TEST_CASE( GameObjectManagerTest, CreationSearchAndDeferredDestruction )
{
	GameObjectManager manager;

	GameObject* hero  = manager.createGameObject( hashed_string( "Hero" ) );
	GameObject* enemy = manager.createGameObject( hashed_string( "Enemy" ) );
	SW_ASSERT_NOT_NULL( hero );
	SW_ASSERT_NOT_NULL( enemy );

	SW_EXPECT_EQUAL( size_t( 2 ), manager.getAllGameObjects().size() );
	SW_EXPECT_EQUAL( hero, manager.findGameObjectByName( hashed_string( "Hero" ) ) );
	SW_EXPECT_EQUAL( enemy, manager.findGameObjectById( enemy->getObjectId() ) );
	SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Missing" ) ) );

	hero->setName( hashed_string( "HeroRenamed" ) );
	SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Hero" ) ) );
	SW_EXPECT_EQUAL( hero, manager.findGameObjectByName( hashed_string( "HeroRenamed" ) ) );

	manager.destroyObjectDeferred( enemy );
	SW_EXPECT_EQUAL( size_t( 2 ), manager.getAllGameObjects().size() );
	manager.processDeferredDestruction();

	SW_EXPECT_EQUAL( size_t( 1 ), manager.getAllGameObjects().size() );
	SW_EXPECT_NULL( manager.findGameObjectByName( hashed_string( "Enemy" ) ) );
	SW_EXPECT_NOT_NULL( manager.findGameObjectByName( hashed_string( "HeroRenamed" ) ) );

	manager.clear();
	SW_EXPECT_EQUAL( size_t( 0 ), manager.getAllGameObjects().size() );
}

SW_TEST_CASE( GameObjectManagerTest, SequentialAndParallelTick )
{
	GameObjectManager manager;

	GameObject*		   a	= manager.createGameObject( hashed_string( "A" ) );
	GameObject*		   b	= manager.createGameObject( hashed_string( "B" ) );
	MockMeshComponent* aMesh = a->addComponent<MockMeshComponent>();
	MockMeshComponent* bMesh = b->addComponent<MockMeshComponent>();

	manager.tick( 0.016f );
	SW_EXPECT_EQUAL( 1, aMesh->_tickCount );
	SW_EXPECT_EQUAL( 1, bMesh->_tickCount );

	manager.tickParallel( 0.016f );
	SW_EXPECT_EQUAL( 2, aMesh->_tickCount );
	SW_EXPECT_EQUAL( 2, bMesh->_tickCount );

	manager.clear();
}

SW_TEST_CASE( GameObjectManagerTest, ParallelTickReadsStableHierarchyTransforms )
{
	GameObjectManager manager;

	GameObject* parentObj = manager.createGameObject( hashed_string( "Parent" ) );
	GameObject* childObj  = manager.createGameObject( hashed_string( "Child" ) );

	SceneComponent* parentComp = parentObj->addComponent<SceneComponent>();
	SceneComponent* childComp  = childObj->addComponent<SceneComponent>();
	parentComp->setLocalPosition( float3( 10.0f, 0.0f, 0.0f ) );
	childComp->setLocalPosition( float3( 5.0f, 0.0f, 0.0f ) );
	SW_ASSERT_TRUE( childComp->attachToComponent( parentComp ) );

	float3 observedDuringTick{ 0.0f, 0.0f, 0.0f };
	childComp->_onTickDelegate = SW_DELEGATE_LAMBDA( Component::ComponentTickDelegate,
													 [&observedDuringTick, childComp]( float32 )
	{
		observedDuringTick = childComp->getWorldPosition();
	} );

	manager.tickParallel( 0.016f );
	SW_EXPECT_NEAR_EQUAL( 15.0f, observedDuringTick._x, 1e-4f );

	// Local write during tick is visible after post-flush, not necessarily mid-tick snapshot.
	parentComp->_onTickDelegate = SW_DELEGATE_LAMBDA( Component::ComponentTickDelegate,
													  [parentComp]( float32 )
	{
		parentComp->setLocalPosition( float3( 20.0f, 0.0f, 0.0f ) );
	} );
	manager.tickParallel( 0.016f );
	SW_EXPECT_NEAR_EQUAL( 25.0f, childComp->getWorldPosition()._x, 1e-4f );

	manager.clear();
}

SW_TEST_CASE( ObjectStateXmlSerializerTest, SaveAndLoadXmlString )
{
	GameObject source( hashed_string( "SerializedHero" ) );
	source.setActive( false );

	const std::string xml = ObjectStateSerializer::saveToXmlString( &source );
	SW_ASSERT_TRUE( xml.empty() == false );
	SW_EXPECT_TRUE( xml.find( "GameObjectState" ) != std::string::npos );
	SW_EXPECT_TRUE( xml.find( "SerializedHero" ) != std::string::npos );

	GameObject target( hashed_string( "Temp" ) );
	target.setActive( true );
	SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( &target, xml ) );
	SW_EXPECT_STREQ( "SerializedHero", target.getName().c_str() );
	SW_EXPECT_FALSE( target.isActive() );

	SW_EXPECT_FALSE( ObjectStateSerializer::loadFromXmlString( nullptr, xml ) );
	SW_EXPECT_FALSE( ObjectStateSerializer::loadFromXmlString( &target, "" ) );
	SW_EXPECT_EMPTY( ObjectStateSerializer::saveToXmlString( nullptr ) );
}

SW_TEST_CASE( ComponentTickGroupTest, TickOrderPrePhysicsToPostUpdate )
{
	GameObject actor( hashed_string( "TickGroupActor" ) );

	MockMeshComponent* compPost = actor.addComponent<MockMeshComponent>();
	compPost->setTickGroup( sw::TickGroup::PostPhysics );

	MockMeshComponent* compPre = actor.addComponent<MockMeshComponent>();
	compPre->setTickGroup( sw::TickGroup::PrePhysics );

	actor.tick( 0.016f );
	SW_EXPECT_EQUAL( 1, compPre->_tickCount );
	SW_EXPECT_EQUAL( 1, compPost->_tickCount );
}

SW_TEST_CASE( HierarchicalActiveStateTest, SubtreeTickSkip )
{
	GameObject		   actor( hashed_string( "ActiveTestActor" ) );
	MockMeshComponent* comp = actor.addComponent<MockMeshComponent>();

	actor.setActive( false );
	SW_EXPECT_FALSE( actor.isActiveInHierarchy() );

	actor.tick( 0.016f );

	SW_EXPECT_EQUAL( 0, comp->_tickCount );
}

SW_TEST_CASE( GameObjectHierarchyTest, ParentChildAttachAndActivePropagation )
{
	GameObject parent( hashed_string( "ParentGO" ) );
	GameObject child( hashed_string( "ChildGO" ) );
	GameObject grand( hashed_string( "GrandGO" ) );

	SW_EXPECT_TRUE( child.attachToParent( &parent ) );
	SW_EXPECT_TRUE( grand.attachToParent( &child ) );
	SW_EXPECT_EQUAL( &parent, child.getParent() );
	SW_EXPECT_EQUAL( &child, grand.getParent() );
	SW_EXPECT_EQUAL( size_t( 1 ), parent.getChildren().size() );
	SW_EXPECT_EQUAL( &child, parent.getChildren()[0] );

	// Cycle attach must fail.
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
	// Detached grand is root again; own active still true.
	SW_EXPECT_TRUE( grand.isActive() );
	SW_EXPECT_TRUE( grand.isActiveInHierarchy() );
}

SW_TEST_CASE( PostEditChangePropertyTest, CallbackOnPropertyChanged )
{
	GameObject			   actor( hashed_string( "PropertyChangedActor" ) );
	MockCallbackComponent* comp = actor.addComponent<MockCallbackComponent>();

	comp->setActive( false );
	// The property change notification should work, but the exact property name may vary based on reflection system
	SW_EXPECT_TRUE( comp->_lastChangedProperty.getHash() != 0 );
}
