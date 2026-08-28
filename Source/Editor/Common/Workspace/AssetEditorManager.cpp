#include "pch.h"

#include "Editor/Common/Workspace/AssetEditorManager.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

namespace sw::editor
{
	void AssetEditorManager::registerAssetEditor( string_view extension, string_view windowTitle )
	{
		_mapExtToWindowTitle[string{ extension }] = string{ windowTitle };
	}

	string_view AssetEditorManager::findEditorForExtension( string_view extension ) const
	{
		auto it = _mapExtToWindowTitle.find( string{ extension } );
		if ( it != _mapExtToWindowTitle.end() )
			return it->second;
		return {};
	}

	string_view AssetEditorManager::findEditorForPath( string_view assetPath ) const
	{
		const string pathStr{ assetPath };
		size_t		 bestLen{ 0 };
		string_view	 bestTitle{};
		for ( const map<string, string>::value_type& pair : _mapExtToWindowTitle )
		{
			if ( FileUtil::endsWithIgnoreCase( pathStr, pair.first ) == false )
				continue;
			if ( pair.first.size() <= bestLen )
				continue;
			bestLen	  = pair.first.size();
			bestTitle = pair.second;
		}
		return bestTitle;
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
		_mapExtToWindowTitle.clear();

		registerAssetEditor( ".anim.json", "Animation Graph" );
		registerAssetEditor( ".anim", "Animation Graph" );
		registerAssetEditor( ".dialogue.json", "Dialogue Graph" );
		registerAssetEditor( ".dialogue", "Dialogue Graph" );
		registerAssetEditor( ".tilemap.xml", "Tile Map Tool" );
		registerAssetEditor( ".tilemap", "Tile Map Tool" );
		registerAssetEditor( ".prefab.xml", "Prefab Editor" );
		registerAssetEditor( ".prefab.json", "Prefab Editor" );
		registerAssetEditor( ".prefab.bin", "Prefab Editor" );
		registerAssetEditor( ".prefab", "Prefab Editor" );
		registerAssetEditor( ".pfb", "Prefab Editor" );
		registerAssetEditor( ".sprite.json", "Sprite Clip" );
		registerAssetEditor( ".sprite", "Sprite Clip" );
		registerAssetEditor( ".seq.json", "Sequencer" );
		registerAssetEditor( ".seq", "Sequencer" );
		registerAssetEditor( ".png", "Sprite Clip" );
		registerAssetEditor( ".jpg", "Sprite Clip" );
		registerAssetEditor( ".jpeg", "Sprite Clip" );
		registerAssetEditor( ".dds", "Sprite Clip" );
		registerAssetEditor( ".tga", "Sprite Clip" );
	}
} // namespace sw::editor
