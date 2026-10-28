#include "pch.h"

#include "Engine/Renderer/Lighting/RealtimeProbeUpdater.h"

#include "Core/Common/BitUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    SW_LOG_CALLER( "RealtimeProbeUpdater" );

    namespace
    {
        struct RealtimeProbeUpdaterInternal
        {
            /// 황금각(라디안) — 하늘 투영의 방위각 간격입니다.
            static constexpr float32 kGoldenAngle = 2.39996323f;

            /// 정수 넷을 float4 의 비트로 싣습니다(셰이더는 asuint 로 읽는다).
            static float4 packBits( uint32 x, uint32 y, uint32 z, uint32 w )
            {
                return float4{ BitUtil::bitsToFloat( x ), BitUtil::bitsToFloat( y ), BitUtil::bitsToFloat( z ), BitUtil::bitsToFloat( w ) };
            }

            /// 부동소수로 실은 정수(원소의 칸 수 · 번호)를 되돌립니다.
            static uint32 toCount( float32 value ) { return static_cast<uint32>( MathUtil::max( value, 0.0f ) + 0.5f ); }
        };
    } // namespace

    RealtimeProbeLayout RealtimeProbeUpdater::planLayout( const vector<RealtimeProbeRequest>& listRequest )
    {
        RealtimeProbeLayout layout;
        for ( const RealtimeProbeRequest& request : listRequest )
        {
            layout._listVoxelWordOffset.push_back( layout._totalVoxelWords );
            layout._listHistoryOffset.push_back( layout._totalHistoryElements );
            layout._listFirstThread.push_back( layout._totalProbes );
            layout._totalVoxelWords += request._pVoxelGrid != nullptr ? static_cast<uint32>( request._pVoxelGrid->_listWord.size() ) : 0u;
            layout._totalHistoryElements += request._probeCount * SH9Color::kGPUFloat4Count;
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
            const SceneVoxelGrid&       grid    = *request._pVoxelGrid;
            const uint32                warmup  = index < listWarmup.size() ? listWarmup[index] : 0u;
            rows.push_back( float4{ element._colorIntensity._x, element._colorIntensity._y, element._colorIntensity._z, request._bounceAlbedo } );
            rows.push_back( float4{ element._directionType._x, element._directionType._y, element._directionType._z, request._blend } );
            // 프로브 버퍼 안의 첫 float4 번호 = 첫 프로브 번호(원소의 params.w) × 프로브당 float4 수.
            rows.push_back( Internal::packBits( Internal::toCount( element._params._x ), Internal::toCount( element._params._y ), Internal::toCount( element._params._z ),
                                                Internal::toCount( element._params._w ) * SH9Color::kGPUFloat4Count ) );
            rows.push_back( float4{ grid._origin._x, grid._origin._y, grid._origin._z, grid._cellSize } );
            rows.push_back( Internal::packBits( grid._dimX, grid._dimY, grid._dimZ, layout._listVoxelWordOffset[index] ) );
            rows.push_back( Internal::packBits( layout._listHistoryOffset[index], layout._listFirstThread[index], request._probeCount, request._rayCount ) );
            rows.push_back( Internal::packBits( MathUtil::max( request._updatePeriod, 1u ), warmup, 0u, 0u ) );
        }
        return rows;
    }

    void RealtimeProbeUpdater::prepare( IRHIDevice* pDevice, const vector<RealtimeProbeRequest>& listRequest, const GPULightBuffer& lightBuffer, const SkyGradient& sky, bool bHasSky,
                                        bool bPipelineReady )
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
                SW_LOG_ERROR( "Realtime light probes cannot update (%#) - the realtime volumes stay black", bPipelineReady ? "the probe buffer has no UAV" : "the probe update compute PSO is missing" );
            }
            return;
        }

        constexpr RHIBufferUsage  kReadUsage    = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        constexpr RHIBufferUsage  kHistoryUsage = kReadUsage | RHIBufferUsage::UnorderedAccess;
        const RealtimeProbeLayout layout        = planLayout( listRequest );
        const uint32              float4Stride  = static_cast<uint32>( sizeof( float4 ) );
        const uint32              wordStride    = static_cast<uint32>( sizeof( uint32 ) );

        // 복셀 단어 - 격자 포인터가 하나라도 달라진 프레임에만 다시 올린다(컴포넌트가 같은 격자를 캐시로 돌려준다).
        bool bVoxelChanged = _listHeldGrid.size() != listRequest.size() || _voxelBuffer.isValid() == false;
        for ( size_t index = 0; bVoxelChanged == false && index < listRequest.size(); ++index )
        {
            bVoxelChanged = _listHeldGrid[index] != listRequest[index]._pVoxelGrid;
        }
        if ( bVoxelChanged )
        {
            vector<uint32> words;
            words.reserve( layout._totalVoxelWords );
            _listHeldGrid.clear();
            for ( const RealtimeProbeRequest& request : listRequest )
            {
                words.insert( words.end(), request._pVoxelGrid->_listWord.begin(), request._pVoxelGrid->_listWord.end() );
                _listHeldGrid.push_back( request._pVoxelGrid );
            }
            if ( _voxelBuffer.ensureCapacity( pDevice, wordStride, layout._totalVoxelWords, kReadUsage, true, false, words.data() ) == false )
            {
                SW_LOG_ERROR( "Failed to create the voxel buffer (%# words) - realtime light probes do not update", layout._totalVoxelWords );
                _listHeldGrid.clear();
                return;
            }
            _voxelBuffer.upload( pDevice, words.data(), layout._totalVoxelWords * wordStride );
        }

        // 기록 - 볼륨 구성(번호와 프로브 수)이 달라지면 0 에서 다시 쌓는다.
        vector<uint64> historyKey;
        for ( const RealtimeProbeRequest& request : listRequest )
        {
            historyKey.push_back( request._volumeID );
            historyKey.push_back( request._probeCount );
        }
        if ( historyKey != _listHistoryKey || _historyBuffer.isValid() == false )
        {
            vector<float4> zeros( layout._totalHistoryElements, float4{} );
            if ( _historyBuffer.ensureCapacity( pDevice, float4Stride, layout._totalHistoryElements, kHistoryUsage, true, true, zeros.data() ) == false || _historyBuffer._uav == kInvalidDescriptorIndex )
            {
                SW_LOG_ERROR( "Failed to create the probe history buffer (%# elements) - realtime light probes do not update", layout._totalHistoryElements );
                return;
            }
            _historyBuffer.upload( pDevice, zeros.data(), layout._totalHistoryElements * float4Stride );
            _listHistoryKey   = std::move( historyKey );
            _framesSinceReset = 0;
        }

        // 입력 행 - 프레임마다.
        vector<uint32> listWarmup;
        for ( const RealtimeProbeRequest& request : listRequest )
        {
            listWarmup.push_back( _framesSinceReset / MathUtil::max( request._updatePeriod, 1u ) );
        }
        _listRowScratch       = makeParamRows( listRequest, layout, listWarmup, projectSky( sky, bHasSky ), _frameCounter, lightBuffer.getGPULightBase() );
        const uint32 rowCount = static_cast<uint32>( _listRowScratch.size() );
        if ( _paramBuffer.ensureCapacity( pDevice, float4Stride, rowCount, kReadUsage, true, false, _listRowScratch.data() ) == false )
        {
            SW_LOG_ERROR( "Failed to create the probe update parameter buffer - realtime light probes do not update" );
            return;
        }
        _paramBuffer.upload( pDevice, _listRowScratch.data(), rowCount * float4Stride );

        _totalProbes = layout._totalProbes;
        ++_frameCounter;
        ++_framesSinceReset;
        _bActive = SW_TRUE;
    }

    void RealtimeProbeUpdater::dispatch( IRHICommandList& cmd, RHIPipelineStateHandle pso, const GPULightBuffer& lightBuffer, RHIDescriptorIndex placeholderSRV )
    {
        if ( _bActive == SW_FALSE || pso == 0 )
            return;
        const RHIStructuredBufferSlot* pProbeBuffer = lightBuffer.findProbeBuffer();
        if ( pProbeBuffer == nullptr || lightBuffer.getProbeUAV() == kInvalidDescriptorIndex )
            return;
        // t0 라이트는 라이트 버퍼가 없는 프레임에 자리표를 건다(라이트 수 0 이면 셰이더가 읽지 않는다 - DX12 는 빈 디스크립터를 읽으면 폴트).
        const RHIDescriptorIndex lights = lightBuffer.isBindable() ? lightBuffer.getBuffer()._srv : placeholderSRV;

        cmd.transitionBuffer( pProbeBuffer->_buffer, RHIBufferState::UnorderedAccess );
        cmd.transitionBuffer( _historyBuffer._buffer, RHIBufferState::UnorderedAccess );
        cmd.setComputePipelineState( pso );
        // g_ProbeLights(t0) / g_ProbeParams(t1) / g_ProbeVoxels(t2) / g_ProbeHistoryRW(u0) / g_ProbeOutRW(u1). probeupdate.hlsl 레지스터와 1:1 이다.
        cmd.bindComputeShaderResource( lights, 0 );
        cmd.bindComputeShaderResource( _paramBuffer._srv, 1 );
        cmd.bindComputeShaderResource( _voxelBuffer._srv, 2 );
        cmd.bindComputeUAV( _historyBuffer._uav, 0 );
        cmd.bindComputeUAV( lightBuffer.getProbeUAV(), 1 );
        cmd.dispatchCompute( ( _totalProbes + kThreadGroupSize - 1u ) / kThreadGroupSize, 1, 1 );
        // 조명 패스(그래프)가 t13 으로 읽는다. 쓰기가 끝났음을 알린다.
        cmd.transitionBuffer( pProbeBuffer->_buffer, RHIBufferState::ShaderResource );
    }

    void RealtimeProbeUpdater::release( IRHIDevice* pDevice )
    {
        _voxelBuffer.release( pDevice );
        _historyBuffer.release( pDevice );
        _paramBuffer.release( pDevice );
        _listHeldGrid.clear();
        _listHistoryKey.clear();
        _framesSinceReset = 0;
        _bActive          = SW_FALSE;
    }
} // namespace sw
