#include "pch.h"

#include "Engine/Renderer/Lighting/RealtimeProbeUpdater.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshRenderData.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIMeshVertexLayout.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Lighting/SceneVoxelizer.h"

namespace sw
{
    namespace
    {
        struct RealtimeProbeUpdaterInternal
        {
            /// 황금각(라디안) — 하늘 투영의 방위각 간격입니다.
            static constexpr float32 kGoldenAngle = 2.39996323f;
            /// 발광 목록의 한 행(float4)에 든 RGB9E5 수입니다(probe_update.hlsl findEmission).
            static constexpr uint32 kPaletteEntriesPerRow = 4;
            /// 위치 하나의 실수 칸 수입니다(scene_voxelize.hlsl loadCorner).
            static constexpr uint32 kFloatsPerPosition = 3;
            static_assert( kFloatsPerPosition * sizeof( float32 ) == constant::render::kMeshPositionStride, "위치 버퍼는 정점 버퍼의 위치 흐름과 같은 모양이다" );

            static constexpr RHIBufferUsage kReadUsage  = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
            static constexpr RHIBufferUsage kWriteUsage = kReadUsage | RHIBufferUsage::UnorderedAccess;

            /// 정수 넷을 float4 의 비트로 싣습니다(셰이더는 asuint 로 읽는다).
            static float4 packBits( uint32 x, uint32 y, uint32 z, uint32 w )
            {
                return float4{ BitUtil::bitsToFloat( x ), BitUtil::bitsToFloat( y ), BitUtil::bitsToFloat( z ), BitUtil::bitsToFloat( w ) };
            }

            /// 부동소수로 실은 정수(원소의 칸 수 · 번호)를 되돌립니다.
            static uint32 toCount( float32 value ) { return static_cast<uint32>( MathUtil::max( value, 0.0f ) + 0.5f ); }
        };
    } // namespace

    bool RealtimeProbeHistoryAllocator::assign( const vector<RealtimeProbeRequest>& listRequest )
    {
        const size_t requestCount = listRequest.size();
        // 1) 빠졌거나 프로브 수가 바뀐 볼륨의 구간을 돌려받는다 — 서 있는 볼륨은 그대로다.
        _listActiveID.clear();
        for ( auto it = _mapSlot.begin(); it != _mapSlot.end(); )
        {
            const uint64 volumeID = it->first;
            const uint32 count    = it->second._count;
            const bool   bStays   = VectorUtil::containsIf( listRequest, [volumeID, count]( const RealtimeProbeRequest& request )
                { return request._volumeID == volumeID && request._probeCount * kFloat4PerProbe == count; } );
            if ( bStays )
            {
                ++it;
                continue;
            }
            if ( count != 0 )
                _span.free( it->second._offset, count );
            it = _mapSlot.erase( it ); // 마지막 원소가 이 자리로 온다 — 같은 자리를 다시 본다
        }
        // 2) 새로 든 볼륨에 구간을 준다. 모자라면 용량을 두 배로 — 버퍼를 다시 만들므로 모두 새로 시작한다.
        bool bGrew = false;
        _listOffset.resize( requestCount );
        _listFramesSinceReset.resize( requestCount );
        _listFresh.assign( requestCount, SW_FALSE );
        for ( size_t requestIndex = 0; requestIndex < requestCount; ++requestIndex )
        {
            const RealtimeProbeRequest& request = listRequest[requestIndex];
            const uint32                count   = request._probeCount * kFloat4PerProbe;
            auto                        it      = _mapSlot.find( request._volumeID );
            if ( it == _mapSlot.end() )
            {
                uint32 offset = count != 0 ? _span.allocate( count ) : 0u;
                if ( offset == SpanAllocator::kInvalidOffset )
                {
                    _span.grow( MathUtil::max( _span.getCapacity() * 2u, _span.getCapacity() + count ) );
                    offset = _span.allocate( count );
                    bGrew  = true;
                }
                it                       = _mapSlot.insert_or_assign( request._volumeID, HistorySlot{ offset, count, 0u } ).first;
                _listFresh[requestIndex] = SW_TRUE;
            }
            _listOffset[requestIndex] = it->second._offset;
        }
        if ( bGrew )
        {
            for ( auto& entry : _mapSlot )
            {
                entry.second._framesSinceReset = 0;
            }
            _listFresh.assign( requestCount, SW_TRUE );
        }
        for ( size_t requestIndex = 0; requestIndex < requestCount; ++requestIndex )
        {
            _listActiveID.push_back( listRequest[requestIndex]._volumeID );
            _listFramesSinceReset[requestIndex] = _mapSlot.find( listRequest[requestIndex]._volumeID )->second._framesSinceReset;
        }
        return bGrew;
    }

