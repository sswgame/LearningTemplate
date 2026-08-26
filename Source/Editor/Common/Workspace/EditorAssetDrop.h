/**
 * @file Workspace/EditorAssetDrop.h
 * @brief 에디터 패널용 SW_ASSET_PATH 드래그앤드롭 헬퍼
 */
#pragma once
#include "Core/Common/StdHeaders.h"

namespace sw
{
	class GameObject;
	class GameObjectManager;
}

namespace sw::editor
{
	// ------------------------------------------------------------------------------
	// 1) 경로 판별 — 확장자로 프리팹 / 텍스처 / 머티리얼
	// ------------------------------------------------------------------------------
	/** @brief 경로가 프리팹 애셋인지 여부를 반환합니다. */
	bool isPrefabAssetPath( const utf8* pPath );
	/** @brief 경로가 텍스처 애셋인지 여부를 반환합니다. */
	bool isTextureAssetPath( const utf8* pPath );
	/** @brief 경로가 머티리얼 애셋인지 여부를 반환합니다. */
	bool isMaterialAssetPath( const utf8* pPath );

	// ------------------------------------------------------------------------------
	// 2) 프리팹 스폰 — 드롭 대상이 프리팹일 때
	// ------------------------------------------------------------------------------
	/** @brief 선택적 부모 아래 프리팹을 스폰합니다. 실패 시 로그 후 nullptr을 반환합니다. */
	GameObject* spawnPrefabFromAssetPath( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent = nullptr );
} // namespace sw::editor
