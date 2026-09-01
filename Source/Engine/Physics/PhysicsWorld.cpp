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

		const float3 normMin = float3::min( aabb._min, aabb._max );
		const float3 normMax = float3::max( aabb._min, aabb._max );

		const int32 minX = PhysicsWorldInternal::toCellCoord( normMin._x, kCellSize );
		const int32 maxX = PhysicsWorldInternal::toCellCoord( normMax._x, kCellSize );
		const int32 minY = PhysicsWorldInternal::toCellCoord( normMin._y, kCellSize );
		const int32 maxY = PhysicsWorldInternal::toCellCoord( normMax._y, kCellSize );
		const int32 minZ = PhysicsWorldInternal::toCellCoord( normMin._z, kCellSize );
		const int32 maxZ = PhysicsWorldInternal::toCellCoord( normMax._z, kCellSize );

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

		const float3 normMin = float3::min( aabb._min, aabb._max );
		const float3 normMax = float3::max( aabb._min, aabb._max );

		const int32 minX = PhysicsWorldInternal::toCellCoord( normMin._x, kCellSize );
		const int32 maxX = PhysicsWorldInternal::toCellCoord( normMax._x, kCellSize );
		const int32 minY = PhysicsWorldInternal::toCellCoord( normMin._y, kCellSize );
		const int32 maxY = PhysicsWorldInternal::toCellCoord( normMax._y, kCellSize );
		const int32 minZ = PhysicsWorldInternal::toCellCoord( normMin._z, kCellSize );
		const int32 maxZ = PhysicsWorldInternal::toCellCoord( normMax._z, kCellSize );

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
							if ( listHandle.empty() )
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
		if ( pBody == nullptr )
			return;

		const AABB oldAABB = pBody->_aabb;
		if ( oldAABB.isValid() && aabb.isValid() )
		{
			const float3 oldNormMin = float3::min( oldAABB._min, oldAABB._max );
			const float3 oldNormMax = float3::max( oldAABB._min, oldAABB._max );
			const float3 newNormMin = float3::min( aabb._min, aabb._max );
			const float3 newNormMax = float3::max( aabb._min, aabb._max );

			const int32 oldMinX = PhysicsWorldInternal::toCellCoord( oldNormMin._x, kCellSize );
			const int32 oldMaxX = PhysicsWorldInternal::toCellCoord( oldNormMax._x, kCellSize );
			const int32 oldMinY = PhysicsWorldInternal::toCellCoord( oldNormMin._y, kCellSize );
			const int32 oldMaxY = PhysicsWorldInternal::toCellCoord( oldNormMax._y, kCellSize );
			const int32 oldMinZ = PhysicsWorldInternal::toCellCoord( oldNormMin._z, kCellSize );
			const int32 oldMaxZ = PhysicsWorldInternal::toCellCoord( oldNormMax._z, kCellSize );

			const int32 newMinX = PhysicsWorldInternal::toCellCoord( newNormMin._x, kCellSize );
			const int32 newMaxX = PhysicsWorldInternal::toCellCoord( newNormMax._x, kCellSize );
			const int32 newMinY = PhysicsWorldInternal::toCellCoord( newNormMin._y, kCellSize );
			const int32 newMaxY = PhysicsWorldInternal::toCellCoord( newNormMax._y, kCellSize );
			const int32 newMinZ = PhysicsWorldInternal::toCellCoord( newNormMin._z, kCellSize );
			const int32 newMaxZ = PhysicsWorldInternal::toCellCoord( newNormMax._z, kCellSize );

			const bool bSameCells = ( oldMinX == newMinX && oldMaxX == newMaxX &&
									  oldMinY == newMinY && oldMaxY == newMaxY &&
									  oldMinZ == newMinZ && oldMaxZ == newMaxZ );
			if ( bSameCells )
			{
				pBody->_aabb = aabb;
				return;
			}
		}

		removeBodyFromGrid( handle, pBody->_aabb );
		pBody->_aabb = aabb;
		insertBodyToGrid( handle, aabb );
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

		const float3 normMin = float3::min( box._min, box._max );
		const float3 normMax = float3::max( box._min, box._max );

		const int32 minX = PhysicsWorldInternal::toCellCoord( normMin._x, kCellSize );
		const int32 maxX = PhysicsWorldInternal::toCellCoord( normMax._x, kCellSize );
		const int32 minY = PhysicsWorldInternal::toCellCoord( normMin._y, kCellSize );
		const int32 maxY = PhysicsWorldInternal::toCellCoord( normMax._y, kCellSize );
		const int32 minZ = PhysicsWorldInternal::toCellCoord( normMin._z, kCellSize );
		const int32 maxZ = PhysicsWorldInternal::toCellCoord( normMax._z, kCellSize );

		const int64 spanX	   = static_cast<int64>( maxX ) - static_cast<int64>( minX ) + 1;
		const int64 spanY	   = static_cast<int64>( maxY ) - static_cast<int64>( minY ) + 1;
		const int64 spanZ	   = static_cast<int64>( maxZ ) - static_cast<int64>( minZ ) + 1;
		const int64 totalCells = ( spanX > 0 && spanY > 0 && spanZ > 0 ) ? ( spanX * spanY * spanZ ) : 0;

		if ( totalCells <= 0 || totalCells > 1024 || totalCells > static_cast<int64>( _bodies.size() ) )
		{
			_bodies.forEachHandle( [&]( ObjectHandle handle, const PhysicsBody& body )
			{
				if ( queryOverlaps( box, layer, body._aabb, body._layer, _layers ) )
					outListHandle.push_back( handle );
			} );
			return;
		}

		vector<BodyHandle> listCandidateHandle;
		listCandidateHandle.reserve( 64 );
		for ( int32 gridZ = minZ; gridZ <= maxZ; ++gridZ )
		{
			for ( int32 gridY = minY; gridY <= maxY; ++gridY )
			{
				for ( int32 gridX = minX; gridX <= maxX; ++gridX )
				{
					auto it = _mapGrid.find( CellCoord{ gridX, gridY, gridZ } );
					if ( it != _mapGrid.end() )
					{
						listCandidateHandle.insert( listCandidateHandle.end(), it->second.begin(), it->second.end() );
					}
				}
			}
		}

		std::sort( listCandidateHandle.begin(), listCandidateHandle.end() );
		listCandidateHandle.erase( std::unique( listCandidateHandle.begin(), listCandidateHandle.end() ), listCandidateHandle.end() );

		for ( BodyHandle handle : listCandidateHandle )
		{
			const PhysicsBody* pBody = _bodies.get( handle );
			if ( pBody != nullptr )
			{
				if ( queryOverlaps( box, layer, pBody->_aabb, pBody->_layer, _layers ) )
					outListHandle.push_back( handle );
			}
		}
	}

	bool PhysicsWorld::sweepTest( const AABB& movingBox, const float3& displacement, uint8 layer, SweepHit& outHit ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		outHit._bHit = false;
		outHit._time = 1.0f;

		if ( movingBox.isValid() == false )
			return false;

		const AABB sweptBounds{
			float3::min( movingBox._min, movingBox._min + displacement ),
			float3::max( movingBox._max, movingBox._max + displacement ) };

		const float3 normMin = float3::min( sweptBounds._min, sweptBounds._max );
		const float3 normMax = float3::max( sweptBounds._min, sweptBounds._max );

		const int32 minX = PhysicsWorldInternal::toCellCoord( normMin._x, kCellSize );
		const int32 maxX = PhysicsWorldInternal::toCellCoord( normMax._x, kCellSize );
		const int32 minY = PhysicsWorldInternal::toCellCoord( normMin._y, kCellSize );
		const int32 maxY = PhysicsWorldInternal::toCellCoord( normMax._y, kCellSize );
		const int32 minZ = PhysicsWorldInternal::toCellCoord( normMin._z, kCellSize );
		const int32 maxZ = PhysicsWorldInternal::toCellCoord( normMax._z, kCellSize );

		const int64 spanX	   = static_cast<int64>( maxX ) - static_cast<int64>( minX ) + 1;
		const int64 spanY	   = static_cast<int64>( maxY ) - static_cast<int64>( minY ) + 1;
		const int64 spanZ	   = static_cast<int64>( maxZ ) - static_cast<int64>( minZ ) + 1;
		const int64 totalCells = ( spanX > 0 && spanY > 0 && spanZ > 0 ) ? ( spanX * spanY * spanZ ) : 0;

		bool	 bFoundHit = false;
		SweepHit nearestHit{};
		nearestHit._time = 1.0f;

		if ( totalCells <= 0 || totalCells > 1024 || totalCells > static_cast<int64>( _bodies.size() ) )
		{
			_bodies.forEachHandle( [&]( ObjectHandle handle, const PhysicsBody& body )
			{
				if ( _layers.shouldCollide( layer, body._layer ) )
				{
					SweepHit hit{};
					if ( CCD::sweepAABB( movingBox, displacement, body._aabb, hit ) )
					{
						if ( hit._time < nearestHit._time || bFoundHit == false )
						{
							bFoundHit				= true;
							nearestHit				= hit;
							nearestHit._hitBody		= handle;
							nearestHit._hitObjectId = body._objectId;
						}
					}
				}
			} );

			if ( bFoundHit )
			{
				outHit = nearestHit;
				return true;
			}
			return false;
		}

		vector<BodyHandle> listCandidateHandle;
		listCandidateHandle.reserve( 64 );
		for ( int32 gridZ = minZ; gridZ <= maxZ; ++gridZ )
		{
			for ( int32 gridY = minY; gridY <= maxY; ++gridY )
			{
				for ( int32 gridX = minX; gridX <= maxX; ++gridX )
				{
					auto it = _mapGrid.find( CellCoord{ gridX, gridY, gridZ } );
					if ( it != _mapGrid.end() )
					{
						listCandidateHandle.insert( listCandidateHandle.end(), it->second.begin(), it->second.end() );
					}
				}
			}
		}

		std::sort( listCandidateHandle.begin(), listCandidateHandle.end() );
		listCandidateHandle.erase( std::unique( listCandidateHandle.begin(), listCandidateHandle.end() ), listCandidateHandle.end() );

		for ( BodyHandle handle : listCandidateHandle )
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

		if ( bFoundHit )
		{
			outHit = nearestHit;
			return true;
		}

		return false;
	}
} // namespace sw
