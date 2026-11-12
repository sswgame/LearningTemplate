#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Name/TagID.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorMenuBar.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/EditorPanelText.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Scene/HierarchyPanel.h"
#include "Editor/SelfTest/EditorSelfTest.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

#include "sw/config/ConfigConstants.h"

#include <imgui.h>
#include <imgui_internal.h>

// 에디터 자체 테스트 중 입력(클릭, 타이핑, 호버)을 흉내 내는 테스트들이다. EditorSelfTestInput 이 다음 프레임의 ImGui 입력 큐에 넣는다.
// 플랫폼 백엔드가 프레임마다 실제 커서를 넣을 수 있으므로, 누르기, 떼기, 호버는 그 단계마다 레이블 위로 다시 옮긴 뒤 넣는다.

namespace sw::editor
{
    namespace
    {
        struct EditorSelfTestInputCasesInternal
        {
            /** @brief 툴팁 지연(ImGuiHoveredFlags_DelayShort)을 기다리는 단계의 상한입니다. 실행기의 테스트당 상한(300)보다 작습니다. */
            static constexpr uint32 kMaxTooltipWaitStep = 240;

            /** @brief 클릭 하나의 누르기와 떼기를 레이블 위에서 넣습니다. 레이블이 없으면 false 입니다. */
            static bool pressOnMark( const utf8* pKey, bool bDown )
            {
                if ( EditorSelfTestInput::moveMouseToMark( pKey ) == false )
                    return false;
                EditorSelfTestInput::setMouseButton( ImGuiMouseButton_Left, bDown );
                return true;
            }

            /** @brief 마우스를 화면 밖으로 치웁니다. 다음 테스트의 호버를 흐리지 않게 하기 위해서입니다. */
            static void moveMouseAway() { EditorSelfTestInput::moveMouse( float2{ -10000.0f, -10000.0f } ); }

