/**
 * @file CoasterCarComponent.h
 * @brief 코스터 차량 한 칸의 모습 — 디렉터가 돌리는 열차에서 자기 칸의 자리 · 기울기를 읽어 둡니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFrameworkComponent.h"

namespace sw
{
    /**
     * @class CoasterCarComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터의 열차를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 루프에서는 롤까지 따라 뒤집힙니다.
     */
    REFLECT( Category = "ThemePark", DisplayName = "Coaster Car View", Tooltip = "Places one car of a coaster train the park game mode runs" )
    class SW_KIT_CLIENT_API CoasterCarComponent : public GameFrameworkComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 디렉터(PrePhysics)가 열차를 나아가게 한 뒤에 읽는다.

        CoasterCarComponent();
        virtual ~CoasterCarComponent() override = default;

        void onTick( TickContext& context ) override;

        /** @brief 따라갈 디렉터 · 코스터 번호 · 칸 번호(0 이 맨 앞)를 정합니다. */
        void assignCar( GameObjectHandle gameMode, int32 coasterIndex, int32 carIndex );

    private:
        PROPERTY( Category = "Car", DisplayName = "Coaster Index", Tooltip = "Which built coaster" )
        int32 _coasterIndex;
        PROPERTY( Category = "Car", DisplayName = "Car Index", Tooltip = "0 is the front car" )
        int32 _carIndex;
        PROPERTY( Category = "Car", DisplayName = "Car Spacing", Tooltip = "Track distance between cars", Min = 0.1, Units = m )
        float32 _carSpacing;
    };
} // namespace sw
