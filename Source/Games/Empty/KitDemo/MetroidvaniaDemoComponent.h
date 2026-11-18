/**
 * @file MetroidvaniaDemoComponent.h
 * @brief 장르 키트 시연 — 메트로배니아(`GF_Metroidvania`)의 죽음과 휴식 한 바퀴입니다. 쉬고, 적을 쓰러뜨려 통화를 벌고, 죽어 시체에 통화를 떨어뜨린 뒤 되찾는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          시나리오: `game/empty/automation/kit_metroidvania.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/MetroidvaniaCatalog.h"

#include "Games/Empty/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class MetroidvaniaDemoComponent
     * @brief 쉬는 곳에서 쉬고 보통 적 · 보스를 쓰러뜨린 뒤 다른 방에서 죽어 시체를 남기고, 같은 자리에서 되찾아 통화가 돌아오는 한 바퀴를 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Metroidvania Demo", Tooltip = "Runs one Metroidvania kit death and rest loop (rest, kills, corpse drop, recovery) and keeps the result" )
    class MetroidvaniaDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        MetroidvaniaDemoComponent();
        virtual ~MetroidvaniaDemoComponent() override;

        void serializeState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getCorpseCurrency() const { return _corpseCurrency; }
        int32 getRecoveredBalance() const { return _recoveredBalance; }
        int32 getRespawnAtRest() const { return _respawnAtRest; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 죽음과 휴식 한 바퀴를 돌리고 결과를 담습니다. 체력 설정을 받지 못하거나 쉬지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runLoop();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Metroidvania catalog XML (rules, sites, enemies)" )
        AssetRef<MetroidvaniaCatalog> _catalogPath;

        SharedAssetRef<MetroidvaniaCatalog> _pCatalog;
        int32                               _corpseCurrency;   ///< 죽을 때 시체에 떨어진 통화
        int32                               _recoveredBalance; ///< 시체를 되찾은 뒤 지갑의 통화
        int32                               _respawnAtRest;    ///< 죽은 뒤 되살아날 곳이 쉰 곳이면 1
    };
} // namespace sw
