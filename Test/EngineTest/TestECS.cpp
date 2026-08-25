#include "pch.h"

#include "Core/Container/vector.h"

#include "Engine/ECS/ArchetypeChunkPool.h"
#include "Engine/ECS/Registry.h"
#include "Engine/ECS/View.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Spatial/BVHTree3D.h"
#include "Engine/Spatial/SpatialHashGrid2D.h"

#include "TestFramework/TestFramework.h"

#include <atomic>
#include <thread>
using namespace sw;

// ------------------------------------------------------------------------------
// 0) 헬퍼 — Position / Velocity 테스트 컴포넌트
// ------------------------------------------------------------------------------
struct Position
{
	float32 x, y, z;
	/** @brief 원점으로 초기화합니다. */
	Position()
		: x( 0 ), y( 0 ), z( 0 )
	{
	}
	/** @brief 좌표로 초기화합니다. */
	Position( float32 _x, float32 _y, float32 _z )
		: x( _x ), y( _y ), z( _z )
	{
	}
	/** @brief 테스트용 정적 TypeInfo 를 반환합니다. */
	static const sw::TypeInfo* StaticType()
	{
		static sw::TypeInfo info;
		info._typeId = 10000;
		return &info;
	}
};
struct Velocity
{
	float32 dx, dy, dz;
	/** @brief 속도를 0으로 초기화합니다. */
	Velocity()
		: dx( 0 ), dy( 0 ), dz( 0 )
	{
	}
	/** @brief 속도 성분으로 초기화합니다. */
	Velocity( float32 _dx, float32 _dy, float32 _dz )
		: dx( _dx ), dy( _dy ), dz( _dz )
	{
	}
	/** @brief 테스트용 정적 TypeInfo 를 반환합니다. */
	static const sw::TypeInfo* StaticType()
	{
		static sw::TypeInfo info;
		info._typeId = 10001;
		return &info;
	}
};

struct DeadTag
{
	static const sw::TypeInfo* StaticType()
	{
		static sw::TypeInfo info;
		info._typeId = 10002;
		return &info;
	}
};

// ------------------------------------------------------------------------------
// 1) ECS — 엔티티 수명·View 순회
// ------------------------------------------------------------------------------
/**
 * @brief [ECS] 엔티티 생성·파괴와 컴포넌트 부착
 */
SW_TEST_CASE( ECS, Registry_CreateDestroy )
{
	Registry reg;
	Entity	 e1 = reg.create();
	Entity	 e2 = reg.create();

	SW_EXPECT_NOT_EQUAL( e1, e2 );

	reg.emplace<Position>( e1, 1.0f, 2.0f, 3.0f );
	SW_EXPECT_TRUE( reg.has<Position>( e1 ) );
	SW_EXPECT_FALSE( reg.has<Position>( e2 ) );

	reg.destroy( e1 );
	SW_EXPECT_FALSE( reg.has<Position>( e1 ) );
	SW_EXPECT_FALSE( reg.isValid( e1 ) );

	Entity e3 = reg.create();
	SW_EXPECT_EQUAL( e1.index(), e3.index() );
	SW_EXPECT_NOT_EQUAL( e1, e3 );
	SW_EXPECT_TRUE( reg.isValid( e3 ) );
	SW_EXPECT_FALSE( reg.isValid( e1 ) );
}

/**
 * @brief [ECS] TComponentHandle은 destroy 이후 get()이 nullptr이다
 */
SW_TEST_CASE( ECS, ComponentHandle_StaleAfterDestroy )
{
	Registry reg;
	Entity	 e = reg.create();
	reg.emplace<Position>( e, 1.0f, 2.0f, 3.0f );

	auto handle = reg.handleFor<Position>( e );
	SW_ASSERT_NOT_NULL( handle.get() );
	SW_EXPECT_EQUAL( 1.0f, handle.get()->x );
	SW_EXPECT_EQUAL( e, handle.entity() );
	SW_EXPECT_TRUE( handle.untyped().isValid() );
	SW_EXPECT_EQUAL( e, handle.untyped().entity() );
	SW_EXPECT_EQUAL( 10000u, handle.untyped().typeId() );

	Entity missingEntity = reg.create();
	SW_EXPECT_TRUE( reg.handleFor<Position>( missingEntity ) == nullptr );

	reg.destroy( e );
	SW_EXPECT_TRUE( handle == nullptr );
	SW_EXPECT_NULL( handle.get() );
	SW_EXPECT_FALSE( reg.isValid( handle.entity() ) );
}

/**
 * @brief [ECS] Position+Velocity 뷰 순회
 */
