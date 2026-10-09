#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/TagID.h"

#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Gui/EditorMenuBar.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/HierarchyPanel.h"
#include "Editor/SelfTest/EditorSelfTest.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "sw/config/ConfigConstants.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>

// 에디터 자체 시험 중 입력(클릭 · 타이핑 · 호버)을 흉내 내는 것들 — EditorSelfTestInput 이 다음 프레임의 ImGui 입력 큐에 넣는다.
// 주의: 플랫폼 백엔드가 프레임마다 실제 커서를 넣을 수 있어, 누르기 · 떼기 · 호버는 그 단계마다 이름표 위로 다시 옮긴 뒤 넣는다.

namespace sw::editor
{
    namespace
    {
        struct EditorSelfTestInputCasesInternal
        {
            /** @brief 툴팁 지연(ImGuiHoveredFlags_DelayShort)을 기다리는 단계의 상한입니다 — 실행기의 시험당 상한(300)보다 작다. */
            static constexpr uint32 kMaxTooltipWaitStep = 240;

            /** @brief 클릭 하나의 누르기 · 떼기를 이름표 위에서 넣습니다. 이름표가 없으면 false. */
            static bool pressOnMark( const utf8* pKey, bool bDown )
            {
                if ( EditorSelfTestInput::moveMouseToMark( pKey ) == false )
                    return false;
                EditorSelfTestInput::setMouseButton( ImGuiMouseButton_Left, bDown );
                return true;
            }

            /** @brief 마우스를 화면 밖으로 치웁니다 — 다음 시험의 호버를 흐리지 않게. */
            static void moveMouseAway() { EditorSelfTestInput::moveMouse( float2{ -10000.0f, -10000.0f } ); }

            // --------------------------------------------------------------------------------------------------
            // input.hierarchySearchTyping — 검색 칸을 클릭하고 타이핑하면 `tag:` 필터가 걸리고, 없는 이름은 0 건 안내를 그린다
            // --------------------------------------------------------------------------------------------------
            struct TypingProbe
            {
                string _pressDiagnosis{}; ///< 누른 프레임 뒤의 입력 대상(`describeInputTarget`) — 실패 이유에 붙인다
                uint64 _objectId{ 0 };
                uint32 _hintCountBefore{ 0 };
            };

            /**
             * @brief 글자가 어디로 가는지 가르는 상태 한 줄입니다 — 활성 위젯 · 이름표 위젯 · 호버 위젯 · 호버 창 · 글자 입력 요구.
             * @details 활성 위젯이 이름표 위젯과 같은데 글자가 안 닿으면 글자 주입 경로가, 다르면 클릭이 칸을 잡지 못한 것이다(호버 창이 다른 창이면
             *          그 창이 가로챘다 — 도킹 구분선(d077a5252) · 처음부터 열린 떠 있는 창).
             */
            static string describeInputTarget( const utf8* pMarkKey )
            {
                const ImGuiContext&                   imguiContext = *ImGui::GetCurrentContext();
                EditorSelfTestMark                    mark{};
                const bool                            bHasMark       = EditorSelfTestMarks::find( pMarkKey, mark );
                const ImGuiWindow*                    pHoveredWindow = imguiContext.HoveredWindow;
                fixed_string<constant::kMaxBuffer512> text;
                formatstring( text.data(), text.capacity(), "active=%u field=%u hoveredId=%u hoveredWindow=%s wantTextInput=%d",
                              static_cast<uint32>( imguiContext.ActiveId ), bHasMark ? mark._itemId : 0u, static_cast<uint32>( imguiContext.HoveredId ),
                              pHoveredWindow != nullptr && pHoveredWindow->Name != nullptr ? pHoveredWindow->Name : "(none)",
                              ImGui::GetIO().WantTextInput ? 1 : 0 );
                return string{ text.c_str() };
            }

            static TypingProbe& getTypingProbe()
            {
                static TypingProbe s_probe;
                return s_probe;
            }

