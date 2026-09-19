/**
 * @file D3D12RHIResourceRecipe.h
 * @brief 이름 붙인 D3D12 리소스 조리법 — "버퍼 하나를 어떻게 만드는가" 를 한 곳에서 정합니다.
 *
 * [왜 조리법인가]
 * D3D12 에서 버퍼를 만들려면 `D3D12_RESOURCE_DESC` 의 일곱 필드를 **버퍼용 고정값**으로 채워야
 * 한다(`Dimension=BUFFER` · `Height=1` · `DepthOrArraySize=1` · `MipLevels=1` · `Format=UNKNOWN` ·
 * `SampleDesc.Count=1` · `Layout=ROW_MAJOR`). 실제로 다른 것은 **크기와 플래그뿐**인데, 이 일곱 줄이
 * 상수 버퍼 · 구조버퍼 · 업로드 스테이징 · 리드백 · 정점 버퍼 · 전체화면 정점 버퍼 **여섯 곳에**
 * 복사돼 있었다. (그중 한 곳에는 `heapProps.Type` 대입이 **두 번** 들어 있었다 — 복붙의 지문이다.)
 *
 * 이것은 [[VulkanRHISamplerRecipe]] 와 같은 생각이다 — 객체를 공유하는 것이 아니라 **값을 만드는
 * 방법에 이름을 붙인다.** 힙 종류(업로드/기본/리드백)와 초기 상태는 부르는 쪽이 정한다. 그것이
 * 자리마다 정말로 다른 결정이기 때문이다.
 *
 * @note 크기를 `uint64` 로 받는다. 예전 한 곳은 `elementSize * elementCount` 를 **32비트로 곱해**
 *       `Width`(UINT64)에 넣고 있었다 — 넘치면 조용히 작은 버퍼가 된다.
 */
#pragma once
#include "Core/Common/Types.h"

#include <d3d12.h>

namespace sw
{
    /**
     * @struct D3D12RHIResourceRecipe
     * @brief 자주 쓰는 D3D12 리소스 설정을 이름으로 돌려줍니다.
     */
    struct D3D12RHIResourceRecipe
    {
        /**
         * @brief 버퍼 리소스 디스크립터입니다.
         * @param sizeBytes 버퍼 크기(바이트).
         * @param flags     UAV 를 쓸 버퍼면 `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS`.
         */
        static D3D12_RESOURCE_DESC bufferDesc( uint64 sizeBytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE )
        {
            D3D12_RESOURCE_DESC resDesc{};
            resDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            resDesc.Width            = sizeBytes;
            resDesc.Height           = 1;
            resDesc.DepthOrArraySize = 1;
            resDesc.MipLevels        = 1;
            resDesc.Format           = DXGI_FORMAT_UNKNOWN;
            resDesc.SampleDesc.Count = 1;
            resDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resDesc.Flags            = flags;
            return resDesc;
        }

        /** @brief 힙 속성입니다 — 버퍼 만들기에서 자리마다 다른 것은 이 종류와 초기 상태뿐입니다. */
        static D3D12_HEAP_PROPERTIES heapProperties( D3D12_HEAP_TYPE type )
        {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = type;
            return heapProps;
        }
    };
} // namespace sw
