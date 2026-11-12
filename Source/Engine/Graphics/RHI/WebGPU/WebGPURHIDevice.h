/**
 * @file WebGPURHIDevice.h
 * @brief WebGPU RHI 디바이스입니다(브라우저 — Emscripten emdawnwebgpu, 데스크톱 — Dawn). 바인딩은 DX11 · GL 과 같은 고정 슬롯 에뮬입니다.
 * @details 모델(docs/PlatformTargets.md):
 *          - 셰이더는 WGSL 꾸러미(`WebGPUShaderContainer`)다. 파이프라인은 자동 레이아웃이고, 바인드 그룹 0 은 그리기마다 지금 건 슬롯으로 정한다
 *            (번호 = `shaderslot::wgpu` 의 띠 + 레지스터 — 셰이더가 쓰는 번호만 든다). 같은 (레이아웃, 항목) 의 그룹은 프레임 안에서 한 번만 짓는다(`WebGPUBindGroupCache`).
 *          - 깊이 텍스처는 WGSL 에서 `texture_depth_*` 로만 읽힌다 — 엔진 셰이더는 깊이를 실수 텍스처로 읽으므로, 셰이더가 읽는 깊이 텍스처는 R32 실수 사본을 함께 두고
 *            `prepareTextureForShaderRead` 가 사본을 채운다(언리얼 모바일의 SceneDepthAux 와 같은 자리, 기억). 샘플 뷰는 그 사본이다.
 *          - 타임스탬프는 `timestamp-query` 기능이 있을 때만이다. WebGPU 는 패스 경계에서만 시각을 적는다 — 패스 밖 지점은 빈 계산 패스의 시작 시각이다.
 *          - 상수 버퍼는 두 길이다. 팩터리 갱신(프레임 밖)은 그 버퍼에 `wgpuQueueWriteBuffer` 하고, 기록 중 갱신 · 루트 상수는 프레임마다의 상수 링에 새 칸을 받아
 *            바인드 그룹이 그 칸을 가리킨다(DX11 `WRITE_DISCARD` 의 이름 바꾸기와 같은 뜻). 링은 그 프레임을 제출하기 전에 한 번 올린다.
 *          - 완료는 `wgpuQueueOnSubmittedWorkDone` 콜백이 프레임 시리얼을 올린다. 브라우저는 GPU 를 동기로 기다릴 수 없다 — `waitIdleInternal` 은 기다리지 않고
 *            해제 큐를 비운다(WebGPU 는 GPU 가 쓰는 객체를 놓아도 구현이 끝날 때까지 살린다 — 명세). 앞서가기 제한도 브라우저 프레임 콜백이 맡는다.
 *          - 장치는 만든 스레드에서만 쓴다(브라우저 JS 객체). 병렬 기록 · 워커 생성은 없다(`RHIStaticCapabilities::makeWebGPUCapabilities`).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/Backend/RHIBindlessRegistry.h"
#include "Engine/Graphics/RHI/Backend/RHIFrameFenceRing.h"
#include "Engine/Graphics/RHI/Backend/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Backend/RHIReleaseQueue.h"
#include "Engine/Graphics/RHI/Backend/RHISwapChainSet.h"
#include "Engine/Graphics/RHI/Backend/RHITimestampRing.h"
#include "Engine/Graphics/RHI/Diagnostics/RHITimestampFrame.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
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
        unique_ptr<RHITextureUploadSerials> _pUploadSerials{};              ///< 첫 업로드 · 마지막 업로드 시리얼(등록 가능 판정 — 원자값이라 레코드가 움직여도 자리가 그대로이게 따로 둔다)
        WGPUTexture                         _depthSampleTexture{ nullptr }; ///< 셰이더가 읽는 깊이 텍스처의 R32 실수 사본(깊이가 아니거나 셰이더가 읽지 않으면 nullptr). `_sampleView` 가 이 사본의 뷰다
        vector<WGPUTextureView>             _listDepthCopyTarget{};         ///< 면마다 사본의 첨부 뷰(밉 0)
        vector<WGPUTextureView>             _listDepthReadView{};           ///< 면마다 원본의 깊이 측면 2D 뷰(밉 0) — 사본 패스가 읽는다
        vector<WGPUBindGroup>               _listDepthCopyGroup{};          ///< 면마다 사본 패스의 그룹 0(`_listDepthReadView` 하나)
        uint64                              _depthCopySerial{ 0 };          ///< 사본을 마지막으로 채운 프레임 시리얼 — 다른 프레임의 사본을 읽으면 오류로 남긴다
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

        bool operator==( const WebGPUBindlessEntry& other ) const { return _handle == other._handle && _bTexture == other._bTexture; }
        /** @brief 자원을 부술 때 이 칸을 찾는 주인입니다(`RHIBindlessRegistry::releaseBuffer` · `releaseTexture`). */
        RHIBindlessOwner getBindlessOwner() const { return _bTexture != SW_FALSE ? RHIBindlessOwner{ 0, _handle } : RHIBindlessOwner{ _handle, 0 }; }
    };

    /** @brief 번호 공간 하나(텍스처 SRV · 버퍼 · UAV)의 표입니다. 기록 때 번호를 자원으로 풀므로 비운 번호는 바로 다시 쓴다(`RHIBindlessReuse::Immediately`). */
    using WebGPUBindlessTable = RHIBindlessRegistry<WebGPUBindlessEntry>;

    /**
     * @class WebGPUBindGroupCache
     * @brief 프레임 안의 바인드 그룹 캐시입니다. 키는 (레이아웃, 항목 전부)이고, 같은 키의 그리기는 지은 그룹을 다시 겁니다.
     * @details 프레임마다 비운다 — 자원은 지연 해제라 프레임 안에서는 항목의 네이티브 핸들이 다른 객체로 다시 쓰이지 않는다(그래서 핸들 값이 곧 정체다).
     *          프레임을 넘겨 들고 있으면 놓인 자원의 핸들 값이 새 객체에 다시 나갈 수 있다.
     */
    class WebGPUBindGroupCache
    {
    public:
        WebGPUBindGroupCache();

        /** @brief @p layout · @p listEntry 의 그룹입니다. 없으면 지어 넣습니다(지은 그룹은 캐시가 가진다 — 부르는 쪽은 놓지 않는다). 짓지 못하면 nullptr 입니다. */
        [[nodiscard]] WGPUBindGroup findOrCreate( WGPUDevice device, WGPUBindGroupLayout layout, const vector<WGPUBindGroupEntry>& listEntry );
        /** @brief 지은 그룹을 모두 놓습니다(프레임 시작 · 종료). 기록한 커맨드는 그룹을 구현이 끝날 때까지 살린다(명세). */
        void   clear();
        uint32 getCreatedCount() const { return _createdCount; }
        uint32 getHitCount() const { return _hitCount; }

        /** @brief 항목 목록의 해시입니다(레이아웃 포함). 장치 없이 돌아 단위 테스트가 본다. */
        [[nodiscard]] static uint64 makeKeyHash( WGPUBindGroupLayout layout, const vector<WGPUBindGroupEntry>& listEntry );
        /** @brief 두 항목 목록이 같은 자원을 같은 번호 · 범위로 거는지입니다(구조체 패딩을 비교하지 않으려고 칸마다 본다). */
        [[nodiscard]] static bool isSameEntryList( const vector<WGPUBindGroupEntry>& listA, const vector<WGPUBindGroupEntry>& listB );

    private:
        struct CachedGroup
        {
            WGPUBindGroupLayout        _layout{ nullptr };
            vector<WGPUBindGroupEntry> _listEntry{};
            WGPUBindGroup              _group{ nullptr };
        };

        unordered_map<uint64, vector<CachedGroup>> _mapGroup;
        uint32                                     _createdCount; ///< 이번 프레임에 지은 그룹 수(진단)
        uint32                                     _hitCount;     ///< 이번 프레임에 다시 건 수(진단)
    };

    /**
     * @brief 프레임 칸 하나의 타임스탬프 되읽기 버퍼입니다(`RHIFrameFenceRing` 칸마다 하나 — 다른 백엔드의 칸마다 쿼리 묶음과 같은 자리).
     * @details 매핑은 그 칸의 프레임 완료(`WebGPUFrameFence::_pendingMapCount`)에 든다 — 칸을 다시 쓰는 `beginFrame` 에서는 매핑이 끝나 있다.
     *          적은 슬롯 마스크는 `RHITimestampRing` 이 칸마다 든다.
     */
    struct WebGPUTimestampReadback
    {
        WGPUBuffer    _buffer{ nullptr };
        uint8         _bQueued{ SW_FALSE }; ///< 이 칸에 풀이 · 복사 · 매핑을 냈다(렌더 스레드만)
        atomic<uint8> _bMapped{ SW_FALSE }; ///< 매핑이 성공했다(콜백이 켠다)
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
     * @brief 제출한 프레임 하나의 완료입니다. 큐 완료 콜백이 `_bJobDone` 을 켜고, 그 프레임에 복사 도착지가 된 읽기 버퍼의 매핑이 모두 끝나야(`_pendingMapCount` 0) 끝난 것으로 본다.
     * @details 매핑까지 기다리는 까닭: `readReadbackBuffer` 는 그 프레임의 지연 해제 콜백 안에서 기다리지 않고 읽는다(장치 계약) — WebGPU 는 매핑 없이 읽을 수 없다.
     */
    struct WebGPUFrameFence
    {
        uint64                  _serial{ 0 };
        atomic<uint8>           _bJobDone{ SW_TRUE }; ///< 빈 칸은 끝난 것이다(`RHIFrameFenceRing` 은 낸 적 없는 칸을 기다리지 않는다)
        atomic<uint32>          _pendingMapCount{ 0 };
        WebGPURHIDevice*        _pDevice{ nullptr };
        vector<RHIBufferHandle> _listMappedBuffer{}; ///< 이 프레임에 매핑하는 읽기 버퍼(콜백이 다 끝나면 "읽을 수 있음" 으로 적는다)
        WGPUFuture              _jobDoneFuture{};    ///< 큐 완료 콜백의 퓨처 — 링이 차면 `wgpuInstanceWaitAny` 로 기다린다
        vector<WGPUFuture>      _listMapFuture{};    ///< 매핑 콜백의 퓨처(같은 대기)

        /** @brief 이 칸에 새 프레임을 겁니다. 칸의 앞 프레임은 끝났어야 합니다(링이 기다린다). */
        void arm( uint64 serial, WebGPURHIDevice* pDevice )
        {
            _serial = serial;
            _bJobDone.store( SW_FALSE, std::memory_order_release );
            _pendingMapCount.store( 0, std::memory_order_release );
            _pDevice = pDevice;
            _listMappedBuffer.clear();
            _jobDoneFuture = WGPUFuture{};
            _listMapFuture.clear();
        }
        /** @brief 칸을 빈 상태(끝남)로 되돌립니다(장치를 놓을 때). */
        void clear()
        {
            _bJobDone.store( SW_TRUE, std::memory_order_release );
            _pendingMapCount.store( 0, std::memory_order_release );
            _listMappedBuffer.clear();
            _jobDoneFuture = WGPUFuture{};
            _listMapFuture.clear();
        }
        /** @brief 큐 완료와 매핑이 모두 끝났는지 봅니다. */
        bool isDone() const { return _bJobDone.load( std::memory_order_acquire ) != SW_FALSE && _pendingMapCount.load( std::memory_order_acquire ) == 0; }
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

        /** @brief 창 핸들로 표면을 만듭니다. @p adapter 는 표면 능력을 물을 어댑터다(장치가 들고 있다 — 웹 헤더에는 장치에서 어댑터를 꺼내는 함수가 없다). 표면 종류가 이 빌드에 없으면 이유를 남기고 false 입니다. */
        [[nodiscard]] bool createSurface( WGPUInstance instance, WGPUAdapter adapter, const RHISwapChainDesc& desc );
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
        WGPUAdapter            _adapter; ///< 표면 능력을 묻는 어댑터 — 장치 소유, 여기서 놓지 않는다
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
        /** @brief @p deviceGeneration 은 등록표가 붙인 이 장치의 세대입니다(전송 티켓에 싣는다). */
        explicit WebGPURHIDevice( uint32 deviceGeneration );
        ~WebGPURHIDevice() override;

        [[nodiscard]] bool initializeInternal( const RHISwapChainDesc& desc ) override;
        void               shutdownInternal() override;
        void               resizeInternal( uint32 width, uint32 height ) override;
        void               applyVSyncInternal() override;

        void   enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate ) override;
        uint32 getPendingReleaseCount() const override { return _releaseQueue.getPendingReleaseCount(); }
        /** @brief 장치 손실 표시를 든 전송 시리얼입니다(`IRHIDevice::isDeviceLost` 가 읽는다 — 손실 콜백이 따로 들던 비트필드는 다른 스레드가 쓰는 경쟁이라 없앴다). */
        const RHITransferSerials& getTransferSerials() const override { return _transferSerials; }

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

        /** @brief 타임스탬프 계측을 켜고 끕니다(`IRHITimestampProvider` — 어느 스레드에서나 부른다). 쿼리 세트는 켠 뒤 첫 프레임 시작에 만든다. */
        void setTimestampEnabled( bool bEnabled ) { _bTimestampEnabled.store( bEnabled ? SW_TRUE : SW_FALSE, std::memory_order_relaxed ); }
        /** @brief 켰고 쿼리 세트가 있으면 `kMaxGPUTimestampSlot`, 아니면 0 입니다(`timestamp-query` 가 없는 어댑터는 늘 0). */
        uint32 getTimestampSlotCount() const;
        /** @brief 마지막으로 매핑이 끝난 프레임의 타임스탬프입니다. 기다리지 않는다. */
        [[nodiscard]] bool readTimestamps( RHITimestampFrame& outFrame );

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
        /** @brief 정적 샘플러 세트(s9..s15)와 빈 슬롯에 걸 1x1 텍스처 · 작은 버퍼, 깊이 사본 파이프라인을 만듭니다. */
        [[nodiscard]] bool createDefaultResources();
        /** @brief 깊이 → R32 실수 사본 파이프라인입니다(백엔드 내장 WGSL — 깊이 텍스처를 `textureLoad` 해 그대로 쓴다). */
        [[nodiscard]] bool createDepthCopyPipeline();
        /**
         * @brief 셰이더가 읽는 깊이 텍스처 @p record 에 R32 실수 사본 · 면마다의 사본 패스 그룹을 만들고 샘플 뷰를 사본으로 둡니다(`createTexture2D`).
         * @details 언리얼 모바일의 SceneDepthAux 처럼 사본이 셰이더 쪽 깊이다(기억). 원본은 깊이 첨부 · 되읽기로만 쓴다.
         */
        [[nodiscard]] bool createDepthSampleCopy( WebGPUTextureRecord& record );
        /** @brief @p record 의 깊이를 사본으로 옮기는 렌더 패스를 면마다 @p encoder 에 적습니다(`prepareTextureForShaderRead`). 열린 패스가 없어야 한다. */
        void encodeDepthSampleCopy( WGPUCommandEncoder encoder, WebGPUTextureRecord& record );
        /** @brief 깊이 사본 자원의 놓기를 지연 해제 큐에 넣습니다(`destroyTexture`). */
        void releaseDepthSampleCopy( WebGPUTextureRecord& record );
        /** @brief 켰는데 쿼리 세트가 없으면 만듭니다(프레임 시작). 기능이 없으면 아무것도 하지 않는다. */
        void ensureTimestampQuerySet();
        /** @brief 이번 프레임에 적은 칸을 굳혀(`RHITimestampRing::closeFrame`) 풀고 이 프레임 칸의 되읽기 버퍼로 복사합니다(프레임 인코더를 닫기 전). */
        void resolveTimestamps();
        /** @brief 이번 프레임 칸의 되읽기 버퍼를 매핑하고, 매핑을 그 프레임 완료(@p pFence)에 넣습니다(제출 뒤). */
        void mapTimestampReadback( WebGPUFrameFence* pFence );
        /** @brief 칸 @p ringSlot 의 앞 프레임 결과를 읽어 `_timestampFrame` 에 풀고 칸을 비웁니다(`beginFrame` — 그 프레임은 끝났다). */
        void collectTimestampReadback( uint32 ringSlot );
        /** @brief 빈 계산 패스의 시작 시각을 @p slotIndex 에 적습니다(`WebGPURHICommandList::writeTimestamp`). 쿼리 세트가 없으면 아무것도 하지 않는다. */
        void writeTimestampSlot( WGPUCommandEncoder encoder, uint32 slotIndex );
        /** @brief 타임스탬프 되읽기 매핑 콜백입니다(userdata1 = 프레임 완료, userdata2 = 되읽기 버퍼). */
        static void onTimestampMapped( WGPUMapAsyncStatus status, WGPUStringView message, void* pUserdata1, void* pUserdata2 );
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
        /** @brief 칸 @p fence 의 큐 완료 · 매핑을 `wgpuInstanceWaitAny` 로 기다립니다(앞서가기 상한 — `beginFrameInternal`). 잃은 장치도 콜백은 온다(명세). */
        [[nodiscard]] bool waitFrameFence( const WebGPUFrameFence& fence ) const;
        /** @brief 큐 완료 콜백입니다(userdata1 = 프레임 완료). */
        static void onFrameJobsDone( WGPUQueueWorkDoneStatus status, WGPUStringView message, void* pUserdata1, void* pUserdata2 );
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
        /** @brief @p table 의 @p index 칸 값을 공유 잠금 아래에서 읽습니다(빈 칸이면 빈 값). 값으로 돌려준다 — 포인터는 다른 스레드의 등록이 표를 늘리면 매달린다. */
        WebGPUBindlessEntry findBindless( const WebGPUBindlessTable& table, RHIDescriptorIndex index ) const;
        /** @brief 이번 프레임(다음 제출)의 시리얼입니다. 상수 링 칸 · 지연 해제가 이 값을 쓴다. 어느 스레드에서나 읽는다. */
        uint64 getFrameSerial() const { return _frameFenceRing.getRecordingSerial(); }
        /** @brief 살아 있는 리스트를 적습니다(종료 때 분리한다). */
        void registerCommandList( WebGPURHICommandList* pList );
        void unregisterCommandList( WebGPURHICommandList* pList );

    private:
        /** @brief `IRHITimestampProvider` 어댑터입니다(장치가 인터페이스를 상속하지 않는다 — 네 백엔드와 같은 모양). */
        class TimestampProvider final : public IRHITimestampProvider
        {
        public:
            explicit TimestampProvider( WebGPURHIDevice& owner )
                : _owner{ owner }
            {
            }

            void               setTimestampEnabled( bool bEnabled ) override { _owner.setTimestampEnabled( bEnabled ); }
            uint32             getTimestampSlotCount() const override { return _owner.getTimestampSlotCount(); }
            [[nodiscard]] bool readTimestamps( RHITimestampFrame& outFrame ) override { return _owner.readTimestamps( outFrame ); }
            /** @brief WebGPU 에는 지금 GPU 시계를 묻는 길이 없다 — 외부 타임라인(Tracy)은 열지 않고 엔진 표(`GPU.<패스>`)만 채운다. */
            [[nodiscard]] bool readGPUClockNanos( int64& outGPUNanos ) override
            {
                outGPUNanos = 0;
                return false;
            }
            bool isGPUClockReadCheap() const override { return false; }

        private:
            WebGPURHIDevice& _owner;
        };

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

        WebGPURHISwapChain                  _swapChain;
        RHISwapChainSet<WebGPURHISwapChain> _windowSwapChains; ///< 다른 창과 이번 프레임에 그린 창(endFrame 이 표시한다)

        WGPUCommandEncoder        _frameEncoder;
        vector<WGPUCommandBuffer> _listPendingCommandBuffer;

        unique_ptr<WebGPURHIResourceFactory> _resourceFactory;
        unique_ptr<WebGPURHICommandContext>  _frameStreamContext;

        RHIHandleTable<WebGPUBufferRecord>   _buffers;
        RHIHandleTable<WebGPUTextureRecord>  _textures;
        RHIHandleTable<WebGPUPipelineRecord> _pipelines;
        RHIHandleTable<RHIRenderPassDesc>    _renderPasses;

        /// @brief 세 번호 공간을 지킨다. 등록은 게임 스레드, 읽기는 기록 스레드 · 진단(`isBindlessIndexLive`)이라 읽기는 공유 잠금이다.
        mutable shared_mutex _bindlessMutex;
        WebGPUBindlessTable  _textureTable;
        WebGPUBindlessTable  _bufferTable;
        WebGPUBindlessTable  _uavTable;

        WGPUSampler     _arrStaticSampler[shaderslot::kStaticSamplerArrayCount];
        WGPUTexture     _defaultTexture;
        WGPUTextureView _defaultTextureView;
        WGPUBuffer      _defaultBuffer;

        WGPURenderPipeline   _depthCopyPipeline;    ///< 깊이 → R32 실수 사본(`createDepthCopyPipeline`)
        WGPUBindGroupLayout  _depthCopyGroupLayout; ///< 그 파이프라인의 그룹 0(깊이 텍스처 하나)
        WebGPUBindGroupCache _bindGroupCache;

        TimestampProvider       _timestampProvider;
        WGPUQuerySet            _timestampQuerySet;                                   ///< `kMaxGPUTimestampSlot` 칸(켠 뒤 첫 프레임에 만든다)
        WGPUBuffer              _timestampResolveBuffer;                              ///< QueryResolve | CopySrc — 풀이 결과를 되읽기 버퍼로 옮긴다
        WebGPUTimestampReadback _arrTimestampReadback[RHIFrameFenceRing::kSlotCount]; ///< 칸마다 되읽기 버퍼(콜백 userdata 라 자리가 움직이지 않는 고정 배열)
        RHITimestampRing        _timestampRing;                                       ///< 이번 프레임 · 칸마다 적은 슬롯
        RHITimestampFrame       _timestampFrame;                                      ///< 마지막으로 읽은 프레임
        atomic<uint8>           _bTimestampEnabled;

        vector<ConstantRing> _listConstantRing;
        uint32               _activeRingIndex;

        RHIReleaseQueue                                        _releaseQueue;
        RHITransferSerials                                     _transferSerials;
        RHITransferFenceQueue<unique_ptr<WebGPUTransferProbe>> _transferQueue;
        RHIPendingReadbackTable<WebGPUTextureReadback>         _readbackTable;
        vector<RHIBufferHandle>                                _listQueuedReadbackBuffer;

        mutex                         _liveCommandListMutex;
        vector<WebGPURHICommandList*> _listLiveCommandList;

        /// @brief 칸마다의 프레임 완료(`RHIFrameFenceRing::getSlot`). 콜백 userdata 라 자리가 움직이지 않는 고정 배열이다.
        WebGPUFrameFence  _arrFrameFence[RHIFrameFenceRing::kSlotCount];
        RHIFrameFenceRing _frameFenceRing; ///< 프레임 시리얼 · 완료 워터마크 · 앞서가기 상한(해제 키는 `getRecordingSerial`)

        uint8 _bInitialized         : 1;
        uint8 _bTextureFormatsTier1 : 1; ///< rg16snorm 첨부 · 블렌드(`texture-formats-tier1`)
        uint8 _bDepth32Stencil8     : 1; ///< 깊이를 되읽을 수 있는 깊이 · 스텐실 형식
        uint8 _bTextureBC           : 1;
        uint8 _bTextureASTC         : 1;
        uint8 _bTextureETC2         : 1;
        uint8 _bDepthClipControl    : 1; ///< 깊이 클립을 끄는 상태(`unclippedDepth`)

        uint8                  _bTimestampQuery : 1; ///< 어댑터가 `timestamp-query` 를 준다(선택 기능)
        [[maybe_unused]] uint8 _reservedFeature : 7;
    };
} // namespace sw
