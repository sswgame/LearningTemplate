/**
 * @file WildWestHUDViewModel.h
 * @brief 승마 HUD 문서가 보이는 값(탑승 상태 글 · 말 스태미나)입니다 — 디렉터 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Binding/UIViewModel.h"

namespace sw
{
    /**
     * @class WildWestHUDViewModel
     * @brief WildWest 플레이어 HUD 의 뷰모델입니다. `HUDControllerComponent` 가 소유하고 HUD 화면(`ui/hud.ui.xml`)에 겁니다.
     * @details 칸은 승마의 것이라 게임이 가집니다 — 슈터의 무기 이름 · 체력 칸을 빌려 쓰지 않습니다(`HUDControllerComponent::getOrCreateViewModel`).
     *          세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 바인딩은 바뀐 칸만 씁니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Wild West HUD View Model" )
    class WildWestHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        WildWestHUDViewModel();
        ~WildWestHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 탑승 상태 한 줄입니다(걸어 다니는 중 · 탄 말과 걸음새 · 조작 안내). 현지화하지 않은 글입니다. */
        void setRideStatus( string_view rideStatus );
        /** @brief 말 스태미나(보이는 정수 — 소수는 올림)와 비율(0..1, 막대)입니다. 말에 타지 않았으면 0 입니다. */
        void setStamina( float32 stamina, float32 ratio );

        const string& getRideStatus() const { return _rideStatus; }
        int32         getStamina() const { return _stamina; }
        float32       getStaminaRatio() const { return _staminaRatio; }

    private:
        PROPERTY( DisplayName = "Ride Status", Tooltip = "One line about the ride: on foot, or the horse, its gait and speed" )
        string _rideStatus;
        PROPERTY( DisplayName = "Stamina" )
        int32 _stamina;
        PROPERTY( DisplayName = "Stamina Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _staminaRatio;
    };
} // namespace sw