            static void finishTyping( HierarchyPanel* pHierarchy, GameObjectManager* pManager )
            {
                TypingProbe& probe = getTypingProbe();
                ImGui::ClearActiveID(); // 검색 칸의 입력 상태를 놓는다 — 다음 시험의 키가 이리 오지 않게
                if ( pHierarchy != nullptr )
                    pHierarchy->setFilterText( "" );
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    pContext->getWorkspace().clearSelection();
                GameObject* pObj = ( pManager != nullptr ) ? pManager->findGameObjectById( probe._objectId ) : nullptr;
                if ( pObj != nullptr )
                    pManager->destroyObject( pObj );
                probe = TypingProbe{};
                moveMouseAway();
            }

            static EditorSelfTestStep runHierarchySearchTyping( EditorSelfTestContext& context )
            {
                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                (void)pContext->getPanelManager().setPanelOpen( "hierarchy", true );
                HierarchyPanel*    pHierarchy = static_cast<HierarchyPanel*>( pContext->getPanelManager().findPanel( "hierarchy" ) );
                GameObjectManager* pManager   = editor::getActiveObjectManager();
                if ( context.expect( pHierarchy != nullptr && pManager != nullptr, "no hierarchy panel or active scene" ) == false )
                    return EditorSelfTestStep::Done;

                TypingProbe& probe = getTypingProbe();
                switch ( context.getStepIndex() )
                {
                    case 0:
                    {
                        GameObject* pObj = pManager->createGameObject( hashed_string( "EditorSelfTestTyped" ) );
                        if ( context.expect( pObj != nullptr, "could not create the probe object" ) == false )
                            return EditorSelfTestStep::Done;
                        pObj->addTag( TagID::request( "SwSelfTest.Typed" ) );
                        probe._objectId = pObj->getObjectId();
                        pHierarchy->setFilterText( "" );
                        return EditorSelfTestStep::Continue; // 패널이 한 번 그려져 이름표가 생기게
                    }
                    case 1:
                    case 5:
                    {
                        // 검색 칸을 누른다(InputText 는 누를 때 잡힌다). 두 번째(5)는 지운 칸을 다시 잡는다.
                        if ( context.expect( pressOnMark( "hierarchy.filter", true ), "the hierarchy search field left no mark" ) == false )
                        {
                            finishTyping( pHierarchy, pManager );
                            return EditorSelfTestStep::Done;
                        }
                        return EditorSelfTestStep::Continue;
                    }
                    case 2:
                    case 6:
                    {
                        if ( context.getStepIndex() == 2 )
                            probe._pressDiagnosis = describeInputTarget( "hierarchy.filter" );
                        (void)pressOnMark( "hierarchy.filter", false );
                        return EditorSelfTestStep::Continue;
                    }
                    case 3:
                    {
                        EditorSelfTestInput::typeText( "tag:SwSelfTest" );
                        return EditorSelfTestStep::Continue;
                    }
                    case 4:
                    {
                        // 글자가 든 프레임에 패널이 칸 → 트리 순으로 그렸다. 칸에 닿았는지 · 필터가 탐침을 찾았는지 본다.
                        // 실패하면 이유에 누른 뒤 · 지금의 입력 대상을 붙인다 — 실행 한 번으로 "클릭이 칸을 못 잡았다" 와 "글자가 안 들어갔다" 가 갈린다(패널 점검 D26).
                        string what{ "typing did not reach the search field [after press: " };
                        what += probe._pressDiagnosis;
                        what += " | now: ";
                        what += describeInputTarget( "hierarchy.filter" );
                        what += "]";
                        (void)context.expect( pHierarchy->getFilterText() == "tag:SwSelfTest", what.c_str() );
                        (void)context.expect( pHierarchy->getVisibleRootCount() == 1, "the typed tag filter did not find the probe object" );
                        // 지우고 없는 이름을 친다 — 입력 중인 칸은 ImGui 가 든 글을 쓰므로 먼저 놓고 지운 뒤 다시 누른다.
                        ImGui::ClearActiveID();
                        pHierarchy->setFilterText( "" );
                        return EditorSelfTestStep::Continue;
                    }
                    case 7:
                    {
                        probe._hintCountBefore = EditorWidgets::getNoSearchResultHintCount();
                        EditorSelfTestInput::typeText( "zzSelfTestNoSuchObject" );
                        return EditorSelfTestStep::Continue;
                    }
                    default:
                    {
                        (void)context.expect( pHierarchy->getFilterText() == "zzSelfTestNoSuchObject", "the second typing did not reach the search field" );
                        (void)context.expect( pHierarchy->getVisibleRootCount() == 0, "a name that matches nothing still shows roots" );
                        (void)context.expect( EditorWidgets::getNoSearchResultHintCount() > probe._hintCountBefore, "zero matches did not draw the no-result hint" );
                        finishTyping( pHierarchy, pManager );
                        return EditorSelfTestStep::Done;
                    }
                }
            }

