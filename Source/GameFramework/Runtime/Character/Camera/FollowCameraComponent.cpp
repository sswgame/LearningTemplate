#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Camera/CameraMode.h"
#include "GameFramework/Runtime/Character/Camera/CameraPose.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    /**
     * @class FollowCameraComponent
     * @brief 같은 오브젝트의 `CameraComponent` 를 `TickGroup::PostUpdate` 에서 둡니다(디렉터가 `PrePhysics` 에서 초점을 적은 뒤). 자기 카메라에만 씁니다.
     */
    REFLECT( Category = "Camera", DisplayName = "Follow Camera", Tooltip = "Follows the game director focus from a fixed offset" )
    class FollowCameraComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 플레이어 컨트롤러(DuringPhysics)가 옮긴 자리가 적용된 뒤(물리 뒤 단계)에 읽는다.

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

    FollowCameraComponent::FollowCameraComponent()
        : _gameMode{}
        , _offset{ 0.0f, 15.0f, -11.0f }
        , _minFarPlane{ 120.0f }
    {
        setCanEverTick( true );
    }

    void FollowCameraComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        applyToCamera( float3{ 0.0f, 0.0f, 0.0f } );
    }

    void FollowCameraComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const GameModeComponent* pGameMode = context.readOther<GameModeComponent>( _gameMode );
        GameObjectHandle         focusObject{};
        float3                   focusPosition{ 0.0f, 0.0f, 0.0f };
        if ( pGameMode == nullptr || pGameMode->readCameraFocus( focusObject, focusPosition ) == false )
        {
            applyToCamera( float3{ 0.0f, 0.0f, 0.0f } );
            return;
        }
        // 디렉터의 초점 자리는 PrePhysics 에서 적은 틱 전 자리다 — 그대로 쓰면 한 프레임 늦게 따라간다. 이 그룹은 물리 뒤 단계라 컨트롤러의
        // 이번 프레임 쓰기가 이미 적용됐으므로 초점 오브젝트 메시의 자리를 바로 읽는다. 남의 오브젝트라 남 읽기 길로 찾는다 — 트랜스폼은 틱 중 쓰기가 단계 경계에서야 보인다.
        const MeshComponent* pMesh = context.readOther<MeshComponent>( focusObject );
        applyToCamera( pMesh != nullptr ? pMesh->getWorldPosition() : focusPosition );
    }

    void FollowCameraComponent::applyToCamera( const float3& focus )
    {
        GameObject*      pOwner  = getOwner();
        CameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        const float32    length  = _offset.getLength();
        if ( pCamera == nullptr || length <= MathUtil::kEpsilon )
            return;
        // 오프셋은 궤도 모드(`CameraPresetMode::Orbit`)의 요 · 피치 · 거리다 — 카메라는 초점에서 오프셋만큼 떨어져 초점을 본다.
        // 모드 계산은 틱 중의 월드 행렬을 읽지 않으므로(틱 안의 자리 쓰기는 틱 뒤에 보인다) 이번 프레임의 초점으로 바로 놓인다.
        CameraPresetDef def;
        def._view._mode         = CameraPresetMode::Orbit;
        def._view._distance     = length;
        def._view._pitch        = Radians::fromRadians( MathUtil::asin( MathUtil::clamp( _offset._y / length, -1.0f, 1.0f ) ) );
        def._view._yaw          = Radians::fromRadians( MathUtil::atan2( -_offset._x, -_offset._z ) );
        def._lens._fieldOfViewY = Radians::fromRadians( pCamera->getFieldOfViewY() );
        def._lens._nearPlane    = pCamera->getNearPlane();
        def._lens._farPlane     = MathUtil::max( pCamera->getFarPlane(), _minFarPlane );
        CameraTarget target;
        target._focus = focus;
        CameraPose::applyToCamera( *pCamera, evaluatePreset( def, target ) );
    }
} // namespace sw

#include "FollowCameraComponent.cpp.gen.inl"