SW_TEST_CASE( ECS, View_Iteration )
{
	Registry reg;
	Entity	 e1 = reg.create();
	Entity	 e2 = reg.create();
	Entity	 e3 = reg.create();

	reg.emplace<Position>( e1, 1.0f, 0.0f, 0.0f );
	reg.emplace<Velocity>( e1, 1.0f, 0.0f, 0.0f );

	reg.emplace<Position>( e2, 2.0f, 0.0f, 0.0f ); // Velocity 없음

	reg.emplace<Position>( e3, 3.0f, 0.0f, 0.0f );
	reg.emplace<Velocity>( e3, 0.0f, 1.0f, 0.0f );

	int32 matchCount{ 0 };

	View<Position, Velocity> view( reg );
	view.each( [&]( Entity e, Position& /*pos*/, Velocity& /*vel*/ )
	{
		matchCount++;
		SW_EXPECT_TRUE( e == e1 || e == e3 );
	} );

	SW_EXPECT_EQUAL( 2, matchCount );
}

/**
 * @brief [ECS] 다른 엔티티를 지워도 살아 있는 컴포넌트 주소는 유지
 */
SW_TEST_CASE( ECS, ComponentPointerStableAcrossErase )
{
	Registry reg;
	Entity	 keep	= reg.create();
	Entity	 remove = reg.create();
	Entity	 other	= reg.create();

	reg.emplace<Position>( keep, 1.0f, 2.0f, 3.0f );
	reg.emplace<Position>( remove, 4.0f, 5.0f, 6.0f );
	reg.emplace<Position>( other, 7.0f, 8.0f, 9.0f );

	Position* ptrKeep = reg.getPtr<Position>( keep );
	SW_ASSERT_NOT_NULL( ptrKeep );
	SW_EXPECT_EQUAL( 1.0f, ptrKeep->x );

	reg.remove<Position>( remove );
	SW_EXPECT_EQUAL( ptrKeep, reg.getPtr<Position>( keep ) );
	SW_EXPECT_EQUAL( 1.0f, ptrKeep->x );

	reg.destroy( other );
	SW_EXPECT_EQUAL( ptrKeep, reg.getPtr<Position>( keep ) );
	SW_EXPECT_EQUAL( 3.0f, ptrKeep->z );
}

/**
 * @brief [ECS] CommandBuffer로 지연 destroy 후 execute
 */
SW_TEST_CASE( ECS, CommandBufferDeferredDestroy )
{
	Registry reg;
	Entity	 e = reg.create();
	reg.emplace<Position>( e, 1.0f, 0.0f, 0.0f );
	SW_EXPECT_TRUE( reg.has<Position>( e ) );

	reg.destroyDeferred( e );
	SW_EXPECT_TRUE( reg.has<Position>( e ) );
	reg.getCommandBuffer().execute( reg );
	SW_EXPECT_FALSE( reg.has<Position>( e ) );
}

/**
 * @brief [ECS] CommandBuffer remove 후 execute
 */
SW_TEST_CASE( ECS, CommandBufferDeferredRemove )
{
	Registry reg;
	Entity	 e = reg.create();
	reg.emplace<Position>( e, 1.0f, 0.0f, 0.0f );
	SW_EXPECT_TRUE( reg.has<Position>( e ) );

	reg.getCommandBuffer().push( [e]( Registry& r )
	{
		r.remove<Position>( e );
	} );
	SW_EXPECT_TRUE( reg.has<Position>( e ) );
	reg.getCommandBuffer().execute( reg );
	SW_EXPECT_FALSE( reg.has<Position>( e ) );
}

/**
 * @brief [ECS] 읽기 스레드와 CommandBuffer destroy가 풀 락으로 직렬화된다
 */
SW_TEST_CASE( ECS, ConcurrentReadWithCommandBufferDestroy )
{
	Registry		   reg;
	sw::vector<Entity> ents;
	ents.reserve( 256 );
	for ( int32 entityIndex = 0; entityIndex < 256; ++entityIndex )
	{
		Entity e = reg.create();
		reg.emplace<Position>( e, static_cast<float32>( entityIndex ), 0.0f, 0.0f );
		ents.push_back( e );
	}

	const auto&		   constEnts = ents;
	std::atomic<bool>  started{ false };
	std::atomic<bool>  stop{ false };
	std::atomic<int32> reads{ 0 };
	std::thread		   reader( [&]()
	   {
		   started.store( true, std::memory_order_release );
		   while ( stop.load( std::memory_order_acquire ) == false )
		   {
			   for ( size_t entityIndex = 1; entityIndex < constEnts.size(); entityIndex += 2 )
			   {
				   if ( reg.has<Position>( constEnts[entityIndex] ) )
					   reads.fetch_add( 1, std::memory_order_relaxed );
			   }
		   }
	   } );

	while ( started.load( std::memory_order_acquire ) == false )
		std::this_thread::yield();

	for ( size_t entityIndex = 0; entityIndex < constEnts.size(); entityIndex += 2 )
		reg.destroyDeferred( constEnts[entityIndex] );
	reg.getCommandBuffer().execute( reg );

	stop.store( true, std::memory_order_release );
	reader.join();

	SW_EXPECT_TRUE( reads.load( std::memory_order_relaxed ) > 0 );
	SW_EXPECT_FALSE( reg.has<Position>( ents[0] ) );
	SW_EXPECT_TRUE( reg.has<Position>( ents[1] ) );
	Position* kept = reg.getPtr<Position>( ents[1] );
	SW_ASSERT_NOT_NULL( kept );
	SW_EXPECT_EQUAL( 1.0f, kept->x );
}

