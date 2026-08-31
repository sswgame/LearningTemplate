#include "pch.h"

#include "Engine/Spatial/BVHTree3D.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/Math.h"

namespace sw
{
	BVHTree3D::BVHTree3D()
		: _listNode{}
		, _listFreeNode{}
		, _mapHandleToNode{}
		, _rootIndex{ invalid_index::kInt32 }
	{
	}

	AABB BVHTree3D::combineAABB( const AABB& a, const AABB& b )
	{
		return AABB{
			float3::min( a._min, b._min ),
			float3::max( a._max, b._max ) };
	}

	float32 BVHTree3D::getSurfaceArea( const AABB& box )
	{
		const float3 extents = box._max - box._min;
		return 2.0f * ( extents._x * extents._y + extents._y * extents._z + extents._z * extents._x );
	}

	int32 BVHTree3D::allocateNode()
	{
		if ( _listFreeNode.empty() == false )
		{
			const int32 nodeIndex = _listFreeNode.back();
			_listFreeNode.pop_back();
			_listNode[static_cast<size_t>( nodeIndex )] = BVHNode3D{};
			return nodeIndex;
		}

		const int32 nodeIndex = static_cast<int32>( _listNode.size() );
		_listNode.emplace_back();
		return nodeIndex;
	}

	void BVHTree3D::freeNode( int32 nodeIndex )
	{
		if ( 0 <= nodeIndex && static_cast<size_t>( nodeIndex ) < _listNode.size() )
		{
			_listNode[static_cast<size_t>( nodeIndex )]._parent		= invalid_index::kInt32;
			_listNode[static_cast<size_t>( nodeIndex )]._leftChild	= invalid_index::kInt32;
			_listNode[static_cast<size_t>( nodeIndex )]._rightChild = invalid_index::kInt32;
			_listNode[static_cast<size_t>( nodeIndex )]._handle		= ObjectHandle{};
			_listFreeNode.push_back( nodeIndex );
		}
	}

	int32 BVHTree3D::insert( ObjectHandle handle, const AABB& bounds )
	{
		if ( handle.isValid() == false )
			return invalid_index::kInt32;

		if ( _mapHandleToNode.find( handle ) != _mapHandleToNode.end() )
			remove( handle );

		const int32 leafIndex = allocateNode();
		BVHNode3D&	leaf	  = _listNode[static_cast<size_t>( leafIndex )];
		leaf._bounds		  = bounds;
		leaf._handle		  = handle;
		leaf._height		  = 0;

		insertLeaf( leafIndex );
		_mapHandleToNode[handle] = leafIndex;
		return leafIndex;
	}

	void BVHTree3D::update( ObjectHandle handle, const AABB& bounds )
	{
		insert( handle, bounds );
	}

	void BVHTree3D::remove( ObjectHandle handle )
	{
		auto it = _mapHandleToNode.find( handle );
		if ( it == _mapHandleToNode.end() )
			return;

		const int32 leafIndex = it->second;
		removeLeaf( leafIndex );
		freeNode( leafIndex );
		_mapHandleToNode.erase( it );
	}

	void BVHTree3D::clear()
	{
		_rootIndex = invalid_index::kInt32;
		_listNode.clear();
		_listFreeNode.clear();
		_mapHandleToNode.clear();
	}

