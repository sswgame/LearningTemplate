/**
 * @file WebGPURHIDevice.h
 * @brief WebGPU RHI 디바이스입니다(브라우저 — Emscripten emdawnwebgpu, 데스크톱 — Dawn). 바인딩은 DX11 · GL 과 같은 고정 슬롯 에뮬입니다.
 * @details 모델(docs/PlatformTargets.md):
 *          - 셰이더는 WGSL 꾸러미(`WebGPUShaderContainer`)다. 파이프라인은 자동 레이아웃이고, 바인드 그룹 0 은 그리기마다 지금 건 슬롯에서 짓는다
 *            (번호 = `shaderslot::wgpu` 의 띠 + 레지스터 — 셰이더가 쓰는 번호만 든다). 언리얼 · 유니티의 WebGPU 백엔드도 그리기마다 그룹을 짓는다(기억).
 *          - 상수 버퍼는 두 길이다. 팩터리 갱신(프레임 밖)은 그 버퍼에 `wgpuQueueWriteBuffer` 하고, 기록 중 갱신 · 루트 상수는 프레임마다의 상수 링에 새 칸을 받아
 *            바인드 그룹이 그 칸을 가리킨다(DX11 `WRITE_DISCARD` 의 이름 바꾸기와 같은 뜻). 링은 그 프레임을 제출하기 전에 한 번 올린다.
 *          - 완료는 `wgpuQueueOnSubmittedWorkDone` 콜백이 프레임 시리얼을 올린다. 브라우저는 GPU 를 동기로 기다릴 수 없다 — `waitIdleInternal` 은 기다리지 않고
 *            해제 큐를 비운다(WebGPU 는 GPU 가 쓰는 객체를 놓아도 구현이 끝날 때까지 살린다 — 명세). 앞서가기 제한도 브라우저 프레임 콜백이 맡는다.
 *          - 장치는 만든 스레드에서만 쓴다(브라우저 JS 객체). 병렬 기록 · 워커 생성은 없다(`RHIStaticCapabilities::makeWebGPUCapabilities`).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/Backend/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Backend/RHIReleaseQueue.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/RHI/RHIGeometryBindings.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferSerials.h"
#include "Engine/Graphics/RHI/WebGPU/WebGPURHIUtil.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    class WebGPURHICommandContext;
    class WebGPURHICommandList;
    class WebGPURHIDevice;
    class WebGPURHIResourceFactory;

    /** @brief 버퍼 하나입니다. 상수 버퍼는 기록 중 갱신이 상수 링의 칸으로 이름을 바꾼다(`_overrideSerial` 이 지금 프레임이면 그 칸을 건다). */
    struct WebGPUBufferRecord
    {
        WGPUBuffer             _buffer;
        uint64                 _sizeBytes;
        uint32                 _elementSize;
        WGPUBufferUsage        _usage;
        uint64                 _overrideSerial;      ///< 상수 링 칸이 유효한 프레임 시리얼(0 이면 없음)
        WGPUBuffer             _overrideBuffer;      ///< 그 칸의 링 버퍼
        uint64                 _overrideOffset;      ///< 그 칸의 오프셋
        uint8                  _bReadback       : 1; ///< MapRead | CopyDst 읽기 버퍼다(`createReadbackBuffer`)
        uint8                  _bReadbackReady  : 1; ///< 복사한 프레임의 매핑이 끝나 `readReadbackBuffer` 가 읽을 수 있다
        uint8                  _bReadbackQueued : 1; ///< 이번 프레임에 복사 도착지가 됐다 — 제출 뒤 매핑한다
        [[maybe_unused]] uint8 _reserved        : 5;

        WebGPUBufferRecord();
    };

    /** @brief 텍스처 하나입니다. 샘플용 뷰는 밉 · 면 전부(깊이는 깊이 측면만), 첨부용 뷰는 면마다 밉 0 하나입니다. */
    struct WebGPUTextureRecord
    {
        WGPUTexture                         _texture{ nullptr };
        WGPUTextureView                     _sampleView{ nullptr };
        vector<WGPUTextureView>             _listSliceView{};
        RHITextureDesc                      _desc{};
        WGPUTextureFormat                   _wgpuFormat{ WGPUTextureFormat_Undefined };
        unique_ptr<RHITextureUploadSerials> _pUploadSerials{}; ///< 첫 업로드 · 마지막 업로드 시리얼(등록 가능 판정 — 원자값이라 레코드가 움직여도 자리가 그대로이게 따로 둔다)
    };

    /** @brief 파이프라인 하나입니다. 자동 레이아웃의 그룹 0 과 그 그룹이 받는 바인딩 번호(오름차순)를 함께 듭니다. */
    struct WebGPUPipelineRecord
    {
        WGPURenderPipeline  _renderPipeline{ nullptr };
        WGPUComputePipeline _computePipeline{ nullptr };
        WGPUBindGroupLayout _groupLayout{ nullptr }; ///< 셰이더가 자원을 하나도 쓰지 않으면 nullptr 이다(그룹을 걸지 않는다)
        vector<uint32>      _listBinding{};
        RHIVertexLayout     _vertexLayout{};
        uint8               _stencilRef{ 0 };
    };

    /** @brief bindless 번호 하나가 가리키는 것입니다. 에뮬이라 번호는 슬롯에 걸 자원을 되짚는 표의 칸일 뿐이다(DX11 과 같다). */
    struct WebGPUBindlessEntry
    {
        uint64 _handle{ 0 };          ///< 버퍼 또는 텍스처 핸들(0 이면 빈 칸)
        uint8  _bTexture{ SW_FALSE }; ///< 참이면 텍스처, 거짓이면 버퍼
    };

    /** @brief 번호 공간 하나(텍스처 SRV · 버퍼 · UAV)의 표입니다. 빈 칸은 다시 씁니다. */
    struct WebGPUBindlessTable
    {
        vector<WebGPUBindlessEntry> _listEntry{};
        vector<RHIDescriptorIndex>  _listFree{};

        [[nodiscard]] RHIDescriptorIndex add( uint64 handle, bool bTexture );
        /** @brief 칸을 비웁니다. 이미 빈 칸(이중 해제 · 다른 공간의 번호)은 빈 목록에 다시 넣지 않는다 — 넣으면 한 번호가 두 자원에 나간다. */
        void                                     remove( RHIDescriptorIndex index );
        [[nodiscard]] const WebGPUBindlessEntry* find( RHIDescriptorIndex index ) const;
        /** @brief @p handle 을 가리키는 칸을 모두 비웁니다(자원을 부술 때 — 네 백엔드와 같은 계약). */
        void removeHandle( uint64 handle, bool bTexture );
    };

    /** @brief 전송 하나의 완료 표지입니다. 콜백이 켜고 `RHITransferFenceQueue` 가 순서대로 거둡니다. 주소가 콜백의 userdata 라 큐 안에서 움직이지 않게 따로 할당한다. */
    struct WebGPUTransferProbe
    {
        atomic<uint8>    _bDone{ SW_FALSE };
        atomic<uint8>    _bFailed{ SW_FALSE };
        WebGPURHIDevice* _pDevice{ nullptr }; ///< 읽기 매핑 콜백이 결과 표를 찾는 장치(업로드 표지는 쓰지 않는다)
        uint64           _serial{ 0 };        ///< 읽기 매핑 콜백이 결과를 찾는 시리얼
    };

    /**
     * @brief 제출한 프레임 하나의 완료입니다. 큐 완료 콜백이 `_bWorkDone` 을 켜고, 그 프레임에 복사 도착지가 된 읽기 버퍼의 매핑이 모두 끝나야(`_pendingMapCount` 0) 끝난 것으로 본다.
     * @details 매핑까지 기다리는 까닭: `readReadbackBuffer` 는 그 프레임의 지연 해제 콜백 안에서 기다리지 않고 읽는다(장치 계약) — WebGPU 는 매핑 없이 읽을 수 없다.
     */
    struct WebGPUFrameFence
    {
        uint64                  _serial{ 0 };
        atomic<uint8>           _bWorkDone{ SW_FALSE };
        atomic<uint32>          _pendingMapCount{ 0 };
        WebGPURHIDevice*        _pDevice{ nullptr };
        vector<RHIBufferHandle> _listMappedBuffer{}; ///< 이 프레임에 매핑하는 읽기 버퍼(콜백이 다 끝나면 "읽을 수 있음" 으로 적는다)
    };

    /** @brief 읽기 하나의 결과입니다(`submitTextureReadback`). 매핑 콜백이 패딩을 벗긴 바이트를 채운다. */
    struct WebGPUTextureReadback
    {
        WGPUBuffer        _buffer{ nullptr };
        RHITextureMipSpan _layout{};
        uint32            _paddedRowBytes{ 0 };
        vector<uint8>     _bytes{};
        uint8             _bDepth{ SW_FALSE };
        uint8             _bDiscarded{ SW_FALSE }; ///< 매핑이 끝나기 전에 버렸다 — 콜백이 버퍼만 놓고 결과를 남기지 않는다
    };

    /**
     * @class WebGPURHISwapChain
     * @brief 창 하나의 WebGPU 표면입니다. 표면 구성(`wgpuSurfaceConfigure`)이 스왑체인이고 프레임마다 지금 텍스처를 받습니다.
     */
    class WebGPURHISwapChain final : public IRHISwapChain
    {
    public:
        WebGPURHISwapChain();
        ~WebGPURHISwapChain() override;
        WebGPURHISwapChain( const WebGPURHISwapChain& )            = delete;
        WebGPURHISwapChain& operator=( const WebGPURHISwapChain& ) = delete;

        uint32 getWidth() const override { return _width; }
        uint32 getHeight() const override { return _height; }

        /** @brief 창 핸들로 표면을 만듭니다. 표면 종류가 이 빌드에 없으면 이유를 남기고 false 입니다. */
        [[nodiscard]] bool createSurface( WGPUInstance instance, const RHISwapChainDesc& desc );
        /** @brief 표면을 이 크기로 구성합니다. 0 크기(최소화)는 구성을 미룬다. */
        [[nodiscard]] bool configure( WGPUDevice device, uint32 width, uint32 height, bool bVSync );
        /** @brief 이번 프레임의 텍스처를 받습니다. 낡았으면 다시 구성하고 이번 프레임은 false 입니다(그 창의 그리기를 건너뛴다). */
        [[nodiscard]] bool acquire( WGPUDevice device );
        /** @brief 받은 텍스처를 표시합니다(브라우저는 프레임 콜백이 끝나면 스스로 표시한다). */
        void present();
        /** @brief 받은 텍스처와 뷰를 놓습니다(프레임 끝). */
        void releaseCurrent();
        /** @brief 표면을 놓습니다. */
        void destroy();

        WGPUTextureView   getCurrentView() const { return _currentView; }
        WGPUTexture       getCurrentTexture() const { return _currentTexture; }
        WGPUTextureFormat getFormat() const { return _format; }
        RHIFormat         getRHIFormat() const { return _rhiFormat; }

    private:
        WGPUSurface            _surface;
        WGPUTexture            _currentTexture;
        WGPUTextureView        _currentView;
        WGPUTextureFormat      _format;
        RHIFormat              _rhiFormat;
        uint32                 _width;
        uint32                 _height;
        uint8                  _bConfigured : 1;
        uint8                  _bVSync      : 1;
        [[maybe_unused]] uint8 _reserved    : 6;
    };

    /**
     * @class WebGPURHIDevice
     * @brief WebGPU 장치 · 큐 · 표면과 프레임 수명입니다. 자원은 `WebGPURHIResourceFactory`, 기록은 `WebGPURHICommandContext` 가 합니다.
     */
    class WebGPURHIDevice : public IRHIDevice
    {
        friend class WebGPURHICommandContext;
        friend class WebGPURHICommandList;
        friend class WebGPURHIResourceFactory;

    public:
        WebGPURHIDevice();
        ~WebGPURHIDevice() override;

        [[nodiscard]] bool initializeInternal( const RHISwapChainDesc& desc ) override;
        void               shutdownInternal() override;
        void               resizeInternal( uint32 width, uint32 height ) override;
        void               applyVSyncInternal() override;

        void   enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate ) override;
        uint32 getPendingReleaseCount() const override { return _releaseQueue.getPendingReleaseCount(); }

        RHIFormat            getBackBufferFormat() const override { return _swapChain.getRHIFormat(); }
        RHICapabilities      getCapabilities() const override;
        [[nodiscard]] bool   supportsTextureFormat( RHIFormat format ) const override;
        IRHIResourceFactory* getResourceFactory() override;
        IRHICommandContext*  getFrameStreamContext() override;
        RHIBackend           getBackendType() const override { return RHIBackend::WebGPU; }
        bool                 isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const override;
        /** @brief VS 가 인스턴스 구조 버퍼(g_SwInstances, t4)를 읽는다 — WebGPU 의 정점 단계 저장 버퍼는 읽기 전용이면 된다. */
        bool                        supportsInstancedSceneDraw() const override { return true; }
        const utf8*                 getBackendName() const override { return "WebGPU"; }
        unique_ptr<IRHICommandList> createCommandList() override;
        void                        executeCommandList( IRHICommandList* pCmdList ) override;
        void                        executeCommandListImmediate( IRHICommandList* pCmdList ) override;

        /**
         * @brief 장치가 @p format 을 만들 WebGPU 형식입니다. 장치 기능에 따른 대체를 여기서 한다 — R16G16_SNORM 은 tier1 이 없으면 RG16Float(셰이더는 `.rg` 를 [-1, 1] 실수로
         *        읽어 같다), D24S8 은 Depth32FloatStencil8 이 있으면 그것(깊이를 되읽을 수 있다). 못 만드는 형식은 `Undefined` 입니다.
         */
        [[nodiscard]] WGPUTextureFormat toTextureFormat( RHIFormat format ) const;

    protected:
        void                         waitIdleInternal() override;
        void                         detachCommandRecordingInternal() override;
        void*                        getNativeDevice() const override { return _device; }
        void*                        getNativeContext() const override { return _frameEncoder; }
        void*                        getNativeCommandQueue() const override { return _queue; }
        void*                        getNativeTexturePointer( RHITextureHandle texture ) const override;
        [[nodiscard]] bool           queryGPUMemoryBudgetInternal( RHIMemoryBudget& outBudget ) override;
        void                         beginFrameInternal() override;
        void                         endFrameInternal( bool vsync, bool bPresent ) override;
        [[nodiscard]] IRHISwapChain* createSwapChainInternal( const RHISwapChainDesc& desc ) override;
        void                         resizeSwapChainInternal( IRHISwapChain& swapChain, uint32 width, uint32 height ) override;
        void                         destroySwapChainInternal( IRHISwapChain& swapChain ) override;
        [[nodiscard]] bool           acquireSwapChainImageInternal( IRHISwapChain& swapChain ) override;

    private:
        /** @brief 비동기 요청(어댑터 · 장치)을 기다립니다. 브라우저는 ASYNCIFY · JSPI 로 링크해야 이 대기가 된다(cmake/Modules/Platform/Web.cmake). */
        [[nodiscard]] bool waitForFuture( WGPUFuture future, const utf8* pWhat ) const;
        /** @brief 어댑터를 받고 필수 기능 · 한도를 대조해 장치를 만듭니다. 필수 기능이 없으면 `DriverUnsupported` 로 멈춘다. */
        [[nodiscard]] bool createDeviceInternal( const RHISwapChainDesc& desc );
        /** @brief 정적 샘플러 세트(s9..s15)와 빈 슬롯에 걸 1x1 텍스처 · 작은 버퍼를 만듭니다. */
        [[nodiscard]] bool createDefaultResources();
        /** @brief 빈 프레임 인코더를 엽니다(프레임 시작 · 리스트 사이 잘린 뒤). */
        void openFrameEncoder();
        /** @brief 열린 프레임 인코더를 닫아 제출 목록에 넣습니다. */
        void closeFrameEncoder();
        /** @brief 모은 커맨드 버퍼를 제출하고 이 제출의 완료 콜백을 겁니다. 상수 링은 그 전에 올린다. */
        void submitPending();
        /** @brief 이번 프레임에 복사 도착지가 된 읽기 버퍼를 매핑합니다(제출 뒤). 매핑이 끝나야 그 프레임이 끝난 것으로 본다. */
        void mapQueuedReadbacks( uint64 frameSerial );
        /** @brief 완료 콜백 · 매핑 콜백을 처리합니다(네이티브는 이벤트를 돌리고, 브라우저는 이벤트 루프가 이미 불렀다). 해제 큐와 전송 워터마크를 올린다. */
        void processCompletions();
        /** @brief 앞에서부터 끝난 프레임을 거둬 완료 시리얼을 올립니다(`processCompletions`). */
        void retireFinishedFrames();
        /** @brief 큐 완료 콜백입니다(userdata1 = 프레임 완료). */
        static void onFrameWorkDone( WGPUQueueWorkDoneStatus status, void* pUserdata1, void* pUserdata2 );
        /** @brief 읽기 버퍼 매핑 콜백입니다(userdata1 = 프레임 완료). */
        static void onFrameBufferMapped( WGPUMapAsyncStatus status, WGPUStringView message, void* pUserdata1, void* pUserdata2 );
        /** @brief 잡히지 않은 검증 오류를 남깁니다(장치 생성 때 건다). 브라우저는 파이프라인 오류도 여기로 늦게 온다. */
        static void onUncapturedError( const WGPUDevice* pDevice, WGPUErrorType type, WGPUStringView message, void* pUserdata1, void* pUserdata2 );
        /** @brief 장치를 잃었습니다 — 전송을 `Failed` 로 돌리고 프레임을 멈춥니다. */
        static void onDeviceLost( const WGPUDevice* pDevice, WGPUDeviceLostReason reason, WGPUStringView message, void* pUserdata1, void* pUserdata2 );

        /**
         * @brief 상수 링에서 @p size 바이트(256 정렬)를 받아 @p pData 를 담고 그 칸을 돌려줍니다. 링은 이 프레임 제출 전에 올라간다.
         * @details 링은 프레임마다 하나이고 그 프레임이 GPU 에서 끝나면 다시 쓴다. 끝나지 않았으면 새 링을 만든다(브라우저는 기다릴 수 없다).
         */
        void allocateConstantSlice( const void* pData, uint32 size, WGPUBuffer& outBuffer, uint64& outOffset );
        /** @brief @p capacity 바이트의 상수 링을 하나 더 두고 지금 링으로 삼습니다. */
        void appendConstantRing( uint64 capacity );

        /** @brief 핸들의 버퍼 레코드입니다. 없으면 nullptr 입니다. */
        WebGPUBufferRecord*        findBuffer( RHIBufferHandle buffer ) { return _buffers.get( buffer ); }
        WebGPUTextureRecord*       findTexture( RHITextureHandle texture ) { return _textures.get( texture ); }
        const WebGPUTextureRecord* findTexture( RHITextureHandle texture ) const { return _textures.get( texture ); }

        /** @brief 지금 프레임 인코더입니다. 프레임 밖이면 nullptr 입니다. */
        WGPUCommandEncoder getFrameEncoder() const { return _frameEncoder; }
        /** @brief 이번 프레임(다음 제출)의 시리얼입니다. 상수 링 칸 · 지연 해제가 이 값을 쓴다. */
        uint64 getFrameSerial() const { return _frameSerial; }
        /** @brief 살아 있는 리스트를 적습니다(종료 때 분리한다). */
        void registerCommandList( WebGPURHICommandList* pList );
        void unregisterCommandList( WebGPURHICommandList* pList );

    private:
        /** @brief 상수 링 하나입니다. 담을 바이트는 CPU 쪽에 모았다가 제출 전에 한 번 올린다. */
        struct ConstantRing
        {
            WGPUBuffer    _buffer{ nullptr };
            vector<uint8> _staging{};
            uint64        _capacity{ 0 };
            uint64        _used{ 0 };
            uint64        _uploaded{ 0 };     ///< 이미 올린 끝(같은 프레임에 여러 번 제출하면 이어서 올린다)
            uint64        _retireSerial{ 0 }; ///< 이 링을 마지막으로 쓴 프레임 — 그 프레임이 끝나야 다시 쓴다
        };

        WGPUInstance _instance;
        WGPUAdapter  _adapter;
        WGPUDevice   _device;
        WGPUQueue    _queue;
        WGPULimits   _limits;

        WebGPURHISwapChain                     _swapChain;
        vector<unique_ptr<WebGPURHISwapChain>> _listExtraSwapChain;
        vector<WebGPURHISwapChain*>            _listAcquiredSwapChain; ///< 이번 프레임에 그린 스왑체인(endFrame 이 표시한다)

        WGPUCommandEncoder        _frameEncoder;
        vector<WGPUCommandBuffer> _listPendingCommandBuffer;

        unique_ptr<WebGPURHIResourceFactory> _resourceFactory;
        unique_ptr<WebGPURHICommandContext>  _frameStreamContext;

        RHIHandleTable<WebGPUBufferRecord>   _buffers;
        RHIHandleTable<WebGPUTextureRecord>  _textures;
        RHIHandleTable<WebGPUPipelineRecord> _pipelines;
        RHIHandleTable<RHIRenderPassDesc>    _renderPasses;

        WebGPUBindlessTable _textureTable;
        WebGPUBindlessTable _bufferTable;
        WebGPUBindlessTable _uavTable;

        WGPUSampler     _arrStaticSampler[shaderslot::kStaticSamplerArrayCount];
        WGPUTexture     _defaultTexture;
        WGPUTextureView _defaultTextureView;
        WGPUBuffer      _defaultBuffer;

        vector<ConstantRing> _listConstantRing;
        uint32               _activeRingIndex;

        RHIReleaseQueue                                        _releaseQueue;
        RHITransferSerials                                     _transferSerials;
        RHITransferFenceQueue<unique_ptr<WebGPUTransferProbe>> _transferQueue;
        RHIPendingReadbackTable<WebGPUTextureReadback>         _readbackTable;
        vector<RHIBufferHandle>                                _listQueuedReadbackBuffer;

        mutex                         _liveCommandListMutex;
        vector<WebGPURHICommandList*> _listLiveCommandList;

        deque<unique_ptr<WebGPUFrameFence>> _listFrameFence;       ///< 제출했고 아직 거두지 않은 프레임(오래된 것이 앞)
        uint64                              _frameSerial;          ///< 이번 프레임(또는 다음 제출)의 시리얼 — 지연 해제가 이 값을 기다린다
        uint64                              _lastSubmittedSerial;  ///< 마지막으로 제출한 프레임 시리얼
        atomic<uint64>                      _completedFrameSerial; ///< GPU 가 끝낸 마지막 프레임 시리얼(`retireFinishedFrames` 가 올린다)

        uint8 _bInitialized         : 1;
        uint8 _bDeviceLost          : 1;
        uint8 _bTextureFormatsTier1 : 1; ///< rg16snorm 첨부 · 블렌드(`texture-formats-tier1`)
        uint8 _bDepth32Stencil8     : 1; ///< 깊이를 되읽을 수 있는 깊이 · 스텐실 형식
        uint8 _bTextureBC           : 1;
        uint8 _bTextureASTC         : 1;
        uint8 _bTextureETC2         : 1;
        uint8 _bDepthClipControl    : 1; ///< 깊이 클립을 끄는 상태(`unclippedDepth`)
    };
} // namespace sw
