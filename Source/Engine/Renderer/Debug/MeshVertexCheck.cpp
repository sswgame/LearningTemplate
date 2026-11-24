#include "pch.h"

#include "Engine/Renderer/Debug/MeshVertexCheck.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshRenderData.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/RHIMeshVertexLayout.h"
#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"
#include "Engine/Renderer/Debug/RenderDebugRegistry.h"
#include "Engine/Renderer/Scene/GPUScene.h"

namespace sw
{
    namespace
    {
        struct MeshVertexCheckInternal
        {
            /// 한 번에 견주는 CPU 정점의 상한입니다. 넘는 메시는 견주지 않고 수를 로그에 적는다 — 시나리오 무대의 메시는 모두 든다.
            static constexpr uint32 kMaxExpectedBytes = 16u * 1024u * 1024u;
            static constexpr uint32 kWordBytes        = static_cast<uint32>( sizeof( uint32 ) );

            /** @brief @p byteOffset 이 든 4 바이트 정렬 단어를 읽습니다(로그용). */
            static uint32 readWord( const uint8* pByte, uint32 byteOffset )
            {
                uint32 word = 0;
                Memory::copy( &word, pByte + byteOffset / kWordBytes * kWordBytes, sizeof( word ) );
                return word;
            }

            /** @brief 사본 하나의 차이를 로그로 남기고 다른 바이트 수를 돌려줍니다(같으면 0). */
            static uint32 reportCopy( const string& name, const utf8* pWhich, RHIMeshVertexFormat format, uint32 vertexCount, const uint8* pExpected, const uint8* pActual )
            {
                const optional<MeshVertexCheck::Difference> difference = MeshVertexCheck::findDifference( format, vertexCount, pExpected, pActual );
                if ( difference.has_value() == false )
                    return 0;
                SW_LOG_ERROR_ID( "MeshVertexCheck.Mismatch",
                                 "Mesh '%#' (%# vertices): the %# differs from the CPU vertices - %# stream, vertex %#, byte %# of the copy (%# bytes differ): expected word 0x%#, got 0x%#", name,
                                 vertexCount, pWhich, difference->_bAttribute != SW_FALSE ? "attribute" : "position", difference->_vertexIndex, difference->_byteOffset,
                                 difference->_differingByteCount, Fmt( readWord( pExpected, difference->_byteOffset ), Format( 8, Format::Padding::Zero ).hexUpper() ),
                                 Fmt( readWord( pActual, difference->_byteOffset ), Format( 8, Format::Padding::Zero ).hexUpper() ) );
                return difference->_differingByteCount;
            }

            /** @brief CPU 정점의 첫 영 길이 노멀을 로그로 남깁니다 — 정점 셰이더에서 NaN 이 되는 값인데, GPU 사본이 같으면 바이트 대조로는 드러나지 않는다. */
            static void reportZeroNormal( const string& name, const vector<RHIVertex>& listVertex )
            {
                for ( size_t vertexIndex = 0; vertexIndex < listVertex.size(); ++vertexIndex )
                {
                    const float32( &arrNormal )[3] = listVertex[vertexIndex]._arrNormal;
                    if ( arrNormal[0] != 0.0f || arrNormal[1] != 0.0f || arrNormal[2] != 0.0f )
                        continue;
                    SW_LOG_INFO_ID( "MeshVertexCheck.ZeroNormal", "Mesh '%#' (%# vertices): CPU vertex %# has a zero normal", name, static_cast<uint32>( listVertex.size() ), static_cast<uint32>( vertexIndex ) );
                    return;
                }
            }
        };
    } // namespace

    optional<MeshVertexCheck::Difference> MeshVertexCheck::findDifference( RHIMeshVertexFormat format, uint32 vertexCount, const uint8* pExpected, const uint8* pActual )
    {
        const uint32         byteCount      = RHIMeshVertexStreams::computeBufferBytes( format, vertexCount );
        const uint32         attributeStart = RHIMeshVertexStreams::computeAttributeOffset( vertexCount );
        optional<Difference> result;
        for ( uint32 byteOffset = 0; byteOffset < byteCount; ++byteOffset )
        {
            if ( pExpected[byteOffset] == pActual[byteOffset] )
                continue;
            if ( result.has_value() == false )
            {
                const bool bAttribute = byteOffset >= attributeStart;
                Difference first{};
                first._byteOffset  = byteOffset;
                first._vertexIndex = bAttribute ? ( byteOffset - attributeStart ) / constant::render::getMeshAttributeStride( format ) : byteOffset / constant::render::kMeshPositionStride;
                first._bAttribute  = bAttribute ? SW_TRUE : SW_FALSE;
                result             = first;
            }
            ++result->_differingByteCount;
        }
        return result;
    }

