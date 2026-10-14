#include "pch.h"

#include "Editor/Viewport/EditorViewportCamera.h"

#include "Core/Common/Defines.h"
#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Viewport/EditorViewportProjection.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportCameraInternal
        {
            /** @brief 에디터 카메라가 지난 프레임에 설정한 위치에서 이 거리(m)보다 멀리 떨어져 있으면 외부에서 옮긴 것으로 봅니다. 부동소수 왕복 오차보다 큰 값입니다. */
            static constexpr float32 kExternalMoveTolerance = 0.001f;

            /** @brief 회전(도, 피치 · 요)에서 시선 방향을 계산합니다. */
            static float3 computeForward( const float3& rotation )
            {
                const float32 pitchRad = MathUtil::toRadian( rotation._x );
                const float32 yawRad   = MathUtil::toRadian( rotation._y );
                return float3{ MathUtil::sin( yawRad ) * MathUtil::cos( pitchRad ), -MathUtil::sin( pitchRad ), MathUtil::cos( yawRad ) * MathUtil::cos( pitchRad ) };
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
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
        , _reserved{ 0 }
    {
    }

    void EditorViewportCamera::processFlyInput( float32 deltaTime, float32& inoutCameraSpeed )
    {
        ImGuiIO& io = ImGui::GetIO();

        // 회전 (마우스 델타)
        _rotation._y += io.MouseDelta.x * 0.2f;
        _rotation._x += io.MouseDelta.y * 0.2f;
        _rotation._x = MathUtil::clamp( _rotation._x, -89.0f, 89.0f );

        // 이동 (WASD + QE)
        const float32 yawRad  = MathUtil::toRadian( _rotation._y );
        const float3  forward = EditorViewportCameraInternal::computeForward( _rotation );
        const float3  right   = float3{ MathUtil::cos( yawRad ), 0.0f, -MathUtil::sin( yawRad ) };
        const float3  up      = float3{ 0.0f, 1.0f, 0.0f };

        float3 moveDir{ 0.0f, 0.0f, 0.0f };
        if ( ImGui::IsKeyDown( ImGuiKey_W ) )
            moveDir += forward;
        if ( ImGui::IsKeyDown( ImGuiKey_S ) )
            moveDir -= forward;
        if ( ImGui::IsKeyDown( ImGuiKey_D ) )
            moveDir += right;
        if ( ImGui::IsKeyDown( ImGuiKey_A ) )
            moveDir -= right;
        if ( ImGui::IsKeyDown( ImGuiKey_E ) )
            moveDir += up;
        if ( ImGui::IsKeyDown( ImGuiKey_Q ) )
            moveDir -= up;

        // 오른쪽 버튼을 누른 채 휠로 비행 속도를 바꾼다(언리얼과 유니티). 툴바 슬라이더와 같은 값이다.
        if ( MathUtil::abs( io.MouseWheel ) > 0.01f )
            inoutCameraSpeed = MathUtil::clamp( inoutCameraSpeed * MathUtil::pow( 1.2f, io.MouseWheel ), 0.5f, 20.0f );
        // 직교 보기에서 비행을 시작하면 원근 보기로 돌아간다. Godot 의 직교 보기와 같다. 직교로 둘러보는 것은 의미가 적기 때문이다.
        _bOrthographicView = SW_FALSE;

        const float32 speed = inoutCameraSpeed * ( io.KeyShift ? 3.0f : 1.0f ) * deltaTime;
        _position += moveDir * speed;
    }

    void EditorViewportCamera::processOrbitInput()
    {
        ImGuiIO& io = ImGui::GetIO();

        // Alt + LMB: 궤도 회전
        if ( io.MouseDown[0] )
        {
            _rotation._y += io.MouseDelta.x * 0.3f;
            _rotation._x += io.MouseDelta.y * 0.3f;
            _rotation._x = MathUtil::clamp( _rotation._x, -89.0f, 89.0f );
        }

        // Alt + RMB 또는 휠: 궤도 줌
        if ( io.MouseDown[1] )
            _orbitDistance = MathUtil::max( _orbitDistance + ( io.MouseDelta.x - io.MouseDelta.y ) * 0.05f, 0.5f );

        if ( MathUtil::abs( io.MouseWheel ) > 0.01f )
            _orbitDistance = MathUtil::max( _orbitDistance - io.MouseWheel * 1.0f, 0.5f );

        _position = _orbitTarget - EditorViewportCameraInternal::computeForward( _rotation ) * _orbitDistance;
    }

    void EditorViewportCamera::processBookmarkKeys()
    {
        ImGuiIO& io = ImGui::GetIO();
        for ( int32 keyIndex = 0; keyIndex < 9; ++keyIndex )
        {
            const ImGuiKey key = static_cast<ImGuiKey>( ImGuiKey_1 + keyIndex );
            if ( ImGui::IsKeyPressed( key, false ) )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                {
                    if ( io.KeyCtrl )
                    {
                        CameraBookmark bm{};
                        bm._position      = _position;
                        bm._rotation      = _rotation;
                        bm._orbitTarget   = _orbitTarget;
                        bm._orbitDistance = _orbitDistance;
                        fixed_string<constant::kMaxBuffer32> arrName;
                        formatstring( arrName.data(), arrName.capacity(), "POI %d", keyIndex + 1 );
                        bm._name = arrName.c_str();
                        pContext->getSubsystem<EditorWorkspace>().setCameraBookmark( static_cast<uint32>( keyIndex ), bm );
                    }
                    else if ( io.KeyAlt == false && io.KeyShift == false )
                    {
                        const CameraBookmark* pBm = pContext->getSubsystem<EditorWorkspace>().getCameraBookmark( static_cast<uint32>( keyIndex ) );
                        if ( pBm != nullptr && pBm->_bValid )
                            applyBookmark( *pBm );
                    }
                }
            }
        }
    }

    void EditorViewportCamera::applyBookmarkRequest( int32& inoutSlot )
    {
        if ( inoutSlot < 0 )
            return;
        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
        {
            const CameraBookmark* pBm = pContext->getSubsystem<EditorWorkspace>().getCameraBookmark( static_cast<uint32>( inoutSlot ) );
            if ( pBm != nullptr && pBm->_bValid )
                applyBookmark( *pBm );
        }
        inoutSlot = -1;
    }

    void EditorViewportCamera::applyToCamera( CameraComponent* pCamera )
    {
        if ( pCamera == nullptr )
            return;
        // 외부(개발 명령 bugitgo, teleport)가 에디터 카메라를 옮겼으면 씬 뷰가 그 위치와 시선을 이어받는다. 덮어쓰면 명령이 한 프레임도 유지되지 않는다.
        const bool bSameCamera   = _lastAppliedCameraID == pCamera->getComponentID();
        const bool bMovedOutside = bSameCamera && ( pCamera->getLocalPosition() - _lastAppliedPosition ).getLength() > EditorViewportCameraInternal::kExternalMoveTolerance;
        // 이 씬 뷰가 처음 쓰는 카메라가 새로 만든 자리에 있지 않으면 앞 에디터 인스턴스가 놓은 것이다(백엔드 교체 · 에디터 리로드) — 기본 포즈로 덮지 않고 이어받는다.
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
        }
        pCamera->setLocalPosition( _position );
        pCamera->setOrthographic( _bOrthographicView == SW_TRUE );
        pCamera->setOrthoHeight( _orthoHeight );
        pCamera->lookAt( _position + EditorViewportCameraInternal::computeForward( _rotation ) );
        _lastAppliedPosition = _position;
        _lastAppliedCameraID = pCamera->getComponentID();
    }

    void EditorViewportCamera::frameSelected()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        GameObject* pRaw = pContext->getSubsystem<EditorSelection>().getPrimaryObject();
        if ( pRaw == nullptr )
            return;

        SceneComponent* pSceneComp = pRaw->getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return;

        const float3 worldPos = pSceneComp->getWorldPosition();

        // 크기는 오브젝트의 월드 상자로 측정한다(`GameObject::getWorldBox`). 메시의 로컬 스케일과 콜라이더 오프셋 크기로 따로 계산하면
        // 부모가 키운 오브젝트나 단위 상자가 아닌 메시를 너무 가깝거나 멀게 잡는다.
        float32 objectRadius = 2.0f;
        AABB    objectBox{};
        if ( pRaw->getWorldBox( objectBox ) )
        {
            const float3 diagonal{ objectBox._max._x - objectBox._min._x, objectBox._max._y - objectBox._min._y, objectBox._max._z - objectBox._min._z };
            objectRadius = MathUtil::max( diagonal.getLength() * 0.5f, 0.1f );
        }

        focusOnPoint( worldPos, objectRadius );
    }

    void EditorViewportCamera::focusOnPoint( const float3& target, float32 radius )
    {
        _orbitTarget   = target;
        _orbitDistance = MathUtil::clamp( MathUtil::max( radius, 0.1f ) * 2.5f, 3.0f, 60.0f );
        _position      = _orbitTarget - EditorViewportCameraInternal::computeForward( _rotation ) * _orbitDistance;
    }

    void EditorViewportCamera::setPose( const float3& position, const float3& rotation )
    {
        _position    = position;
        _rotation    = rotation;
        _orbitTarget = _position + EditorViewportCameraInternal::computeForward( _rotation ) * _orbitDistance;
    }

    void EditorViewportCamera::setOrthographicView( float32 pitch, float32 yaw )
    {
        // 지금 보는 점(시선 앞 궤도 거리)을 축 방향에서 다시 바라본다. 화면 가운데는 그대로 유지된다.
        const float3 focus = _position + EditorViewportCameraInternal::computeForward( _rotation ) * _orbitDistance;
        _rotation._x       = pitch;
        _rotation._y       = yaw;
        _position          = focus - EditorViewportCameraInternal::computeForward( _rotation ) * _orbitDistance;
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
        _position = _orbitTarget - EditorViewportCameraInternal::computeForward( _rotation ) * _orbitDistance;
    }

    void EditorViewportCamera::getViewMatrix( float32* pOutMatrix ) const
    {
        const float4x4 viewMat = float4x4::makeLookAt( _position, _position + EditorViewportCameraInternal::computeForward( _rotation ), float3::Up );
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
        _orbitTarget   = bookmark._orbitTarget;
        _orbitDistance = bookmark._orbitDistance;
    }
} // namespace sw::editor