	void BVHTree3D::insertLeaf( int32 leafIndex )
	{
		if ( _rootIndex == invalid_index::kInt32 )
		{
			_rootIndex											 = leafIndex;
			_listNode[static_cast<size_t>( _rootIndex )]._parent = invalid_index::kInt32;
			return;
		}

		// Surface Area Heuristic (SAH) to find best sibling
		const AABB leafAABB = _listNode[static_cast<size_t>( leafIndex )]._bounds;
		int32	   index	= _rootIndex;

		while ( _listNode[static_cast<size_t>( index )].isLeaf() == false )
		{
			const int32 leftChild  = _listNode[static_cast<size_t>( index )]._leftChild;
			const int32 rightChild = _listNode[static_cast<size_t>( index )]._rightChild;

			const float32 area		   = getSurfaceArea( _listNode[static_cast<size_t>( index )]._bounds );
			const AABB	  combinedAABB = combineAABB( _listNode[static_cast<size_t>( index )]._bounds, leafAABB );
			const float32 combinedArea = getSurfaceArea( combinedAABB );

			const float32 cost			  = 2.0f * combinedArea;
			const float32 inheritanceCost = 2.0f * ( combinedArea - area );

			// Cost of descending into left child
			float32 costLeft = 0.0f;
			if ( _listNode[static_cast<size_t>( leftChild )].isLeaf() )
			{
				const AABB aabb = combineAABB( _listNode[static_cast<size_t>( leftChild )]._bounds, leafAABB );
				costLeft		= getSurfaceArea( aabb ) + inheritanceCost;
			}
			else
			{
				const AABB	  aabb	  = combineAABB( _listNode[static_cast<size_t>( leftChild )]._bounds, leafAABB );
				const float32 oldArea = getSurfaceArea( _listNode[static_cast<size_t>( leftChild )]._bounds );
				const float32 newArea = getSurfaceArea( aabb );
				costLeft			  = ( newArea - oldArea ) + inheritanceCost;
			}

			// Cost of descending into right child
			float32 costRight = 0.0f;
			if ( _listNode[static_cast<size_t>( rightChild )].isLeaf() )
			{
				const AABB aabb = combineAABB( _listNode[static_cast<size_t>( rightChild )]._bounds, leafAABB );
				costRight		= getSurfaceArea( aabb ) + inheritanceCost;
			}
			else
			{
				const AABB	  aabb	  = combineAABB( _listNode[static_cast<size_t>( rightChild )]._bounds, leafAABB );
				const float32 oldArea = getSurfaceArea( _listNode[static_cast<size_t>( rightChild )]._bounds );
				const float32 newArea = getSurfaceArea( aabb );
				costRight			  = ( newArea - oldArea ) + inheritanceCost;
			}

			if ( cost < costLeft && cost < costRight )
				break;

			index = ( costLeft < costRight ) ? leftChild : rightChild;
		}

		const int32 sibling = index;

		// Create a new parent node
		const int32 oldParent = _listNode[static_cast<size_t>( sibling )]._parent;
		const int32 newParent = allocateNode();

		_listNode[static_cast<size_t>( newParent )]._parent		= oldParent;
		_listNode[static_cast<size_t>( newParent )]._bounds		= combineAABB( leafAABB, _listNode[static_cast<size_t>( sibling )]._bounds );
		_listNode[static_cast<size_t>( newParent )]._height		= _listNode[static_cast<size_t>( sibling )]._height + 1;
		_listNode[static_cast<size_t>( newParent )]._leftChild	= sibling;
		_listNode[static_cast<size_t>( newParent )]._rightChild = leafIndex;

		_listNode[static_cast<size_t>( sibling )]._parent	= newParent;
		_listNode[static_cast<size_t>( leafIndex )]._parent = newParent;

		if ( oldParent != invalid_index::kInt32 )
		{
			if ( _listNode[static_cast<size_t>( oldParent )]._leftChild == sibling )
				_listNode[static_cast<size_t>( oldParent )]._leftChild = newParent;
			else
				_listNode[static_cast<size_t>( oldParent )]._rightChild = newParent;
		}
		else
		{
			_rootIndex = newParent;
		}

		// Walk back up the tree refitting AABBs and balancing
		index = _listNode[static_cast<size_t>( leafIndex )]._parent;
		while ( index != invalid_index::kInt32 )
		{
			index = balance( index );

			const int32 leftChild  = _listNode[static_cast<size_t>( index )]._leftChild;
			const int32 rightChild = _listNode[static_cast<size_t>( index )]._rightChild;

			_listNode[static_cast<size_t>( index )]._height = 1 + MathUtil::max(
																	  _listNode[static_cast<size_t>( leftChild )]._height,
																	  _listNode[static_cast<size_t>( rightChild )]._height );
			_listNode[static_cast<size_t>( index )]._bounds = combineAABB(
				_listNode[static_cast<size_t>( leftChild )]._bounds,
				_listNode[static_cast<size_t>( rightChild )]._bounds );

			index = _listNode[static_cast<size_t>( index )]._parent;
		}
	}

