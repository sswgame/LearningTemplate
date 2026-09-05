/**
 * @file D3D11RHIDevice.h
 * @brief Direct3D 11 API 기반 RHI 백엔드 클래스 정의
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIHandleTable.h"
#include "Engine/Graphics/RHI/RHIReleaseQueue.h"

#include <shared_mutex>

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    class D3D11RHICommandContext;
    class D3D11RHICommandList;
    class D3D11RHIResource;
    class D3D11RHISwapChain;

    /**
     * @class D3D11RHIDevice
     * @brief Direct3D 11 그래픽스 디바이스 구현체
     */
    class D3D11RHIDevice : public IRHIDevice
    {
        friend class D3D11RHICommandContext;
        friend class D3D11RHICommandList;

    public:
        friend class D3D11RHISwapChain;
        friend class D3D11RHIResource;
        // ------------------------------------------------------------------------------
        // 1) 수명 — 디바이스/스왑체인, 프레임, 오프스크린
        // ------------------------------------------------------------------------------
        /** @brief 빈 D3D11 디바이스. */
        D3D11RHIDevice();
        /** @brief D3D11 자원을 해제합니다. */
        virtual ~D3D11RHIDevice() override;

        /** @brief Direct3D 11 디바이스, 임베디드 스왑체인 및 렌더 타깃 뷰를 만듭니다. */
        bool initializeInternal( const RHISwapChainDesc& desc ) override;

        /** @brief D3D11 자원 해제 */
        void shutdownInternal() override;

        /** @brief 스왑체인 뷰포트 크기 변경 */
        void resize( uint32 width, uint32 height );

        /** @brief 프레임 시작 (백버퍼 렌더 타깃 클리어) */
        void beginFrame( const float4& clearColor );

        /** @brief 스왑체인 Present 실행 */
        void endFrame( bool vsync = true, bool bPresent = true );

        IRHISwapChain* getSwapChain() override;
        IRHIResource*  getResource() override;
        /** @brief Present/offscreen/replay Immediate Context. */
        IRHICommandContext* getImmediateContext() override;
        /** @brief Mode=Deferred CL 바인딩용 soft Deferred Context. */
        IRHICommandContext* getDeferredCommandContext() override;

        /** @brief GPU idle 대기 후 해제 큐를 flush합니다. */
        void waitIdle() override;

        /** @brief 백엔드 타입 반환 (DirectX11) */
        RHIBackend getBackendType() const override { return RHIBackend::DirectX11; }

        /** @brief 디스크립터 인덱스 테이블 (CB/UAV/텍스처) — 드로우 시 바인드 에뮬레이션. */
        bool supportsBindless() const override { return true; }

        /** @brief VS 가 StructuredBuffer SRV(g_SwInstances, t4)로 GPUScene 인스턴스 버퍼를 읽는다. */
        bool supportsInstancedSceneDraw() const override { return true; }

        /**
         * @brief 병렬 커맨드 기록 지원 여부를 런타임에 반영합니다.
         * @details 리스트마다 자기 Deferred Context를 소유하므로 구조적으로는 병렬 기록이 가능하지만,
         *          드라이버가 커맨드 리스트를 네이티브로 지원하지 않으면 D3D11 런타임이 소프트웨어로
         *          에뮬레이션해서 병렬화 이득보다 오버헤드가 커진다. 그래서 정적 표를 그대로 쓰지 않고
         *          `D3D11_FEATURE_THREADING` 조회 결과로 이 항목만 덮어쓴다.
         */
        RHICapabilities getCapabilities() const override
        {
            RHICapabilities caps            = RHIAvailability::query( RHIBackend::DirectX11 );
            caps._bParallelCommandRecording = _bDriverCommandLists != SW_FALSE ? 1u : 0u;
            return caps;
        }

        /** @brief 백엔드 문자열 반환 */
        const utf8* getBackendName() const override { return "Direct3D 11"; }

        /** @brief ID3D11Device 인터페이스 포인터 반환 */
        void* getNativeDevice() const override { return _device.Get(); }

        /** @brief ID3D11DeviceContext 인터페이스 포인터 반환 */
        void* getNativeContext() const override { return _deviceContext.Get(); }

        /** @brief immediate context는 소유 스레드 전용. */
        bool requiresExclusiveContextThread() const override { return true; }
        /** @brief 호출 스레드에 컨텍스트 소유를 표시합니다. */
        bool bindGraphicsContext() override;
        /** @brief 그래픽스 컨텍스트 바인딩을 해제합니다. */
        void unbindGraphicsContext() override;

        /** @brief IDXGISwapChain 인터페이스 포인터 반환 */
        void* getNativeSwapChain() const override { return _swapChain.Get(); }

        /** @brief D3D11은 단일 큐 모델로 커맨드 큐 포인터가 없음 (nullptr) */
        void* getNativeCommandQueue() const override { return nullptr; }

        /** @brief 네이티브 텍스처 포인터 반환 (ID3D11Texture2D*) */
        void* getNativeTexturePointer( RHITextureHandle texture ) const override;

        /** @brief D3D11 DrawInstancedIndirect 호출 */
        void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0,
                           RHIDescriptorIndex materialDescriptorIndex = kInvalidDescriptorIndex );

        /** @brief 커맨드 리스트 생성 */
        unique_ptr<IRHICommandList> createCommandList( RHICommandListMode mode ) override;

        /** @brief 커맨드 리스트 제출 */
        void executeCommandList( IRHICommandList* pCmdList ) override;

    private:
        /**
         * @brief 스왑체인 백버퍼 RTV를 만듭니다.
         */
        void createRenderTargetView();
        /**
         * @brief 렌더 타깃 뷰를 정리합니다
         */
        void cleanupRenderTargetView();
        /**
         * @brief 풀스크린 삼각형 버텍스 버퍼를 만듭니다.
         */

        /// @brief 텍스처 + SRV/RTV/UAV 뷰
        struct TextureRecord
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D>          _texture;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   _rtv;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   _dsv;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _srv;
            uint32                                           _width;
            uint32                                           _height;
            uint8                                            _bDepth   : 1;
            uint8                                            _reserved : 7;
            /** @brief 빈 텍스처 레코드. */
            TextureRecord()
                : _width{ 0 }
                , _height{ 0 }
                , _bDepth{ SW_FALSE }
                , _reserved{ 0 } {}
        };

        // ------------------------------------------------------------------------------
        // bindless 레지스트리 접근자 — 락을 여기 한 곳에 모아둔다.
        // 커맨드 기록(병렬 가능)이 인덱스로 읽고, register/unregister가 push_back으로 재할당하므로
        // 원시 vector를 직접 인덱싱하면 기록 스레드가 쓰레기를 읽는다. 읽기는 공유 락이라 병렬
        // 기록끼리는 서로를 막지 않는다.
        // ------------------------------------------------------------------------------
        /** @brief 등록된 상수버퍼 슬롯 수입니다. */
        size_t bindlessBufferCount() const;
        /** @brief 인덱스의 상수버퍼 핸들입니다. 범위 밖이면 0입니다. */
        RHIBufferHandle bindlessBufferAt( RHIDescriptorIndex index ) const;
        /** @brief 인덱스의 텍스처 핸들입니다. 범위 밖이면 0입니다. */
        RHITextureHandle bindlessTextureAt( RHIDescriptorIndex index ) const;
        /** @brief 인덱스의 UAV입니다(참조를 하나 올려 돌려줍니다). 범위 밖이면 nullptr입니다. */
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> bindlessUavAt( RHIDescriptorIndex index ) const;

        /** @brief 컴퓨트 루트 상수 CB를 확보합니다. */
        bool ensureComputeRootConstantCB();
        /** @brief 불투명 버퍼 핸들을 ID3D11Buffer로 풉니다. */
        ID3D11Buffer* resolveBuffer( RHIBufferHandle handle ) const;
        /** @brief ComPtr을 테이블에 넣고 핸들을 반환합니다. */
        RHIBufferHandle storeBuffer( Microsoft::WRL::ComPtr<ID3D11Buffer> buffer );
        /** @brief 불투명 텍스처 핸들을 TextureRecord로 풉니다. */
        TextureRecord*       resolveTexture( RHITextureHandle handle );
        const TextureRecord* resolveTexture( RHITextureHandle handle ) const;
        /** @brief TextureRecord를 테이블에 넣고 핸들을 반환합니다. */
        RHITextureHandle storeTexture( TextureRecord record );

        /** @brief setComputeRootConstants 실제 용량(dword). RHITypes.h의
         *         constant::kMinComputeRootConstantDwords(=DX12 기준, 4개 백엔드 공통 안전값) 참고. */
        static constexpr uint32 kMaxComputeRootConstantDwords = 64;

        /// @brief VS/PS + 래스터/블렌드/깊이 상태 묶음
        struct D3D11PipelineStateRecord
        {
            Microsoft::WRL::ComPtr<ID3D11VertexShader>      _vs;
            Microsoft::WRL::ComPtr<ID3D11PixelShader>       _ps;
            Microsoft::WRL::ComPtr<ID3D11ComputeShader>     _cs;
            Microsoft::WRL::ComPtr<ID3D11InputLayout>       _inputLayout;
            Microsoft::WRL::ComPtr<ID3D11RasterizerState>   _rasterizerState;
            Microsoft::WRL::ComPtr<ID3D11BlendState>        _blendState;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depthStencilState;
        };

        /// @brief 렌더 패스 서술 캐시
        struct D3D11RenderPassRecord
        {
            RHIRenderPassDesc _desc{};
            uint8             _bAlive   : 1;
            uint8             _reserved : 7;
        };

        Microsoft::WRL::ComPtr<ID3D11Device>        _device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> _deviceContext;
        /** @brief Thread that may call immediate-context APIs (0 = unbound / GT init). */
        std::thread::id                                _contextOwnerThread;
        Microsoft::WRL::ComPtr<IDXGISwapChain>         _swapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _renderTargetView;

        Microsoft::WRL::ComPtr<ID3D11Buffer> _vertexBuffer; ///< 풀스크린 포스트 (정점 3개)

        RHIHandleTable<Microsoft::WRL::ComPtr<ID3D11Buffer>> _gpuBuffers;
        /// @brief 구조 버퍼 핸들 → SRV (그래픽스 VS 가 StructuredBuffer 로 읽음. GPUScene 인스턴스 버퍼 등).
        unordered_map<RHIBufferHandle, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> _mapBufferSrv;
        RHIHandleTable<TextureRecord>                                                    _gpuTextures;
        RHIBufferHandle                                                                  _boundMeshVb;
        uint32                                                                           _boundMeshStride; ///< 바인딩된 VB stride
        uint32                                                                           _boundMeshOffset;

        /// @brief 드라이버가 커맨드 리스트를 네이티브 지원하면 SW_TRUE — 병렬 기록 capability의 근거.
        uint8 _bDriverCommandLists{ SW_FALSE };

        /// @brief 디스크립터 레지스트리 보호용. 커맨드 기록(D3D11RHICommandContext의 바인드 경로)이
        /// 인덱스로 이 목록들을 읽는 동안, register/unregister가 push_back으로 재할당을 일으키면
        /// 기록 스레드가 쓰레기를 읽는다. 읽기는 공유 락이라 병렬 기록을 직렬화하지 않는다.
        mutable std::shared_mutex _bindlessMutex;

        vector<RHIBufferHandle> _listRegisteredBindless;
        vector<uint32>          _listBindlessFree;

        vector<RHITextureHandle> _listRegisteredTexture;
        vector<uint32>           _listTextureFree;

        vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>> _listRegisteredUAV;
        vector<RHIBufferHandle>                                   _listUavSourceBuffer;
        vector<uint32>                                            _listUavFree;

        Microsoft::WRL::ComPtr<ID3D11Buffer> _computeRootConstantCB;
        uint32                               _arrComputeRootConstantShadow[kMaxComputeRootConstantDwords];

        RHIHandleTable<D3D11PipelineStateRecord> _pipelineStates;
        vector<D3D11RenderPassRecord>            _listRenderPass;

        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depthEnabledState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depthDisabledState;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      _linearSampler;
        RHIPipelineStateHandle                          _activeGraphicsPso;

        HWND   _pHWnd;
        uint32 _width;
        uint32 _height;

        RHIReleaseQueue _releaseQueue;

        sw::unique_ptr<D3D11RHICommandContext> _immContext;
        sw::unique_ptr<D3D11RHICommandContext> _deferredContext;
        sw::unique_ptr<D3D11RHISwapChain>      _swapChainImpl;
        sw::unique_ptr<D3D11RHIResource>       _resourceImpl;
    };
} // namespace sw

