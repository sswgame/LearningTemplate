#include "pch.h"

#include "Editor/Common/GUI/EditorUIContextState.h"

#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/GUI/Panel/EditorDockLayout.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/IEditorDocumentPanel.h"
#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Console/DevCommandRegistry.h"

#include <imgui.h>
#include <implot.h>

namespace sw::editor
{
    void EditorUIContextState::publish( const EditorUIContextState& state )
    {
        using BinderRegistry = EditorExtensionRegistry<EditorUIBinderRegistration>;
        for ( uint32 index = 0; index < BinderRegistry::getCount(); ++index )
        {
            const EditorUIBinderRegistration& registration = BinderRegistry::getAt( index );
            if ( registration._pfnBind != nullptr )
                registration._pfnBind( state );
        }
    }

    EditorUIContextState EditorUIContextState::getCurrent()
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

            static bool runPanelFloat( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*      pContext = editor::findSubsystem<EditorContext>();
                const IEditorPanel* pPanel   = ( listArgument.size() == 1 && pContext != nullptr ) ? pContext->getSubsystem<EditorPanelManager>().findPanelByID( listArgument[0] ) : nullptr;
                if ( pPanel == nullptr )
                    return false;
                pContext->getSubsystem<EditorWorkspace>().requestFloatPanelByID( listArgument[0] );
                outReply = string{ "floating " } + pPanel->getPanelTitle();
                return true;
            }

            /** @brief 인자의 id 가 문서 패널이면 그것을, 아니면 이유를 적고 nullptr 를 돌려줍니다(`panel.saveDocument` · `panel.reloadDocument`). */
            static IEditorDocumentPanel* findDocumentPanel( const vector<string>& listArgument, string& outReply )
            {
                EditorContext*        pContext  = editor::findSubsystem<EditorContext>();
                IEditorPanel*         pPanel    = ( listArgument.size() == 1 && pContext != nullptr ) ? pContext->getSubsystem<EditorPanelManager>().findPanelByID( listArgument[0] ) : nullptr;
                IEditorDocumentPanel* pDocument = pPanel != nullptr ? pPanel->asDocumentPanel() : nullptr;
                if ( pDocument == nullptr )
                    outReply = "no document panel '" + ( listArgument.empty() ? string{} : listArgument[0] ) + "'";
                return pDocument;
            }

            /**
             * @brief 문서 패널의 문서를 dirty 와 무관하게 저장합니다(Ctrl+S 는 dirty 일 때만 저장한다). 저장하지 못하면 실패입니다.
             * @details 저장 왕복 시나리오가 연 문서를 고치지 않고 저장해 정규 꼴로 만든 뒤, 다시 읽고(`panel.reloadDocument`) 또 저장해 바이트를 견준다(`asset.expectScratchUnchanged`).
             */
            static bool runPanelSaveDocument( const vector<string>& listArgument, string& outReply )
            {
                IEditorDocumentPanel* pDocument = findDocumentPanel( listArgument, outReply );
                if ( pDocument == nullptr )
                    return false;
                if ( pDocument->saveDocumentAndClearDirty() == false )
                {
                    outReply = "could not save the document of '" + listArgument[0] + "'";
                    return false;
                }
                outReply = "saved the document of " + listArgument[0];
                return true;
            }

            /** @brief 문서 패널의 문서를 dirty 와 무관하게 디스크에서 다시 읽습니다(`IEditorDocumentPanel::reloadDocument`). */
            static bool runPanelReloadDocument( const vector<string>& listArgument, string& outReply )
            {
                IEditorDocumentPanel* pDocument = findDocumentPanel( listArgument, outReply );
                if ( pDocument == nullptr )
                    return false;
                pDocument->reloadDocument();
                outReply = "reloaded the document of " + listArgument[0];
                return true;
            }

            /** @brief `panel.lockSaveFiles` 가 붙잡은 파일 글입니다(디스크 경로 → 글, 없던 파일은 빈 값). 에디터 범위의 상태 저장소가 소유한다. */
            struct LockedSaveFiles
            {
                unordered_map<string, optional<string>> _mapTextByPath;
            };

