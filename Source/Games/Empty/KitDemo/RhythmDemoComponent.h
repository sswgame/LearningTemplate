/**
 * @file RhythmDemoComponent.h
 * @brief 장르 키트 시연 — 리듬(`GF_Rhythm`)의 한 곡입니다. 레인 둘의 짧은 채보를 오토플레이로 끝까지 연주해 모든 판정이 가장 좁은 창에 들고 곡이 깨지는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          채보는 파일 규칙이 없는 공유 캐시 종류라(`RhythmChart`, 접미사 없음) 타입 있는 참조 대신 에셋 경로 문자열로 받는다(`ParkGameModeComponent` 와 같은 길).
 *          시나리오: `game/empty/automation/kit_rhythm.scenario.xml`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Casual/Rhythm/RhythmChart.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    /**
     * @class RhythmDemoComponent
     * @brief 채보 한 곡을 오토플레이로 연주하고 점수 · 최대 콤보 · 깸 여부를 쥡니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Rhythm Demo", Tooltip = "Runs one Rhythm kit song on autoplay (judgments, combo, score, clear) and keeps the result" )
    class RhythmDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        RhythmDemoComponent();
        virtual ~RhythmDemoComponent() override;

        void serializeState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getDemoState() const { return _demoState; }
        int32 getScore() const { return _score; }
        int32 getMaxCombo() const { return _maxCombo; }
        int32 getCleared() const { return _cleared; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 곡 한 바퀴를 돌리고 결과를 담습니다. 판을 열지 못하면 false 입니다. */
        [[nodiscard]] bool runChart();

    private:
        PROPERTY( Category = "Data", DisplayName = "Chart", AssetPath, Tooltip = "Rhythm chart XML (lanes, bpm, notes)" )
        string _catalogPath;

        SharedAssetRef<RhythmChart> _pChart;
        int32                       _demoState; ///< 0 시작 전, 1 한 바퀴를 마침
        int32                       _score;     ///< 곡이 끝난 점수
        int32                       _maxCombo;  ///< 최대 콤보
        int32                       _cleared;   ///< 곡을 깼으면 1
    };
} // namespace sw