            // --------------------------------------------------------------------------------------------------
            // input.tooltipOnHover — 마우스를 버튼 위에 두면 툴팁 창이 뜬다(EditorWidgets::drawTooltip 의 지연 포함)
            // --------------------------------------------------------------------------------------------------
            static bool isAnyTooltipShown()
            {
                const ImGuiContext* pImGui = ImGui::GetCurrentContext();
                if ( pImGui == nullptr )
                    return false;
                for ( const ImGuiWindow* pWindow : pImGui->Windows )
                {
                    if ( pWindow != nullptr && ( pWindow->Flags & ImGuiWindowFlags_Tooltip ) != 0 && pWindow->Active )
                        return true;
                }
                return false;
            }

            /** @brief 제목에 @p pTitlePart 가 든 창을 찾습니다(아이콘 글자가 붙은 제목). 없으면 nullptr. */
            static ImGuiWindow* findWindowByTitlePart( const utf8* pTitlePart )
            {
                const ImGuiContext* pImGui = ImGui::GetCurrentContext();
                if ( pImGui == nullptr )
                    return nullptr;
                for ( ImGuiWindow* pWindow : pImGui->Windows )
                {
                    if ( pWindow != nullptr && pWindow->Name != nullptr && StringUtil::contains( pWindow->Name, pTitlePart ) )
                        return pWindow;
                }
                return nullptr;
            }

            static EditorSelfTestStep runTooltipOnHover( EditorSelfTestContext& context )
            {
                // 시험이 그리는 창 하나 — 패널 배치와 무관하게 늘 같은 자리 · 맨 위다.
                // 주 뷰포트 안 — 데스크톱 좌표 (40, 300) 은 창 밖일 수 있고, 그러면 창이 자기 플랫폼 창(뷰포트)으로 떠 호버가 실제 커서를 따른다.
                const ImGuiViewport* pMainViewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowViewport( pMainViewport->ID );
                ImGui::SetNextWindowPos( ImVec2( pMainViewport->WorkPos.x + 40.0f, pMainViewport->WorkPos.y + 300.0f ), ImGuiCond_Always );
                ImGui::SetNextWindowSize( ImVec2( 240.0f, 120.0f ), ImGuiCond_Always );
                ImGui::SetNextWindowFocus();
                const bool bVisible = ImGui::Begin( "Self Test Tooltip Probe##EditorSelfTestTooltipProbe", nullptr,
                                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking );
                if ( bVisible )
                {
                    (void)ImGui::Button( "Hover me##SelfTestTooltip" );
                    EditorSelfTestMarks::note( "selftest.tooltipButton" );
                    EditorWidgets::drawTooltip( "self-test tooltip" );
                }
                const bool bShown = bVisible && isAnyTooltipShown();

                ImGui::End();

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                    return EditorSelfTestStep::Continue; // 이름표가 생기게 한 번 그린다
                if ( bShown && stepIndex > 1 )
                {
                    moveMouseAway();
                    return EditorSelfTestStep::Done;
                }
                if ( stepIndex >= kMaxTooltipWaitStep )
                {
                    (void)context.expect( false, "hovering the button never opened its tooltip" );
                    moveMouseAway();
                    return EditorSelfTestStep::Done;
                }
                // 지연 동안 마우스를 버튼 위에 둔다(실제 커서가 끼어들어도 이 프레임의 마지막 위치는 버튼이다).
                if ( EditorSelfTestInput::moveMouseToMark( "selftest.tooltipButton" ) == false )
                {
                    (void)context.expect( false, "the probe button left no mark" );
                    return EditorSelfTestStep::Done;
                }
                return EditorSelfTestStep::Continue;
            }

