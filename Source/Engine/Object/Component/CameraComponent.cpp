#include "pch.h"

#include "Engine/Object/Component/CameraComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Common/RenderTargetDeclaration.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

/**
 * @brief 시야 거리 배율입니다. 사용자 설정 `graphics.viewDistance` 가 넣고, 원근 카메라의 먼 평면에 곱한다(`getProjectionMatrix` — 절두체 컬링이 같은 평면으로 자른다).
 * @details Object 가 UserSettings 보다 아래 층이라 이 변수는 읽는 쪽인 여기에 둔다(설정은 이름으로 넣는다).
 */
SW_GLOBAL_VARIABLE( float32, gv_viewDistanceScale, 1.0f, "시야 거리 배율 — 원근 카메라의 먼 평면에 곱한다 (사용자 설정 graphics.viewDistance)" );

namespace sw
{
    CameraDepthOfFieldValues CameraDepthOfFieldValues::blend( const CameraDepthOfFieldValues& from, const CameraDepthOfFieldValues& to, float32 weight )
    {
        // 흐림은 구경(초점 길이 / f 값)에 비례하므로 f 값의 역수를 섞는다. 0 은 역수 0(핀홀)이라 켜고 끄는 블렌드가 점점 흐려지고 점점 맑아진다.
        const float32            fromOpening = from._aperture > 0.0f ? 1.0f / from._aperture : 0.0f;
        const float32            toOpening   = to._aperture > 0.0f ? 1.0f / to._aperture : 0.0f;
        const float32            opening     = MathUtil::lerp( fromOpening, toOpening, weight );
        CameraDepthOfFieldValues values;
        values._focusDistance = MathUtil::lerp( from._focusDistance, to._focusDistance, weight );
        values._aperture      = opening > 0.0f ? 1.0f / opening : 0.0f;
        values._sensorHeight  = MathUtil::lerp( from._sensorHeight, to._sensorHeight, weight );
        return values;
    }

    CameraComponent::CameraComponent()
        : _fovY{ kDefaultFovY }
        , _nearZ{ kDefaultNearZ }
        , _farZ{ kDefaultFarZ }
        , _orthoHeight{ kDefaultOrthoHeight }
        , _viewOffset{ 0.0f, 0.0f, 0.0f }
        , _priority{ 0 }
        , _role{ CameraRole::Game }
        , _renderOutput{}
        , _postProcess{}
        , _depthOfField{}
        , _lensShift{ 0.0f, 0.0f }
        , _bOrthographic{ false }
        , _cutPending{ 0 }
    {
    }

    CameraComponent* CameraComponent::findOrCreateNamed( GameObjectManager* pObjectManager, hashed_string objectName, CameraRole role,
                                                         const float3& position, const float3& lookTarget )
    {
        if ( pObjectManager == nullptr )
            return nullptr;

        GameObject* pObj = pObjectManager->findGameObjectByName( objectName );
        if ( pObj == nullptr )
            pObj = pObjectManager->createGameObject( objectName );
        if ( pObj == nullptr )
            return nullptr;

        CameraComponent* pCam = pObj->getComponent<CameraComponent>();
        if ( pCam == nullptr )
            pCam = pObj->addComponent<CameraComponent>();
        if ( pCam == nullptr )
            return nullptr;

        pCam->setRole( role );
        pCam->setLocalPosition( position );
        pCam->lookAt( lookTarget );
        pCam->setFieldOfViewY( kDefaultFovY );
        pCam->setNearPlane( kDefaultNearZ );
        pCam->setFarPlane( kDefaultFarZ );
        return pCam;
    }

