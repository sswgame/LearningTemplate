#include "pch.h"

#include "Engine/Physics/PhysicsWorld.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
	namespace
	{
		struct PhysicsWorldInternal
		{
			/**
			 * @brief 부동소수점 월드 좌표를 정수 그리드 셀 좌표로 변환합니다.
			 */
			static int32 toCellCoord( float32 val, float32 cellSize )
			{
				return static_cast<int32>( MathUtil::floor( val / cellSize ) );
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	/**
	 * @brief 대상 AABB가 점유하는 모든 3D 그리드 셀에 바디 핸들을 등록합니다.
	 */
	void PhysicsWorld::insertBodyToGrid( BodyHandle handle, const AABB& aabb )
	{
		if ( aabb.isValid() == false )
			return;

		const float32 normMinX = MathUtil::min( aabb._min._x, aabb._max._x );
		const float32 normMaxX = MathUtil::max( aabb._min._x, aabb._max._x );
		const float32 normMinY = MathUtil::min( aabb._min._y, aabb._max._y );
		const float32 normMaxY = MathUtil::max( aabb._min._y, aabb._max._y );
		const float32 normMinZ = MathUtil::min( aabb._min._z, aabb._max._z );
		const float32 normMaxZ = MathUtil::max( aabb._min._z, aabb._max._z );

		const int32 minX = PhysicsWorldInternal::toCellCoord( normMinX, kCellSize );
		const int32 maxX = PhysicsWorldInternal::toCellCoord( normMaxX, kCellSize );
		const int32 minY = PhysicsWorldInternal::toCellCoord( normMinY, kCellSize );
		const int32 maxY = PhysicsWorldInternal::toCellCoord( normMaxY, kCellSize );
		const int32 minZ = PhysicsWorldInternal::toCellCoord( normMinZ, kCellSize );
		const int32 maxZ = PhysicsWorldInternal::toCellCoord( normMaxZ, kCellSize );

		for ( int32 gridZ = minZ; gridZ <= maxZ; ++gridZ )
		{
			for ( int32 gridY = minY; gridY <= maxY; ++gridY )
			{
				for ( int32 gridX = minX; gridX <= maxX; ++gridX )
				{
					_mapGrid[CellCoord{ gridX, gridY, gridZ }].push_back( handle );
				}
			}
		}
	}

	/**
	 * @brief 대상 AABB가 점유하던 그리드 셀들에서 바디 핸들을 안전하게 제거합니다.
	 */
	void PhysicsWorld::removeBodyFromGrid( BodyHandle handle, const AABB& aabb )
	{
		if ( aabb.isValid() == false )
			return;

		const float32 normMinX = MathUtil::min( aabb._min._x, aabb._max._x );
		const float32 normMaxX = MathUtil::max( aabb._min._x, aabb._max._x );
		const float32 normMinY = MathUtil::min( aabb._min._y, aabb._max._y );
		const float32 normMaxY = MathUtil::max( aabb._min._y, aabb._max._y );
		const float32 normMinZ = MathUtil::min( aabb._min._z, aabb._max._z );
		const float32 normMaxZ = MathUtil::max( aabb._min._z, aabb._max._z );

		const int32 minX = PhysicsWorldInternal::toCellCoord( normMinX, kCellSize );
		const int32 maxX = PhysicsWorldInternal::toCellCoord( normMaxX, kCellSize );
		const int32 minY = PhysicsWorldInternal::toCellCoord( normMinY, kCellSize );
		const int32 maxY = PhysicsWorldInternal::toCellCoord( normMaxY, kCellSize );
		const int32 minZ = PhysicsWorldInternal::toCellCoord( normMinZ, kCellSize );
		const int32 maxZ = PhysicsWorldInternal::toCellCoord( normMaxZ, kCellSize );

		for ( int32 gridZ = minZ; gridZ <= maxZ; ++gridZ )
		{
			for ( int32 gridY = minY; gridY <= maxY; ++gridY )
			{
				for ( int32 gridX = minX; gridX <= maxX; ++gridX )
				{
					auto it = _mapGrid.find( CellCoord{ gridX, gridY, gridZ } );
					if ( it != _mapGrid.end() )
					{
						vector<BodyHandle>& listHandle = it->second;
						auto				handleIt   = std::find( listHandle.begin(), listHandle.end(), handle );
						if ( handleIt != listHandle.end() )
						{
							*handleIt = listHandle.back();
							listHandle.pop_back();
						}
						if ( listHandle.empty() )
						{
							_mapGrid.erase( it );
						}
					}
				}
			}
		}
	}

	/**
	 * @brief 새로운 물리 바디를 월드 풀에 등록하고 공간 그리드에 배치합니다.
	 */
	PhysicsWorld::BodyHandle PhysicsWorld::addBody( const AABB& aabb, uint8 layer, uint64 objectId )
	{
		PhysicsBody body{};
		body._aabb	   = aabb;
		body._layer	   = layer;
		body._objectId = objectId;
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		BodyHandle							handle = _bodies.insert( std::move( body ) );
		insertBodyToGrid( handle, aabb );
		return handle;
	}

	/**
	 * @brief 물리 바디를 월드에서 제거하고 공간 그리드에서 매핑을 해제합니다.
	 */
	void PhysicsWorld::removeBody( BodyHandle handle )
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		const PhysicsBody*					pBody = _bodies.get( handle );
		if ( pBody != nullptr )
		{
			removeBodyFromGrid( handle, pBody->_aabb );
		}
		_bodies.erase( handle );
	}

	/**
	 * @brief 바디의 위치/크기(AABB)를 갱신하고 공간 그리드 셀 점유 상태를 재배치합니다.
	 */
	void PhysicsWorld::setAabb( BodyHandle handle, const AABB& aabb )
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		PhysicsBody*						pBody = _bodies.get( handle );
		if ( pBody != nullptr )
		{
			removeBodyFromGrid( handle, pBody->_aabb );
			pBody->_aabb = aabb;
			insertBodyToGrid( handle, aabb );
		}
	}

	/**
	 * @brief 바디 핸들로부터 물리 바디 정보를 스레드 안전하게 복사 조회합니다.
	 */
	bool PhysicsWorld::tryGetBody( BodyHandle handle, PhysicsBody& out ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		const PhysicsBody*					pBody = _bodies.get( handle );
		if ( pBody != nullptr )
		{
			out = *pBody;
			return true;
		}
		return false;
	}

	/**
	 * @brief 물리 시뮬레이션 한 단계를 진행합니다.
	 */
	void PhysicsWorld::step( float32 deltaTime )
	{
		(void)deltaTime;
	}

	/**
	 * @brief 두 바디 간의 레이어 충돌 마스크 및 AABB 교차 여부를 검사합니다.
	 */
	bool PhysicsWorld::overlaps( BodyHandle a, BodyHandle b ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		const PhysicsBody*					pBodyA = _bodies.get( a );
		const PhysicsBody*					pBodyB = _bodies.get( b );
		if ( pBodyA == nullptr || pBodyB == nullptr )
			return false;
		return queryOverlaps( pBodyA->_aabb, pBodyA->_layer, pBodyB->_aabb, pBodyB->_layer, _layers );
	}

	/**
	 * @brief 특정 3D 바운딩 박스(AABB)와 교차하는 모든 물리 바디들을 공간 그리드를 통해 고속 검색합니다.
	 *
	 * 1. 박스가 걸치는 공간 그리드 셀들을 순회하며 중복 없는 후보 바디 목록을 수집.
	 * 2. 후보 바디들에 대해 레이어 마스크 및 정밀 AABB 교차 검사를 수행하여 outHandles에 저장.
	 */
	void PhysicsWorld::queryAabb( const AABB& box, uint8 layer, vector<BodyHandle>& outListHandle ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		outListHandle.clear();

		if ( box.isValid() == false )
			return;

		const int32 minX = PhysicsWorldInternal::toCellCoord( box._min._x, kCellSize );
		const int32 maxX = PhysicsWorldInternal::toCellCoord( box._max._x, kCellSize );
		const int32 minY = PhysicsWorldInternal::toCellCoord( box._min._y, kCellSize );
		const int32 maxY = PhysicsWorldInternal::toCellCoord( box._max._y, kCellSize );
		const int32 minZ = PhysicsWorldInternal::toCellCoord( box._min._z, kCellSize );
		const int32 maxZ = PhysicsWorldInternal::toCellCoord( box._max._z, kCellSize );

		thread_local vector<BodyHandle> t_listCandidateHandle;
		t_listCandidateHandle.clear();
		for ( int32 gridZ = minZ; gridZ <= maxZ; ++gridZ )
		{
			for ( int32 gridY = minY; gridY <= maxY; ++gridY )
			{
				for ( int32 gridX = minX; gridX <= maxX; ++gridX )
				{
					auto it = _mapGrid.find( CellCoord{ gridX, gridY, gridZ } );
					if ( it != _mapGrid.end() )
					{
						t_listCandidateHandle.insert( t_listCandidateHandle.end(), it->second.begin(), it->second.end() );
					}
				}
			}
		}

		std::sort( t_listCandidateHandle.begin(), t_listCandidateHandle.end() );
		t_listCandidateHandle.erase( std::unique( t_listCandidateHandle.begin(), t_listCandidateHandle.end() ), t_listCandidateHandle.end() );

		for ( BodyHandle handle : t_listCandidateHandle )
		{
			const PhysicsBody* pBody = _bodies.get( handle );
			if ( pBody != nullptr )
			{
				if ( queryOverlaps( box, layer, pBody->_aabb, pBody->_layer, _layers ) )
					outListHandle.push_back( handle );
			}
		}

		if ( t_listCandidateHandle.capacity() > 2048 )
			t_listCandidateHandle.shrink_to_fit();
	}

	bool PhysicsWorld::sweepTest( const AABB& movingBox, const float3& displacement, uint8 layer, SweepHit& outHit ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		outHit._bHit = false;
		outHit._time = 1.0f;

		if ( movingBox.isValid() == false )
			return false;

		const AABB sweptBounds{
			float3{
				   MathUtil::min( movingBox._min._x, movingBox._min._x + displacement._x ),
				   MathUtil::min( movingBox._min._y, movingBox._min._y + displacement._y ),
				   MathUtil::min( movingBox._min._z, movingBox._min._z + displacement._z )},
			float3{
				   MathUtil::max( movingBox._max._x, movingBox._max._x + displacement._x ),
				   MathUtil::max( movingBox._max._y, movingBox._max._y + displacement._y ),
				   MathUtil::max( movingBox._max._z, movingBox._max._z + displacement._z )}
		  };

		const int32 minX = PhysicsWorldInternal::toCellCoord( sweptBounds._min._x, kCellSize );
		const int32 maxX = PhysicsWorldInternal::toCellCoord( sweptBounds._max._x, kCellSize );
		const int32 minY = PhysicsWorldInternal::toCellCoord( sweptBounds._min._y, kCellSize );
		const int32 maxY = PhysicsWorldInternal::toCellCoord( sweptBounds._max._y, kCellSize );
		const int32 minZ = PhysicsWorldInternal::toCellCoord( sweptBounds._min._z, kCellSize );
		const int32 maxZ = PhysicsWorldInternal::toCellCoord( sweptBounds._max._z, kCellSize );

		thread_local vector<BodyHandle> t_listCandidateHandles;
		t_listCandidateHandles.clear();
		for ( int32 gridZ = minZ; gridZ <= maxZ; ++gridZ )
		{
			for ( int32 gridY = minY; gridY <= maxY; ++gridY )
			{
				for ( int32 gridX = minX; gridX <= maxX; ++gridX )
				{
					auto it = _mapGrid.find( CellCoord{ gridX, gridY, gridZ } );
					if ( it != _mapGrid.end() )
					{
						t_listCandidateHandles.insert( t_listCandidateHandles.end(), it->second.begin(), it->second.end() );
					}
				}
			}
		}

		std::sort( t_listCandidateHandles.begin(), t_listCandidateHandles.end() );
		t_listCandidateHandles.erase( std::unique( t_listCandidateHandles.begin(), t_listCandidateHandles.end() ), t_listCandidateHandles.end() );

		bool	 bFoundHit = false;
		SweepHit nearestHit{};
		nearestHit._time = 1.0f;

		for ( BodyHandle handle : t_listCandidateHandles )
		{
			const PhysicsBody* pBody = _bodies.get( handle );
			if ( pBody != nullptr && _layers.shouldCollide( layer, pBody->_layer ) )
			{
				SweepHit hit{};
				if ( CCD::sweepAABB( movingBox, displacement, pBody->_aabb, hit ) )
				{
					if ( hit._time < nearestHit._time || bFoundHit == false )
					{
						bFoundHit				= true;
						nearestHit				= hit;
						nearestHit._hitBody		= handle;
						nearestHit._hitObjectId = pBody->_objectId;
					}
				}
			}
		}

		if ( t_listCandidateHandles.capacity() > 2048 )
			t_listCandidateHandles.shrink_to_fit();

		if ( bFoundHit )
		{
			outHit = nearestHit;
			return true;
		}

		return false;
	}
} // namespace sw
