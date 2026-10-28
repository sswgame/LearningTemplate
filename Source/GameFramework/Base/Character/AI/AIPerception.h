/**
 * @file AIPerception.h
 * @brief AI 의 감각입니다. 시야(거리, 시야각, 가림)와 청각(소리 반경)으로 대상을 감지하고, 본 것을 잠시 기억합니다.
 * @details 기억은 마지막으로 감지한 위치와 그 뒤로 지난 시간을 가집니다.
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
    struct AIPerceptionSettings
    {
        float32 _sightRange{ 12.0f };
        float32 _loseSightRange{ 14.0f };   ///< 이미 보고 있는 대상은 이 거리까지 놓치지 않는다. 경계에서 감지가 깜빡이지 않게 한다
        float32 _sightHalfAngle{ 1.0472f }; ///< 시야각의 절반(라디안). 기본 60° 는 120° 시야다
        float32 _peripheralRange{ 2.0f };   ///< 이 거리 안은 등 뒤도 느낀다
        float32 _hearingScale{ 1.0f };      ///< 소리 반경에 곱하는 값(귀가 밝을수록 크다)
        float32 _memoryDuration{ 5.0f };    ///< 감지가 끊긴 뒤 기억하는 시간
    };
} // namespace sw

namespace sw
{
    /** @brief 감각에 닿을 수 있는 대상 하나입니다. 게임이 감지할 때마다 넘깁니다. */
    struct AIStimulus
    {
        float3  _position{};
        uint64  _id{ 0 };
        float32 _noiseRadius{ 0.0f }; ///< 이번에 낸 소리의 반경. 0 이면 조용하다
    };
} // namespace sw

namespace sw
{
    /** @brief 기억하는 대상입니다. */
    struct AIPerceivedTarget
    {
        float3  _lastKnownPosition{};
        uint64  _id{ 0 };
        float32 _age{ 0.0f };        ///< 마지막으로 감지한 뒤 지난 시간
        uint8   _bSeen{ SW_FALSE };  ///< 지금 보인다
        uint8   _bHeard{ SW_FALSE }; ///< 이번 감지에서 들렸다
    };
} // namespace sw

namespace sw
{
    /**
     * @class AIPerception
     * @brief 언리얼 `UAIPerceptionComponent`(Sight, Hearing, Max Age)를 작게 만든 것입니다. 대상 후보는 게임이 고릅니다(보통 공간 해시의 원 질의).
     * @details 격자가 있으면 가림을 셀 시선(`NavGrid::hasLineOfSight`)으로 봅니다. 벽 뒤는 보이지 않고, 소리는 벽을 넘습니다.
     */
    class SW_GF_API AIPerception
    {
    public:
        AIPerception();

        void setSettings( const AIPerceptionSettings& settings ) { _settings = settings; }
        /**
         * @brief 한 번 감지합니다. 후보 중 보이거나 들린 것을 기억에 넣습니다.
         * @details 감지되지 않은 기억은 나이를 더하고, `_memoryDuration` 이 지나면 잊습니다.
         * @param pGrid 가림을 볼 격자입니다. 없으면 가림을 보지 않습니다.
         */
        void sense( const float3& eyePosition, const float3& forward, const vector<AIStimulus>& listCandidate, const NavGrid* pGrid, float32 deltaTime );
        /** @brief 이 위치와 방향에서 @p target 이 보이는지입니다. 기억과는 상관없습니다. @p bAlreadySeen 이면 놓치는 거리를 씁니다. */
        bool canSee( const float3& eyePosition, const float3& forward, const float3& target, const NavGrid* pGrid, bool bAlreadySeen ) const;

        const vector<AIPerceivedTarget>& getTargets() const { return _listTarget; }
        const AIPerceivedTarget*         findTarget( uint64 id ) const;
        /** @brief 지금 보이는 대상 중 @p origin 에 가장 가까운 것입니다. 없으면 nullptr 입니다. */
        const AIPerceivedTarget* findNearestSeen( const float3& origin ) const;
        void                     forgetAll() { _listTarget.clear(); }

    private:
        AIPerceptionSettings      _settings;
        vector<AIPerceivedTarget> _listTarget;
    };
} // namespace sw
