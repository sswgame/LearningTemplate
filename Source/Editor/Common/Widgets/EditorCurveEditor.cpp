#include "pch.h"

#include "Editor/Common/Widgets/EditorCurveEditor.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorCurveEditorInternal
        {
            static constexpr float32 kKeyRadius        = 4.5f;  ///< 키 점 반지름(픽셀, 배율 1)
            static constexpr float32 kPickRadius       = 8.0f;  ///< 키 · 손잡이를 맞히는 반경
            static constexpr float32 kTangentLength    = 40.0f; ///< 접선 손잡이 길이
            static constexpr float32 kGridMinPixels    = 48.0f; ///< 격자 줄 사이 최소 픽셀
            static constexpr float32 kPreviewSamples   = 64.0f; ///< 미리보기 꺾은선 점 수
            static constexpr float32 kWheelZoomPerStep = 1.15f;

            static ImVec2 toImVec2( const float2& value ) { return ImVec2{ value._x, value._y }; }
            static float2 toFloat2( const ImVec2& value ) { return float2{ value.x, value.y }; }

            /** @brief 이름표를 사각형 하나에 남깁니다(상호작용 없는 항목 — 캔버스의 누름을 뺏지 않는다). */
            static void noteRectMark( const utf8* pKey, const float2& center, float32 radius )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                const ImRect rect{
                    ImVec2{center._x - radius, center._y - radius},
                    ImVec2{center._x + radius, center._y + radius}
                };
                ImGui::ItemAdd( rect, 0, nullptr, ImGuiItemFlags_NoNav );
                EditorSelfTestMarks::note( pKey );
            }

            /** @brief 보이는 시간 범위를 픽셀 두 칸마다 표본으로 꺾은선을 그립니다. */
            static void drawCurveLine( ImDrawList& drawList, const EditorCurveView& view, const FloatCurve& curve, ImU32 color, float32 thickness, float32 pixelStep )
            {
                if ( curve._listKey.empty() )
                    return;
                const uint32 sampleCount = static_cast<uint32>( MathUtil::max( view._frameSize._x / pixelStep, 2.0f ) );
                ImVec2       previous{};
                for ( uint32 sampleIndex = 0; sampleIndex <= sampleCount; ++sampleIndex )
                {
                    const float32 time  = view._timeMin + ( view._timeMax - view._timeMin ) * static_cast<float32>( sampleIndex ) / static_cast<float32>( sampleCount );
                    const ImVec2  point = toImVec2( view.toScreen( time, curve.evaluate( time ) ) );
                    if ( sampleIndex > 0 )
                        drawList.AddLine( previous, point, color, thickness );
                    previous = point;
                }
            }

            /** @brief 눈금 간격 @p step 에 맞는 소수 자리 꼴입니다(1 이상은 정수, 0.1 은 한 자리 …). */
            static const utf8* getTickFormat( float32 step )
            {
                if ( step >= 1.0f )
                    return "%.0f";
                if ( step >= 0.1f )
                    return "%.1f";
                if ( step >= 0.01f )
                    return "%.2f";
                return "%.3f";
            }

            /** @brief 격자(시간 · 값 눈금)와 눈금 글을 그립니다. */
            static void drawGrid( ImDrawList& drawList, const EditorCurveView& view )
            {
                const ImU32                          lineColor = ImGui::GetColorU32( ImGuiCol_Border );
                const ImU32                          textColor = ImGui::GetColorU32( ImGuiCol_TextDisabled );
                const float32                        scale     = EditorThemeUtil::getDpiScale();
                const float32                        left      = view._frameMin._x;
                const float32                        top       = view._frameMin._y;
                const float32                        right     = left + view._frameSize._x;
                const float32                        bottom    = top + view._frameSize._y;
                fixed_string<constant::kMaxBuffer32> label;

                const float32 timeStep = EditorCurveView::computeGridStep( view._timeMax - view._timeMin, view._frameSize._x, kGridMinPixels * scale );
                for ( float32 time = std::ceil( view._timeMin / timeStep ) * timeStep; time <= view._timeMax; time += timeStep )
                {
                    const float32 x = view.toScreen( time, 0.0f )._x;
                    drawList.AddLine( ImVec2{ x, top }, ImVec2{ x, bottom }, lineColor );
                    formatstring( label.data(), label.capacity(), getTickFormat( timeStep ), MathUtil::abs( time ) < timeStep * 0.01f ? 0.0f : time );
                    drawList.AddText( ImVec2{ x + 2.0f, bottom - ImGui::GetTextLineHeight() }, textColor, label.c_str() );
                }
                const float32 valueStep = EditorCurveView::computeGridStep( view._valueMax - view._valueMin, view._frameSize._y, kGridMinPixels * scale );
                for ( float32 value = std::ceil( view._valueMin / valueStep ) * valueStep; value <= view._valueMax; value += valueStep )
                {
                    const float32 y = view.toScreen( 0.0f, value )._y;
                    drawList.AddLine( ImVec2{ left, y }, ImVec2{ right, y }, lineColor );
                    formatstring( label.data(), label.capacity(), getTickFormat( valueStep ), MathUtil::abs( value ) < valueStep * 0.01f ? 0.0f : value );
                    drawList.AddText( ImVec2{ left + 2.0f, y - ImGui::GetTextLineHeight() }, textColor, label.c_str() );
                }
            }

            /** @brief 키 @p index 의 시각을 이웃 키 사이로 묶습니다 — 끄는 동안 순서가 바뀌지 않게(언리얼도 키가 이웃을 넘지 못한다). */
            static float32 clampKeyTime( const FloatCurve& curve, uint32 index, float32 time )
            {
                constexpr float32 kGap = 1e-3f;
                if ( index > 0 )
                    time = MathUtil::max( time, curve._listKey[index - 1]._time + kGap );
                if ( index + 1 < curve._listKey.size() )
                    time = MathUtil::min( time, curve._listKey[index + 1]._time - kGap );
                return time;
            }

            /** @brief 고른 키의 시각 · 값 · 보간 칸입니다. 입력을 마쳤으면 true 입니다. */
            static bool drawSelectedKeyFields( EditorCurveEditorState& state )
            {
                FloatCurve& curve = state._working;
                if ( state._selectedKey >= curve._listKey.size() )
                {
                    ImGui::TextDisabled( "Click a key to edit it. Right-click to add a key. F fits the view." );
                    return false;
                }
                FloatCurveKey& key       = curve._listKey[state._selectedKey];
                bool           bFinished = false;
                const float32  width     = 110.0f * EditorThemeUtil::getDpiScale();
                // 이름을 칸 앞에 둔다(ImGui 기본은 칸 뒤라 "0.000 Time 0.240 Value" 처럼 이름과 값이 어긋나 읽힌다).
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted( "Time" );
                ImGui::SameLine();
                ImGui::SetNextItemWidth( width );
                ImGui::InputFloat( "##time", &key._time, 0.0f, 0.0f, "%.3f" );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                {
                    key._time = clampKeyTime( curve, state._selectedKey, key._time );
                    bFinished = true;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted( "Value" );
                ImGui::SameLine();
                ImGui::SetNextItemWidth( width );
                ImGui::InputFloat( "##value", &key._value, 0.0f, 0.0f, "%.3f" );
                bFinished = bFinished || ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SameLine();
                ImGui::TextUnformatted( "Interpolation" );
                ImGui::SameLine();
                ImGui::SetNextItemWidth( width );
                const utf8* const arrName[] = { "Constant", "Linear", "Cubic" };
                int32             current   = static_cast<int32>( key._interpolation );
                if ( ImGui::Combo( "##interpolation", &current, arrName, 3 ) )
                {
                    key._interpolation = static_cast<CurveInterpolation>( current );
                    bFinished          = true;
                }
                if ( bFinished )
                    curve.sortAndComputeAutoTangents();
                return bFinished;
            }

            /** @brief 오른쪽 클릭 메뉴입니다. 동작을 골랐으면 true 입니다. */
            static bool drawContextMenu( EditorCurveEditorState& state )
            {
                if ( ImGui::BeginPopup( "CurveMenu" ) == false )
                    return false;
                FloatCurve& curve   = state._working;
                bool        bEdited = false;
                if ( state._menuKey < curve._listKey.size() )
                {
                    FloatCurveKey& key = curve._listKey[state._menuKey];
                    if ( ImGui::MenuItem( "Delete Key" ) )
                    {
                        curve._listKey.erase( curve._listKey.begin() + static_cast<ptrdiff_t>( state._menuKey ) );
                        state._selectedKey = invalid_index::kUint32;
                        bEdited            = true;
                    }
                    const utf8* const        arrName[] = { "Constant", "Linear", "Cubic" };
                    const CurveInterpolation arrMode[] = { CurveInterpolation::Constant, CurveInterpolation::Linear, CurveInterpolation::Cubic };
                    for ( uint32 modeIndex = 0; modeIndex < 3 && bEdited == false; ++modeIndex )
                    {
                        if ( ImGui::MenuItem( arrName[modeIndex], nullptr, key._interpolation == arrMode[modeIndex] ) )
                        {
                            key._interpolation = arrMode[modeIndex];
                            bEdited            = true;
                        }
                    }
                    if ( bEdited == false && ImGui::MenuItem( "Auto Tangent", nullptr, key._bAutoTangent ) )
                    {
                        key._bAutoTangent = key._bAutoTangent == false;
                        bEdited           = true;
                    }
                }
                else
                {
                    if ( ImGui::MenuItem( "Add Key" ) )
                    {
                        state._selectedKey = curve.addKey( state._menuTime, state._menuValue );
                        bEdited            = true;
                    }
                    EditorSelfTestMarks::note( "curve.menu.addKey" );
                    if ( ImGui::MenuItem( "Fit View", "F" ) )
                        state._bFitPending = true;
                }
                if ( bEdited )
                    curve.sortAndComputeAutoTangents();
                ImGui::EndPopup();
                return bEdited;
            }

            /** @brief 캔버스 위의 누름 · 끌기 · 휠 · 키를 처리합니다. 끌기를 놓아 편집을 마쳤으면 true 입니다. */
            static bool handleCanvasInput( EditorCurveEditorState& state, bool bHovered, bool bActive )
            {
                ImGuiIO&         io           = ImGui::GetIO();
                FloatCurve&      curve        = state._working;
                EditorCurveView& view         = state._view;
                const float32    pickRadius   = kPickRadius * EditorThemeUtil::getDpiScale();
                const float2     mouse        = toFloat2( io.MousePos );
                const float32    handleLength = kTangentLength * EditorThemeUtil::getDpiScale();

                if ( bHovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
                {
                    state._drag       = EditorCurveDrag::None;
                    state._bDragMoved = false;
                    // 고른 Cubic 키의 접선 손잡이가 먼저다(키 점과 겹치지 않는 자리).
                    if ( state._selectedKey < curve._listKey.size() && curve._listKey[state._selectedKey]._interpolation == CurveInterpolation::Cubic )
                    {
                        const FloatCurveKey& key = curve._listKey[state._selectedKey];
                        if ( float2::getDistance( view.computeTangentHandle( key, true, handleLength ), mouse ) <= pickRadius )
                            state._drag = EditorCurveDrag::LeaveTangent;
                        else if ( float2::getDistance( view.computeTangentHandle( key, false, handleLength ), mouse ) <= pickRadius )
                            state._drag = EditorCurveDrag::ArriveTangent;
                    }
                    if ( state._drag == EditorCurveDrag::None )
                    {
                        state._selectedKey = view.findKeyAt( curve, mouse, pickRadius );
                        if ( state._selectedKey != invalid_index::kUint32 )
                            state._drag = EditorCurveDrag::Key;
                    }
                }
                if ( bHovered && ImGui::IsMouseClicked( ImGuiMouseButton_Middle ) )
                    state._drag = EditorCurveDrag::Pan;
                if ( bHovered && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
                {
                    state._menuKey = view.findKeyAt( curve, mouse, pickRadius );
                    view.toCurve( mouse, state._menuTime, state._menuValue );
                    ImGui::OpenPopup( "CurveMenu" );
                }
                if ( bHovered && io.MouseWheel != 0.0f )
                    view.zoomAt( mouse, std::pow( kWheelZoomPerStep, io.MouseWheel ) );
                if ( bHovered && ImGui::IsKeyPressed( ImGuiKey_F, false ) )
                    state._bFitPending = true;

                const bool bMoving = bActive && ( io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f );
                if ( bMoving && state._drag == EditorCurveDrag::Pan )
                    view.pan( toFloat2( io.MouseDelta ) );
                if ( bMoving && state._drag == EditorCurveDrag::Key && state._selectedKey < curve._listKey.size() )
                {
                    FloatCurveKey& key = curve._listKey[state._selectedKey];
                    float32        time{ 0.0f };
                    float32        value{ 0.0f };
                    view.toCurve( mouse, time, value );
                    if ( io.KeyCtrl == false )
                        key._time = clampKeyTime( curve, state._selectedKey, time ); // Ctrl = 값만
                    if ( io.KeyShift == false )
                        key._value = value; // Shift = 시간만
                    curve.sortAndComputeAutoTangents();
                    state._bDragMoved = true;
                }
                const bool bTangentDrag = state._drag == EditorCurveDrag::LeaveTangent || state._drag == EditorCurveDrag::ArriveTangent;
                if ( bMoving && bTangentDrag && state._selectedKey < curve._listKey.size() )
                {
                    // 두 접선을 함께 움직인다(매끈한 키 — 언리얼 User 접선). 손으로 정했으니 자동 접선은 끈다.
                    FloatCurveKey& key   = curve._listKey[state._selectedKey];
                    const float32  slope = view.computeTangentFromHandle( key, mouse );
                    key._arriveTangent   = slope;
                    key._leaveTangent    = slope;
                    key._bAutoTangent    = false;
                    state._bDragMoved    = true;
                }

                if ( state._drag == EditorCurveDrag::None || bActive )
                    return false;
                // 단추를 놓았다 — 키 · 접선을 실제로 움직였으면 편집 한 번으로 남긴다.
                const bool bFinished = state._drag != EditorCurveDrag::Pan && state._bDragMoved;
                state._drag          = EditorCurveDrag::None;
                state._bDragMoved    = false;
                return bFinished;
            }
        };
    } // namespace

    bool EditorCurveEditor::drawPreview( const utf8* pID, const FloatCurve& curve, float32 height )
    {
        const float32 width = MathUtil::max( ImGui::GetContentRegionAvail().x, 32.0f );
        const ImVec2  size{ width, MathUtil::max( height, 8.0f ) };
        const bool    bClicked = ImGui::InvisibleButton( pID, size );
        const ImVec2  minPoint = ImGui::GetItemRectMin();
        ImDrawList&   drawList = *ImGui::GetWindowDrawList();
        drawList.AddRectFilled( minPoint, ImVec2{ minPoint.x + size.x, minPoint.y + size.y }, ImGui::GetColorU32( ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg ),
                                ImGui::GetStyle().FrameRounding );
        if ( curve._listKey.empty() )
        {
            drawList.AddText( ImVec2{ minPoint.x + 4.0f, minPoint.y + 2.0f }, ImGui::GetColorU32( ImGuiCol_TextDisabled ), "(no keys)" );
            return bClicked;
        }
        EditorCurveView view{};
        view._frameMin  = float2{ minPoint.x + 2.0f, minPoint.y + 2.0f };
        view._frameSize = float2{ size.x - 4.0f, size.y - 4.0f };
        view.fitToCurve( curve, 0.0f );
        EditorCurveEditorInternal::drawCurveLine( drawList, view, curve, ImGui::GetColorU32( ImGuiCol_PlotLines ), 1.5f,
                                                  MathUtil::max( view._frameSize._x / EditorCurveEditorInternal::kPreviewSamples, 1.0f ) );
        EditorWidgets::drawTooltip( "Click to edit the curve" );
        return bClicked;
    }

    bool EditorCurveEditor::drawEditor( EditorCurveEditorState& state )
    {
        bool bFinished = EditorCurveEditorInternal::drawSelectedKeyFields( state );

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 size{ MathUtil::max( available.x, 64.0f ), MathUtil::max( available.y, 64.0f ) };
        ImGui::InvisibleButton( "##curveCanvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle );
        EditorSelfTestMarks::note( "curve.canvas" );
        const bool   bHovered = ImGui::IsItemHovered();
        const bool   bActive  = ImGui::IsItemActive();
        const ImVec2 minPoint = ImGui::GetItemRectMin();

        EditorCurveView& view = state._view;
        view._frameMin        = float2{ minPoint.x, minPoint.y };
        view._frameSize       = float2{ size.x, size.y };
        if ( state._bFitPending )
        {
            view.fitToCurve( state._working );
            state._bFitPending = false;
        }

        bFinished = EditorCurveEditorInternal::handleCanvasInput( state, bHovered, bActive ) || bFinished;
        bFinished = EditorCurveEditorInternal::drawContextMenu( state ) || bFinished;

        ImDrawList& drawList = *ImGui::GetWindowDrawList();
        drawList.PushClipRect( minPoint, ImVec2{ minPoint.x + size.x, minPoint.y + size.y }, true );
        drawList.AddRectFilled( minPoint, ImVec2{ minPoint.x + size.x, minPoint.y + size.y }, ImGui::GetColorU32( ImGuiCol_FrameBg ) );
        EditorCurveEditorInternal::drawGrid( drawList, view );
        EditorCurveEditorInternal::drawCurveLine( drawList, view, state._working, ImGui::GetColorU32( ImGuiCol_PlotLines ), 2.0f, 2.0f );

        const float32                        scale        = EditorThemeUtil::getDpiScale();
        const ImU32                          keyColor     = ImGui::GetColorU32( ImGuiCol_Text );
        const ImU32                          selectColor  = ImGui::GetColorU32( ImGuiCol_PlotLinesHovered );
        const float32                        keyRadius    = EditorCurveEditorInternal::kKeyRadius * scale;
        const float32                        handleLength = EditorCurveEditorInternal::kTangentLength * scale;
        fixed_string<constant::kMaxBuffer32> keyMark;
        for ( size_t index = 0; index < state._working._listKey.size(); ++index )
        {
            const FloatCurveKey& key       = state._working._listKey[index];
            const float2         center    = view.toScreen( key._time, key._value );
            const bool           bSelected = index == state._selectedKey;
            if ( bSelected && key._interpolation == CurveInterpolation::Cubic )
            {
                for ( const bool bLeave : { false, true } )
                {
                    const float2 handle = view.computeTangentHandle( key, bLeave, handleLength );
                    drawList.AddLine( EditorCurveEditorInternal::toImVec2( center ), EditorCurveEditorInternal::toImVec2( handle ), selectColor );
                    drawList.AddCircleFilled( EditorCurveEditorInternal::toImVec2( handle ), keyRadius * 0.8f, selectColor );
                }
            }
            drawList.AddCircleFilled( EditorCurveEditorInternal::toImVec2( center ), keyRadius, bSelected ? selectColor : keyColor );
            formatstring( keyMark.data(), keyMark.capacity(), "curve.key.%#", static_cast<uint32>( index ) );
            EditorCurveEditorInternal::noteRectMark( keyMark.c_str(), center, keyRadius );
        }
        drawList.PopClipRect();
        return bFinished;
    }
} // namespace sw::editor
