/**
 * @file BrDrop.h
 * @brief 비행기 경로(맵을 가로지르는 씨앗 무작위 직선 · 뛰어내릴 수 있는 구간)와 낙하(자유 낙하 → 낙하산, 수평 이동) · 착지 지점 예측입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BrCatalog.h"

namespace sw
{
    /**
     * @struct BrFlightPath
     * @brief 비행기 한 번의 길입니다. 맵은 [0, mapSize]² 이고 길은 한 변에서 들어와 다른 변으로 나갑니다.
     */
    struct SW_GF_API BrFlightPath
    {
        float2  _start{};
        float2  _end{};
        float32 _speed{ 100.0f };
        float32 _altitude{ 600.0f };
        float32 _jumpStartRatio{ 0.0f };
        float32 _jumpEndRatio{ 1.0f };

        /** @brief 씨앗으로 방향 · 중심에서 비킨 거리를 골라 맵 경계에서 자른 길입니다. 같은 씨앗이면 같은 길입니다. */
        static BrFlightPath makeRandom( const BrFlightSettings& settings, float32 mapSize, uint32 seed );

        float32 computeLength() const;
        float32 computeDuration() const;
        /** @brief 이륙 뒤 @p time 초의 비행기 자리(수평)입니다. 끝 뒤는 끝 자리입니다. */
        float2 computePosition( float32 time ) const;
        /** @brief 그 시각에 뛰어내릴 수 있는가입니다. */
        bool    canJump( float32 time ) const;
        float32 computeJumpOpenTime() const { return computeDuration() * _jumpStartRatio; }
        /** @brief 이 시각에 아직 탄 사람은 모두 내립니다. */
        float32 computeJumpCloseTime() const { return computeDuration() * _jumpEndRatio; }
    };
} // namespace sw

namespace sw
{
    /** @brief 낙하 단계입니다. */
    enum class BrFallStage : uint8
    {
        FreeFall = 0,
        Parachute,
        Landed
    };

    /**
     * @class BrSkydiver
     * @brief 한 사람의 낙하입니다. 자리는 (x, 높이, z) 이고 땅 높이는 하나로 둡니다(게임이 착지 지점의 땅 높이를 넘긴다).
     * @details 자유 낙하는 `_freeFallSpeed` 로 내려가며 조종 방향(길이 ≤ 1)에 `_freeFallHorizontalSpeed` 를 곱해 옮겨 갑니다. 땅 위 `_autoOpenHeight` 에
     *          닿으면 낙하산이 저절로 펴집니다(그 전에 손으로 펼 수도 있다). 걸음을 경계에서 나눠 적분하므로 같은 조종이면 `predictLanding` 과 같은 자리에 내립니다.
     */
    class SW_GF_API BrSkydiver
    {
    public:
        BrSkydiver();

        void initialize( const BrFallSettings& settings, const float3& position, float32 groundHeight );
        /** @brief 낙하산을 폅니다. 자유 낙하 중이 아니면 false 입니다. */
        [[nodiscard]] bool tryOpenParachute();
        /** @brief @p steer 방향(수평, 길이는 1 로 자른다)으로 조종하며 떨어집니다. */
        void update( float32 deltaTime, const float2& steer );

        /** @brief 지금부터 @p steer 로 계속 조종하면 내릴 자리(높이 = 땅)입니다. */
        static float3 predictLanding( const BrFallSettings& settings, const float3& position, float32 groundHeight, const float2& steer, BrFallStage stage );

        const float3& getPosition() const { return _position; }
        BrFallStage   getStage() const { return _stage; }
        bool          isLanded() const { return _stage == BrFallStage::Landed; }
        float3        predictLanding( const float2& steer ) const { return predictLanding( _settings, _position, _groundHeight, steer, _stage ); }

    private:
        BrFallSettings _settings;
        float3         _position;
        float32        _groundHeight;
        BrFallStage    _stage;
    };
} // namespace sw
