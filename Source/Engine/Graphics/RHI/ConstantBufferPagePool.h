/**
 * @file ConstantBufferPagePool.h
 * @brief 작은 상수 버퍼(머티리얼 인스턴스)를 디바이스의 64 KiB 페이지 구간에 모읍니다(설계: DesignBacklog "자원 구간 할당" ③, 감사 R-4).
 * @details 등급 칸 고르기는 `ConstantBufferSlotAllocator`, 이 클래스는 페이지 번호마다 RHI 버퍼(`createConstantBufferPage`)와 구간마다 디스크립터
 *          (`registerBindlessBufferSRV( RHIBufferSlice )`)를 갖는다. 비행 중 프레임 복제는 페이지가 갖는다(백엔드가 구간을 그 프레임 칸에 쓴다).
 *          가장 큰 등급(4096 B)보다 큰 요청은 전용 버퍼 하나다(칸 없음). 언리얼 `FD3D12PoolAllocator` · 유니티 SRP Batcher 의 영속 버퍼 오프셋 자리다.
 *          디바이스가 하나씩 갖는다(`IRHIDevice::getConstantBufferPagePool`). 렌더 스레드가 맞추고(`fit`), 놓기(`ConstantBufferPoolSlice::reset`)는 어느 스레드에서든 한다.
 *          잠금은 잎 잠금이다(결정 5-3) — 할당기 · 페이지 표만 그 안에서 고치고, RHI 호출(만들기 · 등록 · `releaseHandle`)과 로그는 잠금 밖에서 한다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/ConstantBufferSlotAllocator.h"
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class ConstantBufferPoolSlice
     * @brief 풀에서 받은 상수 버퍼 구간 하나의 소유 핸들입니다(옮기기만 된다). 소멸 · `reset` 이 준 디바이스의 풀에 돌려준다.
     * @details 칸이 무효이고 구간 버퍼가 있으면 등급보다 커서 받은 전용 버퍼다. 디바이스가 이미 사라졌으면(`RHIRenderResource::clearRHI`) `detach` 으로 핸들만 잊는다 —
     *          든 쪽이 (디바이스, 구간) 짝을 따로 들지 않게 한 형식이다(언리얼 `TRefCountPtr<FRHIUniformBuffer>` 의 소유 자리).
     */
    class SW_API ConstantBufferPoolSlice
    {
    public:
        ConstantBufferPoolSlice() = default;
        ~ConstantBufferPoolSlice();
        ConstantBufferPoolSlice( ConstantBufferPoolSlice&& other ) noexcept;
        ConstantBufferPoolSlice& operator=( ConstantBufferPoolSlice&& other ) noexcept;
        ConstantBufferPoolSlice( const ConstantBufferPoolSlice& )            = delete;
        ConstantBufferPoolSlice& operator=( const ConstantBufferPoolSlice& ) = delete;

        /** @brief 구간을 들고 있으면 true 입니다. */
        bool isValid() const { return _range._buffer != 0; }
        /** @brief 구간을 준 디바이스입니다(들고 있지 않으면 nullptr). */
        IRHIDevice* getDevice() const { return _pDevice; }
        /** @brief 버퍼 안의 구간입니다. */
        const RHIBufferSlice& getRange() const { return _range; }
        /** @brief 구간의 bindless 인덱스입니다. */
        RHIDescriptorIndex getDescriptorIndex() const { return _descriptorIndex; }
        /** @brief 등급 칸입니다(전용 버퍼면 무효). */
        const ConstantBufferSlot& getSlot() const { return _slot; }

        /** @brief 살아 있는 디바이스의 풀에 구간을 돌려주고 비웁니다(어느 스레드든 — 핸들 반환은 `releaseHandle` 이 미룬다). */
        void reset();
        /** @brief 디바이스가 이미 사라졌을 때 핸들만 잊습니다(돌려줄 곳이 없다). */
        void detach();

    private:
        friend class ConstantBufferPagePool;

        IRHIDevice*        _pDevice{ nullptr };
        ConstantBufferSlot _slot;
        RHIBufferSlice     _range;
        RHIDescriptorIndex _descriptorIndex{ kInvalidDescriptorIndex };
    };

    /**
     * @class ConstantBufferPagePool
     * @brief 페이지 RHI 버퍼와 등급 칸 할당기입니다. 파일 머리말 참고.
     */
    class SW_API ConstantBufferPagePool
    {
    public:
        ConstantBufferPagePool()                                           = default;
        ConstantBufferPagePool( const ConstantBufferPagePool& )            = delete;
        ConstantBufferPagePool& operator=( const ConstantBufferPagePool& ) = delete;

        /**
         * @brief (렌더 스레드) @p inoutSlice 가 @p sizeBytes 를 담게 합니다. 비었으면 받고, 등급이 바뀌었으면 옮기며(디스크립터가 새로 난다 — 옛 것은 놓는다), 같으면 그대로입니다.
         * @details 다른 디바이스의 구간이면 먼저 그 디바이스에 돌려준다(백엔드 교체).
         * @return 페이지 · 디스크립터를 만들지 못하면 오류를 남기고 false 이며 @p inoutSlice 는 빈다.
         */
        [[nodiscard]] bool fit( IRHIDevice& device, uint32 sizeBytes, ConstantBufferPoolSlice& inoutSlice );
        /** @brief (렌더 스레드) 구간의 이번 프레임 칸에 @p size 바이트를 씁니다(구간 크기에서 자른다). */
        static void update( const ConstantBufferPoolSlice& slice, const void* pData, uint32 size );
        /** @brief (디바이스 종료) 남은 페이지 버퍼를 모두 돌려주고 할당기를 비웁니다. 구간을 든 쪽은 이미 `releaseRHI` 로 놓았다. */
        void releaseAllPages( IRHIDevice& device );

        /** @brief 열린 페이지 수입니다(백엔드 할당 수 — 측정 `RT.RHI.constantBufferPages`). */
        uint32 getOpenPageCount() const;

    private:
        friend class ConstantBufferPoolSlice;

        /** @brief 구간의 디스크립터 · 칸(빈 페이지) · 전용 버퍼를 `IRHIDevice::releaseHandle` 로 돌려줍니다. 구간 값은 부르는 쪽이 비운다. */
        void releaseSlice( IRHIDevice& device, const ConstantBufferPoolSlice& slice );
        /** @brief 칸 @p slot 의 구간을 엽니다 — 새 페이지면 RHI 버퍼를 만들고, 구간 디스크립터를 등록한다. 실패하면 칸을 할당기에 돌려준다. */
        [[nodiscard]] bool openSlice( IRHIDevice& device, const ConstantBufferSlot& slot, bool bNewPage, ConstantBufferPoolSlice& outSlice ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 전용 버퍼(등급 밖)를 만들고 디스크립터를 등록합니다. */
        [[nodiscard]] static bool openDedicated( IRHIDevice& device, uint32 sizeBytes, ConstantBufferPoolSlice& outSlice );
        /** @brief 칸을 할당기에 돌려주고, 페이지가 닫히면 그 RHI 버퍼를 놓습니다. */
        void releaseSlot( IRHIDevice& device, const ConstantBufferSlot& slot ) SW_EXCLUDES_LOCK( _mutex );
        /** @brief 칸을 할당기에 돌려주고, 페이지가 닫혔으면 그 번호의 RHI 버퍼를 표에서 떼어 돌려줍니다(아니면 0). 잠금 안에서 부른다. */
        RHIBufferHandle releaseSlotLocked( const ConstantBufferSlot& slot ) SW_REQUIRES_LOCK( _mutex );

    private:
        ConstantBufferSlotAllocator _allocator  SW_GUARDED_BY( _mutex );
        vector<RHIBufferHandle> _listPageBuffer SW_GUARDED_BY( _mutex ); ///< 페이지 번호마다 RHI 버퍼(닫힌 번호는 0)
        mutable mutex                           _mutex;
    };
} // namespace sw
