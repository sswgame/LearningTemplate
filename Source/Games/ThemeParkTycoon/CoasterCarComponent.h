/**
 * @file CoasterCarComponent.h
 * @brief 코스터 차량 한 칸의 모습 — 디렉터가 돌리는 열차에서 자기 칸의 자리 · 기울기를 읽어 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class CoasterCarComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터의 열차를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 루프에서는 롤까지 따라 뒤집힙니다.
     */
    REFLECT( Category = "ThemePark", DisplayName = "Coaster Car View", Tooltip = "Places one car of a coaster train the park director runs" )
    class CoasterCarComponent : public Component
    {
    public:
        REFLECT_BODY();

        CoasterCarComponent();
        virtual ~CoasterCarComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 따라갈 디렉터 · 코스터 번호 · 칸 번호(0 이 맨 앞)를 정합니다. */
        void assignCar( GameObjectHandle director, int32 coasterIndex, int32 carIndex );

    private:
        PROPERTY( Category = "Car", DisplayName = "Director", Tooltip = "Object with the ParkDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Car", DisplayName = "Coaster Index", Tooltip = "Which built coaster" )
        int32 _coasterIndex;
        PROPERTY( Category = "Car", DisplayName = "Car Index", Tooltip = "0 is the front car" )
        int32 _carIndex;
        PROPERTY( Category = "Car", DisplayName = "Car Spacing", Tooltip = "Track distance between cars", Min = 0.1, Units = m )
        float32 _carSpacing;
    };
} // namespace sw
