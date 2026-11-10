/**
 * @file MaterialRenderBinding.h
 * @brief 스냅샷이 배치 · 원소마다 싣는 (프록시, 불변 사본) 쌍입니다. 두 형식을 앞선 선언으로만 알아, 게임이 보는 `Material.h` · `MaterialInstance.h` 가
 *        렌더 쪽 헤더(`MaterialRenderProxy.h` — 상수 버퍼 · 셰이더 칸)를 include 하지 않는다(언리얼 `UMaterial` ↔ `FMaterialRenderProxy`).
 */
#pragma once
#include "Core/Memory/Memory.h"

namespace sw
{
    class MaterialRenderProxy;
    struct MaterialRenderData;

    /**
     * @struct MaterialRenderBinding
     * @brief 스냅샷이 배치 · 원소마다 싣는 (프록시, 사본) 쌍입니다. 둘 다 소유권을 함께 싣는다(패킷이 사는 동안 살아 있다).
     * @details 프록시 · 사본의 몸체는 `MaterialRenderProxy.h` 다 — 읽는 쪽(렌더러)이 include 한다.
     */
    struct MaterialRenderBinding
    {
        shared_ptr<MaterialRenderProxy>      _proxy;
        shared_ptr<const MaterialRenderData> _data;

        /** @brief 프록시와 사본이 모두 있으면 true 입니다. */
        bool isValid() const { return _proxy != nullptr && _data != nullptr; }
    };
} // namespace sw
