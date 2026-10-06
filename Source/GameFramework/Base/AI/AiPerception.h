/**
 * @file AiPerception.h
 * @brief AI 의 감각 — 시야(거리 · 시야각 · 가림), 청각(소리 반경), 그리고 본 것을 잠시 기억하기(마지막 위치 · 나이)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class NavGrid;

    /** @brief 감각 설정입니다. */
    struct AiPerceptionSettings
    {
        float32 _sightRange{ 12.0f };
        float32 _loseSightRange{ 14.0f };   ///< 이미 보고 있는 대상은 여기까지 놓치지 않는다(경계에서 깜빡이지 않게)
        float32 _sightHalfAngle{ 1.0472f }; ///< 시야각의 절반(라디안, 기본 60° → 120° 시야)
        float32 _peripheralRange{ 2.0f };   ///< 이 안은 등 뒤도 느낀다
        float32 _hearingScale{ 1.0f };      ///< 소리 반경에 곱한다(귀 밝음)
        float32 _memoryDuration{ 5.0f };    ///< 감지가 끊긴 뒤 기억하는 시간
    };
} // namespace sw

namespace sw
{
    /** @brief 감각에 닿을 수 있는 것 하나입니다(게임이 매 감지마다 넘긴다). */
    struct AiStimulus
    {
        float3  _position{};
        uint64  _id{ 0 };
        float32 _noiseRadius{ 0.0f }; ///< 이번에 낸 소리의 반경(0 = 조용함)
    };
} // namespace sw

namespace sw
{
    /** @brief 기억하는 대상입니다. */
    struct AiPerceivedTarget
    {
        float3  _lastKnownPosition{};
        uint64  _id{ 0 };
        float32 _age{ 0.0f };        ///< 마지막으로 감지한 뒤 지난 시간
        uint8   _bSeen{ SW_FALSE };  ///< 지금 보인다
        uint8   _bHeard{ SW_FALSE }; ///< 이번에 들렸다
    };
} // namespace sw

namespace sw
{
    /**
     * @class AiPerception
     * @brief 언리얼 `UAIPerceptionComponent`(Sight · Hearing, Max Age)의 작은 판입니다. 대상 후보는 게임이 고릅니다(보통 공간 해시의 원 질의).
     * @details 가림은 격자가 있으면 칸 시선(`NavGrid::hasLineOfSight`)으로 봅니다 — 벽 뒤는 안 보이고 소리는 벽을 넘는다.
     */
    class SW_GF_API AiPerception
    {
    public:
        AiPerception();

        void setSettings( const AiPerceptionSettings& settings ) { _settings = settings; }
        /**
         * @brief 한 번 감지합니다. 후보 중 보이거나 들린 것을 기억에 넣고, 감지되지 않은 기억은 나이를 먹이다 `_memoryDuration` 이 지나면 잊습니다.
         * @param pGrid 가림을 볼 격자(없으면 가림 없음).
         */
        void sense( const float3& eyePosition, const float3& forward, const vector<AiStimulus>& listCandidate, const NavGrid* pGrid, float32 deltaTime );
        /** @brief 이 자리 · 방향에서 @p target 이 보이는지입니다(기억과 상관없이). @p bAlreadySeen 이면 놓치는 거리를 씁니다. */
        bool canSee( const float3& eyePosition, const float3& forward, const float3& target, const NavGrid* pGrid, bool bAlreadySeen ) const;

        const vector<AiPerceivedTarget>& getTargets() const { return _listTarget; }
        const AiPerceivedTarget*         findTarget( uint64 id ) const;
        /** @brief 지금 보이는 대상 중 @p origin 에 가장 가까운 것입니다. 없으면 nullptr 입니다. */
        const AiPerceivedTarget* findNearestSeen( const float3& origin ) const;
        void                     forgetAll() { _listTarget.clear(); }

    private:
        AiPerceptionSettings      _settings;
        vector<AiPerceivedTarget> _listTarget;
    };
} // namespace sw