	void BVHTree3D::removeLeaf( int32 leafIndex )
	{
		if ( leafIndex == _rootIndex )
		{
			_rootIndex = invalid_index::kInt32;
			return;
		}

		const int32 parent		= _listNode[static_cast<size_t>( leafIndex )]._parent;
		const int32 grandParent = _listNode[static_cast<size_t>( parent )]._parent;
		const int32 sibling		= ( _listNode[static_cast<size_t>( parent )]._leftChild == leafIndex )
									? _listNode[static_cast<size_t>( parent )]._rightChild
									: _listNode[static_cast<size_t>( parent )]._leftChild;

		if ( grandParent != invalid_index::kInt32 )
		{
			if ( _listNode[static_cast<size_t>( grandParent )]._leftChild == parent )
				_listNode[static_cast<size_t>( grandParent )]._leftChild = sibling;
			else
				_listNode[static_cast<size_t>( grandParent )]._rightChild = sibling;

			_listNode[static_cast<size_t>( sibling )]._parent = grandParent;
			freeNode( parent );

			int32 index = grandParent;
			while ( index != invalid_index::kInt32 )
			{
				index = balance( index );

				const int32 leftChild  = _listNode[static_cast<size_t>( index )]._leftChild;
				const int32 rightChild = _listNode[static_cast<size_t>( index )]._rightChild;

				_listNode[static_cast<size_t>( index )]._bounds = combineAABB(
					_listNode[static_cast<size_t>( leftChild )]._bounds,
					_listNode[static_cast<size_t>( rightChild )]._bounds );
				_listNode[static_cast<size_t>( index )]._height = 1 + MathUtil::max(
																		  _listNode[static_cast<size_t>( leftChild )]._height,
																		  _listNode[static_cast<size_t>( rightChild )]._height );

				index = _listNode[static_cast<size_t>( index )]._parent;
			}
		}
		else
		{
			_rootIndex										  = sibling;
			_listNode[static_cast<size_t>( sibling )]._parent = invalid_index::kInt32;
			freeNode( parent );
		}
	}

