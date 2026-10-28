/**
 * @file OpenGLRHIDevice.h
 * @brief OpenGL 4.6 코어 프로파일 RHI 백엔드 디바이스를 정의합니다.
 * @note glad(GL 심볼)는 이 헤더가 아니라 GL 의 .cpp 들이 직접, 또는 OpenGLRHIDeviceInternal.h 를 거쳐 include 합니다.
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/Backend/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Backend/RHIReleaseQueue.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIRenderPassLedger.h"
#include "Engine/Graphics/RHI/Diagnostics/RHITimestamp.h"
#include "Engine/Graphics/RHI/IRHIContextThreadBinding.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/RHI/Transfer/RHIStagingAllocator.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferSerials.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    struct RHIMemoryBudget;

    class IOpenGLPlatformContext;
    class OpenGLRHICommandContext;
    class OpenGLRHIResourceFactory;

    /**
     * @struct OpenGLRecordingState
     * @brief "지금 GL 컨텍스트에 무엇이 바인딩되어 있나" 를 담는 바인딩 캐시입니다.
     * @details **다른 백엔드와 달리 이것은 디바이스가 소유한다. 그게 맞다.** DX12, Vulkan, DX11 은
     *          리스트마다 독립된 기록 스트림(커맨드 리스트 / Deferred Context)을 가지고 기록을 나중에
     *          재생하므로 캐시도 리스트마다 있어야 한다. 전역이면 서로의 캐시를 덮어쓴다.
     *          OpenGL 은 커맨드 버퍼가 없는 **스레드 종속 상태 머신**이고
     *          `OpenGLRHICommandList` 는 호출을 즉시 GL API 로 흘려보낸다. 실제 상태가 하나뿐이므로
     *          그것을 비추는 캐시도 하나여야 한다. 리스트마다 두면 캐시가 진짜 GL 상태와 어긋난다.
     *          그래서 GL 은 `_bParallelCommandRecording = 0` 이다.
     */
    struct OpenGLRecordingState
    {
        RHIBufferHandle _boundMeshVb{ 0 };
        uint32          _boundMeshStride{ 0 };
        uint32          _boundMeshOffset{ 0 };
        RHIBufferHandle _boundInstanceSlotVb{ 0 }; ///< 슬롯 1: 인스턴스 슬롯 스트림(0이면 바인딩 안 됨)
        uint32          _boundInstanceSlotOffset{ 0 };
        /// @brief 지금 텍스처가 바인딩된 유닛의 비트마스크(비트 i는 유닛 i)입니다. beginRenderPass 가 이 유닛들만 해제합니다.
        uint32          _boundTextureUnitMask{ 0 };
        RHIBufferHandle _boundIndexBuffer{ 0 };
        uint32          _boundIndexStride{ 4 };
        uint32          _boundIndexOffset{ 0 };

        RHIPipelineStateHandle _boundGraphicsPSO{ 0 };
        RHIPipelineStateHandle _boundComputePSO{ 0 }; ///< setComputePipelineState 가 마지막으로 설정한 컴퓨트 PSO. dispatchCompute 가 이 프로그램을 쓴다.
        /// @brief 지금 렌더 패스 타깃의 높이(px)입니다. 기본 프레임버퍼에서는 뷰포트의 y 를 아래 원점으로 뒤집는 데 씁니다(`setViewport`).
        uint32 _renderTargetHeight{ 0 };
        /// @brief 지금 렌더 패스가 기본 프레임버퍼(창의 백버퍼, GL_LOWER_LEFT)에 그리는지 나타냅니다.
        uint8 _bDefaultFramebuffer{ SW_FALSE };
        /// @brief setScissorRect 가 GL_SCISSOR_TEST 를 켜 두었는지 나타냅니다. setViewport 와 beginRenderPass 가 끄고, blitTexture 는 그동안만 끕니다.
        uint8 _bScissorEnabled{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class OpenGLRHIDevice
     * @brief OpenGL 4.6 그래픽스와 컴퓨트 디바이스 구현입니다(SSBO, UBO 바인딩).
     */
    class OpenGLRHIDevice : public IRHIDevice, public IRHITimestampProvider, public IRHIContextThreadBinding
    {
        friend class OpenGLRHICommandContext;

    public:
        RHIBufferHandle createBuffer( const RHIBufferDesc& desc );
        RHIBufferHandle createIndexBuffer( const void* pData, uint32 sizeBytes );
        friend class OpenGLRHIResourceFactory;
        /** @brief 빈 OpenGL 디바이스를 만듭니다. */
        OpenGLRHIDevice();
        /** @brief 컨텍스트와 GL 객체를 정리합니다. */
        virtual ~OpenGLRHIDevice() override;

        /** @brief 플랫폼 GL 컨텍스트(WGL, GLX)를 만들고 glad 로 GL 함수를 불러옵니다. */
        [[nodiscard]] bool initializeInternal( const RHISwapChainDesc& desc ) override;

        /** @brief GL 객체와 컨텍스트를 해제합니다. */
        void shutdownInternal() override;

        /** @brief 종료 3단계입니다. 프레임 스트림 컨텍스트를 해제하고 살아 있는 커맨드 리스트를 분리합니다. */
        void detachCommandRecordingInternal() override;

        /** @brief 크기를 기록하고 glViewport 를 맞춥니다. */
        void resizeInternal( uint32 width, uint32 height ) override;

        /** @brief 프레임을 엽니다(컨텍스트 되찾기, 뷰포트, 타임스탬프 수거). 백버퍼 클리어는 beginRenderPass(핸들 0)가 합니다. */
        void beginFrame() override;

        /** @brief 프레임을 닫고, bPresent 면 플랫폼 컨텍스트로 백버퍼를 내보냅니다(SwapBuffers 등). */
        void endFrame( bool vsync, bool bPresent = true ) override;

        void               setTimestampEnabled( bool bEnabled ) override { _bTimestampEnabled.store( bEnabled ? SW_TRUE : SW_FALSE, std::memory_order_relaxed ); }
        uint32             getTimestampSlotCount() const override;
        [[nodiscard]] bool readTimestamps( RHITimestampFrame& outFrame ) override;
        [[nodiscard]] bool readGPUClockNanos( int64& outGPUNanos ) override;
        bool               isGPUClockReadCheap() const override { return true; }
        /**
         * @brief 커맨드 리스트가 부르는 기록 지점입니다. GL 은 커맨드 버퍼가 없어 그 자리에서 발행합니다.
         * @details 그래서 락이 없다. GL 호출은 컨텍스트를 가진 한 스레드에서만 나간다.
         */
        void writeTimestampSlot( uint32 slotIndex );

        IRHIResourceFactory* getResourceFactory() override;
        /** @brief 프레임 스트림 컨텍스트입니다. 백버퍼 패스와 Present 가 여기에 기록합니다. */
        IRHICommandContext* getFrameStreamContext() override;

        /** @brief GPU 가 끝날 때까지 기다리고(glFinish) 지연 해제 큐를 비웁니다. */
        void waitIdleInternal() override;

        /**
         * @brief 벤더 확장이 있을 때만 드라이버 값을 채웁니다. `GL_NVX_gpu_memory_info` 는 총량과 남은 양(디바이스 전체이며 다른 프로세스 몫을 포함)을 주고,
         *        `GL_ATI_meminfo` 는 남은 양만 줍니다. 둘 다 없으면 false 입니다(드라이버 값은 "모름").
         */
        [[nodiscard]] bool queryGPUMemoryBudgetInternal( RHIMemoryBudget& outBudget ) override;

        /** @brief 지금 기록 중인 프레임을 GPU 가 지난 뒤에 부릅니다. GL 은 프레임 끝마다 내는 펜스(`_arrFrameDoneSync`)로 확인합니다. */
        void   enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate ) override;
        uint32 getPendingReleaseCount() const override { return _releaseQueue.getPendingReleaseCount(); }

        /** @brief 백엔드 타입(OpenGL)을 반환합니다. */
        RHIBackend getBackendType() const override { return RHIBackend::OpenGL; }

        /** @brief VS 가 GPUScene 인스턴스 버퍼를 SSBO(g_SwInstances, t4)로 읽을 수 있어 true 입니다(glBindBufferBase). */
        bool supportsInstancedSceneDraw() const override { return true; }

        /** @brief 정적 능력 테이블에 런타임 값을 덮습니다. 업로드 · 되읽기 링은 초기화가 실제로 만들었을 때만 참입니다(`GL_ARB_buffer_storage`, GL 4.4). */
        RHICapabilities getCapabilities() const override
        {
            RHICapabilities caps          = RHIAvailability::query( RHIBackend::OpenGL );
            caps._bPersistentStagingRing  = _stagingRingBuffer != 0 ? SW_TRUE : SW_FALSE;
            caps._bPersistentReadbackRing = _readbackRingBuffer != 0 ? SW_TRUE : SW_FALSE;
            return caps;
        }

#if !defined( SW_SHIPPING )
        /**
         * @brief 텍스처 · 버퍼 등록 표의 그 자리에 핸들이 있으면 true 입니다(GL 은 인덱스를 텍스처 유닛 · 버퍼 바인딩으로 흉내 낸다).
         * @note GL 의 등록 표에는 잠금이 없다. 표는 GL 컨텍스트를 쥔 스레드만 읽고 쓴다 — 등록 · 해제 · 파괴가 `ScopedOpenGLContext` 안에서 표를 바꾸고, 읽는 쪽은 프레임을 기록하는 스레드다.
         */
        bool isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const override;
#endif

        /** @brief RHI 텍스처 핸들에 대응하는 GL 텍스처 이름을 반환합니다. 없으면 0 입니다. */
        uint32 getGlTextureName( RHITextureHandle texture ) const;

        /** @brief 백엔드 이름 문자열을 반환합니다. */
        const utf8* getBackendName() const override { return "OpenGL (glad 4.6 Core)"; }

        /** @brief 컨텍스트를 기다리다 포기하는 시간입니다. 렌더 워커는 프레임 끝마다 놓으므로 한 프레임이면 충분합니다. */
        static constexpr uint32 kContextAcquireTimeoutMs = 250;

        /**
         * @brief 컨텍스트를 이 스레드로 가져옵니다. 다른 스레드가 가지고 있으면 놓을 때까지 기다립니다.
         * @details GL 컨텍스트는 한 스레드만 current 로 가질 수 있다. 렌더 워커는 프레임이 끝나면
         *          놓으므로(`RenderThread::executePacket`) 기다리면 한 프레임 안에 차례가 온다.
         *          그래서 한 번 시도하고 포기하는 대신 조용히 다시 잡는다. 잡지 못하면 가진 스레드와 가진 시간을 로그에 남긴다.
         * @param timeoutMs 포기하기까지 기다리는 시간(ms)입니다.
         * @return 가져왔으면 true 입니다. 제한 시간을 넘기면 false 이며 그때만 로그를 남깁니다.
         */
        bool acquireGraphicsContextBlocking( uint32 timeoutMs = kContextAcquireTimeoutMs );
        /**
         * @brief 컨텍스트를 이 스레드에 바인딩합니다. 다른 스레드가 가지고 있으면 `kContextAcquireTimeoutMs` 까지 기다립니다.
         * @details 렌더 워커와 자원 생성 가드(ScopedOpenGLContext)가 같은 규칙을 쓴다. 한 번만 시도하면 자원 생성 가드가 잡은 짧은 순간에 렌더 스레드가
         *          `[Error]` 를 남기고 그 프레임의 GL 호출을 모두 잃는다.
         */
        [[nodiscard]] bool bindGraphicsContext() override;
        /** @brief 이 스레드에 이미 우리 컨텍스트가 current 인지 묻습니다. ScopedOpenGLContext 가 남의 바인딩을 해제하지 않게 하는 근거입니다. */
        bool isGraphicsContextCurrent() const;
        /** @brief 그래픽스 컨텍스트 바인딩을 해제합니다. */
        void unbindGraphicsContext() override;

        /** @brief 커맨드 리스트를 만듭니다. */
        unique_ptr<IRHICommandList> createCommandList() override;

        /** @brief 커맨드 리스트를 제출합니다. GL 은 기록이 곧 실행이라 즉시 모드에서만 glFlush 합니다. */
        void executeCommandList( IRHICommandList* pCmdList ) override;

    protected:
        // 네이티브 핸들 getter 는 기반처럼 보호 구역이다 — 바깥은 종류 확인이 든 `IRHIDevice::queryNativeHandles` · `tryGetNativeTexturePointer` 로만 읽는다.
        /** @brief 네이티브 GL 텍스처 이름을 포인터 폭으로 넓혀 반환합니다. 없으면 nullptr(이름 0)입니다. */
        void* getNativeTexturePointer( RHITextureHandle texture ) const override { return reinterpret_cast<void*>( static_cast<uintptr_t>( getGlTextureName( texture ) ) ); }

        /** @brief 플랫폼 디바이스 컨텍스트(HDC · Display*)를 반환합니다. */
        void* getNativeDevice() const override { return _pHDC; }

        /** @brief 플랫폼 렌더 컨텍스트(HGLRC · GLXContext)를 반환합니다. */
        void* getNativeContext() const override { return _pHRC; }

        /** @brief OpenGL 에는 커맨드 큐가 없어 nullptr 를 반환합니다. */
        void* getNativeCommandQueue() const override { return nullptr; }

    private:
        /** @brief makeCurrent 를 한 번 시도하고, 성공하면 가진 스레드와 시각을 기록합니다. */
        [[nodiscard]] bool tryMakeCurrentAndRecordOwner();
        /** @brief 타임스탬프 쿼리 객체를 한 번만 만듭니다. */
        void ensureTimestampQueries();
        /** @brief 다시 쓰기 직전의 묶음에서 결과를 마이크로초로 변환합니다(기다리지 않습니다). */
        void collectTimestampsForSlot();
        /**
         * @brief 방금 끝낸 프레임에 펜스(`glFenceSync`)를 내고, GPU 가 지난 프레임까지의 `enqueueGPURelease` 콜백을 부릅니다(`endFrame` 이 부릅니다).
         * @details 링이 가득 차면(프레임 `kMaxFrameCountInFlight` 개가 GPU 에 남음) 가장 오래된 프레임이 끝날 때까지 기다린다 — DX12 · Vulkan 의 링 슬롯 대기와 같다.
         */
        void retireFrameOnGPU();
        /**
         * @brief 방금 낸 전송 뒤에 펜스를 달고(`glFenceSync` + `glFlush`) 시리얼을 발급합니다. GL 컨텍스트를 가진 스레드에서, 전송 명령을 낸 직후에 부릅니다.
         * @details `glFlush` 가 있어야 프레임을 돌리지 않아도 펜스가 신호한다(계약 7). 시리얼은 컨텍스트를 쥔 채 큐에 낸 순서대로 매긴다.
         *          모든 전송(업로드 · 되읽기)은 GPU 에서 끝나므로 펜스 없는 제출은 없다. `glFenceSync` 가 펜스를 주지 않으면 오류를 남기고 장치 손실로 둔다.
         */
        uint64 endTransfer();
        /** @brief 앞에서부터 끝난 전송의 펜스를 놓고 워터마크를 올립니다. 기다리지 않습니다. GL 컨텍스트를 가진 스레드에서만 부릅니다. */
        void retireFinishedTransfers();
        /**
         * @brief 컨텍스트를 기다리지 않고 끝난 전송을 모읍니다. 이 스레드가 이미 쥐었으면 그대로, 비어 있으면 한 번 집어 모으고 놓습니다.
         * @return 모았으면 true, 다른 스레드가 컨텍스트를 쥐고 있어 못 모았으면 false 입니다(그 스레드의 `endFrame` · 제출이 모은다).
         * @details `pollTransfer` 와 링이 찼을 때의 할당이 부른다. 쥔 쪽에만 맡기면 RT 가 프레임을 돌리지 않는 동안(동기 로드) 기다리는 쪽이 시간 초과까지 멈춘다.
         */
        [[nodiscard]] bool tryRetireFinishedTransfers();
        /**
         * @brief 업로드 스테이징 링(영속 매핑 PBO)을 만듭니다. 초기화 끝에 컨텍스트를 쥔 채 부릅니다.
         * @param soloMaxBytes 링보다 큰 업로드 한 장을 일회성 버퍼로 받는 상한입니다(`RHIStagingAllocator::initialize`).
         * @return `GL_ARB_buffer_storage`(GL 4.4)가 없어 링을 만들지 않았어도 true 입니다(그때 업로드는 모두 `Rejected`). 용량 설정이 0 이거나 GL 이 버퍼를 만들지 못하면 false 입니다.
         */
        [[nodiscard]] bool initializeStagingRing( uint64 capacityBytes, uint64 soloMaxBytes );
        /** @brief 링의 매핑을 풀고 버퍼를 지웁니다. 컨텍스트를 쥔 채, 전송이 모두 끝난 뒤(`waitIdle` 다음) 부릅니다. */
        void shutdownStagingRing();
        /**
         * @brief 업로드 바이트 @p sizeBytes 를 담을 링 구간을 잡습니다. 컨텍스트가 없어도 부를 수 있습니다(복사는 컨텍스트 밖에서 한다).
         * @details 링이 차 있으면 끝난 펜스를 한 번 모아(컨텍스트를 잠깐 빌린다) 다시 시도한다 — 프레임을 돌리지 않는 동기 대기(`submitWhileBusy`)도 링을 비우게 한다(계약 7).
         *          링보다 큰 한 장은 장부가 단독 구간을 내주면 그 크기의 일회성 영속 매핑 버퍼를 만든다(`attachSoloBuffer`, 컨텍스트를 잠깐 빌린다).
         * @return 잡았으면 `Accepted`, 지금 차 있으면 `Busy`, 링이 없거나 링과 단독 상한을 모두 넘거나 장치를 잃었으면 이유를 로그로 남기고 `Rejected` 입니다
         *         (옮기고 놓고 남기는 일은 `RHIStagingRingUtil::settleAllocation` 한 곳).
         */
        [[nodiscard]] RHISubmitResult allocateStagingSpan( uint64 sizeBytes, const utf8* pCaller, RHIStagingSpan& outSpan );
        /**
         * @brief 텍스처 되읽기 링(`GL_MAP_READ_BIT` 영속 매핑 PBO)을 만듭니다. 초기화 끝에 컨텍스트를 쥔 채 부릅니다.
         * @param soloMaxBytes 링보다 큰 읽기 한 장을 일회성 버퍼로 받는 상한입니다(`RHIStagingAllocator::initialize`).
         * @return `GL_ARB_buffer_storage`(GL 4.4)가 없어 링을 만들지 않았어도 true 입니다(그때 되읽기는 모두 `Rejected`). 용량 설정이 0 이거나 GL 이 버퍼를 만들지 못하면 false 입니다.
         */
        [[nodiscard]] bool initializeReadbackRing( uint64 capacityBytes, uint64 soloMaxBytes );
        /// @brief 업로드 · 되읽기 링 구간 시작의 정렬입니다. PBO 오프셋은 픽셀 성분(최대 float 4 바이트)과 BC 블록(16 바이트)의 배수면 되지만, CPU 복사가 캐시 줄 경계에서 시작하도록 넉넉히 둔다.
        static constexpr uint64 kTransferRingAlignmentBytes = 256;
        /** @brief 되읽기 링의 버퍼를 지웁니다. `shutdownStagingRing` 과 같은 때에 부릅니다. 가져가지 않은 되읽기의 구간은 버려진다. */
        void shutdownReadbackRing();
        /**
         * @brief 영속 매핑 링 버퍼 하나를 만들고 매핑합니다(`glBufferStorage` + `glMapBufferRange`, 저장소 플래그 = @p mapFlags).
         * @return 매핑 주소입니다. 만들거나 매핑하지 못하면 버퍼를 지우고 nullptr 이며 @p outBuffer 는 0 입니다.
         */
        [[nodiscard]] uint8* createPersistentRingBuffer( uint64 capacityBytes, uint32 mapFlags, uint32& outBuffer );
        /**
         * @brief 되읽기 바이트 @p sizeBytes 를 담을 링 구간을 잡습니다. 컨텍스트가 없어도 부를 수 있습니다.
         * @details 되읽기 구간은 GPU 완료가 아니라 CPU 가 다 읽었을 때(`takeTextureReadback`) 돌아온다. 버린 읽기의 구간만 펜스가 지나면 돌아오므로, 차 있으면 펜스를 한 번 모은 뒤 다시 본다.
         *          링보다 큰 한 장은 업로드와 같이 단독 구간과 일회성 버퍼로 받는다.
         * @return 잡았으면 `Accepted`, 앞선 되읽기를 아직 가져가지 않아 차 있으면 `Busy`, 링이 없거나 링과 단독 상한을 모두 넘거나 장치를 잃었으면 이유를 로그로 남기고 `Rejected` 입니다
         *         (옮기는 곳은 업로드와 같은 `RHIStagingRingUtil::settleAllocation`).
         */
        [[nodiscard]] RHISubmitResult allocateReadbackSpan( uint64 sizeBytes, RHIStagingSpan& outSpan );

        /// @brief 링보다 큰 전송 한 장(단독 구간, `RHIStagingSpan::_bSolo`)이 쓰는 일회성 영속 매핑 버퍼입니다. 링마다 하나다 — 장부가 단독 구간을 한 번에 하나만 내준다.
        struct OpenGLSoloTransferBuffer
        {
            uint8* _pMapped{ nullptr }; ///< 영속 매핑 주소. 버퍼가 없으면 nullptr
            uint32 _buffer{ 0 };        ///< GL 버퍼 이름. 0 이면 없다
        };
        /**
         * @brief 장부가 내준 단독 구간 @p span 에 그 크기의 영속 매핑 버퍼를 만들어 붙입니다. 컨텍스트를 빌린다(이미 쥐었으면 그대로).
         * @details 앞선 단독 구간의 버퍼가 아직 남아 있으면(장부는 이미 돌려받았다) 먼저 지운다. 만든 버퍼는 메모리 장부의 Staging 에 올린다.
         * @return 붙였으면 `Accepted`. 컨텍스트를 얻지 못하면 `Busy`, 버퍼를 만들지 못하면 `Rejected` 이고 두 경우 모두 구간을 장부에 돌려준다.
         */
        [[nodiscard]] RHISubmitResult attachSoloBuffer( RHIStagingAllocator& ring, OpenGLSoloTransferBuffer& rSolo, const RHIStagingSpan& span, uint32 mapFlags,
                                                        const utf8* pCaller );
        /** @brief 단독 버퍼를 지우고 메모리 장부에서 내립니다. 컨텍스트를 쥔 채 부릅니다. 버퍼가 없으면 아무것도 하지 않습니다. */
        void deleteSoloBuffer( OpenGLSoloTransferBuffer& rSolo );
        /** @brief 장부가 단독 구간을 돌려받았으면 그 버퍼를 지웁니다. 컨텍스트를 쥔 채 `retireFinishedTransfers` 가 부릅니다. */
        void deleteIdleSoloBuffer( RHIStagingAllocator& ring, OpenGLSoloTransferBuffer& rSolo, uint64 completedSerial );
        /** @brief 업로드 구간 @p span 의 바이트를 쓸 매핑 주소입니다(단독 구간이면 단독 버퍼의 시작). */
        [[nodiscard]] uint8* getStagingBytes( const RHIStagingSpan& span ) const;
        /** @brief 업로드 구간 @p span 을 담은 GL 버퍼 이름입니다(`GL_PIXEL_UNPACK_BUFFER` 로 건다). */
        [[nodiscard]] uint32 getStagingBuffer( const RHIStagingSpan& span ) const;
        /** @brief 되읽기 구간 @p span 의 바이트를 읽을 매핑 주소입니다(단독 구간이면 단독 버퍼의 시작). */
        [[nodiscard]] const uint8* getReadbackBytes( const RHIStagingSpan& span ) const;
        /** @brief 되읽기 구간 @p span 을 담은 GL 버퍼 이름입니다(`GL_PIXEL_PACK_BUFFER` 로 건다). */
        [[nodiscard]] uint32 getReadbackBuffer( const RHIStagingSpan& span ) const;

        /** @brief 컴퓨트 루트 상수 UBO 를 확보합니다. */
        [[nodiscard]] bool ensureComputeRootConstantUbo();
        /** @brief MRT 합성 FBO 를 확보합니다. */
        uint32 ensureCompositeFboMrt( const RHITextureHandle* pColor, const uint16* pColorSlice, uint32 colorCount, RHITextureHandle depth, uint32 depthSlice );
        /** @brief 불투명 핸들로 GLuint 이름을 찾습니다. */
        uint32 resolveGlBuffer( RHIBufferHandle handle ) const;
        /**
         * @brief GL 버퍼 이름을 테이블에 넣고 핸들을 반환합니다. GPU 메모리 장부의 Buffer 항목에 `sizeBytes` 를 올리는 유일한 지점입니다.
         * @param sizeBytes `glBufferData` 에 준 크기입니다(논리 크기이며, GL 에는 할당 크기를 물을 API 가 없습니다).
         */
        RHIBufferHandle storeGlBuffer( uint32 glName, uint32 sizeBytes );
        struct OpenGLTextureRecord;
        /** @brief 텍스처(의 면 `slice`)를 지금 바인딩된 FBO 의 `attachment` 에 붙입니다. 면이 여럿인 텍스처는 층(layer)으로 붙입니다. */
        static void attachTextureToFramebuffer( uint32 attachment, const OpenGLTextureRecord& record, uint32 slice );
        /** @brief 불투명 텍스처 핸들로 OpenGLTextureRecord 를 찾습니다. */
        OpenGLTextureRecord*       resolveTexture( RHITextureHandle handle );
        const OpenGLTextureRecord* resolveTexture( RHITextureHandle handle ) const;

        /** @brief setComputeRootConstants 의 실제 용량(dword)입니다. 네 백엔드 공통 안전값은
         *         shaderslot::kRootConstantDwords(DX12 와 Vulkan 의 루트 상수, 푸시 상수 크기)입니다. */

        /// @brief 드로우 때 바인딩할 버퍼와 텍스처 슬롯입니다.
        struct BindlessResourceRecord
        {
            RHIBufferHandle  _buffer{ 0 };
            RHITextureHandle _texture{ 0 }; ///< UAV 레지스트리에서 RW 텍스처(이미지 유닛)면 0이 아니다.

            /** @brief `releaseFreeListIndex` 가 "이 슬롯이 비었는가" 를 묻는 데 씁니다. */
            bool operator==( const BindlessResourceRecord& other ) const
            {
                return _buffer == other._buffer && _texture == other._texture;
            }
            bool operator!=( const BindlessResourceRecord& other ) const { return ( *this == other ) == false; }
        };

        /// @brief GLuint 텍스처와 타깃, 포맷입니다.
        struct OpenGLTextureRecord
        {
            uint32              _texture{ 0 };
            uint32              _fbo{ 0 }; ///< 복사 · 블릿용 FBO(컬러 렌더 타깃의 0 번 면). 렌더 패스는 합성 FBO 를 쓴다.
            uint32              _width{ 0 };
            uint32              _height{ 0 };
            uint32              _mipLevels{ 1 };
            RHIFormat           _format = RHIFormat::R8G8B8A8_UNORM;
            uint32              _internalFormat{ 0 }; ///< GL 내부 포맷. glBindImageTexture 가 쓴다.
            uint32              _target{ 0x0DE1 };    ///< GL_TEXTURE_2D, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_CUBE_MAP 중 하나
            uint32              _arraySize{ 1 };      ///< 면 수(배열 원소, 큐브 면)
            RHITextureDimension _dimension{ RHITextureDimension::Texture2D };
            /// 이 텍스처에 마지막으로 제출한 쓰기 전송(업로드)의 시리얼입니다. 0 이면 전송한 적이 없습니다. bindless 등록이 이 전송이 끝났는지 확인합니다(`RHITransferSerials::isTextureReadyToRegister`).
            atomic<uint64> _lastTransferSerial{ 0 };
            uint8          _bDepthStencil : 1;
            uint8          _bUAV          : 1;
            uint8          _reserved      : 6;
        };

        /// @brief MRT FBO 캐시 키입니다.
        struct CompositeFboKey
        {
            RHITextureHandle _arrColor[kMaxColorAttachments]{};
            RHITextureHandle _depth{ 0 };
            uint32           _colorCount{ 0 };
            uint16           _arrColorSlice[kMaxColorAttachments]{}; ///< 첨부마다 붙인 면
            uint16           _depthSlice{ 0 };
            /** @brief 키가 같으면 true 를 반환합니다. */
            bool operator==( const CompositeFboKey& other ) const
            {
                const bool bFboKeyMismatch = _colorCount != other._colorCount || _depth != other._depth || _depthSlice != other._depthSlice;
                if ( bFboKeyMismatch )
                    return false;
                for ( uint32 colorIndex = 0; colorIndex < _colorCount; ++colorIndex )
                {
                    if ( _arrColor[colorIndex] != other._arrColor[colorIndex] || _arrColorSlice[colorIndex] != other._arrColorSlice[colorIndex] )
                        return false;
                }
                return true;
            }
        };

        /// @brief CompositeFboKey 의 해시입니다.
        struct CompositeFboKeyHash
        {
            /** @brief 키의 해시를 반환합니다. */
            size_t operator()( const CompositeFboKey& key ) const
            {
                size_t hash = static_cast<size_t>( key._depth ) * 1315423911u;
                hash ^= static_cast<size_t>( key._colorCount ) + HashUtil::kGoldenRatio32;
                hash ^= static_cast<size_t>( key._depthSlice ) * 2654435761u;
                for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
                {
                    hash ^= static_cast<size_t>( key._arrColor[colorIndex] ) + HashUtil::kGoldenRatio32 + ( hash << 6 ) + ( hash >> 2 );
                    hash ^= static_cast<size_t>( key._arrColorSlice[colorIndex] ) * 2246822519u;
                }
                return hash;
            }
        };

        /// @brief bindless 텍스처 인덱스 하나입니다(인덱스는 목록 위치이고 값은 텍스처 핸들입니다).
        struct BindlessTextureRecord
        {
            RHITextureHandle _texture{ 0 };

            /** @brief `releaseFreeListIndex` 가 "이 슬롯이 비었는가" 를 묻는 데 씁니다. */
            bool operator==( const BindlessTextureRecord& other ) const { return _texture == other._texture; }
            bool operator!=( const BindlessTextureRecord& other ) const { return ( *this == other ) == false; }
        };

        /// @brief 프로그램과 래스터, 블렌드, 깊이 상태입니다.
        struct OpenGLPipelineStateRecord
        {
            uint32               _program{ 0 };
            uint32               _vao{ 0 };
            RHIPrimitiveTopology _topology = RHIPrimitiveTopology::TriangleList;
            RHIFillMode          _fillMode = RHIFillMode::Solid;
            RHICullMode          _cullMode = RHICullMode::None;
            int32                _depthBias{ 0 };               ///< RHIPipelineStateDesc::_depthBias — glPolygonOffset 의 units
            float32              _slopeScaledDepthBias{ 0.0f }; ///< RHIPipelineStateDesc::_slopeScaledDepthBias — glPolygonOffset 의 factor
            uint8                _bEnableDepthTest    : 1;
            uint8                _bEnableDepthWrite   : 1;
            uint8                _bEnableBlend        : 1;
            uint8                _bPremultipliedAlpha : 1; ///< 블렌드 색 원본 계수가 One 이다(아니면 SrcAlpha 이다).
            uint8                _reserved            : 4;
        };

        /** @brief 플랫폼 GL 컨텍스트(WGL, GLX)입니다. 생성, 바인딩, 프레젠트, VSync 를 모두 여기가 압니다. */
        unique_ptr<IOpenGLPlatformContext> _platformContext;

        /** @brief `queryNativeHandles` 의 디바이스 칸에 싣는 복사본입니다. 실제 소유는 _platformContext 가 합니다. */
        void*  _pHDC;
        void*  _pHRC;
        uint32 _width;
        uint32 _height;
        uint32 _vao;
        uint32 _vbo;             ///< 풀스크린 삼각형(정점 3개)
        uint32 _meshVao;         ///< 씬 메시 드로우용 VAO
        uint32 _defaultSampler;  ///< 엔진과 머티리얼 유닛 밖의 모든 텍스처 유닛(최근접, 클램프)
        uint32 _materialSampler; ///< 머티리얼 텍스처 유닛 t16..t31 (선형, 랩, shaderslot::kMaterialTextureSampler)
        uint32 _engineSampler;   ///< 엔진 텍스처 유닛 t0..t3 · t15(G버퍼 머티리얼) (선형, 클램프, shaderslot::kEngineTextureSampler)
        uint32 _defaultTexture;
        /// @brief 지금 컨텍스트를 가진 스레드(없으면 기본값)와 가진 시각입니다. 잡기에 실패했을 때 누가 가지고 있었는지 로그에 적습니다(진단 전용).
        atomic<std::thread::id> _contextOwnerThread;
        atomic<uint64>          _contextOwnedSinceNanos;

        RHIHandleTable<uint32> _gpuBuffers;
        /// @brief 기록 상태입니다. GL 은 실제 상태가 하나라 리스트도 이것을 함께 씁니다(OpenGLRecordingState 참고).
        OpenGLRecordingState _recordingState;

        vector<BindlessResourceRecord> _listRegisteredBindless;
        vector<uint32>                 _listBindlessFree;
        vector<BindlessResourceRecord> _listRegisteredUAV;
        vector<uint32>                 _listUAVFree;

        RHIHandleTable<OpenGLTextureRecord>                         _gpuTextures;
        unordered_map<CompositeFboKey, uint32, CompositeFboKeyHash> _mapCompositeFbo;

        vector<BindlessTextureRecord> _listRegisteredTexture;
        vector<uint32>                _listTextureFree;

        /**
         * @brief GPU 타임스탬프입니다. 프레임 링만큼 쿼리 묶음을 돌려 씁니다.
         * @details 읽기는 그 묶음을 **다시 쓰기 직전**(링 한 바퀴 뒤)에 GL_QUERY_RESULT_AVAILABLE
         *          로 먼저 물어보고 준비된 슬롯만 읽는다. 준비되지 않은 슬롯을 바로 읽으면 GL 이 거기서 막힌다.
         */
        uint32               _arrTimestampQuery[constant::kMaxGPUTimestampSlot * constant::kMaxFrameCountInFlight];
        GPUTimestampSlotMask _arrTimestampMask[constant::kMaxFrameCountInFlight];
        uint32               _timestampFrameIndex;
        RHITimestampFrame    _timestampFrame; ///< 마지막으로 읽은 프레임(`readTimestamps`)

        uint32 _arrComputeRootConstantShadow[shaderslot::kRootConstantDwords];

        RHIHandleTable<OpenGLPipelineStateRecord> _pipelineStates;
        RHIRenderPassLedger                       _renderPassLedger; ///< 렌더 패스 핸들 장부. 자기 뮤텍스로 직렬화해 어느 스레드에서나 부른다.

        RHIReleaseQueue _releaseQueue;
        /// @brief 프레임마다 끝에 내는 펜스 링입니다(`retireFrameOnGPU`). `GLsync` 는 불투명 포인터라 `void*` 로 둔다. 프레임 번호 n 의 칸은 `( n - 1 ) % kMaxFrameCountInFlight` 이고, 비면 nullptr 입니다.
        void*  _arrFrameDoneSync[constant::kMaxFrameCountInFlight];
        uint64 _frameSerial;          ///< `endFrame` 이 끝낸 프레임 수입니다. `enqueueGPURelease` 는 다음 프레임 번호(`_frameSerial + 1`)에 맡깁니다.
        uint64 _completedFrameSerial; ///< GPU 가 지난 것이 확인된 가장 큰 프레임 번호입니다.

        /// @brief 전송 티켓의 시리얼 발급기와 완료 워터마크입니다(GL 은 컨텍스트 하나라 제출 순서가 곧 실행 순서입니다).
        RHITransferSerials _transferSerials;
        /// @brief 제출한 전송마다 펜스(`GLsync`, `void*`)를 제출 순서대로 쌓습니다(`endTransfer` 만 넣는다). 앞에서부터 끝난 것만 워터마크를 올립니다.
        RHITransferFenceQueue<void*> _transferQueue;
        /// @brief 아직 가져가지 않은 텍스처 읽기가 결과를 받는 되읽기 링 구간입니다. 바이트는 펜스가 지난 뒤 `takeTextureReadback` 이 컨텍스트 밖에서 복사한다.
        RHIPendingReadbackTable<RHIStagingSpan> _readbackTable;
        /// @brief 업로드 스테이징 링의 구간 장부입니다. 구간은 전송 시리얼이 워터마크를 지나면 돌아옵니다(`_transferSerials`).
        RHIStagingAllocator _stagingRing;
        /// @brief 링의 영속 매핑 주소입니다(`GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT`). 어느 스레드에서나 장부가 내준 구간에만 씁니다. 링이 없으면 nullptr 입니다.
        uint8* _pStagingRingMapped;
        /// @brief 텍스처 되읽기 링의 구간 장부입니다. 구간은 시리얼이 아니라 `takeTextureReadback`(`free`) · `discardTextureReadback`(`markSubmitted`)으로 돌아옵니다.
        RHIStagingAllocator _readbackRing;
        /// @brief 되읽기 링의 영속 매핑 주소입니다(`GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT`). 펜스가 지난 구간만 읽습니다. 링이 없으면 nullptr 입니다.
        const uint8* _pReadbackRingMapped;
        /// @brief 업로드 링보다 큰 한 장의 일회성 버퍼입니다. 컨텍스트를 쥔 스레드만 만들고 지우며, 장부가 단독 구간을 내준 동안은 바뀌지 않는다.
        OpenGLSoloTransferBuffer _stagingSolo;
        /// @brief 되읽기 링보다 큰 한 장의 일회성 버퍼입니다. 규칙은 `_stagingSolo` 와 같다.
        OpenGLSoloTransferBuffer _readbackSolo;

        sw::unique_ptr<OpenGLRHICommandContext>  _frameStreamContext;
        sw::unique_ptr<OpenGLRHIResourceFactory> _resourceImpl;

        // 8바이트보다 작은 필드는 끝에 모은다. 큰 필드 사이에 끼면 필드마다 패딩이 생긴다.
        uint32 _computeRootConstantUbo;
        uint32 _stagingRingBuffer;  ///< 스테이징 링 PBO 이름. 0 이면 링이 없다(GL 4.4 미만) — 업로드는 Rejected 다
        uint32 _readbackRingBuffer; ///< 되읽기 링 PBO 이름. 0 이면 링이 없다(GL 4.4 미만) — 텍스처 되읽기는 Rejected 다
        int8   _lastVsync;          ///< -1이면 아직 정하지 않았고, 0/1은 마지막으로 적용한 값이다.
        /// @brief 엔진이 켜기 전에는 쿼리도 만들지 않는다. `setTimestampEnabled` 는 어느 스레드에서나 부를 수 있고 렌더 스레드가 읽으므로 atomic 이다.
        atomic<uint8> _bTimestampEnabled;
        /// @brief 쿼리 묶음을 다 만들었다. 컨텍스트를 쥔 스레드가 만들고(release) 다른 스레드의 `getTimestampSlotCount` 가 읽는다(acquire).
        atomic<uint8>          _bTimestampReady;
        uint8                  _bInitialized   : 1;
        uint8                  _bNvxMemoryInfo : 1; ///< GL_NVX_gpu_memory_info 가 있다(총량과 남은 양).
        uint8                  _bAtiMemInfo    : 1; ///< GL_ATI_meminfo 가 있다(남은 양).
        [[maybe_unused]] uint8 _reservedFlags  : 5;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ScopedOpenGLContext
     * @brief 배타적 GL 컨텍스트 바인딩이 필요한 작업 동안 컨텍스트를 가져오고 놓는 RAII 가드입니다.
     * @details 컨텍스트가 **이미 이 스레드에 current 면 아무것도 하지 않는다.** 주의: 무조건 바인딩하고 무조건
     *          해제하면 렌더 스레드(또는 디바이스를 초기화한 스레드)가 잡아 둔 바인딩을 가드 하나가 지워, 뒤이은 가드 없는
     *          glGen* 이 0 을 받는다. 앱에서는 beginFrame 이 매 프레임 다시 바인딩해 가려지고 RHITest 에서만 드러난다.
     */
    struct ScopedOpenGLContext
    {
        OpenGLRHIDevice* const _pDevice; ///< 생성자에서 받는다
        bool                   _bNeedsUnbind{ false };

        explicit ScopedOpenGLContext( OpenGLRHIDevice* pDevice )
            : _pDevice{ pDevice }
            , _bNeedsUnbind{ false }
        {
            const bool bNeedsContextAcquire = _pDevice != nullptr && _pDevice->requiresExclusiveContextThread() && _pDevice->isGraphicsContextCurrent() == false;
            if ( bNeedsContextAcquire )
            {
                // **기다려서** 가져온다. 한 번 시도하고 포기하면 `_bNeedsUnbind` 만 false 가 되고 본문은
                // 그대로 실행되어, 컨텍스트 없이 glGen* 이 나가 리소스가 조용히 만들어지지 않는다.
                // 렌더 워커가 프레임 끝마다 놓으므로 차례를 기다리는 편이 맞다.
                _bNeedsUnbind = _pDevice->acquireGraphicsContextBlocking();
            }
        }

        ~ScopedOpenGLContext()
        {
            if ( _bNeedsUnbind && _pDevice != nullptr )
                _pDevice->unbindGraphicsContext();
        }
    };
} // namespace sw
