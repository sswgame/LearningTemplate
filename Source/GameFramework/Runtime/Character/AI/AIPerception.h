/**
 * @file AIPerception.h
 * @brief AI 의 감각입니다. 시야(거리, 시야각, 가림)와 청각(소리 반경)으로 대상을 감지하고, 본 것을 잠시 기억합니다.
 * @details 기억은 마지막으로 감지한 위치와 그 뒤로 지난 시간을 가집니다. 잠입 시야가 쓰는 빛 노출 질의(`LightExposure`)도 둡니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Component;
    class GameObjectManager;
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
    /** @brief 자극 종류입니다. 감각은 자기 종류만 본다 — 소리는 시야각 안이어도 보이지 않는다. 씬 색인도 종류마다 층이 따로 있다. */
    enum class AIStimulusKind : uint8
    {
        Sight,   ///< 보이는 몸(반 크기 0 인 점)
        Hearing, ///< 낸 소리(반 크기 = 소리 반경)
        Count,
    };

    /**
     * @brief 감각에 닿을 수 있는 자극 하나입니다(대상 하나가 보이면서 소리를 내면 둘이다). 게임이 감지할 때마다 넘깁니다.
     * @details 종류 없이 만들 수 없다 — 예전 자극은 자리와 소리 반경을 한 칸에 실어 소리만 내는 원천(삐걱 바닥)도 시야 안이면 보인 것이 됐다.
     *          언리얼 `FAIStimulus` 가 감각(`UAISense_Sight` · `UAISense_Hearing`)마다 따로 나고 감각이 자기 자극만 처리하는 나눔이다.
     */
    class AIStimulus
    {
    public:
        /** @brief 보이는 몸입니다. */
        static AIStimulus makeSight( const float3& position, uint64 id ) { return AIStimulus{ AIStimulusKind::Sight, position, id, 0.0f }; }
        /** @brief 반경 @p noiseRadius 의 소리입니다. */
        static AIStimulus makeHearing( const float3& position, uint64 id, float32 noiseRadius ) { return AIStimulus{ AIStimulusKind::Hearing, position, id, noiseRadius }; }

        const float3&  getPosition() const { return _position; }
        uint64         getID() const { return _id; }
        float32        getNoiseRadius() const { return _noiseRadius; }
        AIStimulusKind getKind() const { return _kind; }

    private:
        AIStimulus( AIStimulusKind kind, const float3& position, uint64 id, float32 noiseRadius )
            : _position{ position }
            , _id{ id }
            , _noiseRadius{ noiseRadius }
            , _kind{ kind }
        {
        }

    private:
        float3         _position;
        uint64         _id;
        float32        _noiseRadius; ///< 소리의 반경(Hearing 만)
        AIStimulusKind _kind;
    };
} // namespace sw

namespace sw
{
    /** @brief 기억하는 대상입니다. */
    struct AIPerceivedTarget
    {
        float3  _lastKnownPosition{};
        uint64  _id{ 0 };
        float32 _age{ 0.0f };          ///< 마지막으로 감지한 뒤 지난 시간
        uint8   _bSeen{ SW_FALSE };    ///< 지금 보인다
        uint8   _bHeard{ SW_FALSE };   ///< 이번 감지에서 들렸다
        uint8   _bWasSeen{ SW_FALSE }; ///< 지난 감지에서 보였다(놓치는 거리를 쓰는 판정)
    };
} // namespace sw

namespace sw
{
    /**
     * @class AIPerception
     * @brief 언리얼 `UAIPerceptionComponent`(Sight, Hearing, Max Age)를 작게 만든 것입니다.
     * @details 씬에서는 자극 원천(`AIStimulusSourceComponent`)이 팀 · 종류별 공간 색인에 오르고 감각이 그 색인을 직접 묻는다(씬 판 `sense` — 언리얼 `UAIPerceptionSystem` 의
     *          자극 원천 등록). 씬이 없는 규칙 키트(순수 시뮬레이션)는 후보 목록 판 `sense` 에 자기 자극을 넘긴다.
     *          격자가 있으면 가림을 셀 시선(`NavGrid::hasLineOfSight`)으로 봅니다. 벽 뒤는 보이지 않고, 소리는 벽을 넘습니다.
     */
    class SW_GF_API AIPerception
    {
    public:
        AIPerception();

