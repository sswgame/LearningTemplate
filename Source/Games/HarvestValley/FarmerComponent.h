/**
 * @file FarmerComponent.h
 * @brief 농부와 바라보는 칸 표시의 모습 — 디렉터의 농부 자리를 따라 자기 메시를 옮깁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 농부 뷰가 따라가는 것입니다. */
    ENUM()
    enum class FarmerViewKind : uint8
    {
        Farmer = 0, ///< 농부 자리
        Target,     ///< 농부가 바라보는 칸(밭 밖이면 숨긴다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class FarmerComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 디렉터는 핸들(씬의 PROPERTY)로 들고 매 프레임 풉니다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farmer View", Tooltip = "Follows the farm director farmer or the tile the farmer faces" )
    class FarmerComponent : public Component
    {
    public:
        REFLECT_BODY();

        FarmerComponent();
        virtual ~FarmerComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

    private:
        PROPERTY( Category = "Farmer", DisplayName = "Director", Tooltip = "Object with the FarmDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Farmer", DisplayName = "Height Offset", Tooltip = "Lift above the ground point", Units = m )
        float32 _heightOffset;
        PROPERTY( Category = "Farmer", DisplayName = "Kind", Tooltip = "Follow the farmer or the faced tile" )
        FarmerViewKind _kind;
    };
} // namespace sw
