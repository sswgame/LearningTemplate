#include "pch.h"

#include "Editor/Viewport/TerrainBrushTool.h"

#include "Core/Container/EnumArray.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/Ray.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorViewportMouseCapture.h"
#include "Editor/Viewport/Model/TerrainBrushEdit.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct TerrainBrushToolInternal
        {
            /** @brief 마우스를 가져가는 소유자 표시입니다(주소만 비교한다 — `EditorViewportMouseCapture`). */
            static constexpr utf8 kClaimOwner = 0;
            /** @brief 안쪽 원(감쇠 시작)의 알파입니다. */
            static constexpr float32 kInnerCircleAlpha = 0.5f;
            static constexpr uint32  kLayerCount       = SurfaceLayerWeights::kLayerCount; ///< 레이어 수의 정본(지형 `kMaxLayerCount` 도 이 값이다)

            /** @brief 원 꺾은선 점(Model 이 지형 높이를 따라 낸 것)을 잇습니다. */
            static void drawCircle( EditorProjectedLineRenderer& renderer, const vector<float3>& listPoint, const float4& color )
            {
                for ( size_t index = 1; index < listPoint.size(); ++index )
                {
                    renderer.drawLine( listPoint[index - 1], listPoint[index], color );
                }
            }

            /**
             * @brief 해상도 줄입니다. 대상 지형(없으면 비활성)의 해상도를 고르고 `Resize` 로 다시 표본화합니다.
             * @details 고를 수 있는 해상도는 Model 이 모은 것(청크 칸의 배수 + 1)뿐이다 — 나뉘지 않는 값을 쓸 수 없다.
             */
            static void drawResizeControls( const TerrainBrushViewState& state, vector<TerrainBrushCommand>& outListCommand )
            {
                const uint32 current = state._currentResolution;
                ImGui::BeginDisabled( current == 0 || state._bStroking );
                const string preview = state._resizeResolution > 0 ? to_string( state._resizeResolution ) : string{ "-" };
                EditorChrome::setNextItemWidthColumns( 16.0f );
                if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Resolution" ), "Resolution" ), preview.c_str() ) )
                {
                    vector<uint32> listResolution;
                    TerrainBrushEdit::collectUsableResolutions( listResolution );
                    for ( const uint32 resolution : listResolution )
                    {
                        fixed_string<constant::buffer::kMax32> label;
                        formatstring( label.data(), label.capacity(), "%#%#", resolution, resolution == current ? " (current)" : "" );
                        if ( ImGui::Selectable( label.c_str(), resolution == state._resizeResolution ) )
                            TerrainBrushEdit::setResizeResolution( resolution );
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                ImGui::BeginDisabled( state._resizeResolution == current );
                if ( ImGui::Button( SW_LOCKEY( "EdText", "Resize" ) ) && current != 0 )
                    outListCommand.push_back( TerrainBrushCommand{ TerrainBrushCommandKind::Resize, float3{}, state._resizeResolution } );
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
                    ImGui::SetTooltip( "%s", SW_LOCKEY( "EdTip", "TerrainResample" ) );
            }

            /**
             * @brief 폴리지 고르기입니다 — 고른 폴리지 레이어의 밀도 지형 레이어를 브러시 레이어로 둔다. 칠하면 심기고 Erase Layer 로 지운다.
             * @details 밀도 레이어가 없는 폴리지는 고를 수 없고 까닭을 보인다(Inspector 의 Density Layer 로 정한다).
             */
            static void drawFoliageCombo( TerrainBrushSettings& settings )
            {
                vector<TerrainBrushFoliageLayer> listFoliage;
                TerrainBrushEdit::collectFoliageLayers( listFoliage );
                if ( listFoliage.empty() )
                    return;
                const utf8* pPreview = SW_LOCKEY( "EdWidget", "NoFoliageOnLayer" );
                for ( const TerrainBrushFoliageLayer& foliage : listFoliage )
                {
                    if ( foliage._densityLayerIndex == settings._layerIndex )
                        pPreview = foliage._label.c_str();
                }
                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Foliage" ) );
                const bool bOpen = EditorWidgets::beginCombo( "##Foliage", pPreview );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "TerrainFoliage" ) );
                if ( bOpen == false )
                    return;
                for ( const TerrainBrushFoliageLayer& foliage : listFoliage )
                {
                    ImGui::BeginDisabled( foliage._densityLayerIndex.has_value() == false );
                    if ( ImGui::Selectable( foliage._label.c_str(), foliage._densityLayerIndex == settings._layerIndex ) && foliage._densityLayerIndex.has_value() )
                        settings._layerIndex = *foliage._densityLayerIndex;
                    ImGui::EndDisabled();
                    if ( foliage._densityLayerIndex.has_value() == false )
                        EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "TerrainFoliageNoDensity" ) );
                }
                ImGui::EndCombo();
            }

            /** @brief 브러시 설정 창입니다(모드 · 반경 · 세기 · 감쇠 · 레이어 · 해상도 · 저장). 설정 값은 Model 의 것을 바로 고친다(`TerrainBrushEdit::getSettings`). */
            static void drawSettingsWindow( const EditorViewportVisualizerArgs& args, const TerrainBrushViewState& state, vector<TerrainBrushCommand>& outListCommand )
            {
                TerrainBrushSettings& settings = TerrainBrushEdit::getSettings();
                ImGui::SetNextWindowPos( ImVec2{ args._canvasPos._x + 12.0f, args._canvasPos._y + 48.0f }, ImGuiCond_FirstUseEver );
                if ( ImGui::Begin( "Terrain Brush###TerrainBrushTool", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
                {
                    static constexpr auto kArrModeName = makeEnumTable<TerrainBrushMode, const utf8*>( "Raise", "Lower", "Flatten", "Paint Layer", "Smooth", "Cut Hole", "Fill Hole", "Erase Layer" );
                    int32                 mode         = static_cast<int32>( settings._mode );
                    EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Mode" ) );
                    if ( EditorWidgets::combo( SW_WIDGET_ID( "##Mode" ), &mode, kArrModeName.data(), static_cast<int32>( kArrModeName.size() ) ) )
                        settings._mode = static_cast<TerrainBrushMode>( mode );
                    EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Radius" ) );
                    EditorWidgets::sliderFloat( SW_WIDGET_ID( "##Radius" ), &settings._radius, 0.5f, 64.0f, "%.1f m" );
                    EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Strength" ) );
                    EditorWidgets::sliderFloat( SW_WIDGET_ID( "##Strength" ), &settings._strength, 0.05f, 60.0f, "%.2f /s", ImGuiSliderFlags_Logarithmic );
                    EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Falloff" ) );
                    EditorWidgets::sliderFloat( SW_WIDGET_ID( "##Falloff" ), &settings._falloff, 0.0f, 1.0f, "%.2f" );
                    if ( TerrainBrush::getTarget( settings._mode ) == TerrainBrushTarget::LayerWeight )
                    {
                        drawFoliageCombo( settings );
                        int32 layer = static_cast<int32>( settings._layerIndex );
                        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Layer" ) );
                        if ( EditorWidgets::sliderInt( SW_WIDGET_ID( "##Layer" ), &layer, 0, static_cast<int32>( kLayerCount ) - 1 ) )
                            settings._layerIndex = static_cast<uint32>( layer );
                    }
                    drawResizeControls( state, outListCommand );
                    ImGui::BeginDisabled( state._bCanSave == false );
                    if ( ImGui::Button( state._bUnsaved ? "Save Terrain *" : "Save Terrain" ) )
                        outListCommand.push_back( TerrainBrushCommand{ TerrainBrushCommandKind::Save, float3{}, 0 } );
                    ImGui::EndDisabled();
                    if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
                        ImGui::SetTooltip( "%s", SW_LOCKEY( "EdTip", "TerrainWriteAssets" ) );
                }
                ImGui::End();
            }

            /** @brief 모으기(Model) → 원 그리기 · 마우스 양보 → 붓질 명령 → 설정 창 → 적용(Model). */
            static void process( const EditorViewportVisualizerArgs& args )
            {
                TerrainBrushEdit::markActive( ImGui::GetFrameCount() );
                vector<TerrainBrushCommand> listCommand;
                TerrainBrushViewState       state{};
                const bool                  bUsable = args._pObjectManager != nullptr && args._pViewProj != nullptr && args._bFlat2D == false;
                Ray                         ray{};
                const bool                  bRay = bUsable && EditorViewportProjectionUtil::makeMouseRay( args._pViewProj->invert(), args._canvasPos, args._canvasSize, ray );
                TerrainBrushEdit::gather( bRay ? &ray : nullptr, state );

                TerrainBrushStrokeInput input{};
                input._bUsable   = bUsable;
                input._bStroking = state._bStroking;
                if ( bUsable == false )
                {
                    TerrainBrushEdit::stepStroke( input, listCommand );
                    TerrainBrushEdit::apply( listCommand );
                    return;
                }

                if ( state._bHit )
                {
                    EditorProjectedLineRenderer renderer{ args._pDrawList, *args._pViewProj, args._canvasPos, args._canvasSize };
                    const float4                color{ 1.0f, 0.85f, 0.2f, 1.0f };
                    drawCircle( renderer, state._listCircle, color );
                    drawCircle( renderer, state._listInnerCircle, float4{ color._x, color._y, color._z, kInnerCircleAlpha } );
                }

                // 지형 위에 있거나 칠하는 중이면 마우스를 가져간다 — 클릭 선택과 기즈모가 물러난다.
                const ImVec2 mouse        = ImGui::GetMousePos();
                const bool   bInsideX     = mouse.x >= args._canvasPos._x && mouse.x <= args._canvasPos._x + args._canvasSize._x;
                const bool   bInsideY     = mouse.y >= args._canvasPos._y && mouse.y <= args._canvasPos._y + args._canvasSize._y;
                const bool   bMouseInside = bInsideX && bInsideY && ImGui::IsWindowHovered();
                const bool   bClaim       = state._bEditable && ( state._bStroking || ( bMouseInside && state._bHit && args._bGizmoDragging == false ) );
                if ( bClaim )
                    EditorViewportMouseCapture::acquire( &kClaimOwner );
                else
                    EditorViewportMouseCapture::release( &kClaimOwner );

                input._hit        = state._hit;
                input._bHit       = state._bHit;
                input._bStillDown = ImGui::IsMouseDown( ImGuiMouseButton_Left ) && state._bEditable && state._bStrokeTargetAlive;
                input._bPressed   = bClaim && EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left );
                TerrainBrushEdit::stepStroke( input, listCommand );

                drawSettingsWindow( args, state, listCommand );
                TerrainBrushEdit::apply( listCommand );
            }

            [[nodiscard]] static bool readToolActive( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const optional<int32> activeFrame = TerrainBrushEdit::getLastActiveFrame();
                outValue                          = activeFrame.has_value() && *activeFrame + 1 >= ImGui::GetFrameCount() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    void TerrainBrushTool::process( const EditorViewportVisualizerArgs& args )
    {
        TerrainBrushToolInternal::process( args );
    }

    SW_AUTOMATION_WATCH( editorTerrainBrushToolActive, "Editor.TerrainBrushToolActive", "1 when the terrain tool (scene view Terrain toggle) ran this or the last frame",
                         &TerrainBrushToolInternal::readToolActive );
} // namespace sw::editor
