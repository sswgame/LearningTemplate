/**
 * @file RigIKSolver.h
 * @brief IK 풀이(2 본 · FABRIK · CCD · 조준)와 관절 제한입니다. 모두 `RigPoseBuffer` 의 모델 공간 위에서 돌고 결과를 로컬 회전으로 씁니다.
 * @details 평면(2D) 풀이는 같은 함수에 `RigSolveSpace::_bPlanar` 를 켜서 씁니다 — 위치를 평면에 투영하고 회전은 평면 법선(기본 Z) 둘레로만 납니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class RigPoseBuffer;

    /** @brief 풀이 공간입니다. 평면이면 2D 리그(XY 평면 · Z 축 회전)입니다. */
    struct RigSolveSpace
    {
        float3 _planeNormal{ 0.0f, 0.0f, 1.0f };
        uint8  _bPlanar{ SW_FALSE };

        /** @brief 평면이면 @p vector 에서 법선 성분을 뺍니다. */
        float3 projectVector( const float3& vector ) const;
        /** @brief 평면이면 @p point 를 @p origin 을 지나는 평면에 투영합니다. */
        float3 projectPoint( const float3& point, const float3& origin ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 관절 제한의 종류입니다. */
    enum class RigJointLimitType : uint8
    {
        None = 0, ///< 제한 없음
        Cone,     ///< 본 축의 흔들림(swing)을 원뿔 각 안으로, 비틀림(twist)을 ± 각 안으로
        Hinge,    ///< 한 축(본의 레퍼런스 로컬 축) 둘레 회전만, [최소, 최대] 각
    };

    /**
     * @brief 관절 하나의 제한입니다. 각은 레퍼런스 로컬 회전 기준(라디안)입니다.
     * @details 제한은 로컬 회전 = 레퍼런스 * 차이 의 차이에 겁니다. 본 축은 본에서 자식으로 가는 방향(본 자기 공간)이고 묶을 때 정합니다.
     */
    struct RigJointLimit
    {
        quaternion        _referenceRotation{};
        float3            _boneAxis{ 0.0f, 1.0f, 0.0f };
        float3            _hingeAxis{ 1.0f, 0.0f, 0.0f };
        float32           _swingLimit{ MathUtil::kPi };
        float32           _twistLimit{ MathUtil::kPi };
        float32           _minAngle{ -MathUtil::kPi };
        float32           _maxAngle{ MathUtil::kPi };
        RigJointLimitType _type{ RigJointLimitType::None };
    };
} // namespace sw

namespace sw
{
    /** @brief 사슬 IK 의 반복 설정입니다. */
    struct RigChainSettings
    {
        float32 _tolerance{ 0.001f };           ///< 끝이 목표에 이만큼 가까우면 멈춘다(미터)
        float32 _maxStepAngle{ MathUtil::kPi }; ///< CCD 한 관절 한 걸음의 최대 회전(라디안) — 작으면 부드럽게 휜다
        uint32  _iterationCount{ 10 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RigIKSolver
     * @brief IK 풀이 함수 묶음입니다. 본 번호는 작업 포즈의 번호이고, 사슬은 뿌리 → 끝 순서(각 본이 다음 본의 조상)입니다.
     */
    struct SW_API RigIKSolver
    {
        /**
         * @brief 2 본 IK(해석해) — 뿌리 · 가운데 · 끝 관절이 목표에 닿게 뿌리와 가운데를 돌립니다. 닿지 않으면 목표 쪽으로 곧게 뻗습니다.
         * @param pPole 굽힘 쪽을 가리키는 점(모델 공간)입니다. nullptr 이면 지금 굽은 쪽을 지킵니다.
         * @return 목표에 닿았으면(길이 안) true 입니다.
         */
        static bool solveTwoBone( RigPoseBuffer& pose, uint32 rootBone, uint32 midBone, uint32 endBone, const float3& target, const float3* pPole,
                                  const RigSolveSpace& space );
        /** @brief FABRIK — 위치를 앞뒤로 번갈아 맞춘 뒤 회전으로 옮깁니다. 제한이 있으면 반복마다 회전을 제한하고 위치를 다시 읽습니다. */
        static bool solveFabrik( RigPoseBuffer& pose, span<const uint32> listChainBone, const float3& target, span<const RigJointLimit> listLimit,
                                 const RigChainSettings& settings, const RigSolveSpace& space );
        /** @brief CCD — 끝에서 뿌리로 관절마다 끝을 목표 쪽으로 돌립니다(관절마다 제한을 바로 겁니다). */
        static bool solveCcd( RigPoseBuffer& pose, span<const uint32> listChainBone, const float3& target, span<const RigJointLimit> listLimit,
                              const RigChainSettings& settings, const RigSolveSpace& space );
        /**
         * @brief 본의 로컬 축(@p localAxis)이 목표를 향하게 돌립니다. 지금(애니메이션) 방향에서 @p maxAngle 을 넘으면 그 각까지만 돕니다.
         * @param weight 0..1 — 돌릴 몫(사슬에 나눠 줄 때)
         * @return 돌린 각(라디안)입니다.
         */
        static float32 aimBone( RigPoseBuffer& pose, uint32 bone, const float3& localAxis, const float3& target, float32 maxAngle, float32 weight,
                                const RigSolveSpace& space );
        /** @brief 본의 지금 로컬 회전에 제한을 겁니다. */
        static void applyJointLimit( RigPoseBuffer& pose, uint32 bone, const RigJointLimit& limit );
        /** @brief 차이 회전 @p delta 를 축 @p axis 둘레 비틀림과 그 나머지(흔들림)로 나눕니다 — delta = swing * twist. */
        static void decomposeSwingTwist( const quaternion& delta, const float3& axis, quaternion& outSwing, quaternion& outTwist );
        /**
         * @brief @p from 방향을 @p to 방향으로 돌리는 최소 회전입니다.
         * @details `quaternion::fromToRotation` 은 1e-6 안쪽의 코사인 차를 단위 회전으로 버린다(약 0.08°) — 사슬 IK 의 마지막 몇 mm 가 그 안에 든다.
         *          여기서는 작은 각도 반각 공식(외적, 1 + 내적)을 그대로 정규화해 끝까지 수렴하게 한다.
         */
        static quaternion makeFromToRotation( const float3& from, const float3& to );
        /** @brief 역회전입니다(`quaternion::inverse` 는 const 가 아닌 값에서 제자리 버전이 골라지므로 값으로 받는 창구를 둔다). */
        static quaternion makeInverse( const quaternion& rotation ) { return rotation.inverse(); }
        /** @brief 축 둘레 비틀림 회전의 부호 있는 각(라디안)입니다. */
        static float32 computeTwistAngle( const quaternion& twist, const float3& axis );
    };
} // namespace sw
