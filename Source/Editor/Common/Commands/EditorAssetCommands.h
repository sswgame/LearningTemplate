/**
 * @file EditorAssetCommands.h
 * @brief 애셋 열기 / 씬 로드 / 프리팹·스프라이트 스폰 커맨드
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
	class GameObject;
	class GameObjectManager;
} // namespace sw

namespace sw::editor
{
	/** @brief Resource 폴더 스캔으로 만든 퀵 런처용 파일 항목 */
	struct EditorResourceIndexEntry
	{
		string _category;
		string _title;
		string _detail;
		string _path;
	};

	/**
	 * @class EditorAssetCommands
	 * @brief Content Browser, 뷰포트 드롭, 메뉴가 공유하는 애셋 도메인 동작입니다.
	 */
	class EditorAssetCommands
	{
	public:
		/** @brief 등록된 애셋 에디터를 열거나 씬/머티리얼을 처리합니다. */
		static bool openPath( string_view relativePath );
		/** @brief 씬을 비동기 로드하고 선택을 지웁니다. */
		static bool loadScene( string_view path );
		/** @brief 포커스 애셋 경로만 바꿉니다. */
		static void focusPath( string_view relativePath );
		/** @brief 프리팹을 스폰하고 Undo/선택을 기록합니다. */
		static GameObject* spawnPrefab( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent,
										const utf8* pUndoLabel = "Spawn Prefab" );
		/** @brief 텍스처 경로로 스프라이트 오브젝트를 만듭니다. */
		static GameObject* spawnSprite( GameObjectManager* pManager, const utf8* pPath, const float3& worldPos );
		/** @brief 뷰포트 드롭: 히트 위치는 호출 측, 스폰/로드만 수행합니다. */
		static void dropAt( GameObjectManager* pManager, const utf8* pPath, const float3& spawnPos );
		/** @brief Resource 트리에서 씬/프리팹/텍스처/셰이더/데이터를 분류해 채웁니다. */
		static void collectResourceIndex( vector<EditorResourceIndexEntry>& outList );
	};
} // namespace sw::editor
