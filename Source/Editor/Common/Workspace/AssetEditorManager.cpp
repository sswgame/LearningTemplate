#include "pch.h"

#include "Editor/Common/Workspace/AssetEditorManager.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

namespace sw::editor
{
    void AssetEditorManager::registerAssetEditor( string_view extension, string_view windowTitle )
    {
        _mapOverrideExtToTitle[string{ extension }] = string{ windowTitle };
    }

    string_view AssetEditorManager::findEditorForExtension( string_view extension ) const
    {
        auto it = _mapOverrideExtToTitle.find( string{ extension } );
        if ( it != _mapOverrideExtToTitle.end() )
            return string_view{ it->second };
        return EditorAssetTypeRegistry::findPanelTitleForPath( extension );
    }

    string_view AssetEditorManager::findEditorForPath( string_view assetPath ) const
    {
        size_t        bestLen{ 0 };
        const string* pBestTitle{ nullptr };
        for ( const map<string, string>::value_type& pair : _mapOverrideExtToTitle )
        {
            if ( StringUtil::endsWith( assetPath, pair.first, true ) == false )
                continue;
            if ( pair.first.size() <= bestLen )
                continue;
            bestLen    = pair.first.size();
            pBestTitle = &pair.second;
        }
        if ( pBestTitle != nullptr && pBestTitle->empty() == false )
            return string_view{ *pBestTitle };
        return EditorAssetTypeRegistry::findPanelTitleForPath( assetPath );
    }

    bool AssetEditorManager::openAssetInEditor( string_view assetPath )
    {
        const string_view windowTitle = findEditorForPath( assetPath );
        if ( windowTitle.empty() )
            return false;

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
        {
            pContext->getWorkspace().setFocusedAssetPath( string{ assetPath }.c_str() );
            pContext->getWorkspace().requestOpenPanel( string{ windowTitle }.c_str() );
        }
        return true;
    }

    void AssetEditorManager::registerDefaultMappings()
    {
        _mapOverrideExtToTitle.clear();
    }
} // namespace sw::editor
