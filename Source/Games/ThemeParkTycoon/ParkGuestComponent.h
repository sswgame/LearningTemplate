/**
 * @file ParkGuestComponent.h
 * @brief 손님 한 명의 모습 — 디렉터 시뮬레이션의 같은 번호 손님을 따라 자리 · 보임 · 기분 색을 맞춥니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class ParkGuestComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 디렉터는 핸들로 들고 매 프레임 풉니다.
     * @details 손님 수가 줄면 남는 칸은 숨깁니다(오브젝트를 만들고 지우지 않는 풀). 탄 손님 · 떠난 손님도 숨깁니다.
     */
    REFLECT( Category = "ThemePark", DisplayName = "Park Guest View", Tooltip = "Follows one guest of the park director's simulation" )
    class ParkGuestComponent : public Component
    {
    public:
        REFLECT_BODY();

        ParkGuestComponent();
        virtual ~ParkGuestComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 따라갈 디렉터와 손님 번호를 정합니다(디렉터가 스폰한 뒤 부른다). */
        void assignGuest( GameObjectHandle director, int32 guestIndex );

    private:
        PROPERTY( Category = "Guest", DisplayName = "Director", Tooltip = "Object with the ParkDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Guest", DisplayName = "Guest Index", Tooltip = "Slot in the simulation's guest list" )
        int32 _guestIndex;
        PROPERTY( Category = "Guest", DisplayName = "Height Offset", Tooltip = "Lift above the walking point", Units = m )
        float32 _heightOffset;

        int32 _colorBucket; ///< 지금 입은 기분 색 칸(−1 이면 아직 없다)
    };
} // namespace sw
