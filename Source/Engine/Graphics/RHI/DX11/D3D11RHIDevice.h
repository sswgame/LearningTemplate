/**
 * @file D3D11RHIDevice.h
 * @brief Direct3D 11 RHI 백엔드 디바이스입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHISwapChain.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIGpuTimestamp.h"
#include "Engine/Graphics/RHI/Support/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#include <shared_mutex>

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    struct RHIGpuMemoryBudget;

    class D3D11RHICommandContext;
    class D3D11RHICommandList;
    class D3D11RHIResourceFactory;

    /** @brief 루트 상수 에뮬레이션 버퍼의 dword 수입니다. `D3D11RHIDevice::kMaxComputeRootConstantDwords` 의 기준입니다. */
    inline constexpr uint32 kRootConstantDwordCount = 64;

    /**
     * @struct D3D11RecordingState
     * @brief "지금 이 Deferred Context 에 무엇이 걸려 있나" 입니다. 기록 스트림마다 있어야 하는 상태입니다.
     * @details D3D12 의 `D3D12RecordingState` 와 같은 역할입니다. 리스트마다 자기 Deferred Context 를
     *          소유하는데 이 캐시가 디바이스 전역이면 서로의 바인딩 캐시를 덮어써, 병렬 기록에서 한 패스의
     *          드로우가 다른 패스의 PSO · 정점 버퍼로 나갑니다. 주의: 인자 둘짜리 컨텍스트 생성자는 디바이스의
     *          것을 가리키므로 즉시 컨텍스트 전용입니다. `D3D11RHICommandList` 는 자기 상태를 넘기는 생성자를 씁니다.
     * @note `OpenGLRecordingState` 는 **반대로 디바이스가 소유하는 것이 맞습니다.** GL 은 커맨드 버퍼가
     *       없는 상태 머신이라 실제 상태가 하나뿐입니다. 기준은 "리스트마다 하나" 가 아니라
     *       **"기록 스트림마다 하나"** 입니다.
     */
    struct D3D11RecordingState
    {
        /**
         * @brief 루트 상수 에뮬레이션(계약 슬롯 b2)의 버퍼와 CPU 그림자입니다. **기록 스트림마다** 따로입니다.
         * @details D3D11 에는 루트 상수가 없어 작은 상수버퍼로 흉내 냅니다. 이것이 디바이스 전역이면
         *          병렬로 기록하는 두 패스가 같은 그림자 배열과 같은 버퍼를 번갈아 덮어써, 한쪽 패스의
         *          드로우가 **다른 패스의 월드 행렬**로 그려집니다.
         */
        Microsoft::WRL::ComPtr<ID3D11Buffer> _rootConstantCb;
        uint32                               _arrRootConstantShadow[kRootConstantDwordCount]{};

        RHIBufferHandle        _boundMeshVb{ 0 };
        uint32                 _boundMeshStride{ 0 };
        uint32                 _boundMeshOffset{ 0 };
        RHIBufferHandle        _boundInstanceSlotVb{ 0 }; ///< 슬롯 1: 인스턴스 슬롯 스트림 (0 = 안 걸림)
        uint32                 _boundInstanceSlotOffset{ 0 };
        RHIPipelineStateHandle _activeGraphicsPso{ 0 };
        /**
         * @brief CS UAV 슬롯마다 지금 걸려 있는 버퍼입니다(0 = 없음).
         * @details D3D11 은 같은 리소스를 출력(UAV)과 입력(SRV)에 동시에 걸 수 없습니다. UAV 를 안 떼면
         *          런타임이 **SRV 쪽을 조용히 NULL 로 강제합니다**(경고만 나옵니다). 그래서 어느 슬롯에
         *          어떤 버퍼가 걸려 있는지 기록해 두고 transitionBuffer 가 읽기 상태로 돌릴 때 뗍니다.
         */
        RHIBufferHandle _arrComputeUavBuffer[D3D11_PS_CS_UAV_REGISTER_COUNT]{};
        /**
         * @brief PS SRV 슬롯마다 지금 걸려 있는 텍스처입니다(0 = 없음 · 버퍼).
         * @details 같은 리소스를 입력(SRV)과 출력(RTV · DSV)에 동시에 걸면 `OMSetRenderTargets` 가 **SRV 를 NULL 로 강제하고** 경고만 냅니다.
         *          `prepareTextureForRenderTarget` 가 이 표를 보고 그 텍스처가 걸린 슬롯만 뗍니다(언리얼 D3D11 RHI 의
         *          `ConditionalClearShaderResource` 자리).
         */
        RHITextureHandle _arrPixelSrvTexture[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class D3D11RHIDevice
     * @brief Direct3D 11 그래픽스 디바이스 구현체입니다.
     */
    class D3D11RHIDevice : public IRHIDevice
    {
        friend class D3D11RHICommandContext;
        friend class D3D11RHICommandList;

    public:
        friend class D3D11RHIResourceFactory;
        // ------------------------------------------------------------------------------
        // 1) 수명 — 디바이스 · 스왑체인 · 프레임
        // ------------------------------------------------------------------------------
        /** @brief 빈 D3D11 디바이스로 만듭니다. */
        D3D11RHIDevice();
        /** @brief D3D11 자원을 해제합니다. */
        virtual ~D3D11RHIDevice() override;

        /** @brief Direct3D 11 디바이스와 스왑체인(백버퍼 RTV 포함)을 만듭니다. */
        bool initializeInternal( const RHISwapChainDesc& desc ) override;

        /** @brief D3D11 자원을 해제합니다. */
        void shutdownInternal() override;

        /** @brief 종료 3 단계: 프레임 스트림 컨텍스트를 놓고 살아 있는 커맨드 리스트를 뗍니다. */
        void detachCommandRecordingInternal() override;

        /** @brief 스왑체인 백버퍼를 새 크기로 다시 만듭니다. */
        void resizeInternal( uint32 width, uint32 height ) override;

        /** @brief 프레임을 엽니다(타임스탬프 · 백버퍼 RTV 재취득 · 정적 샘플러). 백버퍼 클리어는 beginRenderPass(핸들 0)가 합니다. */
        void beginFrame( const float4& clearColor ) override;

        /** @brief 프레임을 닫고 Present 합니다(bPresent=false 면 Present 를 생략합니다). */
        void endFrame( bool vsync = true, bool bPresent = true ) override;

        void               setTimestampEnabled( bool bEnabled ) override { _bTimestampEnabled = bEnabled ? SW_TRUE : SW_FALSE; }
        uint32             getTimestampSlotCount() const override;
        [[nodiscard]] bool readTimestamps( RHIGpuTimestampFrame& outFrame ) override;
        [[nodiscard]] bool readGpuClockNanos( int64& outGpuNanos ) override;
        bool               isGpuClockReadCheap() const override { return false; }
        /**
         * @brief 커맨드 리스트가 **자기 Deferred Context 에** 타임스탬프를 겁니다.
         * @details 칸 번호는 패스 인덱스로 고정이라 쿼리 객체 하나를 두 컨텍스트가 건드릴 일이 없습니다.
         *          D3D11 이 금지하는 것이 바로 그것입니다.
         */
        void writeTimestampSlot( ID3D11DeviceContext* pContext, uint32 slotIndex );
        /** @brief D3D11 디버그 레이어의 CORRUPTION/ERROR 메시지를 로그로 비웁니다 (SW_DEBUG, 프레임 끝). */
        void flushDebugMessages( const utf8* pStage );

        IRHIResourceFactory* getResourceFactory() override;
        /** @brief 프레임 스트림 컨텍스트(즉시 컨텍스트)입니다. 백버퍼 패스 · Present 가 여기에 기록합니다. */
        IRHICommandContext* getFrameStreamContext() override;

        /** @brief GPU 가 쉴 때까지 기다린 뒤 해제 큐를 비웁니다. */
        void waitIdleInternal() override;

        /** @brief DXGI `QueryVideoMemoryInfo` 로 이 프로세스의 사용량(로컬 + 비로컬)과 로컬 예산을 채웁니다. 어댑터를 못 찾았으면 false 입니다. */
        [[nodiscard]] bool queryGpuMemoryBudgetInternal( RHIGpuMemoryBudget& outBudget ) override;

        /** @brief 프레임 지연(`kGpuReleaseFrameLatency`) 뒤 해제 큐에 넣습니다. D3D11 은 펜스 대신 endFrame 횟수로 셉니다. */
        void enqueueGpuRelease( const RHIResourceReleaseDelegate& releaseDelegate ) override;

        /** @brief 백엔드 종류(DirectX11)를 반환합니다. */
        RHIBackend getBackendType() const override { return RHIBackend::DirectX11; }
        /**
         * @brief 정적 샘플러 세트를 컨텍스트의 PS 슬롯 s9..s15 에 겁니다.
         * @details 즉시 컨텍스트는 초기화 · 리사이즈(ClearState 뒤) · beginFrame 에서, 지연 컨텍스트는 beginCommandList 마다 부릅니다.
         *          FinishCommandList/ClearState 가 컨텍스트 상태를 비우기 때문입니다. 슬롯 결합 샘플러(s0..s8)는 bindShaderResource 가 겁니다.
         */
        void bindStaticSamplers( ID3D11DeviceContext* pContext ) const;
        /** @brief 스왑체인이 만든 백버퍼 포맷입니다. 백버퍼 PSO 의 렌더 타깃 포맷은 여기서 나옵니다. */
        RHIFormat getBackBufferFormat() const override { return _backBufferFormat; }

        /** @brief VS 가 GPUScene 인스턴스 버퍼를 구조버퍼 SRV(g_SwInstances, t4)로 읽을 수 있어 true 입니다. */
        bool supportsInstancedSceneDraw() const override { return true; }

        /**
         * @brief 병렬 커맨드 기록 지원 여부를 런타임에 반영해 반환합니다.
         * @details 리스트마다 자기 Deferred Context 를 소유하므로 구조적으로는 병렬 기록이 가능하지만,
         *          드라이버가 커맨드 리스트를 네이티브로 지원하지 않으면 D3D11 런타임이 소프트웨어로
         *          에뮬레이션해서 병렬화 이득보다 오버헤드가 커집니다. 그래서 정적 표를 그대로 쓰지 않고
         *          `D3D11_FEATURE_THREADING` 조회 결과로 이 항목만 덮어씁니다.
         */
        RHICapabilities getCapabilities() const override
        {
            RHICapabilities caps            = RHIAvailability::query( RHIBackend::DirectX11 );
            caps._bParallelCommandRecording = _bDriverCommandLists != SW_FALSE ? 1u : 0u;
            return caps;
        }

        /** @brief 백엔드 이름을 반환합니다. */
        const utf8* getBackendName() const override { return "Direct3D 11"; }

        /** @brief ID3D11Device 포인터를 반환합니다. */
        void* getNativeDevice() const override { return _device.Get(); }

        /** @brief 즉시 컨텍스트(ID3D11DeviceContext) 포인터를 반환합니다. */
        void* getNativeContext() const override { return _deviceContext.Get(); }

        /** @brief 즉시 컨텍스트는 소유 스레드에서만 쓰므로 true 입니다. */
        bool requiresExclusiveContextThread() const override { return true; }
        /** @brief 즉시 컨텍스트가 있는지 답합니다. MakeCurrent 가 없어 묶을 것은 없습니다. */
        bool bindGraphicsContext() override;

        /** @brief D3D11 은 커맨드 큐 객체가 없어 nullptr 을 반환합니다. */
        void* getNativeCommandQueue() const override { return nullptr; }

        /** @brief 네이티브 텍스처 포인터(ID3D11Texture2D*)를 반환합니다. */
        void* getNativeTexturePointer( RHITextureHandle texture ) const override;

        /** @brief 커맨드 리스트를 만듭니다(리스트마다 자기 Deferred Context 를 가집니다). */
        unique_ptr<IRHICommandList> createCommandList() override;

        /** @brief 커맨드 리스트를 즉시 컨텍스트의 지금 지점에서 실행합니다(ExecuteCommandList). */
        void executeCommandList( IRHICommandList* pCmdList ) override;

        /** @brief 살아 있는 커맨드 리스트를 등록합니다 (소유하지 않는 참조). */
        void registerCommandList( D3D11RHICommandList* pCmdList );
        /** @brief 등록을 해제합니다. */
        void unregisterCommandList( D3D11RHICommandList* pCmdList );

    private:
        /** @brief 쿼리 묶음을 한 번만 만듭니다. 만들지 못하면 이 백엔드는 타임스탬프를 보고하지 않습니다. */
        void ensureTimestampResources();
        /** @brief 다시 쓰기 직전의 묶음에서 결과를 마이크로초로 풉니다 (기다리지 않습니다). */
        void collectTimestampsForSlot();

        /// @brief 텍스처와 그 뷰(SRV · RTV · UAV)입니다.
        struct TextureRecord
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D>          _texture;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   _rtv;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   _dsv;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _srv;
            /// @brief 면(배열 원소 · 큐브 면)마다의 RTV · DSV 입니다. 면이 하나면 비어 있고 `_rtv` · `_dsv` 를 씁니다(`_rtv` · `_dsv` 는 늘 면 0).
            vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> _listSliceRtv;
            vector<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>> _listSliceDsv;
            uint32                                                 _width;
            uint32                                                 _height;
            uint32                                                 _arraySize; ///< 면 수
            RHITextureDimension                                    _dimension;
            uint8                                                  _bDepth   : 1;
            uint8                                                  _reserved : 7;
            /** @brief 빈 텍스처 레코드로 만듭니다. */
            TextureRecord()
                : _listSliceRtv{}
                , _listSliceDsv{}
                , _width{ 0 }
                , _height{ 0 }
                , _arraySize{ 1 }
                , _dimension{ RHITextureDimension::Texture2D }
                , _bDepth{ SW_FALSE }
                , _reserved{ 0 } {}

            /** @brief 면 `slice` 의 RTV 입니다. 없으면 nullptr. */
            ID3D11RenderTargetView* findSliceRtv( uint32 slice ) const
            {
                if ( _listSliceRtv.empty() )
                    return slice == 0 ? _rtv.Get() : nullptr;
                return slice < _listSliceRtv.size() ? _listSliceRtv[slice].Get() : nullptr;
            }
            /** @brief 면 `slice` 의 DSV 입니다. 없으면 nullptr. */
            ID3D11DepthStencilView* findSliceDsv( uint32 slice ) const
            {
                if ( _listSliceDsv.empty() )
                    return slice == 0 ? _dsv.Get() : nullptr;
                return slice < _listSliceDsv.size() ? _listSliceDsv[slice].Get() : nullptr;
            }
        };

        // ------------------------------------------------------------------------------
        // bindless 레지스트리 접근자. 락을 여기 한 곳에 모아 둔다.
        // 커맨드 기록(병렬 가능)이 인덱스로 읽고, register/unregister 가 push_back 으로 재할당하므로
        // 원시 vector 를 직접 인덱싱하면 기록 스레드가 쓰레기를 읽는다. 읽기는 공유 락이라 병렬
        // 기록끼리는 서로를 막지 않는다.
        // ------------------------------------------------------------------------------
        /** @brief 등록된 버퍼 슬롯 수입니다. */
        size_t bindlessBufferCount() const;
        /** @brief 인덱스의 버퍼 핸들입니다. 범위 밖이면 0 입니다. */
        RHIBufferHandle bindlessBufferAt( RHIDescriptorIndex index ) const;
        /** @brief 인덱스의 텍스처 핸들입니다. 범위 밖이면 0 입니다. */
        RHITextureHandle bindlessTextureAt( RHIDescriptorIndex index ) const;
        /** @brief 인덱스의 UAV 입니다(참조를 하나 올려 반환합니다). 범위 밖이면 nullptr 입니다. */
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> bindlessUavAt( RHIDescriptorIndex index ) const;
        /** @brief UAV 인덱스가 가리키는 원본 버퍼 핸들입니다(없으면 0). SRV/UAV 해저드를 풀 때 씁니다. */
        RHIBufferHandle uavSourceBufferAt( RHIDescriptorIndex index ) const;

        /** @brief 이 기록 스트림의 루트 상수 CB 를 확보합니다(없으면 만듭니다). */
        bool ensureRootConstantCb( D3D11RecordingState& state );
        /**
         * @brief 살아 있는 모든 기록 상태에서 이 버퍼 · PSO 캐시를 지웁니다.
         * @details 캐시가 리스트마다 있으므로 자원이 사라질 때 **모두** 훑어야 합니다. 한 곳만 지우면
         *          다른 리스트가 죽은 핸들을 드로우 시점에 다시 풉니다.
         */
        void forgetBufferInRecordingStates( RHIBufferHandle buffer );
        void forgetPipelineStateInRecordingStates( RHIPipelineStateHandle pso );
        /** @brief 불투명 버퍼 핸들을 ID3D11Buffer 로 풉니다. */
        ID3D11Buffer* resolveBuffer( RHIBufferHandle handle ) const;
        /** @brief ComPtr 을 핸들 표에 넣고 핸들을 반환합니다. GPU 메모리 장부의 Buffer 줄에 크기(`ByteWidth`)를 올리는 유일한 자리입니다. */
        RHIBufferHandle storeBuffer( Microsoft::WRL::ComPtr<ID3D11Buffer> buffer );
        /**
         * @brief 버퍼에 `srvDesc` 로 SRV 를 만들고, 버퍼를 핸들 표에 넣은 뒤 SRV 를 그 핸들에 붙입니다. SRV 를 못 만들면 버퍼만 넣습니다.
         * @details 구조버퍼와 인다이렉트 인자 버퍼 생성이 함께 씁니다(뷰 설명만 다릅니다).
         */
        RHIBufferHandle storeBufferWithSrv( Microsoft::WRL::ComPtr<ID3D11Buffer> buffer, const D3D11_SHADER_RESOURCE_VIEW_DESC& srvDesc );
        /** @brief 불투명 텍스처 핸들을 TextureRecord 로 풉니다. */
        TextureRecord*       resolveTexture( RHITextureHandle handle );
        const TextureRecord* resolveTexture( RHITextureHandle handle ) const;
        /**
         * @brief TextureRecord 를 핸들 표에 넣고 핸들을 반환합니다. GPU 메모리 장부에 `desc` 의 줄 · 논리 크기를 올리는 유일한 자리입니다.
         * @details D3D11 에는 할당 크기를 물을 API 가 없어 서술로 계산한 논리 크기를 적습니다(장부 기준 Logical).
         */
        RHITextureHandle storeTexture( TextureRecord record, const RHITextureDesc& desc );

        /** @brief setComputeRootConstants 의 실제 용량(dword)입니다. 네 백엔드 공통 안전값은
         *         shaderslot::kRootConstantDwords(DX12 · Vulkan 의 루트 · 푸시 상수 크기)입니다. */
        static constexpr uint32 kMaxComputeRootConstantDwords = kRootConstantDwordCount;

        /// @brief VS/PS 와 래스터 · 블렌드 · 깊이 상태 묶음입니다.
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

        /// @brief 렌더 패스 서술 캐시입니다.
        struct D3D11RenderPassRecord
        {
            RHIRenderPassDesc _desc{};
            uint8             _bAlive   : 1;
            uint8             _reserved : 7;
        };

        Microsoft::WRL::ComPtr<ID3D11Device>        _device;
        Microsoft::WRL::ComPtr<IDXGIAdapter3>       _memoryAdapter; ///< 디바이스가 쓰는 어댑터. GPU 메모리 사용량 · 예산 질의용
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> _deviceContext;
        RHIFormat                                   _backBufferFormat; ///< 스왑체인 백버퍼 포맷 (DXGI 는 요청값 그대로)
        /// @brief 창 하나의 백버퍼입니다. 백버퍼 RTV · 크기 · Present 가 모두 여기 모여 있습니다.
        D3D11RHISwapChain _swapChain;

        Microsoft::WRL::ComPtr<ID3D11Buffer> _vertexBuffer; ///< 풀스크린 삼각형(정점 3개). 메시 정점 버퍼가 없는 드로우가 씀

        RHIHandleTable<Microsoft::WRL::ComPtr<ID3D11Buffer>> _gpuBuffers;
        /// @brief 구조버퍼 핸들 → SRV 입니다(그래픽스 VS 가 StructuredBuffer 로 읽습니다. GPUScene 인스턴스 버퍼 등).
        unordered_map<RHIBufferHandle, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> _mapBufferSrv;
        RHIHandleTable<TextureRecord>                                                    _gpuTextures;

        /// @brief 즉시 컨텍스트가 쓰는 기록 상태입니다. 리스트는 각자 자기 것을 갖습니다.
        D3D11RecordingState _recordingState;

        /// @brief 디스크립터 레지스트리를 보호합니다. 커맨드 기록(D3D11RHICommandContext 의 바인드 경로)이
        /// 인덱스로 이 목록들을 읽는 동안 register/unregister 가 push_back 으로 재할당을 일으키면
        /// 기록 스레드가 쓰레기를 읽습니다. 읽기는 공유 락이라 병렬 기록을 직렬화하지 않습니다.
        mutable std::shared_mutex _bindlessMutex;

        /// @brief **즉시 컨텍스트(`_deviceContext`)를 만지는 모든 코드가 잡아야 하는 자물쇠입니다.**
        /// @details `ID3D11DeviceContext` 는 스레드 안전하지 않습니다. 안전한 것은 `ID3D11Device` 뿐입니다.
        ///          기록은 리스트마다 Deferred Context 라 안전하지만, `IRHIResourceFactory` 의 갱신(`updateConstantBuffer` 의
        ///          `Map(WRITE_DISCARD)` 등)과 프레임 스트림 컨텍스트는 즉시 컨텍스트로 나가므로 여기서 잠급니다. 기록 중의
        ///          상수버퍼 갱신은 `IRHICommandList::updateConstantBuffer` 로 리스트의 Deferred Context 에 갑니다 — 드로우마다
        ///          불리는 그 경로가 즉시 컨텍스트로 오면 병렬 기록의 워커들이 한 버퍼를 덮어 패스 CB 가 옆 패스 값으로 바뀝니다.
        mutable mutex _immediateContextMutex;

        /**
         * @struct D3D11TimestampFrame
         * @brief 프레임 하나분의 타임스탬프 쿼리 묶음입니다.
         * @details D3D11 은 틱을 초로 바꿀 주파수를 disjoint 쿼리로만 줍니다. 그 구간 안에서 GPU
         *          클럭이 바뀌었으면 `Disjoint` 가 서고, 그 프레임 값은 통째로 버려야 합니다.
         */
        struct D3D11TimestampFrame
        {
            Microsoft::WRL::ComPtr<ID3D11Query> _disjoint;
            Microsoft::WRL::ComPtr<ID3D11Query> _arrQuery[constant::kMaxGpuTimestampSlot];
            uint32                              _writtenMask{ 0 };
            uint8                               _bPending{ SW_FALSE };
        };

        /**
         * @brief GPU 타임스탬프입니다. 프레임 링만큼 묶음을 돌려 씁니다.
         * @details 읽기는 그 묶음을 **다시 쓰기 직전**(= 링 한 바퀴 뒤)에 DONOTFLUSH 로 한 번만 묻습니다.
         *          아직이면 이번 바퀴는 건너뜁니다. 기다리면 재려던 파이프라인을 멈춰 세웁니다.
         */
        D3D11TimestampFrame _arrTimestampFrame[constant::kMaxFrameCountInFlight];
        uint32              _timestampFrameIndex;
        /// @brief 이번 프레임에 적힌 칸 비트입니다. 패스가 병렬로 기록하므로 원자입니다.
        atomic<uint32> _timestampWrittenMask;
        uint8          _bTimestampEnabled; ///< 엔진이 켜기 전에는 쿼리도 만들지 않음
        uint8          _bTimestampReady;
        uint8          _bTimestampFrameOpen;
        /// @brief 드라이버가 커맨드 리스트를 네이티브로 지원하면 SW_TRUE 입니다. 병렬 기록 능력의 근거입니다.
        uint8                _bDriverCommandLists;
        RHIGpuTimestampFrame _timestampFrame; ///< 마지막으로 읽힌 프레임(`readTimestamps`)
        /**
         * @brief GPU 시계 읽기(`readGpuClockNanos`)용 타임스탬프 쿼리입니다. 처음 읽을 때 만듭니다.
         * @details disjoint 로 감싸지 않습니다 — 프레임의 disjoint 가 열려 있는 동안 불리고, 주파수는 마지막 프레임의 disjoint 값을 씁니다.
         */
        Microsoft::WRL::ComPtr<ID3D11Query> _clockQuery;
        uint64                              _lastTimestampFrequency; ///< 마지막으로 읽은 disjoint 의 주파수(틱/초)

        vector<RHIBufferHandle> _listRegisteredBindless;
        vector<uint32>          _listBindlessFree;

        vector<RHITextureHandle> _listRegisteredTexture;
        vector<uint32>           _listTextureFree;

        vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>> _listRegisteredUAV;
        vector<RHIBufferHandle>                                   _listUavSourceBuffer;
        vector<uint32>                                            _listUavFree;

        RHIHandleTable<D3D11PipelineStateRecord> _pipelineStates;
        vector<D3D11RenderPassRecord>            _listRenderPass;

        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depthEnabledState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depthDisabledState;
        /// @brief 정적 샘플러 세트입니다(bindingslots.hlsli 4, DX12 와 같은 표). s9..s15 에 겁니다. 셰이더가 swSampleIndexWith 의 samplerId 로 고릅니다.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> _arrStaticSampler[shaderslot::kStaticSamplerArrayCount];
        HWND                                       _pHWnd;

        /// @brief 살아 있는 `D3D11RHICommandList` 들입니다. **소유하지 않습니다.** 리사이즈 직전에
        ///        기록물을 버리게 하려고 듭니다(백버퍼 참조를 붙들고 있기 때문입니다).
        mutex                        _liveCmdListMutex;
        vector<D3D11RHICommandList*> _listLiveCmd;

        RHIReleaseQueue _releaseQueue;

        sw::unique_ptr<D3D11RHICommandContext>  _frameStreamContext;
        sw::unique_ptr<D3D11RHIResourceFactory> _resourceImpl;
    };
} // namespace sw

#endif
