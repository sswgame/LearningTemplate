/**
 * @file KartRacingDemoComponent.h
 * @brief 장르 키트 시연 — 카트 레이싱(`GF_KartRacing`)의 한 바퀴 경기입니다. 세 대 중 앞에 세운 차가 결승선을 두 번 넘어 1 등으로 들어오고, 결승 시한이 지나 남은 차가 순위대로 끝나는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          아이템이 없는 경기라 트랙 카탈로그(`.karttracks.xml`) 하나만 쓴다. 시나리오: `game/playground/automation/kit_kartracing.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/KartTrack.h"

#include "Games/Playground/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class KartRacingDemoComponent
     * @brief 세 대로 한 바퀴 경기를 열고, 2 번 차를 격자 앞에 세워 결승까지 몰아 경기가 끝나는 한 바퀴를 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Kart Racing Demo", Tooltip = "Runs one KartRacing kit race (grid, laps, finish, finish timeout places) and keeps the result" )
    class KartRacingDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        KartRacingDemoComponent();
        virtual ~KartRacingDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getWinner() const { return _winner; }
        int32 getRunnerUp() const { return _runnerUp; }
        int32 getLast() const { return _last; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 경기 한 판을 돌리고 결과를 담습니다. 트랙 · 경기를 열지 못하거나 차가 결승선에 닿지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runRace();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Kart track catalog XML" )
        AssetRef<KartTrackCatalog> _catalogPath;

        SharedAssetRef<KartTrackCatalog> _pCatalog;
        PROPERTY( RuntimeState )
        int32 _winner; ///< 1 등 차 번호(-1 없음)
        PROPERTY( RuntimeState )
        int32 _runnerUp; ///< 2 등 차 번호(-1 없음)
        PROPERTY( RuntimeState )
        int32 _last; ///< 3 등 차 번호(-1 없음)
    };
} // namespace sw
