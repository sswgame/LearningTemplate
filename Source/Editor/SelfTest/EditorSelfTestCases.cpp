#include "pch.h"

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
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Resource/ResourceManager.h"

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

                ResourceManager* pResources = editor::getService<ResourceManager>();
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
    SW_EDITOR_SELF_TEST( GameViewResize, "gameView.resizeEveryFrame", 700, &EditorSelfTestCasesInternal::runGameViewResizeEveryFrame );
} // namespace sw::editor
