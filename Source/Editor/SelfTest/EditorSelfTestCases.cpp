#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/String/TagID.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"
#include "Editor/Common/Commands/Scene/EditorViewportPreview.h"
#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/InspectorPanel.h"
#include "Editor/Panels/Profiler/ProfilerPanel.h"
#include "Editor/Panels/Scene/HierarchyPanel.h"
#include "Editor/Panels/Scene/SceneViewPanel.h"
#include "Editor/Panels/Settings/UserSettingsPanel.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryLedger.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorSelfTestCasesInternal
        {
            /** @brief 테스트가 위젯을 그리는 창 이름입니다. 위젯은 창 안에서만 그릴 수 있습니다. */
            static constexpr const utf8* kProbeWindowName = "##EditorSelfTestProbe";

            static bool isNear( float32 left, float32 right, float32 tolerance ) { return ( left - right ) <= tolerance && ( right - left ) <= tolerance; }

            static bool isSameColor( const Color& left, const Color& right )
            {
                constexpr float32 kTolerance = 0.0001f;
                return isNear( left._r, right._r, kTolerance ) && isNear( left._g, right._g, kTolerance ) && isNear( left._b, right._b, kTolerance ) &&
                       isNear( left._a, right._a, kTolerance );
            }

            static bool isSameRGB( const ImVec4& left, const Color& right )
            {
                constexpr float32 kTolerance = 0.0001f;
                return isNear( left.x, right._r, kTolerance ) && isNear( left.y, right._g, kTolerance ) && isNear( left.z, right._b, kTolerance );
            }

            static void beginProbeWindow()
            {
                // 주 뷰포트 안에 둔다 - 데스크톱 좌표 (40, 40) 은 에디터 창 밖이라 프로브가 자기 OS 창(뷰포트)으로 떠 화면 왼쪽 위에서 깜박였다(`input.tooltipOnHover` 와 같다).
                const ImGuiViewport* pMainViewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowViewport( pMainViewport->ID );
                ImGui::SetNextWindowPos( ImVec2( pMainViewport->WorkPos.x + 40.0f, pMainViewport->WorkPos.y + 40.0f ), ImGuiCond_Always );
                ImGui::SetNextWindowSize( ImVec2( 480.0f, 240.0f ), ImGuiCond_Always );
                ImGui::Begin( kProbeWindowName, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking );
            }

            static GameObjectManager* getActiveManager( EditorSelfTestContext& context )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                (void)context.expect( pManager != nullptr, "the editor has no active scene" );
                return pManager;
            }

            /** @brief 테스트가 만든 오브젝트를 지우고 선택을 비웁니다. */
            static void destroyProbeObject( uint64 objectID )
            {
                EditorContext&     editorContext = editor::getSubsystem<EditorContext>();
                GameObjectManager* pManager      = editor::getActiveObjectManager();
                editorContext.getSubsystem<EditorWorkspace>().clearSelection();
                GameObject* pObj = ( pManager != nullptr ) ? pManager->findGameObjectByID( objectID ) : nullptr;
                if ( pObj != nullptr )
                    pManager->destroyObject( pObj );
            }

            // ------------------------------------------------------------------------------
            // theme.palette: 아직 쓰는 곳이 없는 팔레트 아홉 개를 확인한다. 창, 패널, 테두리 색, 액센트와 흐린 글자 색, 액센트 버튼과 헤더 색이다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runPaletteFollowsTheTheme( EditorSelfTestContext& context )
            {
                const EditorThemeConfig savedTheme = EditorThemeUtil::getActiveTheme();
                bool                    bAnyPresetDiffers{ false };
                vector<string>          listThemeName;
                EditorThemeUtil::listThemes( listThemeName );
                (void)context.expect( listThemeName.size() >= 3, "fewer than three theme token sets were loaded (dark, light, highcontrast)" );
                for ( const string& themeName : listThemeName )
                {
                    (void)context.expect( EditorThemeUtil::applyThemeByName( themeName ), "a listed theme could not be applied" );
                    const EditorThemeConfig& active = EditorThemeUtil::getActiveTheme();
                    (void)context.expect( isSameColor( EditorThemeUtil::getWindowBgColor(), active._windowBg ), "getWindowBgColor does not follow the preset" );
                    (void)context.expect( isSameColor( EditorThemeUtil::getPanelBgColor(), active._panelBg ), "getPanelBgColor does not follow the preset" );
                    (void)context.expect( isSameColor( EditorThemeUtil::getBorderColor(), active._border ), "getBorderColor does not follow the preset" );
                    if ( isSameColor( active._windowBg, savedTheme._windowBg ) == false )
                        bAnyPresetDiffers = true;
                }
                (void)context.expect( bAnyPresetDiffers, "every preset has the same window colour - the palette getters prove nothing" );

                beginProbeWindow();
                const ImGuiContext& imguiContext = *ImGui::GetCurrentContext();
                const int32         colorDepth   = imguiContext.ColorStack.Size;

                EditorThemeUtil::pushAccentButton();
                (void)context.expect( isSameRGB( ImGui::GetStyleColorVec4( ImGuiCol_Button ), EditorThemeUtil::getAccentColor() ), "pushAccentButton does not use the accent colour" );
                EditorThemeUtil::popAccentButton();
                (void)context.expect( imguiContext.ColorStack.Size == colorDepth, "pushAccentButton/popAccentButton do not balance" );

                EditorThemeUtil::pushAccentHeader();
                (void)context.expect( isSameRGB( ImGui::GetStyleColorVec4( ImGuiCol_Header ), EditorThemeUtil::getAccentColor() ), "pushAccentHeader does not use the accent colour" );
                EditorThemeUtil::popAccentHeader();
                (void)context.expect( imguiContext.ColorStack.Size == colorDepth, "pushAccentHeader/popAccentHeader do not balance" );

                EditorThemeUtil::textAccent( "accent" );
                (void)context.expect( ImGui::GetItemRectSize().x > 0.0f, "textAccent drew nothing" );
                EditorThemeUtil::textMuted( "muted" );
                (void)context.expect( ImGui::GetItemRectSize().x > 0.0f, "textMuted drew nothing" );
                (void)context.expect( imguiContext.ColorStack.Size == colorDepth, "textAccent/textMuted leave a colour pushed" );
                ImGui::End();

                EditorThemeUtil::applyTheme( savedTheme );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // theme.reapplyKeepsSizes: 1 이 아닌 DPI 에서 테마를 거듭 적용해도, 테마가 적지 않는 크기(도킹 구분선, 테두리 호버 여백)는 기본값에 배율을 한 번만 곱한 값이다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runThemeReapplyKeepsSizes( EditorSelfTestContext& context )
            {
                const EditorThemeConfig savedTheme = EditorThemeUtil::getActiveTheme();
                const float32           savedScale = EditorThemeUtil::getDPIScale();
                const float32           probeScale = savedScale + 0.5f; // 배율 1 은 ScaleAllSizes 를 건너뛰어 거듭 곱해지는지 볼 수 없다. 그래서 모니터 배율과 무관하게 1 이 아닌 값을 쓴다
                (void)context.expect( EditorThemeUtil::applyThemeByName( EditorConfig{}._themeName ), "the default theme token set is missing" );
                EditorThemeUtil::setDPIScale( probeScale );
                const EditorThemeConfig probeTheme = EditorThemeUtil::getActiveTheme();
                for ( uint32 applyIndex = 0; applyIndex < 3; ++applyIndex )
                {
                    EditorThemeUtil::applyTheme( probeTheme );
                }

                const ImGuiStyle  defaults;
                const ImGuiStyle& style = ImGui::GetStyle();
                (void)context.expect( style.DockingSeparatorSize == ImTrunc( defaults.DockingSeparatorSize * probeScale ),
                                      "re-applying the theme compounds the DPI scale into the docking separator size" );
                (void)context.expect( style.WindowBorderHoverPadding == ImTrunc( defaults.WindowBorderHoverPadding * probeScale ),
                                      "re-applying the theme compounds the DPI scale into the window border hover padding" );

                EditorThemeUtil::setDPIScale( savedScale );
                EditorThemeUtil::applyTheme( savedTheme );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // font.iconGlyphs: 아이콘 폰트가 본문 폰트에 합쳐졌는지 확인한다. 글리프를 찾지 못하면 ImGui 는 빈 상자를 그리고 로그 한 줄만 남긴다
            // ------------------------------------------------------------------------------
            static bool hasGlyph( ImFontBaked* pBaked, uint32 codepoint )
            {
                return pBaked != nullptr && pBaked->FindGlyphNoFallback( static_cast<ImWchar>( codepoint ) ) != nullptr;
            }

            static uint32 decodeFirstCodepoint( const utf8* pText )
            {
                uint32 codepoint{ 0 }; // ImTextCharFromUtf8 은 unsigned int* 를 받는다(uint32 와 같은 타입)
                ImTextCharFromUtf8( &codepoint, pText, nullptr );
                return codepoint;
            }

            static EditorSelfTestStep runIconFontHasGlyphs( EditorSelfTestContext& context )
            {
                // ImGuiNotify 토스트가 고정해 둔 Font Awesome 코드포인트(성공)다. 아이콘 폰트가 같은 코드포인트에 글리프를 둔다.
                constexpr uint32 kNotifySuccessCodepoint = 0xF058;
                ImFont*          pFont                   = ImGui::GetFont();
                ImFontBaked*     pBaked                  = pFont != nullptr ? pFont->GetFontBaked( ImGui::GetFontSize() ) : nullptr;
                (void)context.expect( hasGlyph( pBaked, decodeFirstCodepoint( editoricon::kSave ) ), "editor icon font glyph kSave is missing" );
                (void)context.expect( hasGlyph( pBaked, decodeFirstCodepoint( editoricon::kMissing ) ), "editor icon font glyph kMissing is missing" );
                (void)context.expect( hasGlyph( pBaked, kNotifySuccessCodepoint ), "notification glyph U+F058 is missing from the editor icon font" );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // widgets.*: 도움말 마커와 프로퍼티 행 라벨
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runHelpMarkerDrawsTheMarker( EditorSelfTestContext& context )
            {
                beginProbeWindow();
                EditorWidgets::drawHelpMarker( "help text" );
                const float32 markerWidth = ImGui::GetItemRectSize().x;
                (void)context.expect( markerWidth > 0.0f && isNear( markerWidth, ImGui::CalcTextSize( "(?)" ).x, 0.5f ), "drawHelpMarker does not draw the (?) marker" );
                ImGui::End();
                return EditorSelfTestStep::Done;
            }

            static EditorSelfTestStep runPropertyRowPlacesTheValueColumn( EditorSelfTestContext& context )
            {
                constexpr float32 kLabelWidth = 150.0f;
                beginProbeWindow();
                EditorWidgets::drawPropertyRowLabel( "Label", kLabelWidth );
                (void)context.expect( isNear( ImGui::GetCursorPosX(), kLabelWidth, 0.5f ), "the value column does not start at the label width" );
                const float32 expectedWidth = ImGui::GetContentRegionAvail().x - 1.0f;
                (void)context.expect( isNear( ImGui::CalcItemWidth(), expectedWidth, 1.0f ), "the value does not fill the rest of the row" );
                ImGui::TextUnformatted( "value" );
                ImGui::End();
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // dock.corePanelsAreDocked: 영역을 밝힌 열린 패널(`IEditorPanel::getDefaultDockSlot`)이 기본 배치에서 실제로 도킹됐는지 확인한다(열린 채 시작하는 도구 패널 포함)
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runCorePanelsAreDocked( EditorSelfTestContext& context )
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();

                uint32 checkedCount{ 0 };
                for ( const EditorPanelEntry& entry : editorContext.getSubsystem<EditorPanelManager>().getPanels() )
                {
                    const bool bSkipPanel = entry._pInstance == nullptr || entry._pInstance->isOpen() == false || entry._pInstance->getDefaultDockSlot() == EditorDockSlot::Floating;
                    if ( bSkipPanel )
                        continue;
                    const ImGuiWindow* pWindow = ImGui::FindWindowByName( entry._title.c_str() );
                    string             what{ "panel is not docked by the default layout: " };
                    what += entry._title;
                    (void)context.expect( pWindow != nullptr && pWindow->DockId != 0, what.c_str() );
                    ++checkedCount;
                }
                (void)context.expect( checkedCount > 0, "no docked panel was open" );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // dock.followsWindowSize: 창을 키우고 줄여도 메인 뷰포트가 클라이언트 크기를 따르고, 도크 노드가 화면 안에서 창에 대한 비율을 유지하는지 확인한다.
            // ImGui 는 중앙 노드 옆 노드에 마지막 픽셀 크기(SizeRef)를 그대로 준다. 맞추지 않으면 창을 줄일 때 옆 노드는 그대로이고 게임 뷰 쪽이 32 px 로 눌린다.
            // 게임 뷰 위의 Transform 바는 게임 뷰의 뷰포트 안에 남아야 한다. 넘치면 멀티 뷰포트가 OS 창으로 분리한다. 에디터 창은 최소 크기 밑으로 줄지 않는다.
            // ------------------------------------------------------------------------------
            struct DockResizeProbe
            {
                struct NodeShare
                {
                    ImGuiID _id{ 0 };
                    float32 _shareX{ 0.0f };
                    float32 _shareY{ 0.0f };
                };
                vector<NodeShare> _listShare;
                uint32            _originalWidth{ 0 };
                uint32            _originalHeight{ 0 };
            };

            /** @brief 메인 뷰포트를 덮는 도크스페이스의 루트 노드입니다. 없으면 nullptr 입니다. */
            static const ImGuiDockNode* findMainDockspace()
            {
                const ImGuiViewport* pMain       = ImGui::GetMainViewport();
                ImGuiDockContext&    dockContext = ImGui::GetCurrentContext()->DockContext;
                for ( int32 nodeIndex = 0; nodeIndex < dockContext.Nodes.Data.Size; ++nodeIndex )
                {
                    const ImGuiDockNode* pNode     = static_cast<const ImGuiDockNode*>( dockContext.Nodes.Data[nodeIndex].val_p );
                    const bool           bMainRoot = pNode != nullptr && pNode->ParentNode == nullptr && pNode->IsDockSpace() && pNode->HostWindow != nullptr &&
                                           pNode->HostWindow->Viewport == pMain;
                    if ( bMainRoot )
                        return pNode;
                }
                return nullptr;
            }

            static void collectVisibleLeaves( const ImGuiDockNode* pNode, vector<const ImGuiDockNode*>& outListLeaf )
            {
                if ( pNode == nullptr || pNode->IsVisible == false )
                    return;
                if ( pNode->IsLeafNode() )
                {
                    outListLeaf.push_back( pNode );
                    return;
                }
                collectVisibleLeaves( pNode->ChildNodes[0], outListLeaf );
                collectVisibleLeaves( pNode->ChildNodes[1], outListLeaf );
            }

            /**
             * @brief 이번 프레임의 메인 뷰포트와 도크 노드를 검사합니다.
             * @param bCompareShare false 이면 노드마다 창에 대한 비율을 기준으로 기록하고, true 이면 기록해 둔 기준과 비교합니다.
             */
            static void expectDockFitsViewport( EditorSelfTestContext& context, const IWindow& window, bool bCompareShare )
            {
                constexpr float32 kPixelTolerance = 1.0f;
                constexpr float32 kShareTolerance = 0.03f;

                const ImGuiViewport* pMain = ImGui::GetMainViewport();
                (void)context.expect( isNear( pMain->Size.x, static_cast<float32>( window.getWidth() ), kPixelTolerance ) &&
                                          isNear( pMain->Size.y, static_cast<float32>( window.getHeight() ), kPixelTolerance ),
                                      "the main viewport does not match the window client size" );

                const ImGuiDockNode* pRoot = findMainDockspace();
                if ( context.expect( pRoot != nullptr && pRoot->Size.x > 0.0f && pRoot->Size.y > 0.0f, "no main dockspace" ) == false )
                    return;

                vector<const ImGuiDockNode*> listLeaf;
                collectVisibleLeaves( pRoot, listLeaf );
                DockResizeProbe& probe = context.getTestState<DockResizeProbe>();
                const ImVec2     workMax{ pMain->WorkPos.x + pMain->WorkSize.x, pMain->WorkPos.y + pMain->WorkSize.y };
                for ( const ImGuiDockNode* pLeaf : listLeaf )
                {
                    const bool bInside = pLeaf->Pos.x >= pMain->WorkPos.x - kPixelTolerance && pLeaf->Pos.y >= pMain->WorkPos.y - kPixelTolerance &&
                                         pLeaf->Pos.x + pLeaf->Size.x <= workMax.x + kPixelTolerance && pLeaf->Pos.y + pLeaf->Size.y <= workMax.y + kPixelTolerance;
                    (void)context.expect( bInside, "a dock node lies outside the main viewport" );

                    const float32 shareX = pLeaf->Size.x / pRoot->Size.x;
                    const float32 shareY = pLeaf->Size.y / pRoot->Size.y;
                    if ( bCompareShare == false )
                    {
                        probe._listShare.push_back( DockResizeProbe::NodeShare{ pLeaf->ID, shareX, shareY } );
                        continue;
                    }
                    for ( const DockResizeProbe::NodeShare& share : probe._listShare )
                    {
                        if ( share._id != pLeaf->ID )
                            continue;
                        (void)context.expect( isNear( share._shareX, shareX, kShareTolerance ) && isNear( share._shareY, shareY, kShareTolerance ),
                                              "a dock node did not keep its share of the window - side panels kept their pixel size" );
                    }
                }
            }

            static void expectTransformBarStaysInSceneView( EditorSelfTestContext& context )
            {
                constexpr float32  kPixelTolerance       = 1.0f;
                const ImGuiWindow* pSceneView            = ImGui::FindWindowByName( SceneViewPanel::kPanelTitle );
                const ImGuiWindow* pBar                  = ImGui::FindWindowByName( "##EditorTransformBar" );
                const bool         bTransformBarNotDrawn = pSceneView == nullptr || pBar == nullptr || pBar->Active == false;
                if ( bTransformBarNotDrawn )
                    return; // 이번 프레임에 바를 그리지 않았다. 씬 뷰가 가려졌기 때문이다
                (void)context.expect( pBar->Viewport == pSceneView->Viewport, "the transform bar left the scene view's viewport (it became its own OS window)" );
                (void)context.expect( pBar->Pos.x >= pSceneView->Pos.x - kPixelTolerance &&
                                          pBar->Pos.x + pBar->Size.x <= pSceneView->Pos.x + pSceneView->Size.x + kPixelTolerance,
                                      "the transform bar is wider than the scene view" );
            }

            static EditorSelfTestStep runDockFollowsWindowSize( EditorSelfTestContext& context )
            {
                constexpr uint32 kSettleFrameCount = 3; // 크기 통보, 다음 NewFrame 의 DisplaySize, 도크 갱신 순서로 반영된다
                constexpr uint32 kWideWidth        = 1600;
                constexpr uint32 kWideHeight       = 900;
                constexpr uint32 kNarrowWidth      = 640; // 에디터 최소 크기보다 작다. 창은 최소 크기에서 멈춰야 한다
                constexpr uint32 kNarrowHeight     = 360;

                MainWindowSubsystem* pWindowHost = editor::findSubsystem<MainWindowSubsystem>();
                IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                if ( context.expect( pWindow != nullptr, "no active window" ) == false )
                    return EditorSelfTestStep::Done;

                DockResizeProbe& probe     = context.getTestState<DockResizeProbe>();
                const uint32     stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    probe._originalWidth  = pWindow->getWidth();
                    probe._originalHeight = pWindow->getHeight();
                    (void)context.expect( pWindow->setDisplayMode( WindowDisplayMode::Windowed, kWideWidth, kWideHeight ), "this platform cannot resize the window" );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex == kSettleFrameCount )
                {
                    expectDockFitsViewport( context, *pWindow, false );
                    (void)pWindow->setDisplayMode( WindowDisplayMode::Windowed, kNarrowWidth, kNarrowHeight );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex == kSettleFrameCount * 2 )
                {
                    expectDockFitsViewport( context, *pWindow, true );
                    expectTransformBarStaysInSceneView( context );
                    const uint32 minWidth  = pWindow->getMinimumClientWidth();
                    const uint32 minHeight = pWindow->getMinimumClientHeight();
                    (void)context.expect( minWidth > 0 && minHeight > 0, "the editor window has no minimum size" );
                    (void)context.expect( pWindow->getWidth() >= minWidth && pWindow->getHeight() >= minHeight, "the window shrank below the editor minimum size" );
                    (void)pWindow->setDisplayMode( WindowDisplayMode::Windowed, probe._originalWidth, probe._originalHeight );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex < kSettleFrameCount * 3 )
                    return EditorSelfTestStep::Continue;

                expectDockFitsViewport( context, *pWindow, true ); // 원래 크기로 돌려도 비율이 그대로여야 한다
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // inspector.drawLeavesTheObjectAlone: 좁은 enum(CameraRole : uint8) 바로 뒤의 bool 을 인스펙터가 그리면서 건드리지 않는지 확인한다
            // ------------------------------------------------------------------------------
            struct InspectorProbe
            {
                uint64 _objectID{ 0 };
                string _xmlBefore;
            };

            static EditorSelfTestStep runInspectorDrawLeavesTheObjectAlone( EditorSelfTestContext& context )
            {
                InspectorProbe& probe         = context.getTestState<InspectorProbe>();
                EditorContext&  editorContext = editor::getSubsystem<EditorContext>();
                if ( context.getStepIndex() == 0 )
                {
                    GameObjectManager* pManager = getActiveManager( context );
                    if ( pManager == nullptr )
                        return EditorSelfTestStep::Done;
                    GameObject* pObj = pManager->createGameObject( hashed_string( "EditorSelfTestCamera" ) );
                    if ( context.expect( pObj != nullptr, "could not create the probe object" ) == false )
                        return EditorSelfTestStep::Done;
                    CameraComponent* pCamera = pObj->addComponent<CameraComponent>();
                    if ( context.expect( pCamera != nullptr, "could not add a camera" ) == false )
                        return EditorSelfTestStep::Done;
                    pCamera->setRole( CameraRole::Custom );
                    pCamera->setOrthographic( true );
                    probe._objectID  = pObj->getObjectID();
                    probe._xmlBefore = ObjectStateSerializer::saveToXMLString( pObj, ObjectSaveOptions{ SerializePurpose::Snapshot } );
                    (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<InspectorPanel>( true );
                    editorContext.getSubsystem<EditorWorkspace>().selectGameObject( pObj );
                    return EditorSelfTestStep::Continue;
                }
                if ( context.getStepIndex() < 3 )
                    return EditorSelfTestStep::Continue; // 인스펙터가 두 번 그리도록 한다

                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObj     = ( pManager != nullptr ) ? pManager->findGameObjectByID( probe._objectID ) : nullptr;
                if ( context.expect( pObj != nullptr, "the probe object disappeared" ) )
                {
                    const CameraComponent* pCamera = pObj->getComponent<CameraComponent>();
                    (void)context.expect( pCamera != nullptr && pCamera->getRole() == CameraRole::Custom, "the inspector changed the camera role" );
                    (void)context.expect( pCamera != nullptr && pCamera->isOrthographic(), "the inspector cleared the bool next to the enum" );
                    (void)context.expect( ObjectStateSerializer::saveToXMLString( pObj, ObjectSaveOptions{ SerializePurpose::Snapshot } ) == probe._xmlBefore, "drawing the inspector changed the object" );
                }
                const ImGuiWindow* pInspector = ImGui::FindWindowByName( InspectorPanel::kPanelTitle );
                (void)context.expect( pInspector != nullptr && pInspector->Hidden == false, "the inspector was not drawn" );
                destroyProbeObject( probe._objectID );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // preview.materialHoldsOneReference: 머티리얼 미리보기는 머티리얼 하나에 참조 하나만 가진다. acquire 와 release 가 짝이 맞아야 한다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runMaterialPreviewHoldsOneReference( EditorSelfTestContext& context )
            {
                constexpr const utf8* kFirstPath  = "engine/materials/benchtextured.material";
                constexpr const utf8* kSecondPath = "engine/materials/sprite2d.material";

                const MaterialCache& cache = editor::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
                if ( context.expect( cache.isCached( kFirstPath ) == false, "the first probe material is already held by something else" ) == false )
                    return EditorSelfTestStep::Done;

                EditorViewportPreview::applyMaterial( nullptr, kFirstPath );
                EditorViewportPreview::applyMaterial( nullptr, kFirstPath );
                (void)context.expect( cache.isCached( kFirstPath ), "the preview did not acquire the material" );
                EditorViewportPreview::applyMaterial( nullptr, kSecondPath );
                (void)context.expect( cache.isCached( kFirstPath ) == false, "switching the preview left a reference on the previous material" );
                (void)context.expect( cache.isCached( kSecondPath ), "the preview did not acquire the second material" );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // hierarchy.tagFilter: Hierarchy 의 `tag:` 필터가 태그 계층과 대소문자 규칙을 따라 오브젝트를 찾는지 확인한다
            // ------------------------------------------------------------------------------
            struct HierarchyProbe
            {
                uint64 _objectID{ 0 };
            };

            static EditorSelfTestStep runHierarchyTagFilter( EditorSelfTestContext& context )
            {
                // 단계마다 넣을 필터와, 그 필터를 넣은 앞 단계에서 그린 프레임에 보여야 할 루트 수다.
                struct FilterCase
                {
                    const utf8* _pFilter;
                    uint32      _expectedRootCount;
                };
                constexpr FilterCase kArrFilterCase[] = {
                    {        "tag:SwSelfTest", 1}, // 부모 태그로 자식 태그를 찾는다
                    {  "tag:swselftest.child", 1}, // 대소문자를 가리지 않는다
                    {"tag:SwSelfTest.Missing", 0}, // 없는 태그
                };
                constexpr uint32 kCaseCount = static_cast<uint32>( sizeof( kArrFilterCase ) / sizeof( kArrFilterCase[0] ) );

                HierarchyProbe& probe         = context.getTestState<HierarchyProbe>();
                EditorContext&  editorContext = editor::getSubsystem<EditorContext>();
                (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<HierarchyPanel>( true );
                HierarchyPanel* pHierarchy = editorContext.getSubsystem<EditorPanelManager>().findPanel<HierarchyPanel>();
                if ( context.expect( pHierarchy != nullptr, "no hierarchy panel" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    GameObjectManager* pManager = getActiveManager( context );
                    if ( pManager == nullptr )
                        return EditorSelfTestStep::Done;
                    GameObject* pObj = pManager->createGameObject( hashed_string( "EditorSelfTestTagged" ) );
                    if ( context.expect( pObj != nullptr, "could not create the probe object" ) == false )
                        return EditorSelfTestStep::Done;
                    pObj->addTag( TagID::request( "SwSelfTest.Child" ) );
                    probe._objectID = pObj->getObjectID();
                }
                else
                {
                    const FilterCase& previous = kArrFilterCase[stepIndex - 1];
                    string            what{ "unexpected number of roots for " };
                    what += previous._pFilter;
                    (void)context.expect( pHierarchy->getVisibleRootCount() == previous._expectedRootCount, what.c_str() );
                }

                if ( stepIndex < kCaseCount )
                {
                    pHierarchy->setFilterText( kArrFilterCase[stepIndex]._pFilter );
                    return EditorSelfTestStep::Continue;
                }

                pHierarchy->setFilterText( "" );
                destroyProbeObject( probe._objectID );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // hierarchy.offscreenRootsKeepTheirPlace: 화면 밖의 접힌 루트를 빈 공간으로 대체해도 스크롤 길이가 같고, 맨 아래로 내리면 마지막 루트가 그려지는지 확인한다
            // ------------------------------------------------------------------------------
            struct OffscreenRowProbe
            {
                vector<uint64> _listObjectID;
                float32        _scrollMaxWithSkip{ 0.0f };
            };

            /** @brief Hierarchy 트리 영역(자식 창 "##HierarchyTree")을 찾습니다. 없으면 nullptr 입니다. */
            static ImGuiWindow* findHierarchyTreeWindow()
            {
                const ImGuiContext* pImGui = ImGui::GetCurrentContext();
                if ( pImGui == nullptr )
                    return nullptr;
                for ( ImGuiWindow* pWindow : pImGui->Windows )
                {
                    const bool bHierarchyTreeWindow = pWindow != nullptr && pWindow->Name != nullptr && StringUtil::contains( pWindow->Name, "HierarchyTree" );
                    if ( bHierarchyTreeWindow )
                        return pWindow;
                }
                return nullptr;
            }

            static EditorSelfTestStep runHierarchyOffscreenRootsKeepTheirPlace( EditorSelfTestContext& context )
            {
                constexpr uint32 kRootCount = 300;

                OffscreenRowProbe& probe         = context.getTestState<OffscreenRowProbe>();
                EditorContext&     editorContext = editor::getSubsystem<EditorContext>();
                (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<HierarchyPanel>( true );
                HierarchyPanel*    pHierarchy = editorContext.getSubsystem<EditorPanelManager>().findPanel<HierarchyPanel>();
                GameObjectManager* pManager   = editor::getActiveObjectManager();
                if ( context.expect( pHierarchy != nullptr && pManager != nullptr, "no hierarchy panel or active scene" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                ImGuiWindow* pTree     = findHierarchyTreeWindow();
                bool         bDone     = false;
                switch ( stepIndex )
                {
                    case 0:
                    {
                        pHierarchy->setFilterText( "" );
                        for ( uint32 rowIndex = 0; rowIndex < kRootCount; ++rowIndex )
                        {
                            GameObject* pObj = pManager->createGameObject( hashed_string( "EditorSelfTestRow" ) );
                            if ( pObj != nullptr )
                                probe._listObjectID.push_back( pObj->getObjectID() );
                        }
                        (void)context.expect( probe._listObjectID.size() == kRootCount, "could not create the probe rows" );
                        return EditorSelfTestStep::Continue;
                    }
                    case 1:
                    {
                        return EditorSelfTestStep::Continue; // 행이 모두 그려진(빈 공간 포함) 크기로 스크롤 길이가 정해지게 한다
                    }
                    case 2:
                    {
                        if ( context.expect( pTree != nullptr, "the hierarchy tree region was not found" ) == false )
                        {
                            bDone = true;
                            break;
                        }
                        (void)context.expect( pHierarchy->getDrawnRootCount() < pHierarchy->getVisibleRootCount(), "offscreen roots were drawn anyway" );
                        probe._scrollMaxWithSkip = pTree->ScrollMax.y;
                        pHierarchy->setOffscreenRowSkipEnabled( false );
                        return EditorSelfTestStep::Continue;
                    }
                    case 3:
                    {
                        return EditorSelfTestStep::Continue;
                    }
                    case 4:
                    {
                        // 빈 공간의 높이가 그린 행의 높이와 같아야 스크롤 길이가 같다. 다르면 스크롤이 튄다.
                        if ( pTree != nullptr )
                            (void)context.expect( isNear( pTree->ScrollMax.y, probe._scrollMaxWithSkip, 0.5f ), "skipped rows changed the scroll length" );
                        pHierarchy->setOffscreenRowSkipEnabled( true );
                        if ( pTree != nullptr )
                            ImGui::SetScrollY( pTree, pTree->ScrollMax.y ); // 맨 아래로 내린다. 다음 프레임에 반영된다
                        return EditorSelfTestStep::Continue;
                    }
                    case 5:
                    {
                        return EditorSelfTestStep::Continue;
                    }
                    default:
                    {
                        (void)context.expect( probe._listObjectID.empty() == false && pHierarchy->getLastDrawnRootID() == probe._listObjectID.back(),
                                              "scrolled to the bottom, the last root was not drawn" );
                        (void)context.expect( pHierarchy->getDrawnRootCount() < pHierarchy->getVisibleRootCount(), "offscreen roots were drawn at the bottom" );
                        bDone = true;
                        break;
                    }
                }
                if ( bDone == false )
                    return EditorSelfTestStep::Continue;
                pHierarchy->setOffscreenRowSkipEnabled( true );
                if ( pTree != nullptr )
                    ImGui::SetScrollY( pTree, 0.0f );
                for ( const uint64 objectID : probe._listObjectID )
                {
                    destroyProbeObject( objectID );
                }
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // sceneView.resizeEveryFrame: 씬 뷰 렌더 타깃을 프레임마다 다른 크기로 다시 만든다. 패널을 닫아도 두 뷰가 모두 안 보이면 씬 뷰 렌더 타깃에 그린다.
            // 해제한 ImGui 텍스처와 렌더 타깃은 그것을 그렸을 수 있는 마지막 프레임의 GPU 작업이 끝난 뒤에 해제해야 한다. 어기면 Vulkan 검증 레이어가
            // "사용 중인 디스크립터 세트 해제" 를 Error 로 남기고, AppSmokeTest 가 그 줄을 센다.
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runSceneViewResizeEveryFrame( EditorSelfTestContext& context )
            {
                constexpr const utf8* kSceneViewPanelID = SceneViewPanel::kPanelID;
                constexpr uint32      kResizeFrameCount = 90;

                EditorContext& editorContext = editor::getSubsystem<EditorContext>();

                // 씬 뷰 패널은 닫아 둔다. 패널은 그리기 전에 자기 크기로 다시 맞추므로, 열어 두면 이 테스트가 바꾼 크기를 되돌리면서 이미 그린 텍스처를
                // 같은 프레임에 해제한다. 패널은 다음 프레임에 닫히므로 첫 단계는 닫기만 한다.
                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen( kSceneViewPanelID, false );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex > kResizeFrameCount )
                {
                    (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen( kSceneViewPanelID, true );
                    return EditorSelfTestStep::Done;
                }

                // 패널과 같은 순서로 크기를 맞춘 뒤 그 프레임의 텍스처를 그린다. 이 프레임에서 해제한 텍스처는 앞 프레임의 스냅샷만 그렸다.
                const uint32 width  = 256u + ( stepIndex % 2u ) * 64u;
                const uint32 height = 144u + ( stepIndex % 2u ) * 36u;
                editorContext.getSubsystem<EditorRenderViews>().ensureViewTargetSize( EditorViewKind::Scene, width, height );
                const EditorViewTarget& view = editorContext.getSubsystem<EditorRenderViews>().getViewTarget( EditorViewKind::Scene );
                (void)context.expect( view._width == width && view._height == height && view._pTextureID != nullptr,
                                      "the scene view target was not recreated at the requested size" );

                beginProbeWindow();
                if ( view._pTextureID != nullptr )
                    ImGui::Image( reinterpret_cast<ImTextureID>( view._pTextureID ), ImVec2{ static_cast<float32>( width ) * 0.5f, static_cast<float32>( height ) * 0.5f } );
                ImGui::End();
                return EditorSelfTestStep::Continue;
            }

            // ------------------------------------------------------------------------------
            // profiler.gpuMemoryTab: 프로파일러의 GPU Memory 탭이 장부 테이블(항목, 바이트, 비율, 개수, 크기 모름)을 그리는지 확인한다
            // ------------------------------------------------------------------------------
            /** @brief 이 프레임에 그려진 테이블 중 첫 열이 `pFirstColumn` 이고 마지막 열이 `pLastColumn` 인 것을 찾습니다. 없으면 nullptr 입니다. */
            static ImGuiTable* findActiveTable( const utf8* pFirstColumn, const utf8* pLastColumn, int32 columnCount )
            {
                ImGuiContext& imguiContext = *ImGui::GetCurrentContext();
                for ( int32 tableIndex = 0; tableIndex < imguiContext.Tables.GetMapSize(); ++tableIndex )
                {
                    ImGuiTable* pTable            = imguiContext.Tables.TryGetMapData( tableIndex );
                    const bool  bNotMatchingTable = pTable == nullptr || pTable->LastFrameActive != imguiContext.FrameCount || pTable->ColumnsCount != columnCount;
                    if ( bNotMatchingTable )
                        continue;
                    const string_view firstName{ ImGui::TableGetColumnName( pTable, 0 ) };
                    const string_view lastName{ ImGui::TableGetColumnName( pTable, columnCount - 1 ) };
                    if ( firstName == pFirstColumn && lastName == pLastColumn )
                        return pTable;
                }
                return nullptr;
            }

            /** @brief 이름이 `pTabName` 인 탭을 가진 탭 바를 찾아 그 탭을 다음 프레임에 선택하게 합니다. 찾으면 true 입니다. */
            static bool queueTabFocus( const utf8* pTabName )
            {
                ImGuiContext& imguiContext = *ImGui::GetCurrentContext();
                for ( int32 barIndex = 0; barIndex < imguiContext.TabBars.GetMapSize(); ++barIndex )
                {
                    ImGuiTabBar* pTabBar = imguiContext.TabBars.TryGetMapData( barIndex );
                    if ( pTabBar == nullptr )
                        continue;
                    for ( ImGuiTabItem& tab : pTabBar->Tabs )
                    {
                        if ( string_view{ ImGui::TabBarGetTabName( pTabBar, &tab ) } != pTabName )
                            continue;
                        ImGui::TabBarQueueFocus( pTabBar, &tab );
                        return true;
                    }
                }
                return false;
            }

            static EditorSelfTestStep runProfilerGPUMemoryTabDrawsTheLedger( EditorSelfTestContext& context )
            {
                constexpr const utf8* kProfilerPanelID = ProfilerPanel::kPanelID;
                constexpr uint32      kMaxStepCount    = 30;

                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                if ( context.expect( editorContext.getSubsystem<EditorRenderViews>().getRHIDevice() != nullptr, "no RHI device" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen( kProfilerPanelID, true );
                    return EditorSelfTestStep::Continue;
                }

                // 탭은 선택한 다음 프레임에 그려진다. 테이블이 이 프레임에 그려졌으면 확인하고, 아니면 GPU Memory 탭을 다시 선택한다.
                const ImGuiTable* pTable = findActiveTable( "Kind", "Unknown Size", 5 );
                if ( pTable == nullptr && stepIndex < kMaxStepCount )
                {
                    (void)queueTabFocus( "GPU Memory" );
                    return EditorSelfTestStep::Continue;
                }
                (void)context.expect( pTable != nullptr, "the profiler GPU Memory tab never drew its ledger table" );
                const RHIMemoryLedger& ledger = editorContext.getSubsystem<EditorRenderViews>().getRHIDevice()->getMemoryLedger();
                (void)context.expect( ledger.getTrackedBytes() > 0, "the GPU memory ledger is empty in a running editor" );
                (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen( kProfilerPanelID, false );
                return EditorSelfTestStep::Done;
            }

            /**
             * @brief 사용자 설정 창이 카테고리 탭마다 설정 테이블을 그리는지 확인합니다. 탭을 하나씩 선택하고 다음 프레임에 테이블이 그려졌는지 봅니다.
             * @details 창은 게임 메뉴 UI 가 호출할 바인딩 API 만 쓰므로, 모든 종류의 위젯(체크, 슬라이더, 콤보, 키)이 엔진 스키마로 한 번씩 그려진다.
             */
            // ------------------------------------------------------------------------------
            // dpi.monitorScaleFollows: 창 DPI 가 0.5 커지면(100 % 에서 150 %) 글자, 여백, 테마 배율이 함께 따르고, Windows 에서는 WM_DPICHANGED 로 창과 백버퍼가 권장 사각형을 따르는지 확인한다.
            // 실제 150 % 모니터를 대신하는 테스트다(이 PC 는 96 DPI). 글자 래스터의 선명도는 이 테스트로 확인할 수 없다. 끝나면 원래 DPI 와 창 크기로 되돌린다.
            // ------------------------------------------------------------------------------
            struct DPIProbe
            {
                float32 ( *_pfnSavedDPIScale )( ImGuiViewport* ){ nullptr };
                float32 _baseScale{ 1.0f };
                float32 _fakeScale{ 1.5f }; ///< 교체한 창 DPI. 현재 배율에 0.5 를 더한 값이며, 이 PC 가 이미 150 % 여도 바뀐다
                float32 _basePaddingX{ 0.0f };
                bool    _bFakeInstalled{ false }; ///< 플랫폼 콜백을 `fakeWindowDPIScale` 로 바꿔 둔 동안 true
#if defined( SW_PLATFORM_WINDOWS )
                RECT _originalRect{};
                RECT _suggestedRect{};
#endif
                /** @brief 테스트가 어떻게 끝나든(시간 초과 · 실행 중단 포함) 콜백이 원래대로 돌아오도록 소멸자가 되돌립니다. 콜백은 이 객체가 있는 동안에만 걸려 있다. */
                ~DPIProbe() { restorePlatformCallback(); }

                void installFake( ImGuiPlatformIO& platformIO, float32 ( *pfnFake )( ImGuiViewport* ) )
                {
                    _pfnSavedDPIScale                     = platformIO.Platform_GetWindowDpiScale;
                    platformIO.Platform_GetWindowDpiScale = pfnFake;
                    _bFakeInstalled                       = true;
                }

                void restorePlatformCallback()
                {
                    if ( _bFakeInstalled == false )
                        return;
                    _bFakeInstalled = false;
                    if ( ImGui::GetCurrentContext() != nullptr ) // 에디터가 내려가며 ImGui 컨텍스트가 먼저 사라졌으면 되돌릴 곳도 없다
                        ImGui::GetPlatformIO().Platform_GetWindowDpiScale = _pfnSavedDPIScale;
                }
            };

            /** @brief ImGui 의 `Platform_GetWindowDpiScale` 자리에 거는 콜백입니다. 함수 포인터라 상태를 받지 못하므로 지금 도는 테스트의 프로브를 실행기에서 찾습니다. */
            static float32 fakeWindowDPIScale( ImGuiViewport* pViewport )
            {
                (void)pViewport;
                EditorSelfTestContext* pContext = EditorSelfTestRunner::findRunningContext();
                const DPIProbe*        pProbe   = pContext != nullptr ? pContext->findTestState<DPIProbe>() : nullptr;
                if ( pProbe == nullptr ) // 프로브가 사라지면 소멸자가 콜백을 되돌리므로, 걸려 있는 동안에는 항상 있다
                    ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
                return pProbe->_fakeScale;
            }

            static EditorSelfTestStep runDPIMonitorScaleFollows( EditorSelfTestContext& context )
            {
                // 단계 테이블. 교체한 DPI 를 NewFrame 이 읽고 테마가 따르는 데 몇 프레임, 창 메시지가 펌프와 리사이즈를 거치는 데 몇 프레임이 걸리는지 나타낸다.
                constexpr uint32 kCheckScaleStep   = 4;
                constexpr uint32 kCheckWindowStep  = 12;
                constexpr uint32 kCheckRestoreStep = 20;

                DPIProbe&        probe      = context.getTestState<DPIProbe>();
                ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
                const uint32     stepIndex  = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    if ( ImGui::GetIO().ConfigDpiScaleFonts == false )
                        return EditorSelfTestStep::Done; // 배율을 직접 정한 실행(gv_editorUiScale)은 모니터를 따르지 않으므로 확인할 것이 없다
                    probe._baseScale    = EditorThemeUtil::getDPIScale();
                    probe._basePaddingX = ImGui::GetStyle().FramePadding.x;
                    probe._fakeScale    = probe._baseScale + 0.5f;
                    probe.installFake( platformIO, &fakeWindowDPIScale );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex < kCheckScaleStep )
                    return EditorSelfTestStep::Continue; // NewFrame 이 뷰포트 DPI 를 다시 읽고, 다음 프레임에 테마가 따른다
                if ( stepIndex == kCheckScaleStep )
                {
                    const float32 expectedPadding = probe._basePaddingX / probe._baseScale * probe._fakeScale;
                    (void)context.expect( isNear( ImGui::GetMainViewport()->DpiScale, probe._fakeScale, 0.001f ), "the main viewport did not take the window DPI" );
                    (void)context.expect( isNear( ImGui::GetStyle().FontScaleDpi, probe._fakeScale, 0.001f ), "fonts did not follow the window DPI" );
                    (void)context.expect( isNear( EditorThemeUtil::getDPIScale(), probe._fakeScale, 0.001f ), "the theme scale did not follow the window DPI" );
                    (void)context.expect( isNear( ImGui::GetStyle().FramePadding.x, expectedPadding, 0.01f ), "frame padding stayed at the old DPI while fonts grew" );
                    probe.restorePlatformCallback();
#if defined( SW_PLATFORM_WINDOWS )
                    // 창 쪽에는 OS 가 보내는 것과 같은 메시지(새 DPI 와 그 비율만큼 큰 권장 사각형)를 보낸다. SendMessage 로 보내야 하며, PostMessage 로 게시한 WM_DPICHANGED 는 창 프로시저에 닿지 않는다.
                    HWND hWnd = static_cast<HWND>( IImGuiPlatformBackend::tryGetViewportWin32Handle( ImGui::GetMainViewport() ) );
                    (void)context.expect( hWnd != nullptr, "the main viewport has no Win32 window handle" );
                    if ( hWnd != nullptr && GetWindowRect( hWnd, &probe._originalRect ) )
                    {
                        probe._suggestedRect        = probe._originalRect;
                        probe._suggestedRect.right  = probe._originalRect.left + static_cast<LONG>( static_cast<float32>( probe._originalRect.right - probe._originalRect.left ) * probe._fakeScale / probe._baseScale );
                        probe._suggestedRect.bottom = probe._originalRect.top + static_cast<LONG>( static_cast<float32>( probe._originalRect.bottom - probe._originalRect.top ) * probe._fakeScale / probe._baseScale );
                        SendMessageW( hWnd, WM_DPICHANGED, MAKEWPARAM( static_cast<WORD>( 96.0f * probe._fakeScale ), static_cast<WORD>( 96.0f * probe._fakeScale ) ), reinterpret_cast<LPARAM>( &probe._suggestedRect ) );
                    }
#endif
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex < kCheckWindowStep )
                    return EditorSelfTestStep::Continue;
                if ( stepIndex == kCheckWindowStep )
                {
#if defined( SW_PLATFORM_WINDOWS )
                    HWND           hWnd = static_cast<HWND>( IImGuiPlatformBackend::tryGetViewportWin32Handle( ImGui::GetMainViewport() ) );
                    RECT           client{};
                    EditorContext& editorContext         = editor::getSubsystem<EditorContext>();
                    const bool     bCanProbeWindowGrowth = hWnd != nullptr && GetClientRect( hWnd, &client ) && editorContext.getSubsystem<EditorRenderViews>().getRHIDevice() != nullptr;
                    if ( bCanProbeWindowGrowth )
                    {
                        const IRHIDevice* pDevice    = editorContext.getSubsystem<EditorRenderViews>().getRHIDevice();
                        const bool        bGrew      = ( probe._suggestedRect.right - probe._suggestedRect.left ) > ( probe._originalRect.right - probe._originalRect.left );
                        const bool        bMatchesBb = pDevice->getBackBufferWidth() == static_cast<uint32>( client.right ) &&
                                                pDevice->getBackBufferHeight() == static_cast<uint32>( client.bottom );
                        RECT windowRect{};
                        (void)GetWindowRect( hWnd, &windowRect );
                        (void)context.expect( bGrew == false || ( windowRect.right - windowRect.left ) == ( probe._suggestedRect.right - probe._suggestedRect.left ),
                                              "WM_DPICHANGED did not move the window to the suggested rectangle" );
                        (void)context.expect( bMatchesBb, "after WM_DPICHANGED the back buffer does not match the window client area" );
                        // 원래 창 크기와 DPI 로 되돌린다. 다음 테스트가 커진 창에서 실행되지 않게 하기 위해서다.
                        SendMessageW( hWnd, WM_DPICHANGED, MAKEWPARAM( static_cast<WORD>( 96.0f * probe._baseScale ), static_cast<WORD>( 96.0f * probe._baseScale ) ), reinterpret_cast<LPARAM>( &probe._originalRect ) );
                    }
#endif
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex < kCheckRestoreStep )
                    return EditorSelfTestStep::Continue;
                // 실제 DPI 로 돌아왔으므로 테마도 따라 돌아와야 한다. 모니터를 옮겼다가 돌아오는 것과 같다.
                (void)context.expect( isNear( EditorThemeUtil::getDPIScale(), probe._baseScale, 0.001f ), "the theme scale did not follow the DPI back" );
                return EditorSelfTestStep::Done;
            }

            static EditorSelfTestStep runUserSettingsPanelDrawsEveryTab( EditorSelfTestContext& context )
            {
                constexpr const utf8* kPanelID      = UserSettingsPanel::kPanelID;
                EditorContext&        editorContext = editor::getSubsystem<EditorContext>();
                UserSettingsManager&  settings      = editor::getSubsystem<UserSettingsManager>();
                if ( context.expect( settings.getCategories().empty() == false, "no user settings categories" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen( kPanelID, true );
                    return EditorSelfTestStep::Continue;
                }

                // 홀수 단계는 탭을 선택하고, 짝수 단계는 그 탭의 테이블이 이 프레임에 그려졌는지 확인한다.
                const vector<UserSettingCategoryDef>& listCategory  = settings.getCategories();
                const uint32                          categoryIndex = ( stepIndex - 1 ) / 2;
                if ( categoryIndex >= static_cast<uint32>( listCategory.size() ) )
                {
                    (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen( kPanelID, false );
                    return EditorSelfTestStep::Done;
                }
                if ( ( stepIndex % 2 ) == 1 )
                {
                    (void)context.expect( queueTabFocus( listCategory[categoryIndex]._id.c_str() ), "a user settings category tab is missing" );
                    return EditorSelfTestStep::Continue;
                }
                (void)context.expect( findActiveTable( "Setting", "Value", 2 ) != nullptr, "a user settings tab did not draw its table" );
                return EditorSelfTestStep::Continue;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_SELF_TEST( Palette, "theme.palette", 100, &EditorSelfTestCasesInternal::runPaletteFollowsTheTheme );
    SW_EDITOR_SELF_TEST( ThemeReapply, "theme.reapplyKeepsSizes", 110, &EditorSelfTestCasesInternal::runThemeReapplyKeepsSizes );
    SW_EDITOR_SELF_TEST( IconFont, "font.iconGlyphs", 140, &EditorSelfTestCasesInternal::runIconFontHasGlyphs );
    SW_EDITOR_SELF_TEST( HelpMarker, "widgets.helpMarker", 200, &EditorSelfTestCasesInternal::runHelpMarkerDrawsTheMarker );
    SW_EDITOR_SELF_TEST( PropertyRow, "widgets.propertyRow", 210, &EditorSelfTestCasesInternal::runPropertyRowPlacesTheValueColumn );
    SW_EDITOR_SELF_TEST( CoreDock, "dock.corePanelsAreDocked", 300, &EditorSelfTestCasesInternal::runCorePanelsAreDocked );
    SW_EDITOR_SELF_TEST( DockResize, "dock.followsWindowSize", 310, &EditorSelfTestCasesInternal::runDockFollowsWindowSize );
    SW_EDITOR_SELF_TEST( InspectorEnum, "inspector.drawLeavesTheObjectAlone", 400, &EditorSelfTestCasesInternal::runInspectorDrawLeavesTheObjectAlone );
    SW_EDITOR_SELF_TEST( MaterialPreview, "preview.materialHoldsOneReference", 500, &EditorSelfTestCasesInternal::runMaterialPreviewHoldsOneReference );
    SW_EDITOR_SELF_TEST( HierarchyTag, "hierarchy.tagFilter", 600, &EditorSelfTestCasesInternal::runHierarchyTagFilter );
    SW_EDITOR_SELF_TEST( HierarchyOffscreenRows, "hierarchy.offscreenRootsKeepTheirPlace", 610, &EditorSelfTestCasesInternal::runHierarchyOffscreenRootsKeepTheirPlace );
    SW_EDITOR_SELF_TEST( SceneViewResize, "sceneView.resizeEveryFrame", 700, &EditorSelfTestCasesInternal::runSceneViewResizeEveryFrame );
    SW_EDITOR_SELF_TEST( ProfilerGPUMemory, "profiler.gpuMemoryTab", 800, &EditorSelfTestCasesInternal::runProfilerGPUMemoryTabDrawsTheLedger );
    SW_EDITOR_SELF_TEST( UserSettingsPanel, "userSettings.panelDrawsEveryTab", 900, &EditorSelfTestCasesInternal::runUserSettingsPanelDrawsEveryTab );
    SW_EDITOR_SELF_TEST( DPIMonitorScale, "dpi.monitorScaleFollows", 950, &EditorSelfTestCasesInternal::runDPIMonitorScaleFollows );
} // namespace sw::editor
