/**
 * @file EditorViewportVisualizer.cpp
 * @brief 등록된 시각화 가운데 켜진 것을 그립니다(모으기는 `Viewport/Model/Visualizers/` 의 각 파일, 화면 도구는 `Viewport/` 의 그 도구 파일)
 */
#include "pch.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportVisualizerInternal
        {
            /** @brief 아이콘 배지 바탕색입니다. */
            static constexpr Color kBillboardBackground{ Color::fromBytes( 20, 22, 28, 190 ) };
            /** @brief 아이콘 배지 지름(픽셀, 배율 1)입니다. */
            static constexpr float32 kBillboardDiameter     = 26.0f;
            static constexpr float32 kBillboardRimAlpha     = 0.9f;  ///< 배지 테두리 알파
            static constexpr float32 kBillboardRimThickness = 1.5f;  ///< 배지 테두리 굵기(픽셀)
            static constexpr float32 kBillboardGlyphScale   = 1.15f; ///< 글리프 크기 ÷ 배지 반지름

            /** @brief 글자 그림자 색입니다(밝은 배경에서도 읽히게). */
            static constexpr Color kTextShadow{ Color::fromBytes( 0, 0, 0, 200 ) };

            /** @brief 색 역할을 이번 테마의 색으로 풉니다(값에는 테마가 없다). 역할 색에는 값의 알파를 곱한다. */
            static ImU32 resolveColor( EditorVisualizerColorRole role, EditorThemeColorKey themeKey, const float4& color )
            {
                switch ( role )
                {
                    case EditorVisualizerColorRole::ThemeToken:
                        return withAlpha( EditorThemeUtil::findColor( themeKey ), color );
                    case EditorVisualizerColorRole::ColliderWire:
                        return withAlpha( SW_THEME_COLOR( "Viewport", "ColliderWire" ), color );
                    case EditorVisualizerColorRole::CameraWire:
                        return withAlpha( SW_THEME_COLOR( "Viewport", "CameraWire" ), color );
                    case EditorVisualizerColorRole::SelectionOutline:
                        return withAlpha( SW_THEME_COLOR( "Viewport", "SelectionOutline" ), color );
                    case EditorVisualizerColorRole::Hover:
                        return withAlpha( EditorThemeUtil::getViewportHoverColor(), color );
                    case EditorVisualizerColorRole::AxisX:
                        return withAlpha( SW_THEME_COLOR( "Viewport", "AxisX" ), color );
                    case EditorVisualizerColorRole::AxisY:
                        return withAlpha( SW_THEME_COLOR( "Viewport", "AxisY" ), color );
                    case EditorVisualizerColorRole::AxisZ:
                        return withAlpha( SW_THEME_COLOR( "Viewport", "AxisZ" ), color );
                    case EditorVisualizerColorRole::Explicit:
                        break;
                }
                return EditorColorUtil::toImU32( Color::fromFloat4( color ) );
            }

            static ImU32 withAlpha( const Color& theme, const float4& color )
            {
                float4 value = theme.toFloat4();
                value._w *= color._w;
                return EditorColorUtil::toImU32( Color::fromFloat4( value ) );
            }

            /** @brief 선분 하나를 근평면에서 잘라 긋습니다. 다 잘렸으면 false 입니다. */
            static bool drawLine( ImDrawList* pDrawList, const float4x4& viewProj, const float2& canvasPos, const float2& canvasSize, const EditorVisualizerLine& line )
            {
                ImVec2 screenFrom;
                ImVec2 screenTo;
                if ( EditorViewportProjectionUtil::projectSegment( viewProj, line._from, line._to, canvasPos, canvasSize, screenFrom, screenTo ) == false )
                    return false;
                pDrawList->AddLine( screenFrom, screenTo, resolveColor( line._colorRole, line._themeKey, line._color ), line._thickness );
                return true;
            }

            /** @brief 그린 선 · 글자 하나를 그 통계 칸에 셉니다. */
            static void count( EditorVisualizerStats& stats, EditorVisualizerStatSlot slot, bool bText )
            {
                switch ( slot )
                {
                    case EditorVisualizerStatSlot::PhysicsShape:
                        ++stats._shape._physicsSegmentCount;
                        break;
                    case EditorVisualizerStatSlot::NavMesh:
                        ++stats._shape._navMeshSegmentCount;
                        break;
                    case EditorVisualizerStatSlot::DebugDraw:
                        ++( bText ? stats._debugDraw._textCount : stats._debugDraw._segmentCount );
                        break;
                    case EditorVisualizerStatSlot::None:
                        break;
                }
            }

            /** @brief 모은 선분은 근평면에서 잘라, 글자는 점으로 투영해 그립니다. */
            static void drawShapes( const EditorViewportVisualizerArgs& args, const EditorVisualizerDrawList& drawList )
            {
                EditorVisualizerStats& stats   = *args._pStats;
                stats._shape                   = EditorShapeVisualizerStats{};
                stats._debugDraw._segmentCount = 0;
                stats._debugDraw._textCount    = 0;
                if ( drawList._bDebugDrawGathered )
                    stats._debugDraw._frame = ImGui::GetFrameCount();

                for ( const EditorVisualizerLine& line : drawList._listLine )
                {
                    if ( drawLine( args._pDrawList, *args._pViewProj, args._canvasPos, args._canvasSize, line ) )
                        count( stats, line._statSlot, false );
                }
                for ( const EditorVisualizerText& text : drawList._listText )
                {
                    ImVec2 screen;
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, text._position, args._canvasPos, args._canvasSize, screen ) == false )
                        continue;
                    // 글자 그림자를 먼저 깔아 밝은 배경에서도 읽히게 한다.
                    if ( text._bShadow )
                        args._pDrawList->AddText( ImVec2( screen.x + 1.0f, screen.y + 1.0f ), EditorColorUtil::toImU32( kTextShadow ), text._text.c_str() );
                    args._pDrawList->AddText( screen, resolveColor( text._colorRole, text._themeKey, text._color ), text._text.c_str() );
                    count( stats, text._statSlot, true );
                }
            }

            /** @brief 한 글자짜리 UTF-8 글의 코드 포인트입니다. 두 글자 이상이면 0 입니다. */
            static uint32 decodeSingleGlyph( const utf8* pGlyph )
            {
                const uint32 codePoint = EditorComponentIconRow::decodeGlyph( pGlyph );
                if ( codePoint == 0 )
                    return 0;
                uint32 wholeCodePoint = 0;
                return StringUtil::tryDecodeSingleCodepoint( string_view( pGlyph ), wholeCodePoint ) ? codePoint : 0u;
            }

            /** @brief 아이콘 배지(원 + 글리프, 셀프테스트 이름 `viewport.billboard.<오브젝트 이름>`)를 긋고 그 화면 자리를 남깁니다. */
            static void drawBillboardList( const EditorViewportVisualizerArgs& args, const EditorVisualizerDrawList& drawList, vector<EditorVisualizerScreenBillboard>& outListScreenBillboard )
            {
                if ( drawList._listBillboard.empty() )
                    return;
                const float32 radius   = kBillboardDiameter * EditorThemeUtil::getDPIScale() * 0.5f;
                ImFont*       pFont    = ImGui::GetFont();
                const float32 fontSize = radius * kBillboardGlyphScale;
                for ( const EditorVisualizerBillboard& badge : drawList._listBillboard )
                {
                    ImVec2 center{};
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, badge._position, args._canvasPos, args._canvasSize, center ) == false )
                        continue;
                    outListScreenBillboard.push_back( EditorVisualizerScreenBillboard{
                        badge._objectID, badge._componentID, float2{ center.x, center.y },
                        radius, decodeSingleGlyph( badge._row._pGlyph )
                    } );
                    const Color& color = badge._row._color;
                    args._pDrawList->AddCircleFilled( center, radius, EditorColorUtil::toImU32( kBillboardBackground ) );
                    args._pDrawList->AddCircle( center, radius, EditorColorUtil::toImU32( color.withAlpha( kBillboardRimAlpha ) ), 0, kBillboardRimThickness );
                    const ImVec2 glyphSize = pFont->CalcTextSizeA( fontSize, FLT_MAX, 0.0f, badge._row._pGlyph );
                    args._pDrawList->AddText( pFont, fontSize, ImVec2{ center.x - glyphSize.x * 0.5f, center.y - glyphSize.y * 0.5f }, EditorColorUtil::toImU32( color.withAlpha( 1.0f ) ),
                                              badge._row._pGlyph );
                    if ( EditorSelfTestMarks::isEnabled() )
                    {
                        // 상호작용 없는 항목으로 공간만 남긴다 — 캔버스의 누름(피킹)을 뺏지 않는다.
                        ImGui::ItemAdd( ImRect{
                                            ImVec2{center.x - radius, center.y - radius},
                                            ImVec2{center.x + radius, center.y + radius}
                        },
                                        0, nullptr, ImGuiItemFlags_NoNav );
                        EditorSelfTestMarks::recordFormat( "viewport.billboard.%#", badge._name.c_str() );
                    }
                }
            }
        };
    } // namespace

    uint32 EditorViewportVisualizer::drawLines( ImDrawList* pDrawList, const float4x4& viewProj, const float2& canvasPos, const float2& canvasSize, const vector<EditorVisualizerLine>& listLine )
    {
        uint32 drawnCount = 0;
        for ( const EditorVisualizerLine& line : listLine )
        {
            if ( EditorViewportVisualizerInternal::drawLine( pDrawList, viewProj, canvasPos, canvasSize, line ) )
                ++drawnCount;
        }
        return drawnCount;
    }

    uint32 EditorViewportVisualizer::getCount()
    {
        return EditorExtensionRegistry<EditorVisualizerRegistration>::getCount();
    }

    const EditorVisualizerRegistration& EditorViewportVisualizer::getAt( uint32 index )
    {
        return EditorExtensionRegistry<EditorVisualizerRegistration>::getAt( index );
    }

    void EditorViewportVisualizer::drawAll( const EditorViewportVisualizerArgs& args, const EditorVisualizerToggles& toggles, const EditorVisualizerDrawList& drawList,
                                            vector<EditorVisualizerScreenBillboard>& outListScreenBillboard )
    {
        outListScreenBillboard.clear();
        const bool bMissingDrawInputs = args._pDrawList == nullptr || args._pViewProj == nullptr;
        if ( bMissingDrawInputs )
            return;
        EditorViewportVisualizerInternal::drawShapes( args, drawList );
        EditorViewportVisualizerInternal::drawBillboardList( args, drawList, outListScreenBillboard );

        for ( uint32 index = 0; index < getCount(); ++index )
        {
            const EditorVisualizerRegistration& registration = getAt( index );
            if ( toggles.isOn( registration ) == false )
                continue;
            if ( registration._pTool != nullptr )
                registration._pTool( args );
        }
    }

    // 이 종류의 등록 목록 — EditorModule 이미지에 하나(확장 DLL 의 등록자도 내보낸 이 함수로 오른다).
    SW_DEFINE_EDITOR_EXTENSION_KIND( EditorVisualizerRegistration )
} // namespace sw::editor
