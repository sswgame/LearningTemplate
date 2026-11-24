/**
 * @file JRPGBattleHUDViewModel.h
 * @brief 클래식 JRPG HUD 문서가 보이는 값(파티 · 명령 멤버 · 고른 적 · 돈 · 단계마다의 묶음 보임)입니다 — `JRPGAvatarComponent` 는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Presentation/WidgetVisibility.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/Component/JRPGGameModeComponent.h"

namespace sw
{
    /**
     * @class JRPGBattleHUDViewModel
     * @brief 클래식 JRPG HUD 의 뷰모델입니다. `JRPGAvatarComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 글은 문서에 있다(키트 코드는 글을 짓지 않는다) — 이름 칸은 카탈로그의 원문(현지화 키)이고 나머지 글은 단계마다 보이는 위젯 묶음이다.
     *          세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 바인딩은 바뀐 칸만 씁니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "JRPG Battle HUD View Model" )
    class SW_KIT_CLIENT_API JRPGBattleHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        JRPGBattleHUDViewModel();
        ~JRPGBattleHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 이 단계의 위젯 묶음만 보이게 합니다. */
        void setPhase( JRPGGamePhase phase );
        /** @brief 명령을 받는 멤버(걷는 동안은 맨 앞 멤버)의 이름(현지화 키) · HP · MP 입니다. */
        void setMember( string_view name, int32 hp, int32 maxHP, int32 mp );
        /** @brief 고른 적의 이름(현지화 키)과 HP 입니다. */
        void setTarget( string_view name, int32 hp );
        void setGold( int32 gold );
        /** @brief 누를 때까지의 비율과 지금 차례가 막기인지입니다. */
        void setTiming( float32 ratio, bool bBlock );
        /** @brief 맨 앞 멤버의 공격력입니다. */
        void setLeaderAttack( int32 attack );
        /** @brief 쉼터 안내를 보이거나 숨깁니다. */
        void setRestShown( bool bShown );

        const string&    getMemberName() const { return _memberName; }
        const string&    getTargetName() const { return _targetName; }
        int32            getMemberHP() const { return _memberHP; }
        WidgetVisibility getBattleVisibility() const { return _battleVisibility; }

    private:
        /** @brief 보임 여부를 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Member Name", Tooltip = "Localization key (the source text when the table has no entry)" )
        string _memberName;
        PROPERTY( DisplayName = "Target Name", Tooltip = "Localization key (the source text when the table has no entry)" )
        string _targetName;
        PROPERTY( DisplayName = "Member HP" )
        int32 _memberHP;
        PROPERTY( DisplayName = "Member Max HP" )
        int32 _memberMaxHP;
        PROPERTY( DisplayName = "Member MP" )
        int32 _memberMP;
        PROPERTY( DisplayName = "Target HP" )
        int32 _targetHP;
        PROPERTY( DisplayName = "Gold" )
        int32 _gold;
        PROPERTY( DisplayName = "Leader Attack" )
        int32 _leaderAttack;
        PROPERTY( DisplayName = "Timing Ratio" )
        float32 _timingRatio;
        PROPERTY( DisplayName = "Timing Visibility" )
        WidgetVisibility _timingVisibility;
        PROPERTY( DisplayName = "Swing Visibility" )
        WidgetVisibility _swingVisibility;
        PROPERTY( DisplayName = "Block Visibility" )
        WidgetVisibility _blockVisibility;
        PROPERTY( DisplayName = "Explore Visibility" )
        WidgetVisibility _exploreVisibility;
        PROPERTY( DisplayName = "Rest Visibility" )
        WidgetVisibility _restVisibility;
        PROPERTY( DisplayName = "Battle Visibility" )
        WidgetVisibility _battleVisibility;
        PROPERTY( DisplayName = "Victory Visibility" )
        WidgetVisibility _victoryVisibility;
        PROPERTY( DisplayName = "Defeat Visibility" )
        WidgetVisibility _defeatVisibility;
    };
} // namespace sw
