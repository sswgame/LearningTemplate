#include "pch.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserReferenceSearch.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Content/AssetPaths.h"

namespace sw::editor
{
    ContentBrowserReferenceSearch::ContentBrowserReferenceSearch()
        : _queryID{}
        , _listResult{}
        , _bDependencyQuery{ SW_FALSE }
    {
    }

    void ContentBrowserReferenceSearch::query( const EditorReferenceIndex& index, string_view absolutePath, bool bDependencies )
    {
        _queryID          = editor::getSubsystem<AssetPaths>().toAssetID( absolutePath );
        _bDependencyQuery = bDependencies ? SW_TRUE : SW_FALSE;
        if ( bDependencies )
            index.findDependencies( _queryID, _listResult );
        else
            index.findReferrers( _queryID, _listResult );
    }
} // namespace sw::editor
