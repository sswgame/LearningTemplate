// 미리보기 뷰포트의 그리기(렌더 타깃 그림 · 궤도 입력 · 축 표시)입니다 — MVVM View. 씬 · 렌더 타깃 · 카메라 계산은 `Model/EditorPreviewViewportScene.cpp` 다.
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPreviewViewport.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Viewport/Model/EditorOrbitMath.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorPreviewViewportViewInternal
        {
            static constexpr float32 kMinDistance           = 0.2f;
            static constexpr float32 kMaxPitch              = 1.5f;
            static constexpr float32 kRotateRadiansPerPixel = 0.01f;
            /** @brief 자유 카메라에서 휠 한 칸이 시선 방향으로 옮기는 거리(m)입니다. */
            static constexpr float32 kFreeMoveMetersPerWheel = 0.5f;
        };
    } // namespace

    void EditorPreviewViewport::draw( const float2& size, bool bInputActive )
    {
        const bool bHasSize   = size._x > 1.0f && size._y > 1.0f;
        const bool bHasTarget = _targetID != 0;
        if ( bHasTarget == false || bHasSize == false )
            return;
        void* const pTextureID = acquireTargetTexture( size );
        if ( pTextureID == nullptr )
            return;

        const ImVec2 imagePosition = ImGui::GetCursorScreenPos();
        _imagePosition             = float2{ imagePosition.x, imagePosition.y };
        _imageSize                 = size;
        ImGui::Image( reinterpret_cast<ImTextureID>( pTextureID ), ImVec2{ size._x, size._y } );
        const bool bImageHovered = ImGui::IsItemHovered();
        if ( bInputActive && bImageHovered )
        {
            const EditorPointerState pointer = EditorInputGUI::readPointer();
            if ( pointer.isDown( EditorMouseButton::Left ) )
            {
                _orbitYaw -= pointer._delta._x * EditorPreviewViewportViewInternal::kRotateRadiansPerPixel;
                _orbitPitch = MathUtil::clamp( _orbitPitch + pointer._delta._y * EditorPreviewViewportViewInternal::kRotateRadiansPerPixel,
                                               -EditorPreviewViewportViewInternal::kMaxPitch, EditorPreviewViewportViewInternal::kMaxPitch );
            }
            // 씬 뷰와 같은 휠 줌과 가운데 단추 팬이다(`EditorOrbitMath`). 궤도 중심이 같이 움직여 이어지는 회전이 같은 점을 돈다.
            if ( pointer._wheel != 0.0f && _bFreeCamera )
            {
                // 자유 카메라의 휠은 시선 방향으로 움직인다(줌인 = 앞으로, 휠 한 눈금 고정 거리).
                const float3 forward{ -MathUtil::sin( _orbitYaw ) * MathUtil::cos( _orbitPitch ), -MathUtil::sin( _orbitPitch ), -MathUtil::cos( _orbitYaw ) * MathUtil::cos( _orbitPitch ) };
                _freeEye += forward * ( pointer._wheel * EditorPreviewViewportViewInternal::kFreeMoveMetersPerWheel );
            }
            else if ( pointer._wheel != 0.0f )
                _orbitDistance = EditorOrbitMath::dolly( _orbitDistance, pointer._wheel, EditorPreviewViewportViewInternal::kMinDistance );
            if ( pointer.isDown( EditorMouseButton::Middle ) )
            {
                // 시선 f 는 눈에서 대상으로, 오른쪽은 위쪽 축과 f 의 외적(왼손 좌표계), 카메라 위쪽은 f 와 오른쪽의 외적이다.
                const float3  forward{ -MathUtil::sin( _orbitYaw ) * MathUtil::cos( _orbitPitch ), -MathUtil::sin( _orbitPitch ), -MathUtil::cos( _orbitYaw ) * MathUtil::cos( _orbitPitch ) };
                const float3  right = float3{ forward._z, 0.0f, -forward._x }.normalize();
                const float3  up{ forward._y * right._z, forward._z * right._x - forward._x * right._z, -forward._y * right._x };
                const float32 scale = EditorOrbitMath::panWorldPerPixel( _orbitDistance, _fieldOfViewY, size._y );
                const float3  pan   = right * ( -pointer._delta._x * scale ) + up * ( pointer._delta._y * scale );
                _orbitTarget += pan;
                if ( _bFreeCamera )
                    _freeEye += pan;
            }
        }

        submitView();
    }

    void EditorPreviewViewport::drawAxisIndicator() const
    {
        if ( _imageSize._x <= 1.0f || _imageSize._y <= 1.0f )
            return;
        // 시선 f, 오른쪽 r, 위 u 는 draw 의 팬과 같은 식이다(왼손 좌표계). 월드 축 a 의 화면 방향은 ( a·r, -a·u ) 다.
        const float3  forward{ -MathUtil::sin( _orbitYaw ) * MathUtil::cos( _orbitPitch ), -MathUtil::sin( _orbitPitch ), -MathUtil::cos( _orbitYaw ) * MathUtil::cos( _orbitPitch ) };
        const float3  right = float3{ forward._z, 0.0f, -forward._x }.normalize();
        const float3  up{ forward._y * right._z, forward._z * right._x - forward._x * right._z, -forward._y * right._x };
        const float32 length = ImGui::GetTextLineHeight() * 1.6f;
        const ImVec2  origin{ _imagePosition._x + length * 1.5f, _imagePosition._y + _imageSize._y - length * 1.5f };
        struct Axis
        {
            float3      _direction;
            const utf8* _pLabel;
            ImU32       _color;
        };
        // 테마 색 매크로는 토큰 이름을 글자 그대로 받는다(빌드 때 토큰 표와 맞춘다) — 축마다 따로 적는다.
        const Axis arrAxis[] = {
            {float3{ 1.0f, 0.0f, 0.0f }, "X", EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "AxisX" ) )},
            {float3{ 0.0f, 1.0f, 0.0f }, "Y", EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "AxisY" ) )},
            {float3{ 0.0f, 0.0f, 1.0f }, "Z", EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "AxisZ" ) )},
        };
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        for ( const Axis& axis : arrAxis )
        {
            const float32 screenX = axis._direction._x * right._x + axis._direction._y * right._y + axis._direction._z * right._z;
            const float32 screenY = -( axis._direction._x * up._x + axis._direction._y * up._y + axis._direction._z * up._z );
            const ImVec2  tip{ origin.x + screenX * length, origin.y + screenY * length };
            const ImU32   color = axis._color;
            pDrawList->AddLine( origin, tip, color, 2.0f );
            pDrawList->AddText( ImVec2{ tip.x + 2.0f, tip.y - ImGui::GetTextLineHeight() * 0.5f }, color, axis._pLabel );
        }
    }
} // namespace sw::editor
