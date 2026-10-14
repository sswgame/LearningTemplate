#include "pch.h"

#include "Editor/Common/GUI/EditorPanelHost.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/EditorPaths.h"
#include "Editor/Common/GUI/EditorDockLayout.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

namespace sw::editor
{
    span<const utf8* const> EditorPanelHost::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { EditorPanelManager::kSubsystemName, EditorWorkspace::kSubsystemName };
        return kArrDependency;
    }

    EditorPanelHost::EditorPanelHost( EditorContext& context, EditorPanelManager& panelManager )
        : _context{ context }
        , _panelManager{ panelManager }
    {
    }

    bool EditorPanelHost::focusPanel( string_view panelID )
    {
        const IEditorPanel* pPanel = _panelManager.findPanel( panelID );
        if ( pPanel == nullptr )
            return false;
        _context.getSubsystem<EditorWorkspace>().requestOpenPanel( pPanel->getPanelTitle() );
        return true;
    }

    bool EditorPanelHost::maximizePanel( string_view keepPanelID )
    {
        EditorDockLayout* pDock = _context.findSubsystem<EditorDockLayout>();
        if ( pDock == nullptr )
            return false;
        // 직전 배치는 에디터 상태 폴더의 임시 레이아웃으로 둔다(이름 붙인 레이아웃 목록에 섞이지 않게).
        const string folder = FileUtil::joinPath( EditorPaths::getEditorStateDirectory(), "Temp" );
        if ( pDock->saveNamedLayout( kMaximizeLayoutName, folder ) == false )
            return false;
        vector<string> listCloseID;
        for ( const EditorPanelEntry& entry : _panelManager.getPanels() )
        {
            const bool bPanelToClose = entry._id != keepPanelID && entry._pInstance != nullptr && entry._pInstance->isOpen();
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

    bool EditorPanelHost::restoreMaximizedLayout()
    {
        EditorDockLayout* pDock = _context.findSubsystem<EditorDockLayout>();
        if ( pDock == nullptr )
            return false;
        const string folder = FileUtil::joinPath( EditorPaths::getEditorStateDirectory(), "Temp" );
        return pDock->requestLoadNamedLayout( kMaximizeLayoutName, folder );
    }

    bool EditorPanelHost::saveFocusedDirtyDocument()
    {
        return _panelManager.saveFocusedDirtyDocument();
    }

    bool EditorPanelHost::saveAllDirtyDocuments()
    {
        return _panelManager.saveAllDirtyDocuments();
    }

    void EditorPanelHost::discardAllDirtyDocuments()
    {
        _panelManager.discardAllDirtyDocuments();
    }

    uint32 EditorPanelHost::countDirtyDocuments() const
    {
        return _panelManager.countDirtyDocuments();
    }
} // namespace sw::editor
