/**
 * @file EditorInspectorCommands.h
 * @brief 인스펙터 프로퍼티 Undo / 프리팹 적용·복원 커맨드
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/FileSystem/AssetID.h"

namespace sw
{
    class GameObject;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorInspectorCommands
     * @brief 프리팹을 적용 · 복원 · 연결 해제합니다. 프로퍼티 편집의 Undo 는 오브젝트 스냅샷(`EditorTransaction::recordModify`)이 맡습니다.
     */
    class EditorInspectorCommands
    {
    public:
        /** @brief `adoptChildPrefabInstances` 의 결과입니다. `_rejectReason` 이 비어 있지 않으면 거절해서 아무것도 옮기지 않았습니다. */
        struct AdoptResult
        {
            uint32 _adoptedCount{ 0 };
            string _rejectReason;

            bool isRejected() const { return _rejectReason.empty() == false; }
        };

        /** @brief 선택 오브젝트 상태를 프리팹 XML로 저장합니다. */
        [[nodiscard]] static bool applyToPrefab( GameObject* pObj, const AssetID& assetID );
        /** @brief 프리팹 상태로 되돌리고 Undo에 기록합니다. */
        [[nodiscard]] static bool revertToPrefab( GameObject* pObj, string_view prefabPath );
        /**
         * @brief 중첩 부품 @p pPart 를 스폰한 바깥 인스턴스를 그 프리팹에 적용합니다(유니티 "Apply as Override in Prefab '바깥'").
         * @details 부품의 수정은 바깥 프리팹의 노드 오버라이드가 되고, 부품 아래 부품의 수정은 노드의 `<Part>` 가 된다(`PrefabAsset::setFromGameObject`).
         *          @p pPart 가 붙은 부품이 아니면 false 입니다.
         */
        [[nodiscard]] static bool applyToOuterPrefab( GameObject* pPart );
        /**
         * @brief 중첩 부품 @p pPart 를 스폰한 바깥 인스턴스입니다. 붙은 부품이 아니거나 바깥 인스턴스가 없으면 nullptr 입니다.
         * @details 인스펙터가 "바깥 프리팹" 줄과 Apply 대상을 고를 때 쓴다.
         */
        static GameObject* findOuterInstance( const GameObject* pPart );
        /**
         * @brief 오브젝트와 프리팹 연결을 끊습니다.
         * @details 중첩 부품이면 그 부품만 끊을 수 없다(바깥 인스턴스가 다시 스폰해 두 벌이 된다). 대신 맨 바깥에 놓인 인스턴스를 한 겹 푼다 —
         *          그 바로 아래 부품은 저마다 자기 프리팹의 인스턴스로 씬에 남는다(유니티 Unpack Prefab). 어느 인스턴스를 풀었는지 로그로 알린다.
         *          끊었으면 true 입니다. 플레이 중이라 편집이 막혔거나 풀 인스턴스가 없으면 이유를 경고 로그로 남기고 false 입니다.
         */
        [[nodiscard]] static bool unlinkPrefab( GameObject* pObj );
        /**
         * @brief @p pRoot 아래의 프리팹 인스턴스 자식을 @p pRoot 의 중첩 프리팹 부품(`PrefabInstanceRegistry`)으로 옮기고 옮긴 수를 반환합니다.
         * @details 새 프리팹을 만들기 직전에 부른다 — 프리팹 본문은 루트 오브젝트 하나와 중첩 인스턴스 노드뿐이라, 옮기지 않은 자식 인스턴스는
         *          프리팹에 실리지 않는다(유니티: 프리팹 인스턴스를 자식으로 둔 계층으로 프리팹을 만들면 중첩 프리팹이 된다). 옮긴 자식은 씬이
         *          엔티티로 저장하지 않고 루트 인스턴스가 다시 스폰하므로 작업 공간의 인스턴스 경로를 지운다. 루트의 바로 아래와, 옮긴 자식 아래로만
         *          내려간다(부품의 부모는 루트나 다른 부품이어야 한다). 되돌리기는 남기지 않는다 — 기존 프리팹에 쓰는 `applyToPrefab` 은 부르지 않는다.
         *          옮길 오브젝트를 올라와 있는 씬의 `SceneObjectReference` 가 가리키면 아무것도 옮기지 않고 거절한다(`AdoptResult::_rejectReason` 에 어느 참조인지 적고 로그를 남긴다) —
         *          옮기면 그 파일 id 가 다음 로드에 사라져 참조가 끊긴다. 참조를 먼저 지우거나 다른 오브젝트로 바꾼 뒤 다시 만든다.
         */
        [[nodiscard]] static AdoptResult adoptChildPrefabInstances( GameObject* pRoot );
    };
} // namespace sw::editor
