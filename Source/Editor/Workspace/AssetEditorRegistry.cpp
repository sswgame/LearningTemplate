#include "pch.h"

#include "Editor/Workspace/AssetEditorRegistry.h"

#include "Editor/Common/EditorContext.h"
#include "Editor/Workspace/EditorWorkspace.h"

namespace sw
{
	namespace
	{
		AssetEditorRegistry* getImpl()
		{
			EditorContext* pContext = EditorContext::get();
			if ( pContext != nullptr )
				return &pContext->getAssetEditorRegistry();
			return nullptr;
		}
	} // namespace

	// ------------------------------------------------------------------------------
	// Static Methods
	// ------------------------------------------------------------------------------
	void AssetEditorRegistry::registerAssetEditor( string_view extension, string_view windowTitle )
	{
		AssetEditorRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerAssetEditorImpl( extension, windowTitle );
	}

	string_view AssetEditorRegistry::findEditorForExtension( string_view extension )
	{
		AssetEditorRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->findEditorForExtensionImpl( extension );
		return {};
	}

	bool AssetEditorRegistry::openAssetInEditor( string_view assetPath )
	{
		AssetEditorRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			return pRegistry->openAssetInEditorImpl( assetPath );
		return false;
	}

	void AssetEditorRegistry::registerDefaultMappings()
	{
		AssetEditorRegistry* pRegistry = getImpl();
		if ( pRegistry != nullptr )
			pRegistry->registerDefaultMappingsImpl();
	}

	// ------------------------------------------------------------------------------
	// Instance Implementations
	// ------------------------------------------------------------------------------
	void AssetEditorRegistry::registerAssetEditorImpl( string_view extension, string_view windowTitle )
	{
		_mapExtToWindowTitle[string{ extension }] = string{ windowTitle };
	}

	string_view AssetEditorRegistry::findEditorForExtensionImpl( string_view extension ) const
	{
		auto it = _mapExtToWindowTitle.find( string{ extension } );
		if ( it != _mapExtToWindowTitle.end() )
			return it->second;
		return {};
	}

	bool AssetEditorRegistry::openAssetInEditorImpl( string_view assetPath )
	{
		if ( assetPath.empty() )
			return false;

		const size_t dotPos = assetPath.rfind( '.' );
		if ( dotPos == string_view::npos )
			return false;

		const string_view ext		  = assetPath.substr( dotPos );
		const string_view windowTitle = findEditorForExtensionImpl( ext );
		if ( windowTitle.empty() )
			return false;

		EditorWorkspace::setFocusedAssetPath( string{ assetPath }.c_str() );
		EditorWorkspace::requestOpenWindow( string{ windowTitle }.c_str() );
		return true;
	}

	void AssetEditorRegistry::registerDefaultMappingsImpl()
	{
		_mapExtToWindowTitle.clear();

		registerAssetEditorImpl( ".anim", "Animation Graph" );
		registerAssetEditorImpl( ".dialogue", "Dialogue Graph" );
		registerAssetEditorImpl( ".tilemap", "Tile Map Tool" );
		registerAssetEditorImpl( ".prefab", "Prefab Editor" );
		registerAssetEditorImpl( ".sprite", "Sprite Clip" );
		registerAssetEditorImpl( ".seq", "Sequencer" );
		registerAssetEditorImpl( ".png", "Sprite Clip" );
		registerAssetEditorImpl( ".jpg", "Sprite Clip" );
	}
} // namespace sw
