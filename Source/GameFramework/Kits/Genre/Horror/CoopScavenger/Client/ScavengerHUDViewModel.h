/**
 * @file ScavengerHUDViewModel.h
 * @brief 협동 수집 공포 HUD 문서가 보이는 값입니다 — 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
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
     * @class ScavengerHUDViewModel
     * @brief 협동 수집 공포 HUD 의 뷰모델입니다. `ScavengerAvatarComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 글은 문서의 번역 키다 — 여기는 수와 보임만 든다. 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 된다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Scavenger HUD View Model" )
    class SW_KIT_CLIENT_API ScavengerHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        ScavengerHUDViewModel();
        ~ScavengerHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 이번 주기에 더 팔아야 할 값입니다. */
        void setQuotaLeft( int32 value );
        /** @brief 마감 날까지 남은 날입니다. */
        void setDaysLeft( int32 value );
        /** @brief 우주선에 실은 고철의 값입니다. */
        void setShipValue( int32 value );
        /** @brief 회사 돈입니다. */
        void setCredits( int32 value );
        /** @brief 승무원 0 의 체력 비율입니다. */
        void setHealthRatio( float32 ratio );
        /** @brief 지금 위성에서 흐른 하루의 비율입니다. */
        void setDayRatio( float32 ratio );
        /** @brief 궤도의 안내를 보입니다. */
        void setOrbitShown( bool bShown );
        /** @brief 할당량을 채운 글을 보입니다. */
        void setWinShown( bool bShown );
        /** @brief 할당량을 못 채운 글을 보입니다. */
        void setLoseShown( bool bShown );

    private:
        /** @brief 보이면 클릭을 받지 않는 보임, 아니면 접힘입니다. */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Quota Left" )
        int32 _quotaLeft;
        PROPERTY( DisplayName = "Days Left" )
        int32 _daysLeft;
        PROPERTY( DisplayName = "Ship Value" )
        int32 _shipValue;
        PROPERTY( DisplayName = "Credits" )
        int32 _credits;
        PROPERTY( DisplayName = "Health Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _healthRatio;
        PROPERTY( DisplayName = "Day Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _dayRatio;
        PROPERTY( DisplayName = "Orbit Visibility" )
        WidgetVisibility _orbitVisibility;
        PROPERTY( DisplayName = "Win Visibility" )
        WidgetVisibility _winVisibility;
        PROPERTY( DisplayName = "Lose Visibility" )
        WidgetVisibility _loseVisibility;
    };
} // namespace sw