            // --------------------------------------------------------------------------------------------------
            // input.hierarchySearchTyping: 검색 입력란을 클릭하고 타이핑하면 `tag:` 필터가 걸리고, 없는 이름은 0건 안내를 그린다
            // --------------------------------------------------------------------------------------------------
            struct TypingProbe
            {
                void finishTyping( HierarchyPanel* pHierarchy, GameObjectManager* pManager ) const
                {
                    ImGui::ClearActiveID(); // 검색 입력란의 입력 상태를 놓는다. 다음 테스트의 키가 이리 오지 않게 한다
                    if ( pHierarchy != nullptr )
                        pHierarchy->setFilterText( "" );
                    EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                    editorContext.getSubsystem<EditorWorkspace>().clearSelection();
                    GameObject* pObj = ( pManager != nullptr ) ? pManager->findGameObjectByID( _objectID ) : nullptr;
                    if ( pObj != nullptr )
                        pManager->destroyGameObject( pObj );
                    moveMouseAway();
                }
                static EditorSelfTestStep runHierarchySearchTyping( EditorSelfTestContext& context )
                {
                    EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                    (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<HierarchyPanel>( true );
                    HierarchyPanel*    pHierarchy = editorContext.getSubsystem<EditorPanelManager>().findPanel<HierarchyPanel>();
                    GameObjectManager* pManager   = editor::getActiveObjectManager();
                    if ( context.expect( pHierarchy != nullptr && pManager != nullptr, "no hierarchy panel or active scene" ) == false )
                        return EditorSelfTestStep::Done;

                    TypingProbe& probe = context.getTestState<TypingProbe>();
                    switch ( probe._phase )
                    {
                        case 0:
                        {
                            GameObject* pObj = pManager->createGameObject( hashed_string( "EditorSelfTestTyped" ) );
                            if ( context.expect( pObj != nullptr, "could not create the probe object" ) == false )
                                return EditorSelfTestStep::Done;
                            pObj->addTag( TagID::request( "SwSelfTest.Typed" ) );
                            probe._objectID = pObj->getObjectID();
                            pHierarchy->setFilterText( "" );
                            ++probe._phase;
                            return EditorSelfTestStep::Continue; // 패널이 한 번 그려져 레이블이 생기게 한다
                        }
                        case 1:
                        case 5:
                        {
                            // 검색 입력란을 누른다(InputText 는 누를 때 잡힌다). 두 번째(5)는 지운 입력란을 다시 잡는다.
                            if ( context.expect( pressOnMark( "hierarchy.filter", true ), "the hierarchy search field left no mark" ) == false )
                            {
                                probe.finishTyping( pHierarchy, pManager );
                                return EditorSelfTestStep::Done;
                            }
                            ++probe._phase;
                            return EditorSelfTestStep::Continue;
                        }
                        case 2:
                        case 6:
                        {
                            if ( probe._phase == 2 )
                                probe._pressDiagnosis = describeInputTarget( "hierarchy.filter" );
                            (void)pressOnMark( "hierarchy.filter", false );
                            ++probe._phase;
                            return EditorSelfTestStep::Continue;
                        }
                        case 3:
                        {
                            EditorSelfTestInput::typeText( "tag:SwSelfTest" );
                            ++probe._phase;
                            return EditorSelfTestStep::Continue;
                        }
                        case 4:
                        {
                            // 글자 이벤트가 마우스 이벤트와 같은 프레임에 들어오면 ImGui 가 다음 프레임으로 흘려 보낸다(io.ConfigInputTrickleEventQueue). 그래서 몇 프레임 기다린다.
                            if ( pHierarchy->getFilterText() != "tag:SwSelfTest" && probe._waitFrames < TypingProbe::kMaxWaitFrames )
                            {
                                ++probe._waitFrames;
                                return EditorSelfTestStep::Continue;
                            }
                            probe._waitFrames = 0;
                            // 글자가 들어온 프레임에 패널이 입력란, 트리 순서로 그렸다. 글자가 입력란에 도착했는지, 필터가 프로브를 찾았는지 확인한다.
                            // 실패하면 이유에 누른 뒤와 현재의 입력 대상을 붙인다. 실행 한 번으로 "클릭이 입력란을 못 잡았다" 와 "글자가 안 들어갔다" 를 구분할 수 있다.
                            string what{ "typing did not reach the search field [after press: " };
                            what += probe._pressDiagnosis;
                            what += " | now: ";
                            what += describeInputTarget( "hierarchy.filter" );
                            what += "]";
                            (void)context.expect( pHierarchy->getFilterText() == "tag:SwSelfTest", what.c_str() );
                            StringBuilder<constant::buffer::kMax256> rootCountWhatBuilder;
                            rootCountWhatBuilder.appendFormat( "the typed tag filter did not find the probe object (visible roots %#)", pHierarchy->getVisibleRootCount() );
                            const string rootCountWhat{ rootCountWhatBuilder.view() };
                            (void)context.expect( pHierarchy->getVisibleRootCount() == 1, rootCountWhat.c_str() );
                            // 지우고 없는 이름을 입력한다. 입력 중인 입력란은 ImGui 가 가진 텍스트를 쓰므로, 먼저 놓고 지운 뒤 다시 누른다.
                            ImGui::ClearActiveID();
                            pHierarchy->setFilterText( "" );
                            ++probe._phase;
                            return EditorSelfTestStep::Continue;
                        }
                        case 7:
                        {
                            probe._hintCountBefore = EditorWidgets::getNoSearchResultHintCount();
                            EditorSelfTestInput::typeText( "zzSelfTestNoSuchObject" );
                            ++probe._phase;
                            return EditorSelfTestStep::Continue;
                        }
                        default:
                        {
                            if ( pHierarchy->getFilterText() != "zzSelfTestNoSuchObject" && probe._waitFrames < TypingProbe::kMaxWaitFrames )
                            {
                                ++probe._waitFrames;
                                return EditorSelfTestStep::Continue;
                            }
                            (void)context.expect( pHierarchy->getFilterText() == "zzSelfTestNoSuchObject", "the second typing did not reach the search field" );
                            (void)context.expect( pHierarchy->getVisibleRootCount() == 0, "a name that matches nothing still shows roots" );
                            (void)context.expect( EditorWidgets::getNoSearchResultHintCount() > probe._hintCountBefore, "zero matches did not draw the no-result hint" );
                            probe.finishTyping( pHierarchy, pManager );
                            return EditorSelfTestStep::Done;
                        }
                    }
                }

            private:
                string _pressDiagnosis{}; ///< 누른 프레임 뒤의 입력 대상(`describeInputTarget`). 실패 이유에 붙인다
                uint64 _objectID{ 0 };
                uint32 _hintCountBefore{ 0 };
                uint32 _phase{ 0 };      ///< 현재 단계. 프레임 번호가 아니며, 글자를 기다리는 동안 머문다
                uint32 _waitFrames{ 0 }; ///< 글자가 입력란에 도착하기를 기다린 프레임 수

                /** @brief 글자가 입력란에 도착하기를 기다리는 최대 프레임 수입니다. */
                static constexpr uint32 kMaxWaitFrames = 8;
            };

