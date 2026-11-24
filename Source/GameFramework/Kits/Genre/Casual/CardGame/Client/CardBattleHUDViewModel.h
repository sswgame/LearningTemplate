/**
 * @file CardBattleHUDViewModel.h
 * @brief 덱 전투 HUD 문서가 보이는 값입니다 — 클라이언트 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다. 글은 문서의 번역 키다(뷰모델은 수와 보임만 든다).
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
     * @class CardBattleHUDViewModel
     * @brief 덱 전투 HUD 의 뷰모델입니다. `CardBattleViewComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 바인딩은 바뀐 칸만 씁니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "CardGame HUD View Model" )
    class SW_KIT_CLIENT_API CardBattleHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        CardBattleHUDViewModel();
        ~CardBattleHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 내 체력입니다. */
        void setPlayerHP( int32 playerHP );
        /** @brief 내 방어입니다. */
        void setPlayerBlock( int32 playerBlock );
        /** @brief 적 체력입니다. */
        void setEnemyHP( int32 enemyHP );
        /** @brief 적이 이번 턴에 할 일의 양입니다. */
        void setEnemyIntent( int32 enemyIntent );
        /** @brief 남은 에너지입니다. */
        void setEnergy( int32 energy );
        /** @brief 턴입니다. */
        void setTurn( int32 turn );
        /** @brief 고른 카드의 비용입니다. */
        void setCost( int32 cost );
        /** @brief 뽑을 더미 장수입니다. */
        void setDrawCount( int32 drawCount );
        /** @brief 승리 표시를 보이거나 숨깁니다. */
        void setWonShown( bool bShown );
        /** @brief 패배 표시를 보이거나 숨깁니다. */
        void setLostShown( bool bShown );

        int32            getPlayerHP() const { return _playerHP; }
        int32            getPlayerBlock() const { return _playerBlock; }
        int32            getEnemyHP() const { return _enemyHP; }
        int32            getEnemyIntent() const { return _enemyIntent; }
        int32            getEnergy() const { return _energy; }
        int32            getTurn() const { return _turn; }
        int32            getCost() const { return _cost; }
        int32            getDrawCount() const { return _drawCount; }
        WidgetVisibility getWonVisibility() const { return _wonVisibility; }
        WidgetVisibility getLostVisibility() const { return _lostVisibility; }

    private:
        /** @brief 보임 여부를 HUD 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Player HP" )
        int32 _playerHP;
        PROPERTY( DisplayName = "Player Block" )
        int32 _playerBlock;
        PROPERTY( DisplayName = "Enemy HP" )
        int32 _enemyHP;
        PROPERTY( DisplayName = "Enemy Intent" )
        int32 _enemyIntent;
        PROPERTY( DisplayName = "Energy" )
        int32 _energy;
        PROPERTY( DisplayName = "Turn" )
        int32 _turn;
        PROPERTY( DisplayName = "Cost" )
        int32 _cost;
        PROPERTY( DisplayName = "Draw Count" )
        int32 _drawCount;
        PROPERTY( DisplayName = "Won Visibility" )
        WidgetVisibility _wonVisibility;
        PROPERTY( DisplayName = "Lost Visibility" )
        WidgetVisibility _lostVisibility;
    };
} // namespace sw
