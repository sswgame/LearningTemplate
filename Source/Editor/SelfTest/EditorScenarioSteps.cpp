/**
 * @file EditorScenarioSteps.cpp
 * @brief 자동화 시나리오의 에디터 단계(`EditorClick` · `EditorText` · `EditorKey` · `EditorExpectObject` · `EditorExpectDockLayout`)와 에디터 탐침(`Editor.*`).
 * @details 에디터 패널 입력은 엔진 입력 층이 아니라 ImGui 가 받으므로 ImGui 사건으로 넣는다. 게임 뷰 입력은 가상 입력 장치(`<Tap>` …)로, 에디터 패널 입력은
 *          이 단계로 — 두 창구가 한 시나리오 파일에서 같이 쓰인다. 에디터 모듈이 올라온 실행(`-EnableEditor`)에서만 등록되므로, 에디터 없이 이 단계 · 탐침을
 *          쓰면 시작할 때 읽기 오류다. 플레이 · 저장 · 열기 같은 에디터 명령은 엔진 `DevCommand` 단계로 부른다(`play` · `stop` · `editor <커맨드 id>` · `scene.saveAs`).
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWindowTitle.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/HierarchyPanel.h"
#include "Editor/Panels/PreferencesPanel.h"
#include "Editor/Panels/SceneViewPanel.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorCamera.h"
#include "Editor/Viewport/EditorGridUtil.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Renderer/Capture/RenderDocCapture.h"
#include "Engine/Scene/Scene.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Window/IWindow.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    /** @brief 탐침 `Editor.VisualizerOn` 이 볼 시각화 id 입니다. 시나리오가 `<Variable>` 로 정한다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorProbeVisualizer, "", "탐침 Editor.VisualizerOn 이 볼 뷰포트 시각화 id (시나리오용)" );
    /** @brief 탐침 `Editor.PanelOpen` 이 볼 패널 id 입니다. 시나리오가 `<Variable>` 로 정한다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorProbePanel, "", "탐침 Editor.PanelOpen 이 볼 패널 id (시나리오용)" );
    /** @brief 탐침 `Editor.SelectedProperty` 가 볼 `<컴포넌트 타입>.<프로퍼티>` 입니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorProbeProperty, "", "탐침 Editor.SelectedProperty 가 볼 <컴포넌트 타입>.<프로퍼티> (시나리오용)" );
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct EditorScenarioStepsInternal
        {
            /** @brief `EditorExpectDockLayout` 의 도크 칸 최소 변(px) 기본값입니다 — 이보다 좁은 칸은 탭 글자도 못 담는다. */
            static constexpr float32 kDefaultMinDockNodeSide = 24.0f;
            /** @brief 창 · 도크 사각형 비교의 픽셀 허용치입니다. */
            static constexpr float32 kPixelTolerance = 1.0f;

            /** @brief 속성이 @p listAllowed 안에만 있고 @p pRequired 가 있는지 봅니다. 위젯 이름표를 적기 시작한다(이름표는 켜진 뒤 그린 위젯만 적힌다). */
            static bool validate( const AutomationStep& step, std::initializer_list<string_view> listAllowed, const utf8* pRequired, string& outError )
            {
                for ( const AutomationAttribute& attribute : step._listAttribute )
                {
                    bool bKnown = false;
                    for ( const string_view allowed : listAllowed )
                    {
                        bKnown = bKnown || string_view{ attribute._name } == allowed;
                    }
                    if ( bKnown == false )
                    {
                        outError = step.describe() + ": unknown attribute '" + attribute._name + "'";
                        return false;
                    }
                }
                if ( pRequired != nullptr && step.findAttribute( pRequired ) == nullptr )
                {
                    outError = step.describe() + ": needs " + pRequired + "=\"…\"";
                    return false;
                }
                EditorSelfTestMarks::setEnabled( true );
                return true;
            }

            /** @brief 음이 아닌 정수 속성을 읽습니다. 없으면 @p defaultValue 입니다. 형식이 틀리면 false 입니다. */
            [[nodiscard]] static bool readCount( const AutomationStep& step, string_view name, uint32 defaultValue, uint32& outValue )
            {
                const string* pText = step.findAttribute( name );
                int32         value = 0;
                if ( pText == nullptr )
                {
                    outValue = defaultValue;
                    return true;
                }
                if ( StringUtil::parseInt( string_view{ *pText }, value ) == false || value < 0 )
                    return false;
                outValue = static_cast<uint32>( value );
                return true;
            }

            // ------------------------------------------------------------------------------
            // EditorClick · EditorText · EditorKey — ImGui 입력
            // ------------------------------------------------------------------------------
            /** @brief `mods` 속성("ctrl" · "shift" · "ctrl+shift" …)을 수정자 키 목록으로 풉니다. 없으면 빈 목록, 모르는 이름이면 false 입니다. */
            [[nodiscard]] static bool parseModifiers( const AutomationStep& step, vector<int32>& outListKey )
            {
                outListKey.clear();
                const string* pMods = step.findAttribute( "mods" );
                if ( pMods == nullptr )
                    return true;
                const string_view mods{ *pMods };
                size_t            begin = 0;
                while ( begin <= mods.size() )
                {
                    const size_t      plus  = mods.find( '+', begin );
                    const size_t      end   = plus == string_view::npos ? mods.size() : plus;
                    const string_view token = mods.substr( begin, end - begin );
                    if ( StringUtil::equals( token, "ctrl", true ) )
                        outListKey.push_back( ImGuiMod_Ctrl );
                    else if ( StringUtil::equals( token, "shift", true ) )
                        outListKey.push_back( ImGuiMod_Shift );
                    else if ( StringUtil::equals( token, "alt", true ) )
                        outListKey.push_back( ImGuiMod_Alt );
                    else
                        return false;
                    if ( plus == string_view::npos )
                        break;
                    begin = plus + 1;
                }
                return true;
            }

            /** @brief `state` 속성("down" · "up")을 읽습니다. 없으면 누르고 떼는 한 번(`PressState::Tap`), 모르는 값이면 false 입니다. */
            enum class PressState : uint8
            {
                Tap,
                Down,
                Up,
            };

            [[nodiscard]] static bool readPressState( const AutomationStep& step, PressState& outState )
            {
                const string* pState = step.findAttribute( "state" );
                outState             = PressState::Tap;
                if ( pState == nullptr )
                    return true;
                if ( StringUtil::equals( string_view{ *pState }, "down", true ) )
                    outState = PressState::Down;
                else if ( StringUtil::equals( string_view{ *pState }, "up", true ) )
                    outState = PressState::Up;
                else
                    return false;
                return true;
            }

            static bool validateClick( const AutomationStep& step, string& outError )
            {
                if ( validate( step, { "mark", "button", "mods", "state" }, "mark", outError ) == false )
                    return false;
                PressState state = PressState::Tap;
                if ( readPressState( step, state ) == false )
                {
                    outError = step.describe() + ": state must be down or up, got '" + *step.findAttribute( "state" ) + "'";
                    return false;
                }
                vector<int32> listModifier;
                if ( parseModifiers( step, listModifier ) == false )
                {
                    outError = step.describe() + ": mods must be ctrl, shift or alt joined with '+', got '" + *step.findAttribute( "mods" ) + "'";
                    return false;
                }
                const string* pButton = step.findAttribute( "button" );
                int32         button  = 0;
                if ( pButton != nullptr && ( StringUtil::parseInt( string_view{ *pButton }, button ) == false || button < 0 || button > 4 ) )
                {
                    outError = step.describe() + ": button must be 0..4, got '" + *pButton + "'";
                    return false;
                }
                return true;
            }

            static bool validateText( const AutomationStep& step, string& outError ) { return validate( step, { "value" }, "value", outError ); }

            /** @brief 이름표 가운데로 마우스를 옮기고 누르고 뗀다(ImGui 입력 큐가 누름 · 뗌을 프레임에 나눠 넣는다). 이름표가 없으면 실패를 적는다. */
            static bool runClick( AutomationRunner& runner, const AutomationStep& step )
            {
                const string& mark    = *step.findAttribute( "mark" );
                const string* pButton = step.findAttribute( "button" );
                int32         button  = 0;
                if ( pButton != nullptr )
                    (void)StringUtil::parseInt( string_view{ *pButton }, button ); // 검사에서 봤다
                if ( EditorSelfTestInput::moveMouseToMark( mark ) == false )
                {
                    runner.recordFailure( step, "no editor widget is marked '" + mark + "' (was it drawn in the last frame?)" );
                    return true;
                }
                // 누름과 뗌을 두 프레임으로 — 뗌 앞에서 다시 이름표 위로 옮긴다(플랫폼이 그 사이 실제 커서를 넣는다).
                // 수정자(Ctrl+클릭 다중 선택 …)는 누르기 전에 눌러 뗀 뒤에 놓는다.
                vector<int32> listModifier;
                (void)parseModifiers( step, listModifier ); // 검사에서 봤다
                PressState state = PressState::Tap;
                (void)readPressState( step, state ); // 검사에서 봤다
                // state="down" 은 누른 채로 두고(끌기 · 뷰포트 비행), state="up" 은 그 단추를 뗀다 — 사이 프레임에 다른 단계(키를 누른 채)를 넣는다.
                if ( state == PressState::Up )
                {
                    EditorSelfTestInput::setMouseButton( button, false );
                    EditorSelfTestInput::releaseMouseHold();
                    return true;
                }
                if ( state == PressState::Down )
                {
                    // 커서를 먼저 이름표에 붙잡아 한 프레임 둔 뒤 누른다 — 누른 프레임의 마우스 이동량이 0 이라 뷰포트 비행이 시점을 돌리지 않는다.
                    (void)EditorSelfTestInput::holdMouseAtMark( mark ); // 위에서 같은 이름으로 찾았다
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
                (void)EditorSelfTestInput::moveMouseToMark( mark ); // 표식은 누르기 전에 같은 이름으로 찾았다
                EditorSelfTestInput::setMouseButton( button, false );
                for ( const int32 modifier : listModifier )
                {
                    EditorSelfTestInput::setKey( modifier, false );
                }
                return true;
            }

            static bool runText( AutomationRunner& /*runner*/, const AutomationStep& step )
            {
                EditorSelfTestInput::typeText( *step.findAttribute( "value" ) );
                return true;
            }

            /** @brief "Ctrl+Z" 를 ImGuiKey 들로 풉니다(마지막이 키, 앞은 수정자). 모르는 이름이면 false 입니다. */
            [[nodiscard]] static bool parseKeyChord( string_view chord, vector<int32>& outListKey )
            {
                outListKey.clear();
                size_t begin = 0;
                while ( begin <= chord.size() )
                {
                    const size_t      plus  = chord.find( '+', begin );
                    const size_t      end   = plus == string_view::npos ? chord.size() : plus;
                    const string_view token = chord.substr( begin, end - begin );
                    int32             key   = 0;
                    if ( token.empty() || EditorSelfTestInput::findKeyByName( token, key ) == false )
                        return false;
                    outListKey.push_back( key );
                    if ( plus == string_view::npos )
                        break;
                    begin = plus + 1;
                }
                return outListKey.empty() == false;
            }

            static bool validateKey( const AutomationStep& step, string& outError )
            {
                if ( validate( step, { "key", "state" }, "key", outError ) == false )
                    return false;
                PressState state = PressState::Tap;
                if ( readPressState( step, state ) == false )
                {
                    outError = step.describe() + ": state must be down or up, got '" + *step.findAttribute( "state" ) + "'";
                    return false;
                }
                vector<int32> listKey;
                if ( parseKeyChord( *step.findAttribute( "key" ), listKey ) == false )
                {
                    outError = step.describe() + ": unknown key '" + *step.findAttribute( "key" ) + "' (ImGui key names, e.g. Enter · Escape · F5 · Ctrl+Z)";
                    return false;
                }
                return true;
            }

            /** @brief 수정자를 누르고 키를 눌렀다 뗀 뒤 수정자를 거꾸로 뗀다 — ImGui 가 사건을 프레임에 나눠 흘리므로 한 단계로 된다. */
            static bool runKey( AutomationRunner& /*runner*/, const AutomationStep& step )
            {
                vector<int32> listKey;
                (void)parseKeyChord( *step.findAttribute( "key" ), listKey ); // 검사에서 봤다
                PressState state = PressState::Tap;
                (void)readPressState( step, state ); // 검사에서 봤다
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
            // EditorExpectObject — 활성 씬에 그 이름의 오브젝트가 몇 개인지(그 컴포넌트를 가진 것만 셀 수도 있다)
            // ------------------------------------------------------------------------------
            static bool validateExpectObject( const AutomationStep& step, string& outError )
            {
                if ( validate( step, { "name", "count", "component", "selected" }, "name", outError ) == false )
                    return false;
                uint32 value = 0;
                if ( readCount( step, "count", 1, value ) == false || readCount( step, "selected", 0, value ) == false || value > 1 )
                {
                    outError = step.describe() + ": count must be a whole number and selected 0 or 1";
                    return false;
                }
                const string* pComponent = step.findAttribute( "component" );
                TypeRegistry* pRegistry  = editor::getService<TypeRegistry>();
                if ( pComponent != nullptr && ( pRegistry == nullptr || pRegistry->findType( hashed_string( *pComponent ) ) == nullptr ) )
                {
                    outError = step.describe() + ": unknown component type '" + *pComponent + "'";
                    return false;
                }
                return true;
            }

            static bool runExpectObject( AutomationRunner& runner, const AutomationStep& step )
            {
                uint32 expectedCount = 1;
                uint32 bSelected     = 0;
                (void)readCount( step, "count", 1, expectedCount ); // 검사에서 봤다
                (void)readCount( step, "selected", 0, bSelected );  // 검사에서 봤다
                const hashed_string name( *step.findAttribute( "name" ) );
                GameObjectManager*  pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                {
                    runner.recordFailure( step, "no active scene" );
                    return true;
                }

                vector<GameObject*> listObject;
                const string*       pComponent = step.findAttribute( "component" );
                TypeRegistry*       pRegistry  = editor::getService<TypeRegistry>();
                if ( pComponent != nullptr && pRegistry != nullptr )
                    EditorSceneCommands::collectObjectsWithComponent( *pManager, pRegistry->findType( hashed_string( *pComponent ) ), listObject );
                else
                    pManager->getAllGameObjects( listObject );

                EditorContext* pContext   = EditorContext::get();
                uint32         foundCount = 0;
                uint32         pickCount  = 0;
                for ( const GameObject* pObj : listObject )
                {
                    if ( pObj == nullptr || pObj->isPendingDestroy() || pObj->getName().isEqual( name, NameCase::CaseSensitive ) == false )
                        continue;
                    ++foundCount;
                    if ( pContext != nullptr && pContext->getEditorSelection().hasObject( pObj ) )
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
            // EditorExpectDockLayout — 메인 뷰포트가 창과 같고, 보이는 도크 칸이 모두 화면 안 · 최소 변 이상, 창이 최소 크기 이상
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

            static bool validateDockLayout( const AutomationStep& step, string& outError )
            {
                if ( validate( step, { "minNodeSize" }, nullptr, outError ) == false )
                    return false;
                uint32 value = 0;
                if ( readCount( step, "minNodeSize", 0, value ) == false )
                {
                    outError = step.describe() + ": minNodeSize must be pixels";
                    return false;
                }
                return true;
            }

            static bool runDockLayout( AutomationRunner& runner, const AutomationStep& step )
            {
                uint32 minNodeSide = 0;
                (void)readCount( step, "minNodeSize", 0, minNodeSide ); // 검사에서 봤다
                const float32 minSide = minNodeSide > 0 ? static_cast<float32>( minNodeSide ) : kDefaultMinDockNodeSide;

                const IWindow* pWindow = IWindow::getActiveWindow();
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

                const ImGuiDockNode* pRoot = findMainDockspace();
                if ( pRoot == nullptr || pRoot->Size.x <= 0.0f || pRoot->Size.y <= 0.0f )
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
            // 탐침 — `<Expect probe="Editor.*">`
            // ------------------------------------------------------------------------------
            [[nodiscard]] static bool readPlayState( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( static_cast<uint32>( EditorPlaySession::getState() ) );
                return true;
            }

            [[nodiscard]] static bool readSceneDirty( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return false;
                outValue = pContext->getWorkspace().isSceneDirty() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 창 제목이 미저장 표시(`*`)를 달고 있으면 1 입니다 — 셸이 실제로 창에 건 제목을 읽는다. */
            [[nodiscard]] static bool readWindowTitleDirty( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const IWindow* pWindow = IWindow::getActiveWindow();
                if ( pWindow == nullptr )
                    return false;
                const string title = StringUtil::utf16ToUtf8( pWindow->getTitle().c_str() );
                outValue           = EditorWindowTitleUtil::isDirtyTitle( title ) ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readRenderDocAvailable( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = RenderDocCapture::isAvailable() ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readObjectCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const GameObjectManager* pActive = editor::getActiveObjectManager();
                if ( pActive == nullptr )
                    return false;
                uint32 count = 0;
                pActive->forEachGameObject( [&count]( const GameObject* pObj )
                {
                    if ( pObj != nullptr && pObj->isPendingDestroy() == false )
                        ++count;
                } );
                outValue = static_cast<float64>( count );
                return true;
            }

            [[nodiscard]] static bool readSelectionCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return false;
                outValue = static_cast<float64>( pContext->getEditorSelection().getSelectedObjectCount() );
                return true;
            }

            [[nodiscard]] static bool readHierarchyVisibleRoots( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext*        pContext = EditorContext::get();
                const HierarchyPanel* pHierarchy =
                    pContext != nullptr ? static_cast<const HierarchyPanel*>( pContext->getPanelManager().findPanel( "hierarchy" ) ) : nullptr;
                if ( pHierarchy == nullptr )
                    return false;
                outValue = static_cast<float64>( pHierarchy->getVisibleRootCount() );
                return true;
            }

            [[nodiscard]] static bool readNoSearchResultHintShown( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = EditorWidgets::wasFilteredNoResultHintDrawnRecently() ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readThemePreset( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( static_cast<uint32>( EditorThemeUtil::getActiveTheme()._preset ) );
                return true;
            }

            /** @brief 액센트 색을 0xRRGGBB 정수로(채널마다 0..255 반올림) — `equals` 로 정확히 견줄 수 있다. */
            [[nodiscard]] static bool readAccentColor( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const Color4& accent = EditorThemeUtil::getAccentColor();
                const uint32  red    = static_cast<uint32>( accent._r * 255.0f + 0.5f );
                const uint32  green  = static_cast<uint32>( accent._g * 255.0f + 0.5f );
                const uint32  blue   = static_cast<uint32>( accent._b * 255.0f + 0.5f );
                outValue             = static_cast<float64>( ( red << 16 ) | ( green << 8 ) | blue );
                return true;
            }

            [[nodiscard]] static bool readSelectedIsEditorCamera( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return false;
                const GameObject*      pPrimary = pContext->getEditorSelection().getPrimaryObject();
                const CameraComponent* pCamera  = pPrimary != nullptr ? pPrimary->getComponent<CameraComponent>() : nullptr;
                outValue                        = ( pCamera != nullptr && pCamera->getRole() == CameraRole::Editor ) ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readUndoCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const CommandStack* pStack = editor::getService<CommandStack>();
                if ( pStack == nullptr )
                    return false;
                outValue = static_cast<float64>( pStack->getCommandCount() );
                return true;
            }

            [[nodiscard]] static bool readUndoIndex( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const CommandStack* pStack = editor::getService<CommandStack>();
                if ( pStack == nullptr )
                    return false;
                outValue = static_cast<float64>( pStack->getCurrentIndex() );
                return true;
            }

            [[nodiscard]] static bool readLoadingScreenShown( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const UISystem* pUI = editor::getService<UISystem>();
                if ( pUI == nullptr )
                    return false;
                outValue = pUI->isLoadingScreenShown() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 뷰포트 격자가 이번이나 지난 프레임에 그려졌는가입니다. 아니면 격자 탐침은 값을 내지 않습니다(격자를 끈 뷰 · 다른 탭). */
            [[nodiscard]] static const EditorGridStats* findRecentGridStats()
            {
                const EditorGridStats& stats = EditorGridStats::get();
                return stats._frame >= 0 && ImGui::GetFrameCount() - stats._frame <= 1 ? &stats : nullptr;
            }

            [[nodiscard]] static bool readGridStep( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorGridStats* pStats = findRecentGridStats();
                if ( pStats == nullptr )
                    return false;
                outValue = static_cast<float64>( pStats->_step );
                return true;
            }

            [[nodiscard]] static bool readGridMajorLines( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorGridStats* pStats = findRecentGridStats();
                if ( pStats == nullptr )
                    return false;
                outValue = static_cast<float64>( pStats->_majorLineCount );
                return true;
            }

            [[nodiscard]] static bool readGridMisplacedMajorLines( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorGridStats* pStats = findRecentGridStats();
                if ( pStats == nullptr )
                    return false;
                outValue = static_cast<float64>( pStats->_misplacedMajorCount );
                return true;
            }

            /** @brief 씬 뷰 카메라(에디터 카메라)의 월드 위치입니다. Play 중에도 에디터 카메라다. */
            [[nodiscard]] static bool findSceneViewCameraPosition( float3& outPosition )
            {
                const CameraComponent* pCamera = EditorCamera::find( editor::getActiveScene() );
                if ( pCamera == nullptr )
                    return false;
                outPosition = pCamera->getWorldPosition();
                return true;
            }

            /** @brief 게임 뷰가 그리는 카메라(활성 씬의 게임 카메라)의 월드 위치입니다. */
            [[nodiscard]] static bool findGameViewCameraPosition( float3& outPosition )
            {
                const Scene*           pScene  = editor::getActiveScene();
                const CameraComponent* pCamera = pScene != nullptr ? pScene->getActiveGameCamera() : nullptr;
                if ( pCamera == nullptr )
                    return false;
                outPosition = pCamera->getWorldPosition();
                return true;
            }

            [[nodiscard]] static bool readSceneViewCameraX( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                float3 position{};
                if ( findSceneViewCameraPosition( position ) == false )
                    return false;
                outValue = static_cast<float64>( position._x );
                return true;
            }

            [[nodiscard]] static bool readSceneViewCameraY( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                float3 position{};
                if ( findSceneViewCameraPosition( position ) == false )
                    return false;
                outValue = static_cast<float64>( position._y );
                return true;
            }

            [[nodiscard]] static bool readGameViewCameraX( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                float3 position{};
                if ( findGameViewCameraPosition( position ) == false )
                    return false;
                outValue = static_cast<float64>( position._x );
                return true;
            }

            [[nodiscard]] static bool readGameViewCameraY( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                float3 position{};
                if ( findGameViewCameraPosition( position ) == false )
                    return false;
                outValue = static_cast<float64>( position._y );
                return true;
            }

            /** @brief 이번 UI 프레임에 셸이 호스트에 그 뷰 RT 를 알렸는지(`ImGuiEditor::getSceneViewport` · `getGameViewport` 와 같은 판정)입니다. */
            [[nodiscard]] static bool readViewRequested( EditorViewKind kind, float64& outValue )
            {
                const EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return false;
                const bool bSceneDrawn = pContext->wasViewDrawn( EditorViewKind::Scene );
                const bool bGameDrawn  = pContext->wasViewDrawn( EditorViewKind::Game );
                const bool bRequested  = kind == EditorViewKind::Scene ? EditorViewTargetUtil::shouldRequestSceneView( bSceneDrawn, bGameDrawn )
                                                                       : EditorViewTargetUtil::shouldRequestGameView( bGameDrawn );
                outValue               = ( bRequested && pContext->getViewTarget( kind )._renderTarget != 0 ) ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readSceneViewRequested( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readViewRequested( EditorViewKind::Scene, outValue );
            }

            [[nodiscard]] static bool readGameViewRequested( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readViewRequested( EditorViewKind::Game, outValue );
            }

            /** @brief 이번 UI 프레임에 Scene 패널이 씬 뷰를 그렸는지(앞 탭으로 보이는지)입니다. 두 뷰가 다 가려져도 `Editor.SceneViewRequested` 는 1 이다. */
            [[nodiscard]] static bool readSceneViewDrawn( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return false;
                outValue = pContext->wasViewDrawn( EditorViewKind::Scene ) ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readUIScale( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( EditorThemeUtil::getDpiScale() );
                return true;
            }

            /** @brief 씬 뷰가 `-gv_editorProbeVisualizer` 의 시각화를 켜 두었으면 1 입니다. 패널이나 그 id 의 시각화가 없으면 값을 내지 않는다. */
            [[nodiscard]] static bool readVisualizerOn( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return false;
                const SceneViewPanel*               pPanel        = static_cast<const SceneViewPanel*>( pContext->getPanelManager().findPanel( "scene_view" ) );
                const EditorVisualizerRegistration* pRegistration = EditorRegistry<EditorVisualizerRegistration>::find( gv_editorProbeVisualizer );
                if ( pPanel == nullptr || pRegistration == nullptr )
                    return false;
                outValue = pPanel->getViewportClient().getToolbarSettings()._visualizerToggles.isOn( *pRegistration ) ? 1.0 : 0.0;
                return true;
            }

            /**
             * @brief 주 선택 오브젝트의 `-gv_editorProbeProperty`(`<컴포넌트 타입>.<프로퍼티>`) 값입니다. 숫자 프로퍼티(float32 · int32 · uint32)만 읽는다.
             * @details 선택이 없거나 그 컴포넌트 · 프로퍼티가 없으면 값을 내지 않는다.
             */
            [[nodiscard]] static bool readSelectedProperty( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return false;
                GameObject*  pObject = pContext->getEditorSelection().getPrimaryObject();
                const size_t dot     = gv_editorProbeProperty.find( '.' );
                if ( pObject == nullptr || dot == string::npos )
                    return false;
                Component* pComponent = pObject->findComponentByTypeName( hashed_string( gv_editorProbeProperty.substr( 0, dot ).c_str() ) );
                if ( pComponent == nullptr || pComponent->getTypeInfo() == nullptr )
                    return false;
                const PropertyInfo* pProperty = pComponent->getTypeInfo()->findPropertyInHierarchy( hashed_string( gv_editorProbeProperty.substr( dot + 1 ).c_str() ) );
                if ( pProperty == nullptr )
                    return false;
                if ( pProperty->_typeName == hashed_string( "float32" ) )
                    outValue = static_cast<float64>( *pProperty->getValuePtr<float32>( pComponent ) );
                else if ( pProperty->_typeName == hashed_string( "int32" ) )
                    outValue = static_cast<float64>( *pProperty->getValuePtr<int32>( pComponent ) );
                else if ( pProperty->_typeName == hashed_string( "uint32" ) )
                    outValue = static_cast<float64>( *pProperty->getValuePtr<uint32>( pComponent ) );
                else
                    return false;
                return true;
            }

            /** @brief `-gv_editorProbePanel` 의 패널이 열려 있으면 1 입니다. 그 id 의 패널이 없으면 값을 내지 않는다. */
            [[nodiscard]] static bool readProbedPanelOpen( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return false;
                const IEditorPanel* pPanel = pContext->getPanelManager().findPanel( gv_editorProbePanel );
                if ( pPanel == nullptr )
                    return false;
                outValue = pPanel->isOpen() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 환경설정 창의 섹션 목록에 지난 프레임 보인 섹션 수입니다(검색이 거른 뒤). */
            [[nodiscard]] static bool readPreferencesVisibleSections( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( PreferencesPanel::getVisibleSectionCount() );
                return true;
            }

            /** @brief 환경설정 파일에 저장된 키 수입니다(모든 섹션 합 — 기본과 다른 값만 저장된다). */
            [[nodiscard]] static bool readPreferencesSavedKeyCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( EditorPreferencesStore::countSavedKeys( EditorPreferencesStore::getDefaultFilePath() ) );
                return true;
            }

            /** @brief 패널 매니저가 가진 패널 수입니다. 등록 목록이 DLL 마다 갈라지면 줄어든다. */
            [[nodiscard]] static bool readPanelCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return false;
                outValue = static_cast<float64>( pContext->getPanelManager().getPanels().size() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_STEP( editorClick, "EditorClick", &EditorScenarioStepsInternal::runClick, &EditorScenarioStepsInternal::validateClick, false );
    SW_AUTOMATION_STEP( editorText, "EditorText", &EditorScenarioStepsInternal::runText, &EditorScenarioStepsInternal::validateText, false );
    SW_AUTOMATION_STEP( editorKey, "EditorKey", &EditorScenarioStepsInternal::runKey, &EditorScenarioStepsInternal::validateKey, false );
    SW_AUTOMATION_STEP( editorExpectObject, "EditorExpectObject", &EditorScenarioStepsInternal::runExpectObject,
                        &EditorScenarioStepsInternal::validateExpectObject, false );
    SW_AUTOMATION_STEP( editorExpectDockLayout, "EditorExpectDockLayout", &EditorScenarioStepsInternal::runDockLayout,
                        &EditorScenarioStepsInternal::validateDockLayout, false );

    SW_AUTOMATION_PROBE( editorPlayState, "Editor.PlayState", "Play session state: 0 stopped, 1 playing, 2 paused", &EditorScenarioStepsInternal::readPlayState );
    SW_AUTOMATION_PROBE( editorSceneDirty, "Editor.SceneDirty", "1 while the edited scene has unsaved changes", &EditorScenarioStepsInternal::readSceneDirty );
    SW_AUTOMATION_PROBE( editorWindowTitleDirty, "Editor.WindowTitleDirty", "1 when the editor window title carries the unsaved mark (*)",
                         &EditorScenarioStepsInternal::readWindowTitleDirty );
    SW_AUTOMATION_PROBE( editorRenderDocAvailable, "Editor.RenderDocAvailable", "1 when RenderDoc is attached (-renderdoc or launched from RenderDoc)",
                         &EditorScenarioStepsInternal::readRenderDocAvailable );
    SW_AUTOMATION_PROBE( editorObjectCount, "Editor.ObjectCount", "Objects in the active scene", &EditorScenarioStepsInternal::readObjectCount );
    SW_AUTOMATION_PROBE( editorSelectionCount, "Editor.SelectionCount", "Selected objects", &EditorScenarioStepsInternal::readSelectionCount );
    SW_AUTOMATION_PROBE( editorHierarchyVisibleRoots, "Editor.HierarchyVisibleRoots", "Root rows the Hierarchy showed in the last frame (after its filter)",
                         &EditorScenarioStepsInternal::readHierarchyVisibleRoots );
    SW_AUTOMATION_PROBE( editorNoSearchResultHintShown, "Editor.NoSearchResultHintShown", "1 when a panel drew its zero-results hint for a search this or last frame",
                         &EditorScenarioStepsInternal::readNoSearchResultHintShown );
    SW_AUTOMATION_PROBE( editorThemePreset, "Editor.ThemePreset", "Active theme preset: 0 ModernDark, 1 DeepCharcoal, 2 MidnightBlue, 3 ClassicDark",
                         &EditorScenarioStepsInternal::readThemePreset );
    SW_AUTOMATION_PROBE( editorAccentColor, "Editor.AccentColor", "Accent color as 0xRRGGBB", &EditorScenarioStepsInternal::readAccentColor );
    SW_AUTOMATION_PROBE( editorSelectedIsEditorCamera, "Editor.SelectedIsEditorCamera", "1 when the primary selection is the viewport's editor camera",
                         &EditorScenarioStepsInternal::readSelectedIsEditorCamera );
    SW_AUTOMATION_PROBE( editorUndoCount, "Editor.UndoCount", "Commands on the undo stack", &EditorScenarioStepsInternal::readUndoCount );
    SW_AUTOMATION_PROBE( editorUndoIndex, "Editor.UndoIndex", "Position on the undo stack (commands not undone)", &EditorScenarioStepsInternal::readUndoIndex );
    SW_AUTOMATION_PROBE( editorLoadingScreenShown, "Editor.LoadingScreenShown", "1 while the runtime UI shows its loading screen",
                         &EditorScenarioStepsInternal::readLoadingScreenShown );
    SW_AUTOMATION_PROBE( editorGridStep, "Editor.GridStep", "Fine line spacing of the viewport grid in meters (1, 10 or 100)",
                         &EditorScenarioStepsInternal::readGridStep );
    SW_AUTOMATION_PROBE( editorGridMajorLines, "Editor.GridMajorLines", "Major lines the viewport grid drew in the last frame",
                         &EditorScenarioStepsInternal::readGridMajorLines );
    SW_AUTOMATION_PROBE( editorGridMisplacedMajorLines, "Editor.GridMisplacedMajorLines",
                         "Major grid lines drawn off a world multiple of 5 x spacing (0 unless major lines slide with the camera)",
                         &EditorScenarioStepsInternal::readGridMisplacedMajorLines );
    SW_AUTOMATION_PROBE( editorSceneViewCameraX, "Editor.SceneViewCameraX", "Scene view (editor) camera world X", &EditorScenarioStepsInternal::readSceneViewCameraX );
    SW_AUTOMATION_PROBE( editorSceneViewCameraY, "Editor.SceneViewCameraY", "Scene view (editor) camera world Y", &EditorScenarioStepsInternal::readSceneViewCameraY );
    SW_AUTOMATION_PROBE( editorGameViewCameraX, "Editor.GameViewCameraX", "Game view camera (the active scene's game camera) world X",
                         &EditorScenarioStepsInternal::readGameViewCameraX );
    SW_AUTOMATION_PROBE( editorGameViewCameraY, "Editor.GameViewCameraY", "Game view camera (the active scene's game camera) world Y",
                         &EditorScenarioStepsInternal::readGameViewCameraY );
    SW_AUTOMATION_PROBE( editorSceneViewRequested, "Editor.SceneViewRequested", "1 when the editor asked the host to render the scene view this frame",
                         &EditorScenarioStepsInternal::readSceneViewRequested );
    SW_AUTOMATION_PROBE( editorGameViewRequested, "Editor.GameViewRequested", "1 when the editor asked the host to render the game view this frame (0 while its panel is hidden)",
                         &EditorScenarioStepsInternal::readGameViewRequested );
    SW_AUTOMATION_PROBE( editorSceneViewDrawn, "Editor.SceneViewDrawn", "1 when the Scene panel drew the scene view this UI frame (its tab is in front)",
                         &EditorScenarioStepsInternal::readSceneViewDrawn );
    SW_AUTOMATION_PROBE( editorUIScale, "Editor.UIScale", "Editor UI scale (1 = 96 DPI)", &EditorScenarioStepsInternal::readUIScale );
    SW_AUTOMATION_PROBE( editorVisualizerOn, "Editor.VisualizerOn", "1 when the scene view shows the visualizer named by gv_editorProbeVisualizer",
                         &EditorScenarioStepsInternal::readVisualizerOn );
    SW_AUTOMATION_PROBE( editorSelectedProperty, "Editor.SelectedProperty",
                         "Numeric value of gv_editorProbeProperty (<ComponentType>.<property>) on the primary selection", &EditorScenarioStepsInternal::readSelectedProperty );
    SW_AUTOMATION_PROBE( editorProbedPanelOpen, "Editor.PanelOpen", "1 when the panel named by gv_editorProbePanel is open",
                         &EditorScenarioStepsInternal::readProbedPanelOpen );
    SW_AUTOMATION_PROBE( editorPreferencesVisibleSections, "Editor.PreferencesVisibleSections", "Sections the Preferences window listed in the last frame (after its search)",
                         &EditorScenarioStepsInternal::readPreferencesVisibleSections );
    SW_AUTOMATION_PROBE( editorPreferencesSavedKeyCount, "Editor.PreferencesSavedKeyCount", "Keys saved in EditorPreferences.json (only values that differ from the defaults)",
                         &EditorScenarioStepsInternal::readPreferencesSavedKeyCount );
    SW_AUTOMATION_PROBE( editorPanelCount, "Editor.PanelCount", "Panels the panel manager holds (registered panels plus directly added ones)",
                         &EditorScenarioStepsInternal::readPanelCount );
} // namespace sw::editor
