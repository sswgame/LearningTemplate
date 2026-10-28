/**
 * @file PoseInertializer.h
 * @brief 포즈 단위 inertialization 입니다. 전이 순간의 포즈 차이와 속도 차이를 본마다 오프셋으로 잡고, 감쇠 스프링으로 0 까지 풀어 냅니다.
 * @details 크로스페이드는 두 재생을 함께 샘플해 섞지만, inertialization 은 새 재생 하나만 샘플하고 그 위에 "끊긴 순간과의 차이"를 더한다
 *          (David Bollo, GDC 2018 "Inertialization: High-Performance Animation Transitions in Gears of War"). 언리얼의 Inertialization 노드와 같은 자리다.
 *          감쇠 곡선은 Bollo 의 5 차 다항식 대신 임계 감쇠 스프링을 쓴다(Daniel Holden "Spring-It-On" 의 `decay_spring_damper_implicit`).
 *          다항식은 길이(초)가 끝나면 정확히 0 이고, 스프링은 반감기 하나로 모양이 정해지며 도중에 다시 시작해도 속도가 이어진다.
 *          오프셋은 시작 값(x0, v0)만 저장하고 경과 시간으로 닫힌 식을 푼다 — 프레임마다 쌓지 않으므로 오차가 누적되지 않는다.
 *          위치는 차이(source - target), 회전은 `source * inverse( target )` 을 짧은 쪽 축-각 벡터로 바꾼 값이다. 스케일은 새 재생을 그대로 쓴다.
 *          스켈레탈 애니메이터의 그래프 전이(`AnimationBlendMode::Inertialization`)가 쓰고, 레이어 경로도 같은 객체 하나를 레이어마다 두면 된다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class Pose;

    /**
     * @class PoseInertializer
     * @brief 본마다 위치 · 회전 오프셋과 그 속도를 가지고, 시간이 흐르면 감쇠 스프링으로 0 에 수렴시킵니다.
     * @details 쓰는 순서는 다음과 같다. (1) 전이 순간에 `begin` — 끊기기 직전 포즈(source)와 새 재생의 첫 포즈(target)로 오프셋을 잡는다.
     *          (2) 매 프레임 `update` 로 시간을 흘린다. (3) 새 재생을 샘플한 포즈에 `apply` 로 남은 오프셋을 더한다.
     *          `begin` 직후(경과 0)의 `apply` 는 target 을 source 로 정확히 되돌린다 — 포즈가 튀지 않는다.
     */
    class SW_API PoseInertializer
    {
    public:
        PoseInertializer();

        /**
         * @brief 전이를 시작합니다. 진행 중이던 전이는 버리고 새 오프셋을 잡습니다(@p sourcePose 에 이미 남은 오프셋이 들어 있으면 속도도 이어집니다).
         * @param sourcePose 끊기기 직전에 내보낸 포즈입니다.
         * @param pSourcePreviousPose 그 한 프레임 전 포즈입니다. 속도를 구할 기록이 없으면 nullptr(속도 0)입니다.
         * @param targetPose 새 재생의 첫 포즈입니다.
         * @param pTargetPreviousPose 새 재생을 한 프레임 앞서 샘플한 포즈입니다. 없으면 nullptr(새 재생의 속도 0)입니다.
         * @param deltaSeconds 두 프레임 사이 시간(초)입니다. 속도를 구할 때만 쓰며, 0 이하면 속도를 구하지 않습니다.
         * @param halfLifeSeconds 감쇠 반감기(초)입니다. 데이터(그래프 링크의 "half_life")가 정한 값이며 0 보다 커야 합니다.
         * @return 본 수가 서로 다르거나 반감기가 0 이하면 오류를 남기고 false 입니다(진행 중이던 전이도 끝납니다).
         */
        [[nodiscard]] bool begin( const Pose& sourcePose, const Pose* pSourcePreviousPose, const Pose& targetPose, const Pose* pTargetPreviousPose, float32 deltaSeconds,
                                  float32 halfLifeSeconds );
        /** @brief 시간을 흘립니다. 오프셋이 충분히 줄면(반감기의 `kSettleHalfLifeCount` 배) 전이를 끝냅니다. */
        void update( float32 deltaSeconds );
        /** @brief 남은 오프셋을 @p inoutPose 에 더합니다. 전이 중이 아니면 아무것도 하지 않습니다. 본 수는 `begin` 때와 같아야 합니다(다르면 건너뜁니다). */
        void apply( Pose& inoutPose ) const;
        /** @brief 전이를 끝내고 오프셋을 버립니다. */
        void reset();

        /** @brief 전이 중인지 반환합니다. */
        bool isActive() const { return _bActive == SW_TRUE; }
        /** @brief 오프셋을 가진 본 수입니다. */
        uint32 getBoneCount() const { return static_cast<uint32>( _listTranslationOffset.size() ); }
        /** @brief 지금 시각의 본 위치 오프셋입니다(source - target 이 0 으로 줄어 가는 값). 전이 중이 아니거나 범위 밖이면 0 입니다. */
        float3 computeTranslationOffset( uint32 boneIndex ) const;
        /** @brief 지금 시각의 본 회전 오프셋(축-각 벡터, 길이가 라디안)입니다. 전이 중이 아니거나 범위 밖이면 0 입니다. */
        float3 computeRotationOffset( uint32 boneIndex ) const;

        /** @brief 전이를 끝내는 시점입니다. 반감기의 이 배수가 지나면 남은 오프셋은 시작 값의 0.01 % 아래입니다(시작 속도 차이가 0 일 때 약 0.0015 %). */
        static constexpr float32 kSettleHalfLifeCount = 10.0f;

    private:
        /** @brief 지금 시각의 감쇠 스프링 값(x0, v0 에서 시작)을 구합니다. */
        float3 computeDecayedOffset( const float3& initialOffset, const float3& initialVelocity ) const;

        vector<float3> _listTranslationOffset;   ///< 본마다 시작 위치 오프셋(source - target)
        vector<float3> _listTranslationVelocity; ///< 본마다 시작 위치 속도 차이(source 속도 - target 속도)
        vector<float3> _listRotationOffset;      ///< 본마다 시작 회전 오프셋(축-각 벡터)
        vector<float3> _listRotationVelocity;    ///< 본마다 시작 각속도 차이(축-각 벡터 / 초)
        float32        _halfLifeSeconds;
        float32        _elapsedSeconds;
        uint8          _bActive;
    };
} // namespace sw
