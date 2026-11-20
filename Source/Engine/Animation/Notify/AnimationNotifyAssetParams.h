/**
 * @file AnimationNotifyAssetParams.h
 * @brief 알림 처리기가 손으로 읽는 에셋 인자의 저장 꼴 선언입니다(`<Notify handler="SpawnPrefab" prefab="…">`).
 * @details 알림 인자는 처리기마다 이름 · 값 짝으로 읽혀(`AnimationNotifyEntry::getTextParam`) 리플렉션 칸이 아니다. 그래서 에셋 인자는 이름과 꼴을 이 구조체로 선언하고
 *          처리기가 이 구조체의 칸으로 값을 받는다 — 선언이 리플렉션 생성 표(`AssetReferenceFields.generated.json`)에 오르므로 GUID 이전 도구 · 참조 검사가 이 인자를
 *          같은 이름의 다른 칸과 함께 본다(`Scripts/dev/migrate_asset_refs.py` 머리말 "리플렉션 밖에서 손으로 읽는 칸"). 씬 엔티티 칸(`SceneEntityParams`)과 같은 길이다.
 *          에셋을 가리키는 인자를 더하면 여기에 칸을 더하고 처리기가 그 칸을 쓴다.
 */
#pragma once
#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief `SpawnPrefab` 처리기의 에셋 인자입니다. */
    REFLECT()
    struct SW_API AnimationNotifySpawnPrefabParams
    {
        REFLECT_BODY();

        /** @brief 소켓 자리에 스폰할 프리팹입니다 — 저장 꼴은 GUID 참조(`AssetRefBase`). */
        PROPERTY( Required, Tooltip = "Prefab spawned at the socket" )
        AssetRef<PrefabAsset> _prefab{};
    };
} // namespace sw
