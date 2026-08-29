/**
 * @file EditorAssetCommands.h
 * @brief 애셋 열기 / 씬 로드 / 프리팹·스프라이트 스폰 커맨드
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorSessionPolicy.h"

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

	/** @brief Content Browser 폴더 한 항목 */
	struct EditorFolderListingEntry
	{
		string _name;
		string _relativePath;
		string _absolutePath;
		string _extension;
		bool   _bIsDirectory{ false };
	};

	/** @brief 프로파일러 리소스 카탈로그 개수 */
	struct EditorResourceCatalogCounts
	{
		size_t _sceneCount{ 0 };
		size_t _prefabCount{ 0 };
		size_t _textureCount{ 0 };
		size_t _shaderCount{ 0 };
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
		/** @brief dirty면 확인을 띄우고, 아니면 로드합니다. 플레이 중이면 false입니다. */
		static bool tryOpenScene( string_view path );
		/** @brief dirty면 확인을 띄우고, 아니면 빈 씬으로 바꿉니다. */
		static bool tryCreateNewScene();
		/** @brief 포커스된 더티 도구 문서를 저장하거나 활성 씬을 저장합니다. */
		static void saveFocusedOrScene();
		/** @brief 미저장 모달 선택을 적용합니다. */
		static void applyUnsavedSceneChoice( EditorUnsavedChoice choice );
		/** @brief dirty면 확인을 띄우고, 아니면 종료를 허용합니다. */
		static bool tryBeginQuit();
		/** @brief 창 닫기를 시도합니다. dirty면 확인 모달이 뜹니다. */
		static void requestExit();
		/** @brief 활성 씬 세대가 바뀌면 dirty/프리팹 맵을 동기화합니다. */
		static void syncAfterSceneGenerationChange();
		/** @brief 포커스 애셋 경로만 바꿉니다. */
		static void focusPath( string_view relativePath );
		/** @brief 프리팹을 스폰하고 Undo/선택을 기록합니다. */
		static GameObject* spawnPrefab( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent,
										const utf8* pUndoLabel = "Spawn Prefab" );
		/** @brief 텍스처 경로로 스프라이트 오브젝트를 만듭니다. */
		static GameObject* spawnSprite( GameObjectManager* pManager, const utf8* pPath, const float3& worldPos );
		/** @brief 뷰포트 드롭: 히트 위치는 호출 측, 스폰/로드만 수행합니다. */
		static void dropAt( GameObjectManager* pManager, const utf8* pPath, const float3& spawnPos );
		/** @brief 활성 씬을 XML로 저장합니다. path가 비면 씬 소스 경로를 씁니다. */
		static bool saveActiveScene( string_view path = {} );
		/** @brief 소스 경로가 있으면 저장하고, 없으면 Save As 대화상자를 엽니다. */
		static void saveActiveSceneOrPrompt();
		/** @brief 파일을 Content Browser 폴더로 복사하고 메타를 만듭니다. */
		static uint32 importFiles( string_view destFolderAbs, const vector<string>& listSourcePath );
		/** @brief 애셋 파일과 짝 .meta를 삭제합니다. */
		static bool deleteAsset( string_view absolutePath );
		/** @brief Resource 트리에서 씬/프리팹/텍스처/셰이더/데이터를 분류해 채웁니다. */
		static void collectResourceIndex( vector<EditorResourceIndexEntry>& outList );
		/** @brief 폴더의 직속 하위 폴더/파일을 채웁니다. .meta는 제외합니다. */
		static void collectFolderListing( string_view folderAbs, vector<EditorFolderListingEntry>& outList );
		/** @brief 폴더의 직속 하위 폴더 절대 경로를 채웁니다. */
		static void collectChildFolders( string_view folderAbs, vector<string>& outList );
		/** @brief Resource 아래 씬/프리팹/텍스처/셰이더 파일 개수를 셉니다. */
		static void collectResourceCatalogCounts( EditorResourceCatalogCounts& outCounts );
		/** @brief 프리팹을 독립 씬으로 엽니다. 중첩이면 스택에 쌓습니다. */
		static bool enterPrefabIsolation( string_view prefabPath );
		/** @brief 프리팹 독립 편집을 종료합니다. bSaveToPrefab이면 루트를 템플릿에 씁니다. */
		static bool exitPrefabIsolation( bool bSaveToPrefab );
	};
} // namespace sw::editor
