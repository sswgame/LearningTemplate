#include "pch.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

namespace sw::editor
{
    string_view EditorAssetTypeRegistry::matchingFocusedPath( EditorAssetType kind )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        const string& focused = editorContext.getSubsystem<EditorWorkspace>().getFocusedAssetPath();
        if ( matches( kind, focused ) == false )
            return {};
        return focused;
    }

    bool EditorAssetTypeRegistry::consumeWorkspaceFocusKey( string& ioLastKey, uint64 extraToken )
    {
        string scanKey = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().getFocusedAssetPath();
        scanKey += '|';
        scanKey += to_string( extraToken );
        if ( scanKey == ioLastKey )
            return false;
        ioLastKey = std::move( scanKey );
        return true;
    }
} // namespace sw::editor
