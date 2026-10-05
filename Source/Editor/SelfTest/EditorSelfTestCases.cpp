#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/String/TagID.h"

#include "Editor/Common/Commands/EditorViewportPreview.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/HierarchyPanel.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorSelfTestCasesInternal
        {
            /** @brief 시험이 위젯을 그리는 창 이름입니다. 위젯은 창 안에서만 그릴 수 있습니다. */
            static constexpr const utf8* kProbeWindowName = "##EditorSelfTestProbe";

            static bool isNear( float32 left, float32 right, float32 tolerance ) { return ( left - right ) <= tolerance && ( right - left ) <= tolerance; }

            static bool isSameColor( const Color4& left, const Color4& right )
            {
                constexpr float32 kTolerance = 0.0001f;
                return isNear( left._r, right._r, kTolerance ) && isNear( left._g, right._g, kTolerance ) && isNear( left._b, right._b, kTolerance ) &&
                       isNear( left._a, right._a, kTolerance );
            }

            static bool isSameRgb( const ImVec4& left, const Color4& right )
            {
                constexpr float32 kTolerance = 0.0001f;
                return isNear( left.x, right._r, kTolerance ) && isNear( left.y, right._g, kTolerance ) && isNear( left.z, right._b, kTolerance );
            }

            static void beginProbeWindow()
            {
                ImGui::SetNextWindowPos( ImVec2( 40.0f, 40.0f ), ImGuiCond_Always );
                ImGui::SetNextWindowSize( ImVec2( 480.0f, 240.0f ), ImGuiCond_Always );
                ImGui::Begin( kProbeWindowName, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking );
            }

            static GameObjectManager* getActiveManager( EditorSelfTestContext& context )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                (void)context.expect( pManager != nullptr, "the editor has no active scene" );
                return pManager;
            }

            /** @brief 시험이 만든 오브젝트를 지우고 선택을 비웁니다. */
            static void destroyProbeObject( uint64 objectId )
            {
                EditorContext*     pContext = EditorContext::get();
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pContext != nullptr )
                    pContext->getWorkspace().clearSelection();
                GameObject* pObj = ( pManager != nullptr ) ? pManager->findGameObjectById( objectId ) : nullptr;
                if ( pObj != nullptr )
                    pManager->destroyObject( pObj );
            }

            // ------------------------------------------------------------------------------
            // theme.palette — 소비자가 아직 없는 팔레트 아홉(창 · 패널 · 테두리 색, 액센트 · 흐린 글자, 액센트 버튼 · 헤더 짝)
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runPaletteFollowsTheTheme( EditorSelfTestContext& context )
            {
                const EditorThemeConfig savedTheme = EditorThemeUtil::getActiveTheme();
                bool                    bAnyPresetDiffers{ false };
                for ( uint8 presetIndex = 0; presetIndex < static_cast<uint8>( EditorThemePreset::Count ); ++presetIndex )
                {
                    EditorThemeUtil::applyPreset( static_cast<EditorThemePreset>( presetIndex ) );
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
                (void)context.expect( isSameRgb( ImGui::GetStyleColorVec4( ImGuiCol_Button ), EditorThemeUtil::getAccentColor() ), "pushAccentButton does not use the accent colour" );
                EditorThemeUtil::popAccentButton();
                (void)context.expect( imguiContext.ColorStack.Size == colorDepth, "pushAccentButton/popAccentButton do not balance" );

                EditorThemeUtil::pushAccentHeader();
                (void)context.expect( isSameRgb( ImGui::GetStyleColorVec4( ImGuiCol_Header ), EditorThemeUtil::getAccentColor() ), "pushAccentHeader does not use the accent colour" );
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
            // widgets.* — 도움말 표식 · 프로퍼티 행 라벨
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
                EditorWidgets::drawPropertyRowBegin( "Label", kLabelWidth );
                (void)context.expect( isNear( ImGui::GetCursorPosX(), kLabelWidth, 0.5f ), "the value column does not start at the label width" );
                const float32 expectedWidth = ImGui::GetContentRegionAvail().x - 1.0f;
                (void)context.expect( isNear( ImGui::CalcItemWidth(), expectedWidth, 1.0f ), "the value does not fill the rest of the row" );
                ImGui::TextUnformatted( "value" );
                ImGui::End();
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // dock.corePanelsAreDocked — 기본 도킹 배치가 적는 제목과 패널 제목이 맞는다(`-gv_editorOpenPanel=all` 로 기본 배치일 때)
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runCorePanelsAreDocked( EditorSelfTestContext& context )
            {
                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;

                uint32 checkedCount{ 0 };
                for ( const EditorPanelEntry& entry : pContext->getPanelManager().getPanels() )
                {
                    if ( entry._category != EditorPanelCategory::Core || entry._pInstance == nullptr || entry._pInstance->isOpen() == false )
                        continue;
                    const ImGuiWindow* pWindow = ImGui::FindWindowByName( entry._title.c_str() );
                    string             what{ "core panel is not docked by the default layout: " };
                    what += entry._title;
                    (void)context.expect( pWindow != nullptr && pWindow->DockId != 0, what.c_str() );
                    ++checkedCount;
                }
                (void)context.expect( checkedCount > 0, "no core panel was open" );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // inspector.drawLeavesTheObjectAlone — 좁은 enum(CameraRole : uint8) 바로 뒤의 bool 을 인스펙터가 그리며 건드리지 않는다
            // ------------------------------------------------------------------------------
            struct InspectorProbe
            {
                uint64 _objectId{ 0 };
                string _xmlBefore;
            };

            static InspectorProbe& getInspectorProbe()
            {
                static InspectorProbe s_probe;
                return s_probe;
            }

            static EditorSelfTestStep runInspectorDrawLeavesTheObjectAlone( EditorSelfTestContext& context )
            {
                InspectorProbe& probe    = getInspectorProbe();
                EditorContext*  pContext = EditorContext::get();
                if ( context.getStepIndex() == 0 )
                {
                    GameObjectManager* pManager = getActiveManager( context );
                    if ( pManager == nullptr || context.expect( pContext != nullptr, "no editor context" ) == false )
                        return EditorSelfTestStep::Done;
                    GameObject* pObj = pManager->createGameObject( hashed_string( "EditorSelfTestCamera" ) );
                    if ( context.expect( pObj != nullptr, "could not create the probe object" ) == false )
                        return EditorSelfTestStep::Done;
                    CameraComponent* pCamera = pObj->addComponent<CameraComponent>();
                    if ( context.expect( pCamera != nullptr, "could not add a camera" ) == false )
                        return EditorSelfTestStep::Done;
                    pCamera->setRole( CameraRole::Custom );
                    pCamera->setOrthographic( true );
                    probe._objectId  = pObj->getObjectId();
                    probe._xmlBefore = ObjectStateSerializer::saveToXmlString( pObj );
                    (void)pContext->getPanelManager().setPanelOpen( "inspector", true );
                    pContext->getWorkspace().selectGameObject( pObj );
                    return EditorSelfTestStep::Continue;
                }
                if ( context.getStepIndex() < 3 )
                    return EditorSelfTestStep::Continue; // 인스펙터가 두 번 그리게 둔다

                GameObjectManager* pManager = editor::getActiveObjectManager();
                GameObject*        pObj     = ( pManager != nullptr ) ? pManager->findGameObjectById( probe._objectId ) : nullptr;
                if ( context.expect( pObj != nullptr, "the probe object disappeared" ) )
                {
                    const CameraComponent* pCamera = pObj->getComponent<CameraComponent>();
                    (void)context.expect( pCamera != nullptr && pCamera->getRole() == CameraRole::Custom, "the inspector changed the camera role" );
                    (void)context.expect( pCamera != nullptr && pCamera->isOrthographic(), "the inspector cleared the bool next to the enum" );
                    (void)context.expect( ObjectStateSerializer::saveToXmlString( pObj ) == probe._xmlBefore, "drawing the inspector changed the object" );
                }
                const ImGuiWindow* pInspector = ImGui::FindWindowByName( "Inspector" );
                (void)context.expect( pInspector != nullptr && pInspector->Hidden == false, "the inspector was not drawn" );
                destroyProbeObject( probe._objectId );
                probe = InspectorProbe{};
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // preview.materialHoldsOneReference — 머티리얼 미리보기는 머티리얼 하나에 참조 하나만 든다(acquire · release 짝)
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runMaterialPreviewHoldsOneReference( EditorSelfTestContext& context )
            {
                constexpr const utf8* kFirstPath  = "engine/materials/benchtextured.material";
                constexpr const utf8* kSecondPath = "engine/materials/sprite2d.material";

                AssetManager* pResources = editor::getService<AssetManager>();
                if ( context.expect( pResources != nullptr, "no resource manager" ) == false )
                    return EditorSelfTestStep::Done;
                const MaterialCache& cache = pResources->getMaterialManager();
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
            // hierarchy.tagFilter — Hierarchy 의 `tag:` 필터가 태그 계층 · 대소문자를 따라 오브젝트를 찾는다
            // ------------------------------------------------------------------------------
            struct HierarchyProbe
            {
                uint64 _objectId{ 0 };
            };

            static HierarchyProbe& getHierarchyProbe()
            {
                static HierarchyProbe s_probe;
                return s_probe;
            }

            static EditorSelfTestStep runHierarchyTagFilter( EditorSelfTestContext& context )
            {
                // 단계마다 넣을 필터와, 그 필터가 앞 단계에서 그린 프레임에 보여야 할 루트 수.
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

                HierarchyProbe& probe    = getHierarchyProbe();
                EditorContext*  pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                (void)pContext->getPanelManager().setPanelOpen( "hierarchy", true );
                HierarchyPanel* pHierarchy = static_cast<HierarchyPanel*>( pContext->getPanelManager().findPanel( "hierarchy" ) );
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
                    probe._objectId = pObj->getObjectId();
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
                destroyProbeObject( probe._objectId );
                probe = HierarchyProbe{};
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // hierarchy.offscreenRootsKeepTheirPlace — 화면 밖의 접힌 루트를 빈자리로 둬도 스크롤 길이가 같고, 맨 아래로 내리면 마지막 루트가 그려진다
            // ------------------------------------------------------------------------------
            struct OffscreenRowProbe
            {
                vector<uint64> _listObjectId;
                float32        _scrollMaxWithSkip{ 0.0f };
            };

            static OffscreenRowProbe& getOffscreenRowProbe()
            {
                static OffscreenRowProbe s_probe;
                return s_probe;
            }

            /** @brief Hierarchy 트리 구역(자식 창 "##HierarchyTree")을 찾습니다. 없으면 nullptr. */
            static ImGuiWindow* findHierarchyTreeWindow()
            {
                const ImGuiContext* pImGui = ImGui::GetCurrentContext();
                if ( pImGui == nullptr )
                    return nullptr;
                for ( ImGuiWindow* pWindow : pImGui->Windows )
                {
                    if ( pWindow != nullptr && pWindow->Name != nullptr && StringUtil::contains( pWindow->Name, "HierarchyTree" ) )
                        return pWindow;
                }
                return nullptr;
            }

            static EditorSelfTestStep runHierarchyOffscreenRootsKeepTheirPlace( EditorSelfTestContext& context )
            {
                constexpr uint32 kRootCount = 300;

                OffscreenRowProbe& probe    = getOffscreenRowProbe();
                EditorContext*     pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                (void)pContext->getPanelManager().setPanelOpen( "hierarchy", true );
                HierarchyPanel*    pHierarchy = static_cast<HierarchyPanel*>( pContext->getPanelManager().findPanel( "hierarchy" ) );
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
                        probe = OffscreenRowProbe{};
                        for ( uint32 rowIndex = 0; rowIndex < kRootCount; ++rowIndex )
                        {
                            GameObject* pObj = pManager->createGameObject( hashed_string( "EditorSelfTestRow" ) );
                            if ( pObj != nullptr )
                                probe._listObjectId.push_back( pObj->getObjectId() );
                        }
                        (void)context.expect( probe._listObjectId.size() == kRootCount, "could not create the probe rows" );
                        return EditorSelfTestStep::Continue;
                    }
                    case 1:
                    {
                        return EditorSelfTestStep::Continue; // 줄이 다 그려진(빈자리 포함) 크기로 스크롤 길이가 정해지게
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
                        // 빈자리 높이 = 그린 줄의 높이여야 스크롤 길이가 같다(틀리면 스크롤이 튄다).
                        if ( pTree != nullptr )
                            (void)context.expect( isNear( pTree->ScrollMax.y, probe._scrollMaxWithSkip, 0.5f ), "skipped rows changed the scroll length" );
                        pHierarchy->setOffscreenRowSkipEnabled( true );
                        if ( pTree != nullptr )
                            ImGui::SetScrollY( pTree, pTree->ScrollMax.y ); // 맨 아래로 — 다음 프레임에 걸린다
                        return EditorSelfTestStep::Continue;
                    }
                    case 5:
                    {
                        return EditorSelfTestStep::Continue;
                    }
                    default:
                    {
                        (void)context.expect( probe._listObjectId.empty() == false && pHierarchy->getLastDrawnRootId() == probe._listObjectId.back(),
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
                for ( const uint64 objectId : probe._listObjectId )
                    destroyProbeObject( objectId );
                probe = OffscreenRowProbe{};
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // gameView.resizeEveryFrame — 게임 뷰를 프레임마다 다른 크기로 다시 만든다
            // 놓은 ImGui 텍스처 · 렌더 타깃은 그것을 그렸을 수 있는 마지막 프레임의 GPU 작업이 끝난 뒤에 놓여야 한다. 어기면 Vulkan 검증 레이어가
            // "사용 중인 디스크립터 세트 해제" 를 Error 로 남기고, AppSmokeTest 가 그 줄을 센다.
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runGameViewResizeEveryFrame( EditorSelfTestContext& context )
            {
                constexpr const utf8* kGameViewPanelId  = "game_view";
                constexpr uint32      kResizeFrameCount = 90;

                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;

                // 게임 뷰 패널은 닫아 둔다. 패널은 그리기 전에 자기 크기로 다시 맞추므로, 열어 두면 이 시험이 바꾼 크기를 되돌리며 이미 그린 텍스처를
                // 같은 프레임에 놓는다. 패널이 닫히는 것은 다음 프레임이라 첫 단계는 닫기만 한다.
                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    (void)pContext->getPanelManager().setPanelOpen( kGameViewPanelId, false );
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex > kResizeFrameCount )
                {
                    (void)pContext->getPanelManager().setPanelOpen( kGameViewPanelId, true );
                    return EditorSelfTestStep::Done;
                }

                // 패널과 같은 순서다: 크기를 맞춘 뒤 그 프레임의 텍스처를 그린다. 이 프레임이 놓은 텍스처는 앞 프레임의 스냅샷만 그린다.
                const uint32 width  = 256u + ( stepIndex % 2u ) * 64u;
                const uint32 height = 144u + ( stepIndex % 2u ) * 36u;
                pContext->ensureGameViewSize( width, height );
                const EditorGameView& view = pContext->getGameView();
                (void)context.expect( view._width == width && view._height == height && view._pTextureId != nullptr,
                                      "the game view was not recreated at the requested size" );

                beginProbeWindow();
                if ( view._pTextureId != nullptr )
                    ImGui::Image( reinterpret_cast<ImTextureID>( view._pTextureId ), ImVec2{ static_cast<float32>( width ) * 0.5f, static_cast<float32>( height ) * 0.5f } );
                ImGui::End();
                return EditorSelfTestStep::Continue;
            }

            // ------------------------------------------------------------------------------
            // profiler.gpuMemoryTab — 프로파일러의 GPU Memory 탭이 장부 표(줄 · 바이트 · 비율 · 개수 · 크기 모름)를 그린다
            // ------------------------------------------------------------------------------
            /** @brief 이 프레임에 그려진, 첫 열이 `pFirstColumn` 이고 마지막 열이 `pLastColumn` 인 표입니다. 없으면 nullptr. */
            static ImGuiTable* findActiveTable( const utf8* pFirstColumn, const utf8* pLastColumn, int32 columnCount )
            {
                ImGuiContext& imguiContext = *ImGui::GetCurrentContext();
                for ( int32 tableIndex = 0; tableIndex < imguiContext.Tables.GetMapSize(); ++tableIndex )
                {
                    ImGuiTable* pTable = imguiContext.Tables.TryGetMapData( tableIndex );
                    if ( pTable == nullptr || pTable->LastFrameActive != imguiContext.FrameCount || pTable->ColumnsCount != columnCount )
                        continue;
                    const string_view firstName{ ImGui::TableGetColumnName( pTable, 0 ) };
                    const string_view lastName{ ImGui::TableGetColumnName( pTable, columnCount - 1 ) };
                    if ( firstName == pFirstColumn && lastName == pLastColumn )
                        return pTable;
                }
                return nullptr;
            }

            /** @brief 이름이 `pTabName` 인 탭을 가진 탭 바를 찾아 그 탭을 다음 프레임에 고르게 합니다. 찾으면 true. */
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

            static EditorSelfTestStep runProfilerGpuMemoryTabDrawsTheLedger( EditorSelfTestContext& context )
            {
                constexpr const utf8* kProfilerPanelId = "profiler";
                constexpr uint32      kMaxStepCount    = 30;

                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr && pContext->getRhiDevice() != nullptr, "no editor context or RHI device" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    (void)pContext->getPanelManager().setPanelOpen( kProfilerPanelId, true );
                    return EditorSelfTestStep::Continue;
                }

                // 탭은 고른 다음 프레임에 그려진다. 표가 이 프레임에 그려졌으면 확인하고, 아니면 GPU Memory 탭을 다시 고른다.
                const ImGuiTable* pTable = findActiveTable( "Kind", "Unknown Size", 5 );
                if ( pTable == nullptr && stepIndex < kMaxStepCount )
                {
                    (void)queueTabFocus( "GPU Memory" );
                    return EditorSelfTestStep::Continue;
                }
                (void)context.expect( pTable != nullptr, "the profiler GPU Memory tab never drew its ledger table" );
                const RHIMemoryLedger& ledger = pContext->getRhiDevice()->getMemoryLedger();
                (void)context.expect( ledger.getTrackedBytes() > 0, "the GPU memory ledger is empty in a running editor" );
                (void)pContext->getPanelManager().setPanelOpen( kProfilerPanelId, false );
                return EditorSelfTestStep::Done;
            }

            /**
             * @brief 사용자 설정 창이 카테고리 탭마다 설정 표를 그린다 — 탭을 하나씩 골라 다음 프레임에 표가 그려졌는지 본다.
             * @details 창은 게임 메뉴 UI 가 부를 바인딩 API 만 쓰므로, 모든 종류의 위젯(체크 · 슬라이더 · 콤보 · 키)이 엔진 스키마로 한 번씩 그려진다.
             */
            // ------------------------------------------------------------------------------
            // dpi.monitorScaleFollows — 창 DPI 가 0.5 커지면(100 % → 150 %) 글자 · 여백 · 테마 배율이 함께 따르고, Windows 는 WM_DPICHANGED 로 창 · 백버퍼가 권장 사각형을 따른다.
            // 실물 150 % 모니터 대신이다(이 PC 는 96 DPI). 글자 래스터의 선명도는 이것으로 못 본다. 끝에 원래 DPI · 창 크기로 되돌린다.
            // ------------------------------------------------------------------------------
            struct DpiProbe
            {
                float32 ( *_pfnSavedDpiScale )( ImGuiViewport* ){ nullptr };
                float32 _baseScale{ 1.0f };
                float32 _fakeScale{ 1.5f }; ///< 바꿔 끼운 창 DPI — 지금 배율 + 0.5(이 PC 가 이미 150 % 여도 바뀐다)
                float32 _basePaddingX{ 0.0f };
#if defined( SW_PLATFORM_WINDOWS )
                RECT _originalRect{};
                RECT _suggestedRect{};
#endif
            };

            static DpiProbe& getDpiProbe()
            {
                static DpiProbe s_probe;
                return s_probe;
            }

            static float32 fakeWindowDpiScale( ImGuiViewport* pViewport )
            {
                (void)pViewport;
                return getDpiProbe()._fakeScale;
            }

            static EditorSelfTestStep runDpiMonitorScaleFollows( EditorSelfTestContext& context )
            {
                // 단계 표 — 바꿔 끼운 DPI 를 NewFrame 이 읽고 테마가 따르는 데 몇 프레임, 창 메시지가 펌프 · 리사이즈를 거치는 데 몇 프레임.
                constexpr uint32 kCheckScaleStep   = 4;
                constexpr uint32 kCheckWindowStep  = 12;
                constexpr uint32 kCheckRestoreStep = 20;

                DpiProbe&        probe      = getDpiProbe();
                ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
                const uint32     stepIndex  = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    if ( ImGui::GetIO().ConfigDpiScaleFonts == false )
                        return EditorSelfTestStep::Done; // 배율을 직접 정한 실행(gv_editorUiScale)은 모니터를 따르지 않는다 — 볼 것이 없다
                    probe                                 = DpiProbe{};
                    probe._pfnSavedDpiScale               = platformIo.Platform_GetWindowDpiScale;
                    probe._baseScale                      = EditorThemeUtil::getDpiScale();
                    probe._basePaddingX                   = ImGui::GetStyle().FramePadding.x;
                    probe._fakeScale                      = probe._baseScale + 0.5f;
                    platformIo.Platform_GetWindowDpiScale = &fakeWindowDpiScale;
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex < kCheckScaleStep )
                    return EditorSelfTestStep::Continue; // NewFrame 이 뷰포트 DPI 를 다시 읽고, 다음 프레임에 테마가 따른다
                if ( stepIndex == kCheckScaleStep )
                {
                    const float32 expectedPadding = probe._basePaddingX / probe._baseScale * probe._fakeScale;
                    (void)context.expect( isNear( ImGui::GetMainViewport()->DpiScale, probe._fakeScale, 0.001f ), "the main viewport did not take the window DPI" );
                    (void)context.expect( isNear( ImGui::GetStyle().FontScaleDpi, probe._fakeScale, 0.001f ), "fonts did not follow the window DPI" );
                    (void)context.expect( isNear( EditorThemeUtil::getDpiScale(), probe._fakeScale, 0.001f ), "the theme scale did not follow the window DPI" );
                    (void)context.expect( isNear( ImGui::GetStyle().FramePadding.x, expectedPadding, 0.01f ), "frame padding stayed at the old DPI while fonts grew" );
                    platformIo.Platform_GetWindowDpiScale = probe._pfnSavedDpiScale;
#if defined( SW_PLATFORM_WINDOWS )
                    // 창 쪽 — OS 가 보내는 것과 같은 메시지(새 DPI · 그 비율만큼 큰 권장 사각형). 보내기(SendMessage)로 — 주의: 게시(PostMessage)한 WM_DPICHANGED 는 창 프로시저에 닿지 않는다.
                    HWND hWnd = static_cast<HWND>( ImGui::GetMainViewport()->PlatformHandleRaw );
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
                    HWND           hWnd = static_cast<HWND>( ImGui::GetMainViewport()->PlatformHandleRaw );
                    RECT           client{};
                    EditorContext* pContext = EditorContext::get();
                    if ( hWnd != nullptr && GetClientRect( hWnd, &client ) && pContext != nullptr && pContext->getRhiDevice() != nullptr )
                    {
                        const IRHIDevice* pDevice    = pContext->getRhiDevice();
                        const bool        bGrew      = ( probe._suggestedRect.right - probe._suggestedRect.left ) > ( probe._originalRect.right - probe._originalRect.left );
                        const bool        bMatchesBb = pDevice->getBackBufferWidth() == static_cast<uint32>( client.right ) &&
                                                pDevice->getBackBufferHeight() == static_cast<uint32>( client.bottom );
                        RECT windowRect{};
                        (void)GetWindowRect( hWnd, &windowRect );
                        (void)context.expect( bGrew == false || ( windowRect.right - windowRect.left ) == ( probe._suggestedRect.right - probe._suggestedRect.left ),
                                              "WM_DPICHANGED did not move the window to the suggested rectangle" );
                        (void)context.expect( bMatchesBb, "after WM_DPICHANGED the back buffer does not match the window client area" );
                        // 원래 창 크기 · DPI 로 되돌린다 — 다음 시험이 커진 창에서 돌지 않게.
                        SendMessageW( hWnd, WM_DPICHANGED, MAKEWPARAM( static_cast<WORD>( 96.0f * probe._baseScale ), static_cast<WORD>( 96.0f * probe._baseScale ) ), reinterpret_cast<LPARAM>( &probe._originalRect ) );
                    }
#endif
                    return EditorSelfTestStep::Continue;
                }
                if ( stepIndex < kCheckRestoreStep )
                    return EditorSelfTestStep::Continue;
                // 진짜 DPI 로 돌아왔다 — 테마도 따라 돌아와야 한다(옮겨 갔다 돌아오는 모니터 이동과 같다).
                (void)context.expect( isNear( EditorThemeUtil::getDpiScale(), probe._baseScale, 0.001f ), "the theme scale did not follow the DPI back" );
                return EditorSelfTestStep::Done;
            }

            static EditorSelfTestStep runUserSettingsPanelDrawsEveryTab( EditorSelfTestContext& context )
            {
                constexpr const utf8* kPanelId  = "user_settings";
                EditorContext*        pContext  = EditorContext::get();
                UserSettingsManager*  pSettings = editor::getService<UserSettingsManager>();
                const bool            bReady    = pContext != nullptr && pSettings != nullptr && pSettings->getCategories().empty() == false;
                if ( context.expect( bReady, "no editor context or no user settings categories" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    (void)pContext->getPanelManager().setPanelOpen( kPanelId, true );
                    return EditorSelfTestStep::Continue;
                }

                // 홀수 단계는 탭을 고르고, 짝수 단계는 그 탭의 표가 이 프레임에 그려졌는지 본다.
                const vector<UserSettingCategoryDef>& listCategory  = pSettings->getCategories();
                const uint32                          categoryIndex = ( stepIndex - 1 ) / 2;
                if ( categoryIndex >= static_cast<uint32>( listCategory.size() ) )
                {
                    (void)pContext->getPanelManager().setPanelOpen( kPanelId, false );
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
    SW_EDITOR_SELF_TEST( HelpMarker, "widgets.helpMarker", 200, &EditorSelfTestCasesInternal::runHelpMarkerDrawsTheMarker );
    SW_EDITOR_SELF_TEST( PropertyRow, "widgets.propertyRow", 210, &EditorSelfTestCasesInternal::runPropertyRowPlacesTheValueColumn );
    SW_EDITOR_SELF_TEST( CoreDock, "dock.corePanelsAreDocked", 300, &EditorSelfTestCasesInternal::runCorePanelsAreDocked );
    SW_EDITOR_SELF_TEST( InspectorEnum, "inspector.drawLeavesTheObjectAlone", 400, &EditorSelfTestCasesInternal::runInspectorDrawLeavesTheObjectAlone );
    SW_EDITOR_SELF_TEST( MaterialPreview, "preview.materialHoldsOneReference", 500, &EditorSelfTestCasesInternal::runMaterialPreviewHoldsOneReference );
    SW_EDITOR_SELF_TEST( HierarchyTag, "hierarchy.tagFilter", 600, &EditorSelfTestCasesInternal::runHierarchyTagFilter );
    SW_EDITOR_SELF_TEST( HierarchyOffscreenRows, "hierarchy.offscreenRootsKeepTheirPlace", 610, &EditorSelfTestCasesInternal::runHierarchyOffscreenRootsKeepTheirPlace );
    SW_EDITOR_SELF_TEST( GameViewResize, "gameView.resizeEveryFrame", 700, &EditorSelfTestCasesInternal::runGameViewResizeEveryFrame );
    SW_EDITOR_SELF_TEST( ProfilerGpuMemory, "profiler.gpuMemoryTab", 800, &EditorSelfTestCasesInternal::runProfilerGpuMemoryTabDrawsTheLedger );
    SW_EDITOR_SELF_TEST( UserSettingsPanel, "userSettings.panelDrawsEveryTab", 900, &EditorSelfTestCasesInternal::runUserSettingsPanelDrawsEveryTab );
    SW_EDITOR_SELF_TEST( DpiMonitorScale, "dpi.monitorScaleFollows", 950, &EditorSelfTestCasesInternal::runDpiMonitorScaleFollows );
} // namespace sw::editor
