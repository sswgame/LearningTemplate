/**
 * @file EditorScenarioSteps.cpp
 * @brief 자동화 시나리오의 에디터 단계를 구현합니다. 단계는 `EditorClick`, `EditorText`, `EditorKey`, `EditorExpectObject`, `EditorExpectDockLayout` 입니다. 에디터 프로브(`Editor.*`)는 각 패널 · 기능 폴더의 `*Probes.cpp` 가 등록합니다.
 * @details 에디터 패널 입력은 엔진 입력 레이어가 아니라 ImGui 가 받으므로 ImGui 이벤트로 넣는다. 게임 뷰 입력은 가상 입력 장치(`<Tap>` 등)로,
 *          에디터 패널 입력은 이 단계로 넣으며, 두 방식을 한 시나리오 파일에서 함께 쓸 수 있다. 에디터 모듈이 로드된 실행(`-EnableEditor`)에서만 등록되므로,
 *          에디터 없이 이 단계나 프로브를 쓰면 시작할 때 읽기 오류가 난다. 플레이, 저장, 열기 같은 에디터 명령은 엔진 `DevCommand` 단계로 호출한다(`play`, `stop`, `editor <커맨드 id>`, `scene.saveAs`).
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/EditorSceneIterationAccess.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorScenarioStepValidation.h"
#include "Editor/SelfTest/EditorSelfTest.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/SelfTest/EditorStabilityWait.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    /**
     * @brief `-gv_editorSelfTestPattern=<패턴>`: 시나리오의 `<EditorSelfTest pattern>` 대신 이 패턴의 자체 테스트만 돈다(비면 시나리오 값).
     * @details 진 케이스 하나를 다시 볼 때 시나리오 사본을 만들지 않는다(언리얼 `-ExecCmds="Automation RunTests <필터>"`, 유니티 `-testFilter` 자리).
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorSelfTestPattern, "", "Run only editor self tests matching this pattern instead of the scenario's <EditorSelfTest pattern> (empty = scenario value)" );

    namespace
    {
        /**
         * @class EditorSelfTestStepState
         * @brief 시나리오 단계 `EditorSelfTest` 가 시작시킨 실행의 단계입니다. 단계는 한 번에 하나만 돈다(자체 테스트 실행도 하나뿐이다).
         * @details 에디터 서브시스템이라 컨텍스트와 함께 생기고 사라진다 — 정적 칸이면 컨텍스트를 새로 만들어도 지난 단계 주소가 남는다.
         */
        class EditorSelfTestStepState final : public EditorSubsystem
        {
        public:
            static constexpr const utf8* kSubsystemName  = "EditorSelfTestStepState";
            static constexpr int32       kSubsystemOrder = 9510; ///< 자체 테스트 세션(9500) 뒤

            const AutomationStep* _pRequestedStep{ nullptr }; ///< 실행을 청한 단계(빌림 — 시나리오가 단계를 든다)
            const utf8*           getSubsystemName() const override { return kSubsystemName; }
        };

        struct EditorScenarioStepsInternal
        {
            /** @brief `EditorExpectDockLayout` 의 도크 노드 최소 변(px) 기본값입니다. 이보다 좁은 노드는 탭 글자도 담지 못합니다. */
            static constexpr float32 kDefaultMinDockNodeSide = 24.0f;
            /** @brief 창과 도크 사각형을 비교할 때의 픽셀 허용치입니다. */
            static constexpr float32 kPixelTolerance = 1.0f;

            using Validation = EditorScenarioStepValidation;
            using PressState = EditorPressState;

            /**
             * @brief 위젯 레이블 기록을 시작합니다(모든 에디터 단계의 준비 함수). 레이블은 기록을 켠 뒤에 그린 위젯만 기록됩니다.
             * @details 시나리오는 첫 에디터 프레임 앞에 시작하므로 켜는 곳(`EditorStateStore`)은 컨텍스트와 함께 이미 있어야 하고, 없으면(에디터가 꺼진 실행) 켠 척하지 않고 오류로 멈춘다.
             *          예전에는 검사 함수가 켜면서 결과를 버려, 켜는 곳이 아직 없던 때의 호출이 조용히 사라져 모든 클릭이 "no editor widget is marked" 로 졌다.
             */
            [[nodiscard]] static expected<void> prepareMarks( AutomationStepContext& /*runner*/, const AutomationStep& /*step*/ )
            {
                if ( EditorSelfTestMarks::setEnabled( true ) )
                    return {};
                return unexpected( Error{ ErrorKind::Unsupported, "needs the editor (no editor context to record widget marks - run with -EnableEditor)" } );
            }

            // ------------------------------------------------------------------------------
            // EditorClick, EditorText, EditorKey: ImGui 입력
            // ------------------------------------------------------------------------------
            /** @brief `mods` 마스크(`EditorModifierBit`)를 ImGui 수정자 키 목록으로 바꿉니다. 읽기와 모르는 이름 검사는 `Validation::parseModifiers` 가 한다. */
            [[nodiscard]] static vector<int32> makeImGuiModifiers( uint8 modifierMask )
            {
                vector<int32> listKey;
                if ( ( modifierMask & EditorModifierBit::kCtrl ) != 0 )
                    listKey.push_back( ImGuiKey_LeftCtrl );
                if ( ( modifierMask & EditorModifierBit::kShift ) != 0 )
                    listKey.push_back( ImGuiKey_LeftShift );
                if ( ( modifierMask & EditorModifierBit::kAlt ) != 0 )
                    listKey.push_back( ImGuiKey_LeftAlt );
                return listKey;
            }

            /**
             * @brief @p mark 가 그린 창의 보이는 영역 밖(스크롤 아래 · 접힌 위)이면 실패로 알리고 true 를 반환합니다.
             * @details 잘린 위젯의 사각형을 누르면 그 위치에 보이는 다른 위젯이나 빈 곳이 받아, 단계는 성공하고 뒤의 기대만 엉뚱하게 진다.
             */
            static bool reportClippedMark( AutomationStepContext& runner, const AutomationStep& step, const string& mark )
            {
                EditorSelfTestMark found{};
                if ( EditorSelfTestMarks::find( mark, found ) == false || found._bClipped == false )
                    return false;
                runner.recordFailure( step, "editor widget '" + mark + "' is still outside the visible area of its window after scrolling it into view (a collapsed header or a non-scrolling window hides it)" );
                return true;
            }

            /**
             * @brief 레이블에 포인터를 두고(`EditorSelfTestInput::hoverMark`) 그 점이 레이블에 닿지 않으면 실패로 알리고 true 를 반환합니다. 누르기 · 호버 전에 부른다.
             * @details 잘림 검사는 창 밖으로 잘린 것만 잡았다 — 씬 뷰 하단 툴바(같은 창의 자식 오버레이)가 기즈모를 덮어 끌기가 툴바를 눌렀는데 단계는 성공했다(editor.gizmotoplevel).
             *          아직 판정이 없으면(조준 붙잡기가 시한을 넘겼다) 누르지 않는다 — 옛 좌표로 누르면 엉뚱한 것을 누른다.
             */
            static bool reportUnreachableMark( AutomationStepContext& runner, const AutomationStep& step, const string& mark, const float2& anchor )
            {
                string              coverName;
                const EditorMarkHit hit = EditorSelfTestInput::hoverMark( mark, anchor, coverName );
                if ( hit == EditorMarkHit::Reachable )
                    return false;
                EditorSelfTestInput::releaseMouseHold();
                if ( hit == EditorMarkHit::Missing )
                    runner.recordFailure( step, "no editor widget is marked '" + mark + "' (was it drawn in the last frame?)" );
                else if ( hit == EditorMarkHit::Covered )
                    runner.recordFailure( step, "editor widget '" + mark + "' is covered by window '" + coverName + "' at the press point - the press would go there (move the camera/target or the overlay)" );
                else
                    runner.recordFailure( step, "the pointer has not reached editor widget '" + mark + "' yet (it kept moving) - not pressing at a stale point" );
                return true;
            }

            /** @brief 입력 단계가 대상이 자리 잡기를 기다리는 최대 프레임 수입니다. 넘기면 단계가 돌며 실패를 적는다. */
            static constexpr uint32 kStabilityWaitFrames = 60;

            /** @brief 레이블 @p pMark 가 그려졌는지 · 자리 잡았는지 봅니다. 잘려 있으면 그 창을 스크롤해 보이게 하고 자리 잡지 않은 것으로 센다. nullptr(적지 않은 칸)는 둘 다 참입니다. */
            static void checkMark( const string* pMark, bool& inoutDrawn, bool& inoutSettled )
            {
                if ( pMark == nullptr )
                    return;
                EditorSelfTestMark found{};
                const bool         bDrawn = EditorSelfTestMarks::find( *pMark, found );
                if ( bDrawn && found._bClipped )
                    EditorSelfTestMarks::requestScrollIntoView( *pMark );
                inoutDrawn   = inoutDrawn && bDrawn;
                inoutSettled = inoutSettled && bDrawn && found.isSettled() && found._bClipped == false;
            }

            /** @brief 누른 채 둔 버튼 · 키를 놓는 단계(`state="Up"`)나 호버를 푸는 단계(`release`)면 true 입니다 — 기다리면 누른 시간이 길어진다(뷰포트 비행이 더 간다). */
            [[nodiscard]] static bool isRelease( const AutomationStep& step )
            {
                const string* pState = step.findAttribute( "state" );
                return ( pState != nullptr && *pState == "Up" ) || step.findAttribute( "release" ) != nullptr;
            }

            /**
             * @brief 단계가 누를 레이블(`mark` · `toMark`)이 두 프레임 연속 같은 자리에 그려지고 앞 단계의 입력이 다 들어갈 때까지 시나리오 시계를 붙잡습니다(최대 `kStabilityWaitFrames`).
             * @details 레이블은 직전 프레임에 그려져야 하고, 창이 열리거나 도킹 · 팝업이 자리를 잡는 중이면 움직인다. 예전에는 그려지기만 보아 시나리오가 입력 앞에 고정 대기(10 프레임 이상)를
             *          두었고(약 260 곳), 부하가 걸린 기계에서는 그것도 모자랐다. 언리얼 Automation Driver 의 "보일 때까지" · Playwright 의 actionability("stable") 확인과 같다.
             *          기다린 프레임은 `[Scenario] … held the clock` 로그로 남는다. 다 기다려도 그려졌는데 움직이면 실패로 적는다(그려지지 않았으면 단계가 "no editor widget" 을 적는다).
             *          기록을 쓰는 단계가 직접 다시 켠다. 백엔드 교체 · 모듈 리로드가 에디터 컨텍스트를 새로 만들면 새 저장소는 꺼져 있고, 시나리오 준비는 한 번뿐이라 다시 오지 않는다.
             */
            static AutomationStepHold holdUntilMarkStable( AutomationStepContext& runner, const AutomationStep& step, InputManager& input )
            {
                if ( EditorSelfTestMarks::setEnabled( true ) == false )
                    return AutomationStepHold::Run; // 에디터 컨텍스트가 없다 — 단계가 돌며 실패를 적는다
                const string* pMark = step.findAttribute( "mark" );
                if ( pMark == nullptr || isRelease( step ) )
                    return AutomationStepHold::Run; // 호버 풀기 · 누른 것 놓기 — 누를 곳이 없다
                bool bDrawn   = true;
                bool bSettled = true;
                checkMark( pMark, bDrawn, bSettled );
                checkMark( step.findAttribute( "toMark" ), bDrawn, bSettled );
                const bool               bReady = bSettled && EditorSelfTestInput::hasPendingEvents() == false;
                const AutomationStepHold hold   = runner.holdUntilReady( step, input, bReady, kStabilityWaitFrames,
                                                                         "editor widget '" + *pMark + ( bDrawn ? "' to settle (same rect two frames in a row, no pending input)" : "' to be drawn" ) );
                if ( hold == AutomationStepHold::Run && bReady == false && bDrawn )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "editor widget '%#' did not settle (same rect two frames in a row, no pending input) within %# frame(s)", pMark->c_str(),
                                              kStabilityWaitFrames );
                    runner.recordFailure( step, textBuilder.view() );
                }
                return hold;
            }

            /**
             * @brief 클릭 · 끌기 · 호버는 포인터를 레이블에 옮기고, 포인터가 거기 있는 상태로 레이블이 멈추고 그 점의 최상위 창이 판정될 때까지 시나리오 시계를 붙잡습니다.
             * @details 사람의 클릭과 같다: 포인터가 먼저 도착한다. 포인터가 오면 호버 때문에 배치가 바뀌기도 하므로(선택 동기화 · 툴팁) 옛 좌표를 큐에 넣어 누르면
             *          엉뚱한 칸을 누른다. 지난 프레임 자리로 답하는 쪽(ImGuizmo::IsOver)도 포인터가 한 프레임 먼저 와 있어야 맞게 답한다. 언리얼 Automation Driver 의 "이동 후 클릭" 과 같은 자리다.
             *          그 점의 창 판정(`EditorSelfTestInput::hoverMark`)은 포인터가 닿은 프레임이 끝나야 나온다 — 단계(`runClick` · `runDrag` · `runHover`)가 가려졌으면 누르지 않고 실패로 적는다.
             *          끌기는 `toMark` 도 자리 잡기를 기다린다(클릭에는 그 속성이 없어 빈칸이다).
             */
            template <typename TParams>
            static AutomationStepHold holdUntilHoverReady( AutomationStepContext& runner, const AutomationStep& step, InputManager& input )
            {
                if ( isRelease( step ) )
                    return holdUntilMarkStable( runner, step, input );
                if ( EditorSelfTestMarks::setEnabled( true ) == false )
                    return AutomationStepHold::Run; // 에디터 컨텍스트가 없다 — 단계가 돌며 실패를 적는다
                const TParams params   = step.readValidatedProperties<TParams>();
                bool          bDrawn   = true;
                bool          bSettled = true;
                checkMark( &params._mark, bDrawn, bSettled );
                checkMark( step.findAttribute( "toMark" ), bDrawn, bSettled );
                const bool bInputFlushed = EditorSelfTestInput::hasPendingEvents() == false;
                // 플랫폼이 다음 프레임에 실제 커서를 다시 넣으므로 옮기기 한 번으로는 머물지 않는다 - 입구가 프레임마다 붙잡는다. 단계가 놓는다.
                string                   coverName;
                const EditorMarkHit      hit    = EditorSelfTestInput::hoverMark( params._mark, params._anchor, coverName );
                const bool               bReady = bSettled && bInputFlushed && hit != EditorMarkHit::Pending && hit != EditorMarkHit::Missing;
                const AutomationStepHold hold   = runner.holdUntilReady( step, input, bReady, kStabilityWaitFrames,
                                                                         "the pointer to reach editor widget '" + params._mark + ( bDrawn ? "' and it to settle" : "' (not drawn yet)" ) );
                if ( hold == AutomationStepHold::Run && bReady == false && bDrawn )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "editor widget '%#' did not settle under the pointer within %# frame(s)", params._mark, kStabilityWaitFrames );
                    runner.recordFailure( step, textBuilder.view() );
                }
                return hold;
            }

            /** @brief `EditorWaitSettled` 가 붙잡은 동안의 상태입니다(`EditorStabilityWait` — 자체 테스트의 정지 대기와 같은 재료 · 판정). */
            struct StabilityWaitState final : IAutomationStepState
            {
                EditorStabilityWait _wait;
            };

            /**
             * @brief 에디터가 멈출 때까지(정지 상태) 시나리오 시계를 붙잡습니다 — 앞 단계가 낳은 일이 다 끝나 새 입력 없이는 더 바뀔 것이 없는 상태(`EditorStabilityWait`).
             * @details 고정 대기(`<Wait frames>`)를 대신한다 — "N 프레임 동안 바뀌지 않았다" 는 N 을 어떻게 잡든 증명이 아니고, 정지 상태에서 본 값이 곧 그 답이다(일어나지 않음을
             *          보는 단언도 이 뒤에 둔다). 배경 일(시계 잡기 조건)은 실행기가 단계 전에 이미 붙잡는다.
             */
            static AutomationStepHold holdUntilScreenStable( AutomationStepContext& runner, const AutomationStep& step, InputManager& input )
            {
                if ( EditorSelfTestMarks::setEnabled( true ) == false )
                    return AutomationStepHold::Run; // 에디터 컨텍스트가 없다 — 준비가 이미 막았다
                StabilityWaitState* pState = static_cast<StabilityWaitState*>( runner.findHeldStepState( step ) );
                if ( pState == nullptr )
                    pState = static_cast<StabilityWaitState*>( &runner.setHeldStepState( step, make_unique<StabilityWaitState>() ) );
                const EditorStabilityResult evaluation = pState->_wait.advance( runner.findActiveObjectManager() );
                if ( evaluation == EditorStabilityResult::Oscillating )
                    runner.recordFailure( step, pState->_wait.describeOscillation() );
                // 진행 중인 일에는 시한이 없다(`EditorStabilityState`) — 끝나지 않으면 시나리오 벽시계 상한이 까닭과 함께 잡는다.
                // Run 이면 실행기가 상태를 지운다 — 까닭을 먼저 복사해 넘긴다.
                const string waitingFor = pState->_wait.describeWait();
                return runner.holdUntilReady( step, input, evaluation == EditorStabilityResult::Ready,
                                              evaluation == EditorStabilityResult::Oscillating ? 0u : std::numeric_limits<uint32>::max(), waitingFor );
            }

            /**
             * @brief 키 · 글 단계는 받을 곳(키보드 초점)이 두 프레임 연속 같고 앞 단계의 입력이 다 들어갈 때까지 시나리오 시계를 붙잡습니다(최대 `kStabilityWaitFrames`).
             * @details 클릭이 연 입력 칸 · 팝업이 초점을 잡는 데 한두 프레임이 걸려, 예전에는 시나리오가 키 앞에 고정 대기를 두었다. 놓기(`state="Up"`)는 기다리지 않는다.
             */
            static AutomationStepHold holdUntilKeyboardStable( AutomationStepContext& runner, const AutomationStep& step, InputManager& input )
            {
                if ( EditorSelfTestMarks::setEnabled( true ) == false || isRelease( step ) )
                    return AutomationStepHold::Run;
                const bool               bReady = EditorSelfTestInput::hasPendingEvents() == false && EditorSelfTestInput::isKeyboardTargetStable();
                const AutomationStepHold hold   = runner.holdUntilReady( step, input, bReady, kStabilityWaitFrames, "the keyboard focus to settle (no pending input)" );
                if ( hold == AutomationStepHold::Run && bReady == false )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "the keyboard focus did not settle within %# frame(s)", kStabilityWaitFrames );
                    runner.recordFailure( step, textBuilder.view() );
                }
                return hold;
            }

            /**
             * @brief 글 단계는 글을 받을 칸이 활성일 때까지 시나리오 시계를 붙잡습니다. 칸이 끝내 없으면 실패입니다.
             * @details ImGui 는 활성 입력 칸이 없는 프레임의 글자를 버린다. 앞 단계(핀 놓기 · 우클릭)가 연 팝업은 입력이 끝난 뒤 몇 프레임 지나서야 열려,
             *          기다리지 않으면 글자가 조용히 사라지고 다음 Enter 가 거르지 않은 맨 위 줄을 고른다. Playwright 의 "입력 가능할 때까지" 와 같은 자리다.
             */
            static AutomationStepHold holdUntilTextTarget( AutomationStepContext& runner, const AutomationStep& step, InputManager& input )
            {
                if ( EditorSelfTestMarks::setEnabled( true ) == false )
                    return AutomationStepHold::Run;
                const bool               bReady = EditorSelfTestInput::hasPendingEvents() == false && EditorSelfTestInput::isKeyboardTargetStable() && ImGui::GetIO().WantTextInput;
                const AutomationStepHold hold   = runner.holdUntilReady( step, input, bReady, kStabilityWaitFrames, "a text field to take the keyboard focus (no pending input)" );
                if ( hold == AutomationStepHold::Run && bReady == false )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "no text field took the keyboard focus within %# frame(s) - the text would be lost", kStabilityWaitFrames );
                    runner.recordFailure( step, textBuilder.view() );
                }
                return hold;
            }

            /** @brief 레이블 가운데(또는 `anchor` 위치)로 마우스를 옮기고 누른 뒤 뗍니다. ImGui 입력 큐가 누름과 뗌을 프레임에 나눠 넣습니다. 레이블이 없으면 실패를 기록합니다. */
            static bool runClick( AutomationStepContext& runner, const AutomationStep& step )
            {
                const EditorClickParams params = step.readValidatedProperties<EditorClickParams>();
                const string&           mark   = params._mark;
                uint8                   modifierMask{ 0 };
                (void)Validation::parseModifiers( params._mods, modifierMask ); // 시작 때 검사했다
                const int32         button       = params._button;
                const float2        anchor       = params._anchor;
                const vector<int32> listModifier = makeImGuiModifiers( modifierMask );
                const PressState    state        = params._state;
                if ( reportClippedMark( runner, step, mark ) )
                    return true;
                // 누름과 뗌을 두 프레임으로 나누고, 뗌 앞에서 다시 레이블 위로 옮긴다. 그 사이에 플랫폼이 실제 커서를 넣기 때문이다.
                // 수정자(Ctrl+클릭 다중 선택 등)는 누르기 전에 누르고, 놓은 뒤에 해제한다.
                // state="Down" 은 누른 채로 두고(드래그, 뷰포트 비행), state="Up" 은 그 버튼을 놓는다. 그 사이 프레임에 다른 단계(키를 누른 채)를 넣을 수 있다.
                if ( state == PressState::Up )
                {
                    if ( EditorSelfTestInput::releaseMark( mark, anchor, button ) == false )
                        runner.recordFailure( step, "no editor widget is marked '" + mark + "' (was it drawn in the last frame?) - released where the pointer was" );
                    EditorSelfTestInput::releaseMouseHold();
                    return true;
                }
                // 누를 점이 레이블에 닿는지 입구가 판정한다(포인터는 조준 붙잡기가 이미 거기 두었다 — 누른 프레임의 이동량이 0 이라 뷰포트 비행이 시점을 돌리지 않는다).
                if ( reportUnreachableMark( runner, step, mark, anchor ) )
                    return true;
                if ( state != PressState::Down )
                    EditorSelfTestInput::releaseMouseHold(); // 아래 이동과 누름 이벤트가 이어받는다
                for ( const int32 modifier : listModifier )
                {
                    EditorSelfTestInput::setKey( modifier, true );
                }
                string coverName;
                (void)EditorSelfTestInput::pressMark( mark, anchor, button, coverName ); // 바로 위에서 닿는다고 판정했다
                if ( state == PressState::Down )
                    return true;
                EditorSelfTestInput::waitNextFrame();
                // 누른 뒤 위젯이 사라졌으면(닫힌 팝업) 그 자리에서 놓는다 — 그래도 놓아야 버튼이 눌린 채로 남지 않는다.
                if ( EditorSelfTestInput::releaseMark( mark, anchor, button ) == false )
                    SW_LOG_INFO_ID( "EditorScenarioSteps.EditorWidgetGoneWhen", "[Scenario] editor widget '%#' was gone when the click released", mark.c_str() );
                for ( const int32 modifier : listModifier )
                {
                    EditorSelfTestInput::setKey( modifier, false );
                }
                return true;
            }

            /**
             * @brief 레이블 가운데(또는 `anchor` 위치)를 누르고 (dx, dy) 픽셀만큼 드래그해 놓습니다. 키와 핸들 드래그에 씁니다. 프레임마다 커서를 다시 넣으며, 넣지 않은 프레임에는 플랫폼이 실제 커서를 넣습니다.
             * @details 누름, 절반 이동, 끝 이동, 놓기를 네 프레임에 나눈다. 수정자는 누르기 전에 누르고 놓은 뒤에 해제한다.
             */
            static bool runDrag( AutomationStepContext& runner, const AutomationStep& step )
            {
                const EditorDragParams params = step.readValidatedProperties<EditorDragParams>();
                const string&          mark   = params._mark;
                EditorSelfTestMark     found{};
                if ( EditorSelfTestMarks::find( mark, found ) == false )
                {
                    runner.recordFailure( step, "no editor widget is marked '" + mark + "' (was it drawn in the last frame?)" );
                    return true;
                }
                if ( reportClippedMark( runner, step, mark ) || reportUnreachableMark( runner, step, mark, params._anchor ) )
                    return true;
                EditorSelfTestInput::releaseMouseHold(); // 조준 붙잡기를 놓는다 — 아래 누름 · 이동이 끌기 경로를 넣는다
                uint8 modifierMask{ 0 };
                (void)Validation::parseModifiers( params._mods, modifierMask ); // 시작 때 검사했다
                float64             deltaX       = static_cast<float64>( params._dx );
                float64             deltaY       = static_cast<float64>( params._dy );
                const int32         button       = params._button;
                const vector<int32> listModifier = makeImGuiModifiers( modifierMask );
                // `anchor` 는 누르기 시작할 자리를 레이블 안 비율로 잡는다 — 넓은 캔버스(씬 뷰)의 빈 구석에서 끄는 상자 선택처럼 가운데가 아닌 곳에서 시작할 때.
                const float2 start{ found._min._x + ( found._max._x - found._min._x ) * params._anchor._x, found._min._y + ( found._max._y - found._min._y ) * params._anchor._y };
                // `toMark` 는 끝을 다른 위젯의 가운데로 잡는다 — 끌어 놓기(Hierarchy 행을 다른 행에, 에셋을 슬롯에)의 받는 쪽을 픽셀 거리로 세지 않는다.
                if ( params._toMark.empty() == false )
                {
                    EditorSelfTestMark target{};
                    if ( EditorSelfTestMarks::find( params._toMark, target ) == false )
                    {
                        runner.recordFailure( step, "no editor widget is marked '" + params._toMark + "' (was it drawn in the last frame?)" );
                        return true;
                    }
                    deltaX = static_cast<float64>( ( target._min._x + target._max._x ) * 0.5f - start._x );
                    deltaY = static_cast<float64>( ( target._min._y + target._max._y ) * 0.5f - start._y );
                }
                // 끌기 경로는 판정한 점(`start`)에서 잰다 — 레이블 좌표로 손 옮기는 공개 함수는 없다.
                const float2 middle{ static_cast<float32>( deltaX ) * 0.5f, static_cast<float32>( deltaY ) * 0.5f };
                const float2 end{ static_cast<float32>( deltaX ), static_cast<float32>( deltaY ) };
                (void)EditorSelfTestInput::moveByOffset( float2{} ); // 위 reportUnreachableMark 가 `start` 를 Reachable 로 판정했다
                for ( const int32 modifier : listModifier )
                {
                    EditorSelfTestInput::setKey( modifier, true );
                }
                EditorSelfTestInput::waitNextFrame();
                string coverName;
                (void)EditorSelfTestInput::pressMark( mark, params._anchor, button, coverName ); // 위에서 닿는다고 판정했다 — 누르는 점은 `start` 다
                EditorSelfTestInput::waitNextFrame();
                (void)EditorSelfTestInput::moveByOffset( middle );
                EditorSelfTestInput::waitNextFrame();
                (void)EditorSelfTestInput::moveByOffset( end );
                EditorSelfTestInput::waitNextFrame();
                (void)EditorSelfTestInput::moveByOffset( end );
                EditorSelfTestInput::setMouseButton( button, false );
                for ( const int32 modifier : listModifier )
                {
                    EditorSelfTestInput::setKey( modifier, false );
                }
                return true;
            }

            /**
             * @brief 버튼을 누르지 않고 커서를 레이블 위치(`anchor`)에 붙잡아 둡니다(호버). `release="true"` 가 나올 때까지 프레임마다 그 위치를 다시 넣으며,
             *        넣지 않은 프레임에는 플랫폼이 실제 커서를 넣습니다. 레이블이 없으면 실패를 기록합니다.
             */
            static bool runHover( AutomationStepContext& runner, const AutomationStep& step )
            {
                if ( step.findAttribute( "release" ) != nullptr )
                {
                    EditorSelfTestInput::releaseMouseHold();
                    return true;
                }
                const EditorHoverParams params = step.readValidatedProperties<EditorHoverParams>( { "release" } );
                (void)reportUnreachableMark( runner, step, params._mark, params._anchor ); // 닿으면 입구가 붙잡은 채로 둔다 — 가려졌으면 덮은 창의 툴팁을 기대와 견주지 않게 실패로 적는다
                return true;
            }

            static bool runText( AutomationStepContext& /*runner*/, const AutomationStep& step )
            {
                EditorSelfTestInput::typeText( step.readValidatedProperties<EditorTextParams>()._value );
                return true;
            }

            /** @brief "Ctrl+Z" 를 ImGuiKey 목록으로 해석합니다. 마지막이 키이고 앞은 수정자입니다. 모르는 이름이면 false 입니다(낱말 가르기는 `Validation::splitKeyChord`). */
            [[nodiscard]] static bool parseKeyChord( string_view chord, vector<int32>& outListKey )
            {
                outListKey.clear();
                vector<string_view> listToken;
                if ( Validation::splitKeyChord( chord, listToken ) == false )
                    return false;
                for ( const string_view token : listToken )
                {
                    int32 key = 0;
                    if ( EditorSelfTestInput::findKeyByName( token, key ) == false )
                        return false;
                    outListKey.push_back( key );
                }
                return true;
            }

            /** @brief `EditorKey` 의 인자입니다. */
            struct KeyArgs
            {
                vector<int32> _listKey{};
                PressState    _state{ PressState::Tap };
            };

            [[nodiscard]] static expected<KeyArgs> parseKey( const AutomationStep& step )
            {
                EditorKeyParams      params;
                const expected<void> read = step.readProperties( params );
                if ( read.has_value() == false )
                    return unexpected( read.error() );
                KeyArgs args;
                args._state = params._state;
                if ( parseKeyChord( params._key, args._listKey ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "unknown key (ImGui key names, e.g. Enter · Escape · F5 · Ctrl+Z)", params._key } );
                return args;
            }

            /** @brief 키 단계의 준비 함수입니다 - 위젯 기록을 켜고, 키 이름이 ImGui 가 아는 이름인지 봅니다(속성과 낱말 모양은 `Validation::validateKey` 가 이미 봤다). */
            [[nodiscard]] static expected<void> prepareKey( AutomationStepContext& runner, const AutomationStep& step )
            {
                const expected<void> marks = prepareMarks( runner, step );
                if ( marks.has_value() == false )
                    return marks;
                const expected<KeyArgs> args = parseKey( step );
                if ( args.has_value() == false )
                    return unexpected( args.error() );
                return {};
            }

            /** @brief 수정자를 누르고 키를 눌렀다 놓은 뒤 수정자를 거꾸로 해제합니다. ImGui 가 이벤트를 프레임에 나눠 처리하므로 한 단계로 충분합니다. */
            static bool runKey( AutomationStepContext& runner, const AutomationStep& step )
            {
                const expected<KeyArgs> args = parseKey( step );
                if ( args.has_value() == false )
                {
                    runner.recordFailure( step, AutomationStep::describeReason( args.error() ) ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
                    return true;
                }
                const vector<int32>& listKey = args->_listKey;
                const PressState     state   = args->_state;
                if ( state != PressState::Up )
                {
                    for ( const int32 key : listKey )
                    {
                        EditorSelfTestInput::setKey( key, true );
                    }
                }
                if ( state == PressState::Down )
                    return true;
                for ( size_t index = listKey.size(); index > 0; --index )
                {
                    EditorSelfTestInput::setKey( listKey[index - 1], false );
                }
                return true;
            }

            // ------------------------------------------------------------------------------
            // EditorExpectObject: 활성 씬에 그 이름의 오브젝트가 몇 개인지 센다. 그 컴포넌트를 가진 것만 셀 수도 있다
            // ------------------------------------------------------------------------------
            /** @brief 개수 단계의 준비 함수입니다 - 위젯 기록을 켜고, `component` 가 아는 컴포넌트 타입인지 봅니다(속성과 수 형식은 `Validation::validateExpectObject` 가 이미 봤다). */
            [[nodiscard]] static expected<void> prepareExpectObject( AutomationStepContext& runner, const AutomationStep& step )
            {
                const expected<void> marks = prepareMarks( runner, step );
                if ( marks.has_value() == false )
                    return marks;
                const string component = step.readValidatedProperties<EditorExpectObjectParams>()._component;
                if ( component.empty() )
                    return {};
                if ( editor::getSubsystem<TypeRegistry>().findType( hashed_string( component ) ) == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "unknown component type", component } );
                return {};
            }

            static bool runExpectObject( AutomationStepContext& runner, const AutomationStep& step )
            {
                const EditorExpectObjectParams params        = step.readValidatedProperties<EditorExpectObjectParams>();
                const uint32                   expectedCount = params._count;
                const uint32                   bSelected     = params._selected;
                const hashed_string            name( params._name );
                GameObjectManager*             pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                {
                    runner.recordFailure( step, "no active scene" );
                    return true;
                }

                vector<GameObject*> listObject;
                if ( params._component.empty() == false )
                    EditorSceneCommands::collectObjectsWithComponent( *pManager, editor::getSubsystem<TypeRegistry>().findType( hashed_string( params._component ) ), listObject );
                else
                    pManager->getAllGameObjects( editor::EditorSceneIterationAccess::getKey(), listObject );

                uint32 foundCount = 0;
                uint32 pickCount  = 0;
                for ( const GameObject* pObj : listObject )
                {
                    const bool bNameMismatch = pObj == nullptr || pObj->isPendingDestroy() || pObj->getName().isEqual( name, NameCase::CaseSensitive ) == false;
                    if ( bNameMismatch )
                        continue;
                    ++foundCount;
                    if ( editor::getMainSceneSelection().hasObject( pObj ) )
                        ++pickCount;
                }
                if ( foundCount != expectedCount )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "found %# object(s), expected %#", foundCount, expectedCount );
                    runner.recordFailure( step, textBuilder.view() );
                    return true;
                }
                if ( bSelected != 0 && pickCount == 0 )
                    runner.recordFailure( step, "the object is not selected" );
                return true;
            }

            // ------------------------------------------------------------------------------
            // EditorExpectDockLayout: 메인 뷰포트가 창과 같고, 보이는 도크 노드가 모두 화면 안에 있고 최소 변 이상이며, 창이 최소 크기 이상인지 검사한다
            // ------------------------------------------------------------------------------
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

            static bool runDockLayout( AutomationStepContext& runner, const AutomationStep& step )
            {
                const uint32  minNodeSide = step.readValidatedProperties<EditorDockLayoutParams>()._minNodeSize; // 0 이면 아래의 기본 최소 변
                const float32 minSide     = minNodeSide > 0 ? static_cast<float32>( minNodeSide ) : kDefaultMinDockNodeSide;

                const MainWindowSubsystem* pWindowHost = editor::findSubsystem<MainWindowSubsystem>();
                const IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                if ( pWindow == nullptr || ImGui::GetCurrentContext() == nullptr )
                {
                    runner.recordFailure( step, "no window or ImGui context" );
                    return true;
                }
                const bool bAboveMinimum = pWindow->getWidth() >= pWindow->getMinimumClientWidth() && pWindow->getHeight() >= pWindow->getMinimumClientHeight();
                if ( bAboveMinimum == false )
                    runner.recordFailure( step, "the window (" + to_string( pWindow->getWidth() ) + "x" + to_string( pWindow->getHeight() ) +
                                                    ") shrank below the editor minimum size" );

                const ImGuiViewport* pMain         = ImGui::GetMainViewport();
                const float32        widthDelta    = pMain->Size.x - static_cast<float32>( pWindow->getWidth() );
                const float32        heightDelta   = pMain->Size.y - static_cast<float32>( pWindow->getHeight() );
                const bool           bSameAsWindow = -kPixelTolerance <= widthDelta && widthDelta <= kPixelTolerance && -kPixelTolerance <= heightDelta &&
                                           heightDelta <= kPixelTolerance;
                if ( bSameAsWindow == false )
                    runner.recordFailure( step, "the main viewport does not match the window client size" );

                const ImGuiDockNode* pRoot            = findMainDockspace();
                const bool           bNoDockspaceArea = pRoot == nullptr || pRoot->Size.x <= 0.0f || pRoot->Size.y <= 0.0f;
                if ( bNoDockspaceArea )
                {
                    runner.recordFailure( step, "no main dockspace" );
                    return true;
                }
                vector<const ImGuiDockNode*> listLeaf;
                collectVisibleLeaves( pRoot, listLeaf );
                const ImVec2 workMax{ pMain->WorkPos.x + pMain->WorkSize.x, pMain->WorkPos.y + pMain->WorkSize.y };
                for ( const ImGuiDockNode* pLeaf : listLeaf )
                {
                    const bool bInside = pMain->WorkPos.x - kPixelTolerance <= pLeaf->Pos.x && pMain->WorkPos.y - kPixelTolerance <= pLeaf->Pos.y &&
                                         pLeaf->Pos.x + pLeaf->Size.x <= workMax.x + kPixelTolerance && pLeaf->Pos.y + pLeaf->Size.y <= workMax.y + kPixelTolerance;
                    const utf8* pWindowName = ( pLeaf->VisibleWindow != nullptr ) ? pLeaf->VisibleWindow->Name : "(empty)";
                    if ( bInside == false )
                        runner.recordFailure( step, string( "dock node '" ) + pWindowName + "' lies outside the main viewport" );
                    if ( pLeaf->Size.x < minSide || pLeaf->Size.y < minSide )
                        runner.recordFailure( step, string( "dock node '" ) + pWindowName + "' is " + to_string( pLeaf->Size.x ) + "x" + to_string( pLeaf->Size.y ) +
                                                        " px - below the minimum side" );
                }
                return true;
            }

            // ------------------------------------------------------------------------------
            // EditorSelfTest: 에디터 자체 테스트를 시나리오 안에서 돌린다 - 자체 테스트도 시나리오의 백엔드 넷(Vulkan 검증 레이어 포함)과 로그 게이트(선언하지 않은
            //    Error · Warning 이면 실패)를 받는다. 예전에는 따로 도는 CTest 항목(EditorTests)이 기본 백엔드 하나에서 케이스 성패만 봐, 결함이 로그로만 드러나는
            //    케이스(sceneView.resizeEveryFrame - 쓰는 중인 텍스처 해제)는 통과가 아무것도 증명하지 못했다.
            // ------------------------------------------------------------------------------

            /**
             * @brief 첫 차례에 실행을 청하고, 실행이 끝날 때까지 시나리오 시계를 붙잡습니다. 청할 수 없으면 돌려 실패를 적게 합니다.
             * @details 실행의 길이는 테스트 수 · Debug 에 달려 시한이 없다(`UINT32_MAX`) — 끝나지 않으면 벽시계 상한이 지금 테스트 · 단계와 함께 잡는다.
             */
            /** @brief 이 단계가 돌릴 패턴 — 명령줄(`gv_editorSelfTestPattern`)이 있으면 그것, 없으면 시나리오 값. */
            static string readSelfTestPattern( const AutomationStep& step )
            {
                return gv_editorSelfTestPattern.empty() ? step.readValidatedProperties<EditorSelfTestParams>()._pattern : gv_editorSelfTestPattern;
            }

            static AutomationStepHold holdUntilSelfTestsEnd( AutomationStepContext& runner, const AutomationStep& step, InputManager& input )
            {
                EditorSelfTestStepState* const pState = findSubsystem<EditorSelfTestStepState>();
                if ( pState == nullptr )
                    return AutomationStepHold::Run; // 에디터 컨텍스트가 없다 - 단계가 결과 없음으로 진다
                if ( pState->_pRequestedStep != &step )
                {
                    if ( EditorSelfTestRunner::requestRun( readSelfTestPattern( step ), false ) == false )
                        return AutomationStepHold::Run; // 에디터가 없거나 다른 실행(Test Runner 창)이 돈다 - 단계가 결과 없음으로 진다
                    pState->_pRequestedStep = &step;
                }
                return runner.holdUntilReady( step, input, EditorSelfTestRunner::isRunning() == false, std::numeric_limits<uint32>::max(),
                                              "editor self tests (" + EditorSelfTestRunner::describeProgress() + ")" );
            }

            static bool runSelfTest( AutomationStepContext& runner, const AutomationStep& step )
            {
                EditorSelfTestStepState* const pState     = findSubsystem<EditorSelfTestStepState>();
                const bool                     bRequested = pState != nullptr && pState->_pRequestedStep == &step;
                if ( pState != nullptr )
                    pState->_pRequestedStep = nullptr;
                const vector_reference<const EditorSelfTestResult> listResult = EditorSelfTestRunner::getResults();
                if ( bRequested == false || listResult.empty() )
                {
                    runner.recordFailure( step, "no editor self test ran for pattern '" + readSelfTestPattern( step ) + "' (no editor, a run already active, or no match)" );
                    return true;
                }
                for ( const EditorSelfTestResult& result : listResult )
                {
                    if ( result._bPassed == false )
                        runner.recordFailure( step, "self test " + result._id + ": " + result._reason );
                }
                return true;
            }
        };
    } // namespace

    SW_EDITOR_SUBSYSTEM( EditorSelfTestStepState, EditorSelfTestStepState::kSubsystemOrder );

    SW_AUTOMATION_STEP( editorClick, EditorScenarioStepValidation::kClickKind, &EditorScenarioStepsInternal::runClick, &EditorScenarioStepValidation::validateClick, AutomationStepOptions{}.withPrepare( &EditorScenarioStepsInternal::prepareMarks ).withHold( &EditorScenarioStepsInternal::holdUntilHoverReady<EditorClickParams> ) );
    SW_AUTOMATION_STEP( editorDrag, EditorScenarioStepValidation::kDragKind, &EditorScenarioStepsInternal::runDrag, &EditorScenarioStepValidation::validateDrag, AutomationStepOptions{}.withPrepare( &EditorScenarioStepsInternal::prepareMarks ).withHold( &EditorScenarioStepsInternal::holdUntilHoverReady<EditorDragParams> ) );
    SW_AUTOMATION_STEP( editorHover, EditorScenarioStepValidation::kHoverKind, &EditorScenarioStepsInternal::runHover, &EditorScenarioStepValidation::validateHover, AutomationStepOptions{}.withPrepare( &EditorScenarioStepsInternal::prepareMarks ).withHold( &EditorScenarioStepsInternal::holdUntilHoverReady<EditorHoverParams> ) );
    SW_AUTOMATION_STEP( editorText, EditorScenarioStepValidation::kTextKind, &EditorScenarioStepsInternal::runText, &AutomationStep::validateProperties<EditorTextParams>, AutomationStepOptions{}.withPrepare( &EditorScenarioStepsInternal::prepareMarks ).withHold( &EditorScenarioStepsInternal::holdUntilTextTarget ) );
    SW_AUTOMATION_STEP( editorKey, EditorScenarioStepValidation::kKeyKind, &EditorScenarioStepsInternal::runKey, &EditorScenarioStepValidation::validateKey, AutomationStepOptions{}.withPrepare( &EditorScenarioStepsInternal::prepareKey ).withHold( &EditorScenarioStepsInternal::holdUntilKeyboardStable ) );
    SW_AUTOMATION_STEP( editorExpectObject, EditorScenarioStepValidation::kExpectObjectKind, &EditorScenarioStepsInternal::runExpectObject, &AutomationStep::validateProperties<EditorExpectObjectParams>, AutomationStepOptions{}.withPrepare( &EditorScenarioStepsInternal::prepareExpectObject ) );
    SW_AUTOMATION_STEP( editorExpectDockLayout, EditorScenarioStepValidation::kDockLayoutKind, &EditorScenarioStepsInternal::runDockLayout, &AutomationStep::validateProperties<EditorDockLayoutParams>, AutomationStepOptions{}.withPrepare( &EditorScenarioStepsInternal::prepareMarks ) );
    SW_AUTOMATION_STEP( editorWaitSettled, EditorScenarioStepValidation::kWaitStableKind, nullptr, &AutomationStep::validateProperties<EditorNoParams>, AutomationStepOptions{}.withPrepare( &EditorScenarioStepsInternal::prepareMarks ).withHold( &EditorScenarioStepsInternal::holdUntilScreenStable ) );
    SW_AUTOMATION_STEP( editorSelfTest, EditorScenarioStepValidation::kSelfTestKind, &EditorScenarioStepsInternal::runSelfTest, &AutomationStep::validateProperties<EditorSelfTestParams>, AutomationStepOptions{}.withPrepare( &EditorScenarioStepsInternal::prepareMarks ).withHold( &EditorScenarioStepsInternal::holdUntilSelfTestsEnd ) );

} // namespace sw::editor