            /**
             * @brief 글자가 어디로 가는지 구분하는 상태 한 줄을 만듭니다. 활성 위젯, 레이블 위젯, 호버 위젯, 호버 창, 글자 입력 요구를 담습니다.
             * @details 활성 위젯이 레이블 위젯과 같은데 글자가 도착하지 않으면 글자 주입 경로가 문제이고, 다르면 클릭이 입력란을 잡지 못한 것이다. 호버 창이 다른 창이면
             *          그 창이 가로챈 것이다. 도킹 구분선이나 처음부터 열려 있던 떠 있는 창이 그 예다.
             */
            static string describeInputTarget( const utf8* pMarkKey )
            {
                const ImGuiContext&                     imguiContext = *ImGui::GetCurrentContext();
                EditorSelfTestMark                      mark{};
                const bool                              bHasMark       = EditorSelfTestMarks::find( pMarkKey, mark );
                const ImGuiWindow*                      pHoveredWindow = imguiContext.HoveredWindow;
                fixed_string<constant::buffer::kMax512> text;
                formatstring( text.data(), text.capacity(), "active=%u field=%u hoveredID=%u hoveredWindow=%s wantTextInput=%d",
                              static_cast<uint32>( imguiContext.ActiveId ), bHasMark ? mark._itemID : 0u, static_cast<uint32>( imguiContext.HoveredId ),
                              pHoveredWindow != nullptr && pHoveredWindow->Name != nullptr ? pHoveredWindow->Name : "(none)",
                              ImGui::GetIO().WantTextInput ? 1 : 0 );
                return string{ text.c_str() };
            }

            // --------------------------------------------------------------------------------------------------
            // input.tooltipOnHover: 마우스를 버튼 위에 두면 툴팁 창이 뜬다. EditorWidgets::drawTooltip 의 지연도 포함한다
            // --------------------------------------------------------------------------------------------------
            static bool isAnyTooltipShown()
            {
                const ImGuiContext* pImGui = ImGui::GetCurrentContext();
                if ( pImGui == nullptr )
                    return false;
                for ( const ImGuiWindow* pWindow : pImGui->Windows )
                {
                    const bool bActiveTooltipWindow = pWindow != nullptr && ( pWindow->Flags & ImGuiWindowFlags_Tooltip ) != 0 && pWindow->Active;
                    if ( bActiveTooltipWindow )
                        return true;
                }
                return false;
            }

            /** @brief 제목에 @p pTitlePart 가 들어 있는 창을 찾습니다. 아이콘 문자가 붙은 제목도 찾습니다. 없으면 nullptr 입니다. */
            static ImGuiWindow* findWindowByTitlePart( const utf8* pTitlePart )
            {
                const ImGuiContext* pImGui = ImGui::GetCurrentContext();
                if ( pImGui == nullptr )
                    return nullptr;
                for ( ImGuiWindow* pWindow : pImGui->Windows )
                {
                    const bool bTitleMatchesWindow = pWindow != nullptr && pWindow->Name != nullptr && StringUtil::contains( pWindow->Name, pTitlePart );
                    if ( bTitleMatchesWindow )
                        return pWindow;
                }
                return nullptr;
            }