    void MeshVertexCheck::collectMeshes( const GPUScene& scene )
    {
        using Internal = MeshVertexCheckInternal;
        _listMesh.clear();
        _listExpected.clear();
        const vector<GPUMeshBatch>&        listBatch   = scene.getAllBatches();
        const vector<GPUMeshBatchBinding>& listBinding = scene.getBatchBindings();
        vector<const Mesh*>                listSeen;
        uint32                             skippedCount = 0;
        for ( size_t batchIndex = 0; batchIndex < listBatch.size(); ++batchIndex )
        {
            const Mesh* const pMesh       = listBatch[batchIndex]._mesh.get();
            const bool        bComparable = pMesh != nullptr && pMesh->getCPUAccess() == MeshCPUAccess::Keep && pMesh->getRenderData().isRHIValid() && listBinding[batchIndex]._vertexBuffer != 0 &&
                                     VectorUtil::contains( listSeen, pMesh ) == false;
            if ( bComparable == false )
                continue;
            listSeen.push_back( pMesh );
            const MeshCPUView        cpu         = pMesh->getCPUView();
            const vector<RHIVertex>& listVertex  = cpu.getVertices();
            const vector<RHIVertex>& listLOD     = cpu.getLODVertices();
            const uint32             lod0Count   = static_cast<uint32>( listVertex.size() );
            const uint32             vertexCount = pMesh->getDrawVertexCount();
            CheckedMesh              checked{};
            checked._name           = pMesh->getSourceAsset().str();
            checked._format         = pMesh->getVertexFormat();
            checked._vertexCount    = vertexCount;
            checked._expectedOffset = static_cast<uint32>( _listExpected.size() );
            checked._byteCount      = RHIMeshVertexStreams::computeBufferBytes( checked._format, vertexCount );
            checked._batchIndex     = static_cast<uint32>( batchIndex );
            if ( checked._byteCount > Internal::kMaxExpectedBytes - checked._expectedOffset )
            {
                ++skippedCount;
                continue;
            }
            _listExpected.resize( _listExpected.size() + checked._byteCount );
            uint8* const pExpected = _listExpected.data() + checked._expectedOffset;
            RHIMeshVertexStreams::write( checked._format, listVertex.data(), lod0Count, 0, vertexCount, pExpected );
            RHIMeshVertexStreams::write( checked._format, listLOD.data(), vertexCount - lod0Count, lod0Count, vertexCount, pExpected );
            Internal::reportZeroNormal( checked._name, listVertex );
            _listMesh.push_back( std::move( checked ) );
        }
        if ( skippedCount > 0 )
            SW_LOG_INFO_ID( "MeshVertexCheck.Skipped", "Mesh vertex check: %# meshes did not fit in %# bytes and are not compared", skippedCount, Internal::kMaxExpectedBytes );
    }

