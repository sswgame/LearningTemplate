#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPanelService.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorDockLayout.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanelBase.h"
#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

namespace sw::editor
{
    span<const utf8* const> EditorPanelService::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { EditorPanelManager::kSubsystemName, EditorWorkspace::kSubsystemName };
        return kArrDependency;
    }

    EditorPanelService::EditorPanelService( EditorContext& context, EditorPanelManager& panelManager )
        : _context{ context }
        , _panelManager{ panelManager }
    {
    }

    bool EditorPanelService::focusPanel( EditorPanelID panelID )
    {
        if ( _panelManager.findPanelByID( panelID.getView() ) == nullptr )
            return false;
        _context.getSubsystem<EditorWorkspace>().requestOpenPanelByID( panelID.getView() );
        return true;
    }

    bool EditorPanelService::maximizePanel( EditorPanelID keepPanelID )
    {
        EditorDockLayout* pDock = _context.findSubsystem<EditorDockLayout>(); // 선택: 셸이 도킹 배치를 알리기 전과 셸 없는 테스트 호스트에는 없다
        if ( pDock == nullptr )
            return false;
        // 직전 배치는 에디터 상태 폴더의 임시 레이아웃으로 둔다(이름 붙인 레이아웃 목록에 섞이지 않게).
        const AbsolutePath folder = EditorPaths::getEditorStateDirectory().join( "Temp" );
        if ( pDock->saveNamedLayout( kMaximizeLayoutName, folder ) == false )
            return false;
        vector<string> listCloseID;
        for ( const EditorPanelEntry& entry : _panelManager.getPanels() )
        {
            const bool bPanelToClose = entry._id != keepPanelID.getView() && entry._pInstance != nullptr && entry._pInstance->isOpen();
            if ( bPanelToClose )
                listCloseID.push_back( entry._id );
        }
        for ( const string& panelID : listCloseID )
        {
            (void)_panelManager.setPanelOpen( panelID, false ); // 이미 닫혔으면 할 일이 없다
        }
        (void)focusPanel( keepPanelID ); // 없는 id 면 닫기만 한 것이다
        return true;
    }

    bool EditorPanelService::restoreMaximizedLayout()
    {
        EditorDockLayout* pDock = _context.findSubsystem<EditorDockLayout>(); // 선택: 셸이 도킹 배치를 알리기 전과 셸 없는 테스트 호스트에는 없다
        if ( pDock == nullptr )
            return false;
        const AbsolutePath folder = EditorPaths::getEditorStateDirectory().join( "Temp" );
        return pDock->requestLoadNamedLayout( kMaximizeLayoutName, folder );
    }

    bool EditorPanelService::saveFocusedDirtyDocument()
    {
        return _panelManager.saveFocusedDirtyDocument();
    }

    bool EditorPanelService::saveAllDirtyDocuments()
    {
        return _panelManager.saveAllDirtyDocuments();
    }

    void EditorPanelService::discardAllDirtyDocuments()
    {
        _panelManager.discardAllDirtyDocuments();
    }

    uint32 EditorPanelService::countDirtyDocuments() const
    {
        return _panelManager.countDirtyDocuments();
    }

    void EditorPanelService::collectOpenDocuments( vector<EditorOpenDocument>& outList ) const
    {
        for ( const EditorPanelEntry& entry : _panelManager.getPanels() )
        {
            const EditorDocumentPanelBase* const pDocument = entry._pInstance != nullptr ? entry._pInstance->asDocumentPanel() : nullptr;
            if ( pDocument == nullptr || pDocument->getDocumentAssetPath().empty() )
                continue;
            const bool bDirty      = pDocument->isDocumentDirty();
            string     unsavedText = bDirty ? pDocument->captureUnsavedText() : string{};
            const bool bFollows    = bDirty == false || unsavedText.empty() == false; // 글이 있어야 새 자리에서 다시 읽은 뒤 편집을 얹는다
            outList.push_back( EditorOpenDocument{ string{ pDocument->getDocumentAssetPath() }, bDirty, std::move( unsavedText ), bFollows } );
        }
    }

    void EditorPanelService::collectPanelCommandStacks( uint32& outFocusedWindowID, vector<EditorPanelCommandStack>& outList ) const
    {
        outFocusedWindowID = EditorChrome::findFocusedRootWindowID();
        outList.clear();
        for ( const EditorPanelEntry& entry : _panelManager.getPanels() )
        {
            EditorPanel* const pPanel = entry._pInstance.get();
            if ( pPanel == nullptr || pPanel->isOpen() == false )
                continue;
            outList.push_back( EditorPanelCommandStack{ pPanel->getWindowID(), pPanel->findPanelCommandStack(), pPanel->shouldKeepFocusedCommandStack(), entry._id } );
        }
    }
} // namespace sw::editor