    void CameraComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getCameraRegistry().add( this );
        declareRenderTexture();
    }

    void CameraComponent::setRenderOutput( const CameraRenderOutput& output )
    {
        _renderOutput = output;
        declareRenderTexture();
    }

    void CameraComponent::declareRenderTexture() const
    {
        // 렌더 텍스처의 크기를 먼저 알린다 — 그 텍스처를 읽는 머티리얼이 카메라보다 먼저 빌려도 같은 크기로 만들어지게.
        const bool bNoRenderTextureToDeclare = _renderOutput._target != CameraOutputTarget::RenderTexture || _renderOutput._renderTexture.empty() || engine::areEngineServicesBound() == false;
        if ( bNoRenderTextureToDeclare )
            return;
        engine::declareRenderTarget( _renderOutput._renderTexture, _renderOutput._renderTextureWidth, _renderOutput._renderTextureHeight );
    }

    void CameraComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getCameraRegistry().remove( this );
        SceneComponent::onUnregister( manager );
    }

    void CameraComponent::lookAt( const float3& target, const float3& up )
    {
        const float4x4 world   = getWorldMatrix();
        const float3   eye     = world.getTranslation();
        float3         forward = ( target - eye );
        const float32  lenSq   = forward.getLengthSquared();
        if ( lenSq <= MathUtil::kEpsilon )
            return;
        forward.normalize();

        const float32 yaw   = MathUtil::atan2( forward._x, forward._z );
        const float32 pitch = -MathUtil::asin( MathUtil::clamp( forward._y, -1.0f, 1.0f ) );
        (void)up;
        // 요 · 피치는 **월드** 방향이다 — 월드 행렬로 만들어 부모 기준으로 분해해 적는다(`setWorldTransform`). 그대로 로컬 회전에 넣으면
        // 돌아간 부모(플레이어에 붙인 카메라 · 회전한 리그) 아래에서 부모의 회전만큼 엉뚱한 곳을 본다.
        float3     worldScale{};
        quaternion worldRotation{};
        float3     worldTranslation{};
        world.decompose( worldScale, worldRotation, worldTranslation );
        setWorldTransform( float4x4::makeTrs( eye, quaternion::makeFromYawPitchRoll( yaw, pitch, 0.0f ), worldScale ) );
    }

    float4x4 CameraComponent::getViewMatrix() const
    {
        const float4x4 worldMat = getWorldMatrix();
        const float3   eye      = worldMat.getTranslation() + _viewOffset;
        const float3   forward  = float3::transformVector( float3( 0.0f, 0.0f, 1.0f ), worldMat );
        const float3   up       = float3::transformVector( float3( 0.0f, 1.0f, 0.0f ), worldMat );
        const float3   target   = eye + forward;
        return float4x4::makeLookAt( eye, target, up );
    }

    float3 CameraComponent::getCameraForward() const
    {
        float3 forward = float3::transformVector( float3( 0.0f, 0.0f, 1.0f ), getWorldMatrix() );
        if ( forward.getLengthSquared() <= MathUtil::kEpsilon )
            forward = float3( 0.0f, 0.0f, 1.0f );
        else
            forward.normalize();
        return forward;
    }

    float4x4 CameraComponent::getProjectionMatrix( float32 aspectRatio ) const
    {
        const float32 aspect = aspectRatio > 1e-4f ? aspectRatio : ( 16.0f / 9.0f );
        const float32 nearZ  = _nearZ > 1e-4f ? _nearZ : 0.1f;
        const float32 farZ   = _farZ > nearZ + 1e-4f ? _farZ : ( nearZ + 100.0f );

        float4x4 projection{};
        if ( _bOrthographic )
        {
            const float32 height = _orthoHeight > 1e-4f ? _orthoHeight : 10.0f;
            const float32 width  = height * aspect;
            projection           = float4x4::makeOrthographic( width, height, nearZ, farZ );
        }
        else
        {
            const float32 fov = _fovY > 1e-4f ? _fovY : 0.70f;
            // 시야 거리 사용자 설정(graphics.viewDistance)은 원근 카메라의 먼 평면을 늘이고 줄인다 — 절두체 컬링이 같은 평면으로 자른다. 직교(2D · 위에서 보기)는 데이터 그대로다.
            const float32 viewFarZ = MathUtil::max( farZ * static_cast<float32>( gv_viewDistanceScale ), nearZ + 1e-3f );
            projection             = float4x4::makePerspectiveFieldOfView( fov, aspect, nearZ, viewFarZ );
        }
        // 렌즈 시프트 — 클립 공간에서 x · y 를 w 만큼 옮긴다(clip.xy -= 2 · shift · clip.w). NDC 가 -1..1 이라 화면 비율 shift 는 NDC 2 · shift 다.
        // 원근 · 직교 모두 같은 식이다(직교는 w = 1). 행 벡터 규약(`p · M`)이라 이동 행렬을 오른쪽에 곱하고, 시프트가 0 이면 행렬이 그대로다.
        const float4x4 shift{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -2.0f * _lensShift._x, -2.0f * _lensShift._y, 0.0f, 1.0f };
        return projection * shift;
    }

    float4x4 CameraComponent::getViewProjectionMatrix( float32 aspectRatio ) const
    {
        return getViewMatrix() * getProjectionMatrix( aspectRatio );
    }

} // namespace sw