            static EditorSelfTestStep runTooltipOnHover( EditorSelfTestContext& context )
            {
                // 테스트가 그리는 창 하나다. 패널 배치와 무관하게 항상 같은 위치이고 맨 위에 있다.
                // 주 뷰포트 안에 둔다. 데스크톱 좌표 (40, 300) 은 창 밖일 수 있고, 그러면 창이 자기 플랫폼 창(뷰포트)으로 떠서 호버가 실제 커서를 따른다.
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
                    EditorSelfTestMarks::record( "selftest.tooltipButton" );
                    EditorWidgets::drawTooltip( "self-test tooltip" );
                }
                const bool bShown = bVisible && isAnyTooltipShown();

                ImGui::End();

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                    return EditorSelfTestStep::Continue; // 레이블이 생기도록 한 번 그린다
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
                // 지연 동안 마우스를 버튼 위에 둔다. 실제 커서가 끼어들어도 이 프레임의 마지막 위치는 버튼이다.
                if ( EditorSelfTestInput::moveMouseToMark( "selftest.tooltipButton" ) == false )
                {
                    (void)context.expect( false, "the probe button left no mark" );
                    return EditorSelfTestStep::Done;
                }
                return EditorSelfTestStep::Continue;
            }

            // --------------------------------------------------------------------------------------------------
            // input.coveredMarkHitTest: 같은 창의 자식 오버레이가 덮은 레이블은 "가려짐", 덮지 않은 레이블 · 레이블이 담은 자식 창은 "닿음" 이다(`hitTestMark`)
            // --------------------------------------------------------------------------------------------------
            /** @brief 지금 조준하는 레이블과 그 판정을 기다린 프레임 수입니다. */
            struct HitTestProbe
            {
                uint32 _caseIndex{ 0 };
                uint32 _waitSteps{ 0 };
            };

            /** @brief 가림 판정을 기다리는 단계의 상한입니다(포인터 도착 + 그 프레임의 표본 — 보통 두세 프레임). */
            static constexpr uint32 kMaxHitWaitStep = 30;
            /** @brief 프로브 창의 주 뷰포트 작업 영역 안 자리 · 크기와 그 안 위젯 크기입니다(px). 덮개는 덮일 버튼 가운데를 덮되 버튼 밖으로 나간다. */
            static constexpr ImVec2 kHitProbeWindowOffset{ 40.0f, 300.0f };
            static constexpr ImVec2 kHitProbeWindowSize{ 320.0f, 220.0f };
            static constexpr ImVec2 kHitProbeWidgetSize{ 120.0f, 40.0f };
            static constexpr ImVec2 kHitProbeCoverOffset{ 40.0f, 10.0f };
            static constexpr ImVec2 kHitProbeCoverSize{ 200.0f, 30.0f };

