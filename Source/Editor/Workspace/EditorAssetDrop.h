#pragma once
/**
 * @file Workspace/EditorAssetDrop.h
 * @brief Shared SW_ASSET_PATH drag-drop helpers for editor panels
 */

#include "Core/Common/CommonHeaders.h"

namespace sw
{
	class GameObject;
	class GameObjectManager;

	namespace editor
	{
		bool isPrefabAssetPath( const char* path );
		bool isTextureAssetPath( const char* path );
		bool isMaterialAssetPath( const char* path );

		/** @brief Spawn prefab under optional parent; logs and returns nullptr on failure. */
		GameObject* spawnPrefabFromAssetPath( GameObjectManager* manager, const char* path, GameObject* parent = nullptr );
	} // namespace editor
} // namespace sw