    void RealtimeProbeHistoryAllocator::advanceFrame()
    {
        for ( const uint64 volumeID : _listActiveID )
        {
            const auto it = _mapSlot.find( volumeID );
            if ( it != _mapSlot.end() )
                ++it->second._framesSinceReset;
        }
    }

    void RealtimeProbeHistoryAllocator::clear()
    {
        _mapSlot.clear();
        _span.reset( 0 );
        _listActiveID.clear();
        _listOffset.clear();
        _listFramesSinceReset.clear();
        _listFresh.clear();
    }

    RealtimeProbeLayout RealtimeProbeUpdater::planLayout( const vector<RealtimeProbeRequest>& listRequest, const RealtimeProbeHistoryAllocator& history )
    {
        using Internal = RealtimeProbeUpdaterInternal;
        RealtimeProbeLayout layout;
        layout._totalHistoryElements = history.getCapacity();
        uint32 paletteRow            = kHeaderRowCount + static_cast<uint32>( listRequest.size() ) * kVolumeRowCount; // 발광 목록은 볼륨 행들 뒤에 이어진다
        for ( uint32 requestIndex = 0; requestIndex < static_cast<uint32>( listRequest.size() ); ++requestIndex )
        {
            const RealtimeProbeRequest& request = listRequest[requestIndex];
            layout._listVoxelWordOffset.push_back( layout._totalVoxelWords );
            layout._listHistoryOffset.push_back( history.getOffset( requestIndex ) );
            layout._listFirstThread.push_back( layout._totalProbes );
            const SceneVoxelScene& scene = *request._pVoxelScene;
            layout._totalVoxelWords += scene._grid.getWordCount();
            // 발광 칸(순위)은 그 볼륨의 점유 단어 바로 뒤에 붙인다(같은 버퍼 t2). 발광하는 메시가 없으면 셰이더가 읽지 않는다.
            const bool bHasEmission = scene._listEmissionPalette.empty() == false;
            layout._listEmissionWordOffset.push_back( bHasEmission ? layout._totalVoxelWords : SceneVoxelizer::kNoEmission );
            layout._totalVoxelWords += bHasEmission ? scene._grid.getCellCount() : 0u;
            layout._listPaletteRow.push_back( paletteRow );
            paletteRow += ( static_cast<uint32>( scene._listEmissionPalette.size() ) + Internal::kPaletteEntriesPerRow - 1u ) / Internal::kPaletteEntriesPerRow;
            layout._totalProbes += request._probeCount;
        }
        return layout;
    }

    SH9Color RealtimeProbeUpdater::projectSky( const SkyGradient& sky, bool bHasSky )
    {
        SH9Color result{};
        if ( bHasSky == false )
            return result;
        using Internal            = RealtimeProbeUpdaterInternal;
        const float32 sampleCount = static_cast<float32>( kSkySampleCount );
        const float32 weight      = 4.0f * MathUtil::kPi / sampleCount;
        for ( uint32 sampleIndex = 0; sampleIndex < kSkySampleCount; ++sampleIndex )
        {
            const float32 height  = 1.0f - 2.0f * ( static_cast<float32>( sampleIndex ) + 0.5f ) / sampleCount;
            const float32 azimuth = static_cast<float32>( sampleIndex ) * Internal::kGoldenAngle;
            const float32 ring    = MathUtil::sqrt( MathUtil::max( 1.0f - height * height, 0.0f ) );
            const float3  direction{ ring * MathUtil::cos( azimuth ), ring * MathUtil::sin( azimuth ), height };
            result.addSample( direction, sky.evaluateRadiance( direction ), weight );
        }
        return result;
    }

