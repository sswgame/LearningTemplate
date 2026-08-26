#include "pch.h"

#include "Editor/Common/Workspace/EditorAssetDrop.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Utility/Resource/ResourceManager.h"

#include "RuntimeAPI/Service/EditorService.h"

namespace sw::editor
{
	bool isPrefabAssetPath( const utf8* pPath )
	{
		if ( pPath == nullptr )
			return false;
		return FileUtil::endsWithIgnoreCase( pPath, ".prefab.xml" ) ||
			   FileUtil::endsWithIgnoreCase( pPath, ".prefab.json" ) ||
			   FileUtil::endsWithIgnoreCase( pPath, ".prefab.bin" ) ||
			   FileUtil::endsWithIgnoreCase( pPath, ".prefab" );
	}

	bool isTextureAssetPath( const utf8* pPath )
	{
		if ( pPath == nullptr )
			return false;
		return FileUtil::hasExtension( pPath, ".png" ) || FileUtil::hasExtension( pPath, ".jpg" ) ||
			   FileUtil::hasExtension( pPath, ".jpeg" ) || FileUtil::hasExtension( pPath, ".tga" ) ||
			   FileUtil::hasExtension( pPath, ".dds" ) || FileUtil::hasExtension( pPath, ".hdr" );
	}

	bool isMaterialAssetPath( const utf8* pPath )
	{
		return pPath != nullptr && FileUtil::hasExtension( pPath, "._material" );
	}

	GameObject* spawnPrefabFromAssetPath( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent )
	{
		if ( pManager == nullptr || pPath == nullptr || pPath[0] == '\0' )
			return nullptr;

		if ( isPrefabAssetPath( pPath ) == false )
		{
			SW_LOG_INFO( "[EditorAssetDrop] Not a prefab path: %#", pPath );
			return nullptr;
		}

		GameObject* pSpawned = editor::getService<ResourceManager>()->getPrefabManager().spawn( pManager, pPath );
		if ( pSpawned == nullptr )
		{
			SW_LOG_WARNING( "[EditorAssetDrop] Failed to spawn prefab: %#", pPath );
			return nullptr;
		}

		if ( pParent != nullptr )
			pSpawned->attachToParent( pParent );

		SW_LOG_INFO( "[EditorAssetDrop] Spawned prefab from %#", pPath );
		return pSpawned;
	}
} // namespace sw::editor