        void setSettings( const AIPerceptionSettings& settings ) { _settings = settings; }
        /**
         * @brief 한 번 감지합니다. 시각은 보이는 자극만, 청각은 소리 자극만 보고 감지한 것을 기억에 넣습니다.
         * @details 감지되지 않은 기억은 나이를 더하고, `_memoryDuration` 이 지나면 잊습니다.
         * @param pGrid 가림을 볼 격자(절대 자리)입니다. 없으면 가림을 보지 않습니다.
         */
        void sense( const float3& eyePosition, const float3& forward, const vector<AIStimulus>& listCandidate, const NavGrid* pGrid, float32 deltaTime );
        /**
         * @brief 씬 판: @p ownTeam 이 아닌 팀의 자극 원천을 공간 색인에서 직접 골라(`AIStimulusSourceComponent::gatherStimuli`, 시야 상자) 한 번 감지합니다.
         * @details 질의 버퍼는 이 감각이 들고 있어 프레임마다 새로 잡지 않는다. 같은 감각을 두 스레드에서 함께 부르지 않는다.
         *          월드 원점이 옮겨졌으면 기억한 자리를 같은 만큼 옮긴 뒤 감지한다(`GameObjectManager::getWorldOrigin`).
         */
        void sense( const GameObjectManager& manager, uint8 ownTeam, const float3& eyePosition, const float3& forward, const NavGrid* pGrid, float32 deltaTime );
        /** @brief 이 위치와 방향에서 @p target 이 보이는지입니다. 기억과는 상관없습니다. @p bAlreadySeen 이면 놓치는 거리를 씁니다. */
        bool canSee( const float3& eyePosition, const float3& forward, const float3& target, const NavGrid* pGrid, bool bAlreadySeen ) const;

        const vector<AIPerceivedTarget>& getTargets() const { return _listTarget; }
        const AIPerceivedTarget*         findTarget( uint64 id ) const;
        /** @brief 지금 보이는 대상 중 @p origin 에 가장 가까운 것입니다. 없으면 nullptr 입니다. */
        const AIPerceivedTarget* findNearestSeen( const float3& origin ) const;
        void                     clearAll() { _listTarget.clear(); }
        /** @brief 기억한 대상(절대 자리 · 나이 · 보임)입니다. 감각 설정은 쓰는 쪽이 감지마다 넣는다. */
        SW_DECLARE_STATE_IO();

    private:
        AIPerceptionSettings      _settings;
        vector<AIPerceivedTarget> _listTarget;
        vector<AIStimulus>        _listCandidate; ///< 씬 판 `sense` 의 후보 버퍼
        vector<Component*>        _listQueryKey;  ///< 씬 판 `sense` 의 색인 질의 버퍼
        double3                   _worldOrigin;   ///< 기억한 자리가 기준한 월드 원점의 절대 자리
    };

    /**
     * @struct LightExposure
     * @brief 잠입의 빛 노출입니다. 한 위치가 씬의 라이트(점, 스폿, 방향)에 얼마나 드러나는지 계산하며, 0 이면 어둠입니다. 점 라이트와 스폿 라이트는 반경 안에서 (1 - d/r)² × 세기이고(스폿은 원뿔 밖이면 0),
     *        방향광은 세기 그대로입니다. @p bOcclusion 이 켜져 있으면 라이트까지 광선이 막힐 때 그 라이트를 뺍니다(방향광은 라이트 반대쪽 100 m 까지 검사). 꺼져 있으면 가림을 검사하지 않습니다.
     */
    struct SW_GF_API LightExposure
    {
        static float32 computeExposure( const GameObjectManager& manager, const float3& position, bool bOcclusion, uint64 ignoreObjectID = 0 );
    };
} // namespace sw
