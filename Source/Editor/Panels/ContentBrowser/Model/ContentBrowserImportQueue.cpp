#include "pch.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserImportQueue.h"

#include "Core/File/FileDialog.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

namespace sw::editor
{
    ContentBrowserImportQueue::ContentBrowserImportQueue()
        : _mutex{}
        , _listPendingPath{}
    {
    }

    void ContentBrowserImportQueue::openDialog( string_view initialFolderAbs )
    {
        FileDialogParams params{};
        params._type                = FileDialogParams::Type::Open;
        params._title               = "Import to Content Browser";
        params._description         = "Assets";
        params._bEnableMultiselect  = true;
        params._initialDirectory    = string{ initialFolderAbs };
        params._listFilterExtension = {};
        EditorAssetTypeRegistry::appendImportExtensions( params._listFilterExtension );

        FileDialog::open( params, SW_DELEGATE_METHOD( FileDialogDelegate, &ContentBrowserImportQueue::onDialogResult, this ) );
    }

    void ContentBrowserImportQueue::onDialogResult( const vector<string>& listPath )
    {
        ScopedLock<mutex> lock{ _mutex };
        _listPendingPath.insert( _listPendingPath.end(), listPath.begin(), listPath.end() );
    }

    bool ContentBrowserImportQueue::takePending( vector<string>& outListPath )
    {
        outListPath.clear();
        ScopedLock<mutex> lock{ _mutex };
        if ( _listPendingPath.empty() )
            return false;
        outListPath.swap( _listPendingPath );
        return true;
    }
} // namespace sw::editor
