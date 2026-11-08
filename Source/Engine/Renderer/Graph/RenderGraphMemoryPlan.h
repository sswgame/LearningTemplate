/**
 * @file RenderGraphMemoryPlan.h
 * @brief 서술이 다른 트랜지언트 첨부를 힙 하나에 겹쳐 놓는 자리(바이트 오프셋)를 정합니다 — 수명(레벨 구간)이 겹치는 것끼리만 바이트가 겹치지 않는다. CPU 만 쓴다.
 * @details 언리얼 RDG 트랜지언트 할당기(`FRDGTransientResourceAllocator` — 힙 위 placed 자원, 수명이 끝난 메모리를 다음 자원이 받는다)의 자리다.
 *          같은 서술끼리 텍스처 하나를 넘기는 `RenderGraphAliasPlan` 위의 층이다 — 그 계획의 주인(과 별칭을 합친 수명)을 받아 크기 · 정렬로 힙 오프셋을 준다.
 *          배치는 큰 것부터(같으면 앞 레벨 · 목록 차례) 수명이 겹치는 이미 놓은 항목들의 바이트 구간을 피해 가장 낮은 정렬된 오프셋에 놓는다(구간 그래프 색칠의 탐욕).
 *          결과는 입력 순서에 따라 결정적이다. 실제 힙 · placed 텍스처는 백엔드가 만든다(`IRHIResourceFactory::createPlacedTexture2D`, 능력 `_bPlacedTextures`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    /** @brief 힙에 놓을 항목 하나입니다(크기 · 정렬은 백엔드의 `getTextureAllocationInfo`, 수명은 레벨 구간). */
    struct RenderGraphMemoryItem
    {
        hashed_string _name;
        uint64        _sizeBytes{ 0 };
        uint64        _alignment{ 1 }; ///< 2 의 거듭제곱
        uint32        _firstLevel{ 0 };
        uint32        _lastLevel{ 0 };
    };

    /**
     * @struct RenderGraphMemoryPlan
     * @brief 배치 결과입니다 — 항목마다 오프셋(입력과 같은 차례)과 힙 크기.
     */
    struct SW_API RenderGraphMemoryPlan
    {
        /** @brief @p listItem 을 놓습니다. 크기 0 인 항목은 오프셋 0 · 크기 없이 둔다(부르는 쪽이 놓지 않는다). */
        static RenderGraphMemoryPlan build( const vector<RenderGraphMemoryItem>& listItem );
        /** @brief 두 항목의 수명(레벨 구간, 양끝 포함)이 겹치면 true 입니다 — 같은 레벨은 GPU 에서 겹칠 수 있다. */
        static bool overlapsInTime( const RenderGraphMemoryItem& left, const RenderGraphMemoryItem& right );

        vector<uint64> _listOffset; ///< `listItem` 과 같은 차례의 바이트 오프셋
        uint64         _heapBytes{ 0 };
        uint64         _sumBytes{ 0 }; ///< 겹치지 않고 놓았다면의 합(절약을 재는 값)
    };
} // namespace sw
