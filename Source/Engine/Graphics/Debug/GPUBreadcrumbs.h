/**
 * @file GPUBreadcrumbs.h
 * @brief GPU 브레드크럼입니다 — 패스마다 GPU 가 닿은 자리를 표지로 남겨 두었다가 디바이스 소실(TDR) 때 마지막 패스 이름을 로그와 크래시 묶음에 싣습니다.
 * @details 엔진 쪽 표(`패스 이름 ↔ id`)와 소실 보고를 맡고, GPU 에 표지를 쓰는 일은 RHI 백엔드가 한다(`IRHICommandList::writeBreadcrumb`, `IRHIDevice::readLastBreadcrumbs`).
 *          언리얼 `r.GPUCrashDebugging` 의 브레드크럼(Nvidia Aftermath · AMD Radeon GPU Detective 의 마커)과 같은 자리다 — 언리얼은 패스 이름을 표지 값으로 쓰고 소실 뒤 그 값을 이름으로 푼다.
 *
 *          백엔드별 표지:
 *          - **DX12** — `ID3D12GraphicsCommandList2::WriteBufferImmediate`(MARKER_IN)로 기록 가능한 버퍼에 id 를 쓴다. 소실 뒤 그 버퍼는 CPU 가 읽는다. DRED 자동 브레드크럼(명령 종류별 진행)도 켠다.
 *          - **Vulkan** — `VK_NV_device_diagnostic_checkpoints`(`vkCmdSetCheckpointNV`). 확장이 없으면(AMD · 인텔 — `VK_AMD_buffer_marker` 는 이 엔진이 쓰지 않는다) 능력을 끈다.
 *          - **DX11 · OpenGL** — GPU 쪽 표지 수단이 없다(DX11 은 소실 뒤 읽을 수 있는 디바이스측 쓰기가 없고, GL 은 컨텍스트 소실 시 상태를 읽을 길이 없다). 능력을 끄고 CPU 가 마지막으로 기록한 패스만 보고한다.
 *
 *          켜는 스위치는 `-gv_gpuBreadcrumbs`(개발 빌드 기본 켬 — 표지 한 번이 패스당 GPU 쓰기 하나뿐이라 값싸다)이고 Shipping 은 등록되지 않아 늘 꺼진다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"
#include "Core/Subsystem/ISubsystem.h"

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

        /** @brief 이 실행이 브레드크럼을 쓰는지입니다 — 개발 빌드이고 `-gv_gpuBreadcrumbs` 가 켜졌을 때만 참입니다. Shipping 은 늘 거짓입니다. 백엔드가 DRED · 체크포인트를 켤 때도 이 값을 봅니다. */
        [[nodiscard]] static bool isEnabled();

        /**
         * @brief 패스 이름을 표에 올리고(처음이면) id 를 돌려줍니다. 같은 이름은 늘 같은 id 입니다. 꺼져 있거나 표가 찼으면 0 입니다.
         */
        [[nodiscard]] static uint32 registerPass( string_view name );

        /** @brief id 의 이름을 @p pOutName 에 씁니다(할당 없음). 없는 id 면 false 입니다. */
        [[nodiscard]] static bool resolve( uint32 id, utf8* pOutName, uint32 capacity );

        /**
         * @brief 패스의 표지를 남깁니다 — 이름을 표에 올리고 CPU 가 마지막으로 기록한 패스로 적은 뒤 GPU 표지(`IRHICommandList::writeBreadcrumb`)를 기록합니다. 꺼져 있으면 아무것도 하지 않습니다.
         */
        static void mark( IRHICommandList& commands, string_view passName );

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
        static constexpr const utf8* kSubsystemName = "GPUBreadcrumbLog";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 이름을 표에 올리고(처음이면) id 를 돌려줍니다. 표가 찼으면 0 입니다(오류를 한 번 남긴다). */
        [[nodiscard]] uint32 registerPass( string_view name );
        /** @brief id 의 이름을 @p pOutName 에 씁니다. 없는 id 면 false 입니다. */
        [[nodiscard]] bool resolve( uint32 id, utf8* pOutName, uint32 capacity );
        /** @brief CPU 가 마지막으로 기록한 패스를 적습니다(병렬 기록이면 마지막으로 쓴 스레드의 것). */
        void noteRecorded( uint32 id ) { _lastRecordedID.store( id ); }
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
    };
} // namespace sw
