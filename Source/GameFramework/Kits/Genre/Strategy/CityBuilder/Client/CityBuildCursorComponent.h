/**
 * @file CityBuildCursorComponent.h
 * @brief 짓기 커서의 모습 — 짓기 입력(`CityBuildInputComponent`)이 고른 포인터 아래 칸에 고른 것의 크기만 한 반투명 상자를 둡니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFrameworkComponent.h"

namespace sw
{
    /**
     * @class CityBuildCursorComponent
     * @brief `TickGroup::PostUpdate` 에서 짓기 입력(디렉터와 같은 오브젝트 — `_gameMode`)을 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 커서가 땅 밖이면 숨깁니다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "City Build Cursor", Tooltip = "Shows the build cursor of the City Build Input on the game mode object" )
    class SW_KIT_CLIENT_API CityBuildCursorComponent : public GameFrameworkComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 짓기 입력은 틱 뒤에 커서 칸을 정한다 — 지난 프레임 것을 읽는다.

        CityBuildCursorComponent();
        virtual ~CityBuildCursorComponent() override = default;

        void onTick( TickContext& context ) override;

    private:
        PROPERTY( Category = "Cursor", DisplayName = "Height", Tooltip = "Box centre above the ground", Units = m )
        float32 _height;
        PROPERTY( Category = "Cursor", DisplayName = "Thickness", Tooltip = "Box height", Units = m )
        float32 _thickness;
    };
} // namespace sw
