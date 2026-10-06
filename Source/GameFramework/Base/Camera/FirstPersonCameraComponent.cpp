#include "pch.h"

#include "GameFramework/Base/Camera/FirstPersonCameraComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Camera/CameraMode.h"
#include "GameFramework/Base/Camera/CameraPoseUtil.h"
#include "GameFramework/Base/Framework/GameService.h"

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

    FirstPersonLook FirstPersonCameraMath::computeLookAfterMouse( const FirstPersonLook& look, float32 deltaX, float32 deltaY, float32 sensitivity )
    {
        FirstPersonLook turned = look;
        turned.addMouseDelta( deltaX, deltaY, sensitivity );
        return turned;
    }

    FirstPersonCameraComponent::FirstPersonCameraComponent()
        : _yaw{ 0.0f }
        , _pitch{ 0.0f }
        , _maxPitch{ 85.0f * MathUtil::kDegreeToRadian }
        , _mouseSensitivity{ 0.0022f }
        , _lookAction{}
        , _bMouseLook{ true }
        , _bLockMouse{ true }
        , _fieldOfViewY{ 75.0f * MathUtil::kDegreeToRadian }
        , _nearPlane{ 0.05f }
        , _farPlane{ 120.0f }
        , _viewModelName{}
        , _viewModelOffset{ 0.0f, 0.0f, 0.0f }
        , _viewModelYawOffset{ 0.0f }
        , _look{}
        , _eyePosition{ 0.0f, 0.0f, 0.0f }
        , _bMouseLocked{ SW_FALSE }
        , _bLockApplied{ SW_FALSE }
        , _bLockApplyPending{ SW_FALSE }
        , _bLockInitialized{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void FirstPersonCameraComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 마우스로 돌린 시점을 몸을 움직이는 컴포넌트(같은 오브젝트, 뒤 그룹)가 같은 프레임에 쓴다.
        setTickGroup( TickGroup::PrePhysics );
        _look.setMaxPitch( _maxPitch );
        _look.setAngles( _yaw, _pitch );
        // 눈은 카메라가 놓인 자리에서 시작한다 — 몸을 움직이는 컴포넌트가 첫 틱에 넣는다.
        GameObject*            pOwner  = getOwner();
        const CameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        if ( pCamera != nullptr )
            _eyePosition = pCamera->getLocalPosition();
        _bLockInitialized = SW_FALSE;
        applyToCamera();
    }

    void FirstPersonCameraComponent::onEndPlay()
    {
        // 이 컴포넌트가 잠갔으면 푼다(끝은 틱 밖 — 게임 스레드).
        if ( _bLockApplied == SW_TRUE )
            applyMouseLock( false );
        _bMouseLocked = SW_FALSE;
        Component::onEndPlay();
    }

    void FirstPersonCameraComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const InputManager* pInput = game::getService<InputManager>();
        if ( _bMouseLook && pInput != nullptr )
        {
            updateMouseLock( *pInput );
            // 잠금이 쉬는 동안(포커스 밖 · Alt · 개발 콘솔 · 클릭 전)에는 시점을 돌리지 않는다 — 풀린 커서를 움직일 때 화면이 따라 돌면 안 된다.
            const bool bReadMouse = _bLockMouse == false || ( _bMouseLocked == SW_TRUE && pInput->isMouseLockActive() );
            if ( bReadMouse )
            {
                if ( _lookAction.empty() == false )
                {
                    const float2 lookDelta = pInput->getInputMap().getVector2D( _lookAction );
                    addMouseDelta( lookDelta._x, lookDelta._y );
                }
                else
                {
                    const int2 mouseDelta = pInput->getMouseDelta();
                    addMouseDelta( static_cast<float32>( mouseDelta._x ), static_cast<float32>( mouseDelta._y ) );
                }
            }
        }
        else if ( _bLockApplied == SW_TRUE )
        {
            scheduleMouseLockApply(); // 마우스 시점을 껐다 — 잠갔던 것을 푼다
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
        applyToCamera();
    }

    void FirstPersonCameraComponent::addRecoil( float32 pitchKick, float32 yawKick )
    {
        _look.addRecoil( pitchKick, yawKick );
        applyToCamera();
    }

    void FirstPersonCameraComponent::addMouseDelta( float32 deltaX, float32 deltaY )
    {
        _look = FirstPersonCameraMath::computeLookAfterMouse( _look, deltaX, deltaY, _mouseSensitivity );
    }

    void FirstPersonCameraComponent::setMouseLookEnabled( bool bEnabled )
    {
        _bMouseLook = bEnabled;
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
            (void)pViewModel->attachToComponent( pCamera, AttachRule::KeepRelative );
        pViewModel->setLocalPosition( _viewModelOffset );
        pViewModel->setLocalRotation( float3{ 0.0f, _viewModelYawOffset, 0.0f } );
    }

    void FirstPersonCameraComponent::updateMouseLock( const InputManager& input )
    {
        if ( _bLockMouse == false )
            return;
        if ( _bLockInitialized == SW_FALSE )
        {
            _bLockInitialized = SW_TRUE;
            _bMouseLocked     = SW_TRUE;
            scheduleMouseLockApply();
        }
        if ( input.wasKeyPressed( Key::Escape ) )
        {
            _bMouseLocked = _bMouseLocked == SW_TRUE ? SW_FALSE : SW_TRUE;
            scheduleMouseLockApply();
        }
    }

    void FirstPersonCameraComponent::scheduleMouseLockApply()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _bLockApplyPending == SW_TRUE )
            return;
        _bLockApplyPending = SW_TRUE;
        // 입력 매니저의 잠금 · 커서는 게임 스레드의 것이다 — 틱 안이면 틱 뒤로 미룬다. 그 사이 컴포넌트가 사라질 수 있으니 핸들로 다시 찾는다.
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            FirstPersonCameraComponent* pCamera = static_cast<FirstPersonCameraComponent*>( pManager->resolveComponent( self ) );
            if ( pCamera == nullptr )
                return;
            pCamera->_bLockApplyPending = SW_FALSE;
            pCamera->applyMouseLock( pCamera->_bMouseLook && pCamera->_bMouseLocked == SW_TRUE );
        } );
    }

    void FirstPersonCameraComponent::applyMouseLock( bool bLocked )
    {
        InputManager* pInput = game::getService<InputManager>();
        if ( pInput == nullptr )
            return;
        const bool bApplied = _bLockApplied == SW_TRUE;
        if ( bLocked == bApplied )
            return;
        pInput->setMouseLockMode( bLocked ? MouseLockMode::LockedInCenter : MouseLockMode::None );
        pInput->setCursorVisible( bLocked == false );
        _bLockApplied = bLocked ? SW_TRUE : SW_FALSE;
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