            /** @brief 프로브 창 하나를 그립니다: 덮일 버튼 위에 자식 창(씬 뷰 하단 툴바와 같은 오버레이), 덮이지 않은 버튼, 자식 창을 담은 레이블. */
            static void drawHitTestProbeWindow()
            {
                const ImGuiViewport* pMainViewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowViewport( pMainViewport->ID );
                ImGui::SetNextWindowPos( ImVec2( pMainViewport->WorkPos.x + kHitProbeWindowOffset.x, pMainViewport->WorkPos.y + kHitProbeWindowOffset.y ), ImGuiCond_Always );
                ImGui::SetNextWindowSize( kHitProbeWindowSize, ImGuiCond_Always );
                ImGui::SetNextWindowFocus();
                if ( ImGui::Begin( "Self Test Hit Probe##EditorSelfTestHitProbe", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking ) )
                {
                    (void)ImGui::Button( "Covered##SelfTestHit", kHitProbeWidgetSize );
                    EditorSelfTestMarks::record( "selftest.hit.covered" );
                    const ImVec2 coveredMin = ImGui::GetItemRectMin();
                    (void)ImGui::Button( "Open##SelfTestHit", kHitProbeWidgetSize );
                    EditorSelfTestMarks::record( "selftest.hit.open" );
                    (void)ImGui::BeginChild( "##SelfTestHitContent", kHitProbeWidgetSize, ImGuiChildFlags_Borders );
                    ImGui::TextUnformatted( "content" );
                    ImGui::EndChild();
                    EditorSelfTestMarks::record( "selftest.hit.content" );
                    // 덮개는 버튼 뒤에 같은 창의 자식으로 그린다 — 씬 뷰 오버레이 툴바(`EditorViewportOverlays::drawBar`)와 같은 모양이다.
                    // 버튼 가운데를 덮되 버튼 사각형 밖으로 나가게 둔다 — 레이블 안에 든 자식 창은 레이블이 담은 내용으로 치기 때문이다.
                    ImGui::SetCursorScreenPos( ImVec2( coveredMin.x + kHitProbeCoverOffset.x, coveredMin.y + kHitProbeCoverOffset.y ) );
                    (void)ImGui::BeginChild( "##SelfTestHitCover", kHitProbeCoverSize, ImGuiChildFlags_Borders );
                    ImGui::TextUnformatted( "cover" );
                    ImGui::EndChild();
                }
                ImGui::End();
            }

            static EditorSelfTestStep runCoveredMarkHitTest( EditorSelfTestContext& context )
            {
                struct HitCase
                {
                    const utf8*   _pMark;
                    EditorMarkHit _expected;
                };
                constexpr HitCase kArrCase[] = {
                    {"selftest.hit.covered",   EditorMarkHit::Covered},
                    {   "selftest.hit.open", EditorMarkHit::Reachable},
                    {"selftest.hit.content", EditorMarkHit::Reachable},
                };
                drawHitTestProbeWindow();
                if ( context.getStepIndex() == 0 )
                    return EditorSelfTestStep::Continue; // 레이블이 생기도록 한 번 그린다
                HitTestProbe& probe = context.getTestState<HitTestProbe>();
                if ( probe._caseIndex >= std::size( kArrCase ) )
                {
                    EditorSelfTestInput::releaseMouseHold();
                    moveMouseAway();
                    return EditorSelfTestStep::Done;
                }
                const HitCase& hitCase = kArrCase[probe._caseIndex];
                if ( context.expect( EditorSelfTestInput::holdMouseAtMark( hitCase._pMark ), "the hit probe window left no mark" ) == false )
                {
                    EditorSelfTestInput::releaseMouseHold();
                    return EditorSelfTestStep::Done;
                }
                string              coverName;
                const EditorMarkHit hit = EditorSelfTestInput::hitTestMark( hitCase._pMark, float2{ 0.5f, 0.5f }, coverName );
                if ( hit == EditorMarkHit::Pending && ++probe._waitSteps < kMaxHitWaitStep )
                    return EditorSelfTestStep::Continue;
                StringBuilder<constant::buffer::kMax256> whatBuilder;
                whatBuilder.appendFormat( "hit test of '%#' is %# (cover '%#'), expected %#", hitCase._pMark, static_cast<uint32>( hit ), coverName.c_str(),
                                          static_cast<uint32>( hitCase._expected ) );
                const string what{ whatBuilder.view() };
                (void)context.expect( hit == hitCase._expected, what.c_str() );
                ++probe._caseIndex;
                probe._waitSteps = 0;
                return EditorSelfTestStep::Continue;
            }

            // --------------------------------------------------------------------------------------------------
            // input.themeSwatch: 기본이 아닌 테마(라이트)에서 테마 대화상자의 스와치를 누르면 액센트가 바뀐다. 끝나면 원래 테마로 되돌린다
            // --------------------------------------------------------------------------------------------------
            /** @brief 테스트 앞의 테마와 에디터 설정 파일 바이트입니다. 스와치 클릭은 설정 파일을 다시 쓰므로(saveToConfig), 끝나면 바이트째 되돌립니다. */
            struct SwatchProbe
            {
                SwatchProbe()
                    : _theme{}
                    , _configBytes{}
                    , _bConfigExisted{ SW_FALSE }
                {
                }

