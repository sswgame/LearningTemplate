#include "pch.h"

#include "Engine/Renderer/VFX/VFXGPUMeshReadback.h"

#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

#include "vfx_simulate.hlsli"

#include <cstddef>

namespace sw
{
    namespace
    {
        struct VFXGPUMeshReadbackInternal
        {
            /** @brief 방출기 하나의 단어 수입니다 — (살아 있는 수, 그린 수). */
            static constexpr uint32 kWordsPerEmitter = 2;
            static constexpr uint32 kWordBytes       = static_cast<uint32>( sizeof( uint32 ) );
            /** @brief 간접 인자 원소 안 인스턴스 수의 바이트 위치입니다. */
            static constexpr uint32 kInstanceCountOffset = static_cast<uint32>( offsetof( RHIDrawIndexedIndirectCommand, _instanceCount ) );
        };
    } // namespace
} // namespace sw

namespace sw
{
    VFXGPUMeshReadback::VFXGPUMeshReadback()
        : _probeBuffer{}
        , _readback{}
        , _arrMeta{}
        , _readbackSerial{ 0 }
        , _minAcceptedSerial{ 0 }
        , _emitterCount{ 0 }
        , _packedCounts{ 0 }
        , _bHasValue{ 0 }
    {
    }

    VFXGPUMeshReadback::~VFXGPUMeshReadback() = default;

    void VFXGPUMeshReadback::requestReadback( IRHIDevice& device, IRHICommandList& cmd, const vector<VFXGPUSimulation::MeshProbeSource>& listSource, const vector<GPUInstance>& listInstance,
                                              RHIBufferHandle mainIndirectArgs, bool bGPUCullingActive )
    {
        using Internal = VFXGPUMeshReadbackInternal;
        // 컬링이 돌지 않은 프레임의 인자는 CPU 가 채운 배치 전체 수다 — 그린 수가 아니라 되읽지 않는다(감시 값은 RHI.GPUCulling 으로 건너뛴다).
        if ( bGPUCullingActive == false || mainIndirectArgs == 0 )
            return;
        if ( listSource.empty() )
        {
            _minAcceptedSerial = _readbackSerial + 1;
            publish( 0, 0, 0 );
            return;
        }
        constexpr uint32 kWordCount = kMaxEmitters * Internal::kWordsPerEmitter;
        if ( _probeBuffer.isValid() == false )
        {
            const array<uint32, kWordCount> arrZero{};
            if ( _probeBuffer.ensureCapacity( &device, Internal::kWordBytes, kWordCount, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, arrZero.data() ) == false )
                return; // 이유는 ensureCapacity 가 남겼다
        }
        if ( _readback.isInitialized() == false && _readback.initialize( &device, Internal::kWordBytes, kWordCount ) == false )
            return;

        const uint64  next = _readbackSerial + 1;
        ReadbackMeta& meta = _arrMeta[next % kReadbackMetaCount];
        meta._emitterCount = 0;
        vector<RHIBufferTransition> listBefore;
        vector<RHIBufferTransition> listAfter;
        listBefore.push_back( RHIBufferTransition{ _probeBuffer._buffer, RHIBufferState::CopyDest } );
        listBefore.push_back( RHIBufferTransition{ mainIndirectArgs, RHIBufferState::CopySource } );
        listAfter.push_back( RHIBufferTransition{ mainIndirectArgs, RHIBufferState::IndirectArgument } ); // 그리기가 읽던 상태로 되돌린다
        for ( const VFXGPUSimulation::MeshProbeSource& source : listSource )
        {
            const bool bKnownSlot = source._instanceSlot < listInstance.size();
            if ( meta._emitterCount >= kMaxEmitters || bKnownSlot == false )
                continue;
            meta._arrBatch[meta._emitterCount] = listInstance[source._instanceSlot]._meshBatchIndex;
            ++meta._emitterCount;
            listBefore.push_back( RHIBufferTransition{ source._argsBuffer, RHIBufferState::CopySource } );
            listAfter.push_back( RHIBufferTransition{ source._argsBuffer, RHIBufferState::IndirectArgument } ); // 프레임 그래프 표의 등록 접근(프레임 시작이자 끝 — `VFXGPUSimulation::registerFrameResources`)
        }
        if ( meta._emitterCount == 0 )
            return;
        cmd.transitionBuffers( listBefore.data(), static_cast<uint32>( listBefore.size() ) );
        uint32 emitterIndex = 0;
        for ( const VFXGPUSimulation::MeshProbeSource& source : listSource )
        {
            if ( emitterIndex >= meta._emitterCount || source._instanceSlot >= listInstance.size() )
                continue;
            const uint64 wordOffset = static_cast<uint64>( emitterIndex ) * Internal::kWordsPerEmitter * Internal::kWordBytes;
            const uint64 batchArgs  = static_cast<uint64>( meta._arrBatch[emitterIndex] ) * sizeof( RHIDrawIndexedIndirectCommand );
            cmd.copyBufferRegion( _probeBuffer._buffer, wordOffset, source._argsBuffer, static_cast<uint64>( SW_VFX_ARGS_WORD_ALIVE ) * Internal::kWordBytes, Internal::kWordBytes );
            cmd.copyBufferRegion( _probeBuffer._buffer, wordOffset + Internal::kWordBytes, mainIndirectArgs, batchArgs + Internal::kInstanceCountOffset, Internal::kWordBytes );
            ++emitterIndex;
        }
        cmd.transitionBuffers( listAfter.data(), static_cast<uint32>( listAfter.size() ) );
        if ( _readback.request( cmd, _probeBuffer._buffer, RHIBufferState::CopyDest, next ) )
            _readbackSerial = next;
    }

