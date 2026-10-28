#include "pch.h"

#include "Engine/Graphics/RHI/DX/DX11/D3D11RHIDevice.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Graphics/RHI/DX/DX11/D3D11RHICommandContext.h"
#include "Engine/Graphics/RHI/DX/DX11/D3D11RHICommandList.h"
#include "Engine/Graphics/RHI/DX/DX11/D3D11RHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include "Engine/Foundation/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineDefaultAssets.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiMemoryBudget.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D11" );

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
    } // namespace

    D3D11RHIDevice::D3D11RHIDevice()
        : _device{ nullptr }
        , _memoryAdapter{ nullptr }
        , _immediateContext{}
        , _backBufferFormat{ constant::kBackBufferFormat }
        , _swapChain{}
        , _vertexBuffer{ nullptr }
        , _gpuBuffers{}
        , _gpuTextures{}
        , _timestampFrameIndex{ 0 }
        , _timestampWrittenMask{ 0 }
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
        , _listBindlessFree{}
        , _listRegisteredTexture{}
        , _listTextureFree{}
        , _listRegisteredUAV{}
        , _listUAVSourceBuffer{}
        , _listUAVFree{}
        , _pipelineStates{}
        , _renderPassLedger{}
        , _arrStaticSampler{}
        , _pHWnd{ nullptr }
        , _releaseQueue{ constant::kGPUReleaseFrameLatency }
        , _transferSerials{}
        , _transferQueue{ _transferSerials }
        , _listFreeTransferQuery{}
        , _readbackTable{}
        , _uploadStagingBudget{}
        , _directUploadMaxBytes{ 0 }
        , _frameStreamContext{ nullptr }
        , _resourceImpl{ nullptr }
    {
        _resourceImpl          = sw::make_unique<D3D11RHIResourceFactory>( this );
        _pTimestampProvider    = this;
        _pContextThreadBinding = this;
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
            SW_LOG_ERROR( "Failed to create root-constant cbuffer." );
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
        getMemoryLedger().recordAllocation( RHIMemoryKey::makeBuffer( handle ), RHIMemoryKind::Buffer, bufferDesc.ByteWidth );
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
        getMemoryLedger().recordAllocation( RHIMemoryKey::makeTexture( handle ), RHIMemoryLedger::classifyTexture( desc ),
                                            RHIMemoryLedger::computeTextureLogicalBytes( desc ) );
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
        // 장부 크기는 소스 바이트(논리 크기)다 — 드라이버가 행 간격을 늘린 실제 할당은 물을 API 가 없다.
        _pDevice->getMemoryLedger().recordAllocation( RHIMemoryKey::makeDeviceObject( _texture.Get() ), RHIMemoryKind::Staging, sourceBytes );
    }

    void D3D11RHIDevice::UploadStaging::release()
    {
        if ( _pDevice == nullptr )
            return;
        if ( _texture != nullptr )
        {
            // 주소가 다른 자원에 다시 쓰이기 전에 장부에서 내린다.
            _pDevice->getMemoryLedger().recordFree( RHIMemoryKey::makeDeviceObject( _texture.Get() ) );
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
            SW_LOG_ERROR( "%#: CreateTexture2D of the %#×%# staging texture (%# mips, %# bytes) failed hr=0x%#", pCaller, width, height, mipCount, uploadBytes,
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
        record._lastTransferSerial.store( serial );
        return _transferSerials.makeAccepted( serial );
    }

    bool D3D11RHIDevice::queryGPUMemoryBudgetInternal( RHIMemoryBudget& outBudget )
    {
        return queryDxgiMemoryBudget( _memoryAdapter.Get(), outBudget );
    }

    #if !defined( SW_SHIPPING )
    bool D3D11RHIDevice::isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        if ( space == RHIBindlessIndexSpace::Texture )
            return index < _listRegisteredTexture.size() && _listRegisteredTexture[index] != 0;
        return index < _listRegisteredBindless.size() && _listRegisteredBindless[index] != 0;
    }
    #endif
} // namespace sw
#endif
