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
#include "Core/Container/vector.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHISwapChain.h"
#include "Engine/Graphics/RHI/IRHIContextThreadBinding.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/RHI/Support/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"
#include "Engine/Graphics/RHI/Support/RHITimestamp.h"
#include "Engine/Graphics/RHI/Support/RHITransferSerials.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#include <shared_mutex>

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    struct RHIMemoryBudget;

    class D3D11RHICommandContext;
    class D3D11RHICommandList;
    class D3D11RHIResourceFactory;

    /**
     * @struct D3D11RecordingState
     * @brief "지금 이 Deferred Context 에 무엇이 바인딩되어 있나" 를 나타냅니다. 기록 스트림마다 있어야 하는 상태입니다.
     * @details D3D12 의 `D3D12RecordingState` 와 같은 역할이다. 리스트마다 자기 Deferred Context 를
     *          소유하는데 이 캐시가 디바이스 전역이면 서로의 바인딩 캐시를 덮어써서, 병렬 기록에서 한 패스의
     *          드로우가 다른 패스의 PSO 와 정점 버퍼로 나간다. 주의: 인자 둘짜리 컨텍스트 생성자는 디바이스의
     *          것을 가리키므로 즉시 컨텍스트 전용이다. `D3D11RHICommandList` 는 자기 상태를 넘기는 생성자를 쓴다.
     * @note `OpenGLRecordingState` 는 **반대로 디바이스가 소유하는 것이 맞다.** GL 은 커맨드 버퍼가
     *       없는 상태 머신이라 실제 상태가 하나뿐이다. 기준은 "리스트마다 하나" 가 아니라
     *       **"기록 스트림마다 하나"** 다.
     */
    struct D3D11RecordingState
    {
        /**
         * @brief 루트 상수 에뮬레이션(계약 슬롯 b2)의 버퍼와 CPU 그림자입니다. **기록 스트림마다** 따로 있습니다.
         * @details D3D11 에는 루트 상수가 없어 작은 상수 버퍼로 흉내 낸다. 이것이 디바이스 전역이면
         *          병렬로 기록하는 두 패스가 같은 그림자 배열과 같은 버퍼를 번갈아 덮어써서, 한쪽 패스의
         *          드로우가 **다른 패스의 월드 행렬**로 그려진다.
         */
        Microsoft::WRL::ComPtr<ID3D11Buffer> _rootConstantCb;
        uint32                               _arrRootConstantShadow[shaderslot::kRootConstantDwords]{};

        RHIBufferHandle        _boundMeshVb{ 0 };
        uint32                 _boundMeshStride{ 0 };
        uint32                 _boundMeshOffset{ 0 };
        RHIBufferHandle        _boundInstanceSlotVb{ 0 }; ///< 슬롯 1: 인스턴스 슬롯 스트림(0이면 바인딩 안 됨)
        uint32                 _boundInstanceSlotOffset{ 0 };
        RHIPipelineStateHandle _activeGraphicsPSO{ 0 };
        /**
         * @brief CS UAV 슬롯마다 지금 바인딩되어 있는 버퍼입니다(0이면 없음).
         * @details D3D11 은 같은 리소스를 출력(UAV)과 입력(SRV)에 동시에 바인딩할 수 없다. UAV 를 해제하지 않으면
         *          런타임이 **SRV 쪽을 조용히 NULL 로 강제한다**(경고만 나온다). 그래서 어느 슬롯에
         *          어떤 버퍼가 바인딩되어 있는지 기록해 두고, transitionBuffer 가 읽기 상태로 돌릴 때 해제한다.
         */
        RHIBufferHandle _arrComputeUavBuffer[D3D11_PS_CS_UAV_REGISTER_COUNT]{};
        /**
         * @brief PS SRV 슬롯마다 지금 바인딩되어 있는 텍스처입니다(0이면 없음이거나 버퍼).
         * @details 같은 리소스를 입력(SRV)과 출력(RTV, DSV)에 동시에 바인딩하면 `OMSetRenderTargets` 가 **SRV 를 NULL 로 강제하고** 경고만 낸다.
         *          `prepareTextureForRenderTarget` 가 이 테이블을 보고 그 텍스처가 바인딩된 슬롯만 해제한다(언리얼 D3D11 RHI 의
         *          `ConditionalClearShaderResource` 에 해당한다).
         */
        RHITextureHandle _arrPixelSrvTexture[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class D3D11RHIDevice
     * @brief Direct3D 11 그래픽스 디바이스 구현입니다.
     */
    class D3D11RHIDevice : public IRHIDevice, public IRHITimestampProvider, public IRHIContextThreadBinding
    {
        friend class D3D11RHICommandContext;
        friend class D3D11RHICommandList;

    public:
        friend class D3D11RHIResourceFactory;
        // ------------------------------------------------------------------------------
        // 1) 수명: 디바이스, 스왑체인, 프레임
        // ------------------------------------------------------------------------------
        /** @brief 빈 D3D11 디바이스를 만듭니다. */
        D3D11RHIDevice();
        /** @brief D3D11 자원을 해제합니다. */
        virtual ~D3D11RHIDevice() override;

        /** @brief Direct3D 11 디바이스와 스왑체인(백버퍼 RTV 포함)을 만듭니다. */
        bool initializeInternal( const RHISwapChainDesc& desc ) override;

        /** @brief D3D11 자원을 해제합니다. */
        void shutdownInternal() override;

        /** @brief 종료 3단계입니다. 프레임 스트림 컨텍스트를 해제하고 살아 있는 커맨드 리스트를 분리합니다. */
        void detachCommandRecordingInternal() override;

        /** @brief 스왑체인 백버퍼를 새 크기로 다시 만듭니다. */
        void resizeInternal( uint32 width, uint32 height ) override;

        /** @brief 프레임을 엽니다(타임스탬프, 백버퍼 RTV 재취득, 정적 샘플러). 백버퍼 클리어는 beginRenderPass(핸들 0)가 합니다. */
        void beginFrame() override;

        /** @brief 프레임을 닫고 Present 합니다(bPresent=false 면 Present 를 생략합니다). */
        void endFrame( bool vsync = true, bool bPresent = true ) override;

        void               setTimestampEnabled( bool bEnabled ) override { _bTimestampEnabled = bEnabled ? SW_TRUE : SW_FALSE; }
        uint32             getTimestampSlotCount() const override;
        [[nodiscard]] bool readTimestamps( RHITimestampFrame& outFrame ) override;
        [[nodiscard]] bool readGPUClockNanos( int64& outGPUNanos ) override;
        bool               isGPUClockReadCheap() const override { return false; }
        /**
         * @brief 커맨드 리스트가 **자기 Deferred Context 에** 타임스탬프를 기록합니다.
         * @details 슬롯 번호는 패스 인덱스로 고정이라 쿼리 객체 하나를 두 컨텍스트가 건드릴 일이 없다.
         *          D3D11 이 금지하는 것이 바로 그 경우다.
         */
        void writeTimestampSlot( ID3D11DeviceContext* pContext, uint32 slotIndex );
        /** @brief D3D11 디버그 레이어의 CORRUPTION/ERROR 메시지를 로그로 비웁니다(SW_DEBUG, 프레임 끝). */
        void flushDebugMessages( const utf8* pStage );

        IRHIResourceFactory* getResourceFactory() override;
        /** @brief 프레임 스트림 컨텍스트(즉시 컨텍스트)입니다. 백버퍼 패스와 Present 가 여기에 기록합니다. */
        IRHICommandContext* getFrameStreamContext() override;

        /** @brief GPU 가 유휴 상태가 될 때까지 기다린 뒤 해제 큐를 비웁니다. */
        void waitIdleInternal() override;

        /** @brief DXGI `QueryVideoMemoryInfo` 로 이 프로세스의 사용량(로컬과 비로컬)과 로컬 예산을 채웁니다. 어댑터를 찾지 못하면 false 입니다. */
        [[nodiscard]] bool queryGPUMemoryBudgetInternal( RHIMemoryBudget& outBudget ) override;

        /** @brief 지금 기록 중인 프레임을 GPU 가 지난 뒤에 부릅니다. D3D11 은 펜스 대신 프레임 끝마다 내는 이벤트 쿼리(`_arrFrameDoneQuery`)로 확인합니다. */
        void   enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate ) override;
        uint32 getPendingReleaseCount() const override { return _releaseQueue.getPendingReleaseCount(); }

        /** @brief 백엔드 종류(DirectX11)를 반환합니다. */
        RHIBackend getBackendType() const override { return RHIBackend::DirectX11; }
        /**
         * @brief 정적 샘플러 세트를 컨텍스트의 VS · PS 슬롯 s9..s15 에 바인딩합니다.
         * @details 즉시 컨텍스트는 초기화, 리사이즈(ClearState 뒤), beginFrame 에서, 지연 컨텍스트는 beginCommandList 마다 부른다.
         *          FinishCommandList 와 ClearState 가 컨텍스트 상태를 비우기 때문이다. 엔진 슬롯 결합 샘플러(s0..s3)는 bindShaderResource 가 바인딩한다. 정점 · 픽셀 셰이더 둘 다에 건다.
         */
        void bindStaticSamplers( ID3D11DeviceContext* pContext ) const;
        /** @brief 스왑체인이 만든 백버퍼 포맷입니다. 백버퍼 PSO 의 렌더 타깃 포맷이 여기서 나옵니다. */
        RHIFormat getBackBufferFormat() const override { return _backBufferFormat; }

        /** @brief VS 가 GPUScene 인스턴스 버퍼를 구조 버퍼 SRV(g_SwInstances, t4)로 읽을 수 있어 true 입니다. */
        bool supportsInstancedSceneDraw() const override { return true; }

    #if !defined( SW_SHIPPING )
        /** @brief 텍스처 · 버퍼 등록 표의 그 자리에 핸들이 있으면 true 입니다(DX11 은 인덱스를 슬롯 바인딩으로 흉내 낸다). */
        bool isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const override;
    #endif

        /**
         * @brief 병렬 커맨드 기록 지원 여부를 런타임에 반영해 반환합니다.
         * @details 리스트마다 자기 Deferred Context 를 소유하므로 구조적으로는 병렬 기록이 가능하다.
         *          하지만 드라이버가 커맨드 리스트를 네이티브로 지원하지 않으면 D3D11 런타임이 소프트웨어로
         *          에뮬레이션해서 병렬화 이득보다 오버헤드가 커진다. 그래서 정적 테이블을 그대로 쓰지 않고
         *          `D3D11_FEATURE_THREADING` 조회 결과로 이 항목만 덮어쓴다.
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

        /** @brief 즉시 컨텍스트가 있는지 답합니다. MakeCurrent 가 없어 바인딩할 것은 없습니다. 즉시 컨텍스트는 소유 스레드에서만 씁니다. */
        bool bindGraphicsContext() override;
        /** @brief MakeCurrent 가 없어 놓을 것이 없습니다. 소유 스레드를 바꾸는 것은 부르는 쪽(RenderThread)의 순서가 정합니다. */
        void unbindGraphicsContext() override {}

        /** @brief D3D11 은 커맨드 큐 객체가 없어 nullptr 을 반환합니다. */
        void* getNativeCommandQueue() const override { return nullptr; }

        /** @brief 네이티브 텍스처 포인터(ID3D11Texture2D*)를 반환합니다. */
        void* getNativeTexturePointer( RHITextureHandle texture ) const override;

        /** @brief 커맨드 리스트를 만듭니다. 리스트마다 자기 Deferred Context 를 가집니다. */
        unique_ptr<IRHICommandList> createCommandList() override;

        /** @brief 커맨드 리스트를 즉시 컨텍스트의 현재 위치에서 실행합니다(ExecuteCommandList). */
        void executeCommandList( IRHICommandList* pCmdList ) override;

        /** @brief 살아 있는 커맨드 리스트를 등록합니다(소유하지 않는 참조). */
        void registerCommandList( D3D11RHICommandList* pCmdList );
        /** @brief 등록을 해제합니다. */
        void unregisterCommandList( D3D11RHICommandList* pCmdList );

    private:
        /** @brief 쿼리 묶음을 한 번만 만듭니다. 만들지 못하면 이 백엔드는 타임스탬프를 보고하지 않습니다. */
        void ensureTimestampResources();
        /** @brief 다시 쓰기 직전의 묶음에서 결과를 마이크로초로 변환합니다(기다리지 않습니다). */
        void collectTimestampsForSlot();
        /**
         * @brief 방금 끝낸 프레임에 완료 쿼리를 내고, GPU 가 지난 프레임까지의 `enqueueGPURelease` 콜백을 부릅니다(`endFrame` 이 부릅니다).
         * @details 링이 가득 차면(프레임 `kMaxFrameCountInFlight` 개가 GPU 에 남음) 가장 오래된 프레임이 끝날 때까지 기다린다 — DX12 · Vulkan 의 링 슬롯 대기와 같다.
         *          기다리지 않으면 Present 를 하지 않는 반복(오프스크린)에서 GPU 가 얼마든지 뒤처져 "GPU 가 끝난 뒤" 라는 콜백의 약속이 깨진다.
         */
        void retireFrameOnGPU();

        /** @brief 전송 하나의 완료를 알릴 이벤트 쿼리를 풀에서 꺼내거나 만듭니다. 즉시 컨텍스트 잠금 안에서 부릅니다. 못 만들면 nullptr 입니다(이유는 로그). */
        Microsoft::WRL::ComPtr<ID3D11Query> acquireTransferQuery();
        /**
         * @brief 방금 낸 전송 뒤에 이벤트 쿼리를 끝으로 달고 시리얼을 발급합니다. 즉시 컨텍스트 잠금 안에서, 전송 명령을 낸 직후에 부릅니다(시리얼은 제출 순서대로 같은 락 안에서 매긴다).
         * @return 발급한 시리얼입니다.
         */
        uint64 endTransfer( Microsoft::WRL::ComPtr<ID3D11Query> query );
        /** @brief 앞에서부터 끝난 전송을 풀로 돌려주고 워터마크를 올립니다. `Flush` 로 GPU 에 내보내되 기다리지 않습니다. 즉시 컨텍스트 잠금 안에서 부릅니다. */
        void retireFinishedTransfers();

        /// @brief 버퍼와 그 SRV 입니다. SRV 는 셰이더가 버퍼로 읽는 것(구조 버퍼와 인다이렉트 인자 버퍼)에만 있습니다.
        struct BufferRecord
        {
            Microsoft::WRL::ComPtr<ID3D11Buffer>             _buffer;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _srv;
        };

        /// @brief 텍스처와 그 뷰(SRV, RTV, UAV)입니다.
        struct TextureRecord
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D>          _texture;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   _rtv;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   _dsv;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _srv;
            /// @brief 면(배열 원소, 큐브 면)마다의 RTV 와 DSV 입니다. 면이 하나면 비어 있고 `_rtv` 와 `_dsv` 를 씁니다(`_rtv` 와 `_dsv` 는 항상 면 0 입니다).
            vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> _listSliceRtv;
            vector<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>> _listSliceDsv;
            uint32                                                 _width;
            uint32                                                 _height;
            uint32                                                 _arraySize; ///< 면 수
            RHITextureDimension                                    _dimension;
            /// @brief 이 텍스처에 마지막으로 제출한 쓰기 전송(업로드)의 시리얼입니다. 0 이면 전송한 적이 없습니다. bindless 등록이 이 전송이 끝났는지 확인합니다(`RHITransferSerials::isTextureReadyToRegister`).
            atomic<uint64> _lastTransferSerial;
            uint8          _bDepth   : 1;
            uint8          _reserved : 7;
            /** @brief 빈 텍스처 레코드로 만듭니다. */
            TextureRecord()
                : _listSliceRtv{}
                , _listSliceDsv{}
                , _width{ 0 }
                , _height{ 0 }
                , _arraySize{ 1 }
                , _dimension{ RHITextureDimension::Texture2D }
                , _lastTransferSerial{ 0 }
                , _bDepth{ SW_FALSE }
                , _reserved{ 0 } {}

            /** @brief 면 `slice` 의 RTV 입니다. 없으면 nullptr 입니다. */
            ID3D11RenderTargetView* findSliceRtv( uint32 slice ) const
            {
                if ( _listSliceRtv.empty() )
                    return slice == 0 ? _rtv.Get() : nullptr;
                return slice < _listSliceRtv.size() ? _listSliceRtv[slice].Get() : nullptr;
            }
            /** @brief 면 `slice` 의 DSV 입니다. 없으면 nullptr 입니다. */
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
        /** @brief UAV 인덱스가 가리키는 원본 버퍼 핸들입니다(없으면 0). SRV/UAV 해저드를 해소할 때 씁니다. */
        RHIBufferHandle uavSourceBufferAt( RHIDescriptorIndex index ) const;

        /** @brief 이 기록 스트림의 루트 상수 CB 를 확보합니다(없으면 만듭니다). */
        bool ensureRootConstantCb( D3D11RecordingState& state );
        /**
         * @brief 살아 있는 모든 기록 상태에서 이 버퍼와 PSO 캐시를 지웁니다.
         * @details 캐시가 리스트마다 있으므로 자원이 사라질 때 **모두** 훑어야 한다. 한 곳만 지우면
         *          다른 리스트가 죽은 핸들을 드로우 시점에 다시 참조한다.
         */
        void forgetBufferInRecordingStates( RHIBufferHandle buffer );
        void forgetPipelineStateInRecordingStates( RHIPipelineStateHandle pso );
        /** @brief 불투명 버퍼 핸들로 ID3D11Buffer 를 찾습니다. */
        ID3D11Buffer* resolveBuffer( RHIBufferHandle handle ) const;
        /**
         * @brief 불투명 버퍼 핸들의 SRV 입니다(없으면 nullptr). 락 없이 읽습니다. 레코드는 핸들 테이블의 슬롯에 있어 위치가 바뀌지 않습니다.
         * @note 버퍼를 지우는 일(`destroyBuffer`)이 기록과 겹치면 안 됩니다. 다른 스레드는 `IRHIDevice::releaseHandle` 로 해제합니다.
         */
        ID3D11ShaderResourceView* resolveBufferSrv( RHIBufferHandle handle ) const;
        /**
         * @brief 버퍼(와 있으면 SRV)를 핸들 테이블에 넣고 핸들을 반환합니다. GPU 메모리 장부의 Buffer 항목에 크기(`ByteWidth`)를 올리는 유일한 지점입니다.
         * @details SRV 는 넣기 **전에** 레코드에 싣는다. 넣은 뒤에 붙이면 다른 스레드가 SRV 없는 레코드를 볼 수 있다.
         */
        RHIBufferHandle storeBuffer( Microsoft::WRL::ComPtr<ID3D11Buffer> buffer, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv = nullptr );
        /**
         * @brief 버퍼에 `srvDesc` 로 SRV 를 만들어 버퍼와 함께 핸들 테이블에 넣습니다. SRV 를 만들지 못하면 버퍼만 넣습니다.
         * @details 구조 버퍼와 인다이렉트 인자 버퍼 생성이 함께 쓴다(뷰 설명만 다르다).
         */
        RHIBufferHandle storeBufferWithSrv( Microsoft::WRL::ComPtr<ID3D11Buffer> buffer, const D3D11_SHADER_RESOURCE_VIEW_DESC& srvDesc );
        /** @brief 불투명 텍스처 핸들로 TextureRecord 를 찾습니다. */
        TextureRecord*       resolveTexture( RHITextureHandle handle );
        const TextureRecord* resolveTexture( RHITextureHandle handle ) const;
        /**
         * @brief TextureRecord 를 핸들 테이블에 넣고 핸들을 반환합니다. GPU 메모리 장부에 `desc` 의 항목과 논리 크기를 올리는 유일한 지점입니다.
         * @details D3D11 에는 할당 크기를 물을 API 가 없어 서술로 계산한 논리 크기를 기록한다(장부 기준은 Logical).
         */
        RHITextureHandle storeTexture( TextureRecord record, const RHITextureDesc& desc );

        /** @brief setComputeRootConstants 의 실제 용량(dword)입니다. 네 백엔드 공통 안전값은
         *         shaderslot::kRootConstantDwords(DX12 와 Vulkan 의 루트 상수, 푸시 상수 크기)입니다. */

        /// @brief VS/PS 와 래스터, 블렌드, 깊이 상태 묶음입니다.
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
        Microsoft::WRL::ComPtr<IDXGIAdapter3>       _memoryAdapter; ///< 디바이스가 쓰는 어댑터. GPU 메모리 사용량과 예산을 질의하는 데 쓴다.
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> _deviceContext;
        RHIFormat                                   _backBufferFormat; ///< 스왑체인 백버퍼 포맷(DXGI 는 요청값 그대로)
        /// @brief 창 하나의 백버퍼입니다. 백버퍼 RTV, 크기, Present 가 모두 여기 모여 있습니다.
        D3D11RHISwapChain _swapChain;

        Microsoft::WRL::ComPtr<ID3D11Buffer> _vertexBuffer; ///< 풀스크린 삼각형(정점 3개). 메시 정점 버퍼가 없는 드로우가 쓴다.

        /// @brief 버퍼 핸들에서 버퍼와 SRV 를 찾는 테이블입니다(텍스처 `_gpuTextures` 와 같은 구조). 기록 스레드가 SRV 를 락 없이 읽습니다(`resolveBufferSrv`).
        RHIHandleTable<BufferRecord>  _gpuBuffers;
        RHIHandleTable<TextureRecord> _gpuTextures;

        /// @brief 즉시 컨텍스트가 쓰는 기록 상태입니다. 리스트는 각자 자기 것을 가집니다.
        D3D11RecordingState _recordingState;

        /// @brief 디스크립터 레지스트리를 보호합니다. 커맨드 기록(D3D11RHICommandContext 의 바인드 경로)이
        /// 인덱스로 이 목록들을 읽는 동안 register/unregister 가 push_back 으로 재할당을 일으키면
        /// 기록 스레드가 쓰레기를 읽습니다. 읽기는 공유 락이라 병렬 기록을 직렬화하지 않습니다.
        mutable std::shared_mutex _bindlessMutex;

        /// @brief **즉시 컨텍스트(`_deviceContext`)를 건드리는 모든 코드가 잡아야 하는 락입니다.**
        /// @details `ID3D11DeviceContext` 는 스레드 안전하지 않습니다. 안전한 것은 `ID3D11Device` 뿐입니다.
        ///          기록은 리스트마다 Deferred Context 라 안전하지만, `IRHIResourceFactory` 의 갱신(`updateConstantBuffer` 의
        ///          `Map(WRITE_DISCARD)` 등)과 프레임 스트림 컨텍스트는 즉시 컨텍스트로 나가므로 여기서 잠급니다. 기록 중의
        ///          상수 버퍼 갱신은 `IRHICommandList::updateConstantBuffer` 로 리스트의 Deferred Context 에 갑니다. 드로우마다
        ///          불리는 그 경로가 즉시 컨텍스트로 오면 병렬 기록의 워커들이 한 버퍼를 덮어써서 패스 CB 가 옆 패스 값으로 바뀝니다.
        mutable mutex _immediateContextMutex;

        /**
         * @struct D3D11TimestampFrame
         * @brief 프레임 하나분의 타임스탬프 쿼리 묶음입니다.
         * @details D3D11 은 틱을 초로 바꿀 주파수를 disjoint 쿼리로만 준다. 그 구간 안에서 GPU
         *          클럭이 바뀌었으면 `Disjoint` 가 서고, 그 프레임 값은 통째로 버려야 한다.
         */
        struct D3D11TimestampFrame
        {
            Microsoft::WRL::ComPtr<ID3D11Query> _disjoint;
            Microsoft::WRL::ComPtr<ID3D11Query> _arrQuery[constant::kMaxGPUTimestampSlot];
            GPUTimestampSlotMask                _writtenMask{ 0 };
            uint8                               _bPending{ SW_FALSE };
        };

        /**
         * @brief GPU 타임스탬프입니다. 프레임 링만큼 묶음을 돌려 씁니다.
         * @details 읽기는 그 묶음을 **다시 쓰기 직전**(링 한 바퀴 뒤)에 DONOTFLUSH 로 한 번만 묻는다.
         *          아직 결과가 없으면 이번 바퀴는 건너뛴다. 기다리면 측정하려던 파이프라인을 멈춰 세운다.
         */
        D3D11TimestampFrame _arrTimestampFrame[constant::kMaxFrameCountInFlight];
        uint32              _timestampFrameIndex;
        /// @brief 이번 프레임에 기록된 슬롯 비트입니다. 패스가 병렬로 기록하므로 atomic 입니다.
        atomic<GPUTimestampSlotMask> _timestampWrittenMask;
        uint8                        _bTimestampEnabled; ///< 엔진이 켜기 전에는 쿼리도 만들지 않는다.
        uint8                        _bTimestampReady;
        uint8                        _bTimestampFrameOpen;
        /// @brief 드라이버가 커맨드 리스트를 네이티브로 지원하면 SW_TRUE 입니다. 병렬 기록 능력을 판단하는 근거입니다.
        uint8             _bDriverCommandLists;
        RHITimestampFrame _timestampFrame; ///< 마지막으로 읽은 프레임(`readTimestamps`)
        /**
         * @brief GPU 시계 읽기(`readGPUClockNanos`)용 타임스탬프 쿼리입니다. 처음 읽을 때 만듭니다.
         * @details disjoint 로 감싸지 않는다. 프레임의 disjoint 가 열려 있는 동안 불리고, 주파수는 마지막 프레임의 disjoint 값을 쓴다.
         */
        Microsoft::WRL::ComPtr<ID3D11Query> _clockQuery;
        /// @brief `waitIdleInternal` 이 GPU 가 앞선 일을 모두 끝냈는지 묻는 이벤트 쿼리입니다. 디바이스를 세울 때 만들고, 못 만들면 초기화가 실패합니다.
        Microsoft::WRL::ComPtr<ID3D11Query> _idleQuery;
        /// @brief 프레임마다 끝에 내는 이벤트 쿼리 링입니다(`retireFrameOnGPU`). 프레임 번호 n 의 칸은 `( n - 1 ) % kMaxFrameCountInFlight` 입니다. 디바이스를 세울 때 만듭니다.
        Microsoft::WRL::ComPtr<ID3D11Query> _arrFrameDoneQuery[constant::kMaxFrameCountInFlight];
        uint64                              _frameSerial;            ///< `endFrame` 이 끝낸 프레임 수입니다. `enqueueGPURelease` 는 다음 프레임 번호(`_frameSerial + 1`)에 맡깁니다.
        uint64                              _completedFrameSerial;   ///< GPU 가 지난 것이 확인된 가장 큰 프레임 번호입니다.
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
        /// @brief 정적 샘플러 세트입니다(bindingslots.hlsli 4, DX12 와 같은 테이블). s9..s15 에 바인딩합니다. 셰이더가 Bindless::sampleIndexWith 의 samplerID 로 고릅니다.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> _arrStaticSampler[shaderslot::kStaticSamplerArrayCount];
        HWND                                       _pHWnd;

        /// @brief 살아 있는 `D3D11RHICommandList` 들입니다. **소유하지 않습니다.** 리사이즈 직전에
        ///        기록물을 버리게 하려고 가지고 있습니다(백버퍼 참조를 붙들고 있기 때문입니다).
        mutex                        _liveCmdListMutex;
        vector<D3D11RHICommandList*> _listLiveCmd;

        RHIReleaseQueue _releaseQueue;

        /// @brief 읽기 하나가 들고 있는 것입니다. 복사한 스테이징 텍스처와, 결과 바이트를 꺼낼 때 필요한 배치입니다(`takeTextureReadback` 이 `Map( DO_NOT_WAIT )` 로 읽습니다).
        struct TextureReadbackRecord
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> _staging;
            uint32                                  _width{ 0 };
            uint32                                  _height{ 0 };
            uint32                                  _rowBytes{ 0 };
            uint32                                  _sizeBytes{ 0 };
            uint8                                   _bDepth{ SW_FALSE };
        };

        /// @brief 전송 티켓의 시리얼 발급기와 완료 워터마크입니다(DX11 은 즉시 컨텍스트 하나라 제출 순서가 곧 실행 순서입니다).
        RHITransferSerials _transferSerials;
        /// @brief 제출한 전송마다 이벤트 쿼리 하나를 제출 순서대로 쌓습니다. 앞에서부터 끝난 것만 워터마크를 올립니다. 쿼리는 `_immediateContextMutex` 안에서만 만지고 풀(`_listFreeTransferQuery`)로 돌려 씁니다.
        RHITransferFenceQueue<Microsoft::WRL::ComPtr<ID3D11Query>> _transferQueue;
        /// @brief 끝난 이벤트 쿼리 풀입니다. `_immediateContextMutex` 로 보호합니다.
        vector<Microsoft::WRL::ComPtr<ID3D11Query>> _listFreeTransferQuery;
        /// @brief 아직 가져가지 않은 텍스처 읽기입니다(시리얼로 찾습니다).
        RHIPendingReadbackTable<TextureReadbackRecord> _readbackTable;

        sw::unique_ptr<D3D11RHICommandContext>  _frameStreamContext;
        sw::unique_ptr<D3D11RHIResourceFactory> _resourceImpl;
    };
} // namespace sw

#endif
