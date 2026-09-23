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
#include "Engine/Graphics/RHI/Support/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#include <shared_mutex>

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    class D3D11RHICommandContext;
    class D3D11RHICommandList;
    class D3D11RHIResource;

    /** @brief 루트 상수 에뮬레이션 버퍼의 dword 수입니다. `D3D11RHIDevice::kMaxComputeRootConstantDwords` 의 기준입니다. */
    inline constexpr uint32 kRootConstantDwordCount = 64;

    /**
     * @struct D3D11RecordingState
     * @brief "지금 이 Deferred Context 에 무엇이 걸려 있나" 입니다. 기록 스트림마다 있어야 하는 상태입니다.
     * @details D3D12 의 `D3D12RecordingState` 와 같은 역할입니다. 리스트마다 자기 Deferred Context 를
     *          소유하는데 이 캐시가 디바이스 전역이면 서로의 바인딩 캐시를 덮어씁니다. **한동안 정확히
     *          그 상태였습니다.** 이 주석은 있었지만 `D3D11RHICommandList` 가 인자 둘짜리 컨텍스트
     *          생성자를 써서 모든 리스트가 디바이스의 것 하나를 가리켰고, 병렬 기록에서 한 패스의
     *          드로우가 다른 패스의 PSO · 정점 버퍼로 나갔습니다.
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
    };

    /**
     * @class D3D11RHIDevice
     * @brief Direct3D 11 그래픽스 디바이스 구현체입니다.
     */
    class D3D11RHIDevice : public IRHIDevice
    {
        friend class D3D11RHICommandContext;
        friend class D3D11RHICommandList;

    public:
        friend class D3D11RHIResource;
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

        /** @brief 스왑체인 백버퍼를 새 크기로 다시 만듭니다. */
        void resizeInternal( uint32 width, uint32 height ) override;

        /** @brief 프레임을 엽니다(타임스탬프 · 백버퍼 RTV 재취득 · 정적 샘플러). 백버퍼 클리어는 beginRenderPass(핸들 0)가 합니다. */
        void beginFrame( const float4& clearColor ) override;

        /** @brief 프레임을 닫고 Present 합니다(bPresent=false 면 Present 를 생략합니다). */
        void endFrame( bool vsync = true, bool bPresent = true ) override;

        void   setTimestampEnabled( bool bEnabled ) override { _bTimestampEnabled = bEnabled ? SW_TRUE : SW_FALSE; }
        uint32 getTimestampSlotCount() const override;
        bool   readTimestampsMicros( vector<float32>& outListMicro ) override;
        /**
         * @brief 커맨드 리스트가 **자기 Deferred Context 에** 타임스탬프를 겁니다.
         * @details 칸 번호는 패스 인덱스로 고정이라 쿼리 객체 하나를 두 컨텍스트가 건드릴 일이 없습니다.
         *          D3D11 이 금지하는 것이 바로 그것입니다.
         */
        void writeTimestampSlot( ID3D11DeviceContext* pContext, uint32 slotIndex );
        /** @brief D3D11 디버그 레이어의 CORRUPTION/ERROR 메시지를 로그로 비웁니다 (SW_DEBUG, 프레임 끝). */
        void flushDebugMessages( const utf8* pStage );

        IRHIResource* getResource() override;
        /** @brief 프레임 스트림 컨텍스트(즉시 컨텍스트)입니다. 백버퍼 패스 · Present 가 여기에 기록합니다. */
        IRHICommandContext* getFrameStreamContext() override;

        /** @brief GPU 가 쉴 때까지 기다린 뒤 해제 큐를 비웁니다. */
        void waitIdle() override;

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
        /** @brief 부르는 스레드에 컨텍스트 소유를 표시합니다. */
        bool bindGraphicsContext() override;
        /** @brief 그래픽스 컨텍스트 바인딩을 해제합니다. */
        void unbindGraphicsContext() override;

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

        /** @brief 기록 슬롯 표의 크기(동시에 살아 있는 커맨드 리스트 수의 상한)입니다. 넘치면 그 리스트의 기록 중 갱신은 즉시 컨텍스트(잠금)로 갑니다. */
        static constexpr uint32 kMaxRecordingSlot = 64;
        /** @brief 슬롯 없음을 뜻합니다. */
        static constexpr uint32 kNoRecordingSlot = 0xFFFFFFFFu;

        /**
         * @brief 리스트가 태어날 때 기록 슬롯을 내줍니다. 표가 차면 `kNoRecordingSlot` 입니다.
         * @details **리소스 갱신이 어느 스트림으로 나갈지 정하는 장치입니다.** `IRHIResource` 는 커맨드 스트림을 모르는 디바이스 레벨
         *          인터페이스라 `updateConstantBuffer` 가 쓸 수 있는 컨텍스트는 원래 즉시 컨텍스트 하나뿐이었습니다. 그런데 그 함수는
         *          드로우마다 불리므로 웨이브를 병렬로 기록하면 워커 여럿이 같은 즉시 컨텍스트를 동시에 Map 합니다
         *          (`ID3D11DeviceContext` 는 스레드 안전하지 않습니다). 인터페이스에 리스트를 끼우면 RHI 모듈 ABI 가 바뀌므로 백엔드
         *          안에서 풉니다: `Map(WRITE_DISCARD)` 를 Deferred Context 에 하면 D3D11 런타임이 리스트 단위로 버퍼를 버저닝하므로
         *          그 리스트의 드로우가 **기록 시점의 값**을 봅니다.
         *
         *          예전에는 스레드 로컬이 **컨텍스트 포인터**를 그대로 들었습니다. 리스트를 연 스레드와 닫은 스레드가 다르면(RenderGraph
         *          병렬 웨이브의 첫 리스트: 렌더 스레드가 열어 배리어를 적고 워커가 닫습니다) 연 쪽의 포인터가 리스트보다 오래 살아,
         *          그 리스트가 파괴된 뒤 죽은 컨텍스트에 Map 했습니다(세 번 본 간헐 세그폴트, 2026-09-23). 풀어 주는 자리를 늘리는 것은
         *          "빠뜨리지 않는다" 에 기대는 고침이라 구조를 바꿨습니다. **스레드 로컬은 (디바이스 일련번호 · 슬롯 · 기록 세대) 토큰만
         *          들고, 컨텍스트는 디바이스가 소유한 이 슬롯 표에서 세대가 맞을 때만 나옵니다.** 닫거나 놓으면 세대가 바뀌므로 어느
         *          스레드의 토큰이든 저절로 무효이고, 표는 디바이스와 수명이 같아 죽은 메모리를 가리킬 길이 없습니다.
         */
        uint32 acquireRecordingSlot( ID3D11DeviceContext* pContext );
        /** @brief 슬롯을 반납합니다. 세대를 올려 남은 토큰을 모두 무효로 만듭니다. 잠그지 않습니다(디바이스 종료의 detach 안에서도 불립니다). */
        void releaseRecordingSlot( uint32 slot );
        /** @brief 기록을 시작합니다(`beginCommandList`). 새 세대(홀수)를 열고 이 스레드에 토큰을 묶습니다. */
        void beginRecording( uint32 slot );
        /** @brief 기록을 끝냅니다(`endCommandList`). 세대를 짝수로 올립니다. 어느 스레드가 들고 있든 그 토큰은 이제 아무것도 가리키지 않습니다. */
        void endRecording( uint32 slot );
        /**
         * @brief 이 스레드가 이 Deferred Context 로 기록한다고 알립니다. 리스트를 **다른 스레드가 열었을 때** 기록하는 쪽이 패스 시작에서 부릅니다.
         * @details 기록 중(세대 홀수)인 슬롯의 컨텍스트가 아니면 아무것도 하지 않습니다. 즉시 컨텍스트는 슬롯이 없으므로 묶이지 않습니다.
         *          그쪽은 `_immediateContextMutex` 로 지키는 공유 자원입니다.
         */
        void bindRecordingThread( ID3D11DeviceContext* pContext );
        /** @brief 이 스레드의 토큰이 **이 디바이스의, 지금 기록 중인** 리스트를 가리키면 그 Deferred Context 를, 아니면 nullptr 을 반환합니다. */
        ID3D11DeviceContext* resolveRecordingContext() const;

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
            uint32                                           _width;
            uint32                                           _height;
            uint8                                            _bDepth   : 1;
            uint8                                            _reserved : 7;
            /** @brief 빈 텍스처 레코드로 만듭니다. */
            TextureRecord()
                : _width{ 0 }
                , _height{ 0 }
                , _bDepth{ SW_FALSE }
                , _reserved{ 0 } {}
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
        /** @brief ComPtr 을 핸들 표에 넣고 핸들을 반환합니다. */
        RHIBufferHandle storeBuffer( Microsoft::WRL::ComPtr<ID3D11Buffer> buffer );
        /** @brief 불투명 텍스처 핸들을 TextureRecord 로 풉니다. */
        TextureRecord*       resolveTexture( RHITextureHandle handle );
        const TextureRecord* resolveTexture( RHITextureHandle handle ) const;
        /** @brief TextureRecord 를 핸들 표에 넣고 핸들을 반환합니다. */
        RHITextureHandle storeTexture( TextureRecord record );

        /** @brief setComputeRootConstants 의 실제 용량(dword)입니다. RHITypes.h 의
         *         constant::kMinComputeRootConstantDwords(=DX12 기준, 네 백엔드 공통 안전값) 참고. */
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
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> _deviceContext;
        /** @brief 즉시 컨텍스트 API 를 불러도 되는 스레드입니다(0 = 묶이지 않음 · GT 초기화). */
        std::thread::id _contextOwnerThread;
        /// @brief 창 하나의 백버퍼입니다. 백버퍼 RTV · 크기 · Present 가 모두 여기 모여 있습니다.
        D3D11RHISwapChain _swapChain;

        Microsoft::WRL::ComPtr<ID3D11Buffer> _vertexBuffer; ///< 풀스크린 삼각형(정점 3개). 메시 정점 버퍼가 없는 드로우가 씀

        RHIHandleTable<Microsoft::WRL::ComPtr<ID3D11Buffer>> _gpuBuffers;
        /// @brief 구조버퍼 핸들 → SRV 입니다(그래픽스 VS 가 StructuredBuffer 로 읽습니다. GPUScene 인스턴스 버퍼 등).
        unordered_map<RHIBufferHandle, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> _mapBufferSrv;
        RHIHandleTable<TextureRecord>                                                    _gpuTextures;

        /// @brief 즉시 컨텍스트가 쓰는 기록 상태입니다. 리스트는 각자 자기 것을 갖습니다.
        D3D11RecordingState _recordingState;

        /// @brief 드라이버가 커맨드 리스트를 네이티브로 지원하면 SW_TRUE 입니다. 병렬 기록 능력의 근거입니다.
        uint8 _bDriverCommandLists;

        /// @brief 디스크립터 레지스트리를 보호합니다. 커맨드 기록(D3D11RHICommandContext 의 바인드 경로)이
        /// 인덱스로 이 목록들을 읽는 동안 register/unregister 가 push_back 으로 재할당을 일으키면
        /// 기록 스레드가 쓰레기를 읽습니다. 읽기는 공유 락이라 병렬 기록을 직렬화하지 않습니다.
        mutable std::shared_mutex _bindlessMutex;

        /// @brief **즉시 컨텍스트(`_deviceContext`)를 만지는 모든 코드가 잡아야 하는 자물쇠입니다.**
        /// @details `ID3D11DeviceContext` 는 스레드 안전하지 않습니다. 안전한 것은 `ID3D11Device` 뿐입니다.
        ///          기록은 리스트마다 Deferred Context 라 안전하지만, **리소스 갱신은 즉시 컨텍스트로
        ///          나갑니다**(`updateConstantBuffer` 의 `Map(WRITE_DISCARD)` 등). 그 경로는 드로우마다
        ///          불리므로 웨이브를 병렬로 기록하면 워커 여럿이 같은 즉시 컨텍스트를 동시에 Map 합니다.
        ///          실제로 그 레이스가 `RenderPassGpuTest.AmbientOcclusionReachesBloom` 을 세 번에 두 번
        ///          꼴로 깨뜨렸습니다. 크래시이거나, 패스 CB 가 옆 패스 값으로 덮여 Bloom 이 AO 대신 HDR
        ///          컬러를 샘플링해 화면이 하얗게 탔습니다. 둘 다 같은 원인입니다.
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
        uint32              _timestampFrameIndex{ 0 };
        /// @brief 이번 프레임에 적힌 칸 비트입니다. 패스가 병렬로 기록하므로 원자입니다.
        atomic<uint32>  _timestampWrittenMask{ 0 };
        uint8           _bTimestampEnabled{ SW_FALSE }; ///< 엔진이 켜기 전에는 쿼리도 만들지 않음
        uint8           _bTimestampReady{ SW_FALSE };
        uint8           _bTimestampFrameOpen{ SW_FALSE };
        vector<float32> _listTimestampMicro;

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
        Microsoft::WRL::ComPtr<ID3D11SamplerState>      _linearSampler;
        /// @brief 정적 샘플러 세트입니다(bindingslots.hlsli 4, DX12 와 같은 표). s9..s15 에 겁니다. 셰이더가 SW_SampleIndexWith 의 samplerId 로 고릅니다.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> _arrStaticSampler[shaderslot::kStaticSamplerArrayCount];
        HWND                                       _pHWnd;
        RHIFormat                                  _backBufferFormat; ///< 스왑체인 백버퍼 포맷 (DXGI 는 요청값 그대로)

        /// @brief 살아 있는 `D3D11RHICommandList` 들입니다. **소유하지 않습니다.** 리사이즈 직전에
        ///        기록물을 버리게 하려고 듭니다(백버퍼 참조를 붙들고 있기 때문입니다).
        mutex                        _liveCmdListMutex;
        vector<D3D11RHICommandList*> _listLiveCmd;

        RHIReleaseQueue _releaseQueue;

        sw::unique_ptr<D3D11RHICommandContext> _frameStreamContext;
        sw::unique_ptr<D3D11RHIResource>       _resourceImpl;

        /**
         * @struct D3D11RecordingSlot
         * @brief 리스트 하나의 기록 슬롯입니다. 세대(홀수 = 기록 중)와 그 리스트의 Deferred Context 를 듭니다. 디바이스가 소유하므로 리스트가 죽어도 남습니다.
         */
        struct D3D11RecordingSlot
        {
            atomic<uint64>               _generation; ///< 0 = 한 번도 안 쓴 슬롯. begin 이 홀수로, end · 반납이 짝수로 올린다
            atomic<ID3D11DeviceContext*> _pContext;   ///< 내줄 때 적고 돌려받을 때 비운다(소유하지 않는다). nullptr 이면 빈 슬롯
        };
        D3D11RecordingSlot _arrRecordingSlot[kMaxRecordingSlot];
        uint64             _serial; ///< 디바이스마다 유일. 죽은 디바이스의 토큰이 새 디바이스에 맞아떨어지지 않게
    };
} // namespace sw

