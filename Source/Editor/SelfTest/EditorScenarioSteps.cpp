/**
 * @file EditorScenarioSteps.cpp
 * @brief 자동화 시나리오의 에디터 단계를 구현합니다. 단계는 `EditorClick`, `EditorText`, `EditorKey`, `EditorExpectObject`, `EditorExpectDockLayout` 입니다. 에디터 프로브(`Editor.*`)는 각 패널 · 기능 폴더의 `*Probes.cpp` 가 등록합니다.
 * @details 에디터 패널 입력은 엔진 입력 레이어가 아니라 ImGui 가 받으므로 ImGui 이벤트로 넣는다. 게임 뷰 입력은 가상 입력 장치(`<Tap>` 등)로,
 *          에디터 패널 입력은 이 단계로 넣으며, 두 방식을 한 시나리오 파일에서 함께 쓸 수 있다. 에디터 모듈이 로드된 실행(`-EnableEditor`)에서만 등록되므로,
 *          에디터 없이 이 단계나 프로브를 쓰면 시작할 때 읽기 오류가 난다. 플레이, 저장, 열기 같은 에디터 명령은 엔진 `DevCommand` 단계로 호출한다(`play`, `stop`, `editor <커맨드 id>`, `scene.saveAs`).
 */
#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorScenarioStepValidation.h"
#include "Editor/SelfTest/EditorSelfTest.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/AutomationStepContext.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorScenarioStepsInternal
        {
            /** @brief `EditorExpectDockLayout` 의 도크 노드 최소 변(px) 기본값입니다. 이보다 좁은 노드는 탭 글자도 담지 못합니다. */
            static constexpr float32 kDefaultMinDockNodeSide = 24.0f;
            /** @brief 창과 도크 사각형을 비교할 때의 픽셀 허용치입니다. */
            static constexpr float32 kPixelTolerance = 1.0f;

            using Validation       = EditorScenarioStepValidation;
            using PressState       = EditorPressState;
            using ClickArgs        = EditorClickArgs;
            using DragArgs         = EditorDragArgs;
            using ExpectObjectArgs = EditorExpectObjectArgs;

            /**
             * @brief 위젯 레이블 기록을 시작합니다(모든 에디터 단계의 준비 함수). 레이블은 기록을 켠 뒤에 그린 위젯만 기록됩니다.
             * @details 시나리오는 첫 에디터 프레임 앞에 시작하므로 켜는 곳(`EditorStateStore`)은 컨텍스트와 함께 이미 있어야 하고, 없으면(에디터가 꺼진 실행) 켠 척하지 않고 오류로 멈춘다.
             *          예전에는 검사 함수가 켜면서 결과를 버려, 켜는 곳이 아직 없던 때의 호출이 조용히 사라져 모든 클릭이 "no editor widget is marked" 로 졌다.
             */
            static bool prepareMarks( AutomationStepContext& /*runner*/, const AutomationStep& step, string& outError )
            {
                if ( EditorSelfTestMarks::setEnabled( true ) )
                    return true;
                outError = step.describe() + ": needs the editor (no editor context to record widget marks - run with -EnableEditor)";
                return false;
            }

            // ------------------------------------------------------------------------------
            // EditorClick, EditorText, EditorKey: ImGui 입력
            // ------------------------------------------------------------------------------
            /** @brief `mods` 마스크(`EditorModifierBit`)를 ImGui 수정자 키 목록으로 바꿉니다. 읽기와 모르는 이름 검사는 `Validation::parseModifiers` 가 한다. */
            [[nodiscard]] static vector<int32> makeImGuiModifiers( uint8 modifierMask )
            {
                vector<int32> listKey;
                if ( ( modifierMask & EditorModifierBit::kCtrl ) != 0 )
                    listKey.push_back( ImGuiMod_Ctrl );
                if ( ( modifierMask & EditorModifierBit::kShift ) != 0 )
                    listKey.push_back( ImGuiMod_Shift );
                if ( ( modifierMask & EditorModifierBit::kAlt ) != 0 )
                    listKey.push_back( ImGuiMod_Alt );
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
                runner.recordFailure( step, "editor widget '" + mark + "' is outside the visible area of its window (scroll it into view or collapse what is above it)" );
                return true;
            }

            /**
             * @brief 단계가 누를 레이블(`mark` · `toMark`)이 아직 그려지지 않았으면 시나리오 시계를 붙잡고 기다립니다(최대 `kMarkWaitFrames`). 넘기면 단계가 돌며 "no editor widget" 실패를 적습니다.
             * @details 레이블은 직전 프레임에 그려져야 한다. 패널이 프레임을 건너뛰었거나(로딩 · 리사이즈) 콤보 팝업이 한 프레임 늦게 열리면 고정 프레임 단계가 가끔 진다(옛 A-3 이던 것).
             *          언리얼 Automation Driver 의 "보일 때까지 기다리기" · Playwright 의 자동 대기와 같다. 기다린 프레임은 `[Scenario] … held the clock` 로그로 남는다.
             *          기록을 쓰는 단계가 직접 다시 켠다. 백엔드 교체 · 모듈 리로드가 에디터 컨텍스트를 새로 만들면 새 저장소는 꺼져 있고, 시나리오 준비는 한 번뿐이라 다시 오지 않는다.
             */
            static AutomationStepHold holdUntilMarked( AutomationStepContext& runner, const AutomationStep& step, InputManager& input )
            {
                if ( EditorSelfTestMarks::setEnabled( true ) == false )
                    return AutomationStepHold::Run; // 에디터 컨텍스트가 없다 — 단계가 돌며 실패를 적는다
                constexpr uint32   kMarkWaitFrames = 60;
                const string*      pMark           = step.findAttribute( "mark" );
                const string*      pToMark         = step.findAttribute( "toMark" );
                EditorSelfTestMark found{};
                const bool         bMarkDrawn   = pMark == nullptr || EditorSelfTestMarks::find( *pMark, found );
                const bool         bToMarkDrawn = pToMark == nullptr || EditorSelfTestMarks::find( *pToMark, found );
                return runner.holdUntilReady( step, input, bMarkDrawn && bToMarkDrawn, kMarkWaitFrames );
            }

            /** @brief 레이블 가운데(또는 `anchor` 위치)로 마우스를 옮기고 누른 뒤 뗍니다. ImGui 입력 큐가 누름과 뗌을 프레임에 나눠 넣습니다. 레이블이 없으면 실패를 기록합니다. */
            static bool runClick( AutomationStepContext& runner, const AutomationStep& step )
            {
                const string& mark = *step.findAttribute( "mark" );
                ClickArgs     args;
                string        error;
                if ( Validation::parseClick( step, args, error ) == false )
                {
                    runner.recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
                    return true;
                }
                const int32         button       = args._button;
                const float2        anchor       = args._anchor;
                const vector<int32> listModifier = makeImGuiModifiers( args._modifierMask );
                const PressState    state        = args._state;
                if ( reportClippedMark( runner, step, mark ) )
                    return true;
                if ( EditorSelfTestInput::moveMouseToMark( mark, anchor ) == false )
                {
                    runner.recordFailure( step, "no editor widget is marked '" + mark + "' (was it drawn in the last frame?)" );
                    return true;
                }
                // 누름과 뗌을 두 프레임으로 나누고, 뗌 앞에서 다시 레이블 위로 옮긴다. 그 사이에 플랫폼이 실제 커서를 넣기 때문이다.
                // 수정자(Ctrl+클릭 다중 선택 등)는 누르기 전에 누르고, 놓은 뒤에 해제한다.
                // state="down" 은 누른 채로 두고(드래그, 뷰포트 비행), state="up" 은 그 버튼을 놓는다. 그 사이 프레임에 다른 단계(키를 누른 채)를 넣을 수 있다.
                if ( state == PressState::Up )
                {
                    EditorSelfTestInput::setMouseButton( button, false );
                    EditorSelfTestInput::releaseMouseHold();
                    return true;
                }
                if ( state == PressState::Down )
                {
                    // 커서를 먼저 레이블에 붙잡아 한 프레임 둔 뒤 누른다. 누른 프레임의 마우스 이동량이 0 이어야 뷰포트 비행이 시점을 돌리지 않는다.
                    if ( EditorSelfTestInput::holdMouseAtMark( mark, anchor ) == false )
                    {
                        runner.recordFailure( step, "editor widget '" + mark + "' disappeared before it could be pressed" );
                        return true;
                    }
                    EditorSelfTestInput::waitNextFrame();
                }
                for ( const int32 modifier : listModifier )
                {
                    EditorSelfTestInput::setKey( modifier, true );
                }
                EditorSelfTestInput::setMouseButton( button, true );
                if ( state == PressState::Down )
                    return true;
                EditorSelfTestInput::waitNextFrame();
                // 누른 뒤 위젯이 사라졌으면(닫힌 팝업) 그 자리에서 놓는다 — 그래도 놓아야 버튼이 눌린 채로 남지 않는다.
                if ( EditorSelfTestInput::moveMouseToMark( mark, anchor ) == false )
                    SW_LOG_INFO( "[Scenario] editor widget '%#' was gone when the click released", mark.c_str() );
                EditorSelfTestInput::setMouseButton( button, false );
                for ( const int32 modifier : listModifier )
                {
                    EditorSelfTestInput::setKey( modifier, false );
                }
                return true;
            }

            /**
             * @brief 레이블 가운데를 누르고 (dx, dy) 픽셀만큼 드래그해 놓습니다. 키와 핸들 드래그에 씁니다. 프레임마다 커서를 다시 넣으며, 넣지 않은 프레임에는 플랫폼이 실제 커서를 넣습니다.
             * @details 누름, 절반 이동, 끝 이동, 놓기를 네 프레임에 나눈다. 수정자는 누르기 전에 누르고 놓은 뒤에 해제한다.
             */
            static bool runDrag( AutomationStepContext& runner, const AutomationStep& step )
            {
                const string&      mark = *step.findAttribute( "mark" );
                EditorSelfTestMark found{};
                if ( EditorSelfTestMarks::find( mark, found ) == false )
                {
                    runner.recordFailure( step, "no editor widget is marked '" + mark + "' (was it drawn in the last frame?)" );
                    return true;
                }
                if ( reportClippedMark( runner, step, mark ) )
                    return true;
                DragArgs args;
                string   error;
                if ( Validation::parseDrag( step, args, error ) == false )
                {
                    runner.recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
                    return true;
                }
                float64             deltaX       = args._deltaX;
                float64             deltaY       = args._deltaY;
                const int32         button       = static_cast<int32>( args._buttonNumber );
                const vector<int32> listModifier = makeImGuiModifiers( args._modifierMask );
                const float2        start{ ( found._min._x + found._max._x ) * 0.5f, ( found._min._y + found._max._y ) * 0.5f };
                // `toMark` 는 끝을 다른 위젯의 가운데로 잡는다 — 끌어 놓기(Hierarchy 행을 다른 행에, 에셋을 슬롯에)의 받는 쪽을 픽셀 거리로 세지 않는다.
                if ( const string* pToMark = step.findAttribute( "toMark" ); pToMark != nullptr )
                {
                    EditorSelfTestMark target{};
                    if ( EditorSelfTestMarks::find( *pToMark, target ) == false )
                    {
                        runner.recordFailure( step, "no editor widget is marked '" + *pToMark + "' (was it drawn in the last frame?)" );
                        return true;
                    }
                    deltaX = static_cast<float64>( ( target._min._x + target._max._x ) * 0.5f - start._x );
                    deltaY = static_cast<float64>( ( target._min._y + target._max._y ) * 0.5f - start._y );
                }
                const float2 middle{ start._x + static_cast<float32>( deltaX ) * 0.5f, start._y + static_cast<float32>( deltaY ) * 0.5f };
                const float2 end{ start._x + static_cast<float32>( deltaX ), start._y + static_cast<float32>( deltaY ) };
                EditorSelfTestInput::moveMouse( start, found._viewportID );
                for ( const int32 modifier : listModifier )
                {
                    EditorSelfTestInput::setKey( modifier, true );
                }
                EditorSelfTestInput::waitNextFrame();
                EditorSelfTestInput::moveMouse( start, found._viewportID );
                EditorSelfTestInput::setMouseButton( button, true );
                EditorSelfTestInput::waitNextFrame();
                EditorSelfTestInput::moveMouse( middle, found._viewportID );
                EditorSelfTestInput::waitNextFrame();
                EditorSelfTestInput::moveMouse( end, found._viewportID );
                EditorSelfTestInput::waitNextFrame();
                EditorSelfTestInput::moveMouse( end, found._viewportID );
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
                const string& mark = *step.findAttribute( "mark" );
                float2        anchor{ 0.5f, 0.5f }; // 적지 않으면 가운데
                if ( Validation::readAnchor( step, anchor ) == false )
                {
                    runner.recordFailure( step, "anchor must be 'x,y' in 0..1" ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
                    return true;
                }
                if ( EditorSelfTestInput::holdMouseAtMark( mark, anchor ) == false )
                    runner.recordFailure( step, "no editor widget is marked '" + mark + "' (was it drawn in the last frame?)" );
                return true;
            }

            static bool runText( AutomationStepContext& /*runner*/, const AutomationStep& step )
            {
                EditorSelfTestInput::typeText( *step.findAttribute( "value" ) );
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

            [[nodiscard]] static bool parseKey( const AutomationStep& step, KeyArgs& outArgs, string& outError )
            {
                const string* pKey = step.requireAttribute( "key", outError );
                if ( pKey == nullptr )
                    return false;
                if ( Validation::readPressState( step, outArgs._state ) == false )
                {
                    outError = step.describe() + ": state must be down or up, got '" + *step.findAttribute( "state" ) + "'";
                    return false;
                }
                if ( parseKeyChord( *pKey, outArgs._listKey ) == false )
                {
                    outError = step.describe() + ": unknown key '" + *pKey + "' (ImGui key names, e.g. Enter · Escape · F5 · Ctrl+Z)";
                    return false;
                }
                return true;
            }

            /** @brief 키 단계의 준비 함수입니다 - 위젯 기록을 켜고, 키 이름이 ImGui 가 아는 이름인지 봅니다(속성과 낱말 모양은 `Validation::validateKey` 가 이미 봤다). */
            static bool prepareKey( AutomationStepContext& runner, const AutomationStep& step, string& outError )
            {
                KeyArgs args;
                return prepareMarks( runner, step, outError ) && parseKey( step, args, outError );
            }

            /** @brief 수정자를 누르고 키를 눌렀다 놓은 뒤 수정자를 거꾸로 해제합니다. ImGui 가 이벤트를 프레임에 나눠 처리하므로 한 단계로 충분합니다. */
            static bool runKey( AutomationStepContext& runner, const AutomationStep& step )
            {
                KeyArgs args;
                string  error;
                if ( parseKey( step, args, error ) == false )
                {
                    runner.recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
                    return true;
                }
                const vector<int32>& listKey = args._listKey;
                const PressState     state   = args._state;
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
            static bool prepareExpectObject( AutomationStepContext& runner, const AutomationStep& step, string& outError )
            {
                if ( prepareMarks( runner, step, outError ) == false )
                    return false;
                const string* pComponent = step.findAttribute( "component" );
                if ( pComponent == nullptr )
                    return true;
                if ( editor::getSubsystem<TypeRegistry>().findType( hashed_string( *pComponent ) ) == nullptr )
                {
                    outError = step.describe() + ": unknown component type '" + *pComponent + "'";
                    return false;
                }
                return true;
            }

            static bool runExpectObject( AutomationStepContext& runner, const AutomationStep& step )
            {
                ExpectObjectArgs args;
                string           error;
                if ( Validation::parseExpectObject( step, args, error ) == false )
                {
                    runner.recordFailure( step, error ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
                    return true;
                }
                const uint32        expectedCount = args._expectedCount;
                const uint32        bSelected     = args._selected;
                const hashed_string name( *step.findAttribute( "name" ) );
                GameObjectManager*  pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                {
                    runner.recordFailure( step, "no active scene" );
                    return true;
                }

                vector<GameObject*> listObject;
                const string*       pComponent = step.findAttribute( "component" );
                if ( pComponent != nullptr )
                    EditorSceneCommands::collectObjectsWithComponent( *pManager, editor::getSubsystem<TypeRegistry>().findType( hashed_string( *pComponent ) ), listObject );
                else
                    pManager->getAllGameObjects( listObject );

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
                    runner.recordFailure( step, "found " + to_string( foundCount ) + " object(s), expected " + to_string( expectedCount ) );
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
                uint32 minNodeSide = 0; // 적지 않으면 아래의 기본 최소 변
                if ( Validation::readCount( step, "minNodeSize", minNodeSide ) == false )
                {
                    runner.recordFailure( step, "minNodeSize must be pixels" ); // 검사 뒤 단계가 바뀌었다(핫 리로드)
                    return true;
                }
                const float32 minSide = minNodeSide > 0 ? static_cast<float32>( minNodeSide ) : kDefaultMinDockNodeSide;

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
        };
    } // namespace

    SW_AUTOMATION_STEP_WITH_HOOKS( editorClick, EditorScenarioStepValidation::kClickKind, &EditorScenarioStepsInternal::runClick, &EditorScenarioStepValidation::validateClick,
                                   &EditorScenarioStepsInternal::prepareMarks, &EditorScenarioStepsInternal::holdUntilMarked, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( editorDrag, EditorScenarioStepValidation::kDragKind, &EditorScenarioStepsInternal::runDrag, &EditorScenarioStepValidation::validateDrag,
                                   &EditorScenarioStepsInternal::prepareMarks, &EditorScenarioStepsInternal::holdUntilMarked, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( editorHover, EditorScenarioStepValidation::kHoverKind, &EditorScenarioStepsInternal::runHover, &EditorScenarioStepValidation::validateHover,
                                   &EditorScenarioStepsInternal::prepareMarks, &EditorScenarioStepsInternal::holdUntilMarked, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( editorText, EditorScenarioStepValidation::kTextKind, &EditorScenarioStepsInternal::runText, &EditorScenarioStepValidation::validateText,
                                   &EditorScenarioStepsInternal::prepareMarks, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( editorKey, EditorScenarioStepValidation::kKeyKind, &EditorScenarioStepsInternal::runKey, &EditorScenarioStepValidation::validateKey,
                                   &EditorScenarioStepsInternal::prepareKey, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( editorExpectObject, EditorScenarioStepValidation::kExpectObjectKind, &EditorScenarioStepsInternal::runExpectObject,
                                   &EditorScenarioStepValidation::validateExpectObject, &EditorScenarioStepsInternal::prepareExpectObject, nullptr, false );
    SW_AUTOMATION_STEP_WITH_HOOKS( editorExpectDockLayout, EditorScenarioStepValidation::kDockLayoutKind, &EditorScenarioStepsInternal::runDockLayout,
                                   &EditorScenarioStepValidation::validateDockLayout, &EditorScenarioStepsInternal::prepareMarks, nullptr, false );

} // namespace sw::editor