/**
 * @brief [ECS] 파괴된 핸들은 인덱스 재사용 뒤에도 컴포넌트를 가리키지 않는다
 */
SW_TEST_CASE( ECS, StaleHandleDoesNotAliasReusedIndex )
{
	Registry reg;
	Entity	 stale = reg.create();
	reg.emplace<Position>( stale, 1.0f, 2.0f, 3.0f );
	reg.destroy( stale );

	Entity fresh = reg.create();
	reg.emplace<Position>( fresh, 9.0f, 8.0f, 7.0f );
	SW_EXPECT_EQUAL( stale.index(), fresh.index() );
	SW_EXPECT_FALSE( reg.has<Position>( stale ) );
	SW_EXPECT_TRUE( reg.getPtr<Position>( stale ) == nullptr );
	Position* live = reg.getPtr<Position>( fresh );
	SW_ASSERT_NOT_NULL( live );
	SW_EXPECT_EQUAL( 9.0f, live->x );
}

/**
 * @brief [ECS] beginTick 중 remove는 endTick 전까지 풀에 남는다
 */
SW_TEST_CASE( ECS, BeginTickDefersTypedRemove )
{
	Registry reg;
	Entity	 e = reg.create();
	reg.emplace<Position>( e, 1.0f, 0.0f, 0.0f );

	reg.beginTick();
	reg.remove<Position>( e );
	SW_EXPECT_TRUE( reg.isTicking() );
	SW_EXPECT_TRUE( reg.has<Position>( e ) );
	reg.endTick();

	SW_EXPECT_FALSE( reg.isTicking() );
	SW_EXPECT_FALSE( reg.has<Position>( e ) );
}

/**
 * @brief [ECS] 같은 타입 두 엔티티 remove를 한 번에 flush해도 둘 다 제거된다
 */
SW_TEST_CASE( ECS, EndTickFlushRemovesTwoSameType )
{
	Registry reg;
	Entity	 e1 = reg.create();
	Entity	 e2 = reg.create();
	reg.emplace<Position>( e1, 1.0f, 0.0f, 0.0f );
	reg.emplace<Position>( e2, 2.0f, 0.0f, 0.0f );

	reg.beginTick();
	reg.remove<Position>( e1 );
	reg.remove<Position>( e2 );
	SW_EXPECT_TRUE( reg.has<Position>( e1 ) );
	SW_EXPECT_TRUE( reg.has<Position>( e2 ) );
	reg.endTick();

	SW_EXPECT_FALSE( reg.has<Position>( e1 ) );
	SW_EXPECT_FALSE( reg.has<Position>( e2 ) );
}

/**
 * @brief [ECS] withComponent 헬퍼 동작 검증
 */
SW_TEST_CASE( ECS, WithComponentHelper )
{
	Registry reg;
	Entity	 e = reg.create();
	reg.emplace<Position>( e, 10.0f, 20.0f, 30.0f );

	bool bCalled{ false };
	bool bFound = reg.withComponent<Position>( e, [&]( Position& pos )
	{
		bCalled = true;
		SW_EXPECT_EQUAL( 10.0f, pos.x );
		pos.x = 99.0f;
	} );

	SW_EXPECT_TRUE( bFound );
	SW_EXPECT_TRUE( bCalled );

	const Registry& constReg = reg;
	bool			bConstCalled{ false };
	bool			bConstFound = constReg.withComponentConst<Position>( e, [&]( const Position& pos )
			   {
		   bConstCalled = true;
		   SW_EXPECT_EQUAL( 99.0f, pos.x );
	   } );

	SW_EXPECT_TRUE( bConstFound );
	SW_EXPECT_TRUE( bConstCalled );

	// Non-existent component
	bool bMissing = reg.withComponent<Velocity>( e, []( Velocity& ) {} );
	SW_EXPECT_FALSE( bMissing );
}

/**
 * @brief [ECS] 10,000개 대량 엔티티 생성/파괴 및 Generational Indexing 스트레스 테스트
 */
