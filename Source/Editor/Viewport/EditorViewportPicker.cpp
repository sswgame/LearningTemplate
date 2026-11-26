#include "pch.h"

#include "Editor/Viewport/EditorViewportPicker.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/Ray.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Viewport/EditorViewportProjection.h"

#include "Engine/Input/RawInputEvent.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportPickerInternal
        {
            static constexpr float64 kSlowHoverPickSeconds  = 0.002; ///< 이보다 오래 걸린 호버 피킹은 비싸다고 판단한다
            static constexpr float64 kSlowHoverPickInterval = 0.1;   ///< 비싸면 이만큼 간격을 둔다(초)

            /** @brief 누를 때의 수정 키로 고르는 방식을 정합니다. Ctrl 넣고 빼기 · Shift 더하기(언리얼 뷰포트 · 아웃라이너와 같다). */
            static EditorPickSelectMode readSelectMode()
            {
                const uint8 modifierMask = EditorInputGUI::getModifierMask();
                if ( ( modifierMask & ModifierKey::Ctrl ) != 0 )
                    return EditorPickSelectMode::Toggle;
                if ( ( modifierMask & ModifierKey::Shift ) != 0 )
                    return EditorPickSelectMode::Add;
                return EditorPickSelectMode::Replace;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorViewportPicker::EditorViewportPicker()
        : _hoveredObjectID{ 0 }
        , _lastHoverPickSeconds{ 0.0 }
        , _lastHoverPickTime{ 0.0 }
        , _hoverMousePos{ -1.0f, -1.0f }
        , _marqueeStart{ 0.0f, 0.0f }
        , _marqueeMode{ static_cast<uint8>( EditorPickSelectMode::Replace ) }
        , _bMarqueeArmed{ SW_FALSE }
    {
    }

    void EditorViewportPicker::processClick( const EditorViewportPickInput& input, bool bCanvasClicked, bool bGizmoBusy )
    {
        if ( bCanvasClicked == false )
            return;
        if ( bGizmoBusy )
            return;
        if ( ( EditorInputGUI::getModifierMask() & ModifierKey::Alt ) != 0 )
            return;
        const EditorPickSelectMode mode = EditorViewportPickerInternal::readSelectMode();
        EditorViewportPickHit      hit{};
        const bool                 bHit = findObjectUnderMouse( input, hit );
        EditorViewportPick::applyClick( hit, mode );
        if ( bHit )
            return;
        _bMarqueeArmed = SW_TRUE;
        _marqueeStart  = EditorInputGUI::readPointer()._position;
        _marqueeMode   = static_cast<uint8>( mode );
    }

    void EditorViewportPicker::processMarquee( const EditorViewportPickInput& input, ImDrawList* pDrawList )
    {
        if ( _bMarqueeArmed == SW_FALSE )
            return;
        const EditorPointerState pointer = EditorInputGUI::readPointer();
        const float2             minPos{ MathUtil::min( _marqueeStart._x, pointer._position._x ), MathUtil::min( _marqueeStart._y, pointer._position._y ) };
        const float2             maxPos{ MathUtil::max( _marqueeStart._x, pointer._position._x ), MathUtil::max( _marqueeStart._y, pointer._position._y ) };
        if ( pointer.isDown( EditorMouseButton::Left ) )
        {
            // 끌기 문턱 안이면 아직 클릭이다 — 상자를 보이지 않는다(누를 때의 손떨림이 상자로 보이지 않게).
            if ( pointer.isDragging( EditorMouseButton::Left ) )
            {
                const Color accent = SW_THEME_COLOR( "Base", "Accent" ); // 선택을 나타내는 상자라 강조색이다(앱 화면 규칙 — 강조색은 선택에)
                pDrawList->AddRectFilled( ImVec2{ minPos._x, minPos._y }, ImVec2{ maxPos._x, maxPos._y }, EditorColorUtil::toImU32( Color{ accent._r, accent._g, accent._b, 0.15f } ) );
                pDrawList->AddRect( ImVec2{ minPos._x, minPos._y }, ImVec2{ maxPos._x, maxPos._y }, EditorColorUtil::toImU32( accent ) );
            }
            return;
        }
        _bMarqueeArmed         = SW_FALSE;
        const bool bWasDragged = pointer.wasClickedWithoutDrag( EditorMouseButton::Left ) == false;
        if ( bWasDragged )
            EditorViewportPick::selectInRect( input, minPos, maxPos, static_cast<EditorPickSelectMode>( _marqueeMode ) );
    }

    void EditorViewportPicker::processHover( const EditorViewportPickInput& input, bool bCanvasHovered, bool bGizmoBusy )
    {
        // 호버는 마우스가 씬 뷰 위에만 있고(다른 패널이나 팝업이 마우스를 가지고 있지 않고) 드래그, 비행, 궤도, 기즈모 위가 아닐 때만 처리한다(언리얼과 유니티의 호버 강조).
        const EditorPointerState pointer     = EditorInputGUI::readPointer();
        const bool               bAnyButton  = pointer.isDown( EditorMouseButton::Left ) || pointer.isDown( EditorMouseButton::Right ) || pointer.isDown( EditorMouseButton::Middle );
        const bool               bNavigating = pointer.hasModifier( ModifierKey::Alt ) || bAnyButton;
        const bool               bBlocked    = bCanvasHovered == false || bNavigating || ImGui::IsAnyItemActive() || bGizmoBusy;
        if ( bBlocked )
        {
            _hoveredObjectID = 0;
            _hoverMousePos   = float2{ -1.0f, -1.0f };
            return;
        }
        // 마우스가 그대로면 지난 결과를 쓴다. 프레임당 많아야 한 번이고 대개는 0번이다. 카메라를 움직이는 동안은 위에서 막힌다.
        const float2 mouse = pointer._position;
        if ( mouse._x == _hoverMousePos._x && mouse._y == _hoverMousePos._y )
            return;
        // 피킹이 비싼 씬은 마우스가 움직이는 동안 0.1초에 한 번만 찾는다. 레이 피킹은 오브젝트 수에 비례하며, 8000개이면 Debug 에서 약 20 ms 가 걸린다.
        // 이때 마우스 위치를 기록하지 않고 반환하므로, 마우스가 멈춘 뒤 다음 기회에 마지막 위치로 다시 찾는다.
        const float64 now = ImGui::GetTime();
        if ( _lastHoverPickSeconds > EditorViewportPickerInternal::kSlowHoverPickSeconds && now - _lastHoverPickTime < EditorViewportPickerInternal::kSlowHoverPickInterval )
            return;
        _hoverMousePos                   = mouse;
        const int64           startNanos = MonotonicClock::nowNanoseconds();
        EditorViewportPickHit hit{};
        _hoveredObjectID = findObjectUnderMouse( input, hit ) ? hit._objectID : 0;
        // 이미 선택한 오브젝트는 호버로 취급하지 않는다. 선택 상자와 기즈모가 이미 있다. 그 위의 기즈모 핸들과 겹치는 판정도 여기서 없어진다.
        if ( _hoveredObjectID != 0 && EditorViewportPick::isSelected( _hoveredObjectID ) )
            _hoveredObjectID = 0;
        _lastHoverPickSeconds = static_cast<float64>( MonotonicClock::nowNanoseconds() - startNanos ) * 1e-9;
        _lastHoverPickTime    = now;
    }

    bool EditorViewportPicker::findObjectUnderMouse( const EditorViewportPickInput& input, EditorViewportPickHit& outHit )
    {
        Ray          pickRay{};
        const bool   bRay  = EditorViewportProjectionUtil::makeMouseRay( input._viewProj.invert(), input._canvasPos, input._canvasSize, pickRay );
        const ImVec2 mouse = ImGui::GetMousePos();
        return EditorViewportPick::findUnderMouse( input, float2{ mouse.x, mouse.y }, bRay ? &pickRay : nullptr, outHit );
    }
} // namespace sw::editor
