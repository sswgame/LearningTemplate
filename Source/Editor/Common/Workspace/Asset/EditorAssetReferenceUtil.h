/**
 * @file EditorAssetReferenceUtil.h
 * @brief 에셋을 옮긴 뒤 메모리의 씬이 든 에셋 참조(컴포넌트의 에셋 경로 칸 · 엔티티의 프리팹 · 중첩 프리팹 부품)를 새 id 로 바꿔 끼웁니다(감사 P0-4).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    class Component;
    class Scene;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct EditorAssetReferenceUtil
     * @brief 옮김 맵(옛 id → 새 id)으로 메모리의 참조를 고칩니다. 언리얼 "Fix Up Redirectors" 가 로드된 참조자를 고치는 자리다.
     * @details 씬을 다시 읽지 않는다 — 되돌리기 · 선택 · 플레이 상태가 그대로다. 디스크의 씬 글은 `applyAssetMoves` 가 같은 묶음으로 이미 고쳤으므로
     *          메모리를 디스크에 맞출 뿐이라 씬을 dirty 로 만들지 않는다(언리얼은 고친 참조자를 dirty 로 표시해 저장을 요구한다 — 거기서는 디스크를 아직 고치지 않았다).
     *          예전에는 깨끗한 활성 씬이 옛 id 를 든 채 남아, 뒤의 편집 · 저장이 고친 참조를 옛 id 로 다시 썼다.
     */
    struct SW_EDITOR_API EditorAssetReferenceUtil
    {
        /**
         * @brief @p component 의 에셋 경로 칸(`PROPERTY( AssetPath )` · `AssetType = …` · `AssetRef<T>` 와 그 시퀀스, 상속 칸 포함)이 @p mapOldToNew 의 옛 id 면 새 id 로 바꿉니다.
         * @details 바꾼 칸 이름을 @p outListChanged 끝에 더한다 — 부른 쪽이 `onPropertyChanged` 로 알린다(자원을 다시 잡는다). 이 함수는 알리지 않는다.
         * @return 바꾼 값 수입니다(시퀀스는 원소마다).
         */
        static uint32 remapComponent( Component& component, const unordered_map<string, string>& mapOldToNew, vector<hashed_string>& outListChanged );
        /** @brief @p scene 의 모든 오브젝트 컴포넌트(`remapComponent` + `onPropertyChanged`), 엔티티의 프리팹 경로, 중첩 프리팹 부품의 프리팹 경로를 고칩니다. 바꾼 참조 수입니다. */
        static uint32 remapScene( Scene& scene, const unordered_map<string, string>& mapOldToNew );
    };
} // namespace sw::editor