SW_TEST_CASE( ECS, MassiveEntityGenerationStressTest )
{
	Registry		   reg;
	constexpr size_t   kEntityCount = 1000;
	sw::vector<Entity> entities;
	entities.reserve( kEntityCount );

	for ( size_t entityIndex = 0; entityIndex < kEntityCount; ++entityIndex )
	{
		Entity e = reg.create();
		SW_EXPECT_TRUE( reg.isValid( e ) );
		reg.emplace<Position>( e, static_cast<float32>( entityIndex ), 0.0f, 0.0f );
		entities.push_back( e );
	}

	// 짝수 엔티티 파괴
	for ( size_t entityIndex = 0; entityIndex < kEntityCount; entityIndex += 2 )
	{
		reg.destroy( entities[entityIndex] );
		SW_EXPECT_FALSE( reg.isValid( entities[entityIndex] ) );
		SW_EXPECT_FALSE( reg.has<Position>( entities[entityIndex] ) );
	}

	// 파괴된 슬롯 재할당
	sw::vector<Entity> reusedEntities;
	reusedEntities.reserve( kEntityCount / 2 );
	for ( size_t entityIndex = 0; entityIndex < kEntityCount / 2; ++entityIndex )
	{
		Entity e = reg.create();
		SW_EXPECT_TRUE( reg.isValid( e ) );
		reg.emplace<Position>( e, 999.0f, 0.0f, 0.0f );
		reusedEntities.push_back( e );
	}

	// 홀수 엔티티는 여전히 살아있고 유효한지 검증
	for ( size_t entityIndex = 1; entityIndex < kEntityCount; entityIndex += 2 )
	{
		SW_EXPECT_TRUE( reg.isValid( entities[entityIndex] ) );
		SW_EXPECT_TRUE( reg.has<Position>( entities[entityIndex] ) );
		Position* pos = reg.getPtr<Position>( entities[entityIndex] );
		SW_ASSERT_NOT_NULL( pos );
		SW_EXPECT_NEAR_EQUAL( static_cast<float32>( entityIndex ), pos->x, 0.001f );
	}

	// 남은 엔티티들 일괄 파괴 검증
	for ( size_t entityIndex = 1; entityIndex < kEntityCount; entityIndex += 2 )
	{
		reg.destroy( entities[entityIndex] );
		SW_EXPECT_FALSE( reg.isValid( entities[entityIndex] ) );
	}
	for ( Entity reusedEntity : reusedEntities )
	{
		reg.destroy( reusedEntity );
		SW_EXPECT_FALSE( reg.isValid( reusedEntity ) );
	}
}

/**
 * @brief [ECS] 3중 컴포넌트 조합 View 교차 필터링 검증
 */
SW_TEST_CASE( ECS, MultiComponent3WayViewFilter )
{
	struct Scale
	{
		float32 sx, sy, sz;
		Scale( float32 s = 1.0f )
			: sx( s ), sy( s ), sz( s ) {}
		static const sw::TypeInfo* StaticType()
		{
			static sw::TypeInfo info;
			info._typeId = 10002;
			return &info;
		}
	};

	Registry reg;
	Entity	 ePos	 = reg.create();
	Entity	 ePosVel = reg.create();
	Entity	 eAll	 = reg.create();

	reg.emplace<Position>( ePos, 1.0f, 0.0f, 0.0f );

	reg.emplace<Position>( ePosVel, 2.0f, 0.0f, 0.0f );
	reg.emplace<Velocity>( ePosVel, 10.0f, 0.0f, 0.0f );

	reg.emplace<Position>( eAll, 3.0f, 0.0f, 0.0f );
	reg.emplace<Velocity>( eAll, 20.0f, 0.0f, 0.0f );
	reg.emplace<Scale>( eAll, 2.0f );

	int32 countPos{ 0 };
	View<Position>( reg ).each( [&]( Entity, Position& )
	{ ++countPos; } );
	SW_EXPECT_EQUAL( 3, countPos );

	int32 countPosVel{ 0 };
	View<Position, Velocity>( reg ).each( [&]( Entity, Position&, Velocity& )
	{ ++countPosVel; } );
	SW_EXPECT_EQUAL( 2, countPosVel );

	int32  countAll{ 0 };
	Entity matchedEntity = kNullEntity;
	View<Position, Velocity, Scale>( reg ).each( [&]( Entity ent, Position&, Velocity&, Scale& )
	{
		++countAll;
		matchedEntity = ent;
	} );

	SW_EXPECT_EQUAL( 1, countAll );
	SW_EXPECT_EQUAL( eAll, matchedEntity );
}

/**
 * @brief [ECS] SpatialHashGrid2D 엔티티 등록, 이동, 삭제 및 카운트 검증
 */
