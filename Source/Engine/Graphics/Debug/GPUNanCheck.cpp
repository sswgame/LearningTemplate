#include "pch.h"

#include "Engine/Graphics/Debug/GPUNanCheck.h"

#include "Core/Log/LogOnce.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Debug/RenderDiagnostics.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"

namespace sw
{
    namespace
    {
        /// gpu_nan_check.hlsl 의 루트 상수입니다 — 선언 순서 · 크기가 같아야 한다.
        struct GPUNanCheckRootConstants
        {
            uint32 _textureIndex;
            uint32 _width;
            uint32 _height;
            uint32 _slot;
        };
        static_assert( sizeof( GPUNanCheckRootConstants ) == 4 * sizeof( uint32 ), "gpu_nan_check.hlsl 의 루트 상수와 크기가 같아야 한다" );
    } // namespace

    GPUNanCheck::GPUNanCheck()
        : _pDevice{ nullptr }
        , _buffer{}
        , _readback{}
        , _listSlotName{}
        , _listPreviousCount{}
        , _frameSerial{ 0 }
        , _reportedNanCount{ 0 }
        , _reportedInfCount{ 0 }
        , _bScannedThisFrame{ SW_FALSE }
    {
    }

    GPUNanCheck::~GPUNanCheck()
    {
        shutdown();
    }

    bool GPUNanCheck::isRequested()
    {
        return RenderDiagnostics::get()._bNanCheck == SW_TRUE; // 배포본은 상수 기본값이라 PSO 도 디스패치도 만들지 않는다
    }

    bool GPUNanCheck::isScannableFormat( RHIFormat format )
    {
        return isRHIFormatFloatTexel( format );
    }

    bool GPUNanCheck::initialize( IRHIDevice* pDevice )
    {
        if ( isRequested() == false || isActive() )
            return true;
        if ( pDevice == nullptr )
        {
            SW_LOG_ERROR_ID( "GPUNanCheck.NaNCheckRequestedGv", "NaN check is requested but there is no device" );
            return false;
        }
        // 능력 선언 분기 - 컴퓨트가 엔진 텍스처를 읽을 수 있는 백엔드만 쓴다. 에뮬 백엔드(DX11 · GL)의 컴퓨트는 샘플 텍스처를 읽지 못하고, 다른 읽기 길을 두지 않는다.
        if ( pDevice->getCapabilities()._bCompute == SW_FALSE || pDevice->supportsNativeBindlessSampling() == false )
        {
            SW_LOG_WARNING_ID( "GPUNanCheck.GvNanCheckIgnoredBackend", "The NaN check is ignored on this backend: the NaN scan reads output textures in a compute shader and needs native bindless sampling (DirectX12, Vulkan)" );
            return true;
        }

        const vector<uint32> zeroed( kMaxSlot * 2, 0u );
        const bool           bCreated = _buffer.ensureCapacity( pDevice, sizeof( uint32 ), kMaxSlot * 2,
                                                                RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess, true, true, zeroed.data() );
        if ( bCreated == false )
        {
            SW_LOG_ERROR_ID( "GPUNanCheck.FailedCreateNaNCheck", "Failed to create the NaN check counter buffer (%# uints) - the NaN check cannot run", kMaxSlot * 2 );
            _buffer.release( pDevice );
            return false;
        }
        if ( _readback.initialize( pDevice, sizeof( uint32 ), kMaxSlot * 2 ) == false )
        {
            SW_LOG_ERROR_ID( "GPUNanCheck.FailedCreateNaNCheck2", "Failed to create the NaN check readback buffers - the NaN check cannot run" );
            _buffer.release( pDevice );
            return false;
        }
        _pDevice = pDevice;
        _listSlotName.clear();
        _listPreviousCount.assign( kMaxSlot * 2, 0u );
        _frameSerial = 0;
        SW_LOG_INFO_ID( "GPUNanCheck.NaNCheckReadyOutput", "NaN check ready (%# output slots, read back a few frames late)", kMaxSlot );
        return true;
    }

    void GPUNanCheck::shutdown()
    {
        _readback.shutdown();
        if ( _pDevice != nullptr )
            _buffer.release( _pDevice );
        _pDevice = nullptr;
    }

