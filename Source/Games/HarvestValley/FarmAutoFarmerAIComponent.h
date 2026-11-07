/**
 * @file FarmAutoFarmerAIComponent.h
 * @brief 자동 농부 — 농부 폰에 빙의하는 AI 조종자입니다. 자동 플레이 스위치(`-gv_farmAutoPlay`)를 켜면 디렉터가 이것을 농부에 빙의시키고, 끄면 플레이어 조종자가 되찾습니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Gameplay/ActionRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"

namespace sw
{
    /** @brief `farm.walkUpTo` 의 칸입니다. 칸 아래(−Z) 통로로 갔다가 위(+Z)를 보며 걸어 들어갑니다. */
    REFLECT()
    struct FarmWalkUpToParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Blackboard key of the stand spot below the tile" )
        hashed_string _key{};
        PROPERTY( Required, Min = 0.0, Units = m, Tooltip = "Counts as standing on the spot inside this distance" )
        float32 _tolerance{ 0.0f };
        PROPERTY( Required, Min = 0.0, Units = m, Tooltip = "How far below the stand spot the farmer steps back, to walk up into it facing up" )
        float32 _approachBack{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class FarmAutoFarmerAIComponent
     * @brief 감각만 냅니다 — 농부의 자리 · 체력 · 도구 · 가방 · 밭을 블랙보드에 적고, 할 일 고르기(늦었거나 지쳤으면 출하하고 잔다, 거둘 것 → 물 → 심기 → 씨앗 사기 → 갈기)는
     *        행동 트리 데이터(`data/ai/autofarmer.bt.xml`)가 합니다. 플레이어와 같은 폰 버튼(`Farm.*`)과 이동 축만 냅니다.
     * @details 농부의 자리 · 체력 · 도구 · 밭은 디렉터의 판 상태이고, 이 조종자는 그것을 **읽기만** 합니다(감각은 조종 시스템이 틱 전에 게임 스레드에서 부른다).
     *          칸에 도구를 쓸 때는 칸 아래(−Z) 한 칸에 서서 위(+Z)를 봐야 하므로 트리가 `farm.walkUpTo` 로 그 아래 자리로 먼저 간 뒤 위로 걸어 들어갑니다 — 디렉터는 걷는 쪽으로
     *          바라보는 쪽을 정한다(네 방향).
     *          블랙보드(감각): `FarmReady` · `PlayerPosition` · `FacingUp` · `NearShop` · `Stamina` · `Tired` · `ProduceCount` · `SelectedSeedCount` · `SeedSelected` · `HasSeed` · `CanAffordSeed`,
     *          할 일 자리 `HarvestTile` · `WaterTile` · `SowTile` · `TillTile`(칸 순서로 그 일의 첫 칸, 없으면 지운다) · `ShipSpot` · `ShopSpot`, 손에 든 도구 `HoldingHoe` · `HoldingWateringCan` · `HoldingSeeds` · `HoldingHand`,
     *          `ActionTimer`(판단마다 경과 시간만큼 줄인다 — 트리가 행동마다 다시 채운다). 트리가 쓰는 칸: `BuyRemaining` · `SeedCountAtBuy` · `StandPosition`.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Auto Farmer AI", Tooltip = "AI controller that farms while auto play is on; senses the farm, its behavior tree decides" )
    class FarmAutoFarmerAIComponent : public AIControllerComponent
    {
    public:
        REFLECT_BODY();

        FarmAutoFarmerAIComponent();
        virtual ~FarmAutoFarmerAIComponent() override = default;

        /** @brief 따를 디렉터입니다(디렉터가 세운 뒤 · 빙의시키기 전에 부른다). */
        void assignGameMode( GameObjectHandle gameMode ) { _gameMode = gameMode; }

        /** @brief `farm.walkUpTo` 입니다. 섰고 위를 보면 멈추고 성공, 아니면 걸으며 실패합니다(뒤따르는 도구 쓰기를 돌리지 않는다). 트리 데이터가 이름으로 부른다. */
        static ActionResult taskWalkUpTo( ActionContext& context, const FarmWalkUpToParams& params );

    protected:
        void updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "Auto Play", DisplayName = "Cultivate Limit", Tooltip = "Tiles the auto farmer tills at most (what one day of stamina covers); TillTile stays empty beyond it", Min = 0 )
        int32 _cultivateLimit;

        GameObjectHandle _gameMode; ///< 따르는 디렉터의 오브젝트
    };
} // namespace sw