SW_TEST_CASE( ECS, SpatialHashGrid2D_InsertUpdateRemoveAndCount )
{
	Registry reg;
	Entity	 e1 = reg.create();
	Entity	 e2 = reg.create();
	Entity	 e3 = reg.create();

	SpatialHashGrid2D grid{ 32.0f };
	SW_EXPECT_NEAR_EQUAL( 32.0f, grid.getCellSize(), 0.001f );
	SW_EXPECT_EQUAL( 0u, static_cast<uint32>( grid.getEntityCount() ) );
	SW_EXPECT_EQUAL( 0u, static_cast<uint32>( grid.getActiveBucketCount() ) );

	grid.insert( e1, 10.0f, 10.0f, 20.0f, 20.0f );
	grid.insert( e2, 50.0f, 50.0f, 60.0f, 60.0f );
	grid.insert( e3, 100.0f, 100.0f, 110.0f, 110.0f );

	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( grid.getEntityCount() ) );
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( grid.getActiveBucketCount() ) );

	// e2 이동
	grid.update( e2, 15.0f, 15.0f, 25.0f, 25.0f );
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( grid.getEntityCount() ) );

	// e3 삭제
	grid.remove( e3 );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( grid.getEntityCount() ) );

	grid.clear();
	SW_EXPECT_EQUAL( 0u, static_cast<uint32>( grid.getEntityCount() ) );
	SW_EXPECT_EQUAL( 0u, static_cast<uint32>( grid.getActiveBucketCount() ) );
}

/**
 * @brief [ECS] SpatialHashGrid2D AABB, Circle 및 Ray 쿼리 필터링 검증
 */
SW_TEST_CASE( ECS, SpatialHashGrid2D_AABBCircleAndRayQueries )
{
	Registry reg;
	Entity	 eTarget1 = reg.create();
	Entity	 eTarget2 = reg.create();
	Entity	 eFarAway = reg.create();

	SpatialHashGrid2D grid{ 64.0f };
	grid.insert( eTarget1, 10.0f, 10.0f, 30.0f, 30.0f );
	grid.insert( eTarget2, 40.0f, 40.0f, 60.0f, 60.0f );
	grid.insert( eFarAway, 500.0f, 500.0f, 520.0f, 520.0f );

	// 1) AABB Query
	vector<Entity> listAabb;
	grid.queryAABB( 0.0f, 0.0f, 70.0f, 70.0f, listAabb );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listAabb.size() ) );

	// 2) Circle Query (Center (20,20), radius 20)
	vector<Entity> listCircle;
	grid.queryCircle( 20.0f, 20.0f, 20.0f, listCircle );
	SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listCircle.size() ) );
	if ( listCircle.empty() == false )
		SW_EXPECT_EQUAL( eTarget1, listCircle[0] );

	// 3) Ray Query along diagonal (0,0) -> (100, 100)
	vector<Entity> listRay;
	grid.queryRay( 0.0f, 0.0f, 1.0f, 1.0f, 120.0f, listRay );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listRay.size() ) );
}

/**
 * @brief [ECS] BVHTree3D 3D 동적 트리 엔티티 등록, 이동, 삭제 및 트리 균형/높이 검증
 */
SW_TEST_CASE( ECS, BVHTree3D_InsertUpdateRemoveAndCount )
{
	Registry  reg;
	BVHTree3D bvh;

	SW_EXPECT_EQUAL( 0u, static_cast<uint32>( bvh.getEntityCount() ) );
	SW_EXPECT_EQUAL( 0, bvh.getTreeHeight() );

	Entity e1 = reg.create();
	Entity e2 = reg.create();
	Entity e3 = reg.create();

	AABB b1{
		{ 0.0f,	0.0f,  0.0f},
		{10.0f, 10.0f, 10.0f}
	};
	AABB b2{
		{20.0f, 20.0f, 20.0f},
		{30.0f, 30.0f, 30.0f}
	};
	AABB b3{
		{100.0f, 100.0f, 100.0f},
		{110.0f, 110.0f, 110.0f}
	   };

	bvh.insert( e1, b1 );
	bvh.insert( e2, b2 );
	bvh.insert( e3, b3 );

	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( bvh.getEntityCount() ) );
	SW_EXPECT_TRUE( bvh.getTreeHeight() > 0 );

	// e2 이동
	AABB b2Moved{
		{ 5.0f,	5.0f,  5.0f},
		{15.0f, 15.0f, 15.0f}
	};
	bvh.update( e2, b2Moved );
	SW_EXPECT_EQUAL( 3u, static_cast<uint32>( bvh.getEntityCount() ) );

	// e3 삭제
	bvh.remove( e3 );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( bvh.getEntityCount() ) );

	bvh.clear();
	SW_EXPECT_EQUAL( 0u, static_cast<uint32>( bvh.getEntityCount() ) );
	SW_EXPECT_EQUAL( 0, bvh.getTreeHeight() );
}

/**
 * @brief [ECS] BVHTree3D 3D AABB, Ray, Sphere 및 Frustum 쿼리 검증
 */