	int32 BVHTree3D::balance( int32 nodeIndex )
	{
		BVHNode3D& A = _listNode[static_cast<size_t>( nodeIndex )];
		if ( A.isLeaf() || A._height < 2 )
			return nodeIndex;

		const int32 iB = A._leftChild;
		const int32 iC = A._rightChild;

		BVHNode3D& B = _listNode[static_cast<size_t>( iB )];
		BVHNode3D& C = _listNode[static_cast<size_t>( iC )];

		const int32 balanceFactor = C._height - B._height;

		// Rotate C up
		if ( balanceFactor > 1 )
		{
			const int32 iF = C._leftChild;
			const int32 iG = C._rightChild;
			BVHNode3D&	F  = _listNode[static_cast<size_t>( iF )];
			BVHNode3D&	G  = _listNode[static_cast<size_t>( iG )];

			C._leftChild = nodeIndex;
			C._parent	 = A._parent;
			A._parent	 = iC;

			if ( C._parent != invalid_index::kInt32 )
			{
				if ( _listNode[static_cast<size_t>( C._parent )]._leftChild == nodeIndex )
					_listNode[static_cast<size_t>( C._parent )]._leftChild = iC;
				else
					_listNode[static_cast<size_t>( C._parent )]._rightChild = iC;
			}
			else
			{
				_rootIndex = iC;
			}

			if ( F._height > G._height )
			{
				C._rightChild = iF;
				A._rightChild = iG;
				G._parent	  = nodeIndex;
				A._bounds	  = combineAABB( B._bounds, G._bounds );
				C._bounds	  = combineAABB( A._bounds, F._bounds );

				A._height = 1 + MathUtil::max( B._height, G._height );
				C._height = 1 + MathUtil::max( A._height, F._height );
			}
			else
			{
				C._rightChild = iG;
				A._rightChild = iF;
				F._parent	  = nodeIndex;
				A._bounds	  = combineAABB( B._bounds, F._bounds );
				C._bounds	  = combineAABB( A._bounds, G._bounds );

				A._height = 1 + MathUtil::max( B._height, F._height );
				C._height = 1 + MathUtil::max( A._height, G._height );
			}

			return iC;
		}

		// Rotate B up
		if ( balanceFactor < -1 )
		{
			const int32 iD = B._leftChild;
			const int32 iE = B._rightChild;
			BVHNode3D&	D  = _listNode[static_cast<size_t>( iD )];
			BVHNode3D&	E  = _listNode[static_cast<size_t>( iE )];

			B._leftChild = nodeIndex;
			B._parent	 = A._parent;
			A._parent	 = iB;

			if ( B._parent != invalid_index::kInt32 )
			{
				if ( _listNode[static_cast<size_t>( B._parent )]._leftChild == nodeIndex )
					_listNode[static_cast<size_t>( B._parent )]._leftChild = iB;
				else
					_listNode[static_cast<size_t>( B._parent )]._rightChild = iB;
			}
			else
			{
				_rootIndex = iB;
			}

			if ( D._height > E._height )
			{
				B._rightChild = iD;
				A._leftChild  = iE;
				E._parent	  = nodeIndex;
				A._bounds	  = combineAABB( C._bounds, E._bounds );
				B._bounds	  = combineAABB( A._bounds, D._bounds );

				A._height = 1 + MathUtil::max( C._height, E._height );
				B._height = 1 + MathUtil::max( A._height, D._height );
			}
			else
			{
				B._rightChild = iE;
				A._leftChild  = iD;
				D._parent	  = nodeIndex;
				A._bounds	  = combineAABB( C._bounds, D._bounds );
				B._bounds	  = combineAABB( A._bounds, E._bounds );

				A._height = 1 + MathUtil::max( C._height, D._height );
				B._height = 1 + MathUtil::max( A._height, E._height );
			}

			return iB;
		}

		return nodeIndex;
	}

	void BVHTree3D::queryAABB( const AABB& queryBox, vector<ObjectHandle>& outListHandle ) const
	{
		if ( _rootIndex == invalid_index::kInt32 )
			return;

		int32 arrStack[constant::kMaxBuffer256];
		int32 stackCount	   = 0;
		arrStack[stackCount++] = _rootIndex;

		while ( stackCount > 0 )
		{
			const int32		 nodeIndex = arrStack[--stackCount];
			const BVHNode3D& node	   = _listNode[static_cast<size_t>( nodeIndex )];

			if ( node._bounds.intersects( queryBox ) )
			{
				if ( node.isLeaf() )
				{
					outListHandle.push_back( node._handle );
				}
				else
				{
					if ( node._leftChild != invalid_index::kInt32 && stackCount < static_cast<int32>( constant::kMaxBuffer256 - 1 ) )
						arrStack[stackCount++] = node._leftChild;
					if ( node._rightChild != invalid_index::kInt32 && stackCount < static_cast<int32>( constant::kMaxBuffer256 - 1 ) )
						arrStack[stackCount++] = node._rightChild;
				}
			}
		}
	}

