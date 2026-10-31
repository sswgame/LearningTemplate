/**
 * @file FrameFog.cpp
 * @brief 안개 상수와 볼륨 안개(froxel) 컴퓨트의 준비 · 디스패치입니다. 안개 패스(`RenderPassType::Fog`)는 렌더러의 패스 실행이 그린다(`FramePassExecutor::executeFogPass`).
 * @details 안개는 장면 깊이로 시선 위 광학 두께를 구해 이미 칠한 색 위에 블렌드한다(fog.hlsl). 볼륨 안개를 켜면 주 시점에서만 컴퓨트(volumetric_fog.hlsl)가 froxel 마다
 *          라이트 산란을 누적해 구조 버퍼에 쓰고, 안개 패스가 그 버퍼를 t11(`SW_SLOT_PASS_SRV`)로 읽는다. 컴퓨트는 라이트 그리드(`dispatchLightGrid`) 뒤에 돈다.
 *          버퍼를 만드는 준비(`prepareVolumetricFog`)는 기록 전에, 디스패치(`dispatchVolumetricFog`)는 프레임 컴퓨트 프리패스에서 한다.
 */
#include "pch.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Environment/Fog/FogSettings.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PostProcess/FrameFog.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

namespace sw
{
    SW_LOG_CALLER( "FrameFog" );

    namespace
    {
        struct FrameFogInternal
        {
            /// 볼륨 안개 컴퓨트 입력의 행 번호입니다(volumetric_fog.hlsl 의 kRow* 와 같은 순서).
            static constexpr uint32 kRowInverseViewProj = 0;
            static constexpr uint32 kRowViewProj        = 4;
            static constexpr uint32 kRowEyeNear         = 8;
            static constexpr uint32 kRowFarSlices       = 9;
            static constexpr uint32 kRowFogColorDensity = 10;
            static constexpr uint32 kRowFogHeight       = 11;
            static constexpr uint32 kRowSunDirection    = 12;
            static constexpr uint32 kRowSunRadiance     = 13;
            static constexpr uint32 kRowLightGrid       = 14;
            static_assert( kRowLightGrid + 1 == SW_FOG_VOLUME_PARAM_FLOAT4_COUNT, "the parameter rows must fill the buffer exactly (volumetric_fog.hlsl)" );

            /// 행렬의 행 넷을 @p firstRow 부터 float4 넷으로 씁니다(행벡터 규약 — 셰이더가 float4x4( 행 넷 ) 으로 다시 짓는다).
            static void writeMatrixRows( vector<float4>& inoutListRow, uint32 firstRow, const float4x4& matrix )
            {
                inoutListRow[firstRow + 0] = float4{ matrix._11, matrix._12, matrix._13, matrix._14 };
                inoutListRow[firstRow + 1] = float4{ matrix._21, matrix._22, matrix._23, matrix._24 };
                inoutListRow[firstRow + 2] = float4{ matrix._31, matrix._32, matrix._33, matrix._34 };
                inoutListRow[firstRow + 3] = float4{ matrix._41, matrix._42, matrix._43, matrix._44 };
            }

            /// 정수 셋을 float4 의 비트로 싣습니다(셰이더는 asuint 로 읽는다).
            static float4 packBits( uint32 x, uint32 y, uint32 z, uint32 w )
            {
                const uint32 arrBits[4]{ x, y, z, w };
                float4       packed{};
                Memory::copy( &packed, arrBits, sizeof( packed ) );
                return packed;
            }
        };
    } // namespace

    FrameFog::FrameFog( IRHIDevice* const& pDevice, const FramePSOManager& psoManager )
        : _pDevice{ pDevice }
        , _psoManager{ psoManager }
        , _frameFog{}
        , _fogVolumeBuffer{}
        , _fogVolumeParamBuffer{}
        , _listScratchFogVolumeParam{}
    {
    }

    void FrameFog::release()
    {
        _fogVolumeBuffer.release( _pDevice );
        _fogVolumeParamBuffer.release( _pDevice );
        _bFogVolumeActive = SW_FALSE;
    }