            // --------------------------------------------------------------------------------------------------
            // input.classicDarkSwatch — Classic Dark 프리셋에서 테마 대화상자의 스와치를 누르면 액센트가 바뀐다(끝에 원래 테마로 되돌린다)
            // --------------------------------------------------------------------------------------------------
            /** @brief 시험 앞의 테마와 에디터 설정 파일 바이트입니다 — 스와치 클릭은 설정 파일을 다시 쓴다(saveToConfig), 끝에 바이트째 되돌린다. */
            struct SwatchProbe
            {
                EditorThemeConfig _theme;
                vector<uint8>     _configBytes;
                bool              _bConfigExisted{ false };
            };

            static SwatchProbe& getSwatchProbe()
            {
                static SwatchProbe s_probe;
                return s_probe;
            }

            static string getEditorConfigPath() { return EditorUtil::resolveProjectRelativePath( config::kFileRuntimeEditorConfig ); }

            static EditorSelfTestStep runClassicDarkSwatch( EditorSelfTestContext& context )
            {
                const Color4 violet{ 0.65f, 0.35f, 0.95f, 1.0f }; // EditorThemeUtil 의 셋째 스와치(Neon Violet)와 같은 값
                switch ( context.getStepIndex() )
                {
                    case 0:
                    {
                        SwatchProbe& probe = getSwatchProbe();
                        probe._theme       = EditorThemeUtil::getActiveTheme();
                        // 파일이 없을 수 있다(Saved/Editor 는 git 무시 — 새 체크아웃) — 없으면 읽지 않는다(readFile 은 없는 파일을 오류로 남긴다).
                        const string configPath = getEditorConfigPath();
                        probe._bConfigExisted   = FileUtil::isRegularFile( configPath ) && FileUtil::readFile( configPath, probe._configBytes );
                        EditorThemeUtil::applyPreset( EditorThemePreset::ClassicDark );
                        EditorMenuBar::openThemeDialog();
                        return EditorSelfTestStep::Continue;
                    }
                    case 1:
                    {
                        // 대화상자가 한 번 그려졌다(이름표가 생겼다). 떠 있는 창(자기 플랫폼 창)이어도 이름표의 뷰포트를 함께 넣으므로 클릭이 닿는다.
                        (void)context.expect( findWindowByTitlePart( "Theme & Look and Feel" ) != nullptr, "the theme dialog did not open" );
                        return EditorSelfTestStep::Continue;
                    }
                    case 2:
                    {
                        return EditorSelfTestStep::Continue;
                    }
                    case 3:
                    {
                        if ( context.expect( pressOnMark( "theme.swatch.violet", true ), "the theme dialog swatch left no mark" ) == false )
                            break;
                        return EditorSelfTestStep::Continue;
                    }
                    case 4:
                    {
                        (void)pressOnMark( "theme.swatch.violet", false ); // 버튼은 뗄 때 눌린다 — 떼는 자리도 스와치 위여야 한다
                        return EditorSelfTestStep::Continue;
                    }
                    case 5:
                    {
                        return EditorSelfTestStep::Continue;
                    }
                    default:
                    {
                        const Color4& accent       = EditorThemeUtil::getAccentColor();
                        const bool    bIsVioletNow = accent._r == violet._r && accent._g == violet._g && accent._b == violet._b;
                        (void)context.expect( bIsVioletNow, "clicking a swatch in the Classic Dark theme dialog did not change the accent" );
                        (void)context.expect( EditorThemeUtil::getActiveTheme()._preset == EditorThemePreset::ClassicDark,
                                              "the swatch switched the preset away from Classic Dark" );
                        break;
                    }
                }
                // 되돌리기 — 사용자의 에디터 설정에 시험의 테마가 남지 않게. 메모리(활성 설정)는 저장으로, 파일은 원래 바이트로(저장기가 줄끝 · 끝 줄바꿈을 바꾼다).
                SwatchProbe& probe = getSwatchProbe();
                EditorMenuBar::closeThemeDialog();
                EditorThemeUtil::applyTheme( probe._theme );
                EditorThemeUtil::saveToConfig();
                if ( probe._bConfigExisted && FileUtil::writeFile( getEditorConfigPath(), probe._configBytes.data(), probe._configBytes.size() ) == false )
                    (void)context.expect( false, "could not restore the editor config file" );
                // 원래 없던 파일은 지운다 — 못 지우면 시험 테마가 사용자 설정에 남으므로 위 쓰기 실패처럼 시험 실패로 알린다.
                if ( probe._bConfigExisted == false && FileUtil::removeFile( getEditorConfigPath() ) == false )
                    (void)context.expect( false, "could not remove the editor config file the test created" );
                probe = SwatchProbe{};
                moveMouseAway();
                return EditorSelfTestStep::Done;
            }

