#include "pch.h"

#include "GameFramework/Base/Control/FirstPersonCameraComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Camera/CameraMode.h"
#include "GameFramework/Base/Camera/CameraPoseUtil.h"
#include "GameFramework/Base/Control/ControlIntent.h"
#include "GameFramework/Base/Control/PawnComponent.h"
#include "GameFramework/Base/Utility/OrientationUtil.h"

namespace sw
{
    float3 FirstPersonCameraMath::computeViewModelPosition( const float3& eyePosition, const FirstPersonLook& look, const float3& offset )
    {
        // 롤이 없으니 시점의 오른쪽은 수평이고, 위는 앞 × 오른쪽이다 — 카메라 로컬 축과 같다.
        const float3 forward = look.getForward();
        const float3 right   = look.getFlatRight();
        const float3 up      = forward.cross( right );
        return eyePosition + right * offset._x + up * offset._y + forward * offset._z;
    }

    FirstPersonCameraComponent::FirstPersonCameraComponent()
        : _yaw{ 0.0f }
        , _pitch{ 0.0f }
        , _maxPitch{ 85.0f * MathUtil::kDegreeToRadian }
        , _fieldOfViewY{ 75.0f * MathUtil::kDegreeToRadian }
        , _nearPlane{ 0.05f }
        , _farPlane{ 120.0f }
        , _viewModelName{}
        , _viewModelOffset{ 0.0f, 0.0f, 0.0f }
        , _viewModelYawOffset{ 0.0f }
        , _look{}
        , _eyePosition{ 0.0f, 0.0f, 0.0f }
    {
        setCanEverTick( true );
    }

    void FirstPersonCameraComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 조종 회전(조종 시스템 단계 — 틱 앞)을 몸을 움직이는 컴포넌트(같은 오브젝트, 뒤 그룹)가 같은 프레임에 쓴다.
        setTickGroup( TickGroup::PrePhysics );
        _look.setMaxPitch( _maxPitch );
        // 시작 시점은 폰의 조종 회전으로도 넘긴다 — 조종자가 쥐면 이 값에서 이어 간다.
        setAngles( _yaw, _pitch );
        // 눈은 카메라가 놓인 자리에서 시작한다 — 몸을 움직이는 컴포넌트가 첫 틱에 넣는다.
        GameObject*            pOwner  = getOwner();
        const CameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        if ( pCamera != nullptr )
            _eyePosition = pCamera->getLocalPosition();
        applyToCamera();
    }

    void FirstPersonCameraComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const PawnComponent* pPawn = findPawn();
        if ( pPawn != nullptr )
        {
            const ControlIntent& intent = pPawn->getIntent();
            _look.setAngles( intent._controlYaw, intent._controlPitch );
        }
        applyToCamera();
    }

    void FirstPersonCameraComponent::setEyePosition( const float3& eyePosition )
    {
        _eyePosition = eyePosition;
        applyToCamera();
    }

    void FirstPersonCameraComponent::setAngles( float32 yaw, float32 pitch )
    {
        _look.setAngles( yaw, pitch );
        PawnComponent* pPawn = findPawn();
        if ( pPawn != nullptr )
            pPawn->requestControlRotation( _look.getYaw(), _look.getPitch() );
        applyToCamera();
    }

    void FirstPersonCameraComponent::addRecoil( float32 pitchKick, float32 yawKick )
    {
        const float32 yawBefore   = _look.getYaw();
        const float32 pitchBefore = _look.getPitch();
        _look.addRecoil( pitchKick, yawKick );
        // 폰이 있으면 실제로 돈 양(피치 한계에서 잘린 뒤)을 조종 회전에도 쌓는다 — 다음 틱의 시점이 조종자의 값으로 돌아와도 반동이 남는다.
        PawnComponent* pPawn = findPawn();
        if ( pPawn != nullptr )
            pPawn->addControlRotationOffset( MathUtil::wrapAngle( _look.getYaw() - yawBefore ), _look.getPitch() - pitchBefore );
        applyToCamera();
    }

    void FirstPersonCameraComponent::setViewModel( const hashed_string& componentName, const float3& offset, float32 yawOffset )
    {
        _viewModelName      = componentName;
        _viewModelOffset    = offset;
        _viewModelYawOffset = yawOffset;
        applyToCamera();
    }

    void FirstPersonCameraComponent::applyToCamera()
    {
        GameObject*      pOwner  = getOwner();
        CameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        if ( pCamera != nullptr )
        {
            // 1인칭은 카메라 모드(`CameraPresetMode::FirstPerson`)다 — 눈 자리와 시점을 대상으로 넣어 디렉터 · 데이터 프리셋과 같은 계산으로 푼다.
            // 시점의 피치는 위가 + 이고 모드의 피치는 아래가 + 다. 눈 자리는 카메라의 부모 공간 값이라 포즈도 로컬로 쓴다(부모가 움직이면 따라간다).
            CameraPresetDef def;
            def._view._mode         = CameraPresetMode::FirstPerson;
            def._lens._fieldOfViewY = _fieldOfViewY;
            def._lens._nearPlane    = _nearPlane;
            def._lens._farPlane     = _farPlane;
            CameraTarget target;
            target._focus = _eyePosition;
            target._yaw   = _look.getYaw();
            target._pitch = -_look.getPitch();
            CameraPoseUtil::applyToCameraLocal( *pCamera, evaluatePreset( def, target ) );
        }
        MeshComponent* pViewModel = findViewModel();
        if ( pViewModel == nullptr || pCamera == nullptr )
            return;
        // 카메라의 자식이면 로컬 자리만 두면 시점을 따라간다. 아니면 붙인다(틱 중이면 엔진이 틱 뒤로 미룬다).
        if ( pViewModel->getParent() != pCamera )
            (void)pViewModel->attachToComponent( pCamera, AttachRule::KeepRelative ); // 붙일 수 없는 부모면 false — 뷰모델은 제자리에 남는다
        pViewModel->setLocalPosition( _viewModelOffset );
        pViewModel->setLocalRotation( float3{ 0.0f, _viewModelYawOffset, 0.0f } );
    }

    PawnComponent* FirstPersonCameraComponent::findPawn() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
    }

    MeshComponent* FirstPersonCameraComponent::findViewModel() const
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || _viewModelName.empty() )
            return nullptr;
        MeshComponent* pFound = nullptr;
        pOwner->forEachComponentOfType<MeshComponent>( [this, &pFound]( MeshComponent* pMesh )
        {
            if ( pFound == nullptr && pMesh->getComponentName() == _viewModelName )
                pFound = pMesh;
        } );
        return pFound;
    }
} // namespace sw
