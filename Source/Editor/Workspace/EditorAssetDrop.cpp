/**
 * @file EditorAssetDrop.cpp
 */
#include "Workspace/EditorAssetDrop.h"
#include "Core/Game/Prefab/PrefabAsset.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Utility/Log/Logger.h"

#include <cstring>

namespace sw::editor
{
	namespace
	{
		bool endsWithIgnoreCase( const char* path, const char* suffix )
		{
			if ( path == nullptr || suffix == nullptr )
				return false;

			const size_t pathLen   = std::strlen( path );
			const size_t suffixLen = std::strlen( suffix );
			if ( pathLen < suffixLen )
				return false;

			const char* tail = path + ( pathLen - suffixLen );
			for ( size_t i = 0; i < suffixLen; ++i )
			{
				char a = tail[i];
				char b = suffix[i];
				if ( a >= 'A' && a <= 'Z' )
					a = static_cast<char>( a - 'A' + 'a' );
				if ( b >= 'A' && b <= 'Z' )
					b = static_cast<char>( b - 'A' + 'a' );
				if ( a != b )
					return false;
			}
			return true;
		}
	} // namespace

	bool isPrefabAssetPath( const char* path )
	{
		return endsWithIgnoreCase( path, ".prefab.xml" ) ||
			   endsWithIgnoreCase( path, ".prefab.json" ) ||
			   endsWithIgnoreCase( path, ".prefab.bin" ) ||
			   endsWithIgnoreCase( path, ".prefab" );
	}

	bool isTextureAssetPath( const char* path )
	{
		return endsWithIgnoreCase( path, ".png" ) || endsWithIgnoreCase( path, ".jpg" ) ||
			   endsWithIgnoreCase( path, ".jpeg" ) || endsWithIgnoreCase( path, ".tga" ) ||
			   endsWithIgnoreCase( path, ".dds" ) || endsWithIgnoreCase( path, ".hdr" );
	}

	bool isMaterialAssetPath( const char* path )
	{
		return endsWithIgnoreCase( path, ".material" );
	}

	GameObject* spawnPrefabFromAssetPath( GameObjectManager* manager, const char* path, GameObject* parent )
	{
		if ( manager == nullptr || path == nullptr || path[0] == '\0' )
			return nullptr;

		if ( isPrefabAssetPath( path ) == false )
		{
			SW_LOG_INFO( "[EditorAssetDrop] Not a prefab path: %#", path );
			return nullptr;
		}

		GameObject* spawned = PrefabManager::get().spawn( manager, path );
		if ( spawned == nullptr )
		{
			SW_LOG_WARNING( "[EditorAssetDrop] Failed to spawn prefab: %#", path );
			return nullptr;
		}

		if ( parent != nullptr )
			spawned->attachToParent( parent );

		SW_LOG_INFO( "[EditorAssetDrop] Spawned prefab from %#", path );
		return spawned;
	}
} // namespace sw::editor