    float32 RealtimeProbeUpdater::computeError( const vector<RealtimeProbeRequest>& listRequest, uint32 accumulatedFrames )
    {
        float32 error = 0.0f;
        for ( const RealtimeProbeRequest& request : listRequest )
        {
            const uint32 updateCount = accumulatedFrames / MathUtil::max( request._updatePeriod, 1u );
            error                    = MathUtil::max( error, MathUtil::pow( 1.0f - MathUtil::saturate( request._blend ), static_cast<float32>( updateCount ) ) );
        }
        return error;
    }

    vector<float4> RealtimeProbeUpdater::makeParamRows( const vector<RealtimeProbeRequest>& listRequest, const RealtimeProbeLayout& layout, const vector<uint32>& listWarmup,
                                                        const SH9Color& sky, uint32 frameIndex, uint32 lightCount )
    {
        using Internal = RealtimeProbeUpdaterInternal;
        vector<float4> rows;
        rows.reserve( kHeaderRowCount + listRequest.size() * kVolumeRowCount );
        rows.push_back( Internal::packBits( static_cast<uint32>( listRequest.size() ), frameIndex, lightCount, 0u ) );
        // 하늘 SH 의 원시 계수 27 개를 float4 일곱에(마지막 한 칸은 0).
        for ( uint32 row = 0; row < kSkyRowCount; ++row )
        {
            float32 arrValue[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            for ( uint32 component = 0; component < 4; ++component )
            {
                const uint32 coefficientIndex = row * 4u + component;
                arrValue[component]           = coefficientIndex < SH9Color::kCoefficientCount ? sky._arrCoefficient[coefficientIndex] : 0.0f;
            }
            rows.push_back( float4{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] } );
        }
        for ( uint32 index = 0; index < static_cast<uint32>( listRequest.size() ); ++index )
        {
            const RealtimeProbeRequest& request = listRequest[index];
            const GPULight&             element = request._volumeElement;
            const SceneVoxelGrid&       grid    = request._pVoxelScene->_grid;
            const uint32                warmup  = index < listWarmup.size() ? listWarmup[index] : 0u;
            rows.push_back( float4{ element._colorIntensity._x, element._colorIntensity._y, element._colorIntensity._z, request._bounceAlbedo } );
            rows.push_back( float4{ element._directionType._x, element._directionType._y, element._directionType._z, request._blend } );
            // 프로브 버퍼 안의 첫 float4 번호 = 첫 프로브 번호(원소의 params.w) × 프로브당 float4 수.
            rows.push_back( Internal::packBits( Internal::toCount( element._params._x ), Internal::toCount( element._params._y ), Internal::toCount( element._params._z ),
                                                Internal::toCount( element._params._w ) * SH9Color::kGPUFloat4Count ) );
            rows.push_back( float4{ grid._origin._x, grid._origin._y, grid._origin._z, grid._cellSize } );
            rows.push_back( Internal::packBits( grid._dimX, grid._dimY, grid._dimZ, layout._listVoxelWordOffset[index] ) );
            rows.push_back( Internal::packBits( layout._listHistoryOffset[index], layout._listFirstThread[index], request._probeCount, request._rayCount ) );
            rows.push_back( Internal::packBits( MathUtil::max( request._updatePeriod, 1u ), warmup, layout._listEmissionWordOffset[index], BitUtil::floatToBits( request._emissiveIntensity ) ) );
            // 프로브 버퍼 안의 가시성 모먼트 첫 float4(원소의 extra.x — `LightProbeElementBuilder::appendRealtimeVolume`)와 발광 목록의 첫 행.
            rows.push_back( Internal::packBits( Internal::toCount( element._extra._x ), layout._listPaletteRow[index], 0u, 0u ) );
        }
        // 발광 목록 — 볼륨마다 순위 1 부터 행마다 RGB9E5 넷(`planLayout` 과 같은 차례).
        for ( const RealtimeProbeRequest& request : listRequest )
        {
            const vector<uint32>& palette = request._pVoxelScene->_listEmissionPalette;
            for ( size_t first = 0; first < palette.size(); first += Internal::kPaletteEntriesPerRow )
            {
                uint32 arrEntry[Internal::kPaletteEntriesPerRow]{};
                for ( size_t entry = 0; entry < Internal::kPaletteEntriesPerRow && first + entry < palette.size(); ++entry )
                {
                    arrEntry[entry] = palette[first + entry];
                }
                rows.push_back( Internal::packBits( arrEntry[0], arrEntry[1], arrEntry[2], arrEntry[3] ) );
            }
        }
        return rows;
    }

