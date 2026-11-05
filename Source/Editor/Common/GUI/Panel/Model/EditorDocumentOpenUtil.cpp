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

    void EditorDocumentOpenUtil::restoreFocusAfterCancel( EditorWorkspace& workspace, const string& loaded )
    {
        workspace.setFocusedAssetPath( loaded.c_str() );
    }
} // namespace sw::editor
