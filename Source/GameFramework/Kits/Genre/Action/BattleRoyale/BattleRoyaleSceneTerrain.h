/**
 * @file BattleRoyaleSceneTerrain.h
 * @brief 배틀로얄 판이 씬의 지형에 묻는 것입니다 — 정적 바디(건물)가 막는 이동 · 시야 · 설 자리와 씬 내비메시의 길.
 * @details 판의 사람은 바디 없는 점(판 좌표)이라 물리 질의가 사람끼리 막지 않는다. 걷기는 몸 반지름의 구를 쓸어 벽을 따라 미끄러지고(`WalkSweep`),
 *          시야는 게임 공통 질의(`WorldQuery::hasLineOfSight`), 길은 씬 내비게이션(`SceneNavigation::findPath` — 씬의 `NavMeshSurfaceComponent` 가 정적 바디로 베이크한 내비메시)이다.
 *          물리 바디 · 내비메시가 없는 씬(규칙 테스트)이면 막는 것이 없고 길은 곧은 선이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleZone.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 지형 질의의 몸 크기입니다. */
    struct BattleRoyaleBodySettings
    {
        float32 _groundHeight{ 0.0f }; ///< 땅의 절대 높이(판 좌표의 평면)
        float32 _radius{ 0.35f };      ///< 걷는 몸의 반지름
        float32 _eyeHeight{ 1.2f };    ///< 시야 · 사격선의 땅 위 높이
    };
} // namespace sw

namespace sw
{
    /**
     * @class BattleRoyaleSceneTerrain
     * @brief 판 좌표(x · z 절대 자리)로 묻고 판 좌표로 답합니다. 매니저는 빌려 씁니다(디렉터의 씬). 물리를 묻는 함수라 게임 스레드에서만 부릅니다.
     */
    class SW_KIT_API BattleRoyaleSceneTerrain final : public IBrZoneTerrain
    {
    public:
        BattleRoyaleSceneTerrain();

        /** @brief 씬(매니저 — 지형보다 오래 산다)과 몸 크기를 받습니다. 묻는 함수는 이 뒤에만 부릅니다. */
        void initialize( const GameObjectManager& manager, const BattleRoyaleBodySettings& settings );

        /** @brief 원 중심 · 보급 상자를 건물 위에 두지 않습니다. 바디가 서기 전(판을 여는 순간)에 뽑는 첫 원은 모두 허락된다 — 다음 원의 반지름이 커서 중심 자리가 판을 막지 않는다. */
        bool isZoneCenterAllowed( const float2& position ) const override { return isClear( position ); }
        /** @brief 몸이 @p position 에 서도 건물과 겹치지 않으면 true 입니다. */
        bool isClear( const float2& position ) const;
        /** @brief @p from 에서 @p delta 만큼 걸어 닿는 자리입니다. 막히면 남은 걸음을 벽을 따라 미끄러진다. */
        float2 walk( const float2& from, const float2& delta ) const;
        /** @brief 두 자리의 눈높이 사이를 막는 것이 없으면 true 입니다. */
        bool hasLineOfSight( const float2& from, const float2& to ) const;
        /** @brief @p position 에 설 수 없으면(건물 안) 맵(0..@p mapSize) 안의 가장 가까운 설 자리를 고리로 넓혀 찾습니다. 어디에도 없으면 @p position 입니다. */
        float2 findStandingSpot( const float2& position, float32 mapSize ) const;
        /** @brief 내비메시의 길에서 지날 점(시작은 뺀다)을 @p outListPoint 에 채웁니다(앞의 내용은 지운다). 내비메시가 없거나 길이 없으면 @p to 하나입니다 — 벽은 걷기가 막는다. */
        void findRoute( const float2& from, const float2& to, vector<float2>& outListPoint ) const;

    private:
        /** @brief 판 좌표를 땅 위 @p height 의 씬(로컬) 자리로 옮깁니다. */
        float3 toLocal( const float2& position, float32 height ) const;

    private:
        const GameObjectManager* _pManager;
        BattleRoyaleBodySettings _settings;
    };
} // namespace sw
