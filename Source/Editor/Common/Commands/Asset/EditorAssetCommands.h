/**
 * @file EditorAssetCommands.h
 * @brief 애셋 열기와 프리팹 · 스프라이트 스폰 커맨드를 제공합니다.
 * @details 씬 문서 흐름은 `EditorSceneFileCommands`, 프리팹 격리는 `EditorPrefabIsolation`(프리팹 창 `PrefabEditorPanel` 이 가진다), 디스크의 파일 작업은
 *          `EditorAssetFileCommands`, 폴더 목록은 `EditorFolderListing` 이 맡습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    struct EditorPlacementPose;

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
        static GameObject* spawnSprite( GameObjectManager* pManager, const utf8* pPath, const EditorPlacementPose& pose );
        /** @brief 메시(`.mesh`) 경로로 그 메시를 그리는 오브젝트를 만들고 되돌리기 · 선택을 기록합니다. 이름은 파일 이름(확장자 뺌)입니다. */
        static GameObject* spawnMesh( GameObjectManager* pManager, const utf8* pPath, const EditorPlacementPose& pose );
        /**
         * @brief 프리팹을 @p pose(자리 · 회전)로 스폰하고 되돌리기 · 선택을 기록합니다. 자세를 기록 전에 입혀 다시 실행해도 같은 자리 · 회전으로 돌아온다.
         * @details 예전 뷰포트 드롭은 `spawnPrefab`(생성 기록) 뒤에 위치를 바꿔, 되돌리기 → 다시 실행에서 프리팹이 원래 자리(원점)로 돌아갔다.
         */
        static GameObject* spawnPrefabAt( GameObjectManager* pManager, const utf8* pPath, const EditorPlacementPose& pose );
        /** @brief @p relativePath 의 종류가 씬 뷰에 놓아 만드는 종류면 true 입니다(`IEditorAssetTypeActions::canPlaceInViewport`). false 면 놓을 때 연다. */
        [[nodiscard]] static bool canPlaceInViewport( string_view relativePath );
        /** @brief 놓을 수 있는 종류(`canPlaceInViewport`)를 @p pose 로 만듭니다. 자리는 부른 쪽이 표면에서 구하고, 구하지 못했으면 부르지 않는다. */
        static void placeInViewport( GameObjectManager* pManager, const utf8* pPath, const EditorPlacementPose& pose );
    };
} // namespace sw::editor
