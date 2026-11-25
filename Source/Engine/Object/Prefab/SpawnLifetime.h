/**
 * @file SpawnLifetime.h
 * @brief 스폰한 오브젝트가 상태인지, 세운 쪽이 다시 세우는 모습인지입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @enum SpawnLifetime
     * @brief 스폰할 때 반드시 정합니다(`PrefabCache::spawn` · `PrefabPool`) — 기본값이 없어, 모습이 세이브 · 스트리밍 셀 · 전환 · 상태 해시에 섞이는 것을 잊을 수 없다.
     * @details 언리얼 `FActorSpawnParameters::ObjectFlags` 의 `RF_Transient`, 유니티 `HideFlags.DontSave` 자리다.
     */
    enum class SpawnLifetime : uint8
    {
        Persistent, ///< 상태다 — 세이브 · 셀 · 전환 · 모듈 리로드 · 상태 해시에 든다
        Derived     ///< 세운 쪽이 상태에서 다시 세우는 모습이다 — 계층째 거기서 빠진다(`GameObject::setTransient`)
    };
} // namespace sw
