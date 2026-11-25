/**
 * @file ConstantBufferPoolSlice.h
 * @brief 상수 버퍼 페이지 풀(`ConstantBufferPagePool`)에서 받은 구간의 소유 핸들입니다. 구간을 드는 헤더가 풀의 헤더를 끌어오지 않게 따로 둔다.
 */
#pragma once
#include "Core/Foundation/Macros.h"

#include "Engine/Graphics/RHI/ConstantBufferSlotAllocator.h"
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    class ConstantBufferPagePool;
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
        RHIBufferSRVIndex getDescriptorIndex() const { return _descriptorIndex; }
        /** @brief 등급 칸입니다(전용 버퍼면 무효). */
        const ConstantBufferSlot& getSlot() const { return _slot; }

        /** @brief 살아 있는 디바이스의 풀에 구간을 돌려주고 비웁니다(어느 스레드든 — 칸은 그것을 읽는 프레임이 끝난 뒤에 다시 쓰인다). */
        void reset();
        /** @brief 디바이스가 이미 사라졌을 때 핸들만 잊습니다(돌려줄 곳이 없다). */
        void detach();

    private:
        friend class ConstantBufferPagePool;

        IRHIDevice*        _pDevice{ nullptr };
        ConstantBufferSlot _slot;
        RHIBufferSlice     _range;
        RHIBufferSRVIndex  _descriptorIndex{ kInvalidDescriptorIndex };
    };
} // namespace sw