                static EditorSelfTestStep runThemeSwatch( EditorSelfTestContext& context )
                {
                    // 세 번째 스와치(Neon Violet)의 색은 대화상자와 같은 토큰에서 읽는다 — 숫자를 따로 적으면 토큰 값(#RRGGBB 반올림)이 바뀔 때 어긋났다.
                    const Color violet = SW_THEME_COLOR( "AccentSwatch", "NeonViolet" );
                    switch ( context.getStepIndex() )
                    {
                        case 0:
                        {
                            SwatchProbe& probe = context.getTestState<SwatchProbe>();
                            probe._theme       = EditorThemeUtil::getActiveTheme();
                            // 파일이 없을 수 있다. Saved/Editor 는 git 이 무시하므로 새 체크아웃에는 없다. 없으면 읽지 않는다. readFile 은 없는 파일을 오류로 남기기 때문이다.
                            const string configPath = getEditorConfigPath();
                            probe._bConfigExisted   = ( FileUtil::isRegularFile( configPath ) && FileUtil::readFile( configPath, probe._configBytes ) ) ? SW_TRUE : SW_FALSE;
                            (void)context.expect( EditorThemeUtil::applyThemeByName( "light" ), "the light theme token set is missing" );
                            EditorMenuBar::openThemeDialog();
                            return EditorSelfTestStep::Continue;
                        }
                        case 1:
                        {
                            // 대화상자가 한 번 그려져서 레이블이 생겼다. 떠 있는 창(자기 플랫폼 창)이어도 레이블의 뷰포트를 함께 넣으므로 클릭이 닿는다.
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
                            (void)pressOnMark( "theme.swatch.violet", false ); // 버튼은 놓을 때 눌린다. 놓는 위치도 스와치 위여야 한다
                            return EditorSelfTestStep::Continue;
                        }
                        case 5:
                        {
                            return EditorSelfTestStep::Continue;
                        }
                        default:
                        {
                            const Color& accent       = SW_THEME_COLOR( "Base", "Accent" );
                            const bool   bIsVioletNow = accent._r == violet._r && accent._g == violet._g && accent._b == violet._b;
                            (void)context.expect( bIsVioletNow, "clicking a swatch in the light theme dialog did not change the accent" );
                            (void)context.expect( EditorThemeUtil::getActiveTheme()._name == "light", "the swatch switched the theme away from light" );
                            break;
                        }
                    }
                    // 되돌린다. 사용자의 에디터 설정에 테스트의 테마가 남지 않게 하기 위해서다. 메모리(활성 설정)는 저장으로, 파일은 원래 바이트로 되돌린다. 저장기가 줄 끝과 마지막 줄바꿈을 바꾸기 때문이다.
                    SwatchProbe& probe = context.getTestState<SwatchProbe>();
                    EditorMenuBar::closeThemeDialog();
                    EditorThemeUtil::applyTheme( probe._theme );
                    EditorThemeUtil::saveToConfig();
                    if ( probe._bConfigExisted && FileUtil::writeFile( getEditorConfigPath(), probe._configBytes.data(), probe._configBytes.size() ) == false )
                        (void)context.expect( false, "could not restore the editor config file" );
                    // 원래 없던 파일은 지운다. 지우지 못하면 테스트 테마가 사용자 설정에 남으므로 위의 쓰기 실패처럼 테스트 실패로 알린다.
                    if ( probe._bConfigExisted == false && FileUtil::removeFile( getEditorConfigPath() ) == false )
                        (void)context.expect( false, "could not remove the editor config file the test created" );
                    moveMouseAway();
                    return EditorSelfTestStep::Done;
                }

            private:
                EditorThemeConfig _theme;
                vector<uint8>     _configBytes;
                uint8             _bConfigExisted : 1;
            };

            static string getEditorConfigPath() { return EditorPreferencesStore::getDefaultFilePath(); }

