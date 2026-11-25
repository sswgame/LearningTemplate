/**
 * @file ConstantBufferPagePool.h
 * @brief 작은 상수 버퍼(머티리얼 인스턴스)를 디바이스의 64 KiB 페이지 구간에 모읍니다(감사 R-4).
 * @details 등급 칸 고르기는 `ConstantBufferSlotAllocator`, 이 클래스는 페이지 번호마다 RHI 버퍼(`createConstantBufferPage`)와 구간마다 디스크립터
 *          (`registerBindlessBufferSRV( RHIBufferSlice )`)를 갖는다. 비행 중 프레임 복제는 페이지가 갖는다(백엔드가 구간을 그 프레임 칸에 쓴다).
 *          가장 큰 등급(4096 B)보다 큰 요청은 전용 버퍼 하나다(칸 없음). 언리얼 `FD3D12PoolAllocator` · 유니티 SRP Batcher 의 영속 버퍼 오프셋 자리다.
 *          디바이스가 하나씩 갖는다(`IRHIDevice::getConstantBufferPagePool`). 렌더 스레드가 쓰고(`write`), 놓기(`ConstantBufferPoolSlice::reset`)는 어느 스레드에서든 한다.
 *          **구간은 받을 때 한 번만 쓴다.** 값이 바뀌면 새 구간을 받아 쓰고 옛 구간은 놓는다 — 놓은 칸 · 디스크립터 · 전용 버퍼는 그것을 읽는 프레임이 모두 끝난 뒤에 다시 쓴다.
 *          같은 구간을 덮어쓰면 이번 프레임이 이미 기록한 드로우(백엔드의 프레임 칸 · 큐 쓰기)와 GPU 가 읽는 중인 프레임(DX11 NO_OVERWRITE)이 새 값을 본다. 언리얼 유니폼 버퍼 갱신도 새 할당이다.
 *          잠금은 잎 잠금이다(결정 5-3) — 할당기 · 페이지 표만 그 안에서 고치고, RHI 호출(만들기 · 등록 · `releaseHandle`)과 로그는 잠금 밖에서 한다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/Backend/RHIRetireList.h"
#include "Engine/Graphics/RHI/ConstantBufferPoolSlice.h"
#include "Engine/Graphics/RHI/ConstantBufferSlotAllocator.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class ConstantBufferPagePool
     * @brief 페이지 RHI 버퍼와 등급 칸 할당기입니다. 파일 머리말 참고.
     */
    class SW_API ConstantBufferPagePool
    {
    public:
        /** @brief 놓은 구간을 다시 쓰기까지 지나야 하는 경계 수입니다 — 놓은 프레임을 GPU 가 끝내고 그 프레임 칸이 돌아온 뒤다. */
        static constexpr uint64 kReuseDelayCount = constant::render::kMaxFrameCountInFlight;

        /** @brief @p handleReuseCount 는 장치가 프레임을 열 때(1) · GPU 를 통째로 기다린 뒤 오르는 수이고(`IRHIDevice::getHandleReuseCount`) 풀보다 오래 산다 — 칸 다시 쓰기를 프레임에서 떼어 낼 수 없게 받아야 만든다. */
        explicit ConstantBufferPagePool( const atomic<uint64>& handleReuseCount )
            : _handleReuseCount{ handleReuseCount }
        {
        }
        ConstantBufferPagePool( const ConstantBufferPagePool& )            = delete;
        ConstantBufferPagePool& operator=( const ConstantBufferPagePool& ) = delete;

        /**
         * @brief (렌더 스레드) 새 구간을 받아 @p size 바이트를 쓰고 @p inoutSlice 를 그 구간으로 바꿉니다. 옛 구간은 놓는다(다른 디바이스의 구간이면 그 디바이스에).
         * @details bindless 인덱스가 바뀐다 — 부르는 쪽은 매번 새 값을 읽는다.
         * @return 널 데이터 · 크기 0 이거나 페이지 · 디스크립터를 만들지 못하면 오류를 남기고 false 이며 @p inoutSlice 는 그대로입니다.
         */
        [[nodiscard]] bool write( IRHIDevice& device, const void* pData, uint32 size, ConstantBufferPoolSlice& inoutSlice ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief (디바이스 종료) 남은 구간과 페이지 버퍼를 모두 돌려주고 할당기를 비웁니다. 구간을 든 쪽은 이미 `releaseRHI` 로 놓았다. */
        void releaseAllPages( IRHIDevice& device );

        /** @brief 열린 페이지 수입니다(백엔드 할당 수 — 측정 `RT.RHI.constantBufferPages`). */
        uint32 getOpenPageCount() const;

    private:
        friend class ConstantBufferPoolSlice;

        /** @brief 놓았지만 아직 다시 쓰지 않는 구간입니다. 칸이 무효면 전용 버퍼다. */
        struct RetiredSlice
        {
            ConstantBufferSlot _slot;
            RHIBufferSlice     _range;
            RHIBufferSRVIndex  _descriptorIndex{ kInvalidDescriptorIndex };
        };

        /** @brief 구간을 놓은 줄에 넣습니다(어느 스레드든). 구간 값은 부르는 쪽이 비운다. */
        void retireSlice( const ConstantBufferPoolSlice& slice ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 놓은 지 `kReuseDelayCount` 경계가 지난 구간의 칸을 할당기에 돌려주고 디스크립터 · 전용 버퍼 · 닫힌 페이지를 놓습니다. */
        void releaseReadySlices( IRHIDevice& device ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 구간들의 디스크립터 · 전용 버퍼와 페이지 버퍼를 놓습니다(잠금 밖). */
        static void releaseRHI( IRHIDevice& device, const vector<RetiredSlice>& listRetired, const vector<RHIBufferHandle>& listPage );
        /** @brief 칸 @p slot 의 구간을 엽니다 — 새 페이지면 RHI 버퍼를 만들고, 구간 디스크립터를 등록한다. 실패하면 칸을 할당기에 돌려준다. */
        [[nodiscard]] bool openSlice( IRHIDevice& device, const ConstantBufferSlot& slot, bool bNewPage, ConstantBufferPoolSlice& outSlice ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 전용 버퍼(등급 밖)를 만들고 디스크립터를 등록합니다. */
        [[nodiscard]] static bool openDedicated( IRHIDevice& device, uint32 sizeBytes, ConstantBufferPoolSlice& outSlice );
        /** @brief 쓰지 않은 칸(열기 실패)을 할당기에 곧바로 돌려주고, 페이지가 닫히면 그 RHI 버퍼를 놓습니다. */
        void releaseSlot( IRHIDevice& device, const ConstantBufferSlot& slot ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 칸을 할당기에 돌려주고, 페이지가 닫혔으면 그 번호의 RHI 버퍼를 표에서 떼어 돌려줍니다(아니면 0). 잠금 안에서 부른다. */
        RHIBufferHandle releaseSlotLocked( const ConstantBufferSlot& slot ) SW_REQUIRES_LOCK( _mutex );

    private:
        ConstantBufferSlotAllocator _allocator   SW_GUARDED_BY( _mutex );
        RHIRetireList<RetiredSlice> _listRetired SW_GUARDED_BY( _mutex ); ///< 놓은 구간(다시 쓸 경계 수가 키)
        vector<RHIBufferHandle> _listPageBuffer  SW_GUARDED_BY( _mutex ); ///< 페이지 번호마다 RHI 버퍼(닫힌 번호는 0)
        const atomic<uint64>&                    _handleReuseCount;
        mutable mutex                            _mutex;
    };
} // namespace sw