	void BVHTree3D::queryRay( const float3& origin, const float3& direction, float32 maxDist, vector<ObjectHandle>& outListHandle ) const
	{
		if ( _rootIndex == invalid_index::kInt32 || maxDist <= 0.0f )
			return;

		const float3 invDir{
			MathUtil::abs( direction._x ) > MathUtil::Epsilon ? ( 1.0f / direction._x ) : ( 1.0f / MathUtil::Epsilon ),
			MathUtil::abs( direction._y ) > MathUtil::Epsilon ? ( 1.0f / direction._y ) : ( 1.0f / MathUtil::Epsilon ),
			MathUtil::abs( direction._z ) > MathUtil::Epsilon ? ( 1.0f / direction._z ) : ( 1.0f / MathUtil::Epsilon ) };

		auto rayIntersects = [&]( const AABB& box ) -> bool
		{
			float32 t1	 = ( box._min._x - origin._x ) * invDir._x;
			float32 t2	 = ( box._max._x - origin._x ) * invDir._x;
			float32 tMin = MathUtil::min( t1, t2 );
			float32 tMax = MathUtil::max( t1, t2 );

			t1	 = ( box._min._y - origin._y ) * invDir._y;
			t2	 = ( box._max._y - origin._y ) * invDir._y;
			tMin = MathUtil::max( tMin, MathUtil::min( t1, t2 ) );
			tMax = MathUtil::min( tMax, MathUtil::max( t1, t2 ) );

			t1	 = ( box._min._z - origin._z ) * invDir._z;
			t2	 = ( box._max._z - origin._z ) * invDir._z;
			tMin = MathUtil::max( tMin, MathUtil::min( t1, t2 ) );
			tMax = MathUtil::min( tMax, MathUtil::max( t1, t2 ) );

			return tMax >= MathUtil::max( 0.0f, tMin ) && tMin <= maxDist;
		};

		int32 arrStack[constant::kMaxBuffer256];
		int32 stackCount	   = 0;
		arrStack[stackCount++] = _rootIndex;

		while ( stackCount > 0 )
		{
			const int32		 nodeIndex = arrStack[--stackCount];
			const BVHNode3D& node	   = _listNode[static_cast<size_t>( nodeIndex )];

			if ( rayIntersects( node._bounds ) )
			{
				if ( node.isLeaf() )
				{
					outListHandle.push_back( node._handle );
				}
				else
				{
					if ( node._leftChild != invalid_index::kInt32 && stackCount < static_cast<int32>( constant::kMaxBuffer256 - 1 ) )
						arrStack[stackCount++] = node._leftChild;
					if ( node._rightChild != invalid_index::kInt32 && stackCount < static_cast<int32>( constant::kMaxBuffer256 - 1 ) )
						arrStack[stackCount++] = node._rightChild;
				}
			}
		}
	}

	void BVHTree3D::querySphere( const float3& center, float32 radius, vector<ObjectHandle>& outListHandle ) const
	{
		if ( _rootIndex == invalid_index::kInt32 || radius <= 0.0f )
			return;

		const float32 r2			   = radius * radius;
		auto		  sphereIntersects = [&]( const AABB& box ) -> bool
		{
			const float3 closestPoint = center.clamped( box._min, box._max );
			return float3::getDistanceSquared( center, closestPoint ) <= r2;
		};

		int32 arrStack[constant::kMaxBuffer256];
		int32 stackCount	   = 0;
		arrStack[stackCount++] = _rootIndex;

		while ( stackCount > 0 )
		{
			const int32		 nodeIndex = arrStack[--stackCount];
			const BVHNode3D& node	   = _listNode[static_cast<size_t>( nodeIndex )];

			if ( sphereIntersects( node._bounds ) )
			{
				if ( node.isLeaf() )
				{
					outListHandle.push_back( node._handle );
				}
				else
				{
					if ( node._leftChild != invalid_index::kInt32 && stackCount < static_cast<int32>( constant::kMaxBuffer256 - 1 ) )
						arrStack[stackCount++] = node._leftChild;
					if ( node._rightChild != invalid_index::kInt32 && stackCount < static_cast<int32>( constant::kMaxBuffer256 - 1 ) )
						arrStack[stackCount++] = node._rightChild;
				}
			}
		}
	}