    void RealtimeProbeUpdater::prepare( IRHIDevice* pDevice, const vector<RealtimeProbeRequest>& listRequest, const GPULightBuffer& lightBuffer, const SkyGradient& sky, bool bHasSky,
                                        bool bPipelineReady, bool bCreateMissingMeshes )
    {
        _bActive = SW_FALSE;
        if ( pDevice == nullptr || listRequest.empty() )
            return;
        // 못 켜는 이유는 한 번만 알린다. 실시간 볼륨은 검게 남는다 - 베이크로 대신하지 않는다.
        const bool bProbeWritable = lightBuffer.getProbeUAV() != kInvalidDescriptorIndex;
        if ( bPipelineReady == false || bProbeWritable == false )
        {
            if ( _bReportedUnavailable == SW_FALSE )
            {
                _bReportedUnavailable = SW_TRUE;
                SW_LOG_ERROR_ID( "RealtimeProbeUpdater.RealtimeLightProbesCannot", "Realtime light probes cannot update (%#) - the realtime volumes stay black", bPipelineReady ? "the probe buffer has no UAV" : "a probe update or scene voxelize compute PSO is missing" );
            }
            return;
        }

        using Internal                         = RealtimeProbeUpdaterInternal;
        const bool                bHistoryGrew = _historyAllocator.assign( listRequest );
        const RealtimeProbeLayout layout       = planLayout( listRequest, _historyAllocator );
        const uint32              float4Stride = static_cast<uint32>( sizeof( float4 ) );

        // 복셀 - 장면 포인터가 달라진 볼륨의 바뀐 구간만 다시 칠한다(컴포넌트가 같은 장면을 캐시로 돌려준다).
        bool bVoxelChanged = false;
        if ( prepareVoxelPaint( pDevice, listRequest, layout, bCreateMissingMeshes, bVoxelChanged ) == false )
            return;
        if ( bVoxelChanged )
            _framesSinceInputChange = 0;

        // 기록 - 볼륨 정체마다 구간이다. 용량이 늘었거나 버퍼가 없으면 통째로 0 에서, 아니면 새로 든 볼륨의 구간만 0 으로 채운다(서 있는 볼륨은 누적을 잇는다).
        if ( bHistoryGrew || _historyBuffer.isValid() == false )
        {
            vector<float4> zeros( layout._totalHistoryElements, float4{} );
            if ( _historyBuffer.ensureCapacity( pDevice, float4Stride, layout._totalHistoryElements, Internal::kWriteUsage, true, true, zeros.data() ) == false || _historyBuffer._uav == kInvalidDescriptorIndex )
            {
                SW_LOG_ERROR_ID( "RealtimeProbeUpdater.FailedCreateProbeHistory", "Failed to create the probe history buffer (%# elements) - realtime light probes do not update", layout._totalHistoryElements );
                _historyAllocator.clear(); // 다음 프레임에 모든 볼륨이 새로 시작한다
                return;
            }
            _historyBuffer.upload( pDevice, zeros.data(), layout._totalHistoryElements * float4Stride );
            _framesSinceInputChange = 0;
        }
        else
        {
            for ( uint32 requestIndex = 0; requestIndex < static_cast<uint32>( listRequest.size() ); ++requestIndex )
            {
                if ( _historyAllocator.isFresh( requestIndex ) == false )
                    continue;
                const uint32         elementCount = listRequest[requestIndex]._probeCount * RealtimeProbeHistoryAllocator::kFloat4PerProbe;
                const vector<float4> zeros( elementCount, float4{} );
                _historyBuffer.upload( pDevice, zeros.data(), elementCount * float4Stride, layout._listHistoryOffset[requestIndex] * float4Stride );
                _framesSinceInputChange = 0;
            }
        }

        // 입력 행 - 프레임마다. 섞는 비율의 초기 가속은 볼륨마다 기록이 시작한 뒤의 프레임 수다.
        vector<uint32> listWarmup;
        for ( uint32 requestIndex = 0; requestIndex < static_cast<uint32>( listRequest.size() ); ++requestIndex )
        {
            listWarmup.push_back( _historyAllocator.getFramesSinceReset( requestIndex ) / MathUtil::max( listRequest[requestIndex]._updatePeriod, 1u ) );
        }
        _listRowScratch       = makeParamRows( listRequest, layout, listWarmup, projectSky( sky, bHasSky ), _frameCounter, lightBuffer.getGPULightBase() );
        const uint32 rowCount = static_cast<uint32>( _listRowScratch.size() );
        if ( _paramBuffer.ensureCapacity( pDevice, float4Stride, rowCount, Internal::kReadUsage, true, false, nullptr ) == false )
        {
            SW_LOG_ERROR_ID( "RealtimeProbeUpdater.FailedCreateProbeUpdate", "Failed to create the probe update parameter buffer - realtime light probes do not update" );
            return;
        }
        _paramBuffer.upload( pDevice, _listRowScratch.data(), rowCount * float4Stride );

        _totalProbes = layout._totalProbes;
        ++_frameCounter;
        _historyAllocator.advanceFrame();
        ++_framesSinceInputChange;
        _error   = computeError( listRequest, _framesSinceInputChange );
        _bActive = SW_TRUE;
    }

