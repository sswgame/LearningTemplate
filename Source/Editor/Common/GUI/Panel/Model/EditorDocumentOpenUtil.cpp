#include "pch.h"

#include "Editor/Common/GUI/Panel/Model/EditorDocumentOpenUtil.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"

namespace sw::editor
{
    void EditorDocumentOpenUtil::rememberRecent( vector<string>& inoutListPath, string_view assetPath, size_t maxCount )
    {
        if ( assetPath.empty() )
            return;
        string path{ assetPath }; // 지우기 전에 복사한다 — @p assetPath 가 목록 안의 글을 가리킬 수 있다
        VectorUtil::removeValue( inoutListPath, path );
        inoutListPath.insert( inoutListPath.begin(), std::move( path ) );
        if ( inoutListPath.size() > maxCount )
            inoutListPath.resize( maxCount );
    }

    bool EditorDocumentOpenUtil::isNewFocus( string_view focused, string_view loaded )
    {
        return focused.empty() == false && focused != loaded;
    }

    string_view EditorDocumentOpenUtil::chooseDocumentToAccept( string_view pending, string_view focused )
    {
        return pending.empty() ? focused : pending;
    }

    bool EditorDocumentOpenUtil::applyAssetMove( const EditorAssetMoveEvent& event, string& inoutLoaded, vector<string>& inoutListRecent )
    {
        for ( size_t index = 0; index < event._listOldID.size() && index < event._listNewID.size(); ++index )
        {
            if ( inoutLoaded == event._listOldID[index] )
                inoutLoaded = event._listNewID[index];
            for ( string& recentPath : inoutListRecent )
            {
                if ( recentPath == event._listOldID[index] )
                    recentPath = event._listNewID[index];
            }
        }
        return inoutLoaded.empty() == false && VectorUtil::contains( event._listRewrittenID, inoutLoaded );
    }

    void EditorDocumentOpenUtil::restoreFocusAfterCancel( EditorWorkspace& workspace, const string& loaded )
    {
        workspace.setFocusedAssetPath( loaded.c_str() );
    }
} // namespace sw::editor
