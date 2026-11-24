/**
 * @file MechArenaHUDViewModel.h
 * @brief 기체 대전 HUD 문서가 보이는 값(체력 · 부스트 · 탄창 · 두 팀 게이지 · 안내 · 결과)입니다 — 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
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
     * @class MechArenaHUDViewModel
     * @brief 기체 대전 HUD 의 뷰모델입니다. `MechArenaViewComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 글은 문서가 들고(번역 키) 여기는 수와 보임만 정합니다. 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 됩니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Mech Arena HUD View Model" )
    class SW_KIT_CLIENT_API MechArenaHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        MechArenaHUDViewModel();
        ~MechArenaHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 플레이어 기체의 체력 비율 · 남은 부스트 비율(0..1)과 주무기 탄창입니다. */
        void setPilot( float32 healthRatio, float32 boostRatio, int32 ammo );
        /** @brief 두 팀의 남은 전력 게이지입니다. */
        void setTeamGauge( int32 playerGauge, int32 enemyGauge );
        /** @brief 과열 · 격추 글을 보이거나 숨깁니다. */
        void setHints( bool bOverheated, bool bDestroyed );
        /** @brief 결과 글을 보이거나 숨깁니다(둘 다 false 면 진행 중). */
        void setResult( bool bWon, bool bLost );

    private:
        /** @brief 보임 여부를 HUD 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Health Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _healthRatio;
        PROPERTY( DisplayName = "Boost Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _boostRatio;
        PROPERTY( DisplayName = "Ammo" )
        int32 _ammo;
        PROPERTY( DisplayName = "Player Gauge" )
        int32 _playerGauge;
        PROPERTY( DisplayName = "Enemy Gauge" )
        int32 _enemyGauge;
        PROPERTY( DisplayName = "Overheat Visibility" )
        WidgetVisibility _overheatVisibility;
        PROPERTY( DisplayName = "Destroyed Visibility" )
        WidgetVisibility _destroyedVisibility;
        PROPERTY( DisplayName = "Win Visibility" )
        WidgetVisibility _winVisibility;
        PROPERTY( DisplayName = "Lose Visibility" )
        WidgetVisibility _loseVisibility;
    };
} // namespace sw
