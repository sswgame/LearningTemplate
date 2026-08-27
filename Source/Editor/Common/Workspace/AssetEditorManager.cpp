#include "pch.h"

#include "Editor/Common/Workspace/AssetEditorManager.h"

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

	bool AssetEditorManager::openAssetInEditor( string_view assetPath )
	{
		const size_t dotPos = assetPath.rfind( '.' );
		if ( dotPos == string_view::npos )
			return false;

		const string_view ext		  = assetPath.substr( dotPos );
		const string_view windowTitle = findEditorForExtension( ext );
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

		registerAssetEditor( ".anim", "Animation Graph" );
		registerAssetEditor( ".dialogue", "Dialogue Graph" );
		registerAssetEditor( ".tilemap", "Tile Map Tool" );
		registerAssetEditor( ".prefab", "Prefab Editor" );
		registerAssetEditor( ".sprite", "Sprite Clip" );
		registerAssetEditor( ".seq", "Sequencer" );
		registerAssetEditor( ".png", "Sprite Clip" );
		registerAssetEditor( ".jpg", "Sprite Clip" );
		registerAssetEditor( ".dds", "Sprite Clip" );
	}
} // namespace sw::editor
