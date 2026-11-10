/**
 * @file FollowCameraComponent.h
 * @brief 초점을 비스듬히 위에서 따라가는 카메라 — 디렉터가 답하는 초점(`GameModeComponent::readCameraFocus`)을 궤도 모드(오프셋 = 요 · 피치 · 거리)로 봅니다.
 * @details AbilityArena 의 `FollowCameraComponent` 를 옮겼다 — 초점만 디렉터에게 묻고 오프셋은 씬 데이터라 어느 게임이든 쓴다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class FollowCameraComponent
     * @brief 같은 오브젝트의 `CameraComponent` 를 `TickGroup::PostUpdate` 에서 둡니다(디렉터가 `PrePhysics` 에서 초점을 적은 뒤). 자기 카메라에만 씁니다.
     */
    REFLECT( Category = "Camera", DisplayName = "Follow Camera", Tooltip = "Follows the game director focus from a fixed offset" )
    class SW_GF_API FollowCameraComponent : public Component
    {
    public:
        REFLECT_BODY();

        FollowCameraComponent();
        virtual ~FollowCameraComponent() override = default;

        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

    private:
        /** @brief 초점에 오프셋을 더한 자리에서 초점을 보게 둡니다. */
        void applyToCamera( const float3& focus );

    private:
        PROPERTY( Category = "Camera", DisplayName = "Director", Tooltip = "Object with the game director that answers the camera focus" )
        GameObjectHandle _gameMode;
        PROPERTY( Category = "Camera", DisplayName = "Offset", Tooltip = "Camera position relative to the focus", Units = m )
        float3 _offset;
        PROPERTY( Category = "Camera", DisplayName = "Min Far Plane", Tooltip = "Far plane is at least this far", Min = 1.0, Units = m )
        float32 _minFarPlane;
    };
} // namespace sw
