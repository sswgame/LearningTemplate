#pragma once
#include "Engine/ECS/Entity.h"
#include "Engine/Physics/AABB.h"

#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

namespace sw
{
	/**
	 * @brief 3D BVH 트리의 내부/리프 노드
	 */
	struct BVHNode3D
	{
		AABB   _bounds{};
		Entity _entity{ kNullEntity };
		int32  _parent{ -1 };
		int32  _leftChild{ -1 };
		int32  _rightChild{ -1 };
		int32  _height{ 0 };

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

		int32 insert( Entity entity, const AABB& bounds );
		void  update( Entity entity, const AABB& bounds );
		void  remove( Entity entity );
		void  clear();

		void queryAABB( const AABB& queryBox, vector<Entity>& outEntities ) const;
		void queryRay( const float3& origin, const float3& direction, float32 maxDist, vector<Entity>& outEntities ) const;
		void querySphere( const float3& center, float32 radius, vector<Entity>& outEntities ) const;
		void queryFrustum( const float32 viewProj[16], vector<Entity>& outEntities ) const;

		size_t getEntityCount() const;
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

		int32						 _rootIndex;
		vector<BVHNode3D>			 _listNodes;
		vector<int32>				 _listFreeNodes;
		unordered_map<Entity, int32> _mapEntityToNode;
	};
} // namespace sw
