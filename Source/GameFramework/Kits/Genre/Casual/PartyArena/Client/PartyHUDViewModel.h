/**
 * @file PartyHUDViewModel.h
 * @brief 파티 아레나 HUD 문서가 보이는 값입니다 — 클라이언트 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다. 글은 문서의 번역 키다(뷰모델은 수와 보임만 든다).
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
     * @class PartyHUDViewModel
     * @brief 파티 아레나 HUD 의 뷰모델입니다. `PartyArenaViewComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 바인딩은 바뀐 칸만 씁니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "PartyArena HUD View Model" )
    class SW_KIT_CLIENT_API PartyHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        PartyHUDViewModel();
        ~PartyHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 지금 라운드입니다(1 부터). */
        void setRound( int32 round );
        /** @brief 라운드의 남은 초입니다(올림, 제한이 없으면 0). */
        void setTimeLeft( int32 timeLeft );
        /** @brief 내 라운드 점수입니다. */
        void setRoundScore( int32 roundScore );
        /** @brief 내 순위 점수 합입니다. */
        void setTotal( int32 total );
        /** @brief 우승 점수입니다. */
        void setWinScore( int32 winScore );
        /** @brief 튕김 콤보입니다. */
        void setCombo( int32 combo );
        /** @brief 우승 표시를 보이거나 숨깁니다. */
        void setWonShown( bool bShown );
        /** @brief 탈락 표시를 보이거나 숨깁니다. */
        void setLostShown( bool bShown );

        int32            getRound() const { return _round; }
        int32            getTimeLeft() const { return _timeLeft; }
        int32            getRoundScore() const { return _roundScore; }
        int32            getTotal() const { return _total; }
        int32            getWinScore() const { return _winScore; }
        int32            getCombo() const { return _combo; }
        WidgetVisibility getWonVisibility() const { return _wonVisibility; }
        WidgetVisibility getLostVisibility() const { return _lostVisibility; }

    private:
        /** @brief 보임 여부를 HUD 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Round" )
        int32 _round;
        PROPERTY( DisplayName = "Time Left" )
        int32 _timeLeft;
        PROPERTY( DisplayName = "Round Score" )
        int32 _roundScore;
        PROPERTY( DisplayName = "Total" )
        int32 _total;
        PROPERTY( DisplayName = "Win Score" )
        int32 _winScore;
        PROPERTY( DisplayName = "Combo" )
        int32 _combo;
        PROPERTY( DisplayName = "Won Visibility" )
        WidgetVisibility _wonVisibility;
        PROPERTY( DisplayName = "Lost Visibility" )
        WidgetVisibility _lostVisibility;
    };
} // namespace sw