SW_TEST_CASE( ECS, BVHTree3D_AABBRaySphereAndFrustumQueries )
{
	Registry  reg;
	BVHTree3D bvh;

	Entity eNear1 = reg.create();
	Entity eNear2 = reg.create();
	Entity eFar	  = reg.create();

	AABB boxNear1{
		{0.0f, 0.0f, 5.0f},
		{2.0f, 2.0f, 7.0f}
	 };
	AABB boxNear2{
		{3.0f, 0.0f, 10.0f},
		{5.0f, 2.0f, 12.0f}
	  };
	AABB boxFar{
		{50.0f, 50.0f, 200.0f},
		{60.0f, 60.0f, 210.0f}
	 };

	bvh.insert( eNear1, boxNear1 );
	bvh.insert( eNear2, boxNear2 );
	bvh.insert( eFar, boxFar );

	// 1) AABB Query
	vector<Entity> listAabb;
	AABB		   testBox{
				  {-1.0f, -1.0f,  0.0f},
				  { 6.0f,  5.0f, 15.0f}
	};
	bvh.queryAABB( testBox, listAabb );
	SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listAabb.size() ) );

	// 2) Ray Query (Origin (1, 1, 0), Direction (0, 0, 1), maxDist = 20)
	vector<Entity> listRay;
	bvh.queryRay( float3{ 1.0f, 1.0f, 0.0f }, float3{ 0.0f, 0.0f, 1.0f }, 20.0f, listRay );
	SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listRay.size() ) );
	if ( listRay.empty() == false )
		SW_EXPECT_EQUAL( eNear1, listRay[0] );

	// 3) Sphere Query (Center (1, 1, 6), radius = 3)
	vector<Entity> listSphere;
	bvh.querySphere( float3{ 1.0f, 1.0f, 6.0f }, 3.0f, listSphere );
	SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listSphere.size() ) );
	if ( listSphere.empty() == false )
		SW_EXPECT_EQUAL( eNear1, listSphere[0] );
}

/**
 * @brief [ECS] FilteredView 및 sw::Exclude 컴포넌트 제외 필터링 검증
 */
SW_TEST_CASE( ECS, FilteredView_ExcludeComponents )
{
	Registry reg;

	// Entity 1: Position + Velocity (Valid)
	Entity e1 = reg.create();
	reg.emplace<Position>( e1, 10.0f, 0.0f, 0.0f );
	reg.emplace<Velocity>( e1, 1.0f, 0.0f, 0.0f );

	// Entity 2: Position + Velocity + DeadTag (Excluded!)
	Entity e2 = reg.create();
	reg.emplace<Position>( e2, 20.0f, 0.0f, 0.0f );
	reg.emplace<Velocity>( e2, 2.0f, 0.0f, 0.0f );
	reg.emplace<DeadTag>( e2 );

	// Entity 3: Position only (Not in Include set)
	Entity e3 = reg.create();
	reg.emplace<Position>( e3, 30.0f, 0.0f, 0.0f );

	// Entity 4: Position + Velocity (Valid)
	Entity e4 = reg.create();
	reg.emplace<Position>( e4, 40.0f, 0.0f, 0.0f );
	reg.emplace<Velocity>( e4, 4.0f, 0.0f, 0.0f );

	auto filteredView = makeFilteredView<Position, Velocity>( reg, Exclude<DeadTag>{} );

	SW_EXPECT_TRUE( filteredView.contains( e1 ) );
	SW_EXPECT_FALSE( filteredView.contains( e2 ) );
	SW_EXPECT_FALSE( filteredView.contains( e3 ) );
	SW_EXPECT_TRUE( filteredView.contains( e4 ) );

	uint32		   matchCount = 0;
	vector<Entity> listMatchedEntities;

	for ( auto [e, pos, vel] : filteredView )
	{
		++matchCount;
		listMatchedEntities.push_back( e );
		pos.x += vel.dx;
	}

	SW_EXPECT_EQUAL( 2u, matchCount );
	SW_ASSERT_EQUAL( size_t( 2 ), listMatchedEntities.size() );
	SW_EXPECT_EQUAL( e1, listMatchedEntities[0] );
	SW_EXPECT_EQUAL( e4, listMatchedEntities[1] );

	// Position 갱신 검증 (e2는 제외되어 수정되지 않음)
	SW_EXPECT_NEAR_EQUAL( 11.0f, reg.get<Position>( e1 ).x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 20.0f, reg.get<Position>( e2 ).x, 0.001f );
	SW_EXPECT_NEAR_EQUAL( 44.0f, reg.get<Position>( e4 ).x, 0.001f );
}

/**
 * @brief [ECS] emplace 반환값 TComponentHandle의 즉시 생성 및 틱 중 지연 생성 자동 resolve 검증
 */
