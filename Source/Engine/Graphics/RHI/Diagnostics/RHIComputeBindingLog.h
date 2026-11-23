/**
 * @file RHIComputeBindingLog.h
 * @brief 컴퓨트 디스패치가 건 버퍼 번호 → (PSO, 슬롯)의 기록입니다 — 버퍼 보기가 그 버퍼를 읽는 셰이더의 원소 형식을 찾는 데 쓴다(개발 구성만 기록한다).
 * @details 그래프 밖 버퍼는 이름과 원소 크기만 등록된다 — 원소 안의 멤버는 그 버퍼를 읽고 쓰는 컴퓨트 셰이더의 리플렉션에만 있고, 어느 셰이더의 어느 슬롯인지는 걸 때만 안다.
 *          이름을 셰이더 변수 이름과 맞추면(관례) 한쪽만 바뀌었을 때 틀린 형식을 조용히 보인다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Expected.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    /**
     * @class RHIComputeBindingLog
     * @brief 켜 둔 동안만 기록합니다(`setEnabled` — 버퍼 보기가 볼 때). 기록은 병렬 기록 스레드, 조회는 렌더 스레드입니다.
     */
    class SW_API RHIComputeBindingLog
    {
    public:
        /** @brief 버퍼를 건 자리입니다. */
        struct Binding
        {
            RHIPipelineStateHandle _pso{ 0 };
            uint32                 _slot{ 0 };
        };

        /** @brief 기록을 켜고 끕니다. 끄면 기록을 잊습니다. */
        void setEnabled( bool bEnabled );
        bool isEnabled() const { return _bEnabled.load( std::memory_order_relaxed ) != SW_FALSE; }
        /** @brief 디스패치 하나가 번호 @p index(@p bUAV 면 UAV 공간)를 @p pso 의 @p slot 에 건 것을 적습니다(마지막 것이 남는다). */
        void record( RHIDescriptorIndex index, bool bUAV, RHIPipelineStateHandle pso, uint32 slot );
        /** @brief 번호를 마지막으로 건 자리입니다. 기록이 없으면 비어 있습니다. */
        [[nodiscard]] optional<Binding> find( RHIDescriptorIndex index, bool bUAV ) const;

    private:
        /** @brief 번호와 공간을 접은 키입니다. */
        static uint64 makeKey( RHIDescriptorIndex index, bool bUAV ) { return ( static_cast<uint64>( index ) << 1u ) | ( bUAV ? 1u : 0u ); }

    private:
        unordered_map<uint64, Binding> _mapBinding SW_GUARDED_BY( _mutex );
        mutable mutex                              _mutex;
        atomic<uint8>                              _bEnabled{ SW_FALSE };
    };
} // namespace sw