            // --------------------------------------------------------------------------------------------------
            // hierarchy.visibilityToggleFits — Hierarchy 가시성 토글이 아이콘을 담는다(폭이 글꼴 · DPI 배율을 따른다)
            // --------------------------------------------------------------------------------------------------
            static uint64& getToggleProbeObjectId()
            {
                static uint64 s_objectId = 0;
                return s_objectId;
            }

            static EditorSelfTestStep runVisibilityToggleFits( EditorSelfTestContext& context )
            {
                EditorContext*     pContext = EditorContext::get();
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( context.expect( pContext != nullptr && pManager != nullptr, "no editor context or active scene" ) == false )
                    return EditorSelfTestStep::Done;
                (void)pContext->getPanelManager().setPanelOpen( "hierarchy", true );
                HierarchyPanel* pHierarchy = static_cast<HierarchyPanel*>( pContext->getPanelManager().findPanel( "hierarchy" ) );
                uint64&         objectId   = getToggleProbeObjectId();
                const uint32    stepIndex  = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    // 줄이 하나는 있어야 토글이 그려진다 — 빈 씬에서도 돌게 하나 만들고, 앞 시험이 남긴 검색어를 지운다(걸러지면 줄이 없다).
                    if ( pHierarchy != nullptr )
                        pHierarchy->setFilterText( "" );
                    GameObject* pObj = pManager->createGameObject( hashed_string( "EditorSelfTestToggle" ) );
                    if ( context.expect( pObj != nullptr, "could not create the probe object" ) == false )
                        return EditorSelfTestStep::Done;
                    objectId = pObj->getObjectId();
                    return EditorSelfTestStep::Continue;
                }

                // 앞 시험이 연 창이 Hierarchy 를 가렸을 수 있다 — 몇 프레임은 이름표를 기다린다.
                constexpr uint32   kMaxMarkWaitStep = 10;
                EditorSelfTestMark mark{};
                const bool         bMarked = EditorSelfTestMarks::find( "hierarchy.activeToggle", mark );
                if ( bMarked == false && stepIndex < kMaxMarkWaitStep )
                {
                    ImGuiWindow* pWindow = ImGui::FindWindowByName( "Hierarchy" );
                    if ( pWindow != nullptr )
                        ImGui::FocusWindow( pWindow );
                    return EditorSelfTestStep::Continue;
                }
                if ( context.expect( bMarked, "the hierarchy visibility toggle left no mark" ) )
                {
                    const float32 width     = mark._max._x - mark._min._x;
                    const float32 height    = mark._max._y - mark._min._y;
                    const float32 iconWidth = ImGui::CalcTextSize( ICON_FA_EYE_SLASH ).x;
                    string        what{ "the visibility toggle (" };
                    what += to_string( width ) + " px) is narrower than its icon (" + to_string( iconWidth ) + " px)";
                    (void)context.expect( width >= iconWidth, what.c_str() );
                    (void)context.expect( width + 0.5f >= height, "the visibility toggle is not square - it does not follow the frame height" );
                }
                GameObject* pObj = pManager->findGameObjectById( objectId );
                if ( pObj != nullptr )
                    pManager->destroyObject( pObj );
                objectId = 0;
                return EditorSelfTestStep::Done;
            }

