/**
 * @file IRHIDevice.h
 * @brief RHI 커맨드 리스트 기록과 디바이스 추상화
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHICapabilities.h"

namespace sw
{
    class IRHICommandContext;
    class IRHICommandList;
    class IRHIResource;
    class IWindow;
    class RenderPassManager;

    /**
     * @class IRHIDevice
     * @brief DX11/DX12/Vulkan/OpenGL 하드웨어 디바이스 추상화
     * @details Context(제출/기록 슬롯)와 CommandList Mode(언제 replay/flush할지)는 별개다.
     *          프레임 스트림 컨텍스트 = present/offscreen/Deferred CL replay 대상.
     *          Deferred Context = Mode=Deferred일 때 CL 바인딩용(soft 또는 네이티브).
     */
    class SW_API IRHIDevice
    {
    public:
        // ------------------------------------------------------------------------------
        // 8) 수명 — initialize/shutdown, resize
        // ------------------------------------------------------------------------------
        /** @brief 가상 소멸. */
        virtual ~IRHIDevice();
        /** @brief 빈 디바이스. initialize 전에 setInitWindow. */
        IRHIDevice();
        /** @brief 복사를 금지합니다. */
        IRHIDevice( const IRHIDevice& ) = delete;
        /** @brief 대입을 금지합니다. */
        IRHIDevice& operator=( const IRHIDevice& ) = delete;

        /** @brief 스왑체인 서술로 디바이스를 초기화합니다. */
        virtual bool initialize();
        /** @brief 디바이스와 렌더 패스 매니저를 종료합니다. */
        virtual void shutdown();

        /** @brief RHI 디바이스와 스왑체인을 초기화합니다. */
        virtual bool initializeInternal( const RHISwapChainDesc& desc ) = 0;

        /** @brief 디바이스를 종료하고 관련 리소스를 정리합니다. */
        virtual void shutdownInternal() = 0;

        /** @brief GPU의 대기 중인 작업을 모두 끝낼 때까지 기다립니다. */
        virtual void waitIdle() {}

        /** @brief 스왑체인과 백버퍼 크기를 바꿉니다. */
        // ------------------------------------------------------------------------------
        // 9) 오프스크린 검증 — Present 없는 파이프라인 smoke
        // ------------------------------------------------------------------------------
        /**
         * @brief Present 없이 오프스크린 RT로 파이프라인을 검증합니다.
         * @details createTexture2D → beginRenderPass → setPSO → drawFullscreen → destroy.
         * @return 성공 시 true. pso==0 이면 false.
         */
        bool executeOffscreenPipelineSmoke( RHIPipelineStateHandle pso,
                                            RHIDescriptorIndex     materialCb = kInvalidDescriptorIndex,
                                            uint32                 width      = 64,
                                            uint32                 height     = 64 );

        // ------------------------------------------------------------------------------
        // 10) 능력 · 스레드 — 백엔드 종류, bindless, 컨텍스트 소유
        // ------------------------------------------------------------------------------
        /**
         * @brief 이 디바이스가 실제로 채택한 백버퍼 컬러 포맷.
         * @details 기본은 계약값 `constant::kBackBufferFormat` 이다. 하드웨어/서피스 제약으로 그 값을
         *          낼 수 없는 백엔드(Vulkan)만 override 해서 실제 값을 보고한다 — 조용히 다른 포맷을
         *          쓰면 백버퍼를 타깃으로 하는 PSO 가 전부 렌더패스 비호환이 된다.
         */
        virtual RHIFormat getBackBufferFormat() const { return constant::kBackBufferFormat; }

        /** @brief 백엔드 capability를 조회합니다.
         * @note 정적 표는 RHIAvailability::query. DX12/VK native bindless는
         *       supportsNativeBindlessSampling() / override getCapabilities()가 런타임 확정.
         */
        virtual RHICapabilities getCapabilities() const { return RHIAvailability::query( getBackendType() ); }
        /**
         * @brief 프레임 기록을 열고 백버퍼를 준비합니다.
         * @details 예전에는 이 셋이 `IRHISwapChain` 에 있었다. 그런데 스왑체인 구현 넷 중 셋은
         *          디바이스로 그대로 넘기기만 했고, DX12 만 내용이 있었는데 그 내용이 전부
         *          디바이스의 private 멤버를 만지는 것이라 `friend` 가 필요했다 — 분리가 아니라
         *          분리의 반대였다. 프레임 수명주기는 디바이스의 일이므로 여기로 올린다.
         * @note 스왑체인 자체(이미지·포맷·present)를 별도 객체로 소유하게 만드는 것은 별개 과제다.
         *       그건 인터페이스를 acquire/present 로 바꾸는 설계 변경이라 같이 하면 안 된다.
         */
        virtual void beginFrame( const float4& clearColor ) = 0;
        /** @brief 기록을 닫고 큐에 제출합니다. bPresent=false 면 제출만 하고 Present 는 생략합니다. */
        virtual void endFrame( bool vsync = true, bool bPresent = true ) = 0;
        /** @brief 백버퍼 크기를 바꿉니다. */
        virtual void          resize( uint32 width, uint32 height ) = 0;
        virtual IRHIResource* getResource() { return nullptr; }

        /**
         * @brief 디바이스가 소유한 **프레임 스트림**에 기록하는 컨텍스트.
         * @details beginFrame/endFrame 이 여는 디바이스 커맨드 리스트(버퍼)에 그대로 기록한다 —
         *          RenderThread 가 백버퍼 렌더패스를 여는 경로가 이것이다. 패스별 기록은 자기
         *          네이티브 버퍼를 소유하는 IRHICommandList 가 따로 한다.
         *          예전엔 Immediate/Deferred 두 슬롯이 있었지만 모드 구분이 사라져 스트림은 하나다.
         */
        virtual IRHICommandContext* getFrameStreamContext() = 0;

        /** @brief 현재 RHI 백엔드 종류를 반환합니다. */
        virtual RHIBackend getBackendType() const = 0;

        /** @brief 현재 백엔드가 Bindless(무제한 리소스 배열)를 지원하는지 반환합니다. */
        virtual bool supportsBindless() const = 0;

        /**
         * @brief GPU 셰이더가 디스크립터 인덱스로 텍스처를 샘플링하는지 (DX12 힙 / VK indexing).
         * @note false면 CPU가 그 인덱스로 슬롯을 바인딩해야 합니다 (DX11/GL 에뮬레이션).
         */
        virtual bool supportsNativeBindlessSampling() const { return false; }

        /** @brief beginRenderPass에서 다중 컬러 RT를 동시에 바인딩할 수 있는지 (MRT GBuffer 등). */
        virtual bool supportsMultiRenderTarget() const { return true; }

        /**
         * @brief 그래픽스 VS 가 GPUScene 인스턴스 구조버퍼(SwInstanceData)를 읽을 수 있으면 true.
         * @details true 면 FrameRenderer 가 배치당 draw 대신 drawInstanced 로 그리고 per-instance world 를
         *          구조버퍼에서 읽습니다. false 면 드로우당 g_World 를 갱신하는 폴백 경로를 씁니다.
         */
        virtual bool supportsInstancedSceneDraw() const { return false; }

        /**
         * @brief 드로우/Present 컨텍스트를 소유 스레드 하나에서만 써야 하면 true.
         * @details DX11 immediate context + OpenGL (wgl/egl MakeCurrent). DX12/Vulkan: false.
         */
        virtual bool requiresExclusiveContextThread() const { return false; }

        /**
         * @brief 호출 스레드에 그래픽스 컨텍스트를 붙입니다 (RenderThread 진입).
         * @details OpenGL: wglMakeCurrent / glXMakeCurrent. DX11: 소유 표시만 (컨텍스트 자체에
         *          MakeCurrent 없음 — 다른 스레드에서 호출하지 않는 것이 배타성).
         *          DX12/Vulkan: no-op.
         */
        virtual bool bindGraphicsContext() { return true; }

        /** @brief 스레드 바인딩을 해제합니다 (OpenGL MakeCurrent(null); DX11은 소유 표시 해제). */
        virtual void unbindGraphicsContext() {}

        /** @brief 백엔드 이름과 버전 포맷 문자열을 반환합니다. */
        virtual const utf8* getBackendName() const = 0;

        // ------------------------------------------------------------------------------
        // 11) 네이티브 핸들 — 디바이스/컨텍스트/스왑체인/큐, ImGui Vulkan
        // ------------------------------------------------------------------------------
        /** @brief 네이티브 디바이스 포인터 (ID3D12Device, VkDevice 등). */
        virtual void* getNativeDevice() const = 0;

        /** @brief 네이티브 컨텍스트 포인터 (ID3D11DeviceContext, EGLContext 등). */
        virtual void* getNativeContext() const = 0;

        /** @brief 네이티브 스왑체인 포인터. */
        virtual void* getNativeSwapChain() const = 0;

        /** @brief 네이티브 커맨드 큐 포인터. */
        virtual void* getNativeCommandQueue() const = 0;

        /**
         * @brief RHI 텍스처 → 백엔드 native texture name (OpenGL GLuint 등). 미지원 시 0.
         * @note Editor MODULE이 RHI_* device MODULE concrete 타입에 링크하지 않도록 가상화.
         */
        virtual uint32 getNativeTextureName( RHITextureHandle texture ) const
        {
            (void)texture;
            return 0;
        }

        /**
         * @brief 네이티브 텍스처 리소스 포인터 (D3D12 ID3D12Resource*, D3D11 ID3D11Texture2D* 등). 미지원 시 nullptr.
         * @note Editor MODULE이 RHI_* device MODULE concrete 타입에 링크하지 않도록 가상화.
         */
        virtual void* getNativeTexturePointer( RHITextureHandle texture ) const
        {
            (void)texture;
            return nullptr;
        }

        /**
         * @brief Vulkan ImGui 초기화용 native 핸들. 비-Vulkan이면 false.
         */
        virtual bool queryVulkanImGuiNative( RHIVulkanImGuiNative& out ) const
        {
            (void)out;
            return false;
        }

        /**
         * @brief Vulkan ImGui 텍스처 등록용 VkImageView. 비-Vulkan/미존재면 false.
         */
        virtual bool queryVulkanTextureView( RHITextureHandle texture, void*& pOutImageView ) const
        {
            (void)texture;
            pOutImageView = nullptr;
            return false;
        }

        /** @brief initializeInternal에 넘길 윈도우를 저장합니다. */
        void setInitWindow( IWindow* pWindow ) { _pInitWindow = pWindow; }
        /** @brief CLI에 --VSYNC가 없을 때 쓸 스왑체인 VSync입니다. */
        void setPreferredVSync( bool bVSync ) { _bPreferredVSync = bVSync; }
        /** @brief 디바이스가 소유한 RenderPassManager를 반환합니다. */
        RenderPassManager& getRenderPassManager() const;

        // ------------------------------------------------------------------------------
        // 14) 커맨드 리스트 — 생성, 그래픽스 스레드에서만 execute
        // ------------------------------------------------------------------------------

        /** @brief 독립 커맨드 리스트를 만듭니다 (기록 전용; GPU 적용은 executeCommandList). */
        virtual unique_ptr<IRHICommandList> createCommandList() = 0;

        /**
         * @brief 커맨드 리스트를 **프레임 스트림 순서에 맞춰** 제출 대기열에 넣습니다.
         * @details beginFrame~endFrame 사이에서 쓴다. 디바이스는 이 지점에서 프레임 스트림을 잘라
         *          [지금까지의 세그먼트][이 리스트][새 세그먼트] 순서로 잇고 endFrame 에서 한 번에
         *          큐로 넘긴다 — 같은 큐의 제출 순서가 곧 실행 순서다. 그래픽스 실행 스레드 전용.
         */
        virtual void executeCommandList( IRHICommandList* pCmdList ) = 0;

        /**
         * @brief 프레임 스트림과 무관하게 **곧바로** 큐에 제출합니다.
         * @details 프레임 밖 일회성 작업(오프스크린 스모크, 리소스 업로드, 썸네일 렌더 등)용이다.
         *          프레임 순서 보장이 필요 없고 beginFrame 이 열려 있지 않은 경우에만 쓸 것 —
         *          프레임 중 호출하면 스트림 순서를 건너뛰므로 렌더 결과가 어긋난다.
         *          기본 구현은 executeCommandList 로 위임한다(DX11/GL 처럼 기록이 곧 실행인 백엔드).
         */
        virtual void executeCommandListImmediate( IRHICommandList* pCmdList ) { executeCommandList( pCmdList ); }

        /**
         * @brief 커맨드 리스트가 제출될 때마다 프레임 스트림을 **곧바로** 큐로 내보낼지 지정합니다.
         * @details 기본(false)은 프레임 끝에 한 번에 제출한다. 켜면 executeCommandList 마다 큐 제출이
         *          한 번씩 일어나 오버헤드가 크지만, 두 모드 모두 [세그먼트][리스트] 순서를 지키므로
         *          실행 순서는 같다 — 달라지는 건 제출 '시점'뿐이다. GPU 오류(DEVICE_HUNG, 검증 레이어)가
         *          어느 제출에서 났는지 좁힐 때 쓴다.
         * @note 백엔드별로 "즉시"가 가리키는 것이 다르다. DX12/Vulkan 은 모아둔 커맨드 리스트를 그
         *       자리에서 큐로 제출하고, DX11/GL 은 애초에 기록 스트림이 곧 제출 스트림이라 순서는
         *       이미 맞으므로 `Flush`/`glFlush` 로 GPU 에 밀어내기만 한다. 어느 쪽이든 효과는 같다 —
         *       리스트 경계에서 GPU 작업이 끊긴다.
         * @note 값의 출처는 Engine 의 `gv_rhiImmediateSubmit` 이고, RenderThread 가 프레임마다 밀어넣는다.
         *       백엔드는 MODULE DLL 로 따로 빌드되므로 전역 변수를 그쪽까지 export 하지 않는다 —
         *       정책은 Engine 이 정하고 디바이스는 메커니즘만 갖는다.
         */
        void setImmediateSubmit( bool bEnable ) { _bImmediateSubmit = bEnable; }

        /**
         * @brief 지금 여러 스레드가 동시에 패스를 기록하는 구간인지 알립니다.
         * @details **이 구간에서는 bindless 레지스트리를 바꿀 수 없다.** 레지스트리는 기록 중에
         *          드로우마다 읽히는데, 그 사이에 register/unregister 가 resize 를 일으키면 읽는
         *          쪽이 잡아 둔 참조가 dangling 이 되고 GPU 가 쓰레기 디스크립터를 읽는다.
         *
         *          그래서 등록은 전부 기록 **밖**(그래프 셋업 · 리소스 로드)에서 끝낸다. 그러면
         *          기록 중 레지스트리는 불변이라 읽기에 락이 필요 없다 — 락을 잘 거는 대신 애초에
         *          공유하지 않는 쪽을 택한 것이고, 상용 엔진(UE RDG)도 같은 방식이다.
         * @note 이 플래그는 그 규칙을 **감시**하기 위한 것이다. 디버그 빌드에서 규칙이 깨지면
         *       백엔드가 로그로 알린다 — 규칙이 조용히 썩는 것을 막는 것이 목적이다.
         * @note 값의 출처는 `RenderGraph::executeParallel` 이다. 백엔드는 MODULE DLL 로 따로
         *       빌드되므로 전역 변수를 export 하지 않는다 — 정책은 Engine, 메커니즘은 디바이스.
         */
        void setParallelRecording( bool bEnable ) { _bParallelRecording = bEnable; }

        /** @brief setParallelRecording 참고. 백엔드가 규칙 위반을 감지하는 데 쓴다. */
        bool isParallelRecording() const { return _bParallelRecording; }

        /**
         * @brief 지금 bindless 레지스트리를 바꿔도 되는 시점인지 확인하고, 아니면 디버거를 세웁니다.
         * @details 백엔드의 register/unregister 진입부에서 부른다. 규칙이 깨지면 로그를 남기고
         *          **디버거를 세운다**(`SW_LOG_ASSERT`) — 로그만으로는 다른 줄에 묻혀 지나친다.
         *          Release 에서는 제거된다.
         * @param pWhat 로그에 남길 호출 지점 이름.
         */
        void checkRegistryMutableNow( const utf8* pWhat ) const;

        /**
         * @brief 병렬 기록 중에 배리어가 나왔음을 **진단으로만** 남깁니다 (디버그 전용, 8회까지).
         * @details 배리어는 웨이브 프롤로그(`RenderGraph::setWavePrologue`)가 단일 스레드에서 미리
         *          발행하는 것이 원칙이다. 다만 프롤로그는 그래프가 **선언한** 입출력만 알 수 있어서
         *          선언이 빠진 자원(뎁스 첨부가 대표적)은 놓친다 — 그래서 이건 assert 가 아니다.
         *          실제 전이는 락이 보호하므로 남아 있어도 안전하고, 여기 찍히는 이름이 곧
         *          "파이프라인 선언을 채우면 사라질 자리" 다.
         * @note bindless 레지스트리 쪽(`checkRegistryMutableNow`)은 불변식이 확실해서 assert 다.
         *       두 감시의 격이 다른 이유가 이것이다.
         */
        void noteBarrierDuringRecording( const utf8* pWhat ) const;

    protected:
        IWindow*                      _pInitWindow;
        unique_ptr<RenderPassManager> _renderPassManager;
        bool                          _bPreferredVSync;
        /// @brief setImmediateSubmit 참고 — 프레임 스트림을 자를 때마다 즉시 제출할지.
        bool _bImmediateSubmit{ false };
        /// @brief setParallelRecording 참고 — 지금이 병렬 패스 기록 구간인가.
        bool _bParallelRecording{ false };
    };
} // namespace sw