#else
namespace sw
{
    /** @brief 비-Windows 환경용 스텁 D3D11RHIDevice */
    class D3D11RHIDevice : public IRHIDevice
    {
        friend class D3D11RHICommandContext;

    public:
        /** @brief 비-Windows 스텁. initialize는 항상 실패. */
        D3D11RHIDevice() = default;
        /** @brief 스텁 소멸. */
        ~D3D11RHIDevice() = default;

        bool initializeInternal( const RHISwapChainDesc& ) { return false; }
        void shutdownInternal() {}
        void resize( uint32, uint32 ) {}
        void beginFrame( const float4& ) {}
        void endFrame( bool, bool = true ) {}

        RHIBackend  getBackendType() const { return RHIBackend::DirectX11; }
        bool        supportsBindless() const { return true; }
        const utf8* getBackendName() const { return "Direct3D 11 (Not Supported on non-Windows)"; }

        void* getNativeDevice() const { return nullptr; }
        void* getNativeContext() const { return nullptr; }
        void* getNativeSwapChain() const override { return nullptr; }
        void* getNativeCommandQueue() const { return nullptr; }

        IRHISwapChain*      getSwapChain() override { return nullptr; }
        IRHIResource*       getResource() override { return nullptr; }
        IRHICommandContext* getImmediateContext() override { return nullptr; }
        IRHICommandContext* getDeferredCommandContext() override { return nullptr; }

        sw::unique_ptr<IRHICommandList> createCommandList( RHICommandListMode ) { return nullptr; }
        void                            executeCommandList( IRHICommandList* ) {}
    };
} // namespace sw
#endif