    uint32 GPUNanCheck::getOrCreateSlot( string_view name )
    {
        for ( uint32 slot = 0; slot < static_cast<uint32>( _listSlotName.size() ); ++slot )
        {
            if ( string_view( _listSlotName[slot] ) == name )
                return slot;
        }
        if ( _listSlotName.size() >= kMaxSlot )
            return kMaxSlot;
        _listSlotName.push_back( string( name ) );
        return static_cast<uint32>( _listSlotName.size() - 1 );
    }

    void GPUNanCheck::scan( IRHICommandList& commands, RHIPipelineStateHandle pso, string_view name, RHIDescriptorIndex srv, uint32 width, uint32 height )
    {
        if ( isActive() == false || pso == 0 || srv == kInvalidDescriptorIndex || width == 0 || height == 0 )
            return;
        const uint32 slot = getOrCreateSlot( name );
        if ( slot >= kMaxSlot )
        {
            SW_LOG_ERROR_ONCE_ID( "GPUNanCheck.NaNCheckMoreOutputs", "NaN check has more than %# outputs - the rest are not scanned", kMaxSlot );
            return;
        }
        GPUNanCheckRootConstants constants{ srv, width, height, slot };
        commands.transitionBuffer( _buffer._buffer, RHIBufferState::UnorderedAccess );
        commands.setComputePipelineState( pso );
        commands.bindComputeUAV( _buffer._uav, 0 );
        commands.setComputeRootConstants( 0, sizeof( constants ) / sizeof( uint32 ), &constants );
        commands.dispatchThreads( width, height, 1, kGroupSize, kGroupSize, 1 );
        _bScannedThisFrame = SW_TRUE;
    }

    void GPUNanCheck::requestReadback( IRHICommandList& commands )
    {
        if ( isActive() == false || _bScannedThisFrame == SW_FALSE )
            return;
        _bScannedThisFrame = SW_FALSE;
        const uint64 next  = _frameSerial + 1;
        // 읽기 버퍼가 모두 GPU 가 쓰는 중이면 이 프레임의 사본은 건너뛴다 - 카운터는 누적이라 다음 사본이 이번 수까지 담는다.
        if ( _readback.request( commands, _buffer._buffer, RHIBufferState::UnorderedAccess, next ) )
            _frameSerial = next;
    }

    void GPUNanCheck::poll()
    {
        if ( isActive() == false )
            return;
        vector<uint8> bytes;
        uint64        frame = 0;
        if ( _readback.takeLatest( bytes, frame ) == false )
            return;
        if ( bytes.size() < static_cast<size_t>( kMaxSlot ) * 2 * sizeof( uint32 ) )
        {
            SW_LOG_ERROR_ONCE_ID( "GPUNanCheck.NaNCheckReadbackSmaller", "NaN check readback is smaller than the counter buffer (%# bytes)", static_cast<uint32>( bytes.size() ) );
            return;
        }
        vector<uint32> words( kMaxSlot * 2 );
        Memory::copy( words.data(), bytes.data(), words.size() * sizeof( uint32 ) );

        for ( uint32 slot = 0; slot < static_cast<uint32>( _listSlotName.size() ); ++slot )
        {
            // 부호 없는 뺄셈이라 카운터가 한 바퀴 돌아도 맞는다.
            const uint32 nanCount            = words[slot * 2] - _listPreviousCount[slot * 2];
            const uint32 infCount            = words[slot * 2 + 1] - _listPreviousCount[slot * 2 + 1];
            _listPreviousCount[slot * 2]     = words[slot * 2];
            _listPreviousCount[slot * 2 + 1] = words[slot * 2 + 1];
            if ( nanCount == 0 && infCount == 0 )
                continue;
            _reportedNanCount += nanCount;
            _reportedInfCount += infCount;
            SW_LOG_ERROR_ID( "GPUNanCheck.NaNInfOutputFrames", "NaN/Inf in the output of %# (frames up to %#): %# NaN texels, %# Inf texels", _listSlotName[slot].c_str(), frame, nanCount, infCount );
        }
    }
} // namespace sw
