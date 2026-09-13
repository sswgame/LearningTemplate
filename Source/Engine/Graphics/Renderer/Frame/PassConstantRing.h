/**
 * @file PassConstantRing.h
 * @brief 프레임마다 되감는 패스 상수버퍼 슬롯 링 — 드로우마다 하나씩 나눠 준다.
 * @details `updateConstantBuffer` 는 버퍼의 **프레임 슬롯 하나**에 쓰고 GPU 는 제출 뒤에 읽는다. 그래서 여러 드로우가
 *          같은 버퍼를 쓰면 전부 마지막에 쓴 값을 본다 — 한 패스의 드로우들이 서로를 덮어쓰던 버그가 그것이었다
 *          (RenderPassGpuTest.MultiBatchPassKeepsPerBatchConstants). 언리얼도 드로우별 느슨한 파라미터는 드로우마다
 *          유니폼 버퍼를 따로 잡는다. 여기서는 슬롯을 미리 만들어 두고 원자 커서로 나눠 준다 — 패스가 병렬로
 *          기록되므로 락 없이 분배해야 한다.
 *
 *          0 번 슬롯은 **프레임 시드 전용**이다(직렬 경로의 `FramePassContext` 가 든다). 패스와 드로우는 1 번부터 받는다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class PassConstantRing
     * @brief 패스/드로우 상수버퍼 슬롯의 소유자. 만들고(initialize·ensureCapacity), 나눠 주고(acquire), 놓는다(release).
     */
    class SW_API PassConstantRing
    {
    public:
        /// @brief 처음 만들어 두는 슬롯 수. 배치 수에 따라 `ensureCapacity` 가 더 키운다.
        static constexpr uint32 kInitialSlotCount = 64;
        /// @brief 슬롯 상한. 넘으면 에러를 남기고 마지막 슬롯을 공유한다(그 프레임은 배치 상수가 섞인다).
        static constexpr uint32 kMaxSlotCount = 4096;
        /// @brief 슬롯 하나의 바이트 수 — 엔진 PassCB 레이아웃(리플렉션)이 이 안에 들어야 한다.
        static constexpr uint32 kSlotBytes = 512;

        PassConstantRing();
        ~PassConstantRing() = default;

        PassConstantRing( const PassConstantRing& )            = delete;
        PassConstantRing& operator=( const PassConstantRing& ) = delete;

        /** @brief 초기 슬롯(kInitialSlotCount)을 만듭니다. 이미 있으면 그대로 true. */
        bool initialize( IRHIDevice* pDevice );
        /** @brief 슬롯을 디바이스에 돌려주고 비웁니다 — 디바이스가 **살아 있을 때**. */
        void release( IRHIDevice* pDevice );
        /** @brief 디바이스가 이미 사라졌다 — 핸들만 잊습니다 (destroy 를 부르면 use-after-free). */
        void forget();

        /** @brief 프레임 시작: 커서를 1 번 슬롯으로 되감고 고갈 경고 래치를 풉니다. */
        void beginFrame();
        /**
         * @brief 슬롯을 하나 빌립니다 — **드로우마다** 하나씩. 슬롯이 하나도 없으면 false.
         * @details 커서는 원자적이라 병렬 기록에서도 안전하다. 슬롯 배열은 기록 시작 전에 잡아 두고 병렬 구간에서
         *          크기가 변하지 않는다. 모자라면 마지막 슬롯을 공유하고 프레임당 한 번 경고한다.
         */
        bool acquire( RHIBufferHandle& outBuffer, RHIDescriptorIndex& outIndex );
        /**
         * @brief 슬롯 수를 `needed` 이상으로 늘립니다 — **기록 시작 전에만** 부릅니다.
         * @details 버퍼 생성과 bindless 등록은 레지스트리를 바꾸므로 병렬 기록 중에는 할 수 없다
         *          (IRHIDevice::setParallelRecording). 배치 수를 아는 프레임 시작 지점에서 미리 키운다.
         */
        void ensureCapacity( IRHIDevice* pDevice, uint32 needed );

        /** @brief 0 번 슬롯 — 프레임 시드 전용. 없으면 false. */
        bool getSeedSlot( RHIBufferHandle& outBuffer, RHIDescriptorIndex& outIndex ) const;
        /** @brief 지금 만들어 둔 슬롯 수. */
        uint32 getSlotCount() const { return static_cast<uint32>( _listSlot.size() ); }
        /** @brief 지금까지 한 프레임에서 쓴 슬롯 수의 최댓값 — 다음 프레임 용량 산정의 바닥값(단조 증가). */
        uint32 getHighWater() const { return _highWater.load( std::memory_order_relaxed ); }

    private:
        /** @brief 슬롯 하나 — 상수버퍼와 그 bindless 인덱스. */
        struct Slot
        {
            RHIBufferHandle    _buffer{ 0 };
            RHIDescriptorIndex _index{ kInvalidDescriptorIndex };
        };

        /** @brief 슬롯을 하나 더 만듭니다. 못 만들면 false. */
        bool appendSlot( IRHIDevice* pDevice );

        vector<Slot>   _listSlot;
        atomic<uint32> _cursor;
        atomic<uint32> _highWater;
        /// @brief 슬롯 고갈 경고를 프레임당 한 번만 남기기 위한 래치.
        atomic<uint8> _bExhaustedLogged;
    };
} // namespace sw
