/**
 * @file MonsterCollectorHUDViewModel.h
 * @brief 몬스터 수집 HUD 문서가 보이는 값(나선 개체 · 상대 · 고른 기술 · 볼 · 잡은 수 · 단계마다의 묶음 보임)입니다 — `MonsterCollectorAvatarComponent` 는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Presentation/WidgetVisibility.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Component/MonsterCollectorGameModeComponent.h"

namespace sw
{
    /**
     * @class MonsterCollectorHUDViewModel
     * @brief 몬스터 수집 HUD 의 뷰모델입니다. `MonsterCollectorAvatarComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 글은 문서에 있다(키트 코드는 글을 짓지 않는다) — 이름 칸은 카탈로그의 원문(현지화 키)이고 나머지 글은 단계마다 보이는 위젯 묶음이다.
     *          세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 바인딩은 바뀐 칸만 씁니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Monster Collector HUD View Model" )
    class SW_KIT_CLIENT_API MonsterCollectorHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        MonsterCollectorHUDViewModel();
        ~MonsterCollectorHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 이 단계의 위젯 묶음만 보이게 합니다. @p bAwaitingSwitch 면 전투 안내 대신 교체 안내입니다. */
        void setPhase( MonsterCollectorPhase phase, bool bAwaitingSwitch );
        /** @brief 나선 개체의 종 이름(현지화 키) · 레벨 · HP 입니다. */
        void setLead( string_view name, int32 level, int32 hp, int32 maxHP );
        /** @brief 야생 상대의 종 이름(현지화 키) · 레벨 · HP 입니다. */
        void setFoe( string_view name, int32 level, int32 hp );
        /** @brief 고른 기술의 이름(현지화 키)입니다. */
        void setMoveName( string_view name );
        void setCapture( int32 remainingBallCount, int32 captureCount, int32 captureGoal );
        /** @brief 쉼터 안내를 보이거나 숨깁니다. */
        void setRestShown( bool bShown );

    private:
        /** @brief 보임 여부를 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Lead Name", Tooltip = "Localization key (the source text when the table has no entry)" )
        string _leadName;
        PROPERTY( DisplayName = "Foe Name", Tooltip = "Localization key (the source text when the table has no entry)" )
        string _foeName;
        PROPERTY( DisplayName = "Move Name", Tooltip = "Localization key (the source text when the table has no entry)" )
        string _moveName;
        PROPERTY( DisplayName = "Lead Level" )
        int32 _leadLevel;
        PROPERTY( DisplayName = "Lead HP" )
        int32 _leadHP;
        PROPERTY( DisplayName = "Lead Max HP" )
        int32 _leadMaxHP;
        PROPERTY( DisplayName = "Foe Level" )
        int32 _foeLevel;
        PROPERTY( DisplayName = "Foe HP" )
        int32 _foeHP;
        PROPERTY( DisplayName = "Remaining Balls" )
        int32 _remainingBallCount;
        PROPERTY( DisplayName = "Capture Count" )
        int32 _captureCount;
        PROPERTY( DisplayName = "Capture Goal" )
        int32 _captureGoal;
        PROPERTY( DisplayName = "Explore Visibility" )
        WidgetVisibility _exploreVisibility;
        PROPERTY( DisplayName = "Rest Visibility" )
        WidgetVisibility _restVisibility;
        PROPERTY( DisplayName = "Battle Visibility" )
        WidgetVisibility _battleVisibility;
        PROPERTY( DisplayName = "Command Visibility" )
        WidgetVisibility _commandVisibility;
        PROPERTY( DisplayName = "Switch Visibility" )
        WidgetVisibility _switchVisibility;
        PROPERTY( DisplayName = "Victory Visibility" )
        WidgetVisibility _victoryVisibility;
        PROPERTY( DisplayName = "Defeat Visibility" )
        WidgetVisibility _defeatVisibility;
    };
} // namespace sw