	void BVHTree3D::queryFrustum( const float4x4& viewProj, vector<ObjectHandle>& outListHandle ) const
	{
		const float32* pArr = &viewProj._11;
		// Extract 6 frustum planes from column-major viewProj matrix
		// Left, Right, Bottom, Top, Near, Far
		float4 arrPlane[6] = {
			float4{pArr[3] + pArr[0], pArr[7] + pArr[4],  pArr[11] + pArr[8], pArr[15] + pArr[12]},
			float4{pArr[3] - pArr[0], pArr[7] - pArr[4],  pArr[11] - pArr[8], pArr[15] - pArr[12]},
			float4{pArr[3] + pArr[1], pArr[7] + pArr[5],  pArr[11] + pArr[9], pArr[15] + pArr[13]},
			float4{pArr[3] - pArr[1], pArr[7] - pArr[5],  pArr[11] - pArr[9], pArr[15] - pArr[13]},
			float4{			pArr[2],			 pArr[6],			  pArr[10],			pArr[14]},
			float4{pArr[3] - pArr[2], pArr[7] - pArr[6], pArr[11] - pArr[10], pArr[15] - pArr[14]}
		 };

		for ( int32 planeIndex = 0; planeIndex < 6; ++planeIndex )
		{
			const float3  normal{ arrPlane[planeIndex]._x, arrPlane[planeIndex]._y, arrPlane[planeIndex]._z };
			const float32 length = normal.getLength();
			if ( length > MathUtil::Epsilon )
			{
				const float32 invLength = 1.0f / length;
				arrPlane[planeIndex]._x *= invLength;
				arrPlane[planeIndex]._y *= invLength;
				arrPlane[planeIndex]._z *= invLength;
				arrPlane[planeIndex]._w *= invLength;
			}
		}

		auto frustumIntersects = [&]( const AABB& box ) -> bool
		{
			for ( int32 planeIndex = 0; planeIndex < 6; ++planeIndex )
			{
				const float4& plane = arrPlane[planeIndex];
				const float3  p{
					 plane._x > 0.0f ? box._max._x : box._min._x,
					plane._y > 0.0f ? box._max._y : box._min._y,
					plane._z > 0.0f ? box._max._z : box._min._z };

				if ( ( float3{ plane._x, plane._y, plane._z }.dot( p ) + plane._w ) < 0.0f )
					return false;
			}
			return true;
		};

		if ( _rootIndex == invalid_index::kInt32 )
			return;

		int32 arrStack[constant::kMaxBuffer256];
		int32 stackCount	   = 0;
		arrStack[stackCount++] = _rootIndex;

		while ( stackCount > 0 )
		{
			const int32		 nodeIndex = arrStack[--stackCount];
			const BVHNode3D& node	   = _listNode[static_cast<size_t>( nodeIndex )];

			if ( frustumIntersects( node._bounds ) )
			{
				if ( node.isLeaf() )
				{
					outListHandle.push_back( node._handle );
				}
				else
				{
					if ( node._leftChild != invalid_index::kInt32 && stackCount < static_cast<int32>( constant::kMaxBuffer256 - 1 ) )
						arrStack[stackCount++] = node._leftChild;
					if ( node._rightChild != invalid_index::kInt32 && stackCount < static_cast<int32>( constant::kMaxBuffer256 - 1 ) )
						arrStack[stackCount++] = node._rightChild;
				}
			}
		}
	}

	size_t BVHTree3D::getHandleCount() const
	{
		return _mapHandleToNode.size();
	}

	size_t BVHTree3D::getNodeCount() const
	{
		return _listNode.size() - _listFreeNode.size();
	}

	int32 BVHTree3D::getTreeHeight() const
	{
		if ( _rootIndex == invalid_index::kInt32 )
			return 0;
		return _listNode[static_cast<size_t>( _rootIndex )]._height;
	}
} // namespace sw
