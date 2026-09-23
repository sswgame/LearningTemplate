#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct AABB;

    /**
     * @brief 연속 충돌 감지(CCD) 스윕 검사 결과입니다.
     */
    struct SweepHit
    {
        bool       _bHit{ false };
        float32    _time{ 1.0f };
        float3     _hitPoint{ 0.0f, 0.0f, 0.0f };
        float3     _hitNormal{ 0.0f, 0.0f, 0.0f };
        uint64     _hitObjectId{ 0 };
        SlotHandle _hitBody{};
    };

    /**
     * @class CCD
     * @brief 빠른 투사체가 벽을 뚫고 지나가지 않게 하는 연속 충돌 감지(CCD) 스윕 알고리즘입니다.
     * @note **두 함수 모두 시작할 때 @p outHit 을 비웁니다.** 그래서 `false` 를 받은 뒤에 남는 것은
     *       늘 빈 결과입니다. 결과 구조체를 재사용해 여러 대상을 훑어도 이전 충돌이 살아남지 않습니다.
     *       예전에는 `sweepSphere` 만 비우고 `sweepAabb` 는 비우지 않아, 형제 둘이 다른 약속을
     *       하고 있었습니다.
     */
    class SW_API CCD
    {
    public:
        /**
         * @brief 움직이는 AABB 와 정적 대상 AABB 사이의 연속 충돌을 검사합니다.
         * @param movingBox 시작 위치의 이동 AABB
         * @param displacement 이동 변위 벡터(속도 * deltaTime)
         * @param targetBox 정적 대상 AABB
         * @param outHit 충돌 시각 t in [0, 1], 접촉 법선과 접촉점. **빗나가면 비워집니다.**
         * @return 충돌하면 true 입니다.
         */
        static bool sweepAabb( const AABB& movingBox, const float3& displacement, const AABB& targetBox, SweepHit& outHit );

        /**
         * @brief 움직이는 구와 정적 대상 AABB 사이의 연속 충돌을 검사합니다.
         * @param startCenter 구의 시작 중심점
         * @param radius 구의 반지름
         * @param displacement 이동 변위 벡터
         * @param targetBox 정적 대상 AABB
         * @param outHit 충돌 시각 t in [0, 1] 과 접촉점. **빗나가면 비워집니다.**
         * @return 충돌하면 true 입니다.
         */
        static bool sweepSphere( const float3& startCenter, float32 radius, const float3& displacement, const AABB& targetBox, SweepHit& outHit );
    };
} // namespace sw
