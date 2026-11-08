#include "pch.h"

#include "Engine/Graphics/Debug/GPUMessage.h"

#include "Core/Container/formatString.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/BitUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Debug/RenderDiagnostics.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    namespace
    {
        struct GPUMessageInternal
        {
            /// 한 번 읽을 때 구별해 모으는 id 수의 상한입니다(넘는 id 의 기록은 버려진 수로 센다).
            static constexpr uint32 kMaxDistinctPerPoll = 64;
            /// 서식이 인자 하나를 글로 바꿀 때 쓰는 임시 칸입니다(float 의 가장 긴 표기도 들어간다).
            static constexpr uint32 kArgTextCapacity = 48;

            /// 등록 이름은 id 의 10 진 글이다 — 이름 중복 거절이 곧 id 중복 거절이다.
            static void makeIDName( uint32 id, utf8* pOut, uint32 capacity ) { formatstring( pOut, capacity, "%#", id ); }

            static SW_DEFINE_LOCKED_REGISTRATION_LIST( getFormatList, const GPUMessageFormat, "GPUMessageFormat", RegistrationOrder::Insertion, true );

            /** @brief 인자 칸 하나를 종류대로 글로 씁니다. */
            static void formatArg( GPUMessageArg kind, uint32 bits, utf8* pOut, uint32 capacity )
            {
                switch ( kind )
                {
                    case GPUMessageArg::Uint:
                    {
                        formatstring( pOut, capacity, "%#", bits );
                        return;
                    }
                    case GPUMessageArg::Int:
                    {
                        formatstring( pOut, capacity, "%#", static_cast<int32>( bits ) );
                        return;
                    }
                    case GPUMessageArg::Float:
                    {
                        formatstring( pOut, capacity, "%#", BitUtil::bitsToFloat( bits ) );
                        return;
                    }
                    case GPUMessageArg::Hex:
                    {
                        formatstring( pOut, capacity, "0x%x", bits );
                        return;
                    }
                    case GPUMessageArg::None:
                    {
                        break;
                    }
                }
                pOut[0] = '\0';
            }
        };

        /// @brief 한 번의 읽기에서 메시지 id 하나로 묶은 기록입니다.
        struct GPUMessageGroup
        {
            GPUMessageRecord _first;
            uint32           _count;
        };
    } // namespace

    // ------------------------------------------------------------------------------
    // 엔진 서식 표 — 번호는 gpu_message_ids.hlsli. 게임 · 키트는 자기 .cpp 에서 같은 매크로로 더한다.
    // ------------------------------------------------------------------------------
    SW_GPU_MESSAGE_FORMAT( cullLODLevel, SW_GPU_ASSERT_ID_CULL_LOD_LEVEL, GPUMessageSeverity::Error, "LOD level %# is not below the group level count", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None, "gpu_cull.hlsl" );
    SW_GPU_MESSAGE_FORMAT( cullLODPrevious, SW_GPU_ASSERT_ID_CULL_LOD_PREVIOUS, GPUMessageSeverity::Error, "previous LOD level %# is not below the group level count", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None,
                           "gpu_cull.hlsl" );
    SW_GPU_MESSAGE_FORMAT( cullLODLevelBits, SW_GPU_ASSERT_ID_CULL_LOD_LEVEL_BITS, GPUMessageSeverity::Error, "LOD level %# does not fit the 3 bit field of a visible entry", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None,
                           GPUMessageArg::None, "gpu_cull.hlsl" );
    SW_GPU_MESSAGE_FORMAT( cullFadeLevel, SW_GPU_ASSERT_ID_CULL_FADE_LEVEL, GPUMessageSeverity::Error, "crossfade progress %# does not fit the 4 bit field of an entry", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None,
                           "gpu_cull.hlsl" );
    SW_GPU_MESSAGE_FORMAT( cullBatchRange, SW_GPU_ASSERT_ID_CULL_BATCH_RANGE, GPUMessageSeverity::Error, "visible entry batch index %# is outside the batch table", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None,
                           "gpu_cull.hlsl" );
    SW_GPU_MESSAGE_FORMAT( cullVisibleRange, SW_GPU_ASSERT_ID_CULL_VISIBLE_RANGE, GPUMessageSeverity::Error, "visible list write slot %# is outside the visible range (entry dropped)", GPUMessageArg::Uint, GPUMessageArg::None,
                           GPUMessageArg::None, GPUMessageArg::None, "gpu_cull.hlsl" );
    SW_GPU_MESSAGE_FORMAT( cullInstanceID, SW_GPU_ASSERT_ID_CULL_INSTANCE_ID, GPUMessageSeverity::Error, "instance id %# does not fit the 24 bit field of a visible entry", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None,
                           "gpu_cull.hlsl" );
    SW_GPU_MESSAGE_FORMAT( markerClipNaN, SW_GPU_ASSERT_ID_MARKER_CLIP_NAN, GPUMessageSeverity::Warning, "marker projection produced a NaN or infinite clip position (w %#)", GPUMessageArg::Float, GPUMessageArg::None, GPUMessageArg::None,
                           GPUMessageArg::None, "canvas_marker.hlsl" );
    SW_GPU_MESSAGE_FORMAT( markerInstanceRange, SW_GPU_ASSERT_ID_MARKER_INSTANCE_RANGE, GPUMessageSeverity::Error, "marker instance slot %# is outside the GPUScene instance buffer", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None,
                           GPUMessageArg::None, "canvas_marker.hlsl" );
    SW_GPU_MESSAGE_FORMAT( markerRowRange, SW_GPU_ASSERT_ID_MARKER_ROW_RANGE, GPUMessageSeverity::Error, "marker input row %# is outside the row buffer", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None, "canvas_marker.hlsl" );
    SW_GPU_MESSAGE_FORMAT( markerWorldNaN, SW_GPU_ASSERT_ID_MARKER_WORLD_NAN, GPUMessageSeverity::Warning, "marker world point is NaN or infinite (x %#)", GPUMessageArg::Float, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None,
                           "canvas_marker.hlsl" );
    SW_GPU_MESSAGE_FORMAT( morphVertexRange, SW_GPU_ASSERT_ID_MORPH_VERTEX_RANGE, GPUMessageSeverity::Error, "rest vertex element %# is outside the rest buffer", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None,
                           "mesh_morph.hlsl" );
    SW_GPU_MESSAGE_FORMAT( morphProgramSlot, SW_GPU_ASSERT_ID_MORPH_PROGRAM_SLOT, GPUMessageSeverity::Error, "deform program slot %# is outside the program table", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None,
                           "mesh_morph.hlsl" );
    SW_GPU_MESSAGE_FORMAT( modifierProgramRange, SW_GPU_ASSERT_ID_MODIFIER_PROGRAM_RANGE, GPUMessageSeverity::Error, "modifier program start %# is outside the program table", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None,
                           GPUMessageArg::None, "mesh_modifier.hlsli" );
    SW_GPU_MESSAGE_FORMAT( modifierRecordRange, SW_GPU_ASSERT_ID_MODIFIER_RECORD_RANGE, GPUMessageSeverity::Error, "modifier record %# is outside the program table", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None,
                           "mesh_modifier.hlsli" );
    SW_GPU_MESSAGE_FORMAT( modifierRawRange, SW_GPU_ASSERT_ID_MODIFIER_RAW_RANGE, GPUMessageSeverity::Error, "raw program table read %# is outside the table", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None,
                           "mesh_modifier.hlsli" );
    SW_GPU_MESSAGE_FORMAT( modifierNodeRange, SW_GPU_ASSERT_ID_MODIFIER_NODE_RANGE, GPUMessageSeverity::Error, "conform BVH node index %# is not below the node count", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None, GPUMessageArg::None,
                           "mesh_modifier.hlsli" );
    SW_GPU_MESSAGE_FORMAT( modifierTriangleRange, SW_GPU_ASSERT_ID_MODIFIER_TRIANGLE_RANGE, GPUMessageSeverity::Error, "conform BVH leaf triangle %# is outside the triangle table", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None,
                           GPUMessageArg::None, "mesh_modifier.hlsli" );
    SW_GPU_MESSAGE_FORMAT( modifierStackOverflow, SW_GPU_ASSERT_ID_MODIFIER_STACK_OVERFLOW, GPUMessageSeverity::Error, "conform BVH traversal stack would overflow (depth %#)", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None,
                           GPUMessageArg::None, "mesh_modifier.hlsli" );
    SW_GPU_MESSAGE_FORMAT( modifierUVTableRange, SW_GPU_ASSERT_ID_MODIFIER_UV_TABLE_RANGE, GPUMessageSeverity::Error, "modifier vertex UV table read %# is outside the program table", GPUMessageArg::Uint, GPUMessageArg::None, GPUMessageArg::None,
                           GPUMessageArg::None, "mesh_modifier.hlsli" );
    SW_GPU_MESSAGE_FORMAT( presentPixel, SW_GPU_MESSAGE_ID_PRESENT_PIXEL, GPUMessageSeverity::Info, "present pixel color %# %# %#", GPUMessageArg::Float, GPUMessageArg::Float, GPUMessageArg::Float, GPUMessageArg::None, "fullscreen_blit.hlsl" );

    bool GPUMessageFormat::isConsistent() const
    {
        if ( _pFormat == nullptr )
            return false;
        uint32 placeholderCount = 0;
        for ( const utf8* pCursor = _pFormat; pCursor[0] != '\0'; ++pCursor )
        {
            if ( pCursor[0] == '%' && pCursor[1] == '#' )
                ++placeholderCount;
        }
        uint32 usedArgCount = 0;
        for ( const GPUMessageArg kind : _arrArg )
        {
            if ( kind != GPUMessageArg::None )
                ++usedArgCount;
        }
        return placeholderCount == usedArgCount;
    }

    void GPUMessageFormat::formatArgs( const GPUMessageRecord& record, utf8* pOut, uint32 capacity ) const
    {
        if ( pOut == nullptr || capacity == 0 )
            return;
        uint32 written = 0;
        uint32 argSlot = 0;
        for ( const utf8* pCursor = _pFormat; pCursor != nullptr && pCursor[0] != '\0' && written + 1 < capacity; ++pCursor )
        {
            const bool bPlaceholder = pCursor[0] == '%' && pCursor[1] == '#';
            if ( bPlaceholder == false )
            {
                pOut[written++] = pCursor[0];
                continue;
            }
            ++pCursor; // '#' 를 건너뛴다
            // 다음 쓰는 칸 — `isConsistent` 가 `%#` 수와 쓰는 칸 수를 맞춰 두었다.
            while ( argSlot < SW_GPU_MESSAGE_ARG_COUNT && _arrArg[argSlot] == GPUMessageArg::None )
            {
                ++argSlot;
            }
            if ( argSlot >= SW_GPU_MESSAGE_ARG_COUNT )
                continue;
            utf8 arrArgText[GPUMessageInternal::kArgTextCapacity];
            GPUMessageInternal::formatArg( _arrArg[argSlot], record._arrArg[argSlot], arrArgText, sizeof( arrArgText ) );
            ++argSlot;
            for ( const utf8* pArg = arrArgText; pArg[0] != '\0' && written + 1 < capacity; ++pArg )
            {
                pOut[written++] = pArg[0];
            }
        }
        pOut[written] = '\0';
    }

    bool GPUMessageFormat::registerFormat( const GPUMessageFormat* pFormat )
    {
        if ( pFormat == nullptr )
            return false;
        if ( pFormat->isConsistent() == false )
        {
            SW_LOG_ERROR_ID( "GPUMessage.FormatArgMismatch", "GPU message %# format '%#' does not have one placeholder per used argument - not registered", pFormat->_id,
                             pFormat->_pFormat != nullptr ? pFormat->_pFormat : "<null>" );
            return false;
        }
        utf8 arrName[constant::buffer::kMax16];
        GPUMessageInternal::makeIDName( pFormat->_id, arrName, sizeof( arrName ) );
        const RegistrationResult result = GPUMessageInternal::getFormatList().withLock( [&]( RegistrationList<const GPUMessageFormat>& list )
        {
            return list.add( pFormat, arrName );
        } );
        if ( result == RegistrationResult::DuplicateName )
            SW_LOG_ERROR_ID( "GPUMessage.DuplicateID", "GPU message id %# is registered twice (%# and another format) - the second is rejected", pFormat->_id, pFormat->_pSource );
        return result == RegistrationResult::Added || result == RegistrationResult::AlreadyPresent;
    }

    void GPUMessageFormat::unregisterFormat( const GPUMessageFormat* pFormat )
    {
        GPUMessageInternal::getFormatList().withLock( [&]( RegistrationList<const GPUMessageFormat>& list )
        {
            (void)list.remove( pFormat ); // 없는 항목이면 할 일이 없다
        } );
    }

    const GPUMessageFormat* GPUMessageFormat::find( uint32 id )
    {
        utf8 arrName[constant::buffer::kMax16];
        GPUMessageInternal::makeIDName( id, arrName, sizeof( arrName ) );
        return GPUMessageInternal::getFormatList().withLock( [&]( const RegistrationList<const GPUMessageFormat>& list )
        {
            return static_cast<const GPUMessageFormat*>( list.findByName( arrName ) );
        } );
    }

    uint32 GPUMessageFormat::getRegisteredCount()
    {
        return GPUMessageInternal::getFormatList().withLock( []( const RegistrationList<const GPUMessageFormat>& list )
        {
            return list.getCount();
        } );
    }

    GPUMessageRingRead GPUMessageRingRead::read( const uint32* pWords, uint32 wordCount, uint32 processedCounter )
    {
        GPUMessageRingRead result;
        result._counter = processedCounter;
        if ( pWords == nullptr || wordCount < SW_GPU_MESSAGE_BUFFER_UINTS )
            return result;
        const uint32 counter  = pWords[SW_GPU_MESSAGE_HEADER_COUNTER];
        const uint32 newCount = counter - processedCounter; // 32 비트로 돌아도 차는 맞는다
        result._counter       = counter;
        if ( newCount == 0 )
            return result;
        const uint32 readable = newCount < SW_GPU_MESSAGE_RING_CAPACITY ? newCount : static_cast<uint32>( SW_GPU_MESSAGE_RING_CAPACITY );
        result._dropped       = newCount - readable;
        result._listRecord.reserve( readable );
        const uint32 firstSequence = counter - readable;
        for ( uint32 offset = 0; offset < readable; ++offset )
        {
            const uint32  sequence = firstSequence + offset;
            const uint32* pRecord  = &pWords[kRecordBase + ( sequence % SW_GPU_MESSAGE_RING_CAPACITY ) * SW_GPU_MESSAGE_RECORD_UINTS];
            // 순번 + 1 이 맞는 칸만 믿는다 — 쓰다 만 칸이나 더 뒤의 기록이 덮은 칸은 버려진 것으로 센다.
            if ( pRecord[SW_GPU_MESSAGE_RECORD_SEQUENCE] != sequence + 1u )
            {
                ++result._dropped;
                continue;
            }
            GPUMessageRecord record;
            record._id = pRecord[SW_GPU_MESSAGE_RECORD_ID];
            record._x  = pRecord[SW_GPU_MESSAGE_RECORD_X];
            record._y  = pRecord[SW_GPU_MESSAGE_RECORD_Y];
            for ( uint32 argIndex = 0; argIndex < SW_GPU_MESSAGE_ARG_COUNT; ++argIndex )
            {
                record._arrArg[argIndex] = pRecord[SW_GPU_MESSAGE_RECORD_ARG0 + argIndex];
            }
            result._listRecord.push_back( record );
        }
        return result;
    }

    GPUMessageChannel::GPUMessageChannel()
        : _pDevice{ nullptr }
        , _buffer{}
        , _readback{}
        , _frameSerial{ 0 }
        , _processedCounter{ 0 }
        , _reportedCount{ 0 }
        , _droppedCount{ 0 }
    {
    }

    GPUMessageChannel::~GPUMessageChannel()
    {
        shutdown();
    }

    bool GPUMessageChannel::isRequested()
    {
        return RenderDiagnostics::get()._bGPUMessages == SW_TRUE; // 배포본은 상수 기본값이라 define 도 PSO 변형도 바인딩도 만들지 않는다
    }

    bool GPUMessageChannel::initialize( IRHIDevice* pDevice )
    {
        if ( isRequested() == false )
            return true;
        if ( isActive() )
            return true;
        if ( pDevice == nullptr )
        {
            SW_LOG_ERROR_ID( "GPUMessage.RequestedNoDevice", "GPU messages are requested but there is no device" );
            return false;
        }
        const RHICapabilities& caps = pDevice->getCapabilities();
        if ( caps._bCompute == SW_FALSE || caps._bGraphicsShaderWrites == SW_FALSE )
        {
            SW_LOG_ERROR_ID( "GPUMessage.RequestedNoShaderWrites", "GPU messages are requested but this backend cannot write a buffer from compute (%#) and pixel shaders (%#)",
                             static_cast<uint32>( caps._bCompute ), static_cast<uint32>( caps._bGraphicsShaderWrites ) );
            return false;
        }

        // 0 으로 채워 만든다 — 순번 + 1 이 0 인 칸이 "빈 칸" 이고 선택 칸은 updateControl 이 첫 프레임 앞에 채운다. Vulkan 은 새 버퍼를 지우지 않는다.
        const vector<uint32> zeroed( SW_GPU_MESSAGE_BUFFER_UINTS, 0u );
        const bool           bCreated = _buffer.ensureCapacity( pDevice, sizeof( uint32 ), SW_GPU_MESSAGE_BUFFER_UINTS,
                                                                RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess, true, true, zeroed.data() );
        if ( bCreated == false )
        {
            SW_LOG_ERROR_ID( "GPUMessage.FailedCreateRing", "Failed to create the GPU message ring buffer (%# uints) - GPU messages cannot run", static_cast<uint32>( SW_GPU_MESSAGE_BUFFER_UINTS ) );
            _buffer.release( pDevice );
            return false;
        }
        if ( _readback.initialize( pDevice, sizeof( uint32 ), SW_GPU_MESSAGE_BUFFER_UINTS ) == false )
        {
            SW_LOG_ERROR_ID( "GPUMessage.FailedCreateReadback", "Failed to create the GPU message readback buffers - GPU messages cannot run" );
            _buffer.release( pDevice );
            return false;
        }
        _pDevice          = pDevice;
        _frameSerial      = 0;
        _processedCounter = 0;
        SW_LOG_INFO_ID( "GPUMessage.RingReady", "GPU message ring ready (%# records, read back a few frames late)", static_cast<uint32>( SW_GPU_MESSAGE_RING_CAPACITY ) );
        return true;
    }

    void GPUMessageChannel::shutdown()
    {
        _readback.shutdown();
        if ( _pDevice != nullptr )
            _buffer.release( _pDevice );
        _pDevice = nullptr;
    }

    void GPUMessageChannel::updateControl( const GPUMessageSelection& selection, uint32 logBudget )
    {
        if ( isActive() == false )
            return;
        // 머리 [1..7] 과 예산 칸 — 예산 칸은 0 으로 되돌려 이번 프레임의 SW_GPU_LOG 가 다시 센다.
        uint32 arrControl[SW_GPU_MESSAGE_CONTROL_UINTS]{};
        arrControl[SW_GPU_MESSAGE_HEADER_SELECT_X - 1]      = selection._pixelX;
        arrControl[SW_GPU_MESSAGE_HEADER_SELECT_Y - 1]      = selection._pixelY;
        arrControl[SW_GPU_MESSAGE_HEADER_SELECT_THREAD - 1] = selection._thread;
        arrControl[SW_GPU_MESSAGE_HEADER_LOG_BUDGET - 1]    = logBudget;
        _buffer.upload( _pDevice, arrControl, sizeof( arrControl ), sizeof( uint32 ) * SW_GPU_MESSAGE_HEADER_SELECT_X );
    }

    void GPUMessageChannel::bindCompute( IRHICommandList& commands )
    {
        if ( isActive() == false )
            return;
        commands.transitionBuffer( _buffer._buffer, RHIBufferState::UnorderedAccess );
        commands.bindComputeUAV( _buffer._uav, SW_SLOT_GPU_MESSAGE_UAV );
    }

    void GPUMessageChannel::bindGraphics( IRHICommandList& commands )
    {
        if ( isActive() == false )
            return;
        // 상태 전이는 렌더 패스 밖에서 한다 — 링은 프리패스(컴퓨트)가 이미 UAV 상태로 두었고 되읽기 복사도 UAV 상태로 돌려 둔다(`requestReadback`).
        commands.bindGraphicsUAV( _buffer._uav, SW_SLOT_GPU_MESSAGE_UAV );
    }

    void GPUMessageChannel::requestReadback( IRHICommandList& commands )
    {
        if ( isActive() == false )
            return;
        const uint64 next = _frameSerial + 1;
        // 읽기 버퍼가 모두 GPU 가 쓰는 중이면 이 프레임의 사본은 건너뛴다 — 링은 지우지 않는 누적 카운터라 다음 사본이 이번 기록까지 담는다.
        if ( _readback.request( commands, _buffer._buffer, RHIBufferState::UnorderedAccess, next ) )
            _frameSerial = next;
    }

    void GPUMessageChannel::poll()
    {
        if ( isActive() == false )
            return;
        vector<uint8> bytes;
        uint64        frame = 0;
        if ( _readback.takeLatest( bytes, frame ) == false )
            return;
        if ( bytes.size() < static_cast<size_t>( SW_GPU_MESSAGE_BUFFER_UINTS ) * sizeof( uint32 ) )
        {
            SW_LOG_ERROR_ONCE_ID( "GPUMessage.ReadbackSmaller", "GPU message readback is smaller than the ring (%# bytes)", static_cast<uint32>( bytes.size() ) );
            return;
        }
        vector<uint32> words( SW_GPU_MESSAGE_BUFFER_UINTS );
        Memory::copy( words.data(), bytes.data(), words.size() * sizeof( uint32 ) );

        const GPUMessageRingRead ring = GPUMessageRingRead::read( words.data(), static_cast<uint32>( words.size() ), _processedCounter );
        _processedCounter             = ring._counter;
        uint64 dropped                = ring._dropped;

        vector<GPUMessageGroup> listGroup;
        for ( const GPUMessageRecord& record : ring._listRecord )
        {
            GPUMessageGroup* pGroup = nullptr;
            for ( GPUMessageGroup& group : listGroup )
            {
                if ( group._first._id == record._id )
                {
                    pGroup = &group;
                    break;
                }
            }
            if ( pGroup == nullptr )
            {
                if ( listGroup.size() >= GPUMessageInternal::kMaxDistinctPerPoll )
                {
                    ++dropped;
                    continue;
                }
                listGroup.push_back( GPUMessageGroup{ record, 0 } );
                pGroup = &listGroup.back();
            }
            ++pGroup->_count;
        }

        for ( const GPUMessageGroup& group : listGroup )
        {
            _reportedCount += group._count;
            const GPUMessageFormat* pFormat = GPUMessageFormat::find( group._first._id );
            if ( pFormat == nullptr )
            {
                SW_LOG_ERROR_ID( "GPUMessage.UnknownID", "GPU message id %# (x%#, frame %#) is not in the format table - the shader and SW_GPU_MESSAGE_FORMAT disagree", group._first._id, group._count,
                                 frame );
                continue;
            }
            utf8 arrText[constant::buffer::kMax256];
            pFormat->formatArgs( group._first, arrText, sizeof( arrText ) );
            utf8 arrWhere[constant::buffer::kMax64];
            if ( group._first.isPixel() )
                formatstring( arrWhere, sizeof( arrWhere ), "pixel %#,%#", group._first._x, group._first._y );
            else if ( group._first._x != SW_GPU_MESSAGE_NO_COORD )
                formatstring( arrWhere, sizeof( arrWhere ), "thread %#", group._first._x );
            else
                formatstring( arrWhere, sizeof( arrWhere ), "no thread" );
            switch ( pFormat->_severity )
            {
                case GPUMessageSeverity::Info:
                {
                    SW_LOG_INFO_ID( "GPUMessage.Print", "GPU message %# in %# (x%#, frame %#, first at %#): %#", group._first._id, pFormat->_pSource, group._count, frame, arrWhere, arrText );
                    break;
                }
                case GPUMessageSeverity::Warning:
                {
                    SW_LOG_WARNING_ID( "GPUMessage.Warning", "GPU assert %# in %# (x%#, frame %#, first at %#): %#", group._first._id, pFormat->_pSource, group._count, frame, arrWhere, arrText );
                    break;
                }
                case GPUMessageSeverity::Error:
                {
                    SW_LOG_ERROR_ID( "GPUMessage.Error", "GPU assert %# in %# (x%#, frame %#, first at %#): %#", group._first._id, pFormat->_pSource, group._count, frame, arrWhere, arrText );
                    break;
                }
            }
        }
        if ( dropped > 0 )
        {
            _droppedCount += dropped;
            SW_LOG_WARNING_ID( "GPUMessage.RingDropped", "GPU message ring dropped %# records (frame %#) - the ring holds %# records between reads", dropped, frame,
                               static_cast<uint32>( SW_GPU_MESSAGE_RING_CAPACITY ) );
        }
    }
} // namespace sw