    void MeshVertexCheck::recordCopies( IRHICommandList& cmd, IRHIDevice& device, const GPUScene& scene )
    {
        using Internal         = MeshVertexCheckInternal;
        const uint32 wordCount = static_cast<uint32>( _listExpected.size() ) * 2u / Internal::kWordBytes;
        const bool   bReady    = wordCount > 0 &&
                            _probeBuffer.ensureCapacity( &device, Internal::kWordBytes, wordCount, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, nullptr ) &&
                            _readback.initialize( &device, Internal::kWordBytes, wordCount );
        if ( bReady == false )
            return;
        for ( const CheckedMesh& checked : _listMesh )
        {
            const GPUMeshBatchBinding& binding         = scene.getBatchBindings()[checked._batchIndex];
            const RHIBufferHandle      proxy           = scene.getAllBatches()[checked._batchIndex]._mesh->getRenderData().getVertexBuffer();
            const uint32               attributeStride = constant::render::getMeshAttributeStride( checked._format );
            const uint64               positionBytes   = static_cast<uint64>( checked._vertexCount ) * constant::render::kMeshPositionStride;
            const uint64               attributeBytes  = static_cast<uint64>( checked._vertexCount ) * attributeStride;
            const uint64               proxyCopy       = static_cast<uint64>( checked._expectedOffset ) * 2u;
            const uint64               drawnCopy       = proxyCopy + checked._byteCount;
            // 정점 풀의 복사와 같은 전이다 — 원본은 CopySource 로 옮겼다가 그리기가 읽는 상태로 되돌린다. 풀 밖 메시는 두 원본이 같은 버퍼다.
            const RHIBufferTransition arrBefore[3] = {
                RHIBufferTransition{ _probeBuffer._buffer,   RHIBufferState::CopyDest},
                RHIBufferTransition{                proxy, RHIBufferState::CopySource},
                RHIBufferTransition{binding._vertexBuffer, RHIBufferState::CopySource}
            };
            cmd.transitionBuffers( arrBefore, 3 );
            cmd.copyBufferRegion( _probeBuffer._buffer, proxyCopy, proxy, 0, checked._byteCount );
            cmd.copyBufferRegion( _probeBuffer._buffer, drawnCopy, binding._vertexBuffer, static_cast<uint64>( binding._baseVertex ) * constant::render::kMeshPositionStride, positionBytes );
            cmd.copyBufferRegion( _probeBuffer._buffer, drawnCopy + positionBytes, binding._vertexBuffer, binding._attributeOffset + static_cast<uint64>( binding._baseVertex ) * attributeStride,
                                  attributeBytes );
            const RHIBufferTransition arrAfter[2] = {
                RHIBufferTransition{                proxy, RHIBufferState::VertexOrConstant},
                RHIBufferTransition{binding._vertexBuffer, RHIBufferState::VertexOrConstant}
            };
            cmd.transitionBuffers( arrAfter, 2 );
        }
        _bPending = _readback.request( cmd, _probeBuffer._buffer, RHIBufferState::CopyDest, ++_tag ) ? SW_TRUE : SW_FALSE;
    }

    void MeshVertexCheck::report( const vector<uint8>& listByte ) const
    {
        using Internal            = MeshVertexCheckInternal;
        uint32 differingMeshCount = 0;
        if ( listByte.size() < _listExpected.size() * 2u )
            return;
        for ( const CheckedMesh& checked : _listMesh )
        {
            const uint8* const pExpected  = _listExpected.data() + checked._expectedOffset;
            const uint8* const pProxy     = listByte.data() + static_cast<size_t>( checked._expectedOffset ) * 2u;
            const uint32       proxyBytes = Internal::reportCopy( checked._name, "render proxy", checked._format, checked._vertexCount, pExpected, pProxy );
            const uint32       drawnBytes = Internal::reportCopy( checked._name, "drawn vertex buffer", checked._format, checked._vertexCount, pExpected, pProxy + checked._byteCount );
            differingMeshCount += proxyBytes + drawnBytes > 0 ? 1u : 0u;
        }
        SW_LOG_INFO_ID( "MeshVertexCheck.Done", "Mesh vertex check: %# of %# meshes differ from their CPU vertices", differingMeshCount, static_cast<uint32>( _listMesh.size() ) );
    }

    void MeshVertexCheck::service( IRHICommandList& cmd, IRHIDevice& device, const GPUScene& scene, RenderDebugRegistry& registry )
    {
        if ( _bPending != SW_FALSE )
        {
            vector<uint8> listByte;
            uint64        tag = 0;
            if ( _readback.takeLatest( listByte, tag ) == false || tag != _tag )
                return; // 한 번에 하나만 되읽는다 — 새 요청은 우편함에 남아 다음 차례를 기다린다
            report( listByte );
            release( &device );
        }
        if ( registry.takeMeshVertexCheckRequest() == false )
            return;
        collectMeshes( scene );
        recordCopies( cmd, device, scene );
    }

    void MeshVertexCheck::release( IRHIDevice* pDevice )
    {
        _readback.shutdown();
        _probeBuffer.release( pDevice );
        _bPending = SW_FALSE;
    }
} // namespace sw
