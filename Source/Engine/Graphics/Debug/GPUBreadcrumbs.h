/**
 * @file GPUBreadcrumbs.h
 * @brief GPU 브레드크럼입니다 — 패스마다 GPU 가 닿은 자리를 표지로 남겨 두었다가 디바이스 소실(TDR) 때 마지막 패스 이름을 로그와 크래시 묶음에 싣습니다.
 * @details 엔진 쪽 표(`패스 이름 ↔ id`)와 소실 보고를 맡고, GPU 에 표지를 쓰는 일은 RHI 백엔드가 한다(`IRHICommandList::writeBreadcrumb`, `IRHIDevice::readLastBreadcrumbs`).
 *          언리얼 `r.GPUCrashDebugging` 의 브레드크럼(Nvidia Aftermath · AMD Radeon GPU Detective 의 마커)과 같은 자리다 — 언리얼은 패스 이름을 표지 값으로 쓰고 소실 뒤 그 값을 이름으로 푼다.
 *
 *          패스마다 시작 표지(id)와 끝 표지(`id | kEndMarkerBit`)를 쓴다(`GPUPassScope`) — 소실 보고가 "마지막으로 끝난 패스" 와 "시작했지만 끝나지 않은 패스(원인 후보)" 를 가른다.
 *
 *          백엔드별 표지:
 *          - **DX12** — `ID3D12GraphicsCommandList2::WriteBufferImmediate` 로 기록 가능한 버퍼에 쓴다: 시작 표지는 MARKER_IN(앞 명령이 시작한 뒤)으로 [0] 칸, 끝 표지는 MARKER_OUT(앞 명령이 끝난 뒤)으로 [1] 칸.
 *            소실 뒤 그 버퍼는 CPU 가 읽는다. DRED 자동 브레드크럼(명령 종류별 진행) · 페이지 폴트도 켠다.
 *          - **Vulkan** — `VK_NV_device_diagnostic_checkpoints`(`vkCmdSetCheckpointNV`). 없으면 `VK_AMD_buffer_marker`(AMD · 인텔 Mesa — 호스트 보임 버퍼 두 칸에 DX12 와 같은 모양으로 쓴다: 시작 TOP_OF_PIPE [0] · 끝 BOTTOM_OF_PIPE [1]). 둘 다 없으면 능력을 끈다(`VulkanBreadcrumbMode`).
 *          - **DX11 · OpenGL** — GPU 쪽 표지 수단이 없다(DX11 은 소실 뒤 읽을 수 있는 디바이스측 쓰기가 없고, GL 은 컨텍스트 소실 시 상태를 읽을 길이 없다). 능력을 끄고 CPU 가 마지막으로 기록한 패스만 보고한다.
 *
 *          켜는 스위치는 `-gv_gpuBreadcrumbs`(개발 구성 기본 켬 — 표지 한 번이 패스당 GPU 쓰기 하나뿐이라 값싸다)이고, 배포 구성은 기본 끔이며 실행 인자로 켠다(언리얼 `-gpucrashdebugging`).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    class IRHICommandList;

    /**
     * @class GPUBreadcrumbs
     * @brief 패스 이름 표와 디바이스 소실 보고의 창구입니다. 모든 함수는 어느 스레드에서 불러도 됩니다(패스는 병렬로 기록될 수 있다).
     * @details 상태(이름 표 · CPU 가 마지막으로 기록한 패스 · 소실 보고 여부)는 서비스 범위의 서브시스템 `GPUBreadcrumbLog` 가 갖고, 이 창구는 서비스 표의 칸으로 찾는다
     *          (`engine::findSubsystem<GPUBreadcrumbLog>()`). 칸이 비면(엔진 없는 도구 · 테스트) 표지를 남기지 않고 소실 보고는 이름 없이 로그만 남긴다.
     */
    class SW_API GPUBreadcrumbs
    {
    public:
        /// @brief 표가 담는 이름의 수입니다. 패스 이름은 유한하다 — 넘으면 새 이름은 표지를 쓰지 않는다(오류를 한 번 남긴다).
        static constexpr uint32 kMaxName = 512;
        /// @brief 이름 하나의 최대 길이입니다(넘으면 잘린다).
        static constexpr uint32 kNameLength = 48;
        /// @brief 소실 때 한 번에 보고할 표지의 수입니다(Vulkan 은 파이프라인 단계마다 하나씩 돌려준다).
        static constexpr uint32 kMaxReported = 8;

        /** @brief 이 실행이 브레드크럼을 쓰는지입니다(`-gv_gpuBreadcrumbs`, 배포 구성은 기본 끔). 백엔드가 DRED · 체크포인트를 켤 때도 이 값을 봅니다. */
        [[nodiscard]] static bool isEnabled();

        /**
         * @brief 패스 이름을 표에 올리고(처음이면) id 를 돌려줍니다. 같은 이름은 늘 같은 id 입니다. 꺼져 있거나 표가 찼으면 0 입니다.
         */
        [[nodiscard]] static uint32 registerPass( string_view name );

        /** @brief id 의 이름을 @p pOutName 에 씁니다(할당 없음). 없는 id 면 false 입니다. */
        [[nodiscard]] static bool resolve( uint32 id, utf8* pOutName, uint32 capacity );

        /// @brief 끝 표지의 비트입니다 — 패스 끝에서 `id | kEndMarkerBit` 을 쓴다. 이름 표의 id 는 `kMaxName` 이하라 겹치지 않는다.
        static constexpr uint32 kEndMarkerBit = 0x80000000u;

        /**
         * @brief 패스의 시작 표지를 남깁니다 — 이름을 표에 올리고 CPU 가 마지막으로 기록한 패스로 적은 뒤 GPU 표지(`IRHICommandList::writeBreadcrumb`)를 기록합니다.
         * @return 짝이 되는 `endPass` 에 넘길 id 입니다. 꺼져 있거나 표가 찼으면 0 이고 아무것도 기록하지 않습니다.
         */
        [[nodiscard]] static uint32 beginPass( IRHICommandList& commands, string_view passName );
        /** @brief 패스의 끝 표지(`id | kEndMarkerBit`)를 남깁니다. @p id 가 0 이면 아무것도 하지 않습니다. 소실 뒤 "마지막으로 끝난 패스" 와 "시작했지만 끝나지 않은 패스" 를 가른다. */
        static void endPass( IRHICommandList& commands, uint32 id );

        /**
         * @struct Report
         * @brief 소실 뒤 읽은 표지들을 푼 결과입니다(`parse`). id 는 끝 비트를 뗀 이름 표의 id 다.
         */
        struct Report
        {
            uint32 _lastCompletedID{ 0 };          ///< 끝 표지가 닿은 패스(0 = 없음) — 이 패스까지는 GPU 가 끝냈다
            uint32 _arrInFlightID[kMaxReported]{}; ///< 시작 표지만 닿은 패스 — 소실 때 GPU 가 돌던 패스(원인 후보)
            uint32 _inFlightCount{ 0 };
        };
        /**
         * @brief 백엔드가 돌려준 표지 값(`IRHIDevice::readLastBreadcrumbs` — DX12 는 [시작 칸, 끝 칸], Vulkan 은 파이프라인 단계마다 하나)을 풉니다. GPU 없이 테스트한다.
         * @details 끝 표지가 여럿이면 앞의 것을 마지막으로 끝난 패스로 본다(Vulkan 은 늦은 단계가 앞이 아니라 단계 순서라 같은 값이 겹친다). 시작 표지는 끝 표지와 id 가 같으면 끝난 것이다.
         */
        [[nodiscard]] static Report parse( const uint32* pMarker, uint32 markerCount );

        /** @brief CPU 가 마지막으로 기록한 패스의 id 입니다(0 = 없음). GPU 표지를 못 쓰는 백엔드의 소실 보고가 이것을 읽습니다. */
        [[nodiscard]] static uint32 getLastRecordedID();

        /**
         * @brief 디바이스 소실을 보고합니다 — 오류 로그 한 줄, 크래시 문맥(`gpu_last_pass` · `gpu_lost_backend`), 크래시 빵부스러기(`CrashBreadcrumbStore`)에 마지막 패스 이름을 싣습니다.
         *        크래시 묶음(`crash_<세션>.txt` · `.breadcrumbs.txt`)이 이 값을 그대로 담는다.
         * @param pBackend  백엔드 이름입니다.
         * @param pGPUID    GPU 가 닿은 표지 id 목록입니다(없으면 nullptr · 0 — CPU 가 마지막으로 기록한 패스만 보고한다).
         * @param gpuCount  `pGPUID` 의 수입니다. 0 인 id 는 건너뜁니다(표지를 쓰지 않은 값).
         * @note 첫 보고만 로그를 남깁니다(소실 뒤 같은 호출이 프레임마다 와도 줄이 쌓이지 않는다).
         */
        static void reportDeviceLost( const utf8* pBackend, const uint32* pGPUID, uint32 gpuCount );
    };

    /**
     * @class GPUPassScope
     * @brief GPU 패스 하나의 구간입니다 — 만들 때 브레드크럼 시작 표지와 이벤트 표지(RenderDoc · PIX 의 구간 이름)를 열고, 사라질 때 둘을 거꾸로 닫습니다.
     * @details 언리얼 `RDG_EVENT_SCOPE` + RHI 브레드크럼(`FRHIBreadcrumbScope`)을 한 객체로 둔 자리다. 패스마다 "표지 · 이벤트 열기 … 닫기" 를 손으로 짝지으면
     *          일찍 돌아가는 길에서 짝이 어긋난다(이름 없는 패스가 연 적 없는 이벤트를 닫았다). 이름이 비면 아무것도 열지 않는다.
     */
    class SW_API GPUPassScope
    {
    public:
        /// @brief 타임스탬프를 쓰지 않는 구간의 슬롯 값입니다.
        static constexpr uint32 kNoTimestamp = constant::invalid_index::kUint32;

        GPUPassScope( IRHICommandList& commands, string_view passName );
        /**
         * @brief 구간의 시작 · 끝에 GPU 타임스탬프도 씁니다 — 시작은 @p timestampBeginSlot, 끝은 그 다음 칸(`IRHICommandList::writeTimestamp`).
         * @details 슬롯이 `kNoTimestamp` 면 쓰지 않는다. 쓸지는 부르는 쪽이 정한다(계측 스위치 `GPUPassTimeline::isRecording` · 칸 배치 `FrameRendererUtil`).
         *          타임스탬프는 이벤트 표지 안쪽이라 캡처 도구에서 같은 구간에 붙는다.
         */
        GPUPassScope( IRHICommandList& commands, string_view passName, uint32 timestampBeginSlot );
        ~GPUPassScope();
        GPUPassScope( const GPUPassScope& )            = delete;
        GPUPassScope& operator=( const GPUPassScope& ) = delete;

    private:
        IRHICommandList& _commands;
        uint32           _breadcrumbID;       ///< 0 이면 끝 표지를 쓰지 않는다
        uint32           _timestampBeginSlot; ///< `kNoTimestamp` 면 타임스탬프를 쓰지 않는다
        uint8            _bEventOpen;         ///< 이벤트 표지를 열었으면 1
    };
} // namespace sw

