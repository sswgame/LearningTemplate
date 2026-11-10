/**
 * @file RHIStagingPlacement.h
 * @brief 프레임 칸마다 하나인 구조 버퍼 업로드 스테이징(DX12 업로드 힙 · Vulkan 호스트 버퍼)의 자리 잡기와 성장 정책입니다.
 * @details 두 백엔드가 같은 규칙(정렬한 커서에 이어 쓰고, 모자라면 필요량의 두 배를 64 KiB 로 맞춰 새로 만들며 옛 것은 펜스 뒤 놓는다)을 따로 적었다.
 *          한쪽만 바꾸면 백엔드마다 다시 만드는 빈도가 갈린다. 64 KiB 는 D3D12 버퍼 배치 정렬(`D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT`)이라 두 백엔드에 같이 둔다.
 *          언리얼 D3D12 업로드 할당자의 페이지 배수 성장과 같은 자리다(기억에 의존한 비교).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    /**
     * @struct RHIStagingPlacement
     * @brief 업로드 하나를 놓을 자리와, 스테이징을 키워야 하면 새 용량입니다(`place`).
     */
    struct RHIStagingPlacement
    {
        static constexpr uint64 kGrowthFactor = 2;        ///< 모자라면 필요량의 몇 배로 키우는가(자주 다시 만들지 않게)
        static constexpr uint64 kGranularity  = 65536ull; ///< 새 용량 정렬(D3D12 버퍼 배치 정렬과 같다)

        uint64 _offset{ 0 };      ///< 정렬한 커서(키울 때 옛 버퍼가 있었으면 부르는 쪽이 0 으로 되감는다 — 옛 것은 펜스 뒤 놓는다)
        uint64 _newCapacity{ 0 }; ///< 키워야 하면 새 용량, 아니면 0

        /** @brief 커서 · 정렬 · 크기 · 지금 용량으로 자리를 정합니다. @p bHasBuffer 가 거짓이거나 @p capacity 가 모자라면 새 용량을 정한다. */
        static RHIStagingPlacement place( uint64 cursor, uint64 alignment, uint64 sizeBytes, uint64 capacity, bool bHasBuffer )
        {
            RHIStagingPlacement placement{};
            placement._offset = MathUtil::align( cursor, alignment );
            if ( bHasBuffer && capacity >= placement._offset + sizeBytes )
                return placement;
            // 새 용량은 지금 자리까지 포함한 필요량으로 잡는다(이전 두 백엔드와 같은 계산).
            placement._newCapacity = MathUtil::align( ( placement._offset + sizeBytes ) * kGrowthFactor, kGranularity );
            return placement;
        }
    };
} // namespace sw
