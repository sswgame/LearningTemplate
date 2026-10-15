/**
 * @file EditorUIDevCommands.cpp
 * @brief 에디터 UI 를 만지는 개발 명령(레이아웃 저장 · 읽기 · 초기화, 패널 앞으로 · 닫기, 테마)입니다.
 * @details 도킹 · 패널 매니저 · 테마는 GUI 층이라 ImGui 없는 동작(`Common/Commands`)이 아니라 여기서 등록합니다.
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"

#include "Editor/Common/GUI/EditorDockLayout.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

namespace sw::editor
{
    namespace
    {
        struct EditorUIDevCommandsInternal
        {
            static bool runTheme( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                // 적용만 한다 — 설정 파일에 쓰지 않는다(테스트 · 시나리오가 사용자 설정을 바꾸지 않게). 대화상자에서 고르면 저장된다.
                if ( EditorThemeUtil::applyThemeByName( listArgument[0] ) == false )
                {
                    vector<string> listName;
                    EditorThemeUtil::listThemes( listName );
                    outReply = "unknown theme '" + listArgument[0] + "' (";
                    for ( size_t nameIndex = 0; nameIndex < listName.size(); ++nameIndex )
                    {
                        if ( nameIndex > 0 )
                            outReply += ", ";
                        outReply += listName[nameIndex];
                    }
                    outReply += ")";
                    return false;
                }
                outReply = "theme " + listArgument[0];
                return true;
            }

            static bool runLayoutSave( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*    pContext          = EditorContext::get();
                EditorDockLayout* pDock             = pContext != nullptr ? pContext->findSubsystem<EditorDockLayout>() : nullptr;
                const bool        bSaveLayoutFailed = listArgument.size() != 1 || pDock == nullptr || pDock->saveNamedLayout( listArgument[0] ) == false;
                if ( bSaveLayoutFailed )
                    return false;
                outReply = "saved layout " + listArgument[0];
                return true;
            }

            static bool runLayoutLoad( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*    pContext          = EditorContext::get();
                EditorDockLayout* pDock             = pContext != nullptr ? pContext->findSubsystem<EditorDockLayout>() : nullptr;
                const bool        bLoadLayoutFailed = listArgument.size() != 1 || pDock == nullptr || pDock->requestLoadNamedLayout( listArgument[0] ) == false;
                if ( bLoadLayoutFailed )
                    return false;
                outReply = "loading layout " + listArgument[0];
                return true;
            }

            /** @brief 패널을 열고 그 창을 앞으로 가져옵니다(같은 도크 영역의 탭이면 그 탭이 선택된다). 다음 프레임 시작에 적용됩니다. */
            static bool runPanelFocus( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*      pContext = EditorContext::get();
                const IEditorPanel* pPanel   = ( listArgument.size() == 1 && pContext != nullptr ) ? pContext->getSubsystem<EditorPanelManager>().findPanelByID( listArgument[0] ) : nullptr;
                if ( pPanel == nullptr )
                    return false;
                pContext->getSubsystem<EditorWorkspace>().requestOpenPanel( pPanel->getPanelTitle() );
                outReply = string{ "focusing " } + pPanel->getPanelTitle();
                return true;
            }

            static bool runPanelClose( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext          = EditorContext::get();
                const bool     bClosePanelFailed = listArgument.size() != 1 || pContext == nullptr || pContext->getSubsystem<EditorPanelManager>().setPanelOpen( listArgument[0], false ) == false;
                if ( bClosePanelFailed )
                    return false;
                outReply = "closed " + listArgument[0];
                return true;
            }

            /** @brief 핵심(Core)이 아닌 패널(도구 · 커스텀 · 에디터 확장)을 닫고 새 인스턴스로 바꿉니다 — 새 에디터 세션의 패널 상태로 돌립니다(시나리오 체인의 초기화가 쓴다). */
            static bool runPanelResetOptional( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = EditorContext::get();
                if ( listArgument.empty() == false || pContext == nullptr )
                    return false;
                const uint32 resetCount = pContext->getSubsystem<EditorPanelManager>().resetOptionalPanels( pContext->getSubsystem<EditorRenderViews>().getRHIDevice() );
                outReply                = "reset " + to_string( resetCount ) + " optional panel(s)";
                return true;
            }

            static bool runLayoutReset( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*    pContext = EditorContext::get();
                EditorDockLayout* pDock    = pContext != nullptr ? pContext->findSubsystem<EditorDockLayout>() : nullptr;
                if ( listArgument.empty() == false || pDock == nullptr )
                    return false;
                pDock->requestResetDefault();
                outReply = "resetting to the default layout";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( Theme, "editor.theme", "editor.theme <name>", "Apply an editor theme token set (Resource/editor/theme/<name>.tokens.json) for this session",
                    &EditorUIDevCommandsInternal::runTheme );
    SW_DEV_COMMAND( LayoutSave, "layout.save", "layout.save <name>", "Save the dock layout and panel visibility under a name",
                    &EditorUIDevCommandsInternal::runLayoutSave );
    SW_DEV_COMMAND( LayoutLoad, "layout.load", "layout.load <name>", "Load a named layout on the next frame", &EditorUIDevCommandsInternal::runLayoutLoad );
    SW_DEV_COMMAND( PanelFocus, "panel.focus", "panel.focus <panelID>", "Open a panel and bring its window (its tab) to the front on the next frame",
                    &EditorUIDevCommandsInternal::runPanelFocus );
    SW_DEV_COMMAND( PanelClose, "panel.close", "panel.close <panelID>", "Close a panel", &EditorUIDevCommandsInternal::runPanelClose );
    SW_DEV_COMMAND( PanelResetOptional, "panel.resetOptional", "panel.resetOptional",
                    "Close every panel that is not a core panel (tools, custom and extension panels) and replace it with a new instance, as in a new editor session",
                    &EditorUIDevCommandsInternal::runPanelResetOptional );
    SW_DEV_COMMAND( LayoutReset, "layout.reset", "layout.reset", "Reset the dock layout to the default editor layout on the next frame (scenarios that click by position)",
                    &EditorUIDevCommandsInternal::runLayoutReset );
} // namespace sw::editor
