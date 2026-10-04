#include "pch.h"

#include "GameFramework/Components/OrthoCameraRigComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct OrthoCameraRigComponentInternal
        {
            static constexpr float32 kOverrideNearPlane = 0.05f; ///< 탑승 시점은 차 바로 앞까지 보인다
        };
    } // namespace
} // namespace sw

namespace sw
{
    float3 OrthoCameraRigMath::computeToCamera( float32 yaw, float32 pitch )
    {
        const float32 horizontal = MathUtil::cos( pitch );
        return float3{ -MathUtil::sin( yaw ) * horizontal, MathUtil::sin( pitch ), -MathUtil::cos( yaw ) * horizontal };
    }

    OrthoCameraView OrthoCameraRigMath::computeView( const float3& focus, float32 yaw, float32 pitch, float32 distance )
    {
        // 카메라는 초점의 반대쪽 위에서 초점을 본다 — 보는 방향의 요 · 피치가 곧 리그의 요 · 피치다(`lookAt` 과 같은 배치).
        OrthoCameraView view;
        view._position = focus + computeToCamera( yaw, pitch ) * distance;
        view._euler    = float3{ pitch, yaw, 0.0f };
        return view;
    }

    float3 OrthoCameraRigMath::computePanDirection( float32 yaw, float32 forwardInput, float32 rightInput )
    {
        const float3 forward{ MathUtil::sin( yaw ), 0.0f, MathUtil::cos( yaw ) };
        const float3 right{ forward._z, 0.0f, -forward._x };
        return forward * forwardInput + right * rightInput;
    }

    float32 OrthoCameraRigMath::computeZoomedHeight( float32 orthoHeight, float32 wheel, float32 zoomStep, float32 minHeight, float32 maxHeight )
    {
        if ( wheel == 0.0f )
            return orthoHeight;
        const float32 zoomed = orthoHeight * ( wheel > 0.0f ? zoomStep : 1.0f / zoomStep );
        return MathUtil::clamp( zoomed, minHeight, maxHeight );
    }

    float3 OrthoCameraRigMath::clampFocus( const float3& focus, const float3& focusMin, const float3& focusMax )
    {
        return float3{ MathUtil::clamp( focus._x, focusMin._x, focusMax._x ), focus._y, MathUtil::clamp( focus._z, focusMin._z, focusMax._z ) };
    }

    GameRay OrthoCameraRigMath::computeScreenRay( const float3& focus, float32 yaw, float32 pitch, float32 distance, float32 orthoHeight, float32 aspect,
                                                  const float2& mouseNormalized )
    {
        // 직교 시점 — 화면의 점마다 광선이 카메라 앞 방향과 나란하다. 시작점은 카메라 자리에서 화면 오른쪽 · 위로 옮긴 자리다.
        const float3  toCamera = computeToCamera( yaw, pitch );
        const float3  right{ MathUtil::cos( yaw ), 0.0f, -MathUtil::sin( yaw ) };
        const float3  up{ MathUtil::sin( pitch ) * MathUtil::sin( yaw ), MathUtil::cos( pitch ), MathUtil::sin( pitch ) * MathUtil::cos( yaw ) };
        const float32 halfHeight = orthoHeight * 0.5f;
        GameRay       ray;
        ray._origin = focus + toCamera * distance + right * ( ( mouseNormalized._x * 2.0f - 1.0f ) * halfHeight * aspect ) +
                      up * ( ( 1.0f - mouseNormalized._y * 2.0f ) * halfHeight );
        ray._direction = toCamera * -1.0f;
        return ray;
    }

    OrthoCameraRigComponent::OrthoCameraRigComponent()
        : _focus{ 0.0f, 0.0f, 0.0f }
        , _yaw{ 45.0f * MathUtil::DegreeToRadian }
        , _pitch{ 30.0f * MathUtil::DegreeToRadian }
        , _distance{ 250.0f }
        , _orthoHeight{ 110.0f }
        , _minOrthoHeight{ 25.0f }
        , _maxOrthoHeight{ 220.0f }
        , _zoomStep{ 0.85f }
        , _panSpeed{ 40.0f }
        , _panReferenceHeight{ 110.0f }
        , _rotateStep{ MathUtil::HalfPi }
        , _farPlaneScale{ 2.5f }
        , _focusMin{ 0.0f, 0.0f, 0.0f }
        , _focusMax{ 0.0f, 0.0f, 0.0f }
        , _bInputEnabled{ true }
        , _bWasdPan{ true }
        , _bClampFocus{ false }
        , _overridePosition{ 0.0f, 0.0f, 0.0f }
        , _overrideEuler{ 0.0f, 0.0f, 0.0f }
        , _overrideFieldOfViewY{ CameraComponent::kDefaultFovY }
        , _overrideFarPlane{ CameraComponent::kDefaultFarZ }
        , _bOverride{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void OrthoCameraRigComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 시점을 덮어쓰는 쪽(게임 디렉터)이 앞선 그룹에서 넣은 값을 같은 프레임에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
        applyToCamera();
    }

