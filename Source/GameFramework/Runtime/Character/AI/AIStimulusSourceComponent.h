/**
 * @file AIStimulusSourceComponent.h
 * @brief AI 감각에 닿는 자극 원천입니다. 등록하면 팀 · 자극 종류로 나눈 씬 공간 색인(`SceneSpatialGrid` 층)에 오르고, 감각(`AIPerception::sense` 씬 판)이 그 색인을 직접 묻습니다.
 */
#pragma once
#include "Core/Container/SlotHandle.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    struct AIStimulus;
    struct SpatialGridLayerDesc;

    /** @brief 자극 종류입니다. 종류마다 색인 층이 따로 있다. */
    enum class AIStimulusKind : uint8
    {
        Sight,   ///< 보이는 몸(반 크기 0 인 점)
        Hearing, ///< 낸 소리(반 크기 = 소리 반경)
        Count,
    };

    /**
     * @class AIStimulusSourceComponent
     * @brief 언리얼 `UAIPerceptionStimuliSourceComponent` 에 해당합니다. 게임이 후보 목록을 골라 넘기지 않고, 원천이 등록 · 해제할 때만 색인에 오르내립니다.
     * @details 자리는 색인이 월드 변경 번호로 따라간다(`SceneSpatialGrid` — 원천은 틱하지 않는다). 감각은 자기 팀이 아닌 층만 묻는다.
     *          소리는 `reportNoise` 가 반경을 걸고 `clearNoise` 가 거둔다(틱 안이면 색인 고치기를 틱 뒤로 미룬다).
     */
    REFLECT( Category = "AI", DisplayName = "AI Stimulus Source", Tooltip = "Registers this object in the AI perception index (by team and stimulus kind) so AI senses find it without a candidate list" )
    class SW_GF_API AIStimulusSourceComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 색인이 나누는 팀 수입니다. 팀 번호는 0 .. kTeamCount - 1 입니다. */
        static constexpr uint8 kTeamCount = 4;
        /** @brief 층마다 처음 거는 셀 크기(m)입니다. 소리 반경이 크면 색인이 셀을 키운다. */
        static constexpr float32 kCellSize = 8.0f;

        AIStimulusSourceComponent();
        ~AIStimulusSourceComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;

        uint8 getTeam() const { return _team; }
        /** @brief 팀을 정합니다. 등록 전(붙인 직후 · 로드)에만 부릅니다 — 등록된 뒤에는 층을 옮기지 않습니다. */
        void    setTeam( uint8 team ) { _team = team; }
        bool    isVisible() const { return _bVisible; }
        float32 getNoiseRadius() const { return _noiseRadius; }
        /** @brief 소리를 냅니다. 반경 안의 다른 팀 감각이 듣습니다. 다시 부르면 반경을 바꾸고, `clearNoise` 까지 남습니다. */
        void reportNoise( float32 radius );
        /** @brief 소리를 거둡니다. */
        void clearNoise() { reportNoise( 0.0f ); }

        /** @brief 팀 @p team 의 @p kind 층입니다. 범위 밖 팀은 단언으로 멈춥니다. */
        static const SpatialGridLayerDesc& getLayer( uint8 team, AIStimulusKind kind );
        /**
         * @brief @p ownTeam 이 아닌 팀의 원천 중 @p eye 둘레 반 크기 @p range 상자에 걸린 것(보이는 몸 · 상자에 닿는 소리)을 @p outListStimulus 에 담습니다(먼저 비운다).
         * @details 오브젝트 하나는 한 번만 담는다(번호 = 오브젝트 id). 고르기만 하고 보이는지 · 들리는지는 감각이 지금 자리로 판정한다.
         *          @p inoutListKey 는 질의 버퍼다(감각이 들고 있어 프레임마다 새로 잡지 않는다).
         */
        static void gatherStimuli( const GameObjectManager& manager, uint8 ownTeam, const float3& eye, float32 range, vector<AIStimulus>& outListStimulus, vector<Component*>& inoutListKey );

    private:
        PROPERTY( Category = "AI", DisplayName = "Team", Min = 0, Max = 3, Tooltip = "Team index; AI senses only look at teams other than their own" )
        uint8 _team;
        PROPERTY( Category = "AI", DisplayName = "Visible", Tooltip = "Registers the body for sight; turn off for sources that are only heard" )
        bool _bVisible;

        float32    _noiseRadius;
        SlotHandle _arrEntry[static_cast<size_t>( AIStimulusKind::Count )];
    };
} // namespace sw
