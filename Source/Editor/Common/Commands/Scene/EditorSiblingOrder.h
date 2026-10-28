/**
 * @file EditorSiblingOrder.h
 * @brief 계층 창의 형제 순서를 다룹니다. 같은 부모 아래 오브젝트의 표시 순서를 정하고, 끌어 놓은 위치로 옮기고, 폴더(정리용 빈 오브젝트)를 만듭니다.
 * @details ImGui 를 쓰지 않으므로 EditorTest 가 테스트한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 계층 창의 행 위에 끌어 놓은 위치입니다. */
    enum class SiblingDropZone : uint8
    {
        Before = 0, ///< 행의 위쪽 가장자리. 그 오브젝트 앞의 형제로 옮긴다
        Onto,       ///< 행의 가운데. 그 오브젝트의 자식으로 옮긴다
        After       ///< 행의 아래쪽 가장자리. 그 오브젝트 뒤의 형제로 옮긴다
    };

    /**
     * @struct EditorSiblingOrder
     * @brief 형제 순서(`GameObject::getSiblingIndex`)를 읽고 고치는 동작입니다. 유니티의 `Transform.SetSiblingIndex` 와 하이어라키 끌기, 언리얼 아웃라이너 폴더에 해당합니다.
     * @details 순서는 (형제 번호, 오브젝트 id) 입니다. 번호가 모두 0 인 옛 씬은 오브젝트 id 순, 곧 파일 순서 그대로입니다. 끌어 놓으면 형제 전체를 0 부터 다시 매깁니다.
     *          바뀐 오브젝트마다 되돌리기 기록이 있고 한 단계로 묶입니다. 순서는 계층 창이 그리는 순서이고 틱이나 렌더 순서와는 무관합니다.
     *          폴더는 `Editor.Folder` 태그가 붙은 빈 오브젝트입니다. 트랜스폼이 있어 자식이 따라 움직이며, 유니티의 빈 오브젝트와 같습니다.
     */
    struct EditorSiblingOrder
    {
        /** @brief 행 위쪽 몇 분의 일이 Before, 아래쪽 몇 분의 일이 After 인지입니다. 가운데는 Onto 입니다. */
        static constexpr float32 kEdgeFraction = 0.3f;
        /** @brief 폴더 오브젝트에 붙는 태그입니다. */
        static constexpr const utf8* kFolderTagName = "Editor.Folder";

        /** @brief 커서 높이 @p cursorY 가 행 [@p rowMinY, @p rowMaxY] 의 어느 위치인지 정합니다. 행 높이가 0 이면 Onto 입니다. */
        static SiblingDropZone computeDropZone( float32 cursorY, float32 rowMinY, float32 rowMaxY );

        /** @brief 표시 순서에서 @p pLeft 가 @p pRight 보다 앞이면 true 입니다(형제 번호, 같으면 오브젝트 id). */
        static bool isBefore( const GameObject* pLeft, const GameObject* pRight );
        /** @brief 형제 목록을 표시 순서로 정렬합니다(안정 정렬). */
        static void sortSiblings( vector<GameObject*>& inoutListSibling );
        /** @brief @p parent 의 자식을 표시 순서로 모읍니다(먼저 비운다). */
        static void collectChildrenInOrder( const GameObject& parent, vector<GameObject*>& outListChild );

        /**
         * @brief @p pDragged 를 @p pTarget 의 앞(@p bAfter 가 false) 또는 뒤 형제로 옮깁니다. 부모가 다르면 먼저 재부모(월드 위치 유지)합니다.
         * @param pSkip 형제 목록에서 뺄 오브젝트입니다(계층 창이 보이지 않는 에디터 카메라). nullptr 이면 빼지 않는다.
         * @return 옮겼으면 true 입니다. 같은 오브젝트거나 순환이 생기거나 편집이 막혔으면(Play) false 이고 아무것도 바꾸지 않는다.
         * @details 되돌리기는 한 단계(`Reorder GameObject`)입니다. 형제의 번호는 0 부터 형제 수만큼 다시 매기며 65535 로 막힌다.
         */
        [[nodiscard]] static bool moveBeside( GameObjectManager& manager, GameObject* pDragged, GameObject* pTarget, bool bAfter, const GameObject* pSkip = nullptr );
        /** @brief @p pObject 를 형제 사이에서 한 단계 위(@p bDown 이 false) 또는 아래로 옮깁니다. 이미 끝이면 false 입니다. */
        [[nodiscard]] static bool moveStep( GameObjectManager& manager, GameObject* pObject, bool bDown, const GameObject* pSkip = nullptr );

        /** @brief 폴더 오브젝트이면 true 입니다(`kFolderTagName` 태그). */
        static bool isFolder( const GameObject& object );
        /**
         * @brief 정리용 폴더를 @p pParent 아래(nullptr 이면 루트)에 만들고 선택합니다. 되돌리기는 한 단계(`Create Folder`)입니다.
         * @return 만든 폴더입니다. 편집이 막혔으면 nullptr 입니다.
         */
        static GameObject* createFolder( GameObjectManager& manager, GameObject* pParent );
    };
} // namespace sw::editor
