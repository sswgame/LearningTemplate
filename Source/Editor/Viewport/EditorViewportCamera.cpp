// 씬 뷰 카메라의 입력 읽기입니다(MVVM View — 비행 · 궤도 · 북마크 키). 카메라 값 · 씬 카메라 쓰기 · 북마크 저장은 `Model/EditorViewportCameraScene.cpp` 다.
#include "pch.h"

#include "Editor/Viewport/EditorViewportCamera.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Input/EditorInputActionNames.h"
#include "Editor/Common/Input/EditorInputGUI.h"

#include "Engine/Input/RawInputEvent.h"

namespace sw::editor
{
    namespace
    {
        struct EditorViewportCameraInternal
        {
            /** @brief 비행 속도(m/s)의 범위입니다. 툴바 슬라이더와 휠이 같은 범위를 쓴다. */
            static constexpr float32 kFlySpeedMin = 0.5f;
            static constexpr float32 kFlySpeedMax = 20.0f;
            /** @brief 비행 속도가 휠 한 칸에 곱해지는 비율입니다. */
            static constexpr float32 kFlySpeedWheelRatio = 1.2f;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorViewportCamera::processFlyInput( float32 deltaTime, float32& inoutCameraSpeed )
    {
        const EditorPointerState pointer = EditorInputGUI::readPointer();

        // 회전 (마우스 델타)
        _rotation._y += pointer._delta._x * 0.2f;
        _rotation._x += pointer._delta._y * 0.2f;
        _rotation._x = MathUtil::clamp( _rotation._x, -89.0f, 89.0f );

        // 이동 (WASD + QE) — 비행 레이어(`Editor.Viewport.Fly`, 오른쪽 단추를 누른 동안 켜진다)의 액션이다.
        const float32 yawRad  = MathUtil::toRadian( _rotation._y );
        const float3  forward = computeForward( _rotation );
        const float3  right   = float3{ MathUtil::cos( yawRad ), 0.0f, -MathUtil::sin( yawRad ) };
        const float3  up      = float3{ 0.0f, 1.0f, 0.0f };

        float3 moveDir{ 0.0f, 0.0f, 0.0f };
        if ( EditorInputGUI::isActionDown( editorinput::kViewportFlyForward ) )
            moveDir += forward;
        if ( EditorInputGUI::isActionDown( editorinput::kViewportFlyBack ) )
            moveDir -= forward;
        if ( EditorInputGUI::isActionDown( editorinput::kViewportFlyRight ) )
            moveDir += right;
        if ( EditorInputGUI::isActionDown( editorinput::kViewportFlyLeft ) )
            moveDir -= right;
        if ( EditorInputGUI::isActionDown( editorinput::kViewportFlyUp ) )
            moveDir += up;
        if ( EditorInputGUI::isActionDown( editorinput::kViewportFlyDown ) )
            moveDir -= up;

        // 오른쪽 버튼을 누른 채 휠로 비행 속도를 바꾼다(언리얼과 유니티). 툴바 슬라이더와 같은 값이다.
        if ( MathUtil::abs( pointer._wheel ) > 0.01f )
            inoutCameraSpeed = MathUtil::clamp( inoutCameraSpeed * MathUtil::pow( EditorViewportCameraInternal::kFlySpeedWheelRatio, pointer._wheel ), EditorViewportCameraInternal::kFlySpeedMin,
                                                EditorViewportCameraInternal::kFlySpeedMax );
        // 직교 보기에서 비행을 시작하면 원근 보기로 돌아간다. Godot 의 직교 보기와 같다. 직교로 둘러보는 것은 의미가 적기 때문이다.
        _bOrthographicView = SW_FALSE;

        const float32 speed = inoutCameraSpeed * ( pointer.hasModifier( ModifierKey::Shift ) ? 3.0f : 1.0f ) * deltaTime;
        _position += moveDir * speed;
        // 궤도 중심은 매 프레임 시선 앞 궤도 거리에 둔다. 비행 뒤 Alt 궤도가 옛 중심으로 튀지 않는다(언리얼 · 유니티).
        syncOrbitTargetToView();

        // 누르는 프레임에 새 누름으로 지우고, 키로 옮긴 프레임이 하나라도 있으면 비행이다(맥락 메뉴가 놓을 때 본다).
        // 마우스로 돌린 것은 맥락 메뉴가 끌기 거리(끌기 임계값)로 가린다 — 누를 때의 손떨림 한 픽셀로 메뉴가 막히지 않게 여기서는 세지 않는다.
        if ( pointer.wasPressed( EditorMouseButton::Right ) )
            _bFlownThisRightHold = SW_FALSE;
        const bool bMoved = moveDir._x != 0.0f || moveDir._y != 0.0f || moveDir._z != 0.0f;
        if ( bMoved )
            _bFlownThisRightHold = SW_TRUE;
    }

    void EditorViewportCamera::processOrbitInput()
    {
        const EditorPointerState pointer = EditorInputGUI::readPointer();

        // Alt + LMB: 궤도 회전
        if ( pointer.isDown( EditorMouseButton::Left ) )
        {
            _rotation._y += pointer._delta._x * 0.3f;
            _rotation._x += pointer._delta._y * 0.3f;
            _rotation._x = MathUtil::clamp( _rotation._x, -89.0f, 89.0f );
        }

        // Alt + RMB 또는 휠: 궤도 줌
        if ( pointer.isDown( EditorMouseButton::Right ) )
            _orbitDistance = MathUtil::max( _orbitDistance + ( pointer._delta._x - pointer._delta._y ) * 0.05f, kOrbitDistanceMin );

        if ( MathUtil::abs( pointer._wheel ) > 0.01f )
            dolly( pointer._wheel );

        _position = _orbitTarget - computeForward( _rotation ) * _orbitDistance;
    }

    void EditorViewportCamera::processBookmarkKeys()
    {
        static_assert( sizeof( editorinput::kArrViewportBookmark ) / sizeof( editorinput::kArrViewportBookmark[0] ) == kBookmarkKeyCount, "a bookmark action per slot" );
        static_assert( sizeof( editorinput::kArrViewportSetBookmark ) / sizeof( editorinput::kArrViewportSetBookmark[0] ) == kBookmarkKeyCount, "a bookmark action per slot" );
        // 1 ~ 9 는 북마크로 가고 Ctrl+1 ~ 9 는 지금 시점을 저장한다(`Editor.Panel.scene_view` 의 viewport.bookmarkN · viewport.setBookmarkN).
        // 수정 키는 정확히 맞아야 발화하므로 Shift · Alt 를 누른 숫자는 어느 쪽도 아니다.
        for ( uint32 slot = 0; slot < kBookmarkKeyCount; ++slot )
        {
            if ( EditorInputGUI::wasActionTriggered( editorinput::kArrViewportSetBookmark[slot] ) )
                saveBookmark( slot );
            else if ( EditorInputGUI::wasActionTriggered( editorinput::kArrViewportBookmark[slot] ) )
                goToBookmark( slot );
        }
    }
} // namespace sw::editor
