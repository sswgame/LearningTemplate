#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHICommandContext.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHICommandList.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIResource.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    namespace
    {
        /**
         * @struct D3D11RecordingToken
         * @brief 이 스레드가 기록 중인 리스트의 토큰 — 컨텍스트 포인터를 들지 않는다. 디바이스가 슬롯 표로 검증한다
         *        (`D3D11RHIDevice::acquireRecordingSlot` 주석).
         */
        struct D3D11RecordingToken
        {
            uint64 _deviceSerial;
            uint64 _generation;
            uint32 _slot;
        };
        thread_local D3D11RecordingToken s_recordingToken{};

        /// @brief 디바이스 일련번호의 출처 — 같은 주소에 새 디바이스가 서도 옛 토큰이 맞지 않게.
        atomic<uint64> s_deviceSerialCounter{ 0 };

    #if defined( SW_DEBUG )
        // 아래 표와 판별 함수는 디버그 레이어 메시지를 거르는 `flushDebugMessages` 전용이고, 그 함수의
        // 본문 전체가 SW_DEBUG 안에 있다. 가드를 맞추지 않으면 Release 빌드에서 "정의했는데 아무도
        // 쓰지 않는다"(-Wunused-function)가 남는다.

        /** @brief 출력과 입력에 같은 리소스가 동시에 걸렸을 때 D3D11 이 내는 메시지 ID 목록. */
        constexpr D3D11_MESSAGE_ID arrHazardMessageId[] = {
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
         * @details D3D11 은 이걸 **WARNING** 으로 낸다. 그런데 결과는 조용한 실패다 — 런타임이 한쪽을
         *          NULL 로 강제하고 셰이더는 0 을 읽는다. 인스턴스 버퍼(t4)가 컴퓨트 UAV 에 걸린 채
         *          남아서 DX11 만 화면에 아무것도 못 그리던 게 이 경고 뒤에 숨어 있었고, 심각도로
         *          거른 탓에 로그에 한 줄도 안 나왔다. 그래서 해저드만은 ERROR 로 올린다.
         */
        bool isHazardMessage( D3D11_MESSAGE_ID id )
        {
            // switch 로 적으면 -Wswitch-enum 이 나머지 1318개를 다루라고 요구한다. 경고를 끄는
            // 대신 목록 순회로 바꾼다 — ID 를 더 넣을 때도 한 줄이다.
            for ( const D3D11_MESSAGE_ID hazardId : arrHazardMessageId )
            {
                if ( id == hazardId )
                    return true;
            }
            return false;
        }
    #endif
    } // namespace

    D3D11RHIDevice::D3D11RHIDevice()
        : _device{ nullptr }
        , _deviceContext{ nullptr }
        , _contextOwnerThread{}
        , _swapChain{}
        , _vertexBuffer{ nullptr }
        , _gpuBuffers{}
        , _gpuTextures{}
        , _bDriverCommandLists{ SW_FALSE }
        , _listRegisteredBindless{}
        , _listBindlessFree{}
        , _listRegisteredTexture{}
        , _listTextureFree{}
        , _listRegisteredUAV{}
        , _listUavSourceBuffer{}
        , _listUavFree{}
        , _pipelineStates{}
        , _listRenderPass{}
        , _depthEnabledState{ nullptr }
        , _depthDisabledState{ nullptr }
        , _linearSampler{ nullptr }
        , _arrStaticSampler{}
        , _pHWnd{ nullptr }
        , _backBufferFormat{ constant::kBackBufferFormat }
        , _releaseQueue{ constant::kGpuReleaseFrameLatency }
        , _frameStreamContext{ nullptr }
        , _resourceImpl{ nullptr }
        , _arrRecordingSlot{}
        , _serial{ 0 }
    {
        _serial       = s_deviceSerialCounter.fetch_add( 1, std::memory_order_relaxed ) + 1;
        _resourceImpl = sw::make_unique<D3D11RHIResource>( this );
    }

    D3D11RHIDevice::~D3D11RHIDevice()
    {
        shutdown();
    }

    IRHIResource*       D3D11RHIDevice::getResource() { return _resourceImpl.get(); }
    IRHICommandContext* D3D11RHIDevice::getFrameStreamContext() { return _frameStreamContext.get(); }

    uint32 D3D11RHIDevice::acquireRecordingSlot( ID3D11DeviceContext* pContext )
    {
        if ( pContext == nullptr )
            return kNoRecordingSlot;
        for ( uint32 slot = 0; slot < kMaxRecordingSlot; ++slot )
        {
            // 빈 슬롯(nullptr)을 CAS 로 집는다 — 잠금 없이, 어느 스레드에서 리스트를 만들어도 된다. 세대는 되돌리지 않는다:
            // 되쓰는 슬롯의 옛 토큰이 새 리스트의 세대와 맞아떨어지면 안 된다.
            ID3D11DeviceContext* pExpected = nullptr;
            if ( _arrRecordingSlot[slot]._pContext.compare_exchange_strong( pExpected, pContext, std::memory_order_acq_rel ) )
                return slot;
        }
        SW_LOG_WARNING( "D3D11 recording slots exhausted (%#) — this command list's in-record updates fall back to the immediate context", kMaxRecordingSlot );
        return kNoRecordingSlot;
    }

    void D3D11RHIDevice::releaseRecordingSlot( uint32 slot )
    {
        if ( slot >= kMaxRecordingSlot )
            return;
        D3D11RecordingSlot& entry = _arrRecordingSlot[slot];
        // 세대를 반드시 바꾼다(다음 짝수) — 리스트가 기록 중에 죽었어도 남은 토큰은 전부 무효가 된다.
        const uint64 generation = entry._generation.load( std::memory_order_relaxed );
        entry._generation.store( ( generation | 1 ) + 1, std::memory_order_release );
        entry._pContext.store( nullptr, std::memory_order_release );
    }

    void D3D11RHIDevice::beginRecording( uint32 slot )
    {
        if ( slot >= kMaxRecordingSlot )
            return;
        D3D11RecordingSlot& entry      = _arrRecordingSlot[slot];
        const uint64        generation = entry._generation.load( std::memory_order_relaxed );
        // 늘 새 세대다 — 닫지 않고 다시 열어도 앞 세션의 토큰은 무효가 된다.
        const uint64 recordingGeneration = ( ( generation & 1 ) != 0 ) ? generation + 2 : generation + 1;
        entry._generation.store( recordingGeneration, std::memory_order_release );
        s_recordingToken = D3D11RecordingToken{ _serial, recordingGeneration, slot };
    }

    void D3D11RHIDevice::endRecording( uint32 slot )
    {
        if ( slot >= kMaxRecordingSlot )
            return;
        D3D11RecordingSlot& entry      = _arrRecordingSlot[slot];
        const uint64        generation = entry._generation.load( std::memory_order_relaxed );
        if ( ( generation & 1 ) != 0 )
            entry._generation.store( generation + 1, std::memory_order_release );
        // 이 스레드의 토큰이 이 슬롯이면 비운다 — 세대가 바뀌어 어차피 무효지만, 다음 조회를 짧게 끝낸다.
        if ( s_recordingToken._deviceSerial == _serial && s_recordingToken._slot == slot )
            s_recordingToken = D3D11RecordingToken{};
    }

    void D3D11RHIDevice::bindRecordingThread( ID3D11DeviceContext* pContext )
    {
        if ( pContext == nullptr || pContext == _deviceContext.Get() )
            return;
        for ( uint32 slot = 0; slot < kMaxRecordingSlot; ++slot )
        {
            const D3D11RecordingSlot& entry = _arrRecordingSlot[slot];
            if ( entry._pContext.load( std::memory_order_acquire ) != pContext )
                continue;
            const uint64 generation = entry._generation.load( std::memory_order_acquire );
            if ( ( generation & 1 ) != 0 )
                s_recordingToken = D3D11RecordingToken{ _serial, generation, slot };
            return;
        }
    }

    ID3D11DeviceContext* D3D11RHIDevice::resolveRecordingContext() const
    {
        const D3D11RecordingToken& token = s_recordingToken;
        if ( token._deviceSerial != _serial || token._slot >= kMaxRecordingSlot )
            return nullptr;
        const D3D11RecordingSlot& entry    = _arrRecordingSlot[token._slot];
        ID3D11DeviceContext*      pContext = entry._pContext.load( std::memory_order_acquire );
        // 컨텍스트를 먼저 읽고 세대를 나중에 본다 — 세대가 아직 토큰과 같으면 그 컨텍스트는 그 세대의 것이다.
        if ( entry._generation.load( std::memory_order_acquire ) != token._generation )
            return nullptr;
        return pContext;
    }

    void D3D11RHIDevice::bindStaticSamplers( ID3D11DeviceContext* pContext ) const
    {
        if ( pContext == nullptr || _arrStaticSampler[0] == nullptr )
            return;
        ID3D11SamplerState* arrSampler[shaderslot::kStaticSamplerArrayCount]{};
        for ( uint32 samplerIndex = 0; samplerIndex < shaderslot::kStaticSamplerArrayCount; ++samplerIndex )
            arrSampler[samplerIndex] = _arrStaticSampler[samplerIndex].Get();
        pContext->PSSetSamplers( shaderslot::dx11::kStaticSampler0, shaderslot::kStaticSamplerArrayCount, arrSampler );
    }

    void D3D11RHIDevice::flushDebugMessages( const utf8* pStage )
    {
    #if defined( SW_DEBUG )
        // 디버그 레이어의 CORRUPTION/ERROR 만 로그로 올린다 — WARNING(null 샘플러 → 기본 상태 등)은 정상 경로에서도 매 드로우
        // 나오므로 버린다. DX12 의 flushDebugMessages 와 같은 자리(프레임 끝)에서 부른다.
        Microsoft::WRL::ComPtr<ID3D11InfoQueue> queue;
        if ( _device == nullptr || FAILED( _device.As( &queue ) ) || queue == nullptr )
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
            if ( pMessage->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION || pMessage->Severity == D3D11_MESSAGE_SEVERITY_ERROR ||
                 isHazardMessage( pMessage->ID ) )
                SW_LOG_ERROR( "[%#] %#", pStage, pMessage->pDescription );
        }
        queue->ClearStoredMessages();
    #else
        (void)pStage;
    #endif
    }

    void* D3D11RHIDevice::getNativeTexturePointer( RHITextureHandle texture ) const
    {
        const TextureRecord* pRec = resolveTexture( texture );
        return pRec != nullptr ? pRec->_texture.Get() : nullptr;
    }

    bool D3D11RHIDevice::ensureRootConstantCb( D3D11RecordingState& state )
    {
        if ( state._rootConstantCb != nullptr )
            return true;
        if ( _device == nullptr )
            return false;

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth      = kMaxComputeRootConstantDwords * sizeof( uint32 );
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        // `ID3D11Device::CreateBuffer` 는 free-threaded 라 병렬 기록 중에 만들어도 된다 —
        // 스레드마다 자기 상태의 버퍼를 만들 뿐, 공유 표를 건드리지 않는다.
        if ( FAILED( _device->CreateBuffer( &desc, nullptr, state._rootConstantCb.GetAddressOf() ) ) )
        {
            SW_LOG_ERROR( "Failed to create root-constant cbuffer." );
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------------------------
    // D3D11RHISwapChain Implementation
    // ------------------------------------------------------------------------------

    // ------------------------------------------------------------------------------
    // D3D11RHIResource Implementation
    // ------------------------------------------------------------------------------

    ID3D11Buffer* D3D11RHIDevice::resolveBuffer( RHIBufferHandle handle ) const
    {
        const Microsoft::WRL::ComPtr<ID3D11Buffer>* pSlot = _gpuBuffers.get( handle );
        return pSlot != nullptr ? pSlot->Get() : nullptr;
    }

    size_t D3D11RHIDevice::bindlessBufferCount() const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        return _listRegisteredBindless.size();
    }

    RHIBufferHandle D3D11RHIDevice::bindlessBufferAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredBindless.size() )
            return 0;
        return _listRegisteredBindless[index];
    }

    RHITextureHandle D3D11RHIDevice::bindlessTextureAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredTexture.size() )
            return 0;
        return _listRegisteredTexture[index];
    }

    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> D3D11RHIDevice::bindlessUavAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredUAV.size() )
            return nullptr;
        return _listRegisteredUAV[index];
    }

    RHIBufferHandle D3D11RHIDevice::uavSourceBufferAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listUavSourceBuffer.size() )
            return RHIBufferHandle{ 0 };
        return _listUavSourceBuffer[index];
    }

    RHIBufferHandle D3D11RHIDevice::storeBuffer( Microsoft::WRL::ComPtr<ID3D11Buffer> buffer )
    {
        if ( buffer == nullptr )
            return 0;
        return _gpuBuffers.insert( std::move( buffer ) );
    }

    D3D11RHIDevice::TextureRecord* D3D11RHIDevice::resolveTexture( RHITextureHandle handle )
    {
        return _gpuTextures.get( handle );
    }

    const D3D11RHIDevice::TextureRecord* D3D11RHIDevice::resolveTexture( RHITextureHandle handle ) const
    {
        return _gpuTextures.get( handle );
    }

    RHITextureHandle D3D11RHIDevice::storeTexture( TextureRecord record )
    {
        if ( record._texture == nullptr )
            return 0;
        return _gpuTextures.insert( std::move( record ) );
    }
} // namespace sw
#endif
