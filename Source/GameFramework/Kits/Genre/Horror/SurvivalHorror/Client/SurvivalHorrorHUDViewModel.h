/**
 * @file SurvivalHorrorHUDViewModel.h
 * @brief 생존 공포 HUD 문서가 보이는 값입니다 — 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
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
     * @class SurvivalHorrorHUDViewModel
     * @brief 생존 공포 HUD 의 뷰모델입니다. `SurvivalHorrorAvatarComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 글은 문서의 번역 키다 — 여기는 수와 보임만 든다. 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 된다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Survival Horror HUD View Model" )
    class SW_KIT_CLIENT_API SurvivalHorrorHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        SurvivalHorrorHUDViewModel();
        ~SurvivalHorrorHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 가방에 든 물건 수입니다. */
        void setItems( int32 value );
        /** @brief 체력 비율입니다. */
        void setHealthRatio( float32 ratio );
        /** @brief 정신력 비율입니다. */
        void setSanityRatio( float32 ratio );
        /** @brief 손전등 배터리 비율입니다. */
        void setBatteryRatio( float32 ratio );
        /** @brief 빛 없이 어둠에 있는 안내를 보입니다. */
        void setDarkShown( bool bShown );
        /** @brief 환각의 글을 보입니다. */
        void setHallucinationShown( bool bShown );
        /** @brief 전투 중의 글을 보입니다. */
        void setFightShown( bool bShown );
        /** @brief 빠져나간 글을 보입니다. */
        void setWinShown( bool bShown );
        /** @brief 죽은 글을 보입니다. */
        void setLoseShown( bool bShown );

    private:
        /** @brief 보이면 클릭을 받지 않는 보임, 아니면 접힘입니다. */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Items" )
        int32 _items;
        PROPERTY( DisplayName = "Health Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _healthRatio;
        PROPERTY( DisplayName = "Sanity Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _sanityRatio;
        PROPERTY( DisplayName = "Battery Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _batteryRatio;
        PROPERTY( DisplayName = "Dark Visibility" )
        WidgetVisibility _darkVisibility;
        PROPERTY( DisplayName = "Hallucination Visibility" )
        WidgetVisibility _hallucinationVisibility;
        PROPERTY( DisplayName = "Fight Visibility" )
        WidgetVisibility _fightVisibility;
        PROPERTY( DisplayName = "Win Visibility" )
        WidgetVisibility _winVisibility;
        PROPERTY( DisplayName = "Lose Visibility" )
        WidgetVisibility _loseVisibility;
    };
} // namespace sw
