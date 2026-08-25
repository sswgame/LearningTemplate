#pragma once
#include "Engine/ECS/Entity.h"
#include "Engine/Physics/AABB.h"

#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ObjectHandle.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
	/**
	 * @brief 연속 충돌 감지(CCD) 스윕 테스트 결과
	 */
	struct SweepHit
	{
		bool		 _bHit{ false };
		float32		 _time{ 1.0f };
		float3		 _hitPoint{ 0.0f, 0.0f, 0.0f };
		float3		 _hitNormal{ 0.0f, 0.0f, 0.0f };
		sw::Entity	 _hitEntity{ sw::kNullEntity };
		ObjectHandle _hitBody{};
	};

	/**
	 * @brief 고속 투사체 터널링 방지를 위한 연속 충돌 감지(CCD) 스윕 알고리즘
	 */
	class SW_API CCD
	{
	public:
		/**
		 * @brief 이동하는 AABB와 정적 대상 AABB 간의 연속 충돌 검사
		 * @param movingBox 시작 위치의 이동 AABB
		 * @param displacement 이동 변위 벡터 (속도 * deltaTime)
		 * @param targetBox 정적 대상 AABB
		 * @param outHit 충돌 시각 t in [0, 1], 접촉 법선 및 접촉점 결과
		 * @return 충돌 발생 시 true
		 */
		static bool sweepAABB( const AABB& movingBox, const float3& displacement, const AABB& targetBox, SweepHit& outHit );

		/**
		 * @brief 이동하는 구(Sphere)와 정적 대상 AABB 간의 연속 충돌 검사
		 * @param startCenter 구의 시작 중심점
		 * @param radius 구의 반지름
		 * @param displacement 이동 변위 벡터
		 * @param targetBox 정적 대상 AABB
		 * @param outHit 충돌 시각 t in [0, 1] 및 접촉점 결과
		 * @return 충돌 발생 시 true
		 */
		static bool sweepSphere( const float3& startCenter, float32 radius, const float3& displacement, const AABB& targetBox, SweepHit& outHit );
	};
} // namespace sw
