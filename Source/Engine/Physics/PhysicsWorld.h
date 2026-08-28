#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/HandleTable.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Physics/AABB.h"
#include "Engine/Physics/CCD.h"
#include "Engine/Physics/CollisionLayers.h"

#include <shared_mutex>

namespace sw
{
	/** @brief PhysicsWorld에 등록된 AABB 바디입니다. */
	struct PhysicsBody
	{
		AABB   _aabb{};
		uint64 _objectId{ 0 };
		uint8  _layer{ 0 };
	};

	/**
	 * @class PhysicsWorld
	 * @brief 겹침 질의와 레이어 필터. step()은 적분하지 않습니다.
	 */
	class SW_API PhysicsWorld
	{
	public:
		using BodyHandle = ObjectHandle;

		/** @brief 기본 레이어 행렬로 빈 월드를 만듭니다. */
		PhysicsWorld() = default;

		/** @brief AABB 바디를 등록합니다. */
		BodyHandle addBody( const AABB& aabb, uint8 layer, uint64 objectId = 0 );
		/** @brief 바디를 제거합니다. */
		void removeBody( BodyHandle handle );
		/** @brief 바디 AABB를 갱신합니다. */
		void setAabb( BodyHandle handle, const AABB& aabb );
		/** @brief 핸들이 유효하면 out에 복사하고 true. */
		bool tryGetBody( BodyHandle handle, PhysicsBody& out ) const;
		/** @brief 솔버 자리. 현재는 질의를 바꾸지 않습니다. */
		void step( float32 deltaTime );

		/** @brief 두 바디가 레이어와 AABB 모두에서 겹치면 true. */
		bool overlaps( BodyHandle a, BodyHandle b ) const;
		/** @brief box와 겹치는 바디 핸들을 out에 넣습니다. */
		void queryAabb( const AABB& box, uint8 layer, vector<BodyHandle>& outHandles ) const;
		/** @brief movingBox가 displacement만큼 이동할 때 layer의 대상들과 연속 충돌(CCD) 검사를 수행합니다. */
		bool sweepTest( const AABB& movingBox, const float3& displacement, uint8 layer, SweepHit& outHit ) const;

		/** @brief 레이어 필터를 반환합니다. */
		CollisionLayers& layers() { return _layers; }
		/** @brief 레이어 필터를 반환합니다. */
		const CollisionLayers& layers() const { return _layers; }

	private:
		struct CellCoord
		{
			int32 _x{ 0 };
			int32 _y{ 0 };
			int32 _z{ 0 };

			bool operator==( const CellCoord& other ) const noexcept
			{
				return _x == other._x && _y == other._y && _z == other._z;
			}
		};

		struct CellCoordHash
		{
			size_t operator()( const CellCoord& coord ) const noexcept
			{
				size_t hash = std::hash<int32>{}( coord._x );
				hash ^= std::hash<int32>{}( coord._y ) + 0x9e3779b9 + ( hash << 6 ) + ( hash >> 2 );
				hash ^= std::hash<int32>{}( coord._z ) + 0x9e3779b9 + ( hash << 6 ) + ( hash >> 2 );
				return hash;
			}
		};

		static constexpr float32 kCellSize = 64.0f;

	private:
		void insertBodyToGrid( BodyHandle handle, const AABB& aabb );
		void removeBodyFromGrid( BodyHandle handle, const AABB& aabb );

	private:
		mutable std::shared_mutex									_mutex;
		HandleTable<PhysicsBody>									_bodies;
		CollisionLayers												_layers;
		unordered_map<CellCoord, vector<BodyHandle>, CellCoordHash> _mapGrid;
	};
} // namespace sw