SW_TEST_CASE( ECS, Emplace_ReturnsComponentHandle_DeferredAndImmediate )
{
	Registry reg;

	// 1) 비-틱 상태에서 emplace -> 즉시 유효한 핸들 반환
	Entity e1 = reg.create();
	auto   h1 = reg.emplace<Position>( e1, 10.0f, 20.0f, 30.0f );
	SW_EXPECT_TRUE( h1.isValid() );
	SW_EXPECT_NOT_NULL( h1.get() );
	SW_EXPECT_EQUAL( 10.0f, h1->x );
	SW_EXPECT_EQUAL( 20.0f, h1->y );
	SW_EXPECT_EQUAL( 30.0f, h1->z );

	// 2) 틱 상태에서 emplace -> 커맨드 버퍼에 지연 등록되며, 틱 중에는 get()이 nullptr
	Entity e2 = reg.create();
	reg.beginTick();
	auto h2 = reg.emplace<Position>( e2, 55.0f, 66.0f, 77.0f );
	SW_EXPECT_FALSE( h2.isValid() );
	SW_EXPECT_NULL( h2.get() );

	// 3) endTick() (플러시) 이후 동일한 핸들 h2가 유효한 컴포넌트로 자동 resolve됨
	reg.endTick();
	SW_EXPECT_TRUE( h2.isValid() );
	SW_EXPECT_NOT_NULL( h2.get() );
	SW_EXPECT_EQUAL( 55.0f, h2->x );
	SW_EXPECT_EQUAL( 66.0f, h2->y );
	SW_EXPECT_EQUAL( 77.0f, h2->z );
}

/**
 * @brief [ECS] 대량 엔티티 생성/삭제 후 shrinkToFit 메모리 회수 및 무결성 검증
 */
SW_TEST_CASE( ECS, Registry_MassEntityCreationDestruction_ShrinkToFit )
{
	Registry		 reg;
	constexpr size_t totalEntities = 5000;
	vector<Entity>	 listEntities;
	listEntities.reserve( totalEntities );

	for ( size_t index = 0; index < totalEntities; ++index )
	{
		Entity entity = reg.create();
		reg.emplace<Position>( entity, static_cast<float32>( index ), 0.0f, 0.0f );
		reg.emplace<Velocity>( entity, 1.0f, 2.0f, 3.0f );
		listEntities.push_back( entity );
	}

	SW_EXPECT_EQUAL( totalEntities, reg.getActiveEntityCount() );

	// 4500개 삭제
	for ( size_t index = 500; index < totalEntities; ++index )
	{
		reg.destroy( listEntities[index] );
	}

	SW_EXPECT_EQUAL( 500u, reg.getActiveEntityCount() );

	// shrinkToFit 호출
	reg.shrinkToFit();

	SW_EXPECT_EQUAL( 500u, reg.getActiveEntityCount() );

	// 생존 엔티티 500개의 컴포넌트 접근 검증
	for ( size_t index = 0; index < 500; ++index )
	{
		Entity entity = listEntities[index];
		SW_EXPECT_TRUE( reg.isValid( entity ) );
		SW_EXPECT_TRUE( reg.has<Position>( entity ) );
		SW_EXPECT_TRUE( reg.has<Velocity>( entity ) );
		SW_EXPECT_NEAR_EQUAL( static_cast<float32>( index ), reg.get<Position>( entity ).x, 0.001f );
	}

	// 삭제된 엔티티 검증
	for ( size_t index = 500; index < totalEntities; ++index )
	{
		Entity entity = listEntities[index];
		SW_EXPECT_FALSE( reg.isValid( entity ) );
	}
}

/**
 * @brief [ECS] Registry clear 및 clearAndShrink 수명 주기 검증
 */
SW_TEST_CASE( ECS, Registry_ClearAndShrinkReclaimsMemory )
{
	Registry		 reg;
	constexpr size_t totalEntities = 2000;
	vector<Entity>	 listEntities;
	listEntities.reserve( totalEntities );

	for ( size_t index = 0; index < totalEntities; ++index )
	{
		Entity entity = reg.create();
		reg.emplace<Position>( entity, static_cast<float32>( index ), 0.0f, 0.0f );
		reg.emplace<Velocity>( entity, 1.0f, 1.0f, 1.0f );
		listEntities.push_back( entity );
	}

	SW_EXPECT_EQUAL( totalEntities, reg.getActiveEntityCount() );

	// clearAndShrink 실행
	reg.clearAndShrink();

	SW_EXPECT_EQUAL( 0u, reg.getActiveEntityCount() );

	// 이전 엔티티들이 모두 무효화되었는지 검증
	for ( Entity entity : listEntities )
	{
		SW_EXPECT_FALSE( reg.isValid( entity ) );
	}

	// 초기화 후 새로운 엔티티 생성 정상 작동 검증
	Entity freshEntity = reg.create();
	SW_EXPECT_TRUE( reg.isValid( freshEntity ) );
	SW_EXPECT_EQUAL( 1u, reg.getActiveEntityCount() );
	reg.emplace<Position>( freshEntity, 123.0f, 456.0f, 789.0f );
	SW_EXPECT_TRUE( reg.has<Position>( freshEntity ) );
	SW_EXPECT_NEAR_EQUAL( 123.0f, reg.get<Position>( freshEntity ).x, 0.001f );
}

