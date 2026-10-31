#include "pch.h"

#include "Editor/Common/GUI/EditorUIContext.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/GUI/Panel/EditorDockLayout.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include <imgui.h>
#include <implot.h>

namespace sw::editor
{
    void EditorUIContext::publish( const EditorUIContextState& state )
    {
        using BinderRegistry = EditorExtensionRegistry<EditorUIBinderRegistration>;
        for ( uint32 index = 0; index < BinderRegistry::getCount(); ++index )
        {
            const EditorUIBinderRegistration& registration = BinderRegistry::getAt( index );
            if ( registration._pfnBind != nullptr )
                registration._pfnBind( state );
        }
    }

    EditorUIContextState EditorUIContext::getCurrent()
    {
        // 컨텍스트의 주인은 ImGui 자신이다(이 모듈의 사본). 같은 값을 따로 들고 있으면 두 곳이 어긋날 수 있어 묻는다.
        return EditorUIContextState{ ImGui::GetCurrentContext(), ImPlot::GetCurrentContext() };
    }

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
                EditorContext*    pContext          = editor::findSubsystem<EditorContext>();
                EditorDockLayout* pDock             = pContext != nullptr ? pContext->findSubsystem<EditorDockLayout>() : nullptr; // 선택: 셸이 도킹 배치를 알리기 전과 셸 없는 테스트 호스트에는 없다
                const bool        bSaveLayoutFailed = listArgument.size() != 1 || pDock == nullptr || pDock->saveNamedLayout( listArgument[0] ) == false;
                if ( bSaveLayoutFailed )
                    return false;
                outReply = "saved layout " + listArgument[0];
                return true;
            }

            static bool runLayoutLoad( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*    pContext          = editor::findSubsystem<EditorContext>();
                EditorDockLayout* pDock             = pContext != nullptr ? pContext->findSubsystem<EditorDockLayout>() : nullptr; // 선택: 셸이 도킹 배치를 알리기 전과 셸 없는 테스트 호스트에는 없다
                const bool        bLoadLayoutFailed = listArgument.size() != 1 || pDock == nullptr || pDock->requestLoadNamedLayout( listArgument[0] ) == false;
                if ( bLoadLayoutFailed )
                    return false;
                outReply = "loading layout " + listArgument[0];
                return true;
            }

            /** @brief 패널을 열고 그 창을 앞으로 가져옵니다(같은 도크 영역의 탭이면 그 탭이 선택된다). 다음 프레임 시작에 적용됩니다. */
            static bool runPanelFocus( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*      pContext = editor::findSubsystem<EditorContext>();
                const IEditorPanel* pPanel   = ( listArgument.size() == 1 && pContext != nullptr ) ? pContext->getSubsystem<EditorPanelManager>().findPanelByID( listArgument[0] ) : nullptr;
                if ( pPanel == nullptr )
                    return false;
                pContext->getSubsystem<EditorWorkspace>().requestOpenPanelByID( listArgument[0] );
                outReply = string{ "focusing " } + pPanel->getPanelTitle();
                return true;
            }

            static bool runPanelClose( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext          = editor::findSubsystem<EditorContext>();
                const bool     bClosePanelFailed = listArgument.size() != 1 || pContext == nullptr || pContext->getSubsystem<EditorPanelManager>().setPanelOpen( listArgument[0], false ) == false;
                if ( bClosePanelFailed )
                    return false;
                const IEditorPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanelByID( listArgument[0] );
                outReply                   = ( pPanel != nullptr && pPanel->isOpen() ? "waiting for the close confirmation of " : "closed " ) + listArgument[0];
                return true;
            }

            /** @brief 핵심(Core)이 아닌 패널(도구 · 커스텀 · 에디터 확장)을 닫고 새 인스턴스로 바꿉니다 — 새 에디터 세션의 패널 상태로 돌립니다(시나리오 체인의 초기화가 쓴다). */
            static bool runPanelResetOptional( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( listArgument.empty() == false || pContext == nullptr )
                    return false;
                const uint32 resetCount = pContext->getSubsystem<EditorPanelManager>().resetOptionalPanels( pContext->getSubsystem<EditorRenderViews>().getRHIDevice() );
                outReply                = formatToString( "reset %# optional panel(s)", resetCount );
                return true;
            }

            static bool runLayoutReset( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*    pContext = editor::findSubsystem<EditorContext>();
                EditorDockLayout* pDock    = pContext != nullptr ? pContext->findSubsystem<EditorDockLayout>() : nullptr; // 선택: 셸이 도킹 배치를 알리기 전과 셸 없는 테스트 호스트에는 없다
                if ( listArgument.empty() == false || pDock == nullptr )
                    return false;
                pDock->requestResetDefault();
                outReply = "resetting to the default layout";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( Theme, "editor.theme", "editor.theme <name>", "Apply an editor theme token set (Content/editor/theme/<name>.tokens.json) for this session",
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