namespace sw
{
    /**
     * @class GPUBreadcrumbLog
     * @brief GPU 브레드크럼의 세션 상태입니다 — 패스 이름 표(id = 칸 번호 + 1, 한 번 올린 이름은 바뀌지 않는다), CPU 가 마지막으로 기록한 패스, 소실을 보고했는지.
     * @details 서비스 범위의 서브시스템이다(`EngineSubsystemSet` — 렌더 타깃 목록과 같은 자리). 예전에는 파일 정적이라 엔진을 다시 세워도 이전 실행의 소실 보고 표시가 남았다.
     *          `GPUBreadcrumbs` 창구만 부른다. 언리얼 `FGPUBreadcrumbs`(RHI 가 갖는 브레드크럼 상태)와 같은 자리.
     */
    class SW_API GPUBreadcrumbLog final : public ISubsystem
    {
    public:
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::RenderCPU; ///< 할당 용도(`ISubsystem::getSubsystemMemoryTag`)
        static constexpr const utf8* kSubsystemName      = "GPUBreadcrumbLog";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 이름을 표에 올리고(처음이면) id 를 돌려줍니다. 표가 찼으면 0 입니다(오류를 한 번 남긴다). */
        [[nodiscard]] uint32 registerPass( string_view name );
        /** @brief id 의 이름을 @p pOutName 에 씁니다. 없는 id 면 false 입니다. */
        [[nodiscard]] bool resolve( uint32 id, utf8* pOutName, uint32 capacity );
        /** @brief CPU 가 마지막으로 기록한 패스를 적습니다(병렬 기록이면 마지막으로 쓴 스레드의 것). */
        void recordRecorded( uint32 id ) { _lastRecordedID.store( id ); }
        /** @brief CPU 가 마지막으로 기록한 패스의 id 입니다(0 = 없음). */
        [[nodiscard]] uint32 getLastRecordedID() const { return _lastRecordedID.load(); }
        /** @brief 소실 보고를 처음 맡으면 true 입니다 — 소실 뒤에는 같은 호출이 프레임마다 온다. */
        [[nodiscard]] bool claimLostReport() { return _bLostReported.exchange( 1 ) == 0; }

    private:
        mutex                                     _mutex;
        uint64                                    _arrHash[GPUBreadcrumbs::kMaxName] SW_GUARDED_BY( _mutex ){};
        fixed_string<GPUBreadcrumbs::kNameLength> _arrName[GPUBreadcrumbs::kMaxName] SW_GUARDED_BY( _mutex ){};
        uint32 _count                             SW_GUARDED_BY( _mutex ){ 0 };
        atomic<uint32>                            _lastRecordedID{ 0 };
        atomic<uint32>                            _bLostReported{ 0 };

        SW_SERVICE_SLOT( GPUBreadcrumbLog, Optional );
    };
} // namespace sw