            /**
             * @brief 문서 패널이 저장할 파일(`IEditorDocumentPanel::collectFilesToSave`)의 글을 붙잡고 모두 읽기 전용으로 둡니다 — 버전 관리가 잠그지 않은 파일(git LFS lockable)을 흉내 낸다.
             * @details 패널이 내놓는 목록을 그대로 잠그므로 여러 파일을 쓰는 패널 · 연 경로가 없는 상태 문서도 같은 명령이다. 목록이 비면 확인할 것이 없어 실패다. 끝에 `panel.unlockSaveFiles` 로 푼다.
             */
            static bool runPanelLockSaveFiles( const vector<string>& listArgument, string& outReply )
            {
                const IEditorDocumentPanel* pDocument = findDocumentPanel( listArgument, outReply );
                if ( pDocument == nullptr )
                    return false;
                vector<string> listPath;
                pDocument->collectFilesToSave( listPath );
                if ( listPath.empty() )
                {
                    outReply = "'" + listArgument[0] + "' has no file to save - open or edit its document first";
                    return false;
                }
                LockedSaveFiles& locked = getEditorState<LockedSaveFiles>();
                locked._mapTextByPath.clear();
                for ( const string& path : listPath )
                {
                    string     text;
                    const bool bExists = FileUtil::isRegularFile( path );
                    if ( bExists && ( FileUtil::readTextFile( path, text ) == false || FileUtil::setWritable( path, false ) == false ) )
                    {
                        outReply = "could not read or lock '" + path + "'";
                        return false;
                    }
                    locked._mapTextByPath.insert_or_assign( path, bExists ? optional<string>{ std::move( text ) } : nullopt );
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "locked %# file(s) of %#", listPath.size(), listArgument[0] );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief 문서 패널 저장이 거절되면(`saveDocumentAndClearDirty` 가 false) 성공입니다 — 잠근 파일 앞에서 쓰지 않고 알렸는지 본다(알림 · 로그는 시나리오가 따로 본다). */
            static bool runPanelExpectSaveRefused( const vector<string>& listArgument, string& outReply )
            {
                IEditorDocumentPanel* pDocument = findDocumentPanel( listArgument, outReply );
                if ( pDocument == nullptr )
                    return false;
                if ( pDocument->saveDocumentAndClearDirty() )
                {
                    outReply = "the document of '" + listArgument[0] + "' was saved over locked files";
                    return false;
                }
                outReply = "the save of " + listArgument[0] + " was refused";
                return true;
            }

            /** @brief `panel.lockSaveFiles` 로 잠근 파일의 쓰기를 다시 허락하고, 글이 그대로인지(없던 파일은 여전히 없는지) 견줍니다. 하나라도 바뀌었으면 실패입니다. */
            static bool runPanelUnlockSaveFiles( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                LockedSaveFiles& locked = getEditorState<LockedSaveFiles>();
                string           changedPath;
                for ( const auto& [path, savedText] : locked._mapTextByPath )
                {
                    string     text;
                    const bool bExists = FileUtil::isRegularFile( path );
                    if ( bExists )
                        (void)FileUtil::setWritable( path, true ); // 먼저 푼다 — 견주기가 져도 다음 시나리오가 잠긴 파일을 만나지 않게
                    const bool bSame = savedText.has_value() ? ( bExists && FileUtil::readTextFile( path, text ) && text == *savedText ) : bExists == false;
                    if ( bSame == false && changedPath.empty() )
                        changedPath = path;
                }
                locked._mapTextByPath.clear();
                if ( changedPath.empty() == false )
                {
                    outReply = "'" + changedPath + "' changed while it was locked";
                    return false;
                }
                outReply = "unlocked and unchanged";
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
                const uint32                             resetCount = pContext->getSubsystem<EditorPanelManager>().resetOptionalPanels();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "reset %# optional panel(s)", resetCount );
                outReply = string( replyBuilder.view() );
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
    SW_DEV_COMMAND( PanelFloat, "panel.float", "panel.float <panelID>",
                    "Undock an open panel and move it outside the main window on the next frame - it becomes its own OS window (an ImGui platform viewport)",
                    &EditorUIDevCommandsInternal::runPanelFloat );
    SW_DEV_COMMAND( PanelClose, "panel.close", "panel.close <panelID>", "Close a panel", &EditorUIDevCommandsInternal::runPanelClose );
    SW_DEV_COMMAND( PanelSaveDocument, "panel.saveDocument", "panel.saveDocument <panelID>", "Save the document of a document panel whether or not it has unsaved edits (save round trips)",
                    &EditorUIDevCommandsInternal::runPanelSaveDocument );
    SW_DEV_COMMAND( PanelLockSaveFiles, "panel.lockSaveFiles", "panel.lockSaveFiles <panelID>",
                    "Remember the text of every file a document panel would save and make them read-only, like files not checked out (undo with panel.unlockSaveFiles)",
                    &EditorUIDevCommandsInternal::runPanelLockSaveFiles );
    SW_DEV_COMMAND( PanelExpectSaveRefused, "panel.expectSaveRefused", "panel.expectSaveRefused <panelID>",
                    "Fail unless saving the document of a document panel is refused (its files are locked)", &EditorUIDevCommandsInternal::runPanelExpectSaveRefused );
    SW_DEV_COMMAND( PanelUnlockSaveFiles, "panel.unlockSaveFiles", "panel.unlockSaveFiles",
                    "Make the files panel.lockSaveFiles locked writable again and fail if any of them changed while locked", &EditorUIDevCommandsInternal::runPanelUnlockSaveFiles );
    SW_DEV_COMMAND( PanelReloadDocument, "panel.reloadDocument", "panel.reloadDocument <panelID>", "Read the document of a document panel again from disk, dropping unsaved edits (save round trips)",
                    &EditorUIDevCommandsInternal::runPanelReloadDocument );
    SW_DEV_COMMAND( PanelResetOptional, "panel.resetOptional", "panel.resetOptional",
                    "Close every panel that is not a core panel (tools, custom and extension panels) and replace it with a new instance, as in a new editor session",
                    &EditorUIDevCommandsInternal::runPanelResetOptional );
    SW_DEV_COMMAND( LayoutReset, "layout.reset", "layout.reset", "Reset the dock layout to the default editor layout on the next frame (scenarios that click by position)",
                    &EditorUIDevCommandsInternal::runLayoutReset );

    // 이 종류의 등록 목록 — EditorModule 이미지에 하나(확장 DLL 의 등록자도 내보낸 이 함수로 오른다).
    SW_DEFINE_EDITOR_EXTENSION_KIND( EditorUIBinderRegistration )
} // namespace sw::editor