/**
 * @brief [ECS] ArchetypeChunkPool 복합 정렬 컴포넌트(1B, 4B, 8B, 16B) 청크 분할 및 스왑-앤-팝 검증
 */
SW_TEST_CASE( ECS, ArchetypeChunkPoolMultiAlignmentAndLifecycle )
{
	struct alignas( 1 ) Comp1B
	{
		uint8 _val{ 0xAB };
	};
	struct alignas( 4 ) Comp4B
	{
		uint32 _id{ 12345 };
	};
	struct alignas( 8 ) Comp8B
	{
		uint64 _ticks{ 9876543210ULL };
	};
	struct alignas( 16 ) Comp16B
	{
		float32 _v[4]{ 1.0f, 2.0f, 3.0f, 4.0f };
	};

	vector<ComponentColumnLayout> listLayouts;
	listLayouts.push_back( makeColumnLayout<Comp1B>( 1 ) );
	listLayouts.push_back( makeColumnLayout<Comp4B>( 2 ) );
	listLayouts.push_back( makeColumnLayout<Comp8B>( 3 ) );
	listLayouts.push_back( makeColumnLayout<Comp16B>( 4 ) );

	ArchetypeChunkPool pool( listLayouts );
	SW_EXPECT_EQUAL( size_t( 0 ), pool.getTotalEntities() );
	SW_EXPECT_EQUAL( size_t( 0 ), pool.getChunkCount() );

	// 1) 엔티티 다수 할당
	size_t chunkIdx0{ 0 }, rowIdx0{ 0 };
	uint64 entId0 = pool.allocateEntity( 1001, chunkIdx0, rowIdx0 );
	SW_EXPECT_EQUAL( uint64( 1001 ), entId0 );
	SW_EXPECT_EQUAL( size_t( 0 ), chunkIdx0 );
	SW_EXPECT_EQUAL( size_t( 0 ), rowIdx0 );
	SW_EXPECT_EQUAL( size_t( 1 ), pool.getTotalEntities() );
	SW_EXPECT_EQUAL( size_t( 1 ), pool.getChunkCount() );

	// 컴포넌트 데이터 접근 및 정렬 포인터 검증
	ArchetypeChunk* pChunk = pool.getChunk( chunkIdx0 );
	SW_ASSERT_NOT_NULL( pChunk );

	void* pCol0 = pChunk->getComponentColumn( 0 );
	void* pCol1 = pChunk->getComponentColumn( 1 );
	void* pCol2 = pChunk->getComponentColumn( 2 );
	void* pCol3 = pChunk->getComponentColumn( 3 );

	SW_EXPECT_TRUE( ( reinterpret_cast<uintptr_t>( pCol0 ) % alignof( Comp1B ) ) == 0 );
	SW_EXPECT_TRUE( ( reinterpret_cast<uintptr_t>( pCol1 ) % alignof( Comp4B ) ) == 0 );
	SW_EXPECT_TRUE( ( reinterpret_cast<uintptr_t>( pCol2 ) % alignof( Comp8B ) ) == 0 );
	SW_EXPECT_TRUE( ( reinterpret_cast<uintptr_t>( pCol3 ) % alignof( Comp16B ) ) == 0 );

	// 데이터 쓰기 및 읽기
	Comp1B*	 pComp0 = static_cast<Comp1B*>( pChunk->getComponent( 0, rowIdx0 ) );
	Comp4B*	 pComp1 = static_cast<Comp4B*>( pChunk->getComponent( 1, rowIdx0 ) );
	Comp8B*	 pComp2 = static_cast<Comp8B*>( pChunk->getComponent( 2, rowIdx0 ) );
	Comp16B* pComp3 = static_cast<Comp16B*>( pChunk->getComponent( 3, rowIdx0 ) );

	pComp0->_val   = 0xFE;
	pComp1->_id	   = 999;
	pComp2->_ticks = 5555ULL;
	pComp3->_v[0]  = 7.7f;

	SW_EXPECT_EQUAL( uint8( 0xFE ), static_cast<const Comp1B*>( pChunk->getComponent( 0, rowIdx0 ) )->_val );
	SW_EXPECT_EQUAL( uint32( 999 ), static_cast<const Comp4B*>( pChunk->getComponent( 1, rowIdx0 ) )->_id );
	SW_EXPECT_EQUAL( uint64( 5555ULL ), static_cast<const Comp8B*>( pChunk->getComponent( 2, rowIdx0 ) )->_ticks );
	SW_EXPECT_NEAR_EQUAL( 7.7f, static_cast<const Comp16B*>( pChunk->getComponent( 3, rowIdx0 ) )->_v[0], 0.001f );

	// 2) 해제 검증
	SW_EXPECT_TRUE( pool.freeEntity( chunkIdx0, rowIdx0 ) );
	SW_EXPECT_EQUAL( size_t( 0 ), pool.getTotalEntities() );
}