    void FrameFog::applyFrameFog( bool bHasFog, const FogSettings& fog )
    {
        // 셰이더 상수 배치는 FogSettings 머리 주석이 정본이다(fog.hlsl 이 같은 순서로 읽는다).
        _bHasFrameFog = bHasFog ? SW_TRUE : SW_FALSE;
        if ( bHasFog == false )
        {
            _frameFog = FrameFogState{};
            return;
        }
        // 방향광 산란 세기가 0 이면 색도 0 으로 보내 셰이더가 분기 없이 더하게 한다.
        const float32 inscatterScale = MathUtil::max( fog._inscatterIntensity, 0.0f );
        _frameFog._colorDensity      = float4{ fog._color._x, fog._color._y, fog._color._z, MathUtil::max( fog._density, 0.0f ) };
        _frameFog._heightParams      = float4{ MathUtil::max( fog._heightFalloff, 0.0f ), fog._baseHeight, MathUtil::max( fog._startDistance, 0.0f ), MathUtil::saturate( fog._maxOpacity ) };
        _frameFog._inscatter         = float4{ fog._inscatterColor._x * inscatterScale, fog._inscatterColor._y * inscatterScale, fog._inscatterColor._z * inscatterScale,
                                       MathUtil::max( fog._inscatterExponent, 1.0f ) };
        // 볼륨은 컴퓨트가 돈 프레임의 주 시점에서만 켠다(executeFogPass 가 x 를 정한다). 물속 안개는 볼륨이 없다.
        const bool bVolumetric  = fog._bVolumetric == SW_TRUE && fog._bUnderwater == SW_FALSE;
        _frameFog._volumeParams = float4{ 0.0f, MathUtil::max( fog._volumeDistance, 1.0f ), MathUtil::max( fog._scattering, 0.0f ), MathUtil::clamp( fog._anisotropy, -0.95f, 0.95f ) };
        _frameFog._bVolumetric  = bVolumetric ? SW_TRUE : SW_FALSE;
    }

    void FrameFog::prepareVolumetricFog( IRHIDevice* pDevice, const RenderView& mainView, const float4& keyLightDirIntensity, const float4& keyLightColorAmbient,
                                         const GPULightBuffer& lightBuffer, uint32 mainLightGridIndex )
    {
        using Internal     = FrameFogInternal;
        _bFogVolumeActive  = SW_FALSE;
        const bool bWanted = pDevice != nullptr && _bHasFrameFog == SW_TRUE && _frameFog._bVolumetric == SW_TRUE;
        if ( bWanted == false )
            return;
        // 못 켜는 이유는 이유마다 한 번만 알린다(`SW_LOG_ERROR_ONCE`). 볼륨 없이 평범한 안개로 그린다 — 안개 패스의 꺼진 상태이지 다른 경로가 아니다.
        const bool bPipelineReady = _psoManager.getEnginePSO( RenderPassType::VolumetricFog ) != 0;
        if ( bPipelineReady == false )
        {
            SW_LOG_ERROR_ONCE( "The volumetric fog compute PSO is missing - the fog is drawn without the light-scattering volume" );
            return;
        }

        constexpr RHIBufferUsage kVolumeUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
        constexpr RHIBufferUsage kParamUsage  = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        constexpr uint32         kCellCount   = SW_FOG_VOLUME_DIM_X * SW_FOG_VOLUME_DIM_Y * SW_FOG_VOLUME_DIM_Z;
        const uint32             rowStride    = static_cast<uint32>( sizeof( float4 ) );
        const bool               bVolumeReady = _fogVolumeBuffer.ensureCapacity( pDevice, rowStride, kCellCount, kVolumeUsage, true, true, nullptr ) &&
                                  _fogVolumeBuffer._uav != kInvalidDescriptorIndex && _fogVolumeBuffer._srv != kInvalidDescriptorIndex;
        if ( bVolumeReady == false )
        {
            SW_LOG_ERROR_ONCE( "Failed to create the volumetric fog buffer (%# cells) - the fog is drawn without the light-scattering volume", kCellCount );
            return;
        }

        // 입력 행 — 주 시점의 행렬, 눈, 안개 값, 해, 라이트 그리드(volumetric_fog.hlsl 머리 주석과 같은 순서).
        const float4x4 inverse = mainView._viewProj.invert();
        // (0, 0, 1, 0) 의 역투영 동차 좌표 w 가 0 이면 직교다(fog.hlsl 과 같은 판정). 원근은 눈에서, 직교는 화면 점의 가까운 면 점에서 쏜다.
        const bool bPerspective = MathUtil::abs( inverse._34 ) > 1.0e-6f;
        // 행은 이름 붙은 번호(kRow*)로 쓴다 — 넣는 순서가 셰이더의 행 번호와 어긋날 수 없다.
        vector<float4>& listRow = _listScratchFogVolumeParam;
        listRow.assign( SW_FOG_VOLUME_PARAM_FLOAT4_COUNT, float4{} );
        Internal::writeMatrixRows( listRow, Internal::kRowInverseViewProj, inverse );
        Internal::writeMatrixRows( listRow, Internal::kRowViewProj, mainView._viewProj );
        listRow[Internal::kRowEyeNear]         = float4{ mainView._position._x, mainView._position._y, mainView._position._z, SW_FOG_VOLUME_NEAR };
        listRow[Internal::kRowFarSlices]       = float4{ _frameFog._volumeParams._y, static_cast<float32>( SW_FOG_VOLUME_DIM_Z ), static_cast<float32>( SW_FOG_VOLUME_DIM_X ),
                                                   static_cast<float32>( SW_FOG_VOLUME_DIM_Y ) };
        listRow[Internal::kRowFogColorDensity] = _frameFog._colorDensity;
        listRow[Internal::kRowFogHeight]       = float4{ _frameFog._heightParams._x, _frameFog._heightParams._y, _frameFog._heightParams._z, _frameFog._volumeParams._w };
        // 해: 키 라이트의 진행 방향의 반대가 해 쪽이다. 키 라이트가 없으면 세기 0 이라 방향은 무엇이든 기여가 없다.
        const float3  lightTravel           = float3{ keyLightDirIntensity._x, keyLightDirIntensity._y, keyLightDirIntensity._z };
        const float32 travelLength          = lightTravel.getLength();
        const float3  toSun                 = travelLength > MathUtil::kEpsilon ? lightTravel * ( -1.0f / travelLength ) : float3{ 0.0f, 1.0f, 0.0f };
        const float32 sunIntensity          = keyLightDirIntensity._w;
        listRow[Internal::kRowSunDirection] = float4{ toSun._x, toSun._y, toSun._z, _frameFog._volumeParams._z };
        listRow[Internal::kRowSunRadiance]  = float4{ keyLightColorAmbient._x * sunIntensity, keyLightColorAmbient._y * sunIntensity, keyLightColorAmbient._z * sunIntensity,
                                                     bPerspective ? 1.0f : 0.0f };
        const bool   bLights                = lightBuffer.isBindable();
        const uint32 gridBase               = ( bLights && mainLightGridIndex != invalid_index::kUint32 ) ? lightBuffer.getGridBase( mainLightGridIndex ) : invalid_index::kUint32;
        const bool   bGrid                  = gridBase != invalid_index::kUint32 && lightBuffer.findGridBuffer() != nullptr;
        listRow[Internal::kRowLightGrid]    = Internal::packBits( bLights ? lightBuffer.getCount() : 0u, bGrid ? gridBase : 0u, bGrid ? 1u : 0u, 0u );

        const uint32 paramRowCount = static_cast<uint32>( _listScratchFogVolumeParam.size() );
        if ( _fogVolumeParamBuffer.ensureCapacity( pDevice, rowStride, paramRowCount, kParamUsage, true, false, _listScratchFogVolumeParam.data() ) == false )
        {
            SW_LOG_ERROR_ONCE( "Failed to create the volumetric fog parameter buffer - the fog is drawn without the light-scattering volume" );
            return;
        }
        _fogVolumeParamBuffer.upload( pDevice, _listScratchFogVolumeParam.data(), paramRowCount * rowStride );
        _bFogVolumeActive = SW_TRUE;
    }

