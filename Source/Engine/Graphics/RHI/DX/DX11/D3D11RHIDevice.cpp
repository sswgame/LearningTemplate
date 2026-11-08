#include "pch.h"

#include "Engine/Graphics/RHI/DX/DX11/D3D11RHIDevice.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Log/LogOnce.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/RHI/DX/DX11/D3D11RHICommandContext.h"
#include "Engine/Graphics/RHI/DX/DX11/D3D11RHICommandList.h"
#include "Engine/Graphics/RHI/DX/DX11/D3D11RHIResourceFactory.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/Diagnostics/RHITimestampFrame.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Graphics/RHI/DX/DXGITearing.h"
    #include "Engine/Graphics/RHI/Backend/RHILiveCommandListUtil.h"
    #include <thread>

    #include "Engine/Foundation/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineDefaultAssets.h"
    #include "Engine/Graphics/RHI/DX/DXGIAdapter.h"
    #include "Engine/Graphics/RHI/DX/DXGIFormat.h"
    #include "Engine/Graphics/RHI/DX/DXGIMemoryBudget.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    namespace
    {
    #if defined( SW_DEBUG )
        // 아래 목록과 판별 함수는 디버그 레이어 메시지를 거르는 `flushDebugMessages` 전용이고, 그 함수의
        // 본문 전체가 SW_DEBUG 안에 있다. 가드를 맞추지 않으면 Release 빌드에서 "정의했는데 아무도
        // 쓰지 않는다"(-Wunused-function)가 남는다.

        /** @brief 출력과 입력에 같은 리소스가 동시에 걸렸을 때 D3D11 이 내는 메시지 ID 목록입니다. */
        constexpr D3D11_MESSAGE_ID arrHazardMessageID[] = {
            D3D11_MESSAGE_ID_DEVICE_VSSETSHADERRESOURCES_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_PSSETSHADERRESOURCES_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_GSSETSHADERRESOURCES_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_HSSETSHADERRESOURCES_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_DSSETSHADERRESOURCES_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_CSSETSHADERRESOURCES_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_CSSETUNORDEREDACCESSVIEWS_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_OMSETRENDERTARGETSANDUNORDEREDACCESSVIEWS_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_OMSETRENDERTARGETS_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_SOSETTARGETS_HAZARD,
        };

        /**
         * @brief 리소스가 출력과 입력에 동시에 걸린 "해저드" 메시지인지 판별합니다.
         * @details D3D11 은 이것을 **WARNING** 으로 냅니다. 그런데 결과는 조용한 실패입니다. 런타임이 한쪽을
         *          NULL 로 강제하고 셰이더는 0 을 읽습니다(예: 인스턴스 버퍼 t4 가 컴퓨트 UAV 에 걸린 채
         *          남으면 화면에 아무것도 안 그려집니다). 심각도로만 거르면 로그에 한 줄도 안 나오므로
         *          해저드만은 ERROR 로 올립니다.
         */
        bool isHazardMessage( D3D11_MESSAGE_ID id )
        {
            // switch 로 적으면 -Wswitch-enum 이 나머지 1318개를 다루라고 요구한다. 경고를 끄는
            // 대신 목록을 순회한다. ID 를 더 넣을 때도 한 줄이다.
            for ( const D3D11_MESSAGE_ID hazardID : arrHazardMessageID )
            {
                if ( id == hazardID )
                    return true;
            }
            return false;
        }
    #endif

        constexpr uint32 kDefaultNumerator   = 60;
        constexpr uint32 kDefaultDenominator = 1;

        struct D3D11RHIDeviceInternal
        {
            /** @brief GPU 시계 읽기가 기다리는 상한입니다. 큐에 프레임 몇 개가 쌓여 있어도 넉넉하다. */
            static constexpr uint32 kClockReadTimeoutMilliseconds = 500;
            /** @brief `waitIdle` 과 프레임 링 대기가 GPU 를 기다리는 상한입니다. 넘으면 오류를 남기고 이어간다(끝나지 않은 일이 있다는 뜻이라 조용히 넘기지 않는다). */
            static constexpr uint32 kIdleWaitTimeoutMilliseconds = 10000;
        };
    } // namespace

    D3D11RHIDevice::D3D11RHIDevice( uint32 deviceGeneration )
        : _device{ nullptr }
        , _memoryAdapter{ nullptr }
        , _immediateContext{}
        , _backBufferFormat{ constant::render::kBackBufferFormat }
        , _swapChain{}
        , _gpuBuffers{}
        , _gpuTextures{}
        , _timestampFrameIndex{ 0 }
        , _timestampWrittenMask{}
        , _bTimestampEnabled{ SW_FALSE }
        , _bTimestampReady{ SW_FALSE }
        , _bTimestampFrameOpen{ SW_FALSE }
        , _bDriverCommandLists{ SW_FALSE }
        , _timestampFrame{}
        , _clockQuery{}
        , _idleQuery{}
        , _arrFrameDoneQuery{}
        , _frameSerial{ 0 }
        , _completedFrameSerial{ 0 }
        , _lastTimestampFrequency{ 0 }
        , _listRegisteredBindless{}
        , _freeBindless{}
        , _listRegisteredTexture{}
        , _freeTexture{}
        , _listRegisteredUAV{}
        , _listUAVSourceBuffer{}
        , _freeUAV{}
        , _pipelineStates{}
        , _renderPassRegistry{}
        , _arrStaticSampler{}
        , _pHWnd{ nullptr }
        , _releaseQueue{ constant::render::kGPUReleaseFrameLatency }
        , _transferSerials{ deviceGeneration }
        , _transferQueue{ _transferSerials }
        , _listFreeTransferQuery{}
        , _readbackTable{}
        , _uploadStagingBudget{}
        , _directUploadMaxBytes{ 0 }
        , _frameStreamContext{ nullptr }
        , _resourceImpl{ nullptr }
        , _timestampProvider{ *this }
        , _contextThreadBinding{ *this }
    {
        _resourceImpl          = sw::make_unique<D3D11RHIResourceFactory>( this );
        _pTimestampProvider    = &_timestampProvider;
        _pContextThreadBinding = &_contextThreadBinding;
    }

    D3D11RHIDevice::~D3D11RHIDevice()
    {
        shutdown();
    }

    IRHIResourceFactory* D3D11RHIDevice::getResourceFactory()
    {
        return _resourceImpl.get();
    }
    IRHICommandContext* D3D11RHIDevice::getFrameStreamContext()
    {
        return _frameStreamContext.get();
    }

    void D3D11RHIDevice::bindStaticSamplers( ID3D11DeviceContext* pContext ) const
    {
        if ( pContext == nullptr || _arrStaticSampler[0] == nullptr )
            return;
        ID3D11SamplerState* arrSampler[shaderslot::kStaticSamplerArrayCount]{};
        for ( uint32 samplerIndex = 0; samplerIndex < shaderslot::kStaticSamplerArrayCount; ++samplerIndex )
        {
            arrSampler[samplerIndex] = _arrStaticSampler[samplerIndex].Get();
        }
        pContext->PSSetSamplers( shaderslot::dx11::kStaticSampler0, shaderslot::kStaticSamplerArrayCount, arrSampler );
        // 정점 셰이더도 머티리얼 텍스처를 읽는다(툰 외곽선 두께) — 같은 세트를 건다.
        pContext->VSSetSamplers( shaderslot::dx11::kStaticSampler0, shaderslot::kStaticSamplerArrayCount, arrSampler );
        // 변위 머티리얼의 도메인 스테이지가 높이 맵을 읽는다(material_displacement.hlsli).
        pContext->DSSetSamplers( shaderslot::dx11::kStaticSampler0, shaderslot::kStaticSamplerArrayCount, arrSampler );
    }

    void D3D11RHIDevice::flushDebugMessages( const utf8* pStage )
    {
    #if defined( SW_DEBUG )
        // 디버그 레이어의 CORRUPTION/ERROR 만 로그로 남긴다. WARNING(null 샘플러 → 기본 상태 등)은 정상 경로에서도 매 드로우
        // 나오므로 버린다. DX12 의 flushDebugMessages 와 같은 시점(프레임 끝)에 부른다.
        Microsoft::WRL::ComPtr<ID3D11InfoQueue> queue;
        const bool                              bInfoQueueUnavailable = _device == nullptr || FAILED( _device.As( &queue ) ) || queue == nullptr;
        if ( bInfoQueueUnavailable )
            return;
        const UINT64 count = queue->GetNumStoredMessages();
        for ( UINT64 messageIndex = 0; messageIndex < count; ++messageIndex )
        {
            SIZE_T length = 0;
            queue->GetMessage( messageIndex, nullptr, &length );
            vector<uint8>  bytes( length );
            D3D11_MESSAGE* pMessage = reinterpret_cast<D3D11_MESSAGE*>( bytes.data() );
            if ( FAILED( queue->GetMessage( messageIndex, pMessage, &length ) ) )
                continue;
            const bool bReportableMessage = pMessage->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION || pMessage->Severity == D3D11_MESSAGE_SEVERITY_ERROR ||
                                            isHazardMessage( pMessage->ID );
            if ( bReportableMessage && isHazardMessage( pMessage->ID ) )
                SW_LOG_ERROR_ID( "RHI.ValidationHazard", "[%#] %#", pStage, pMessage->pDescription );
            else if ( bReportableMessage )
                SW_LOG_ERROR_ID( "RHI.ValidationMessage", "[%#] %#", pStage, pMessage->pDescription );
        }
        queue->ClearStoredMessages();
    #else
        (void)pStage;
    #endif
    }

    void* D3D11RHIDevice::getNativeTexturePointer( RHITextureHandle texture ) const
    {
        const TextureRecord* pRecord = resolveTexture( texture );
        return pRecord != nullptr ? pRecord->_texture.Get() : nullptr;
    }

    void D3D11RHIDevice::withNativeContextInternal( const RHINativeContextVisitor& visitor ) const
    {
        if ( _immediateContext.isValid() == false )
        {
            visitor( nullptr );
            return;
        }
        const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
        visitor( context.get() );
    }

    bool D3D11RHIDevice::ensureRootConstantCb( D3D11RecordingState& state )
    {
        if ( state._rootConstantCb != nullptr )
            return true;
        if ( _device == nullptr )
            return false;

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth      = shaderslot::kRootConstantDwords * sizeof( uint32 );
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        // `ID3D11Device::CreateBuffer` 는 free-threaded 라 병렬 기록 중에 만들어도 된다.
        // 스레드마다 자기 상태의 버퍼를 만들 뿐, 공유 테이블을 건드리지 않는다.
        if ( isHrSuccess( _device->CreateBuffer( &desc, nullptr, state._rootConstantCb.GetAddressOf() ), "CreateBuffer" ) == false )
        {
            SW_LOG_ERROR_ID( "D3D11.FailedCreateRootConstant", "Failed to create root-constant cbuffer." );
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------------------------
    // 리소스 조회와 저장: 핸들 테이블과 bindless 목록 (D3D11RHIResourceFactory 와 컨텍스트가 쓴다)
    // ------------------------------------------------------------------------------

    ID3D11Buffer* D3D11RHIDevice::resolveBuffer( RHIBufferHandle handle ) const
    {
        const BufferRecord* pRecord = _gpuBuffers.get( handle );
        return pRecord != nullptr ? pRecord->_buffer.Get() : nullptr;
    }

    ID3D11ShaderResourceView* D3D11RHIDevice::resolveBufferSRV( RHIBufferHandle handle ) const
    {
        const BufferRecord* pRecord = _gpuBuffers.get( handle );
        return pRecord != nullptr ? pRecord->_srv.Get() : nullptr;
    }

    size_t D3D11RHIDevice::bindlessBufferCount() const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        return _listRegisteredBindless.size();
    }

    RHIBufferHandle D3D11RHIDevice::bindlessBufferAt( RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredBindless.size() )
            return 0;
        return _listRegisteredBindless[index];
    }

    RHITextureHandle D3D11RHIDevice::bindlessTextureAt( RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredTexture.size() )
            return 0;
        return _listRegisteredTexture[index];
    }

    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> D3D11RHIDevice::bindlessUAVAt( RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredUAV.size() )
            return nullptr;
        return _listRegisteredUAV[index];
    }

    RHIBufferHandle D3D11RHIDevice::uavSourceBufferAt( RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listUAVSourceBuffer.size() )
            return RHIBufferHandle{ 0 };
        return _listUAVSourceBuffer[index];
    }

    RHIBufferHandle D3D11RHIDevice::storeBuffer( Microsoft::WRL::ComPtr<ID3D11Buffer> buffer, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv )
    {
        if ( buffer == nullptr )
            return 0;
        D3D11_BUFFER_DESC bufferDesc{};
        buffer->GetDesc( &bufferDesc );
        BufferRecord record{};
        record._buffer               = std::move( buffer );
        record._srv                  = std::move( srv );
        const RHIBufferHandle handle = _gpuBuffers.insert( std::move( record ) );
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeBuffer( handle ), RHIMemoryKind::Buffer, bufferDesc.ByteWidth );
        return handle;
    }

    RHIBufferHandle D3D11RHIDevice::storeBufferWithSRV( Microsoft::WRL::ComPtr<ID3D11Buffer> buffer, const D3D11_SHADER_RESOURCE_VIEW_DESC& srvDesc )
    {
        // 뷰를 만들지 못한 버퍼는 등록하지 않는다 — 뷰 없이 등록하면 셰이더가 그 버퍼를 조용히 0 으로 읽는다.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        if ( buffer == nullptr || isHrSuccess( _device->CreateShaderResourceView( buffer.Get(), &srvDesc, srv.GetAddressOf() ), "CreateShaderResourceView (buffer)" ) == false )
            return 0;
        return storeBuffer( std::move( buffer ), std::move( srv ) );
    }

    D3D11RHIDevice::TextureRecord* D3D11RHIDevice::resolveTexture( RHITextureHandle handle )
    {
        return _gpuTextures.get( handle );
    }

    const D3D11RHIDevice::TextureRecord* D3D11RHIDevice::resolveTexture( RHITextureHandle handle ) const
    {
        return _gpuTextures.get( handle );
    }

    RHITextureHandle D3D11RHIDevice::storeTexture( TextureRecord record, const RHITextureDesc& desc )
    {
        if ( record._texture == nullptr )
            return 0;
        const RHITextureHandle handle = _gpuTextures.insert( std::move( record ) );
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeTexture( handle ), RHIMemoryTracker::classifyTexture( desc ),
                                             RHIMemoryTracker::computeTextureLogicalBytes( desc ) );
        return handle;
    }

    D3D11RHIDevice::UploadStaging::UploadStaging()
        : _texture{}
        , _pDevice{ nullptr }
        , _bytes{ 0 }
    {
    }

    D3D11RHIDevice::UploadStaging::UploadStaging( D3D11RHIDevice& device, uint64 bytes )
        : _texture{}
        , _pDevice{ &device }
        , _bytes{ bytes }
    {
    }

    D3D11RHIDevice::UploadStaging::~UploadStaging()
    {
        release();
    }

    D3D11RHIDevice::UploadStaging::UploadStaging( UploadStaging&& other ) noexcept
        : _texture{ std::move( other._texture ) }
        , _pDevice{ std::exchange( other._pDevice, nullptr ) }
        , _bytes{ std::exchange( other._bytes, uint64{ 0 } ) }
    {
    }

    D3D11RHIDevice::UploadStaging& D3D11RHIDevice::UploadStaging::operator=( UploadStaging&& other ) noexcept
    {
        if ( this == &other )
            return *this;
        release();
        _texture = std::move( other._texture );
        _pDevice = std::exchange( other._pDevice, nullptr );
        _bytes   = std::exchange( other._bytes, uint64{ 0 } );
        return *this;
    }

    void D3D11RHIDevice::UploadStaging::attachTexture( Microsoft::WRL::ComPtr<ID3D11Texture2D> texture, uint64 sourceBytes )
    {
        if ( _pDevice == nullptr || texture == nullptr )
            ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
        SW_ASSERT( _texture == nullptr );
        _texture = std::move( texture );
        // 추적기 크기는 소스 바이트(논리 크기)다 — 드라이버가 행 간격을 늘린 실제 할당은 물을 API 가 없다.
        _pDevice->getMemoryTracker().recordAllocation( RHIMemoryKey::makeDeviceObject( _texture.Get() ), RHIMemoryKind::Staging, sourceBytes );
    }

    void D3D11RHIDevice::UploadStaging::release()
    {
        if ( _pDevice == nullptr )
            return;
        if ( _texture != nullptr )
        {
            // 주소가 다른 자원에 다시 쓰이기 전에 추적기에서 내린다.
            _pDevice->getMemoryTracker().recordFree( RHIMemoryKey::makeDeviceObject( _texture.Get() ) );
            _texture.Reset();
        }
        _pDevice->_uploadStagingBudget.release( _bytes );
        _pDevice = nullptr;
        _bytes   = 0;
    }

    D3D11RHIDevice::UploadStaging D3D11RHIDevice::allocateUploadStaging( uint64 bytes )
    {
        // 예산보다 큰 업로드 하나는 예산 전부를 잡는다 — 복사 중인 것이 없을 때만 들어가고, 그동안 다른 업로드는 Busy 다.
        // 한 장을 쪼개 여러 번 내면 앞 조각의 GPU 완료를 제출 안에서 기다려야 해서(계약: 제출은 기다리지 않는다) 혼자 들어가는 쪽을 골랐다.
        // 예산 전부를 잡아야 하는 업로드가 못 받으면 예산이 예약을 남겨 작은 업로드가 끊이지 않아 굶는 일을 막는다(RHIStagingBudget).
        // 두 번째 시도는 끝난 전송을 모은 뒤다. 모으는 일은 즉시 컨텍스트 잠금이 필요해 예산이 찼을 때만 한다.
        for ( uint32 attempt = 0; attempt < 2; ++attempt )
        {
            if ( attempt != 0 )
            {
                const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
                retireFinishedTransfers( context );
            }
            uint64 reservedBytes{ 0 };
            if ( _uploadStagingBudget.tryReserve( bytes, MonotonicClock::nowMicroseconds(), reservedBytes ) )
                return UploadStaging{ *this, reservedBytes };
        }
        return UploadStaging{};
    }

    RHITransferSubmit D3D11RHIDevice::submitStagedUpload( const utf8* pCaller, TextureRecord& record, UINT width, UINT height, UINT mipCount,
                                                          const D3D11_SUBRESOURCE_DATA* pArrInitialData, UINT firstDestinationSubresource, UINT destinationX,
                                                          UINT destinationY )
    {
        uint64 uploadBytes{ 0 };
        for ( UINT mip = 0; mip < mipCount; ++mip )
        {
            uploadBytes += pArrInitialData[mip].SysMemSlicePitch;
        }
        UploadStaging staging = allocateUploadStaging( uploadBytes );
        if ( staging.isReserved() == false )
            return RHITransferSubmit::busy(); // 앞선 업로드를 GPU 가 끝내면 예산이 돌아온다 — 호출자가 다음 프레임에 다시 낸다

        // 즉시 컨텍스트 잠금 밖이다. 장치 호출은 스레드 안전하고, 초기 데이터 복사(큰 텍스처는 수십 MiB)가 여기서 끝난다.
        // 스테이징은 대상과 같은 포맷이라 CopySubresourceRegion 이 그대로 받는다. 큐브 · 배열 플래그는 한 면만 담으므로 두지 않는다.
        D3D11_TEXTURE2D_DESC destinationDesc{};
        record._texture->GetDesc( &destinationDesc );
        D3D11_TEXTURE2D_DESC stagingDesc{};
        stagingDesc.Width              = width;
        stagingDesc.Height             = height;
        stagingDesc.MipLevels          = mipCount;
        stagingDesc.ArraySize          = 1;
        stagingDesc.Format             = destinationDesc.Format;
        stagingDesc.SampleDesc.Count   = 1;
        stagingDesc.SampleDesc.Quality = 0;
        stagingDesc.Usage              = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags          = 0;
        stagingDesc.CPUAccessFlags     = D3D11_CPU_ACCESS_WRITE;
        stagingDesc.MiscFlags          = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
        const HRESULT                           hr = _device->CreateTexture2D( &stagingDesc, pArrInitialData, stagingTexture.GetAddressOf() );
        if ( FAILED( hr ) )
        {
            SW_LOG_ERROR_ID( "D3D11.CreateTexture2DStagingTextureMips", "%#: CreateTexture2D of the %#×%# staging texture (%# mips, %# bytes) failed hr=0x%#", pCaller, width, height, mipCount, uploadBytes,
                             static_cast<uint32>( hr ) );
            return RHITransferSubmit::rejected();
        }
        staging.attachTexture( std::move( stagingTexture ), uploadBytes );

        // 잠금 안에서는 GPU 복사 명령과 끝을 알리는 쿼리만 낸다. 시리얼은 같은 잠금 안에서 매긴다(계약 5).
        const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
        Microsoft::WRL::ComPtr<ID3D11Query>     query   = acquireTransferQuery( context );
        if ( query == nullptr )
            return RHITransferSubmit::rejected();
        for ( UINT mip = 0; mip < mipCount; ++mip )
        {
            context->CopySubresourceRegion( record._texture.Get(), firstDestinationSubresource + mip, destinationX, destinationY, 0, staging.getTexture(), mip, nullptr );
        }
        const uint64 serial = endTransfer( context, std::move( query ), std::move( staging ) );
        record._uploadSerials.recordWrite( serial );
        return _transferSerials.makeAccepted( serial );
    }

    bool D3D11RHIDevice::queryGPUMemoryBudgetInternal( RHIMemoryBudget& outBudget )
    {
        return queryDXGIMemoryBudget( _memoryAdapter.Get(), outBudget );
    }

    bool D3D11RHIDevice::isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        if ( space == RHIBindlessIndexSpace::Texture )
            return index < _listRegisteredTexture.size() && _listRegisteredTexture[index] != 0;
        return index < _listRegisteredBindless.size() && _listRegisteredBindless[index] != 0;
    }

    // 길이 유지(긴 함수 정리 기준): 디바이스 · 스왑체인 · 상태 객체 생성의 순서가 곧 의미라 나누지 않는다.
    bool D3D11RHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
        // 이 백엔드는 Win32 전용이다 — 핸들이 Win32 표면의 것이 아니면 HWND 로 읽지 않고 오류로 멈춘다.
        void* pWindowHandle{ nullptr };
        if ( desc.tryGetWin32Handle( pWindowHandle ) == false )
        {
            SW_LOG_ERROR_ID( "D3D11.D3D11SwapChainNeeds", "The D3D11 swap chain needs a Win32 surface" );
            return false;
        }
        _pHWnd            = static_cast<HWND>( pWindowHandle );
        _backBufferFormat = desc._format;
        // D3D11 에는 자원의 할당 크기를 물을 API 가 없다. 추적기는 서술로 계산한 논리 크기다.
        getMemoryTracker().setSizeBasis( RHIMemorySizeBasis::Logical );
        // 큰 업로드는 스테이징 예산 안에서만 받는다. 0 이면 큰 업로드를 하나도 받지 못하므로 기본값으로 바꾸지 않고 초기화를 멈춘다.
        if ( desc._stagingRingBytes == 0 )
        {
            SW_LOG_ERROR_ID( "D3D11.UploadStagingBudgetGv", "The upload staging budget is 0 (gv_rhiStagingRingMegabytes) - Direct3D 11 does not start" );
            return false;
        }
        _uploadStagingBudget.initialize( desc._stagingRingBytes );
        _directUploadMaxBytes = desc._directUploadMaxBytes;

        // DX12 와 같게 FLIP_DISCARD 를 쓴다(DXGI 의 HWND 규칙). 한 HWND 에 플립 모델 스왑체인을 한 번 만든 뒤에는,
        // 같은 창에 DISCARD/blt 체인을 만들어 Present 해도 사용자가 보는 화면이 갱신되지 않을 수 있다
        // (멈춘 프레임).
        DXGI_SWAP_CHAIN_DESC swapChainDesc{};
        swapChainDesc.BufferCount                        = ( desc._bufferCount < 2 ) ? 2 : desc._bufferCount;
        swapChainDesc.BufferDesc.Width                   = desc._width;
        swapChainDesc.BufferDesc.Height                  = desc._height;
        swapChainDesc.BufferDesc.Format                  = toDXGIFormat( desc._format );
        swapChainDesc.BufferDesc.RefreshRate.Numerator   = kDefaultNumerator;
        swapChainDesc.BufferDesc.RefreshRate.Denominator = kDefaultDenominator;
        swapChainDesc.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.OutputWindow                       = _pHWnd;
        swapChainDesc.SampleDesc.Count                   = 1;
        swapChainDesc.SampleDesc.Quality                 = 0;
        swapChainDesc.Windowed                           = TRUE;
        swapChainDesc.SwapEffect                         = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        // VSync 를 끄려면 티어링 허용 스왑체인이어야 한다. 동기화 간격 0 만으로는 DWM 합성이
        // vblank 에 맞춰 넘겨 주므로 화면 주사율에 그대로 붙는다(RHIDxgiTearing.h).
        // VSync 는 실행 중에도 바뀌므로(`IRHIDevice::setVSync`) 지원하면 늘 티어링 허용으로 만든다 — 켜진 VSync 는 Present( 1, 0 ) 이다.
        const bool bAllowTearing = queryDXGIAllowTearing();
        swapChainDesc.Flags      = bAllowTearing ? static_cast<UINT>( DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING ) : 0u;

        UINT createDeviceFlags{ 0 };
    #if defined( SW_DEBUG )
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

        D3D_FEATURE_LEVEL           featureLevel;
        constexpr D3D_FEATURE_LEVEL arrFeatureLevel[2] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0,
        };

        Microsoft::WRL::ComPtr<IDXGISwapChain>      createdSwapChain;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> createdContext;
        // 소프트웨어 어댑터(WARP) — CI 러너와 같은 래스터라이저(gv_rhiSoftwareAdapter). 하드웨어는 고성능 순서의 첫 어댑터다(RHIDxgiAdapter.h — 기본 어댑터는 하이브리드 노트북의 내장 GPU).
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if ( desc._bSoftwareAdapter == false )
        {
            if ( SUCCEEDED( CreateDXGIFactory1( IID_PPV_ARGS( factory.GetAddressOf() ) ) ) )
                adapter = findDXGIPreferredAdapter( factory.Get(), desc._bPreferMinimumPower );
            if ( adapter == nullptr )
            {
                SW_LOG_ERROR_ID( "D3D11.NoHardwareAdapter", "No hardware DXGI adapter (IDXGIFactory6 high-performance enumeration found none)" );
                return false;
            }
            DXGI_ADAPTER_DESC1 adapterDesc{};
            if ( SUCCEEDED( adapter->GetDesc1( &adapterDesc ) ) )
                SW_LOG_INFO_ID( "D3D11.AdapterSelected", "Direct3D 11 adapter: %# (VRAM %# MB)", StringUtil::utf16ToUTF8( adapterDesc.Description ).c_str(),
                                static_cast<uint32>( adapterDesc.DedicatedVideoMemory / constant::memory::kBytesPerMegabyte ) );
        }
        markInitPhase( "adapter" );
        // 어댑터를 넘기면 드라이버 종류는 UNKNOWN 이어야 한다(D3D11CreateDevice 규칙).
        const D3D_DRIVER_TYPE driverType = desc._bSoftwareAdapter ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_UNKNOWN;
        HRESULT               hr         = D3D11CreateDeviceAndSwapChain( adapter.Get(),
                                                                          driverType,
                                                                          nullptr,
                                                                          createDeviceFlags,
                                                                          arrFeatureLevel,
                                                                          SW_COUNT_OF( arrFeatureLevel ),
                                                                          D3D11_SDK_VERSION,
                                                                          &swapChainDesc,
                                                                          createdSwapChain.GetAddressOf(),
                                                                          _device.GetAddressOf(),
                                                                          &featureLevel,
                                                                          createdContext.GetAddressOf() );

        if ( FAILED( hr ) )
        {
            SW_LOG_ERROR_ID( "D3D11.FailedCreateDirect3DDevice", "Failed to create Direct3D 11 Device and SwapChain! HRESULT: %#", hr );
            return false;
        }
        // 즉시 컨텍스트는 렌더 스레드와 전송 · 폴링 스레드가 함께 쓴다. 런타임 보호를 켜지 못하면 초기화를 실패시킨다(폴백 없음).
        if ( _immediateContext.initialize( std::move( createdContext ) ) == false )
            return false;
        markInitPhase( "device+swapchain" );

        // 메모리 질의는 디바이스가 실제로 쓰는 어댑터에 한다. DXGI 1.4 가 없으면 드라이버 값은 "모름" 이다.
        {
            Microsoft::WRL::ComPtr<IDXGIDevice>  dxgiDevice;
            Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            if ( SUCCEEDED( _device.As( &dxgiDevice ) ) && SUCCEEDED( dxgiDevice->GetAdapter( adapter.GetAddressOf() ) ) )
            {
                adapter.As( &_memoryAdapter );
                // 실제로 선 어댑터가 소프트웨어(WARP)인지 — 요청이 아니라 결과를 적는다.
                Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
                DXGI_ADAPTER_DESC1                    adapterDesc{};
                _bSoftwareAdapter = SUCCEEDED( adapter.As( &adapter1 ) ) && SUCCEEDED( adapter1->GetDesc1( &adapterDesc ) ) &&
                                    ( adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ) != 0;
            }
        }

        // GPU 완료를 확인하는 이벤트 쿼리(waitIdle 용 하나, 프레임마다 내는 링)를 지금 만든다. 없으면 `waitIdle` 과 `enqueueGPURelease` 가 "GPU 가 끝난 뒤" 를
        // 지킬 수 없으므로 폴백 없이 초기화를 실패시킨다.
        {
            D3D11_QUERY_DESC eventDesc{};
            eventDesc.Query              = D3D11_QUERY_EVENT;
            bool bCompletionQueriesReady = isHrSuccess( _device->CreateQuery( &eventDesc, _idleQuery.GetAddressOf() ), "CreateQuery" );
            for ( Microsoft::WRL::ComPtr<ID3D11Query>& frameDoneQuery : _arrFrameDoneQuery )
            {
                bCompletionQueriesReady = bCompletionQueriesReady && isHrSuccess( _device->CreateQuery( &eventDesc, frameDoneQuery.GetAddressOf() ), "CreateQuery" );
            }
            if ( bCompletionQueriesReady == false )
            {
                SW_LOG_ERROR_ID( "D3D11.FailedCreateD3D11Event", "Failed to create the D3D11 event queries that confirm GPU completion" );
                return false;
            }
        }

        // D3D11 은 디바이스와 스왑체인이 한 호출에서 함께 나온다. 만들어진 것을 넘겨 소유시킨다.
        _swapChain.attach( createdSwapChain.Get(), _pHWnd, desc._width, desc._height, swapChainDesc.Flags );

        // Deferred Context 기반 병렬 기록이 실익이 있는지는 드라이버가 커맨드 리스트를 네이티브로
        // 지원하는지에 달렸다. 지원하지 않으면 D3D11 런타임이 소프트웨어로 에뮬레이션하므로 병렬화
        // 이득보다 오버헤드가 커진다. 그래서 이 값으로 병렬 기록 능력을 런타임에 결정한다.
        {
            D3D11_FEATURE_DATA_THREADING threadingCaps{};
            if ( SUCCEEDED( _device->CheckFeatureSupport( D3D11_FEATURE_THREADING, &threadingCaps, sizeof( threadingCaps ) ) ) )
            {
                _bDriverCommandLists = threadingCaps.DriverCommandLists != FALSE ? SW_TRUE : SW_FALSE;
                SW_LOG_INFO_ID( "D3D11.ThreadingCapsDriverConcurrentCreatesDriverCommandLists", "Threading caps: DriverConcurrentCreates=%#, DriverCommandLists=%#",
                                static_cast<uint32>( threadingCaps.DriverConcurrentCreates ),
                                static_cast<uint32>( threadingCaps.DriverCommandLists ) );
            }
        }

        _swapChain.acquireNextImage( _device.Get() );

        {
            // 깊이 상태는 PSO 마다 만든다(createPipelineState). 장치 전역 깊이 상태는 두지 않는다 — PSO 없는 드로우를 대신 그리는 길이 된다.
            // 정적 샘플러 세트 s9..s15. DX12 루트 시그니처 정적 샘플러(D3D12RHIDevice.cpp)와 같은 표다. 비교 샘플러(7)는
            // 에뮬 경로가 깊이를 직접 비교하므로 없다.
            struct StaticSamplerSpec
            {
                D3D11_FILTER               _filter;
                D3D11_TEXTURE_ADDRESS_MODE _address;
                uint32                     _anisotropy;
            };
            const StaticSamplerSpec arrSpec[shaderslot::kStaticSamplerArrayCount] = {
                {D3D11_FILTER_MIN_MAG_MIP_LINEAR,   D3D11_TEXTURE_ADDRESS_WRAP, 1}, // LINEAR_WRAP
                {D3D11_FILTER_MIN_MAG_MIP_LINEAR,  D3D11_TEXTURE_ADDRESS_CLAMP, 1}, // LINEAR_CLAMP
                { D3D11_FILTER_MIN_MAG_MIP_POINT,   D3D11_TEXTURE_ADDRESS_WRAP, 1}, // POINT_WRAP
                { D3D11_FILTER_MIN_MAG_MIP_POINT,  D3D11_TEXTURE_ADDRESS_CLAMP, 1}, // POINT_CLAMP
                {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_MIRROR, 1}, // LINEAR_MIRROR
                {       D3D11_FILTER_ANISOTROPIC,   D3D11_TEXTURE_ADDRESS_WRAP, 8}, // ANISO_WRAP
                { D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_BORDER, 1}, // POINT_BORDER
            };
            for ( uint32 samplerIndex = 0; samplerIndex < shaderslot::kStaticSamplerArrayCount; ++samplerIndex )
            {
                D3D11_SAMPLER_DESC staticDesc{};
                staticDesc.Filter         = arrSpec[samplerIndex]._filter;
                staticDesc.AddressU       = arrSpec[samplerIndex]._address;
                staticDesc.AddressV       = arrSpec[samplerIndex]._address;
                staticDesc.AddressW       = arrSpec[samplerIndex]._address;
                staticDesc.MaxAnisotropy  = arrSpec[samplerIndex]._anisotropy;
                staticDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
                staticDesc.MinLOD         = 0.0f;
                staticDesc.MaxLOD         = D3D11_FLOAT32_MAX;
                // 정적 샘플러는 셰이더가 번호로 고르는 필수 세트다. 빠지면 그 샘플러로 읽는 텍스처가 조용히 검정이 되므로 초기화를 실패시킨다.
                if ( isHrSuccess( _device->CreateSamplerState( &staticDesc, _arrStaticSampler[samplerIndex].GetAddressOf() ), "CreateSamplerState" ) == false )
                {
                    SW_LOG_ERROR_ID( "D3D11.CreateSamplerStateFailedStaticSampler", "CreateSamplerState failed for static sampler %#.", samplerIndex );
                    return false;
                }
            }
            const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
            bindStaticSamplers( context.get() );
        }

        SW_LOG_INFO_ID( "D3D11.Direct3DRHIBackendDevice", "Direct3D 11 RHI Backend Device Initialized Successfully (FLIP_DISCARD)." );
        // 프레임 스트림은 렌더 스레드만 쓰고 호출마다 런타임 보호를 받는다. 다른 스레드의 묶음(`lock()`)은 그 호출 사이에 끼지 않는다.
        _frameStreamContext = sw::make_unique<D3D11RHICommandContext>( this, _immediateContext.getPerCallProtected() );
        return true;
    }

    void D3D11RHIDevice::detachCommandRecordingInternal()
    {
        _frameStreamContext.reset();
        RHILiveCommandListUtil::detachAll( _liveCmdListMutex, _listLiveCmd );
    }

    void D3D11RHIDevice::shutdownInternal()
    {
        _listFrameSwapChain.clear();
        _listWindowSwapChain.clear();
        _swapChain.shutdown();
        _gpuTextures.clear();
        _gpuBuffers.clear();
        _recordingState._activeGraphicsPSO = 0;
        _listRegisteredBindless.clear();
        _freeBindless.clear();
        _listRegisteredTexture.clear();
        _freeTexture.clear();
        _listRegisteredUAV.clear();
        _listUAVSourceBuffer.clear();
        _freeUAV.clear();
        _recordingState = D3D11RecordingState{};
        _idleQuery.Reset();
        for ( Microsoft::WRL::ComPtr<ID3D11Query>& frameDoneQuery : _arrFrameDoneQuery )
        {
            frameDoneQuery.Reset();
        }
        _frameSerial          = 0;
        _completedFrameSerial = 0;
        // 낸 전송의 쿼리와 아직 가져가지 않은 읽기는 장치와 함께 놓는다(COM 이 GPU 가 쓰는 동안 자원을 붙들어 준다).
        _transferQueue.retireAll( []( TransferProbe& probe )
        { probe._query.Reset(); } );
        _listFreeTransferQuery.clear();
        _immediateContext.shutdown();
        _memoryAdapter.Reset();
        _device.Reset();
    }

    void D3D11RHIDevice::resizeInternal( uint32 width, uint32 height )
    {
        if ( _swapChain.isValid() == false || ( width == 0 && height == 0 ) )
            return;
        resizeSwapChainBuffers( _swapChain, width, height );
    }

    IRHISwapChain* D3D11RHIDevice::createSwapChainInternal( const RHISwapChainDesc& desc )
    {
        unique_ptr<D3D11RHISwapChain> pSwapChain = make_unique<D3D11RHISwapChain>();
        if ( pSwapChain->createLike( _device.Get(), _swapChain, static_cast<HWND>( desc._pWindowHandle ), desc._width, desc._height ) == false )
            return nullptr;
        _listWindowSwapChain.push_back( std::move( pSwapChain ) );
        return _listWindowSwapChain.back().get();
    }

    void D3D11RHIDevice::resizeSwapChainInternal( IRHISwapChain& swapChain, uint32 width, uint32 height )
    {
        resizeSwapChainBuffers( static_cast<D3D11RHISwapChain&>( swapChain ), width, height ); // 이 디바이스가 만든 것만 들어온다(`createSwapChain`)
    }

    void D3D11RHIDevice::destroySwapChainInternal( IRHISwapChain& swapChain )
    {
        // 컨텍스트에 걸린 백버퍼 바인딩을 먼저 푼다 — 남아 있으면 스왑체인이 그 참조 때문에 창보다 오래 산다.
        if ( _immediateContext.isValid() )
        {
            const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
            context->ClearState();
        }
        (void)VectorUtil::removeIf( _listWindowSwapChain, [&swapChain]( const unique_ptr<D3D11RHISwapChain>& pEntry )
        { return pEntry.get() == &swapChain; } );
    }

    D3D11RHISwapChain& D3D11RHIDevice::useSwapChain( IRHISwapChain* pSwapChain )
    {
        if ( pSwapChain == nullptr || pSwapChain == &_swapChain )
            return _swapChain;
        D3D11RHISwapChain& swapChain = static_cast<D3D11RHISwapChain&>( *pSwapChain ); // 이 디바이스가 만든 것만 들어온다(`createSwapChain`)
        if ( std::find( _listFrameSwapChain.begin(), _listFrameSwapChain.end(), &swapChain ) == _listFrameSwapChain.end() )
        {
            swapChain.acquireNextImage( _device.Get() );
            _listFrameSwapChain.push_back( &swapChain );
        }
        return swapChain;
    }

    void D3D11RHIDevice::resizeSwapChainBuffers( D3D11RHISwapChain& swapChain, uint32 width, uint32 height )
    {

        // 백버퍼를 가리키는 참조가 하나라도 남아 있으면 ResizeBuffers 가 거부된다
        // (DXGI_ERROR_INVALID_CALL). 참조는 세 군데에 있다: 백버퍼 RTV, Immediate Context 의
        // 바인딩, 그리고 **기록이 끝난 커맨드 리스트**다. 마지막 것을 빠뜨리면 창 크기 변경이
        // 매번 조용히 실패한다.
        swapChain.releaseBackBufferRTV();
        {
            ScopedLock<mutex> lock{ _liveCmdListMutex };
            for ( D3D11RHICommandList* pLiveList : _listLiveCmd )
            {
                pLiveList->releaseRecordedState();
            }
        }
        if ( _immediateContext.isValid() )
        {
            const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
            context->ClearState();
            context->Flush();
        }
        if ( swapChain.resize( width, height ) == false )
            return;
        swapChain.acquireNextImage( _device.Get() );
    }

    bool D3D11RHIDevice::bindGraphicsContext()
    {
        // 즉시 컨텍스트에는 MakeCurrent 가 없다. 묶을 것이 없으므로 컨텍스트가 있는지만 답한다.
        return _immediateContext.isValid();
    }

    void D3D11RHIDevice::markDeviceLost( const utf8* pCause, HRESULT hr )
    {
        if ( _transferSerials.markLost() )
        {
            SW_LOG_ERROR_ID( "D3D11.D3D11DeviceLostHr", "The D3D11 device was lost (%#, hr=0x%#) - later transfer submits are rejected and GPU waits stop", pCause, static_cast<uint32>( hr ) );
            GPUBreadcrumbs::reportDeviceLost( "Direct3D 11", nullptr, 0 ); // GPU 쪽 표지가 없다 - CPU 가 마지막으로 기록한 패스만 싣는다
        }
    }

    bool D3D11RHIDevice::isDeviceRemoved()
    {
        if ( _device == nullptr )
            return false;
        const HRESULT removedReason = _device->GetDeviceRemovedReason();
        if ( SUCCEEDED( removedReason ) )
            return false;
        markDeviceLost( "GetDeviceRemovedReason", removedReason );
        return true;
    }

    bool D3D11RHIDevice::isDeviceLost()
    {
        return _transferSerials.isLost() || isDeviceRemoved();
    }

    bool D3D11RHIDevice::debugRemoveDevice()
    {
        if ( _device == nullptr )
            return false;
        SW_LOG_WARNING_ID( "D3D11.MarkingD3D11DeviceLost", "Marking the D3D11 device lost on request (rhi.removeDevice) - D3D11 cannot remove a device, so this is the state after the first "
                                                           "DXGI_ERROR_DEVICE_REMOVED: later transfer submits are rejected" );
        _transferSerials.markLost(); // 이미 잃은 장치여도 같은 상태다
        return true;
    }

    void D3D11RHIDevice::reportQueryFailure( HRESULT hr, const utf8* pStage )
    {
        if ( isDeviceRemoved() == false )
            markDeviceLost( pStage, hr );
    }

    bool D3D11RHIDevice::isEventQueryDone( const D3D11ImmediateContext::ScopedLock& context, ID3D11Query* pQuery, UINT getDataFlags )
    {
        BOOL          bDone = FALSE;
        const HRESULT hr    = context->GetData( pQuery, &bDone, sizeof( bDone ), getDataFlags );
        if ( FAILED( hr ) )
        {
            // 쿼리를 읽지 못한다 = 장치를 잃었다. 표시하고 "끝남" 으로 돌려 대기 루프가 끝나게 한다(GPU 가 더 진행하지 않는다).
            reportQueryFailure( hr, "event query GetData" );
            return true;
        }
        return hr != S_FALSE && bDone != FALSE;
    }

    bool D3D11RHIDevice::waitForEventQuery( const D3D11ImmediateContext::ScopedLock& context, ID3D11Query* pQuery, uint32 timeoutMilliseconds )
    {
        const Deadline deadline = Deadline::afterMilliseconds( timeoutMilliseconds );
        while ( isEventQueryDone( context, pQuery, 0 ) == false )
        {
            if ( deadline.isExpired() )
                return false;
            ThreadUtil::yield();
        }
        return true;
    }

    uint32 D3D11RHIDevice::getTimestampSlotCount() const
    {
        const bool bTimestampsOn = _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _bTimestampReady.load( std::memory_order_acquire ) != SW_FALSE;
        return bTimestampsOn ? constant::render::kMaxGPUTimestampSlot : 0u;
    }

    bool D3D11RHIDevice::readTimestamps( RHITimestampFrame& outFrame )
    {
        outFrame = _timestampFrame;
        return outFrame._listMicro.empty() == false;
    }

    bool D3D11RHIDevice::readGPUClockNanos( int64& outGPUNanos )
    {
        outGPUNanos = 0;
        // 주파수는 프레임 disjoint 가 준다 — 아직 한 프레임도 읽지 않았으면 틱을 나노초로 바꿀 근거가 없다.
        if ( _device == nullptr || _immediateContext.isValid() == false )
            return false;

        // 주파수는 렌더 스레드의 `collectTimestampsForSlot` 이 같은 가드 안에서 쓴다.
        const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
        if ( _lastTimestampFrequency == 0 )
            return false;
        if ( _clockQuery == nullptr )
        {
            D3D11_QUERY_DESC stampDesc{};
            stampDesc.Query = D3D11_QUERY_TIMESTAMP;
            if ( isHrSuccess( _device->CreateQuery( &stampDesc, &_clockQuery ), "CreateQuery" ) == false )
                return false;
        }

        // D3D11 에는 "지금 GPU 시계" 를 묻는 API 가 없다. 타임스탬프 하나를 내고 GPU 가 그것을 지날 때까지 묻는다 — 돌아오는 순간의 값이 곧
        // 지금이다(관찰 지연만큼 이르다). 큐에 쌓인 프레임만큼 기다리므로 컨텍스트를 열 때 한 번만 부른다(`isGPUClockReadCheap` false).
        context->End( _clockQuery.Get() );
        context->Flush();
        const Deadline deadline = Deadline::afterMilliseconds( D3D11RHIDeviceInternal::kClockReadTimeoutMilliseconds );
        uint64         tick{ 0 };
        for ( HRESULT hr = context->GetData( _clockQuery.Get(), &tick, sizeof( tick ), 0 ); hr != S_OK; hr = context->GetData( _clockQuery.Get(), &tick, sizeof( tick ), 0 ) )
        {
            if ( FAILED( hr ) )
            {
                reportQueryFailure( hr, "readGPUClockNanos GetData" ); // 시한까지 돌지 않고 장치 손실로 표시한다
                return false;
            }
            if ( deadline.isExpired() )
                return false;
            ThreadUtil::yield();
        }
        outGPUNanos = RHITimestampFrame::convertTickToNanos( tick, constant::time::kNanosecondsPerSecond / static_cast<float64>( _lastTimestampFrequency ) );
        return true;
    }

    void D3D11RHIDevice::writeTimestampSlot( ID3D11DeviceContext* pContext, uint32 slotIndex )
    {
        const bool bCannotWriteTimestamp = pContext == nullptr || _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE ||
                                           _bTimestampReady.load( std::memory_order_acquire ) == SW_FALSE || slotIndex >= constant::render::kMaxGPUTimestampSlot;
        if ( bCannotWriteTimestamp )
            return;

        // 이 프레임 묶음의 칸 하나. Deferred Context 마다 다른 칸이라 같은 쿼리 객체가 겹치지 않는다.
        ID3D11Query* pQuery = _arrTimestampFrame[_timestampFrameIndex]._arrQuery[slotIndex].Get();
        if ( pQuery == nullptr )
            return;
        // 타임스탬프 쿼리는 Begin 이 없다. End 하나가 "지금 GPU 시각" 이다.
        pContext->End( pQuery );
        _timestampWrittenMask.set( slotIndex );
    }

    void D3D11RHIDevice::ensureTimestampResources()
    {
        const bool bTimestampResourcesNotNeeded = _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE ||
                                                  _bTimestampReady.load( std::memory_order_relaxed ) != SW_FALSE || _device == nullptr;
        if ( bTimestampResourcesNotNeeded )
            return;

        D3D11_QUERY_DESC disjointDesc{};
        disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        D3D11_QUERY_DESC stampDesc{};
        stampDesc.Query = D3D11_QUERY_TIMESTAMP;

        for ( uint32 frameIndex = 0; frameIndex < constant::render::kMaxFrameCountInFlight; ++frameIndex )
        {
            D3D11TimestampFrame& frame = _arrTimestampFrame[frameIndex];
            if ( isHrSuccess( _device->CreateQuery( &disjointDesc, &frame._disjoint ), "CreateQuery" ) == false )
                return;
            for ( uint32 slotIndex = 0; slotIndex < constant::render::kMaxGPUTimestampSlot; ++slotIndex )
            {
                if ( isHrSuccess( _device->CreateQuery( &stampDesc, &frame._arrQuery[slotIndex] ), "CreateQuery" ) == false )
                    return;
            }
        }
        _bTimestampReady.store( SW_TRUE, std::memory_order_release ); // 위에서 만든 쿼리를 기록 워커가 본다
    }

    void D3D11RHIDevice::collectTimestampsForSlot()
    {
        _timestampFrame._listMicro.clear();
        D3D11TimestampFrame& frame                   = _arrTimestampFrame[_timestampFrameIndex];
        const bool           bNoTimestampFrameToRead = getTimestampSlotCount() == 0 || frame._bPending == SW_FALSE || frame._writtenMask.any() == false;
        if ( bNoTimestampFrameToRead )
            return;

        const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
        // **GetData 는 즉시 컨텍스트 전용이다.** Deferred Context 에서 End 한 쿼리도 여기서만 읽는다.
        // **이 묶음은 곧 다시 End 하므로 지금 읽지 못하면 영영 잃는다.** 한 번만 묻고(DONOTFLUSH) 물러나면 GPU 가 몇 프레임 뒤처진 무거운
        // 그래프(디퍼드 15 패스)에서 프레임 뒤쪽 패스의 칸이 늘 준비 전이라 GPU 표에서 Tonemap · Present · Canvas 가 빠졌다. 그래서 묶음마다
        // 상한(kClockReadTimeoutMilliseconds)까지 기다려 읽는다 — DX12 · Vulkan 이 묶음을 다시 쓰기 전에 펜스를 기다리는 것과 같은 자리다.
        // 프로파일러가 켜졌을 때만 오는 경로다(`_bTimestampEnabled`).
        const Deadline deadline     = Deadline::afterMilliseconds( D3D11RHIDeviceInternal::kClockReadTimeoutMilliseconds );
        auto           waitForQuery = [this, &context, &deadline]( ID3D11Query* pQuery, void* pData, uint32 byteSize ) -> bool
        {
            for ( HRESULT hr = context->GetData( pQuery, pData, byteSize, 0 ); hr != S_OK; hr = context->GetData( pQuery, pData, byteSize, 0 ) )
            {
                if ( FAILED( hr ) )
                {
                    reportQueryFailure( hr, "timestamp GetData" ); // 시한까지 돌지 않고 장치 손실로 표시한다
                    return false;
                }
                if ( deadline.isExpired() )
                    return false;
                ThreadUtil::yield();
            }
            return true;
        };
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
        if ( waitForQuery( frame._disjoint.Get(), &disjointData, sizeof( disjointData ) ) == false )
            return;

        frame._bPending = SW_FALSE;
        // 이 구간에서 GPU 클럭이 흔들렸다. 틱을 초로 바꿀 근거가 없으니 프레임을 통째로 버린다.
        if ( disjointData.Disjoint != FALSE || disjointData.Frequency == 0 )
            return;

        uint64               arrTick[constant::render::kMaxGPUTimestampSlot]{};
        GPUTimestampSlotMask readyMask{};
        for ( uint32 slotIndex = 0; slotIndex < constant::render::kMaxGPUTimestampSlot; ++slotIndex )
        {
            if ( frame._writtenMask.test( slotIndex ) == false )
                continue;

            uint64 tick{ 0 };
            if ( waitForQuery( frame._arrQuery[slotIndex].Get(), &tick, sizeof( tick ) ) == false )
                continue;
            arrTick[slotIndex] = tick;
            readyMask.set( slotIndex );
        }
        _lastTimestampFrequency = disjointData.Frequency;
        RHITimestampFrame::resolve( arrTick, readyMask, constant::time::kNanosecondsPerSecond / static_cast<float64>( disjointData.Frequency ), _timestampFrame );
    }

    void D3D11RHIDevice::beginFrameInternal()
    {
        if ( _immediateContext.isValid() == false || _swapChain.isValid() == false )
            return;

        // 이 묶음은 곧 다시 쓴다. 덮어쓰기 전에 지난 바퀴의 결과를 한 번만 묻는다.
        ensureTimestampResources();
        collectTimestampsForSlot();
        const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
        if ( getTimestampSlotCount() != 0 )
        {
            _arrTimestampFrame[_timestampFrameIndex]._writtenMask = GPUTimestampSlotMask{};
            _timestampWrittenMask.clear();

            // disjoint 는 프레임 전체를 감싼다. 그 사이에 즉시 컨텍스트가 커맨드 리스트를 실행한다.
            context->Begin( _arrTimestampFrame[_timestampFrameIndex]._disjoint.Get() );
            _bTimestampFrameOpen = SW_TRUE;
        }

        // FLIP_DISCARD 는 백버퍼를 돌려 쓴다. Present 가 보여 줄 그 버퍼에 그리도록 매 프레임 다시 잡는다.
        _swapChain.acquireNextImage( _device.Get() );
        // 정적 샘플러 세트는 컨텍스트 상태라 ClearState 로 사라질 수 있다. 프레임마다 다시 건다(값싸다).
        bindStaticSamplers( context.get() );
        if ( _swapChain.getBackBufferRTV() == nullptr )
            return;

        // beginFrame 은 프레임 수명주기만 맡고 백버퍼를 바인딩하거나 클리어하지 않는다. 백버퍼를 타깃으로
        // 삼고 클리어하는 것은 RenderThread 가 여는 명시적 beginRenderPass(핸들 0) 뿐이다.
        // RTV 재취득은 FLIP_DISCARD 때문에 수명주기에 속한다.

        constexpr float32 kDefaultViewportX        = 0.0f;
        constexpr float32 kDefaultViewportY        = 0.0f;
        constexpr float32 kDefaultViewportMinDepth = 0.0f;
        constexpr float32 kDefaultViewportMaxDepth = 1.0f;

        D3D11_VIEWPORT viewport;
        viewport.Width    = static_cast<float32>( _swapChain.getWidth() );
        viewport.Height   = static_cast<float32>( _swapChain.getHeight() );
        viewport.MinDepth = kDefaultViewportMinDepth;
        viewport.MaxDepth = kDefaultViewportMaxDepth;
        viewport.TopLeftX = kDefaultViewportX;
        viewport.TopLeftY = kDefaultViewportY;
        // 래스터라이저 상태가 가위를 늘 켜 두므로 뷰포트와 같은 가위를 함께 건다(IRHICommandList::setViewport 규약).
        const D3D11_RECT scissor{ 0, 0, static_cast<LONG>( _swapChain.getWidth() ), static_cast<LONG>( _swapChain.getHeight() ) };
        context->RSSetViewports( 1, &viewport );
        context->RSSetScissorRects( 1, &scissor );
    }

    void D3D11RHIDevice::endFrameInternal( bool vsync, bool bPresent )
    {
    #if defined( SW_DEBUG )
        flushDebugMessages( "endFrame" );
    #endif
        // beginFrame 이 열었으면 반드시 닫는다. 짝이 안 맞으면 런타임이 경고를 뿜고 값이 무의미해진다.
        if ( _bTimestampFrameOpen != SW_FALSE )
        {
            D3D11TimestampFrame& frame = _arrTimestampFrame[_timestampFrameIndex];
            {
                const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
                context->End( frame._disjoint.Get() );
            }
            frame._writtenMask   = _timestampWrittenMask.load();
            frame._bPending      = frame._writtenMask.any() ? SW_TRUE : SW_FALSE;
            _bTimestampFrameOpen = SW_FALSE;
            _timestampFrameIndex = ( _timestampFrameIndex + 1 ) % constant::render::kMaxFrameCountInFlight;
        }

        if ( _swapChain.isValid() == false )
            return;

        _swapChain.releaseBackBufferRTV();
        // 이 프레임에 그린 다른 창을 메인 창과 같은 규칙으로 표시한다(무인 실행은 내보내지 않는다).
        for ( D3D11RHISwapChain* pWindow : _listFrameSwapChain )
        {
            pWindow->releaseBackBufferRTV();
            if ( bPresent )
            {
                const HRESULT windowHr = pWindow->present( vsync );
                if ( FAILED( windowHr ) )
                    SW_LOG_ERROR_ONCE_ID( "D3D11.WindowPresentFailedHr", "Present of a window swap chain failed hr=%#", static_cast<uint32>( windowHr ) );
            }
        }
        _listFrameSwapChain.clear();

        if ( bPresent )
        {
            const HRESULT hr = _swapChain.present( vsync );
            if ( FAILED( hr ) )
            {
                // 장치 제거는 자동 복구가 없어 이후 매 프레임 여기로 들어온다 - 첫 실패만 남긴다(DX12 의 _bDeviceRemovedLogged 와 같은 뜻). 손실 표시는 markDeviceLost 가 한 번 남긴다.
                SW_LOG_ERROR_ONCE_ID( "D3D11.PresentFailedHr", "Present failed hr=%#", static_cast<uint32>( hr ) );
                // 장치 제거 · 리셋으로 실패했으면 전송 손실로 표시한다. 디버그 전용인 flushDebugMessages 에 맡기지 않는다.
                if ( hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET )
                    markDeviceLost( "Present", hr );
                else
                    (void)isDeviceRemoved(); // 다른 실패 코드여도 제거 이유가 실패면 표시한다(DEVICE_HUNG 등)
            }
        }

        retireFrameOnGPU();
        _releaseQueue.tickFrame();
    }

    void D3D11RHIDevice::waitIdleInternal()
    {
        if ( _immediateContext.isValid() )
        {
            const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
            // Flush 는 명령을 GPU 로 보낼 뿐 끝나기를 기다리지 않는다. 이벤트 쿼리를 내고 GPU 가 거기를 지날 때까지 묻는다.
            // 기다리지 않으면 아래 해제 큐 콜백(비동기 되읽기의 Map 등)이 GPU 가 끝나기 전에 돌아 "끝난 뒤" 라는 약속이 깨진다.
            // 기다리는 동안 다른 스레드의 즉시 컨텍스트 호출도 멈춘다 — 쿼리 뒤에 새 전송이 끼면 아래 `retireAll` 이 끝나지 않은 전송을 끝났다고 본다.
            context->End( _idleQuery.Get() );
            context->Flush();
            if ( waitForEventQuery( context, _idleQuery.Get(), D3D11RHIDeviceInternal::kIdleWaitTimeoutMilliseconds ) == false )
                SW_LOG_ERROR_ID( "D3D11.WaitIdleGPUDidNot", "waitIdle: the GPU did not finish within %# ms", D3D11RHIDeviceInternal::kIdleWaitTimeoutMilliseconds );
            _completedFrameSerial = _frameSerial; // GPU 가 쉰다 — 낸 프레임이 모두 끝났다
            // 낸 전송도 모두 끝났다. 쿼리를 풀로 돌려주고 워터마크를 올린다(업로드 스테이징은 항목과 함께 놓인다).
            _transferQueue.retireAll( [this]( TransferProbe& probe )
            { _listFreeTransferQuery.push_back( std::move( probe._query ) ); } );
        }
        _releaseQueue.flushAll();
    }

    void D3D11RHIDevice::retireFrameOnGPU()
    {
        if ( _immediateContext.isValid() == false )
            return;
        uint64 completedSerial{ 0 };
        {
            const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
            const uint64                            serial  = _frameSerial + 1;
            const uint32                            slot    = static_cast<uint32>( ( serial - 1 ) % constant::render::kMaxFrameCountInFlight );
            // 링이 한 바퀴 돌았다. 이 칸은 프레임 serial - kMaxFrameCountInFlight 의 것이니, 그 프레임이 끝나야 쿼리를 다시 쓴다.
            const bool bRingFull = serial - _completedFrameSerial > constant::render::kMaxFrameCountInFlight;
            if ( bRingFull )
            {
                if ( waitForEventQuery( context, _arrFrameDoneQuery[slot].Get(), D3D11RHIDeviceInternal::kIdleWaitTimeoutMilliseconds ) == false )
                    SW_LOG_ERROR_ID( "D3D11.EndFrameGPUDidNot", "endFrame: the GPU did not finish frame %# within %# ms", serial - constant::render::kMaxFrameCountInFlight,
                                     D3D11RHIDeviceInternal::kIdleWaitTimeoutMilliseconds );
                _completedFrameSerial = serial - constant::render::kMaxFrameCountInFlight;
            }
            context->End( _arrFrameDoneQuery[slot].Get() );
            _frameSerial = serial;

            // 이미 끝난 프레임을 오래된 순서로 모은다. Present 가 명령을 보냈으므로 여기서는 Flush 하지 않고 묻기만 한다.
            while ( _completedFrameSerial < _frameSerial )
            {
                const uint64       next      = _completedFrameSerial + 1;
                ID3D11Query* const pNextDone = _arrFrameDoneQuery[( next - 1 ) % constant::render::kMaxFrameCountInFlight].Get();
                if ( isEventQueryDone( context, pNextDone, D3D11_ASYNC_GETDATA_DONOTFLUSH ) == false )
                    break;
                _completedFrameSerial = next;
            }
            completedSerial = _completedFrameSerial;
        }
        // 콜백은 즉시 컨텍스트 잠금 밖에서 부른다 — 비동기 되읽기의 콜백이 같은 잠금을 잡고 Map 한다.
        _releaseQueue.tickCompleted( completedSerial );
    }

    Microsoft::WRL::ComPtr<ID3D11Query> D3D11RHIDevice::acquireTransferQuery( const D3D11ImmediateContext::ScopedLock& context )
    {
        (void)context; // 풀이 이 가드로 지켜진다는 증명으로만 받는다
        if ( _listFreeTransferQuery.empty() == false )
        {
            Microsoft::WRL::ComPtr<ID3D11Query> query = std::move( _listFreeTransferQuery.back() );
            _listFreeTransferQuery.pop_back();
            return query;
        }
        D3D11_QUERY_DESC eventDesc{};
        eventDesc.Query = D3D11_QUERY_EVENT;
        Microsoft::WRL::ComPtr<ID3D11Query> query;
        if ( isHrSuccess( _device->CreateQuery( &eventDesc, query.GetAddressOf() ), "CreateQuery" ) == false )
        {
            SW_LOG_ERROR_ID( "D3D11.AcquireTransferQueryFailedCreateEvent", "acquireTransferQuery: failed to create the event query that marks a transfer's end" );
            return nullptr;
        }
        return query;
    }

    uint64 D3D11RHIDevice::endTransfer( const D3D11ImmediateContext::ScopedLock& context, Microsoft::WRL::ComPtr<ID3D11Query> query, UploadStaging staging )
    {
        context->End( query.Get() );
        return _transferQueue.enqueue( TransferProbe{ std::move( query ), std::move( staging ) } );
    }

    void D3D11RHIDevice::retireFinishedTransfers( const D3D11ImmediateContext::ScopedLock& context )
    {
        // 플래그 0 으로 묻는다 — 명령을 GPU 로 내보내므로 프레임을 돌리지 않아도 되풀이 폴링이 끝에 이른다.
        // 끝난 항목의 업로드 스테이징은 큐에서 빠질 때 놓이고 예산이 돌아온다.
        _transferQueue.retireDone(
            [this, &context]( TransferProbe& probe )
        {
            BOOL          bDone = FALSE;
            const HRESULT hr    = context->GetData( probe._query.Get(), &bDone, sizeof( bDone ), 0 );
            if ( FAILED( hr ) )
            {
                reportQueryFailure( hr, "pollTransfer GetData on the transfer query" );
                return false;
            }
            return hr != S_FALSE && bDone != FALSE;
        },
            [this]( TransferProbe& probe )
        { _listFreeTransferQuery.push_back( std::move( probe._query ) ); } );
    }

    void D3D11RHIDevice::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate )
    {
        _releaseQueue.enqueueGPURelease( releaseDelegate, _frameSerial + 1 );
    }

    unique_ptr<IRHICommandList> D3D11RHIDevice::createCommandList()
    {
        unique_ptr<D3D11RHICommandList> list = make_unique<D3D11RHICommandList>( this );
        if ( list->isValid() == false )
        {
            SW_LOG_ERROR_ID( "D3D11.D3D11RHIDeviceCreateCommandListFailedCreate", "D3D11RHIDevice::createCommandList: failed to create the native Deferred Context." );
            return nullptr;
        }
        return list;
    }

    void D3D11RHIDevice::removeBufferInRecordingStates( RHIBufferHandle buffer )
    {
        _recordingState._geometry.remove( buffer );

        ScopedLock<mutex> lock{ _liveCmdListMutex };
        for ( D3D11RHICommandList* pList : _listLiveCmd )
        {
            if ( pList == nullptr )
                continue;
            pList->getRecordingState()._geometry.remove( buffer );
        }
    }

    void D3D11RHIDevice::removePipelineStateInRecordingStates( RHIPipelineStateHandle pso )
    {
        if ( _recordingState._activeGraphicsPSO == pso )
            _recordingState._activeGraphicsPSO = 0;

        ScopedLock<mutex> lock{ _liveCmdListMutex };
        for ( D3D11RHICommandList* pList : _listLiveCmd )
        {
            if ( pList != nullptr && pList->getRecordingState()._activeGraphicsPSO == pso )
                pList->getRecordingState()._activeGraphicsPSO = 0;
        }
    }

    void D3D11RHIDevice::registerCommandList( D3D11RHICommandList* pCmdList )
    {
        ScopedLock<mutex> lock{ _liveCmdListMutex };
        _listLiveCmd.push_back( pCmdList );
    }

    void D3D11RHIDevice::unregisterCommandList( D3D11RHICommandList* pCmdList )
    {
        ScopedLock<mutex> lock{ _liveCmdListMutex };
        (void)VectorUtil::removeSingleSwap( _listLiveCmd, pCmdList ); // 등록되지 않은 리스트면 뺄 것이 없다
    }

    void D3D11RHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        if ( pCmdList == nullptr || _immediateContext.isValid() == false )
            return;
        auto*              pNative = static_cast<D3D11RHICommandList*>( pCmdList );
        ID3D11CommandList* pList   = pNative->getNativeCommandList();
        if ( pList == nullptr )
            return;
        // DX11 은 스트림을 자를 필요가 없다. 기록 대상(Deferred Context)과 제출 대상(Immediate
        // Context)이 처음부터 분리돼 있어서, 이 호출은 Immediate Context 스트림의 '지금 이 지점'에
        // 그대로 끼워진다. DX12/Vulkan 이 세그먼트를 잘라 얻는 순서 보장을 공짜로 갖는다.
        const D3D11ImmediateContext::ScopedLock context = _immediateContext.lock();
        context->ExecuteCommandList( pList, FALSE );

        // 남은 차이는 제출 시점뿐이다. Immediate Context 는 커맨드를 모아뒀다가 드라이버가 정한
        // 때(보통 Present)에 GPU 로 보내므로, 오류가 나면 어느 리스트 때문인지 알 수 없다.
        // 즉시 모드에서는 리스트마다 밀어내 그 경계에서 오류가 드러나게 한다.
        if ( _bImmediateSubmit )
            context->Flush();
    }
} // namespace sw
#endif
