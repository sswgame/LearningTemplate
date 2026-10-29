/**
 * @file BattleRoyaleDrop.h
 * @brief 비행기 경로와 낙하를 다룹니다. 경로는 맵을 가로지르는 시드 기반 무작위 직선이며 뛰어내릴 수 있는 구간이 있고, 낙하는 자유 낙하에서 낙하산으로 이어지며 수평 이동과 착지 지점 예측을 지원합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BattleRoyaleCatalog.h"

namespace sw
{
    /**
     * @struct BattleRoyaleFlightPath
     * @brief 비행기 한 번의 경로입니다. 맵은 [0, mapSize]² 이고, 경로는 한 변에서 들어와 다른 변으로 나갑니다.
     */
    struct SW_KIT_API BattleRoyaleFlightPath
    {
        float2  _start{};
        float2  _end{};
        float32 _speed{ 100.0f };
        float32 _altitude{ 600.0f };
        float32 _jumpStartRatio{ 0.0f };
        float32 _jumpEndRatio{ 1.0f };

        /**
         * @brief 시드로 방향과 중심에서 비킨 거리를 골라 맵 경계에서 자른 경로를 @p outPath 에 담습니다. 같은 시드면 같은 경로가 나옵니다.
         * @return 비행 설정이 `PROPERTY( Min · Max )` 범위 안이고 @p mapSize 가 1 이상이면 true 입니다. 밖이면 오류를 남기고 false 이며 @p outPath 를 바꾸지 않습니다(값을 조용히 맞추지 않는다).
         */
        [[nodiscard]] static bool makeRandom( const BattleRoyaleFlightDef& settings, float32 mapSize, uint32 seed, BattleRoyaleFlightPath& outPath );

        float32 computeLength() const;
        float32 computeDuration() const;
        /** @brief 이륙 뒤 @p time 초의 비행기 수평 위치를 반환합니다. 경로가 끝난 뒤에는 끝 위치를 반환합니다. */
        float2 computePosition( float32 time ) const;
        /** @brief 그 시각에 뛰어내릴 수 있는지 반환합니다. */
        bool    canJump( float32 time ) const;
        float32 computeJumpOpenTime() const { return computeDuration() * _jumpStartRatio; }
        /** @brief 이 시각에 아직 비행기에 탄 사람은 모두 내립니다. */
        float32 computeJumpCloseTime() const { return computeDuration() * _jumpEndRatio; }
    };
} // namespace sw

namespace sw
{
    /** @brief 낙하 단계입니다. */
    enum class BattleRoyaleFallStage : uint8
    {
        FreeFall = 0,
        Parachute,
        Landed
    };

    /**
     * @class BattleRoyaleSkydiver
     * @brief 한 사람의 낙하입니다. 위치는 (x, 높이, z) 이고 땅 높이는 하나로 둡니다(게임이 착지 지점의 땅 높이를 넘깁니다).
     * @details 자유 낙하는 `_freeFallSpeed` 로 내려가며, 조종 방향(길이 ≤ 1)에 `_freeFallHorizontalSpeed` 를 곱해 옮겨 간다. 땅 위 `_autoOpenHeight` 에 닿으면 낙하산이 저절로 펴진다(그 전에 손으로 펼 수도 있다).
     *          걸음을 경계에서 나눠 적분하므로 같은 조종이면 `predictLanding` 과 같은 위치에 내린다.
     */
    class SW_KIT_API BattleRoyaleSkydiver
    {
    public:
        BattleRoyaleSkydiver();

        void initialize( const BattleRoyaleFallDef& settings, const float3& position, float32 groundHeight );
        /** @brief 낙하산을 폅니다. 자유 낙하 중이 아니면 false 를 반환합니다. */
        [[nodiscard]] bool tryOpenParachute();
        /** @brief @p steer 방향(수평이며 길이는 1로 자른다)으로 조종하며 떨어집니다. */
        void update( float32 deltaTime, const float2& steer );

        /** @brief 지금부터 @p steer 로 계속 조종하면 내릴 위치(높이는 땅 높이)를 예측합니다. */
        static float3 predictLanding( const BattleRoyaleFallDef& settings, const float3& position, float32 groundHeight, const float2& steer, BattleRoyaleFallStage stage );

        const float3&         getPosition() const { return _position; }
        BattleRoyaleFallStage getStage() const { return _stage; }
        bool                  isLanded() const { return _stage == BattleRoyaleFallStage::Landed; }
        float3                predictLanding( const float2& steer ) const { return predictLanding( _settings, _position, _groundHeight, steer, _stage ); }

    private:
        BattleRoyaleFallDef   _settings;
        float3                _position;
        float32               _groundHeight;
        BattleRoyaleFallStage _stage;
    };
} // namespace sw