    void OrthoCameraRigComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bOverride == SW_FALSE && _bInputEnabled )
            applyInput( deltaTime );
        applyToCamera();
    }

    void OrthoCameraRigComponent::setViewOverride( const float3& position, const float3& euler, float32 fieldOfViewY, float32 farPlane )
    {
        _overridePosition     = position;
        _overrideEuler        = euler;
        _overrideFieldOfViewY = fieldOfViewY;
        _overrideFarPlane     = farPlane;
        _bOverride            = SW_TRUE;
    }

    void OrthoCameraRigComponent::applyInput( float32 deltaTime )
    {
        const InputManager* pInput = game::getService<InputManager>();
        if ( pInput == nullptr )
            return;
        float32    forwardInput = 0.0f;
        float32    rightInput   = 0.0f;
        const bool bWasd        = _bWasdPan;
        if ( pInput->isKeyDown( Key::Up ) || ( bWasd && pInput->isKeyDown( Key::W ) ) )
            forwardInput += 1.0f;
        if ( pInput->isKeyDown( Key::Down ) || ( bWasd && pInput->isKeyDown( Key::S ) ) )
            forwardInput -= 1.0f;
        if ( pInput->isKeyDown( Key::Right ) || ( bWasd && pInput->isKeyDown( Key::D ) ) )
            rightInput += 1.0f;
        if ( pInput->isKeyDown( Key::Left ) || ( bWasd && pInput->isKeyDown( Key::A ) ) )
            rightInput -= 1.0f;
        // 확대할수록 느리게 — 화면에서 보이는 빠르기가 같다.
        const float32 panScale = _panReferenceHeight > 0.0f ? _orthoHeight / _panReferenceHeight : 1.0f;
        const float3  pan      = OrthoCameraRigMath::computePanDirection( _yaw, forwardInput, rightInput );
        _focus                 = _focus + pan * ( _panSpeed * panScale * deltaTime );
        if ( _bClampFocus )
            _focus = OrthoCameraRigMath::clampFocus( _focus, _focusMin, _focusMax );
        if ( pInput->wasKeyPressed( Key::Q ) )
            _yaw -= _rotateStep;
        if ( pInput->wasKeyPressed( Key::E ) )
            _yaw += _rotateStep;
        _orthoHeight = OrthoCameraRigMath::computeZoomedHeight( _orthoHeight, pInput->getMouseWheel(), _zoomStep, _minOrthoHeight, _maxOrthoHeight );
    }

    void OrthoCameraRigComponent::setOrthoHeight( float32 orthoHeight )
    {
        _orthoHeight = MathUtil::clamp( orthoHeight, _minOrthoHeight, _maxOrthoHeight );
    }

    bool OrthoCameraRigComponent::findGroundPoint( const float2& mouseNormalized, float32 aspect, float32 groundHeight, float3& outPoint ) const
    {
        const GameRay ray      = OrthoCameraRigMath::computeScreenRay( _focus, _yaw, _pitch, _distance, _orthoHeight, aspect, mouseNormalized );
        float32       distance = 0.0f;
        if ( RayMath::intersectHorizontalPlane( ray, groundHeight, _distance * 3.0f, distance ) == false )
            return false;
        outPoint = ray._origin + ray._direction * distance;
        return true;
    }

    void OrthoCameraRigComponent::applyToCamera()
    {
        GameObject*      pOwner  = getOwner();
        CameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        if ( pCamera == nullptr )
            return;
        if ( _bOverride == SW_TRUE )
        {
            pCamera->setOrthographic( false );
            pCamera->setFieldOfViewY( _overrideFieldOfViewY );
            pCamera->setNearPlane( OrthoCameraRigComponentInternal::kOverrideNearPlane );
            pCamera->setFarPlane( _overrideFarPlane );
            pCamera->setLocalPosition( _overridePosition );
            pCamera->setLocalRotation( _overrideEuler );
            return;
        }
        const OrthoCameraView view = OrthoCameraRigMath::computeView( _focus, _yaw, _pitch, _distance );
        pCamera->setOrthographic( true );
        pCamera->setOrthoHeight( _orthoHeight );
        pCamera->setNearPlane( CameraComponent::kDefaultNearZ );
        pCamera->setFarPlane( _distance * _farPlaneScale );
        pCamera->setLocalPosition( view._position );
        pCamera->setLocalRotation( view._euler );
    }
} // namespace sw
