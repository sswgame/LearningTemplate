/**
 * @file RigSpringChain.h
 * @brief 스프링 본(꼬리, 머리 가닥, 망토 끝 같은 2 차 움직임)입니다. 베를레 입자 체인, 고정 스텝, 구와 캡슐 충돌체를 담습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct RigSolveSpace;

    class RigPoseBuffer;

    /** @brief 스프링 충돌체의 형태입니다. */
    enum class RigSpringColliderShape : uint8
    {
        Sphere = 0, ///< 점 A 와 반지름으로 정한다.
        Capsule,    ///< 선분 A-B 와 반지름으로 정한다.
    };

    /** @brief 본에 붙은 충돌체 하나입니다. 점은 본 공간(본의 모델 변환으로 옮김)입니다. */
    struct RigSpringCollider
    {
        float3                 _pointA{};
        float3                 _pointB{};
        uint32                 _bone{ 0 };
        float32                _radius{ 0.05f };
        RigSpringColliderShape _shape{ RigSpringColliderShape::Sphere };
    };
} // namespace sw

namespace sw
{
    /** @brief 스프링 사슬의 물성입니다. */
    struct RigSpringSettings
    {
        float3  _gravityOverride{};               ///< `_bUseGravityOverride` 일 때 쓰는 월드 가속도(m/s²)다. 리그 데이터의 "gravity" 값이다.
        float32 _gravityScale{ 1.0f };            ///< 월드 중력(설정된 물리 중력)에 곱하는 배율이다. 리그 데이터의 "gravity_scale" 값이다.
        float32 _stiffness{ 0.1f };               ///< 스텝마다 애니메이션 자세 쪽으로 당기는 비율이다(0..1).
        float32 _damping{ 0.1f };                 ///< 스텝마다 잃는 속도의 비율이다(0..1).
        float32 _particleRadius{ 0.02f };         ///< 입자 반지름이다(충돌).
        float32 _fixedStep{ 1.0f / 60.0f };       ///< 스텝 길이(초)다. 프레임 시간과 무관하게 같은 결과가 나온다.
        float32 _teleportDistance{ 1.0f };        ///< 한 프레임에 뿌리가 이만큼 넘게 움직이면 순간이동으로 보고 다시 시작한다.
        uint32  _maxSubStep{ 4 };                 ///< 프레임당 스텝 상한(넘는 시간은 버린다)
        uint8   _bUseGravityOverride{ SW_FALSE }; ///< 월드 중력 대신 `_gravityOverride` 를 쓴다(언리얼 AnimDynamics 의 GravityOverride).
    };
} // namespace sw

namespace sw
{
    /**
     * @class RigSpringChain
     * @brief 사슬 하나의 상태(월드 공간 입자)와 시뮬레이션입니다. 첫 본은 애니메이션에 묶이고 나머지가 흔들립니다.
     * @details 스텝은 다음 순서로 진행한다. 속도 = (현재 - 이전) × (1 - 감쇠)를 구하고, 중력을 더하고, 애니메이션 자세(부모 입자 + 애니메이션 상대 방향) 쪽으로 강성만큼 당기고,
     *          부모와의 길이를 맞추고, 충돌체 밖으로 밀어내고, 다시 길이를 맞춘다. 입자가 월드 공간이라 캐릭터가 움직이면 끝이 뒤처진다(관성).
     *          평면(2D)이면 입자를 뿌리를 지나는 평면에 둔다. 마지막으로 본을 뿌리부터 차례로 입자 쪽으로 돌린다.
     */
    class SW_API RigSpringChain
    {
    public:
        RigSpringChain();

        /** @brief 상태를 버립니다(다음 시뮬레이션이 애니메이션 자세에서 시작). */
        void reset();
        /** @brief 시뮬레이션 상태가 있는지입니다. */
        bool isSimulating() const { return _bInitialized == SW_TRUE; }
        /** @brief 지난 시뮬레이션의 스텝 수를 반환합니다(테스트와 진단용). */
        uint32 getLastStepCount() const { return _lastStepCount; }
        /** @brief 입자의 월드 위치입니다. */
        const vector<float3>& getParticles() const { return _listPosition; }

        /**
         * @brief @p deltaSeconds 만큼 고정 스텝으로 진행하고 사슬 본을 돌립니다.
         * @param listBone 뿌리 → 끝 본(각 본이 다음 본의 조상)
         * @param worldFromModel 유닛의 월드 행렬
         */
        void simulate( RigPoseBuffer& pose, span<const uint32> listBone, const RigSpringSettings& settings, span<const RigSpringCollider> listCollider,
                       const float4x4& worldFromModel, const float3& worldGravity, float32 deltaSeconds, const RigSolveSpace& space );

    private:
        vector<float3>  _listPosition;
        vector<float3>  _listPreviousPosition;
        vector<float3>  _listAnimated; ///< 이번 프레임 애니메이션 자세다(월드).
        vector<float32> _listLength;   ///< 부모 입자까지의 길이다(애니메이션 자세에서 측정한다).
        float3          _lastRootPosition;
        float32         _accumulator;
        uint32          _lastStepCount;
        uint8           _bInitialized;
    };
} // namespace sw
