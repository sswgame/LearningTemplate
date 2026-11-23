// 씬 뷰 카메라의 값 · 계산 · 씬 카메라 쓰기 · 북마크 저장입니다(ImGui 없음). 입력 읽기는 `EditorViewportCamera.cpp`(MVVM View)다.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Viewport/EditorOrbitMath.h"
#include "Editor/Viewport/EditorViewportCamera.h"
#include "Editor/Viewport/EditorViewportProjection.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw::editor
{
    namespace
    {
        struct EditorViewportCameraInternal
        {
            /** @brief 에디터 카메라가 지난 프레임에 설정한 위치에서 이 거리(m)보다 멀리 떨어져 있으면 외부에서 옮긴 것으로 봅니다. 부동소수 왕복 오차보다 큰 값입니다. */
            static constexpr float32 kExternalMoveTolerance = 0.001f;
            /** @brief 초점(F)이 물체 반지름에 곱해 궤도 거리를 정하는 비율입니다. 상자가 화면에 여유 있게 들어온다. */
            static constexpr float32 kFrameDistancePerRadius = 2.5f;
            /** @brief 초점이 쓰는 물체 반지름(m)의 최솟값입니다. 점 같은 물체에서 거리가 0 이 되지 않게 한다. */
            static constexpr float32 kFrameRadiusMin = 0.1f;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    float3 EditorViewportCamera::computeForward( const float3& rotation )
    {
        const float32 pitchRad = MathUtil::toRadian( rotation._x );
        const float32 yawRad   = MathUtil::toRadian( rotation._y );
        return float3{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ), MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };
    }

    EditorViewportCamera::EditorViewportCamera()
        : _position{ 0.0f, 3.0f, -6.0f }
        , _rotation{ 20.0f, 0.0f, 0.0f }
        , _orbitTarget{ 0.0f, 0.0f, 0.0f }
        , _lastAppliedPosition{ 0.0f, 0.0f, 0.0f }
        , _lastAppliedCameraID{ 0 }
        , _orbitDistance{ 8.0f }
        , _fovY{ 60.0f }
        , _nearZ{ 0.1f }
        , _farZ{ 1000.0f }
        , _orthoHeight{ 10.0f }
        , _bOrthographicView{ SW_FALSE }
        , _bFlownThisRightHold{ SW_FALSE }
        , _reserved{ 0 }
    {
        syncOrbitTargetToView(); // 처음 Alt 궤도도 지금 보는 점을 돈다(옛 원점이 아니라)
    }

    void EditorViewportCamera::syncOrbitTargetToView()
    {
        _orbitTarget = _position + computeForward( _rotation ) * _orbitDistance;
    }

    void EditorViewportCamera::dolly( float32 mouseWheel )
    {
        _orbitDistance = EditorOrbitMath::dolly( _orbitDistance, mouseWheel, kOrbitDistanceMin );
        _position      = _orbitTarget - computeForward( _rotation ) * _orbitDistance;
    }

    void EditorViewportCamera::pan( float32 pixelX, float32 pixelY, float32 viewportHeightPixels )
    {
        if ( viewportHeightPixels <= 0.0f )
            return; // 뷰포트 높이를 모르면 픽셀을 월드 길이로 바꿀 수 없다
        const float32 yawRad   = MathUtil::toRadian( _rotation._y );
        const float32 pitchRad = MathUtil::toRadian( _rotation._x );
        const float32 sinYaw   = MathUtil::sin( yawRad );
        const float32 cosYaw   = MathUtil::cos( yawRad );
        const float3  right{ cosYaw, 0.0f, -sinYaw };
        // 시선과 오른쪽에 직교하는 카메라 위쪽(왼손 좌표계)
        const float3 up{ sinYaw * MathUtil::sin( pitchRad ), MathUtil::cos( pitchRad ), cosYaw * MathUtil::sin( pitchRad ) };
        // 픽셀 하나의 월드 길이: 직교는 화면 높이 / 픽셀 높이, 원근은 궤도 중심 깊이에서의 화면 높이 / 픽셀 높이
        const float32 scale = _bOrthographicView == SW_TRUE ? _orthoHeight / viewportHeightPixels : EditorOrbitMath::panWorldPerPixel( _orbitDistance, MathUtil::toRadian( _fovY ), viewportHeightPixels );
        // 끈 방향으로 장면이 따라오므로 카메라는 반대로 움직인다. 궤도 중심도 같이 옮겨 이어지는 Alt 궤도가 같은 점을 돈다.
        const float3 delta = right * ( -pixelX * scale ) + up * ( pixelY * scale );
        _position += delta;
        _orbitTarget += delta;
    }

    void EditorViewportCamera::saveBookmark( uint32 slot )
    {
        CameraBookmark bookmark{};
        bookmark._position      = _position;
        bookmark._rotation      = _rotation;
        bookmark._orbitDistance = _orbitDistance;
        fixed_string<constant::buffer::kMax32> name;
        formatstring( name.data(), name.capacity(), "POI %#", slot + 1 );
        bookmark._name = name.c_str();
        editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().setCameraBookmark( slot, bookmark );
    }

    void EditorViewportCamera::goToBookmark( uint32 slot )
    {
        const CameraBookmark* pBookmark = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().getCameraBookmark( slot );
        if ( pBookmark != nullptr && pBookmark->_bValid )
            applyBookmark( *pBookmark );
    }

    void EditorViewportCamera::applyBookmarkRequest( optional<uint32>& inoutSlot )
    {
        if ( inoutSlot.has_value() == false )
            return;
        goToBookmark( *inoutSlot );
        inoutSlot = nullopt;
    }

    void EditorViewportCamera::applyToCamera( CameraComponent* pCamera )
    {
        if ( pCamera == nullptr )
            return;
        // 외부(개발 명령 bugreport.goto, teleport)가 에디터 카메라를 옮겼으면 씬 뷰가 그 위치와 시선을 이어받는다. 덮어쓰면 명령이 한 프레임도 유지되지 않는다.
        const bool bSameCamera   = _lastAppliedCameraID == pCamera->getComponentID();
        const bool bMovedOutside = bSameCamera && ( pCamera->getLocalPosition() - _lastAppliedPosition ).getLength() > EditorViewportCameraInternal::kExternalMoveTolerance;
        // 이 씬 뷰가 처음 쓰는 카메라가 새로 만든 위치에 있지 않으면 앞 에디터 인스턴스가 놓은 것이다(백엔드 교체 · 에디터 리로드) — 기본 포즈로 덮지 않고 이어받는다.
        const bool bFirstApply  = _lastAppliedCameraID == 0;
        const bool bPlacedEarly = bFirstApply && ( pCamera->getLocalPosition() - EditorCamera::getSpawnPosition() ).getLength() > EditorViewportCameraInternal::kExternalMoveTolerance;
        if ( bMovedOutside || bPlacedEarly )
        {
            const float3  forward = pCamera->getCameraForward();
            const float32 length  = forward.getLength();
            _position             = pCamera->getLocalPosition();
            if ( length > 0.0f )
            {
                _rotation._x = MathUtil::toDegree( MathUtil::asin( MathUtil::clamp( -forward._y / length, -1.0f, 1.0f ) ) );
                _rotation._y = MathUtil::toDegree( MathUtil::atan2( forward._x, forward._z ) );
            }
            // 이어받은 시점에서도 궤도 중심은 시선 앞 궤도 거리다(옛 중심이 남으면 다음 Alt 궤도가 튄다).
            syncOrbitTargetToView();
        }
        pCamera->setLocalPosition( _position );
        pCamera->setOrthographic( _bOrthographicView == SW_TRUE );
        pCamera->setOrthoHeight( _orthoHeight );
        pCamera->lookAt( _position + computeForward( _rotation ) );
        _lastAppliedPosition = _position;
        _lastAppliedCameraID = pCamera->getComponentID();
    }

    void EditorViewportCamera::frameSelected()
    {
        vector<GameObject*> listSelected;
        editor::getMainSceneSelection().getSelectedObjects( listSelected );

        // 크기는 선택 전체의 월드 상자를 합쳐 측정한다(`GameObject::getWorldBox`). 하나만 보면 여러 개를 골랐을 때 일부가 화면 밖에 남는다.
        // 상자가 없는 오브젝트(빈 오브젝트)는 위치 한 점으로 넣는다.
        AABB bounds = AABB::infinite();
        bool bAny   = false;
        for ( const GameObject* pObject : listSelected )
        {
            if ( pObject == nullptr )
                continue;
            AABB box{};
            if ( pObject->getWorldBox( box ) == false )
            {
                const SceneComponent* pSceneComp = pObject->getPrimarySceneComponent();
                if ( pSceneComp == nullptr )
                    continue;
                const float3 point = pSceneComp->getWorldPosition();
                box                = AABB{ point, point };
            }
            bounds = bAny ? bounds.unionWith( box ) : box;
            bAny   = true;
        }
        if ( bAny == false )
            return;

        const float3 diagonal = bounds._max - bounds._min;
        focusOnPoint( bounds.getCenter(), diagonal.getLength() * 0.5f );
    }

    void EditorViewportCamera::focusOnPoint( const float3& target, float32 radius )
    {
        _orbitTarget = target;
        // 거리는 경계 반지름에 비례한다. 작은 물체에는 가까이, 큰 지형에는 멀리 물러난다(고정 범위로 자르지 않는다).
        _orbitDistance = MathUtil::max( MathUtil::max( radius, EditorViewportCameraInternal::kFrameRadiusMin ) * EditorViewportCameraInternal::kFrameDistancePerRadius,
                                        kOrbitDistanceMin );
        _position      = _orbitTarget - computeForward( _rotation ) * _orbitDistance;
    }

    void EditorViewportCamera::resetToDefault()
    {
        const uint64 lastAppliedCameraID = _lastAppliedCameraID;
        const float3 lastAppliedPosition = _lastAppliedPosition;
        *this                            = EditorViewportCamera{};
        _lastAppliedCameraID             = lastAppliedCameraID;
        _lastAppliedPosition             = lastAppliedPosition;
    }

    void EditorViewportCamera::setPose( const float3& position, const float3& rotation )
    {
        _position = position;
        _rotation = rotation;
        syncOrbitTargetToView();
    }

    void EditorViewportCamera::setOrthographicView( float32 pitch, float32 yaw )
    {
        // 지금 보는 점(시선 앞 궤도 거리)을 축 방향에서 다시 바라본다. 화면 가운데는 그대로 유지된다.
        const float3 focus = _position + computeForward( _rotation ) * _orbitDistance;
        _rotation._x       = pitch;
        _rotation._y       = yaw;
        _position          = focus - computeForward( _rotation ) * _orbitDistance;
        _orbitTarget       = focus;
        _bOrthographicView = SW_TRUE;
    }

    void EditorViewportCamera::toggleOrthographic()
    {
        _bOrthographicView = _bOrthographicView == SW_TRUE ? SW_FALSE : SW_TRUE;
    }

    void EditorViewportCamera::zoomOrthographic( float32 mouseWheel )
    {
        // 직교 보기에서는 휠이 확대와 축소다
        _orthoHeight = MathUtil::clamp( _orthoHeight * MathUtil::pow( 0.9f, mouseWheel ), 0.5f, 1000.0f );
    }

    void EditorViewportCamera::orbitTo( const float3& rotation )
    {
        _rotation = rotation;
        _position = _orbitTarget - computeForward( _rotation ) * _orbitDistance;
    }

    void EditorViewportCamera::getViewMatrix( float32* pOutMatrix ) const
    {
        const float4x4 viewMat = float4x4::makeLookAt( _position, _position + computeForward( _rotation ), float3::Up );
        EditorViewportProjectionUtil::storeGizmoMatrix( pOutMatrix, viewMat );
    }

    void EditorViewportCamera::getProjectionMatrix( float32* pOutMatrix, float32 aspect ) const
    {
        const float32  effectiveAspect = aspect > 0.001f ? aspect : 1.0f;
        const float4x4 projMat         = float4x4::makePerspectiveFieldOfView( MathUtil::toRadian( _fovY ), effectiveAspect, _nearZ, _farZ );
        EditorViewportProjectionUtil::storeGizmoMatrix( pOutMatrix, projMat );
    }

    void EditorViewportCamera::applyBookmark( const CameraBookmark& bookmark )
    {
        _position      = bookmark._position;
        _rotation      = bookmark._rotation;
        _orbitDistance = bookmark._orbitDistance;
        // 궤도 중심은 저장하지 않고 위치 · 회전 · 거리에서 구한다.
        syncOrbitTargetToView();
    }
} // namespace sw::editor