#else
namespace sw
{
    /** @brief Windows 가 아닌 환경용 스텁 D3D11RHIDevice 입니다. */
    class D3D11RHIDevice : public IRHIDevice
    {
        friend class D3D11RHICommandContext;

    public:
        /** @brief Windows 가 아닌 환경용 스텁입니다. initialize 는 언제나 실패합니다. */
        D3D11RHIDevice() = default;
        /** @brief 스텁 소멸자입니다. */
        ~D3D11RHIDevice() = default;

        bool initializeInternal( const RHISwapChainDesc& ) override { return false; }
        void shutdownInternal() override {}
        void resizeInternal( uint32, uint32 ) override {}
        void beginFrame( const float4& ) override {}
        void endFrame( bool, bool = true ) override {}

        RHIBackend  getBackendType() const override { return RHIBackend::DirectX11; }
        const utf8* getBackendName() const override { return "Direct3D 11 (Not Supported on non-Windows)"; }

        void* getNativeDevice() const override { return nullptr; }
        void* getNativeContext() const override { return nullptr; }
        void* getNativeCommandQueue() const override { return nullptr; }

        IRHIResource*       getResource() override { return nullptr; }
        IRHICommandContext* getFrameStreamContext() override { return nullptr; }

        sw::unique_ptr<IRHICommandList> createCommandList() override { return nullptr; }
        void                            executeCommandList( IRHICommandList* ) override {}
    };
} // namespace sw
#endif
