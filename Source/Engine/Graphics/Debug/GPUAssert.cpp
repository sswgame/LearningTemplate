#include "pch.h"

#include "Engine/Graphics/Debug/GPUAssert.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/BitUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/Diagnostics/GPUDiagnosticsBuild.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    SW_LOG_CALLER( "GPUAssert" );

    /**
     * @brief `-gv_gpuAssert=1`: assert 를 가진 컴퓨트 셰이더(gpu_cull · canvas_marker · mesh_morph)를 `SW_GPU_ASSERT_ENABLED` 변형으로 컴파일해 조건이 틀린 스레드가 링 버퍼에 기록을 남기고, CPU 가 몇 프레임 뒤 로그로 낸다.
     * @details 기본 끔 — 켜면 그 패스의 셰이더 변형이 하나 더 컴파일되고 u4 에 버퍼가 걸린다(Shipping 은 등록되지 않아 늘 끔).
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_gpuAssert, false,
                             "셰이더 assert(SW_GPU_ASSERT) 를 켠다 - assert 를 가진 컴퓨트 셰이더가 UAV 링에 기록하고 CPU 가 몇 프레임 뒤 로그로 낸다 (개발 빌드만)" );

    namespace
    {
        struct GPUAssertInternal
        {
            /// 표의 모든 항목입니다. 번호는 gpu_assert_ids.hlsli 의 SW_GPU_ASSERT_ID_* 이다.
            static constexpr GPUAssertInfo kArrInfo[] = {
                {         SW_GPU_ASSERT_ID_CULL_LOD_LEVEL,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,                         "LOD level is not below the group level count",       "gpu_cull.hlsl"},
                {      SW_GPU_ASSERT_ID_CULL_LOD_PREVIOUS,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,                "previous LOD level is not below the group level count",       "gpu_cull.hlsl"},
                {    SW_GPU_ASSERT_ID_CULL_LOD_LEVEL_BITS,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,            "LOD level does not fit the 3 bit field of a visible entry",       "gpu_cull.hlsl"},
                {        SW_GPU_ASSERT_ID_CULL_FADE_LEVEL,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,          "crossfade progress does not fit the 4 bit field of an entry",       "gpu_cull.hlsl"},
                {       SW_GPU_ASSERT_ID_CULL_BATCH_RANGE,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,                 "visible entry batch index is outside the batch table",       "gpu_cull.hlsl"},
                {     SW_GPU_ASSERT_ID_CULL_VISIBLE_RANGE,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint, "visible list write slot is outside the visible range (entry dropped)",       "gpu_cull.hlsl"},
                {       SW_GPU_ASSERT_ID_CULL_INSTANCE_ID,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,         "instance id does not fit the 24 bit field of a visible entry",       "gpu_cull.hlsl"},
                {        SW_GPU_ASSERT_ID_MARKER_CLIP_NAN, GPUAssertSeverity::Warning, GPUAssertValueKind::Float,           "marker projection produced a NaN or infinite clip position",  "canvas_marker.hlsl"},
                {  SW_GPU_ASSERT_ID_MARKER_INSTANCE_RANGE,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,         "marker instance slot is outside the GPUScene instance buffer",  "canvas_marker.hlsl"},
                {       SW_GPU_ASSERT_ID_MARKER_ROW_RANGE,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,                           "marker input row is outside the row buffer",  "canvas_marker.hlsl"},
                {       SW_GPU_ASSERT_ID_MARKER_WORLD_NAN, GPUAssertSeverity::Warning, GPUAssertValueKind::Float,                                "marker world point is NaN or infinite",  "canvas_marker.hlsl"},
                {     SW_GPU_ASSERT_ID_MORPH_VERTEX_RANGE,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,                       "rest vertex element is outside the rest buffer",     "mesh_morph.hlsl"},
                {     SW_GPU_ASSERT_ID_MORPH_PROGRAM_SLOT,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,                     "deform program slot is outside the program table",     "mesh_morph.hlsl"},
                { SW_GPU_ASSERT_ID_MODIFIER_PROGRAM_RANGE,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,                  "modifier program start is outside the program table", "mesh_modifier.hlsli"},
                {  SW_GPU_ASSERT_ID_MODIFIER_RECORD_RANGE,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,                         "modifier record is outside the program table", "mesh_modifier.hlsli"},
                {     SW_GPU_ASSERT_ID_MODIFIER_RAW_RANGE,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,                          "raw program table read is outside the table", "mesh_modifier.hlsli"},
                {    SW_GPU_ASSERT_ID_MODIFIER_NODE_RANGE,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,                   "conform BVH node index is not below the node count", "mesh_modifier.hlsli"},
                {SW_GPU_ASSERT_ID_MODIFIER_TRIANGLE_RANGE,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,              "conform BVH leaf triangle is outside the triangle table", "mesh_modifier.hlsli"},
                {SW_GPU_ASSERT_ID_MODIFIER_STACK_OVERFLOW,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,                           "conform BVH traversal stack would overflow", "mesh_modifier.hlsli"},
                {SW_GPU_ASSERT_ID_MODIFIER_UV_TABLE_RANGE,   GPUAssertSeverity::Error,  GPUAssertValueKind::Uint,           "modifier vertex UV table read is outside the program table", "mesh_modifier.hlsli"},
            };

            /// 한 번 읽을 때 구별해 모으는 id 수의 상한입니다(표에 없는 id 도 따로 센다).
            static constexpr uint32 kMaxDistinctPerPoll = 64;
        };

        /// @brief 한 번의 읽기에서 조건 id 하나로 묶은 기록입니다.
        struct GPUAssertGroup
        {
            uint32 _id;
            uint32 _count;
            uint32 _firstValueBits;
            uint32 _firstThread;
        };
    } // namespace

    const GPUAssertInfo* findGPUAssertInfo( uint32 conditionID )
    {
        for ( const GPUAssertInfo& info : GPUAssertInternal::kArrInfo )
        {
            if ( info._id == conditionID )
                return &info;
        }
        return nullptr;
    }

    uint32 getGPUAssertInfoCount()
    {
        return static_cast<uint32>( sizeof( GPUAssertInternal::kArrInfo ) / sizeof( GPUAssertInternal::kArrInfo[0] ) );
    }

    GPUAssertMonitor::GPUAssertMonitor()
        : _pDevice{ nullptr }
        , _buffer{}
        , _readback{}
        , _frameSerial{ 0 }
        , _processedCount{ 0 }
        , _reportedCount{ 0 }
        , _droppedCount{ 0 }
    {
    }

    GPUAssertMonitor::~GPUAssertMonitor()
    {
        shutdown();
    }

    bool GPUAssertMonitor::isRequested()
    {
        return GPUDiagnosticsBuild::isAvailable() && static_cast<bool>( gv_gpuAssert ); // 배포본은 define 도 PSO 변형도 바인딩도 만들지 않는다
    }

    bool GPUAssertMonitor::initialize( IRHIDevice* pDevice )
    {
        if ( isRequested() == false )
            return true;
        if ( isActive() )
            return true;
        if ( pDevice == nullptr )
        {
            SW_LOG_ERROR( "GPU assert is requested (gv_gpuAssert) but there is no device" );
            return false;
        }
        if ( pDevice->getCapabilities()._bCompute == SW_FALSE )
        {
            SW_LOG_ERROR( "GPU assert is requested (gv_gpuAssert) but this backend has no compute support (the UAV ring needs compute)" );
            return false;
        }

        // 0 으로 채워 만든다 — 순번 + 1 이 0 인 칸이 "빈 칸" 이고 Vulkan 은 새 버퍼를 지우지 않는다.
        const vector<uint32> zeroed( SW_GPU_ASSERT_BUFFER_UINTS, 0u );
        const bool           bCreated = _buffer.ensureCapacity( pDevice, sizeof( uint32 ), SW_GPU_ASSERT_BUFFER_UINTS,
                                                                RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess, true, true, zeroed.data() );
        if ( bCreated == false )
        {
            SW_LOG_ERROR( "Failed to create the GPU assert ring buffer (%# uints) - gv_gpuAssert cannot run", static_cast<uint32>( SW_GPU_ASSERT_BUFFER_UINTS ) );
            _buffer.release( pDevice );
            return false;
        }
        if ( _readback.initialize( pDevice, sizeof( uint32 ), SW_GPU_ASSERT_BUFFER_UINTS ) == false )
        {
            SW_LOG_ERROR( "Failed to create the GPU assert readback buffers - gv_gpuAssert cannot run" );
            _buffer.release( pDevice );
            return false;
        }
        _pDevice        = pDevice;
        _frameSerial    = 0;
        _processedCount = 0;
        SW_LOG_INFO( "GPU assert ring ready (%# records, read back a few frames late)", static_cast<uint32>( SW_GPU_ASSERT_RING_CAPACITY ) );
        return true;
    }

    void GPUAssertMonitor::shutdown()
    {
        _readback.shutdown();
        if ( _pDevice != nullptr )
            _buffer.release( _pDevice );
        _pDevice = nullptr;
    }

    void GPUAssertMonitor::bind( IRHICommandList& commands )
    {
        if ( isActive() == false )
            return;
        commands.transitionBuffer( _buffer._buffer, RHIBufferState::UnorderedAccess );
        commands.bindComputeUAV( _buffer._uav, SW_SLOT_COMPUTE_ASSERT_UAV );
    }

    void GPUAssertMonitor::requestReadback( IRHICommandList& commands )
    {
        if ( isActive() == false )
            return;
        const uint64 next = _frameSerial + 1;
        // 읽기 버퍼가 모두 GPU 가 쓰는 중이면 이 프레임의 사본은 건너뛴다 — 링은 지우지 않는 누적 카운터라 다음 사본이 이번 기록까지 담는다.
        if ( _readback.request( commands, _buffer._buffer, RHIBufferState::UnorderedAccess, next ) )
            _frameSerial = next;
    }

    void GPUAssertMonitor::poll()
    {
        if ( isActive() == false )
            return;
        vector<uint8> bytes;
        uint64        frame = 0;
        if ( _readback.takeLatest( bytes, frame ) == false )
            return;
        if ( bytes.size() < static_cast<size_t>( SW_GPU_ASSERT_BUFFER_UINTS ) * sizeof( uint32 ) )
        {
            SW_LOG_ERROR_ONCE( "GPU assert readback is smaller than the ring (%# bytes)", static_cast<uint32>( bytes.size() ) );
            return;
        }
        vector<uint32> words( SW_GPU_ASSERT_BUFFER_UINTS );
        Memory::copy( words.data(), bytes.data(), words.size() * sizeof( uint32 ) );

        // 누적 카운터는 32 비트로 돈다 — 지난번과의 차를 부호 없는 뺄셈으로 구해 한 바퀴 돌아도 맞는다.
        const uint32 counter  = words[0];
        const uint32 newCount = counter - static_cast<uint32>( _processedCount );
        if ( newCount == 0 )
            return;
        const uint32 readable = newCount < SW_GPU_ASSERT_RING_CAPACITY ? newCount : static_cast<uint32>( SW_GPU_ASSERT_RING_CAPACITY );
        uint64       dropped  = newCount - readable;

        vector<GPUAssertGroup> listGroup;
        const uint32           firstSequence = counter - readable;
        for ( uint32 offset = 0; offset < readable; ++offset )
        {
            const uint32  sequence = firstSequence + offset;
            const uint32* pRecord  = &words[SW_GPU_ASSERT_HEADER_UINTS + ( sequence % SW_GPU_ASSERT_RING_CAPACITY ) * SW_GPU_ASSERT_RECORD_UINTS];
            // 순번 + 1 이 맞는 칸만 믿는다 — 쓰다 만 칸이나 더 뒤의 기록이 덮은 칸은 버려진 것으로 센다.
            if ( pRecord[3] != sequence + 1u )
            {
                ++dropped;
                continue;
            }
            GPUAssertGroup* pGroup = nullptr;
            for ( GPUAssertGroup& group : listGroup )
            {
                if ( group._id == pRecord[0] )
                {
                    pGroup = &group;
                    break;
                }
            }
            if ( pGroup == nullptr )
            {
                if ( listGroup.size() >= GPUAssertInternal::kMaxDistinctPerPoll )
                {
                    ++dropped;
                    continue;
                }
                listGroup.push_back( GPUAssertGroup{ pRecord[0], 0, pRecord[2], pRecord[1] } );
                pGroup = &listGroup.back();
            }
            ++pGroup->_count;
        }
        _processedCount = counter;

        for ( const GPUAssertGroup& group : listGroup )
        {
            const GPUAssertInfo* pInfo = findGPUAssertInfo( group._id );
            if ( pInfo == nullptr )
            {
                SW_LOG_ERROR( "GPU assert id %# (x%#, frame %#) is not in the assert table - the shader and GPUAssert.cpp disagree", group._id, group._count, frame );
                _reportedCount += group._count;
                continue;
            }
            const float32 floatValue = BitUtil::bitsToFloat( group._firstValueBits );
            const bool    bFloat     = pInfo->_valueKind == GPUAssertValueKind::Float;
            if ( pInfo->_severity == GPUAssertSeverity::Error )
            {
                if ( bFloat )
                    SW_LOG_ERROR( "GPU assert %# in %# (x%#, frame %#): %# - first value %#, thread %#", group._id, pInfo->_pPass, group._count, frame, pInfo->_pMessage, floatValue,
                                  group._firstThread );
                else
                    SW_LOG_ERROR( "GPU assert %# in %# (x%#, frame %#): %# - first value %#, thread %#", group._id, pInfo->_pPass, group._count, frame, pInfo->_pMessage,
                                  group._firstValueBits, group._firstThread );
            }
            else
            {
                if ( bFloat )
                    SW_LOG_WARNING( "GPU assert %# in %# (x%#, frame %#): %# - first value %#, thread %#", group._id, pInfo->_pPass, group._count, frame, pInfo->_pMessage, floatValue,
                                    group._firstThread );
                else
                    SW_LOG_WARNING( "GPU assert %# in %# (x%#, frame %#): %# - first value %#, thread %#", group._id, pInfo->_pPass, group._count, frame, pInfo->_pMessage,
                                    group._firstValueBits, group._firstThread );
            }
            _reportedCount += group._count;
        }
        if ( dropped > 0 )
        {
            _droppedCount += dropped;
            SW_LOG_WARNING( "GPU assert ring dropped %# records (frame %#) - the ring holds %# records between reads", dropped, frame, static_cast<uint32>( SW_GPU_ASSERT_RING_CAPACITY ) );
        }
    }
} // namespace sw
