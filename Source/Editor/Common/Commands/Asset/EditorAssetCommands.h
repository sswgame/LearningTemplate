/**
 * @file EditorAssetCommands.h
 * @brief 애셋 열기와 프리팹 · 스프라이트 스폰 커맨드를 제공합니다.
 * @details 씬 문서 흐름은 `EditorSceneFileCommands`, 프리팹 격리는 `EditorPrefabIsolation`(프리팹 창 `PrefabEditorPanel` 이 가진다), 디스크의 파일 작업은
 *          `EditorAssetFileCommands`, 폴더 목록은 `EditorFolderListing` 이 맡습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    struct float3;

    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorAssetCommands
     * @brief Content Browser, 뷰포트 드롭, 메뉴가 함께 쓰는 애셋 열기 · 스폰 동작입니다.
     */
    class EditorAssetCommands
    {
    public:
        /** @brief 등록된 애셋 에디터를 열거나 씬과 머티리얼을 처리합니다. */
        [[nodiscard]] static bool openPath( string_view relativePath );
        /** @brief 포커스 애셋 경로만 바꿉니다. */
        static void focusPath( string_view relativePath );
        /** @brief 프리팹을 스폰하고 Undo 와 선택을 기록합니다. */
        static GameObject* spawnPrefab( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent,
                                        const utf8* pUndoLabel = "Spawn Prefab" );
        /**
         * @brief 프리팹을 스폰하고 워크스페이스에 프리팹 경로를 적습니다. Undo · 선택은 기록하지 않습니다.
         * @details 부모가 있으면 그 아래에 스폰합니다. 실패하면 로그를 남기고 nullptr 을 반환합니다. 프리팹 격리가 루트를 임시로 만들 때 씁니다.
         */
        static GameObject* spawnPrefabWithoutUndo( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent );
        /** @brief 텍스처 경로로 스프라이트 오브젝트를 만듭니다. */
        static GameObject* spawnSprite( GameObjectManager* pManager, const utf8* pPath, const float3& worldPos );
        /** @brief 메시(`.mesh`) 경로로 그 메시를 그리는 오브젝트를 만들고 되돌리기 · 선택을 기록합니다. 이름은 파일 이름(확장자 뺌)입니다. */
        static GameObject* spawnMesh( GameObjectManager* pManager, const utf8* pPath, const float3& worldPos );
        /** @brief 뷰포트에 드롭한 것을 스폰하거나 로드합니다. 히트 위치는 호출하는 쪽이 구합니다. */
        static void dropAt( GameObjectManager* pManager, const utf8* pPath, const float3& spawnPos );
    };
} // namespace sw::editor