            // --------------------------------------------------------------------------------------------------
            // hierarchy.visibilityToggleFits: Hierarchy 가시성 토글이 아이콘을 담을 수 있다. 폭이 글꼴과 DPI 배율을 따른다
            // --------------------------------------------------------------------------------------------------
            /** @brief 이 테스트가 만든 오브젝트입니다. */
            struct ToggleProbe
            {
                static EditorSelfTestStep runVisibilityToggleFits( EditorSelfTestContext& context )
                {
                    EditorContext&     editorContext = editor::getSubsystem<EditorContext>();
                    GameObjectManager* pManager      = editor::getActiveObjectManager();
                    if ( context.expect( pManager != nullptr, "no active scene" ) == false )
                        return EditorSelfTestStep::Done;
                    (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<HierarchyPanel>( true );
                    HierarchyPanel* pHierarchy = editorContext.getSubsystem<EditorPanelManager>().findPanel<HierarchyPanel>();
                    uint64&         objectID   = context.getTestState<ToggleProbe>()._objectID;
                    const uint32    stepIndex  = context.getStepIndex();
                    if ( stepIndex == 0 )
                    {
                        // 행이 하나는 있어야 토글이 그려진다. 빈 씬에서도 실행되도록 하나 만들고, 앞 테스트가 남긴 검색어를 지운다. 걸러지면 행이 없다.
                        if ( pHierarchy != nullptr )
                            pHierarchy->setFilterText( "" );
                        GameObject* pObj = pManager->createGameObject( hashed_string( "EditorSelfTestToggle" ) );
                        if ( context.expect( pObj != nullptr, "could not create the probe object" ) == false )
                            return EditorSelfTestStep::Done;
                        objectID = pObj->getObjectID();
                        return EditorSelfTestStep::Continue;
                    }

                    // 앞 테스트가 연 창이 Hierarchy 를 가렸을 수 있으므로 몇 프레임은 레이블을 기다린다.
                    constexpr uint32   kMaxMarkWaitStep = 10;
                    EditorSelfTestMark mark{};
                    const bool         bMarked = EditorSelfTestMarks::find( "hierarchy.activeToggle", mark );
                    if ( bMarked == false && stepIndex < kMaxMarkWaitStep )
                    {
                        ImGuiWindow* pWindow = ImGui::FindWindowByName( EditorPanelText::makeWindowID( HierarchyPanel::kPanelTitle ).c_str() );
                        if ( pWindow != nullptr )
                            ImGui::FocusWindow( pWindow );
                        return EditorSelfTestStep::Continue;
                    }
                    if ( context.expect( bMarked, "the hierarchy visibility toggle left no mark" ) )
                    {
                        const float32                            width     = mark._max._x - mark._min._x;
                        const float32                            height    = mark._max._y - mark._min._y;
                        const float32                            iconWidth = ImGui::CalcTextSize( SW_EDITOR_ICON( "EyeSlash" ) ).x;
                        string                                   what{ "the visibility toggle (" };
                        StringBuilder<constant::buffer::kMax256> whatPart;
                        whatPart.appendFormat( "%# px) is narrower than its icon (%# px)", width, iconWidth );
                        what += whatPart.c_str();
                        (void)context.expect( width >= iconWidth, what.c_str() );
                        (void)context.expect( width + 0.5f >= height, "the visibility toggle is not square - it does not follow the frame height" );
                    }
                    GameObject* pObj = pManager->findGameObjectByID( objectID );
                    if ( pObj != nullptr )
                        pManager->destroyGameObject( pObj );
                    return EditorSelfTestStep::Done;
                }

            private:
                uint64 _objectID{ 0 };
            };