    void VFXGPUMeshReadback::poll()
    {
        using Internal = VFXGPUMeshReadbackInternal;
        vector<uint8> bytes;
        uint64        serial = 0;
        if ( _readback.takeLatest( bytes, serial ) == false || serial < _minAcceptedSerial )
            return;
        const ReadbackMeta& meta     = _arrMeta[serial % kReadbackMetaCount];
        const uint32        needWord = meta._emitterCount * Internal::kWordsPerEmitter;
        if ( bytes.size() < static_cast<size_t>( needWord ) * Internal::kWordBytes )
            return;
        vector<uint32> listWord( needWord );
        Memory::copy( listWord.data(), bytes.data(), listWord.size() * sizeof( uint32 ) );
        uint32 aliveCount = 0;
        uint32 drawnCount = 0;
        for ( uint32 emitterIndex = 0; emitterIndex < meta._emitterCount; ++emitterIndex )
        {
            aliveCount += listWord[emitterIndex * Internal::kWordsPerEmitter];
            // 같은 배치를 앞 방출기가 이미 셌으면 넘긴다 — 배치의 수는 그 배치 전체다.
            bool bCounted = false;
            for ( uint32 earlier = 0; earlier < emitterIndex; ++earlier )
            {
                bCounted = bCounted || meta._arrBatch[earlier] == meta._arrBatch[emitterIndex];
            }
            if ( bCounted == false )
                drawnCount += listWord[emitterIndex * Internal::kWordsPerEmitter + 1];
        }
        publish( meta._emitterCount, aliveCount, drawnCount );
    }

    void VFXGPUMeshReadback::publish( uint32 emitterCount, uint32 aliveCount, uint32 drawnCount )
    {
        _emitterCount.store( emitterCount );
        _packedCounts.store( static_cast<uint64>( aliveCount ) | ( static_cast<uint64>( drawnCount ) << 32 ) );
        _bHasValue.store( 1 );
    }

    void VFXGPUMeshReadback::release( IRHIDevice* pDevice )
    {
        _readback.shutdown();
        _probeBuffer.release( pDevice );
    }
} // namespace sw
