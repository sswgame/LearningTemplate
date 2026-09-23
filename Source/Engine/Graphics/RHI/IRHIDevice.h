/**
 * @file IRHIDevice.h
 * @brief RHI 디바이스 추상화입니다. 프레임 수명주기 · 커맨드 리스트 · 능력 조회 · 네이티브 핸들을 맡습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHICapabilities.h"

namespace sw
{

    class IRenderSurface;
    class IRHICommandContext;
    class IRHICommandList;
    class IRHIResource;

    /**
     * @class IRHIDevice
     * @brief DX11 · DX12 · Vulkan · OpenGL 하드웨어 디바이스 추상화입니다.
     * @details 프레임 수명주기(beginFrame/endFrame/resize), 커맨드 리스트 생성 · 제출, 능력 조회, 네이티브 핸들을
     *          맡습니다. 리소스 생성 · 파괴는 `getResource()` 가 반환하는 `IRHIResource` 가 맡습니다.
     *          기록 스트림은 둘입니다: 디바이스가 소유한 프레임 스트림(`getFrameStreamContext`)과, 패스마다 만드는
     *          커맨드 리스트(`createCommandList`)입니다.
     */
    class SW_API IRHIDevice
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 — initialize/shutdown, resize
        // ------------------------------------------------------------------------------
        /** @brief 가상 소멸자입니다. */
        virtual ~IRHIDevice();
        /** @brief 빈 디바이스로 만듭니다. initialize 전에 setRenderSurface 를 부릅니다. */
        IRHIDevice();
        /** @brief 복사를 금지합니다. */
        IRHIDevice( const IRHIDevice& ) = delete;
        /** @brief 대입을 금지합니다. */
        IRHIDevice& operator=( const IRHIDevice& ) = delete;

        /** @brief 표면(`setRenderSurface`)에서 스왑체인 서술(크기 · 백버퍼 3 개 · 포맷 · VSync)을 채워 `initializeInternal` 을 부릅니다. */
        virtual bool initialize();
        /** @brief 디바이스를 종료합니다. 자원을 든 쪽에는 내리기 전에 알립니다. */
        virtual void shutdown();

        /** @brief RHI 디바이스와 스왑체인을 초기화합니다. */
        virtual bool initializeInternal( const RHISwapChainDesc& desc ) = 0;

        /** @brief 디바이스를 종료하고 관련 리소스를 정리합니다. */
        virtual void shutdownInternal() = 0;

        /** @brief 백버퍼(스왑체인)를 새 크기로 다시 만듭니다. `resize` 가 크기를 적은 뒤 부릅니다. */
        virtual void resizeInternal( uint32 width, uint32 height ) = 0;

        /** @brief GPU 에 제출된 작업이 모두 끝날 때까지 기다립니다. */
        virtual void waitIdle() {}

        // ------------------------------------------------------------------------------
        // 2) 능력 · 스레드 — 백엔드 종류, bindless, 컨텍스트 소유
        // ------------------------------------------------------------------------------
        /**
         * @brief 이 디바이스가 실제로 채택한 백버퍼 컬러 포맷입니다.
         * @details 기본은 계약값 `constant::kBackBufferFormat` 입니다. 하드웨어 · 서피스 제약으로 그 값을
         *          낼 수 없는 백엔드(Vulkan)만 override 해서 실제 값을 보고합니다. 조용히 다른 포맷을
         *          쓰면 백버퍼를 타깃으로 하는 PSO 가 모두 렌더 패스 비호환이 됩니다.
         */
        virtual RHIFormat getBackBufferFormat() const { return constant::kBackBufferFormat; }

        /** @brief 백엔드 능력을 조회합니다.
         * @note 정적 표는 RHIAvailability::query 입니다. DX12 · Vulkan 의 네이티브 bindless 는
         *       supportsNativeBindlessSampling() · getCapabilities() override 가 런타임에 확정합니다.
         */
        virtual RHICapabilities getCapabilities() const { return RHIAvailability::query( getBackendType() ); }
        /**
         * @brief 프레임 기록을 열고 백버퍼를 준비합니다.
         * @details 예전에는 이 셋(beginFrame · endFrame · resize)이 `IRHISwapChain` 에 있었습니다. 그런데 스왑체인 구현 넷 중 셋은
         *          디바이스로 그대로 넘기기만 했고, DX12 만 내용이 있었는데 그 내용이 모두
         *          디바이스의 private 멤버를 만지는 것이라 `friend` 가 필요했습니다. 분리가 아니라
         *          분리의 반대였습니다. 프레임 수명주기는 디바이스의 일이므로 여기로 올렸습니다.
         * @note 스왑체인 자체(백버퍼 · 이미지 인덱스 · 동기화 · present)는 백엔드 안의 구체 클래스
         *       `<백엔드>RHISwapChain` 이 소유합니다. 가상 인터페이스로 되돌리지 않습니다(Graphics/README.md). GL 에는 없습니다.
         */
        virtual void beginFrame( const float4& clearColor ) = 0;
        /** @brief 기록을 닫고 큐에 제출합니다. bPresent=false 면 제출만 하고 Present 는 생략합니다. */
        virtual void endFrame( bool vsync = true, bool bPresent = true ) = 0;

        /**
         * @brief 타임스탬프 계측을 켜고 끕니다. **끄면 백엔드는 아무 자원도 만들지 않고 아무것도 읽지 않습니다.**
         * @details 계측은 공짜가 아닙니다. 쿼리 힙 · 풀, 프레임마다의 리셋 · resolve · 읽기가 따라붙습니다.
         *          그래서 "잴 사람이 있을 때만" 켭니다. **정책은 엔진의 것이고 여기는 메커니즘만 둡니다.**
         *          백엔드는 별도 모듈이라 엔진 전역(프로파일러)을 볼 수 없습니다.
         *          켠 다음 프레임부터 슬롯이 열립니다(자원을 그때 만듭니다).
         */
        virtual void setTimestampEnabled( bool bEnabled ) { (void)bEnabled; }

        /**
         * @brief 이번 프레임에 쓸 수 있는 타임스탬프 슬롯 수입니다. 0 이면 이 백엔드 · 드라이버가 지원하지 않습니다.
         * @details 슬롯은 프레임마다 0 부터 다시 씁니다. 기록은 `IRHICommandList::writeTimestamp` 가 합니다.
         */
        virtual uint32 getTimestampSlotCount() const { return 0; }

        /**
         * @brief **이미 끝난 프레임**의 타임스탬프를 마이크로초로 읽습니다(프레임 시작 기준 누적).
         * @return 읽을 것이 있으면 true. GPU 가 아직 안 끝냈으면 false 이고, 다음 프레임에 다시 물으면 됩니다.
         * @details 결과는 몇 프레임 늦습니다. **기다리지 않습니다.** 기다리면 재려던 그 파이프라인을
         *          멈춰 세워 숫자가 거짓이 됩니다.
         *          이번 프레임에 적히지 않은 슬롯은 **음수**로 옵니다. 백엔드마다 안 적은 칸에 남는
         *          것이 다르기 때문입니다(DX12 · DX11 은 지난 사이클 값, Vulkan · GL 은 미가용).
         *          부르는 쪽은 음수가 하나라도 낀 구간을 통째로 버려야 합니다.
         */
        virtual bool readTimestampsMicros( vector<float32>& outListMicro )
        {
            outListMicro.clear();
            return false;
        }
        /**
         * @brief 백버퍼 크기를 바꿉니다. 채택된 크기는 `getBackBufferWidth/Height` 가 답합니다.
         * @details 크기를 여기 적어 두는 이유: 렌더러가 첨부 크기를 정할 때 **창에 묻지 않고 디바이스에 묻게**
         *          하려는 것입니다. 렌더러가 보는 것은 스왑체인이지 OS 창이 아닙니다(언리얼 `FRHIViewport` 의 자리).
         *          예전에는 `FrameRenderer` 가 `IWindow::getActiveWindow()` 전역을 읽었고, 그래서 Graphics 가
         *          Window 를 include 했습니다.
         */
        void resize( uint32 width, uint32 height );
        /** @brief 백버퍼 너비입니다. initialize 때는 표면 크기이고, 그 뒤로는 마지막 `resize` 값입니다. */
        uint32 getBackBufferWidth() const { return _backBufferWidth; }
        /** @brief 백버퍼 높이입니다. initialize 때는 표면 크기이고, 그 뒤로는 마지막 `resize` 값입니다. */
        uint32                getBackBufferHeight() const { return _backBufferHeight; }
        virtual IRHIResource* getResource() { return nullptr; }

        /**
         * @brief 디바이스가 소유한 **프레임 스트림**에 기록하는 컨텍스트입니다.
         * @details beginFrame/endFrame 이 여는 디바이스 커맨드 리스트(버퍼)에 그대로 기록합니다.
         *          RenderThread 가 백버퍼 렌더 패스를 여는 경로가 이것입니다. 패스별 기록은 자기
         *          네이티브 버퍼를 소유하는 IRHICommandList 가 따로 합니다.
         *          예전에는 Immediate/Deferred 두 슬롯이 있었지만 모드 구분이 사라져 스트림은 하나입니다.
         */
        virtual IRHICommandContext* getFrameStreamContext() = 0;

        /** @brief 현재 RHI 백엔드 종류를 반환합니다. */
        virtual RHIBackend getBackendType() const = 0;

        /**
         * @brief 셰이더가 디스크립터 인덱스로 텍스처를 샘플링할 수 있는지 반환합니다(DX12 텍스처 배열 테이블 · Vulkan 디스크립터 인덱싱).
         * @note false 면 CPU 가 그 인덱스로 슬롯을 바인딩해야 합니다(DX11 · GL 에뮬레이션).
         */
        virtual bool supportsNativeBindlessSampling() const { return false; }

        /** @brief beginRenderPass 에서 컬러 RT 여러 개를 동시에 바인딩할 수 있는지 반환합니다(MRT G버퍼 등). */
        virtual bool supportsMultiRenderTarget() const { return true; }

        /**
         * @brief 그래픽스 VS 가 GPUScene 인스턴스 구조버퍼(SwInstanceData)를 읽을 수 있으면 true 를 반환합니다.
         * @details true 면 FrameRenderer 가 인스턴스 버퍼를 걸고 배치를 drawIndirect(멀티 드로우)로 그리며, 인스턴스마다의
         *          월드 · materialIndex 는 구조버퍼에서 읽습니다. false 면 씬 메시를 그릴 수 없습니다. 드로우당 g_World 폴백 경로는 없습니다.
         */
        virtual bool supportsInstancedSceneDraw() const { return false; }

        /**
         * @brief 드로우 · Present 컨텍스트를 소유 스레드 하나에서만 써야 하면 true 를 반환합니다.
         * @details DX11 즉시 컨텍스트와 OpenGL(wgl/egl MakeCurrent)이 그렇습니다. DX12 · Vulkan 은 false 입니다.
         */
        virtual bool requiresExclusiveContextThread() const { return false; }

        /**
         * @brief 부르는 스레드에 그래픽스 컨텍스트를 붙입니다(RenderThread 진입).
         * @details OpenGL 은 wglMakeCurrent · glXMakeCurrent 입니다. DX11 은 소유 표시만 합니다(컨텍스트에
         *          MakeCurrent 가 없어, 다른 스레드에서 부르지 않는 것이 곧 배타성입니다).
         *          DX12 · Vulkan 은 할 일이 없습니다.
         */
        virtual bool bindGraphicsContext() { return true; }

        /** @brief 스레드 바인딩을 풉니다(OpenGL 은 MakeCurrent(null), DX11 은 소유 표시 해제). */
        virtual void unbindGraphicsContext() {}

        /** @brief 백엔드 이름 문자열을 반환합니다. */
        virtual const utf8* getBackendName() const = 0;

        // ------------------------------------------------------------------------------
        // 3) 네이티브 핸들 — 디바이스/컨텍스트/스왑체인/큐
        // ------------------------------------------------------------------------------
        /** @brief 네이티브 디바이스 포인터(ID3D12Device, VkDevice 등)를 반환합니다. */
        virtual void* getNativeDevice() const = 0;

        /** @brief 네이티브 컨텍스트 포인터(ID3D11DeviceContext, EGLContext 등)를 반환합니다. */
        virtual void* getNativeContext() const = 0;

        /** @brief 네이티브 커맨드 큐 포인터를 반환합니다. */
        virtual void* getNativeCommandQueue() const = 0;

        /**
         * @brief RHI 텍스처의 백엔드 네이티브 텍스처 이름(OpenGL GLuint 등)을 반환합니다. 지원하지 않으면 0 입니다.
         * @note 에디터 MODULE 이 RHI_* 디바이스 MODULE 의 구체 타입에 링크하지 않도록 가상 함수로 둡니다.
         */
        virtual uint32 getNativeTextureName( RHITextureHandle texture ) const
        {
            (void)texture;
            return 0;
        }

        /**
         * @brief 네이티브 텍스처 리소스 포인터(D3D12 ID3D12Resource*, D3D11 ID3D11Texture2D* 등)를 반환합니다. 지원하지 않으면 nullptr 입니다.
         * @note 에디터 MODULE 이 RHI_* 디바이스 MODULE 의 구체 타입에 링크하지 않도록 가상 함수로 둡니다.
         */
        virtual void* getNativeTexturePointer( RHITextureHandle texture ) const
        {
            (void)texture;
            return nullptr;
        }

        /** @brief 스왑체인을 걸 표면을 저장합니다. `initialize` 가 핸들·크기를 여기서 읽습니다. */
        void setRenderSurface( IRenderSurface* pSurface ) { _pSurface = pSurface; }
        /** @brief CLI 에 --VSYNC 가 없을 때 쓸 스왑체인 VSync 를 정합니다. `initialize` 전에 부릅니다. */
        void setPreferredVSync( bool bVSync ) { _bPreferredVSync = bVSync; }
        /**
         * @brief 실제로 채택된 VSync 값입니다(설정값 → CLI `--VSYNC` 순으로 정해집니다).
         * @details **프레젠트 경로가 읽어야 하는 값이 이것입니다.** 예전에는 `RenderThread` 가
         *          `endFrame( true )` 를 못박고 있어서 설정도 CLI 도 아무 효과가 없었습니다.
         *          `RHISwapChainDesc::_bVSync` 는 채워지기만 하고 아무도 읽지 않는 필드였고,
         *          그래서 `_bVSync: false` 설정으로도 프레임이 모니터 주사율에 묶여 있었습니다.
         */
        bool isVSyncEnabled() const { return _bPreferredVSync; }
        // ------------------------------------------------------------------------------
        // 4) 커맨드 리스트 — 생성, 그래픽스 스레드에서만 execute
        // ------------------------------------------------------------------------------

        /** @brief 독립 커맨드 리스트를 만듭니다(기록 전용이고, GPU 에 넘기는 것은 executeCommandList 입니다). */
        virtual unique_ptr<IRHICommandList> createCommandList() = 0;

        /**
         * @brief 커맨드 리스트를 **프레임 스트림 순서에 맞춰** 제출 대기열에 넣습니다.
         * @details beginFrame~endFrame 사이에서 씁니다. 디바이스는 이 지점에서 프레임 스트림을 잘라
         *          [지금까지의 세그먼트][이 리스트][새 세그먼트] 순서로 잇고 endFrame 에서 한 번에
         *          큐로 넘깁니다. 같은 큐의 제출 순서가 곧 실행 순서입니다. 그래픽스 실행 스레드 전용입니다.
         */
        virtual void executeCommandList( IRHICommandList* pCmdList ) = 0;

        /**
         * @brief 프레임 스트림과 무관하게 **곧바로** 큐에 제출합니다.
         * @details 프레임 밖 일회성 작업(오프스크린 스모크, 리소스 업로드, 썸네일 렌더 등)용입니다.
         *          프레임 순서 보장이 필요 없고 beginFrame 이 열려 있지 않은 경우에만 쓸 것.
         *          프레임 중에 부르면 스트림 순서를 건너뛰므로 렌더 결과가 어긋납니다.
         *          기본 구현은 executeCommandList 로 넘깁니다(DX11/GL 처럼 기록이 곧 실행인 백엔드).
         */
        virtual void executeCommandListImmediate( IRHICommandList* pCmdList ) { executeCommandList( pCmdList ); }

        /**
         * @brief 커맨드 리스트가 제출될 때마다 프레임 스트림을 **곧바로** 큐로 내보낼지 정합니다.
         * @details 기본(false)은 프레임 끝에 한 번에 제출합니다. 켜면 executeCommandList 마다 큐 제출이
         *          한 번씩 일어나 오버헤드가 크지만, 두 모드 모두 [세그먼트][리스트] 순서를 지키므로
         *          실행 순서는 같습니다. 달라지는 것은 제출 '시점'뿐입니다. GPU 오류(DEVICE_HUNG, 검증 레이어)가
         *          어느 제출에서 났는지 좁힐 때 씁니다.
         * @note 백엔드마다 "즉시" 가 가리키는 것이 다릅니다. DX12/Vulkan 은 모아 둔 커맨드 리스트를 그
         *       자리에서 큐로 제출하고, DX11/GL 은 애초에 기록 스트림이 곧 제출 스트림이라 순서는
         *       이미 맞으므로 `Flush`/`glFlush` 로 GPU 에 밀어내기만 합니다. 어느 쪽이든 효과는 같습니다.
         *       리스트 경계에서 GPU 작업이 끊깁니다.
         * @note 값의 출처는 Engine 의 `gv_rhiImmediateSubmit` 이고, RenderThread 가 프레임마다 밀어 넣습니다.
         *       백엔드는 MODULE DLL 로 따로 빌드되므로 전역 변수를 그쪽까지 export 하지 않습니다.
         *       정책은 Engine 이 정하고 디바이스는 메커니즘만 갖습니다.
         */
        void setImmediateSubmit( bool bEnable ) { _bImmediateSubmit = bEnable; }

        /**
         * @brief 지금 여러 스레드가 동시에 패스를 기록하는 구간인지 알립니다.
         * @details **이 구간에서는 bindless 레지스트리를 바꿀 수 없습니다.** 레지스트리는 기록 중에
         *          드로우마다 읽히는데, 그 사이에 register/unregister 가 resize 를 일으키면 읽는
         *          쪽이 잡아 둔 참조가 dangling 이 되고 GPU 가 쓰레기 디스크립터를 읽습니다.
         *
         *          그래서 등록은 모두 기록 **밖**(그래프 셋업 · 리소스 로드)에서 끝냅니다. 그러면
         *          기록 중 레지스트리는 불변이라 읽기에 락이 필요 없습니다. 락을 잘 거는 대신 애초에
         *          공유하지 않는 쪽을 택한 것이고, 상용 엔진(UE RDG)도 같은 방식입니다.
         * @note 이 플래그는 그 규칙을 **감시**하기 위한 것입니다. 디버그 빌드에서 규칙이 깨지면
         *       백엔드가 로그로 알립니다. 규칙이 조용히 썩는 것을 막는 것이 목적입니다.
         * @note 값의 출처는 `RenderGraph::executeParallel` 입니다. 백엔드는 MODULE DLL 로 따로
         *       빌드되므로 전역 변수를 export 하지 않습니다. 정책은 Engine, 메커니즘은 디바이스입니다.
         */
        void setParallelRecording( bool bEnable ) { _bParallelRecording = bEnable; }

        /**
         * @brief 지금 리소스 테이블(bindless 레지스트리 · 텍스처 레코드)을 바꿔도 되는 시점인지 확인합니다.
         * @details 백엔드의 register/unregister 진입부에서 부릅니다. 규칙이 깨지면 로그를 남기고
         *          **디버거를 세웁니다**(`SW_LOG_ASSERT`). 로그만으로는 다른 줄에 묻혀 지나칩니다.
         *          Release 에서는 제거됩니다.
         * @param pWhat 로그에 남길 호출 지점 이름.
         */
        void assertRegistryMutableNow( const utf8* pWhat ) const;

        /**
         * @brief 병렬 기록 중에 배리어가 나왔음을 **진단으로만** 남깁니다(디버그 전용, 8회까지).
         * @details 배리어는 웨이브 프롤로그(`RenderGraph::setWavePrologue`)가 단일 스레드에서 미리
         *          발행하는 것이 원칙입니다. 다만 프롤로그는 그래프가 **선언한** 입출력만 알 수 있어서
         *          선언이 빠진 자원(뎁스 첨부가 대표적)은 놓칩니다. 그래서 이것은 assert 가 아닙니다.
         *          실제 전이는 락이 보호하므로 남아 있어도 안전하고, 여기 찍히는 이름이 곧
         *          "파이프라인 선언을 채우면 사라질 자리" 입니다.
         * @note bindless 레지스트리 쪽(`assertRegistryMutableNow`)은 불변식이 확실해서 assert 입니다.
         *       두 감시의 격이 다른 이유가 이것입니다.
         */
        void noteBarrierDuringRecording( const utf8* pWhat ) const;

    protected:
        IRenderSurface* _pSurface;
        uint32          _backBufferWidth;
        uint32          _backBufferHeight;
        bool            _bPreferredVSync;
        /// @brief 프레임 스트림을 자를 때마다 곧바로 제출할지입니다(setImmediateSubmit 참고).
        bool _bImmediateSubmit;
        /// @brief 지금이 병렬 패스 기록 구간인지입니다(setParallelRecording 참고).
        bool _bParallelRecording;
    };
} // namespace sw
