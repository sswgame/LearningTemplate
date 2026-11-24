/**
 * @file MonsterHuntHUDViewModel.h
 * @brief 몬스터 사냥 HUD 문서가 보이는 값(보상 · 체력 · 스태미나 · 오일 · 독성 · 단서 · 단계마다의 묶음 보임)입니다 — `MonsterHuntAvatarComponent` 는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Presentation/WidgetVisibility.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Component/MonsterHuntGameModeComponent.h"

namespace sw
{
    /**
     * @class MonsterHuntHUDViewModel
     * @brief 몬스터 사냥 HUD 의 뷰모델입니다. `MonsterHuntAvatarComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 글은 문서에 있다(키트 코드는 글을 짓지 않는다) — 단계마다 보이는 위젯 묶음의 보임과 수만 낸다.
     *          세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 바인딩은 바뀐 칸만 씁니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Monster Hunt HUD View Model" )
    class SW_KIT_CLIENT_API MonsterHuntHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        MonsterHuntHUDViewModel();
        ~MonsterHuntHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 이 단계의 위젯 묶음만 보이게 합니다. */
        void setPhase( MonsterHuntPhase phase );
        void setReward( int32 reward );
        /** @brief 사냥꾼의 HP 와 스태미나(보이는 정수)입니다. */
        void setHunter( int32 hp, int32 stamina );
        /** @brief 괴물의 HP 와 약점을 아는지입니다. */
        void setMonster( int32 hp, bool bWeaknessKnown );
        /** @brief 바른 오일의 남은 적중 수와 독성(보이는 정수)입니다. */
        void setAlchemy( int32 oilHits, int32 toxicity );
        /** @brief 찾은 단서 수와 지금 감각 안의 단서가 있는지입니다. */
        void setClues( int32 foundCount, bool bNearby );

    private:
        /** @brief 보임 여부를 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Reward" )
        int32 _reward;
        PROPERTY( DisplayName = "Hunter HP" )
        int32 _hunterHP;
        PROPERTY( DisplayName = "Stamina" )
        int32 _stamina;
        PROPERTY( DisplayName = "Monster HP" )
        int32 _monsterHP;
        PROPERTY( DisplayName = "Oil Hits" )
        int32 _oilHits;
        PROPERTY( DisplayName = "Toxicity" )
        int32 _toxicity;
        PROPERTY( DisplayName = "Found Clues" )
        int32 _foundClueCount;
        PROPERTY( DisplayName = "Haggle Visibility" )
        WidgetVisibility _haggleVisibility;
        PROPERTY( DisplayName = "Investigate Visibility" )
        WidgetVisibility _investigateVisibility;
        PROPERTY( DisplayName = "Sense Visibility" )
        WidgetVisibility _senseVisibility;
        PROPERTY( DisplayName = "Hunt Visibility" )
        WidgetVisibility _huntVisibility;
        PROPERTY( DisplayName = "Weakness Visibility" )
        WidgetVisibility _weaknessVisibility;
        PROPERTY( DisplayName = "Victory Visibility" )
        WidgetVisibility _victoryVisibility;
        PROPERTY( DisplayName = "Defeat Visibility" )
        WidgetVisibility _defeatVisibility;
    };
} // namespace sw
