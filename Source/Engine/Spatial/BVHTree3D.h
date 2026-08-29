#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ObjectHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Physics/AABB.h"

namespace sw
{
	/**
	 * @brief 3D BVH 트리의 내부/리프 노드
	 */
	struct BVHNode3D
	{
		AABB		 _bounds{};
		ObjectHandle _handle{};
		int32		 _parent{ -1 };
		int32		 _leftChild{ -1 };
		int32		 _rightChild{ -1 };
		int32		 _height{ 0 };

		bool isLeaf() const { return _leftChild == -1; }
	};

	/**
	 * @brief 3D 씬 가속을 위한 고성능 동적 Bounding Volume Hierarchy (BVH) 트리
	 */
	class SW_API BVHTree3D
	{
	public:
		BVHTree3D();
		~BVHTree3D()								 = default;
		BVHTree3D( const BVHTree3D& )				 = default;
		BVHTree3D& operator=( const BVHTree3D& )	 = default;
		BVHTree3D( BVHTree3D&& ) noexcept			 = default;
		BVHTree3D& operator=( BVHTree3D&& ) noexcept = default;

		int32 insert( ObjectHandle handle, const AABB& bounds );
		void  update( ObjectHandle handle, const AABB& bounds );
		void  remove( ObjectHandle handle );
		void  clear();

		void queryAABB( const AABB& queryBox, vector<ObjectHandle>& outHandles ) const;
		void queryRay( const float3& origin, const float3& direction, float32 maxDist, vector<ObjectHandle>& outHandles ) const;
		void querySphere( const float3& center, float32 radius, vector<ObjectHandle>& outHandles ) const;
		void queryFrustum( const float32 viewProj[16], vector<ObjectHandle>& outHandles ) const;

		size_t getHandleCount() const;
		size_t getNodeCount() const;
		int32  getTreeHeight() const;

	private:
		int32 allocateNode();
		void  freeNode( int32 nodeIndex );
		void  insertLeaf( int32 leafIndex );
		void  removeLeaf( int32 leafIndex );
		int32 balance( int32 nodeIndex );

		static AABB	   combineAABB( const AABB& a, const AABB& b );
		static float32 getSurfaceArea( const AABB& box );

		vector<BVHNode3D>				   _listNode;
		vector<int32>					   _listFreeNode;
		unordered_map<ObjectHandle, int32> _mapHandleToNode;
		int32							   _rootIndex;
	};
} // namespace sw