            // --------------------------------------------------------------------------------------------------
            // hierarchy.selectedRowLeavesTheToggleVisible: 선택한 행의 선택 배경과 클릭 영역이 가시성 토글을 덮지 않는다
            // --------------------------------------------------------------------------------------------------
            /** @brief 이 테스트가 만든 오브젝트입니다. */
            struct RowProbe
            {
                static EditorSelfTestStep runSelectedRowLeavesTheToggleVisible( EditorSelfTestContext& context )
                {
                    constexpr const utf8* kRowTestObjectName = "EditorSelfTestRowProbe";
                    constexpr uint32      kMaxMarkWaitStep   = 10;
                    EditorContext&        editorContext      = editor::getSubsystem<EditorContext>();
                    GameObjectManager*    pManager           = editor::getActiveObjectManager();
                    if ( context.expect( pManager != nullptr, "no active scene" ) == false )
                        return EditorSelfTestStep::Done;
                    (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<HierarchyPanel>( true );
                    HierarchyPanel* pHierarchy = editorContext.getSubsystem<EditorPanelManager>().findPanel<HierarchyPanel>();
                    uint64&         objectID   = context.getTestState<RowProbe>()._objectID;
                    const uint32    stepIndex  = context.getStepIndex();
                    if ( stepIndex == 0 )
                    {
                        if ( pHierarchy != nullptr )
                            pHierarchy->setFilterText( "" );
                        GameObject* pObj = pManager->createGameObject( hashed_string( kRowTestObjectName ) );
                        if ( context.expect( pObj != nullptr, "could not create the probe object" ) == false )
                            return EditorSelfTestStep::Done;
                        objectID = pObj->getObjectID();
                        editorContext.getSubsystem<EditorWorkspace>().selectGameObject( pObj );
                        return EditorSelfTestStep::Continue;
                    }

                    EditorSelfTestMark row{};
                    EditorSelfTestMark toggle{};
                    const bool         bMarked = EditorSelfTestMarks::find( string( "hierarchy.row." ) + kRowTestObjectName, row ) &&
                                         EditorSelfTestMarks::find( string( "hierarchy.toggle." ) + kRowTestObjectName, toggle );
                    if ( bMarked == false && stepIndex < kMaxMarkWaitStep )
                        return EditorSelfTestStep::Continue;
                    if ( context.expect( bMarked, "the probe row or its toggle left no mark" ) )
                    {
                        string                                   what{ "the selected row starts at x " };
                        StringBuilder<constant::buffer::kMax256> whatPart;
                        whatPart.appendFormat( "%#, over the toggle that ends at x %#", row._min._x, toggle._max._x );
                        what += whatPart.c_str();
                        (void)context.expect( toggle._max._x <= row._min._x + 0.5f, what.c_str() );
                    }
                    editorContext.getSubsystem<EditorWorkspace>().clearSelection();
                    GameObject* pObj = pManager->findGameObjectByID( objectID );
                    if ( pObj != nullptr )
                        pManager->destroyGameObject( pObj );
                    return EditorSelfTestStep::Done;
                }

            private:
                uint64 _objectID{ 0 };
            };
        };
    } // namespace

    SW_EDITOR_SELF_TEST( HierarchySearchTyping, "input.hierarchySearchTyping", 1000, &EditorSelfTestInputCasesInternal::TypingProbe::runHierarchySearchTyping );
    SW_EDITOR_SELF_TEST( TooltipOnHover, "input.tooltipOnHover", 1010, &EditorSelfTestInputCasesInternal::runTooltipOnHover );
    SW_EDITOR_SELF_TEST( CoveredMarkHitTest, "input.coveredMarkHitTest", 1015, &EditorSelfTestInputCasesInternal::runCoveredMarkHitTest );
    SW_EDITOR_SELF_TEST( ThemeSwatch, "input.themeSwatch", 1020, &EditorSelfTestInputCasesInternal::SwatchProbe::runThemeSwatch );
    SW_EDITOR_SELF_TEST( VisibilityToggleFits, "hierarchy.visibilityToggleFits", 1030, &EditorSelfTestInputCasesInternal::ToggleProbe::runVisibilityToggleFits );
    SW_EDITOR_SELF_TEST( SelectedRowToggle, "hierarchy.selectedRowLeavesTheToggleVisible", 1040,
                         &EditorSelfTestInputCasesInternal::RowProbe::runSelectedRowLeavesTheToggleVisible );
} // namespace sw::editor
