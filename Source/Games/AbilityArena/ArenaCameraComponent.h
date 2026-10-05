/**
 * @file ArenaCameraComponent.h
 * @brief 플레이어를 비스듬히 위에서 따라가는 카메라 — 디렉터가 적은 플레이어 자리를 궤도 모드(오프셋 = 요 · 피치 · 거리)로 봅니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class ArenaCameraComponent
     * @brief 같은 오브젝트의 `CameraComponent` 를 `TickGroup::PostUpdate` 에서 둡니다(디렉터가 `PrePhysics` 에서 초점을 적은 뒤). 자기 카메라에만 씁니다.
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Camera", Tooltip = "Follows the arena player from a fixed offset" )
    class ArenaCameraComponent : public Component
    {
    public:
        REFLECT_BODY();

        ArenaCameraComponent();
        virtual ~ArenaCameraComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

    private:
        /** @brief 초점에 오프셋을 더한 자리에서 초점을 보게 둡니다. */
        void applyToCamera( const float3& focus );

    private:
        PROPERTY( Category = "Camera", DisplayName = "Director", Tooltip = "Object with the ArenaDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Camera", DisplayName = "Offset", Tooltip = "Camera position relative to the player", Units = m )
        float3 _offset;
        PROPERTY( Category = "Camera", DisplayName = "Min Far Plane", Tooltip = "Far plane is at least this far", Min = 1.0, Units = m )
        float32 _minFarPlane;
    };
} // namespace sw
