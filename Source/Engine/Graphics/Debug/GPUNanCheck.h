/**
 * @file GPUNanCheck.h
 * @brief 패스 출력 텍스처의 NaN · Inf 감시입니다 — 컴퓨트가 부동소수 출력 텍스처를 훑어 NaN · Inf 가 든 텍셀 수를 슬롯별 카운터에 더하고, CPU 가 몇 프레임 뒤 되읽어 패스 이름과 함께 로그로 냅니다.
 * @details 언리얼 `r.CheckNaN`(패스마다 출력의 NaN · Inf 를 잡는다)과 같은 선이다. 켜는 스위치는 `-gv_nanCheck=1`(기본 끔)이고 개발 빌드에서만 등록된다(Shipping 은 늘 꺼진다 — PSO 도 디스패치도 없다).
 *          카운터는 지우지 않는 누적 값이라 클리어 패스가 필요 없다: CPU 가 지난번 읽은 값과의 차가 이번 구간의 수다. 슬롯은 이름으로 정해진다(패스 이름 + 첨부 이름 — 같은 이름은 늘 같은 슬롯).
 *
 *          **네이티브 bindless(DX12 · Vulkan)만 쓴다.** 컴퓨트가 엔진 텍스처를 읽어야 하는데 DX11 · GL 의 컴퓨트는 샘플 텍스처를 못 읽는다(binding.hlsli). 그 백엔드에서는 `initialize` 가 이유를 경고로 남기고
 *          꺼 둔다(능력 선언 분기 — 다른 길로 대신 읽지 않는다). 스캔은 그래프가 끝난 뒤의 내용을 본다(`FramePassExecutor::scanPassOutputsForNan`) — 같은 프레임 안에서 첨부를 재사용하는 패스가 있으면
 *          마지막 쓴 패스의 출력이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/AsyncBufferReadback.h"
#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHICommandList;
    class IRHIDevice;

    /**
     * @class GPUNanCheck
     * @brief NaN · Inf 카운터 버퍼 하나와 그 되읽기 · 로그입니다. 렌더 스레드 하나가 쓴다(`FrameRenderer` 가 갖는다).
     */
    class SW_API GPUNanCheck
    {
    public:
        /// @brief 감시할 수 있는 출력의 수입니다(슬롯 하나가 카운터 둘 — NaN · Inf).
        static constexpr uint32 kMaxSlot = 64;
        /// @brief 스레드 그룹 한 변입니다(gpu_nan_check.hlsl 의 numthreads 와 같다).
        static constexpr uint32 kGroupSize = 8;

        GPUNanCheck();
        ~GPUNanCheck();
        GPUNanCheck( const GPUNanCheck& )            = delete;
        GPUNanCheck& operator=( const GPUNanCheck& ) = delete;

        /** @brief 이 실행이 NaN 감시를 요구하는지입니다 — 개발 빌드이고 `-gv_nanCheck` 가 켜졌을 때만 참입니다. Shipping 은 늘 거짓입니다. PSO 는 이 값으로 만들어집니다. */
        [[nodiscard]] static bool isRequested();

        /**
         * @brief 카운터 버퍼(0 으로 채운 UAV 구조 버퍼)와 되읽기 고리를 만듭니다. 요구되지 않았거나 백엔드가 못 쓰면(네이티브 bindless 가 없음) 만들지 않고 true 입니다(못 쓰는 이유는 경고로 남긴다).
         * @return 요구됐고 쓸 수 있는데 만들지 못하면 false 입니다(이유는 로그).
         */
        [[nodiscard]] bool initialize( IRHIDevice* pDevice );
        /** @brief 버퍼와 고리를 놓습니다. 여러 번 불러도 안전합니다. */
        void shutdown();
        /** @brief 감시가 켜져 PSO 와 버퍼가 갖춰졌는지입니다. */
        bool isActive() const { return _buffer.isValid(); }

        /** @brief 텍스처 포맷이 NaN · Inf 를 담을 수 있는 부동소수 컬러인지입니다(감시 대상). */
        static bool isScannableFormat( RHIFormat format );

        /**
         * @brief 컴퓨트 디스패치 하나를 기록합니다 — 호출하기 전에 @p texture 를 셰이더 읽기 상태로 옮겨 둔다. 이름의 슬롯이 없으면 새로 잡고, 슬롯이 모두 차면 오류를 한 번 남기고 건너뜁니다.
         * @param pName   로그에 나올 이름(패스 이름과 첨부 이름)입니다.
         * @param srv     텍스처의 bindless 번호입니다.
         */
        void scan( IRHICommandList& commands, RHIPipelineStateHandle pso, string_view name, RHIDescriptorIndex srv, uint32 width, uint32 height );
        /** @brief 이 프레임의 스캔이 모두 끝난 뒤 한 번 부릅니다. 카운터 전체를 읽기 버퍼로 복사하는 명령을 넣습니다(프레임 번호가 요청의 tag 다). 스캔이 없었으면 하지 않는다. */
        void requestReadback( IRHICommandList& commands );
        /** @brief 도착한 가장 최근 사본을 읽어 지난번보다 늘어난 슬롯을 로그로 냅니다. 몇 프레임 늦게 도착한다. */
        void poll();

        /** @brief 지금까지 로그로 낸 NaN 텍셀 수입니다(검사용). */
        uint64 getReportedNanCount() const { return _reportedNanCount; }
        /** @brief 지금까지 로그로 낸 Inf 텍셀 수입니다(검사용). */
        uint64 getReportedInfCount() const { return _reportedInfCount; }

    private:
        /// @brief 슬롯의 이름을 찾거나 새로 잡습니다. 슬롯이 모두 찼으면 kMaxSlot 입니다.
        uint32 getOrCreateSlot( string_view name );

        IRHIDevice*             _pDevice;
        RHIStructuredBufferSlot _buffer;
        AsyncBufferReadback     _readback;
        vector<string>          _listSlotName;
        vector<uint32>          _listPreviousCount; ///< 슬롯마다 지난번까지 읽은 카운터(NaN · Inf 순서로 둘씩)
        uint64                  _frameSerial;
        uint64                  _reportedNanCount;
        uint64                  _reportedInfCount;
        uint8                   _bScannedThisFrame;
    };
} // namespace sw