    bool RealtimeProbeUpdater::prepareVoxelPaint( IRHIDevice* pDevice, const vector<RealtimeProbeRequest>& listRequest, const RealtimeProbeLayout& layout, bool bCreateMissingMeshes,
                                                  bool& outChanged )
    {
        using Internal = RealtimeProbeUpdaterInternal;
        outChanged     = false;
        // 앞 프레임이 짠 칠하기를 기록하지 못했다(그래프가 돌지 않았다) — 무엇이 칠해졌는지 모르므로 모두 새로 칠한다.
        if ( _bPaintPending == SW_TRUE )
            _listPainted.clear();
        _bPaintPending = SW_FALSE;
        _listPendingCopy.clear();

        const uint64 previousGeneration = _voxelBuffer._generation;
        const bool   bVoxelReady        = _voxelBuffer.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), layout._totalVoxelWords, Internal::kWriteUsage, true, true, nullptr );
        if ( bVoxelReady == false || _voxelBuffer._uav == kInvalidDescriptorIndex )
        {
            SW_LOG_ERROR_ID( "RealtimeProbeUpdater.FailedCreateVoxelBuffer", "Failed to create the voxel buffer (%# words) - realtime light probes do not update", layout._totalVoxelWords );
            _listPainted.clear();
            return false;
        }
        if ( _voxelBuffer._generation != previousGeneration )
            _listPainted.clear(); // 새 버퍼다 — 칠한 것이 없다

        // 볼륨마다 같은 자리에 칠한 지난 장면이 있으면 바뀐 구간만, 없으면 격자 전체를 칠한다.
        vector<PaintedVolume>         listPainted;
        vector<SceneVoxelPaintTarget> listTarget;
        for ( size_t index = 0; index < listRequest.size(); ++index )
        {
            PaintedVolume current{ listRequest[index]._volumeID, layout._listVoxelWordOffset[index], layout._listEmissionWordOffset[index], listRequest[index]._pVoxelScene };
            const bool    bSamePlace = index < _listPainted.size() && _listPainted[index]._volumeID == current._volumeID && _listPainted[index]._wordOffset == current._wordOffset &&
                                    _listPainted[index]._emissionOffset == current._emissionOffset;
            const SceneVoxelScene* const pPainted = bSamePlace ? _listPainted[index]._pScene.get() : nullptr;
            if ( pPainted != current._pScene.get() )
                listTarget.push_back( SceneVoxelPaintTarget{ current._pScene.get(), pPainted, current._wordOffset, current._emissionOffset } );
            listPainted.push_back( std::move( current ) );
        }
        const SceneVoxelPaintPlan plan = SceneVoxelizer::planPaint( listTarget ); // 지난 장면(`_listPainted`)을 읽는다 — 아래에서 바꾼다
        if ( plan.isEmpty() == false && uploadPaint( pDevice, plan, bCreateMissingMeshes ) == false )
        {
            SW_LOG_ERROR_ID( "RealtimeProbeUpdater.FailedCreatePaintBuffers", "Failed to create the voxel paint buffers (%# vertices, %# indices) - realtime light probes do not update", plan._vertexCount,
                             plan._indexCount );
            _listPainted.clear();
            _listPendingCopy.clear();
            return false;
        }
        _listPainted      = std::move( listPainted );
        _clearThreadCount = plan._clearThreadCount;
        _paintThreadCount = plan._paintThreadCount;
        outChanged        = plan.isEmpty() == false;
        _bPaintPending    = outChanged ? SW_TRUE : SW_FALSE;
        return true;
    }

    bool RealtimeProbeUpdater::uploadPaint( IRHIDevice* pDevice, const SceneVoxelPaintPlan& plan, bool bCreateMissingMeshes )
    {
        using Internal            = RealtimeProbeUpdaterInternal;
        const uint32 float4Stride = static_cast<uint32>( sizeof( float4 ) );
        const uint32 clearRows    = static_cast<uint32>( plan._listClearRow.size() );
        const uint32 paintRows    = static_cast<uint32>( plan._listPaintRow.size() );
        const bool   bJobsReady   = _voxelClearJobBuffer.ensureCapacity( pDevice, float4Stride, clearRows, Internal::kReadUsage, true, false, nullptr ) &&
                                _voxelPaintJobBuffer.ensureCapacity( pDevice, float4Stride, paintRows, Internal::kReadUsage, true, false, nullptr );
        // 칠할 메시가 없어도 셰이더의 t1 · t2 는 빈 슬롯으로 두지 않는다(DX12 는 빈 디스크립터를 읽으면 폴트).
        const uint32 positionCount = MathUtil::max( plan._vertexCount, 1u ) * Internal::kFloatsPerPosition;
        const uint32 indexCount    = MathUtil::max( plan._indexCount, 1u );
        const bool   bMeshReady    = bJobsReady &&
                                _voxelPositionBuffer.ensureCapacity( pDevice, static_cast<uint32>( sizeof( float32 ) ), positionCount, Internal::kReadUsage, true, false, nullptr ) &&
                                _voxelIndexBuffer.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), indexCount, Internal::kReadUsage, true, false, nullptr );
        if ( bMeshReady == false )
            return false;
        _voxelClearJobBuffer.upload( pDevice, plan._listClearRow.data(), clearRows * float4Stride );
        _voxelPaintJobBuffer.upload( pDevice, plan._listPaintRow.data(), paintRows * float4Stride );

        _listIndexScratch.clear();
        _listIndexScratch.reserve( plan._indexCount );
        for ( const SceneVoxelPaintPlan::MeshSpan& span : plan._listMesh )
        {
            const Mesh& mesh = *span._mesh;
            _listIndexScratch.insert( _listIndexScratch.end(), mesh.getIndices().begin(), mesh.getIndices().end() );
            MeshRenderData& renderData = mesh.getRenderData();
            if ( renderData.isRHIValid() == false && mesh.isDynamic() == false && bCreateMissingMeshes )
                (void)renderData.initRHI( pDevice ); // 실패하면 아래에서 알린다
            const uint32 byteOffset = span._firstVertex * constant::render::kMeshPositionStride;
            const uint32 byteCount  = mesh.getVertexCount() * constant::render::kMeshPositionStride;
            if ( renderData.isRHIValid() )
            {
                // 렌더 프록시 정점 버퍼는 [위치 구간(LOD 0 이 앞)][속성 구간] 이다(`RHIMeshVertexStreams`).
                _listPendingCopy.push_back( PendingPositionCopy{ span._mesh, byteOffset, byteCount } );
                continue;
            }
            // GPU 버퍼가 없는 동적 메시(프레임마다 새 메시 — 천)만 CPU 정점을 올린다. 그 밖은 GPU 버퍼를 만들지 못한 것이다 — 위치를 0 으로 두어(넓이 없는 삼각형) 칠하지 않는다.
            _listPositionScratch.assign( static_cast<size_t>( mesh.getVertexCount() ) * Internal::kFloatsPerPosition, 0.0f );
            if ( mesh.isDynamic() )
            {
                const vector<RHIVertex>& listVertex = mesh.getCPUView().getVertices();
                for ( size_t vertexIndex = 0; vertexIndex < listVertex.size(); ++vertexIndex )
                {
                    for ( uint32 component = 0; component < Internal::kFloatsPerPosition; ++component )
                    {
                        _listPositionScratch[vertexIndex * Internal::kFloatsPerPosition + component] = listVertex[vertexIndex]._arrPosition[component];
                    }
                }
            }
            else
                SW_LOG_ERROR_ONCE_ID( "RealtimeProbeUpdater.OccluderWithoutGPUBuffer", "A mesh that occludes realtime probes has no GPU vertex buffer - it does not block their light" );
            _voxelPositionBuffer.upload( pDevice, _listPositionScratch.data(), byteCount, byteOffset );
        }
        _voxelIndexBuffer.upload( pDevice, _listIndexScratch.data(), static_cast<uint32>( _listIndexScratch.size() * sizeof( uint32 ) ) );
        return true;
    }

    void RealtimeProbeUpdater::recordUploads( IRHICommandList& cmd )
    {
        if ( _listPendingCopy.empty() )
            return;
        vector<RHIBufferTransition> listBefore;
        vector<RHIBufferTransition> listAfter;
        listBefore.push_back( RHIBufferTransition{ _voxelPositionBuffer._buffer, RHIBufferState::CopyDest } );
        listAfter.push_back( RHIBufferTransition{ _voxelPositionBuffer._buffer, RHIBufferState::ShaderResource } );
        for ( const PendingPositionCopy& copy : _listPendingCopy )
        {
            const RHIBufferHandle source = copy._mesh->getRenderData().getVertexBuffer();
            listBefore.push_back( RHIBufferTransition{ source, RHIBufferState::CopySource } );
            listAfter.push_back( RHIBufferTransition{ source, RHIBufferState::VertexOrConstant } );
        }
        cmd.transitionBuffers( listBefore.data(), static_cast<uint32>( listBefore.size() ) );
        for ( const PendingPositionCopy& copy : _listPendingCopy )
        {
            cmd.copyBufferRegion( _voxelPositionBuffer._buffer, copy._destinationOffset, copy._mesh->getRenderData().getVertexBuffer(), 0, copy._sizeBytes );
        }
        cmd.transitionBuffers( listAfter.data(), static_cast<uint32>( listAfter.size() ) );
        _listPendingCopy.clear();
    }

    void RealtimeProbeUpdater::dispatchVoxelJobs( IRHIPassCommandList& cmd, RHIPipelineStateHandle pso, const RHIStructuredBuffer& jobBuffer, uint32 threadCount ) const
    {
        if ( pso == 0 || threadCount == 0 )
            return;
        // 바인딩은 scene_voxelize.hlsl 레지스터와 1:1 이다. 스레드가 한 줄 상한(축마다 그룹 65535)을 넘으면 y 축으로 접는다.
        cmd.setComputePipelineState( pso );
        cmd.bindComputeShaderResource( jobBuffer._srv, 0 );
        cmd.bindComputeShaderResource( _voxelPositionBuffer._srv, 1 );
        cmd.bindComputeShaderResource( _voxelIndexBuffer._srv, 2 );
        cmd.bindComputeUAV( _voxelBuffer._uav, 0 );
        const uint32 rowCount = ( threadCount + SceneVoxelizer::kThreadsPerRow - 1u ) / SceneVoxelizer::kThreadsPerRow;
        cmd.dispatchThreads( MathUtil::min( threadCount, SceneVoxelizer::kThreadsPerRow ), rowCount, 1, SceneVoxelizer::kThreadGroupSize, 1, 1 );
    }

    void RealtimeProbeUpdater::dispatchVoxelClear( IRHIPassCommandList& cmd, RHIPipelineStateHandle pso ) const
    {
        dispatchVoxelJobs( cmd, pso, _voxelClearJobBuffer, _clearThreadCount );
    }

    void RealtimeProbeUpdater::dispatchVoxelPaint( IRHIPassCommandList& cmd, RHIPipelineStateHandle pso )
    {
        dispatchVoxelJobs( cmd, pso, _voxelPaintJobBuffer, _paintThreadCount );
        _bPaintPending = SW_FALSE;
    }

    void RealtimeProbeUpdater::dispatch( IRHIPassCommandList& cmd, RHIPipelineStateHandle pso, const GPULightBuffer& lightBuffer, RHIDescriptorIndex placeholderSRV )
    {
        if ( _bActive == SW_FALSE || pso == 0 )
            return;
        const RHIStructuredBuffer* pProbeBuffer = lightBuffer.findProbeBuffer();
        if ( pProbeBuffer == nullptr || lightBuffer.getProbeUAV() == kInvalidDescriptorIndex )
            return;
        // t0 라이트는 라이트 버퍼가 없는 프레임에 자리표를 건다(라이트 수 0 이면 셰이더가 읽지 않는다 - DX12 는 빈 디스크립터를 읽으면 폴트).
        const RHIDescriptorIndex lights = lightBuffer.isBindable() ? lightBuffer.getBuffer()._srv : placeholderSRV;

        // 두 쓰기 버퍼의 UAV 전이와 조명 패스(그래프, t13) 앞 셰이더 읽기는 프레임 그래프가 낸다(`ProbeIrradiance` · `ProbeHistory` — `FrameGraphExecutor::beginFrame` 이 등록한다).
        cmd.setComputePipelineState( pso );
        // g_ProbeLights(t0) / g_ProbeParams(t1) / g_ProbeVoxels(t2) / g_ProbeHistoryRW(u0) / g_ProbeOutRW(u1). probe_update.hlsl 레지스터와 1:1 이다.
        cmd.bindComputeShaderResource( lights, 0 );
        cmd.bindComputeShaderResource( _paramBuffer._srv, 1 );
        cmd.bindComputeShaderResource( _voxelBuffer._srv, 2 );
        cmd.bindComputeUAV( _historyBuffer._uav, 0 );
        cmd.bindComputeUAV( lightBuffer.getProbeUAV(), 1 );
        cmd.dispatchThreads( _totalProbes, 1, 1, kThreadGroupSize, 1, 1 );
    }

    void RealtimeProbeUpdater::release( IRHIDevice* pDevice )
    {
        _voxelBuffer.release( pDevice );
        _voxelClearJobBuffer.release( pDevice );
        _voxelPaintJobBuffer.release( pDevice );
        _voxelPositionBuffer.release( pDevice );
        _voxelIndexBuffer.release( pDevice );
        _historyBuffer.release( pDevice );
        _paramBuffer.release( pDevice );
        _listPainted.clear();
        _listPendingCopy.clear();
        _historyAllocator.clear();
        _framesSinceInputChange = 0;
        _bActive                = SW_FALSE;
        _bPaintPending          = SW_FALSE;
    }
} // namespace sw
