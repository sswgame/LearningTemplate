/**
 * @file KartHUDViewModel.h
 * @brief 카트 경기 HUD 문서가 보이는 값입니다 — 클라이언트 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다. 글은 문서의 번역 키다(뷰모델은 수와 보임만 든다).
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
     * @class KartHUDViewModel
     * @brief 카트 경기 HUD 의 뷰모델입니다. `KartRaceViewComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 바인딩은 바뀐 칸만 씁니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "KartRacing HUD View Model" )
    class SW_KIT_CLIENT_API KartHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        KartHUDViewModel();
        ~KartHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 지금 순위입니다(1 부터). */
        void setPlace( int32 place );
        /** @brief 달리는 차 수입니다. */
        void setRacerCount( int32 racerCount );
        /** @brief 지금 바퀴입니다. */
        void setLap( int32 lap );
        /** @brief 마칠 바퀴 수입니다. */
        void setLapCount( int32 lapCount );
        /** @brief 속도입니다(km/h). */
        void setSpeed( int32 speed );
        /** @brief 출발까지 남은 초입니다(올림). */
        void setCountdown( int32 countdown );
        /** @brief 카운트다운을 보이거나 숨깁니다. */
        void setCountdownShown( bool bShown );
        /** @brief 아이템을 든 표시를 보이거나 숨깁니다. */
        void setItemShown( bool bShown );
        /** @brief 역주행 경고를 보이거나 숨깁니다. */
        void setWrongWayShown( bool bShown );
        /** @brief 결승 표시를 보이거나 숨깁니다. */
        void setFinishShown( bool bShown );

        int32            getPlace() const { return _place; }
        int32            getRacerCount() const { return _racerCount; }
        int32            getLap() const { return _lap; }
        int32            getLapCount() const { return _lapCount; }
        int32            getSpeed() const { return _speed; }
        int32            getCountdown() const { return _countdown; }
        WidgetVisibility getCountdownVisibility() const { return _countdownVisibility; }
        WidgetVisibility getItemVisibility() const { return _itemVisibility; }
        WidgetVisibility getWrongWayVisibility() const { return _wrongWayVisibility; }
        WidgetVisibility getFinishVisibility() const { return _finishVisibility; }

    private:
        /** @brief 보임 여부를 HUD 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Place" )
        int32 _place;
        PROPERTY( DisplayName = "Racer Count" )
        int32 _racerCount;
        PROPERTY( DisplayName = "Lap" )
        int32 _lap;
        PROPERTY( DisplayName = "Lap Count" )
        int32 _lapCount;
        PROPERTY( DisplayName = "Speed" )
        int32 _speed;
        PROPERTY( DisplayName = "Countdown" )
        int32 _countdown;
        PROPERTY( DisplayName = "Countdown Visibility" )
        WidgetVisibility _countdownVisibility;
        PROPERTY( DisplayName = "Item Visibility" )
        WidgetVisibility _itemVisibility;
        PROPERTY( DisplayName = "Wrong Way Visibility" )
        WidgetVisibility _wrongWayVisibility;
        PROPERTY( DisplayName = "Finish Visibility" )
        WidgetVisibility _finishVisibility;
    };
} // namespace sw
