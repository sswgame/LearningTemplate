/**
 * @file FarmAutoFarmerAiComponent.h
 * @brief 자동 농부 — 농부 폰에 빙의하는 AI 조종자입니다. 자동 플레이 스위치(`-gv_farmAutoPlay`)를 켜면 디렉터가 이것을 농부에 빙의시키고, 끄면 플레이어 조종자가 되찾습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Control/Controller/AiControllerComponent.h"

namespace sw
{
    class FarmDirectorComponent;

    /**
     * @class FarmAutoFarmerAiComponent
     * @brief 할 일을 고른다 — 늦었거나 지쳤으면 출하하고 잔다, 거둘 것 → 물 → 심기 → 갈기 → 씨앗 사기. 플레이어와 같은 폰 버튼(`Farm.*`)과 이동 축만 냅니다.
     * @details 농부의 자리 · 체력 · 도구 · 밭은 디렉터의 판 상태이고, 이 조종자는 그것을 **읽기만** 합니다(판단은 조종 시스템이 틱 전에 게임 스레드에서 부른다).
     *          칸에 도구를 쓸 때는 칸 아래(−Z) 한 칸에 서서 위(+Z)를 봐야 하므로 그 아래 자리로 먼저 간 뒤 위로 걸어 들어갑니다 — 디렉터는 걷는 쪽으로
     *          바라보는 쪽을 정한다(네 방향). 가게에서는 한 번 서면 `_buyBatch` 개까지 이어서 삽니다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Auto Farmer AI", Tooltip = "AI controller that farms while auto play is on" )
    class FarmAutoFarmerAiComponent : public AiControllerComponent
    {
    public:
        REFLECT_BODY();

        FarmAutoFarmerAiComponent();
        virtual ~FarmAutoFarmerAiComponent() override = default;

        /** @brief 따를 디렉터입니다(디렉터가 세운 뒤 · 빙의시키기 전에 부른다). */
        void assignDirector( GameObjectHandle director ) { _director = director; }

    protected:
        void think( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        /** @brief 칸 아래 자리로 갔다가 위로 걸어 들어갑니다. 섰고 위를 보면 true 입니다. */
        bool walkUpTo( const FarmDirectorComponent& director, const float3& standPosition );
        /** @brief @p standPosition 으로 갑니다. 섰으면 true 입니다. */
        bool walkTo( const FarmDirectorComponent& director, const float3& standPosition );

    private:
        PROPERTY( Category = "Auto Play", DisplayName = "Action Interval", Tooltip = "Seconds between actions on a tile or at the bin", Min = 0.0, Units = s )
        float32 _actionInterval;
        PROPERTY( Category = "Auto Play", DisplayName = "Cultivate Limit", Tooltip = "Tiles the auto farmer looks after (what one day of stamina covers)", Min = 0 )
        int32 _cultivateLimit;
        PROPERTY( Category = "Auto Play", DisplayName = "Buy Batch", Tooltip = "Seeds bought in one visit to the shop", Min = 1 )
        int32 _buyBatch;
        PROPERTY( Category = "Auto Play", DisplayName = "Stand Tolerance", Tooltip = "Counts as standing on the spot inside this distance", Min = 0.0, Units = m )
        float32 _standTolerance;

        GameObjectHandle _director;       ///< 따르는 디렉터의 오브젝트
        float32          _actionTimer;    ///< 0 이하가 되면 다음 행동
        int32            _buyRemaining;   ///< 가게에서 이어서 살 남은 개수(0 이면 사지 않는 중)
        int32            _seedCountAtBuy; ///< 지난 사기 누름 때 가방의 고른 씨앗 수(못 샀는지 본다)
    };
} // namespace sw
