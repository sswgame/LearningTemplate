/**
 * @file ShooterMath.h
 * @brief 슈터의 수학 — 광선(히트스캔) 판정, 결정적 난수, 탄 퍼짐, 1인칭 시점(요 · 피치)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 광선 하나 — 시작점과 단위 방향입니다. */
    struct ShooterRay
    {
        float3 _origin{};
        float3 _direction{ 0.0f, 0.0f, 1.0f };
    };

    /**
     * @class ShooterRandom
     * @brief 씨앗이 같으면 같은 수열을 내는 난수(xorshift32)입니다 — 탄 퍼짐을 시험 · 리플레이에서 되풀이할 수 있게 합니다.
     */
    class SW_GF_API ShooterRandom
    {
    public:
        explicit ShooterRandom( uint32 seed = 0x9E3779B9u );

        void   setSeed( uint32 seed );
        uint32 nextUint();
        /** @brief [0, 1) 의 실수입니다. */
        float32 nextFloat();
        /** @brief [@p minValue, @p maxValue) 의 실수입니다. */
        float32 nextRange( float32 minValue, float32 maxValue );

    private:
        uint32 _state;
    };

    /**
     * @struct ShooterMath
     * @brief 히트스캔 · 시점 계산입니다. 좌표계는 엔진과 같습니다(+Y 위, +Z 앞, 왼손).
     */
    struct SW_GF_API ShooterMath
    {
        /**
         * @brief 광선이 구와 만나는 가장 가까운 앞쪽 거리입니다. 시작점이 구 안이면 0 입니다. 안 만나면 false 입니다.
         * @param maxDistance 이 거리보다 먼 만남은 놓친 것으로 봅니다.
         */
        [[nodiscard]] static bool intersectSphere( const ShooterRay& ray, const float3& center, float32 radius, float32 maxDistance, float32& outDistance );
        /** @brief 광선이 축 정렬 상자와 만나는 가장 가까운 앞쪽 거리입니다(슬랩 판정). 시작점이 안이면 0 입니다. */
        [[nodiscard]] static bool intersectAabb( const ShooterRay& ray, const float3& boxMin, const float3& boxMax, float32 maxDistance, float32& outDistance );
        /** @brief 요 · 피치(라디안, 피치 + 가 위)의 바라보는 단위 방향입니다. */
        static float3 computeLookDirection( float32 yaw, float32 pitch );
        /**
         * @brief @p direction 주위의 원뿔(반각 @p coneHalfAngle 라디안) 안에서 고르게 고른 방향입니다. 반각이 0 이면 그대로입니다.
         * @details 원뿔 안의 입체각에 고르게 — cos θ 를 [cos 반각, 1] 에서 고르고 방위각을 [0, 2π) 에서 고른다.
         */
        static float3 applySpread( const float3& direction, float32 coneHalfAngle, ShooterRandom& random );
    };

    /**
     * @class FirstPersonLook
     * @brief 마우스 이동으로 요 · 피치를 돌리는 1인칭 시점입니다. 피치는 ±`_maxPitch` 에서 자릅니다(뒤로 넘어가지 않는다).
     */
    class SW_GF_API FirstPersonLook
    {
    public:
        FirstPersonLook();

        /** @brief 마우스 이동(픽셀)을 감도(라디안/픽셀)로 돌립니다. 화면 아래(+y)로 끌면 아래를 본다. */
        void addMouseDelta( float32 deltaX, float32 deltaY, float32 sensitivity );
        /** @brief 반동 — 피치를 올리고(위로 튄다) 요를 조금 흔듭니다(라디안). */
        void addRecoil( float32 pitchKick, float32 yawKick );
        void setAngles( float32 yaw, float32 pitch );

        float32 getYaw() const { return _yaw; }
        float32 getPitch() const { return _pitch; }
        float3  getForward() const { return ShooterMath::computeLookDirection( _yaw, _pitch ); }
        /** @brief 수평 앞(이동용, 피치 없음)입니다. */
        float3 getFlatForward() const { return ShooterMath::computeLookDirection( _yaw, 0.0f ); }
        /** @brief 수평 오른쪽입니다. */
        float3 getFlatRight() const;

    private:
        float32 _yaw;
        float32 _pitch;
        float32 _maxPitch;
    };
} // namespace sw
