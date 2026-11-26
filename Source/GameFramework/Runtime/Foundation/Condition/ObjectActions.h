/**
 * @file ObjectActions.h
 * @brief 오브젝트 동작 어휘의 칸입니다 — `object.destroy` · `object.replace` · `object.spawn` · `object.teleport`.
 * @details 대상은 실행기가 채운 `ActionTargetSlot` 이다. 지역 효과의 물체 대상 할 일(불붙은 상자가 재로 바뀐다 · 늪에 빠진 열쇠가 돌아온다 · 포털)이 이 이름을 부르고,
 *          대화 · 행동 트리도 대상을 채우면 같은 이름을 쓴다. 언리얼 Blueprint 의 DestroyActor · SpawnActor · SetActorLocation(Teleport) 의 자리다(기억).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief `object.replace` · `object.spawn` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API ObjectPrefabParams
    {
        REFLECT_BODY();

        /** @brief 스폰할 프리팹입니다 — 저장 꼴은 GUID 참조(`AssetRefBase`). 글 칸이었을 때는 같은 이름의 참조 칸(`prefab`)과 GUID 이전 도구가 가르지 못했다. */
        PROPERTY( Required, Tooltip = "Prefab spawned at the target's position (*.prefab.xml)" )
        AssetRef<PrefabAsset> _prefab{};
    };

    /** @brief `object.teleport` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API ObjectTeleportParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Object name whose position the target moves to (checkpoint, portal exit, key home)" )
        hashed_string _to{};
    };
} // namespace sw