    void FrameFog::dispatchVolumetricFog( IRHICommandList& cmd, const GPULightBuffer& lightBuffer, RHIDescriptorIndex placeholderSRV ) const
    {
        if ( _bFogVolumeActive == SW_FALSE )
            return;
        SW_PROFILE_SCOPE( "RT.Fog.Volume" );
        // t0 라이트 · t2 그리드는 라이트 버퍼가 없거나 뷰의 그리드가 없는 프레임에 자리표를 건다(라이트 수 0 이면 셰이더가 읽지 않는다 — DX12 는 빈 디스크립터를 읽으면 폴트).
        const bool                     bLights = lightBuffer.isBindable();
        const RHIStructuredBufferSlot* pGrid   = bLights ? lightBuffer.findGridBuffer() : nullptr;
        const RHIDescriptorIndex       lights  = bLights ? lightBuffer.getBuffer()._srv : placeholderSRV;
        const RHIDescriptorIndex       grid    = pGrid != nullptr ? pGrid->_srv : placeholderSRV;

        cmd.transitionBuffer( _fogVolumeBuffer._buffer, RHIBufferState::UnorderedAccess );
        cmd.setComputePipelineState( _psoManager.getEnginePSO( RenderPassType::VolumetricFog ) );
        // g_FogLights(t0) / g_FogParams(t1) / g_FogLightGrid(t2) / g_FogVolumeRW(u0). volumetric_fog.hlsl 레지스터와 1:1 이다.
        cmd.bindComputeShaderResource( lights, 0 );
        cmd.bindComputeShaderResource( _fogVolumeParamBuffer._srv, 1 );
        cmd.bindComputeShaderResource( grid, 2 );
        cmd.bindComputeUAV( _fogVolumeBuffer._uav, 0 );
        constexpr uint32 kGroupSize = SW_FOG_VOLUME_GROUP_SIZE;
        cmd.dispatchCompute( ( SW_FOG_VOLUME_DIM_X + kGroupSize - 1u ) / kGroupSize, ( SW_FOG_VOLUME_DIM_Y + kGroupSize - 1u ) / kGroupSize, 1 );
        // 안개 패스(그래프)가 t11 로 읽는다. 쓰기가 끝났음을 알린다.
        cmd.transitionBuffer( _fogVolumeBuffer._buffer, RHIBufferState::ShaderResource );
    }
} // namespace sw