            // --------------------------------------------------------------------------------------------------
            // hierarchy.selectedRowLeavesTheToggleVisible — 고른 줄의 선택 배경 · 클릭 영역이 가시성 토글을 덮지 않는다
            // --------------------------------------------------------------------------------------------------
            static uint64& getRowProbeObjectId()
            {
                static uint64 s_objectId = 0;
                return s_objectId;
            }

            static EditorSelfTestStep runSelectedRowLeavesTheToggleVisible( EditorSelfTestContext& context )
            {
                constexpr const utf8* kProbeName       = "EditorSelfTestRowProbe";
                constexpr uint32      kMaxMarkWaitStep = 10;
                EditorContext*        pContext         = EditorContext::get();
                GameObjectManager*    pManager         = editor::getActiveObjectManager();
                if ( context.expect( pContext != nullptr && pManager != nullptr, "no editor context or active scene" ) == false )
                    return EditorSelfTestStep::Done;
                (void)pContext->getPanelManager().setPanelOpen( "hierarchy", true );
                HierarchyPanel* pHierarchy = static_cast<HierarchyPanel*>( pContext->getPanelManager().findPanel( "hierarchy" ) );
                uint64&         objectId   = getRowProbeObjectId();
                const uint32    stepIndex  = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    if ( pHierarchy != nullptr )
                        pHierarchy->setFilterText( "" );
                    GameObject* pObj = pManager->createGameObject( hashed_string( kProbeName ) );
                    if ( context.expect( pObj != nullptr, "could not create the probe object" ) == false )
                        return EditorSelfTestStep::Done;
                    objectId = pObj->getObjectId();
                    pContext->getWorkspace().selectGameObject( pObj );
                    return EditorSelfTestStep::Continue;
                }

                EditorSelfTestMark row{};
                EditorSelfTestMark toggle{};
                const bool         bMarked = EditorSelfTestMarks::find( string( "hierarchy.row." ) + kProbeName, row ) &&
                                     EditorSelfTestMarks::find( string( "hierarchy.toggle." ) + kProbeName, toggle );
                if ( bMarked == false && stepIndex < kMaxMarkWaitStep )
                    return EditorSelfTestStep::Continue;
                if ( context.expect( bMarked, "the probe row or its toggle left no mark" ) )
                {
                    string what{ "the selected row starts at x " };
                    what += to_string( row._min._x ) + ", over the toggle that ends at x " + to_string( toggle._max._x );
                    (void)context.expect( toggle._max._x <= row._min._x + 0.5f, what.c_str() );
                }
                pContext->getWorkspace().clearSelection();
                GameObject* pObj = pManager->findGameObjectById( objectId );
                if ( pObj != nullptr )
                    pManager->destroyObject( pObj );
                objectId = 0;
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( HierarchySearchTyping, "input.hierarchySearchTyping", 1000, &EditorSelfTestInputCasesInternal::runHierarchySearchTyping );
    SW_EDITOR_SELF_TEST( TooltipOnHover, "input.tooltipOnHover", 1010, &EditorSelfTestInputCasesInternal::runTooltipOnHover );
    SW_EDITOR_SELF_TEST( ClassicDarkSwatch, "input.classicDarkSwatch", 1020, &EditorSelfTestInputCasesInternal::runClassicDarkSwatch );
    SW_EDITOR_SELF_TEST( VisibilityToggleFits, "hierarchy.visibilityToggleFits", 1030, &EditorSelfTestInputCasesInternal::runVisibilityToggleFits );
    SW_EDITOR_SELF_TEST( SelectedRowToggle, "hierarchy.selectedRowLeavesTheToggleVisible", 1040,
                         &EditorSelfTestInputCasesInternal::runSelectedRowLeavesTheToggleVisible );
} // namespace sw::editor
