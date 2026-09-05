/**
 * @file D3D12RHIDevice.h
 * @brief Direct3D 12 API 기반 RHI 백엔드 클래스 정의
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/FrameResourceRing.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIHandleTable.h"
#include "Engine/Graphics/RHI/RHIReleaseQueue.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    DXGI_FORMAT toDxgiFormatD3D12( RHIFormat format );
    class D3D12RHICommandContext;
    class D3D12RHICommandList;
    class D3D12RHIResource;
    class D3D12RHISwapChain;

    /**
     * @struct D3D12RecordingState
     * @brief "지금 이 커맨드 리스트가 기록 중" 상태 — 디바이스 전역이 아니라 리스트(컨텍스트)마다 있어야 한다.
     * @details 예전엔 이 필드들이 D3D12RHIDevice 에 있어서 Immediate/Deferred Context 가 사실상 같은
     *          커맨드 리스트를 가리키는 별칭이었다. `D3D12RHICommandList` 가 자기 것을 소유하게 해서
     *          진짜 독립된 Deferred Context/병렬 기록의 전제조건을 만든다 (스왑체인 리소스 상태처럼
     *          "실제 GPU 리소스의 상태"를 나타내는 것은 여기 포함하지 않는다 — 그건 디바이스/리소스 전역).
     */
    struct D3D12RecordingState
    {
        RHIBufferHandle        _boundMeshVb{ 0 };
        uint32                 _boundMeshStride{ 0 };
        uint32                 _boundMeshOffset{ 0 };
        RHIBufferHandle        _boundIndexBuffer{ 0 };
        uint32                 _boundIndexStride{ 4 };
        uint32                 _boundIndexOffset{ 0 };
        RHIPipelineStateHandle _activeGraphicsPso{ 0 };
        RHITextureHandle       _arrActiveColorTarget[kMaxColorAttachments]{};
        RHITextureHandle       _activeDepthTarget{ 0 };
        uint32                 _activeColorTargetCount{ 0 };
        uint8                  _bActiveSwapchainRT : 1;
        uint8                  _bRecording         : 1;
        [[maybe_unused]] uint8 _reserved           : 6;

        /** @brief 기록 안 한 상태로 초기화. */
        D3D12RecordingState()
            : _bActiveSwapchainRT{ 0 }
            , _bRecording{ 0 }
            , _reserved{ 0 }
        {
        }
    };

    /**
     * @class D3D12RHIDevice
     * @brief Direct3D 12 그래픽스 디바이스 구현체 (Bindless 지원)
     */
    class D3D12RHIDevice : public IRHIDevice
    {
    public:
        friend class D3D12RHISwapChain;
        friend class D3D12RHIResource;
        friend class D3D12RHICommandContext;
        friend class D3D12RHICommandList;
        // ------------------------------------------------------------------------------
        // 1) 수명 — 디바이스/큐/스왑체인, 프레임, 오프스크린
        // ------------------------------------------------------------------------------
        /** @brief 빈 D3D12 디바이스. */
        D3D12RHIDevice();
        /** @brief D3D12 자원과 펜스를 해제합니다. */
        virtual ~D3D12RHIDevice() override;

        /** @brief Direct3D 12 디바이스, 커맨드 큐, DXGI 스왑체인 및 힙 리소스 초기화 */
        bool initializeInternal( const RHISwapChainDesc& desc ) override;

        /** @brief D3D12 자원 및 펜스 동기화 객체 해제 */
        void shutdownInternal() override;

        /** @brief GPU 명령 완료 대기 (Fence Sync) */
        void waitIdle() override;

        /** @brief 오프스크린 패스를 종료합니다. */
        IRHISwapChain* getSwapChain() override;
        IRHIResource*  getResource() override;
        /** @brief Present/offscreen/replay Immediate Context. */
        IRHICommandContext* getImmediateContext() override;
        /** @brief Mode=Deferred CL 바인딩용 soft Deferred Context. */
        IRHICommandContext* getDeferredCommandContext() override;

        /** @brief 백엔드 타입 반환 (DirectX12) */
        RHIBackend getBackendType() const override { return RHIBackend::DirectX12; }

        /** @brief D3D12는 네이티브 Bindless(Unbounded Descriptor Table)를 지원함 (true 반환) */
        bool supportsBindless() const override { return true; }

        /** @brief 루트 시그니처가 힙 인덱싱이면 ResourceDescriptorHeap[index] 네이티브 샘플링. */
        bool supportsNativeBindlessSampling() const override { return _bHeapDirectlyIndexed != 0; }

        /** @brief 힙 인덱싱이면 VS 가 ResourceDescriptorHeap[g_SwInstancesIndex] 로 인스턴스 버퍼를 읽는다. */
        bool supportsInstancedSceneDraw() const override { return _bHeapDirectlyIndexed != 0; }

        /** @brief 런타임 native bindless 반영. */
        RHICapabilities getCapabilities() const override
        {
            RHICapabilities caps  = RHIAvailability::query( RHIBackend::DirectX12 );
            caps._bNativeBindless = _bHeapDirectlyIndexed != 0 ? 1u : 0u;
            return caps;
        }

        /** @brief 백엔드 문자열 반환 */
        const utf8* getBackendName() const override { return "Direct3D 12"; }

        /** @brief ID3D12Device 포인터 반환 */
        void* getNativeDevice() const override { return _device.Get(); }

        /** @brief ID3D12GraphicsCommandList 포인터 반환 */
        void* getNativeContext() const override { return _commandList.Get(); }

        /** @brief IDXGISwapChain3 포인터 반환 */
        void* getNativeSwapChain() const override { return _swapChain.Get(); }

        /** @brief ID3D12CommandQueue 포인터 반환 */
        void* getNativeCommandQueue() const override { return _commandQueue.Get(); }

        /** @brief 네이티브 텍스처 포인터 반환 (ID3D12Resource*) */
        void* getNativeTexturePointer( RHITextureHandle texture ) const override;

        /** @brief 독립 커맨드 리스트 생성 */
        unique_ptr<IRHICommandList> createCommandList( RHICommandListMode mode ) override;

        /** @brief 독립 커맨드 리스트 제출 */
        void executeCommandList( IRHICommandList* pCmdList ) override;

    private:
        /**
         * @brief 스왑체인 RTV/DSV를 만듭니다.
         */
        void createRenderTargets();
        /**
         * @brief 렌더 타깃을 정리합니다
         */
        void cleanupRenderTargets();
        /**
         * @brief 이전 프레임 완료를 기다립니다
         */
        void waitForPreviousFrame();
        /**
         * @brief 링 슬롯이 GPU에서 풀릴 때까지 기다린 뒤 다음 슬롯으로 진행합니다.
         */
        void waitForRingSlot();
        /**
         * @brief 현재 링 슬롯에 펜스를 기록하고 해제 큐를 진행합니다. GPU를 기다리지 않습니다.
         */
        void signalCurrentFrame();
        /** @brief 현재 링 슬롯의 커맨드 얼로케이터입니다 (레거시 Immediate/Deferred Context 전용). */
        ID3D12CommandAllocator* currentAllocator();
        /**
         * @brief 현재 링 슬롯의 `D3D12RHICommandList` 전용 얼로케이터입니다.
         * @details 레거시 `_arrCommandAllocator` 와 별개 — 같은 프레임 안에서 스왑체인 begin/end(레거시
         *          리스트)와 `FrameRenderer` 의 진짜 네이티브 리스트가 동시에 "열려" 있을 수 있으므로,
         *          같은 얼로케이터를 공유하면 안 된다(D3D12 는 열린 리스트가 있는 얼로케이터를 Reset 하면
         *          안 됨). 링 인덱스는 `waitForRingSlot()` 이 이미 이번 프레임에 정한 것을 그대로 쓴다
         *          (다시 대기하지 않음 — 한 프레임에 한 번만 전진).
         */
        ID3D12CommandAllocator* currentFrameCmdAllocator();
        /** @brief 불투명 버퍼 핸들을 GPU 리소스로 풉니다. */
        ID3D12Resource* resolveBuffer( RHIBufferHandle handle ) const;
        /** @brief 불투명 텍스처 핸들을 GPU 리소스로 풉니다. */
        ID3D12Resource* resolveTexture( RHITextureHandle handle ) const;
        /** @brief ComPtr을 테이블에 넣고 핸들을 반환합니다. */
        RHIBufferHandle storeBuffer( Microsoft::WRL::ComPtr<ID3D12Resource> buffer );
        /** @brief ComPtr을 테이블에 넣고 핸들을 반환합니다. */
        RHITextureHandle storeTexture( Microsoft::WRL::ComPtr<ID3D12Resource> texture );
        /**
         * @brief 풀스크린 삼각형 버텍스 버퍼를 만듭니다.
         */
        bool createGlobalResources();
        /**
         * @brief D3D12 InfoQueue 메시지를 로그로 비웁니다.
         */
        void flushDebugMessages( const utf8* pStage );

        static constexpr uint32 kMaxOffscreenRtvs             = 32;
        static constexpr uint32 kMaxOffscreenDsvs             = 16;
        static constexpr uint32 kGraphicsSrvRootParam0        = 5;  ///< t0..t3 descriptor tables
        static constexpr uint32 kComputeRootConstantsParam    = 10; ///< 32-bit root constants (compute)
        static constexpr uint32 kMaterialCbvParam             = 11; ///< b1 (MaterialCB) 디스크립터 테이블
        static constexpr uint32 kMaxComputeRootConstantDwords = 16;

        /// @brief 오프스크린 텍스처 + RTV/SRV 핸들
        struct OffscreenTextureRecord
        {
            D3D12_CPU_DESCRIPTOR_HANDLE _rtvHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE _dsvHandle{};
            uint32                      _rtvIndex{ 0 };
            uint32                      _dsvIndex{ 0 };
            D3D12_RESOURCE_STATES       _state  = D3D12_RESOURCE_STATE_COMMON;
            DXGI_FORMAT                 _format = DXGI_FORMAT_UNKNOWN;
            uint32                      _width{ 0 };
            uint32                      _height{ 0 };
            uint8                       _bHasRtv  : 1;
            uint8                       _bHasDsv  : 1;
            uint8                       _reserved : 6;
        };

        /// @brief 네이티브 PSO + 루트 시그니처
        struct D3D12PipelineStateRecord
        {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
        };

        static constexpr uint32 kMaxShaderVisibleDescriptors = 32768;

        /// @brief 렌더 패스 서술 캐시
        struct D3D12RenderPassRecord
        {
            RHIRenderPassDesc _desc{};
            uint8             _bAlive   : 1;
            uint8             _reserved : 7;
        };

        /// @brief 힙 슬롯에 등록된 버퍼/텍스처
        struct BindlessResourceRecord
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> _resource;
            D3D12_CPU_DESCRIPTOR_HANDLE            _cpuHandle{};
            D3D12_GPU_DESCRIPTOR_HANDLE            _gpuHandle{};
            RHIBufferHandle                        _buffer{ 0 };
            RHITextureHandle                       _texture{ 0 };
        };

        Microsoft::WRL::ComPtr<ID3D12Device>              _device;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue>        _commandQueue;
        Microsoft::WRL::ComPtr<IDXGISwapChain3>           _swapChain;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>      _rtvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>      _dsvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>      _cbvHeap;
        Microsoft::WRL::ComPtr<ID3D12RootSignature>       _rootSignature;
        Microsoft::WRL::ComPtr<ID3D12RootSignature>       _computeRootSignature;
        Microsoft::WRL::ComPtr<ID3D12Resource>            _vertexBuffer; ///< 풀스크린 포스트 (정점 3개)
        Microsoft::WRL::ComPtr<ID3D12CommandSignature>    _drawCommandSignature;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature>    _drawIndexedCommandSignature;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature>    _dispatchCommandSignature;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    _arrCommandAllocator[FrameResourceRing::kFrameCount];
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _commandList;
        /// @brief `D3D12RHICommandList`(진짜 네이티브 프레임 리스트) 전용 얼로케이터 링 — 레거시와 별개.
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> _arrFrameCmdAllocator[FrameResourceRing::kFrameCount];
        FrameResourceRing                              _frameRing;

        vector<Microsoft::WRL::ComPtr<ID3D12Resource>>         _listRenderTarget;
        RHIHandleTable<Microsoft::WRL::ComPtr<ID3D12Resource>> _gpuBuffers;
        RHIHandleTable<Microsoft::WRL::ComPtr<ID3D12Resource>> _gpuTextures;
        /// @brief 리소스 상태 전이 맵 보호용 — 여러 커맨드 리스트가 동시에 같은 자원을 전이할 수 있으므로.
        mutex                                                 _resourceStateMutex;
        unordered_map<RHIBufferHandle, D3D12_RESOURCE_STATES> _mapStructuredBufferState;
        /// @brief 구조 버퍼 핸들 → 요소 stride (bindless StructuredBuffer SRV 생성용).
        unordered_map<RHIBufferHandle, uint32> _mapStructuredStride;

        unordered_map<RHITextureHandle, OffscreenTextureRecord> _mapOffscreenTexture;
        uint32                                                  _nextOffscreenRtvIndex;
        uint32                                                  _nextOffscreenDsvIndex;
        vector<uint32>                                          _listFreeOffscreenRtvIndex;
        vector<uint32>                                          _listFreeOffscreenDsvIndex;
        unordered_map<RHIBufferHandle, uint32>                  _mapCbAlignedSize;
        unordered_map<RHIBufferHandle, void*>                   _mapCbMapped;

        RHIHandleTable<D3D12PipelineStateRecord> _pipelineStates;
        vector<D3D12RenderPassRecord>            _listRenderPass;

        /// @brief 스왑체인 백버퍼의 실제 리소스 상태 — 리스트가 아니라 리소스 자체에 속한 전역 상태.
        D3D12_RESOURCE_STATES  _swapchainState;
        uint8                  _bHeapDirectlyIndexed : 1;
        [[maybe_unused]] uint8 _reservedPassFlags    : 7;

        /// @brief Immediate/Deferred Context(레거시 단일 공유 리스트)용 기록 상태.
        D3D12RecordingState _legacyState;

        vector<BindlessResourceRecord> _listRegisteredBindless;
        vector<uint32>                 _listFreeBindless;

        vector<BindlessResourceRecord> _listRegisteredUAV;
        vector<uint32>                 _listFreeUav;

        UINT _rtvDescriptorSize;
        UINT _cbvDescriptorSize;
        UINT _allocatedDescriptorsCount;
        UINT _frameIndex;

        HANDLE                              _fenceEvent;
        Microsoft::WRL::ComPtr<ID3D12Fence> _fence;
        UINT64                              _fenceValue;

        HWND   _pHWnd;
        uint32 _width;
        uint32 _height;
        uint32 _bufferCount;

        RHIReleaseQueue _releaseQueue;

        /** @brief Present / offscreen / Deferred CL replay 대상 Immediate Context. */
        sw::unique_ptr<D3D12RHICommandContext> _immContext;
        /** @brief Mode=Deferred일 때 CL 바인딩용 soft Deferred Context (present 대상 아님). */
        sw::unique_ptr<D3D12RHICommandContext> _deferredContext;
        sw::unique_ptr<D3D12RHISwapChain>      _swapChainImpl;
        sw::unique_ptr<D3D12RHIResource>       _resourceImpl;
    };
} // namespace sw

