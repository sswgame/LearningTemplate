/**
 * @file FightingHUDViewModel.h
 * @brief 격투 HUD 문서가 보이는 값(두 쪽 체력 · 라운드 승 · 남은 시간 · 결과)입니다 — 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
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
     * @class FightingHUDViewModel
     * @brief 격투 HUD 의 뷰모델입니다. `FightingViewComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 결과 글은 문서가 들고(번역 키) 여기는 보임만 정합니다. 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 됩니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Fighting HUD View Model" )
    class SW_KIT_CLIENT_API FightingHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        FightingHUDViewModel();
        ~FightingHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 두 쪽 체력 비율(0..1)입니다. */
        void setHealth( float32 playerRatio, float32 opponentRatio );
        /** @brief 두 쪽 라운드 승입니다. */
        void setRounds( int32 playerRounds, int32 opponentRounds );
        /** @brief 라운드의 남은 시간(초)입니다. 보이는 값은 올린 정수다. */
        void setSecondsRemaining( float32 seconds );
        /** @brief 대전 결과입니다(0 플레이어, 1 상대, 2 무승부, −1 끝나지 않음 — `FightingMatch::getMatchWinner`). */
        void setMatchWinner( int32 winner );

    private:
        PROPERTY( DisplayName = "Player Health Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _playerHealthRatio;
        PROPERTY( DisplayName = "Opponent Health Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _opponentHealthRatio;
        PROPERTY( DisplayName = "Player Rounds" )
        int32 _playerRounds;
        PROPERTY( DisplayName = "Opponent Rounds" )
        int32 _opponentRounds;
        PROPERTY( DisplayName = "Seconds Remaining" )
        int32 _secondsRemaining;
        PROPERTY( DisplayName = "Win Visibility" )
        WidgetVisibility _winVisibility;
        PROPERTY( DisplayName = "Lose Visibility" )
        WidgetVisibility _loseVisibility;
        PROPERTY( DisplayName = "Draw Visibility" )
        WidgetVisibility _drawVisibility;
    };
} // namespace sw
