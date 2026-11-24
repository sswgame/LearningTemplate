/**
 * @file BattleRoyaleHUDViewModel.h
 * @brief 배틀로얄 HUD 문서가 보이는 값(체력 · 남은 인원 · 구역 시간 · 안내 · 결과)입니다 — 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Presentation/WidgetVisibility.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class BattleRoyaleHUDViewModel
     * @brief 배틀로얄 HUD 의 뷰모델입니다. `BattleRoyaleViewComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 글은 문서가 들고(번역 키) 여기는 수와 보임만 정합니다. 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 됩니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Battle Royale HUD View Model" )
    class SW_KIT_CLIENT_API BattleRoyaleHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        BattleRoyaleHUDViewModel();
        ~BattleRoyaleHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 체력 비율(0..1)입니다. */
        void setHealth( float32 ratio );
        /** @brief 죽지 않은 사람 수입니다. */
        void setRemaining( int32 remaining );
        /** @brief 구역의 지금 단계가 끝날 때까지 남은 시간(초)입니다. 보이는 값은 올린 정수다. */
        void setZoneSeconds( float32 seconds );
        /** @brief 뛰어내리기 안내 · 기절 글을 보이거나 숨깁니다. */
        void setHints( bool bDropHint, bool bDowned );
        /** @brief 결과 글을 보이거나 숨깁니다(둘 다 false 면 진행 중). */
        void setResult( bool bWon, bool bLost );

    private:
        /** @brief 보임 여부를 HUD 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Health Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _healthRatio;
        PROPERTY( DisplayName = "Remaining People" )
        int32 _remaining;
        PROPERTY( DisplayName = "Zone Seconds" )
        int32 _zoneSeconds;
        PROPERTY( DisplayName = "Drop Hint Visibility" )
        WidgetVisibility _dropHintVisibility;
        PROPERTY( DisplayName = "Downed Visibility" )
        WidgetVisibility _downedVisibility;
        PROPERTY( DisplayName = "Win Visibility" )
        WidgetVisibility _winVisibility;
        PROPERTY( DisplayName = "Lose Visibility" )
        WidgetVisibility _loseVisibility;
    };
} // namespace sw