#else
namespace sw
{
    /** @brief 비-Windows 환경용 스텁 D3D12RHIDevice */
    class D3D12RHIDevice : public IRHIDevice
    {
    public:
        /** @brief 비-Windows 스텁. initialize는 항상 실패. */
        D3D12RHIDevice() = default;
        /** @brief 스텁 소멸. */
        ~D3D12RHIDevice() override = default;

        bool initializeInternal( const RHISwapChainDesc& ) override { return false; }
        void shutdownInternal() override {}
        void resize( uint32, uint32 ) override {}
        void beginFrame( const float4& ) {}
        void endFrame( bool, bool = true ) override {}

        RHIBackend  getBackendType() const override { return RHIBackend::DirectX12; }
        bool        supportsBindless() const override { return true; }
        const utf8* getBackendName() const override { return "Direct3D 12 (Not Supported on non-Windows)"; }

        void* getNativeDevice() const override { return nullptr; }
        void* getNativeContext() const override { return nullptr; }
        void* getNativeSwapChain() const { return nullptr; }
        void* getNativeCommandQueue() const override { return nullptr; }

        IRHISwapChain*      getSwapChain() override { return nullptr; }
        IRHIResource*       getResource() override { return nullptr; }
        IRHICommandContext* getImmediateContext() override { return nullptr; }
        IRHICommandContext* getDeferredCommandContext() override { return nullptr; }

        sw::unique_ptr<IRHICommandList> createCommandList( RHICommandListMode ) override { return nullptr; }
        void                            executeCommandList( IRHICommandList* ) override {}
    };
} // namespace sw
#endif
