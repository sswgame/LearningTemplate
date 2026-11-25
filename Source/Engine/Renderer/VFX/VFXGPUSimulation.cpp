#include "pch.h"

#include "Engine/Renderer/VFX/VFXGPUSimulation.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Graph/FrameBufferTable.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"
#include "Engine/VFX/VFXEffectDef.h"

#include "vfx_simulate.hlsli"

namespace sw
{
    namespace
    {
        /**
         * @brief 프레임 그래프 자원 이름(파이프라인 XML `framecomputerenderpass.xml` 의 `_listResource`)입니다. 한 번만 intern 한다(전역 문자열 표의 뮤텍스를 프레임마다 잡지 않는다).
         * @details 시뮬레이션하는 방출기와 읽기만 하는 방출기는 쓰는 패스가 달라 이름을 나눈다(`VFXGPUSimulation::registerFrameResources`).
         */
        struct VFXFrameResource
        {
            static hashed_string state()
            {
                static const hashed_string kName( "ParticleState" );
                return kName;
            }
            static hashed_string particles()
            {
                static const hashed_string kName( "Particles" );
                return kName;
            }
            static hashed_string args()
            {
                static const hashed_string kName( "ParticleArgs" );
                return kName;
            }
            static hashed_string idleParticles()
            {
                static const hashed_string kName( "IdleParticles" );
                return kName;
            }
            static hashed_string idleArgs()
            {
                static const hashed_string kName( "IdleParticleArgs" );
                return kName;
            }
            static hashed_string order()
            {
                static const hashed_string kName( "ParticleOrder" );
                return kName;
            }
        };

        struct VFXGPUSimulationInternal
        {
            static constexpr uint32 kWordsPerRow = 4;
            /** @brief 입자 상태 원소 하나의 바이트 수입니다(vfx_simulate.hlsl 의 VFXParticleState). */
            static constexpr uint32 kStateStride = static_cast<uint32>( sizeof( float4 ) ) * SW_VFX_STATE_FLOAT4_COUNT;
            /** @brief 입자 상태는 두 반쪽이다 — 단계마다 한쪽에서 읽어 다른 쪽에 압축한다. */
            static constexpr uint32 kStateHalfCount = 2;
            static_assert( SW_VFX_ARGS_WORD_ALIVE * sizeof( uint32 ) >= sizeof( RHIDrawIndirectCommand ), "the alive count must sit after the draw arguments" );
            static_assert( SW_VFX_ARGS_WORD_DROPPED < SW_VFX_ARGS_WORD_COUNT, "the dropped counter must sit inside the arguments buffer" );
            static_assert( SW_VFX_ARGS_WORD_DEAD_FIRST >= SW_VFX_ARGS_WORD_COUNT, "the dead slot stack must sit after the fixed words" );

            static uint32 lowWord( uint64 value ) { return static_cast<uint32>( value ); }
            static uint32 highWord( uint64 value ) { return static_cast<uint32>( value >> 32 ); }

            static void appendRow( vector<uint32>& inoutListWord, uint32 x, uint32 y, uint32 z, uint32 w )
            {
                inoutListWord.push_back( x );
                inoutListWord.push_back( y );
                inoutListWord.push_back( z );
                inoutListWord.push_back( w );
            }

            /** @brief 행렬 @p matrix 의 행 넷(행벡터 규약 — _11.._14 가 첫 행)을 붙입니다. */
            static void appendMatrixRows( vector<uint32>& inoutListWord, const float4x4& matrix )
            {
                appendRow( inoutListWord, BitUtil::floatToBits( matrix._11 ), BitUtil::floatToBits( matrix._12 ), BitUtil::floatToBits( matrix._13 ), BitUtil::floatToBits( matrix._14 ) );
                appendRow( inoutListWord, BitUtil::floatToBits( matrix._21 ), BitUtil::floatToBits( matrix._22 ), BitUtil::floatToBits( matrix._23 ), BitUtil::floatToBits( matrix._24 ) );
                appendRow( inoutListWord, BitUtil::floatToBits( matrix._31 ), BitUtil::floatToBits( matrix._32 ), BitUtil::floatToBits( matrix._33 ), BitUtil::floatToBits( matrix._34 ) );
                appendRow( inoutListWord, BitUtil::floatToBits( matrix._41 ), BitUtil::floatToBits( matrix._42 ), BitUtil::floatToBits( matrix._43 ), BitUtil::floatToBits( matrix._44 ) );
            }

            /** @brief 커브 키 하나의 구간 모양 단어 둘(상수, 직선)입니다. 둘 다 0 이면 에르미트다 — 열거자를 더하면 이 switch 가 빌드를 멈춘다. */
            static void computeInterpolationWords( CurveInterpolation interpolation, uint32& outConstant, uint32& outLinear )
            {
                outConstant = 0;
                outLinear   = 0;
                switch ( interpolation )
                {
                    case CurveInterpolation::Constant:
                    {
                        outConstant = 1;
                        break;
                    }
                    case CurveInterpolation::Linear:
                    {
                        outLinear = 1;
                        break;
                    }
                    case CurveInterpolation::Cubic:
                    {
                        break;
                    }
                    case CurveInterpolation::Eased:
                    {
                        // GPU 커브 표에는 이징 곡선 칸이 없다 — 데이터 결함으로 알리고 직선으로 싣는다(셰이더에 BlendCurve 를 옮기기 전까지 VFX 커브는 Eased 를 쓰지 않는다).
                        SW_LOG_ERROR_ID( "VFXGPUSimulation.EasedCurveKey", "GPU VFX curves do not support Eased keys (sequencer and CPU curves only) - uploaded as Linear" );
                        outLinear = 1;
                        break;
                    }
                }
            }

            /** @brief @p curve 의 키를 @p inoutListWord 끝에 붙이고, 구간 첫 행(@p sectionFirstWord 기준) 기준의 첫 행을 돌려줍니다. */
            static uint32 appendCurve( vector<uint32>& inoutListWord, size_t sectionFirstWord, const FloatCurve& curve )
            {
                const uint32 firstRow = static_cast<uint32>( ( inoutListWord.size() - sectionFirstWord ) / kWordsPerRow );
                for ( const FloatCurveKey& key : curve._listKey )
                {
                    uint32 constantWord = 0;
                    uint32 linearWord   = 0;
                    computeInterpolationWords( key._interpolation, constantWord, linearWord );
                    appendRow( inoutListWord, BitUtil::floatToBits( key._time ), BitUtil::floatToBits( key._value ), BitUtil::floatToBits( key._arriveTangent ), BitUtil::floatToBits( key._leaveTangent ) );
                    appendRow( inoutListWord, constantWord, linearWord, 0u, 0u );
                }
                return firstRow;
            }

            /** @brief 행 @p row 의 단어 @p word 를 고칩니다(구간 첫 단어 기준). */
            static void setWord( vector<uint32>& inoutListWord, size_t sectionFirstWord, uint32 row, uint32 word, uint32 value )
            {
                inoutListWord[sectionFirstWord + static_cast<size_t>( row ) * kWordsPerRow + word] = value;
            }

            /** @brief 비 가림 맵의 두께(m) — 맵 아래 어디에 있든 덮였다(장면 깊이처럼 "다른 물체 뒤" 를 가리지 않는다). */
            static constexpr float32 kRainOcclusionThickness = 1.0e6f;

            /** @brief 충돌을 켜고 비 가림 맵을 읽는 방법이면 true 입니다. */
            static bool usesRainOcclusionDepth( const VFXCollisionDef& collision )
            {
                return collision._mode != VFXCollisionMode::None && collision._method == VFXCollisionMethod::RainOcclusion;
            }

            /** @brief 깊이 충돌의 플래그입니다. 깊이 방법이 아니면 0 이다(물리 레이캐스트는 GPU 에 오지 않는다 — 모듈 표가 막는다). */
            static uint32 computeCollisionFlags( const VFXCollisionDef& collision )
            {
                const bool bSceneDepth = collision._mode != VFXCollisionMode::None && collision._method == VFXCollisionMethod::SceneDepth;
                if ( bSceneDepth == false && usesRainOcclusionDepth( collision ) == false )
                    return 0u;
                return collision._mode == VFXCollisionMode::Kill ? SW_VFX_FLAG_COLLIDE_KILL : SW_VFX_FLAG_COLLIDE_BOUNCE;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VFXGPUSimulation::EmitterState::EmitterState( shared_ptr<const VFXEmitterDef> pDef, float32 spawnScale )
        : _clock{ std::move( pDef ), spawnScale }
        , _doneStepCount{ 0 }
        , _stateBuffer{}
        , _particleBuffer{}
        , _argsBuffer{}
        , _drawSource{}
        , _pRunToken{}
        , _frameSlot{ constant::invalid_index::kUint32 }
        , _bNeedsReset{ SW_TRUE }
        , _bSeen{ SW_FALSE }
        , _bSimulated{ SW_FALSE }
    {
    }

    VFXGPUSimulation::VFXGPUSimulation()
        : _mapState{}
        , _listDispatch{}
        , _listMeshDispatch{}
        , _listBatchDrawSource{}
        , _listParamWord{}
        , _listScratchStaleRunID{}
        , _listSortDispatch{}
        , _listBatchState{}
        , _listBatchSorted{}
        , _listLightRange{}
        , _frameInput{}
        , _paramBuffer{}
        , _orderBuffer{}
        , _pendingStepCount{ 0 }
        , _readSlotCount{ 0 }
        , _bPipelineMissingLogged{ SW_FALSE }
        , _bMeshPipelineMissingLogged{ SW_FALSE }
        , _bLightOverflowLogged{ SW_FALSE }
    {
    }

    VFXGPUSimulation::~VFXGPUSimulation() = default;

    VFXGPULightAssignment VFXGPUSimulation::assignLightRanges( const vector<GPUParticleBatch>& listBatch, const vector<GPUVFXMeshEmitter>& listMeshEmitter,
                                                               vector<VFXGPULightRange>& outListRange )
    {
        const size_t batchCount = listBatch.size();
        outListRange.assign( batchCount + listMeshEmitter.size(), VFXGPULightRange{} );
        VFXGPULightAssignment assignment{};
        for ( size_t rangeIndex = 0; rangeIndex < outListRange.size(); ++rangeIndex )
        {
            const bool                  bBatch   = rangeIndex < batchCount;
            const VFXGPUEmitterRequest* pRequest = bBatch ? listBatch[rangeIndex].findGPURequest() : &listMeshEmitter[rangeIndex - batchCount]._request;
            // 메시 방출기는 인스턴스 칸이 있어야 시뮬레이션이 돈다(prepareFrame). 돌지 않는 방출기가 슬롯을 받으면 죽은 슬롯으로 남는다.
            const bool bDrawn      = bBatch || listMeshEmitter[rangeIndex - batchCount]._pListInstanceSlot != nullptr;
            const bool bHasRequest = pRequest != nullptr && pRequest->_pDef != nullptr;
            if ( bHasRequest == false || bDrawn == false )
                continue;
            const uint32 maxLights = pRequest->_pDef->_light._maxLights;
            if ( maxLights == 0 )
                continue;
            if ( assignment._slotCount + maxLights > GPULightBuffer::kMaxGPULight )
            {
                ++assignment._overflowEmitterCount;
                continue;
            }
            outListRange[rangeIndex] = VFXGPULightRange{ assignment._slotCount, maxLights };
            assignment._slotCount += maxLights;
        }
        return assignment;
    }

    bool VFXGPUSimulation::usesRainOcclusion( const vector<GPUParticleBatch>& listBatch, const vector<GPUVFXMeshEmitter>& listMeshEmitter )
    {
        const auto reads = []( const VFXGPUEmitterRequest* pRequest )
        { return pRequest != nullptr && pRequest->_pDef != nullptr && VFXGPUSimulationInternal::usesRainOcclusionDepth( pRequest->_pDef->_collision ); };
        for ( const GPUParticleBatch& batch : listBatch )
        {
            if ( reads( batch.findGPURequest() ) )
                return true;
        }
        for ( const GPUVFXMeshEmitter& emitter : listMeshEmitter )
        {
            if ( reads( &emitter._request ) )
                return true;
        }
        return false;
    }

    const VFXDepthHistoryInput& VFXGPUSimulation::getCollisionDepth( const VFXCollisionDef& collision ) const
    {
        return VFXGPUSimulationInternal::usesRainOcclusionDepth( collision ) ? _frameInput._rainOcclusion : _frameInput._depth;
    }

    void VFXGPUSimulation::prepareFrame( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch, const vector<GPUVFXMeshEmitter>& listMeshEmitter,
                                         vector_reference<const VFXGPUParticleSortJob> listSortJob, uint32 gpuOrderCount, const VFXSimulationFrameInput& input )
    {
        _frameInput       = input;
        _pendingStepCount = 0;
        _listDispatch.clear();
        _listMeshDispatch.clear();
        _listParamWord.clear();
        _listBatchDrawSource.assign( listBatch.size(), nullptr );
        _listSortDispatch.clear();
        _listBatchState.assign( listBatch.size(), nullptr );
        _listBatchSorted.assign( listBatch.size(), SW_FALSE );
        _readSlotCount = 0;
        for ( auto& [runID, pState] : _mapState )
        {
            (void)runID;
            pState->_bSeen      = SW_FALSE;
            pState->_bSimulated = SW_FALSE;
            pState->_frameSlot  = constant::invalid_index::kUint32;
        }

        bool bAnyGPUBatch = listMeshEmitter.empty() == false;
        for ( const GPUParticleBatch& batch : listBatch )
        {
            bAnyGPUBatch = bAnyGPUBatch || batch.findGPURequest() != nullptr;
        }
        reportMissingSimulationPipeline( bAnyGPUBatch );

        assignParticleLights( listBatch, listMeshEmitter );

        if ( input._bSimulationPipelineReady )
        {
            SW_PROFILE_SCOPE( "RT.VFX.SimulationPrepare" );
            prepareParticleEmitters( device, listBatch );
            prepareMeshEmitters( device, listBatch, listMeshEmitter );
            prepareParticleSortJobs( device, listBatch, listSortJob, gpuOrderCount );
        }

        releaseStaleEmitters( device );

        uploadParamBuffer( device, listBatch );
    }

    void VFXGPUSimulation::reportMissingSimulationPipeline( bool bAnyGPUBatch )
    {
        const bool bReportMissingPipeline = bAnyGPUBatch && _frameInput._bSimulationPipelineReady == false && _bPipelineMissingLogged == false;
        if ( bReportMissingPipeline )
        {
            // 대신 CPU 로 그리지 않는다 — 시뮬레이션 대상은 데이터와 빌드가 정했고, 바꿔 그리면 CPU 와 GPU 를 비교하는 화면이 거짓이 된다.
            SW_LOG_ERROR_ID( "VFXGPUSimulation.VFXSimulationComputePSO", "The VFX simulation compute PSO is missing - GPU-simulated emitters are not drawn" );
            _bPipelineMissingLogged = SW_TRUE;
        }
    }

    void VFXGPUSimulation::assignParticleLights( const vector<GPUParticleBatch>& listBatch, const vector<GPUVFXMeshEmitter>& listMeshEmitter )
    {
        // 파티클 라이트 — 라이트 버퍼가 GPU 구간을 받은 프레임에만 쓴다. 범위는 렌더러가 구간 크기를 정할 때와 같은 함수로 얻는다(어긋나면 쓰지 않는다).
        _listLightRange.clear();
        const bool bLightsReady = _frameInput._lightSlotCount > 0 && _frameInput._lightUAV != kInvalidDescriptorIndex && _frameInput._lightBuffer != 0;
        if ( bLightsReady )
        {
            const VFXGPULightAssignment assignment = assignLightRanges( listBatch, listMeshEmitter, _listLightRange );
            if ( assignment._slotCount != _frameInput._lightSlotCount )
            {
                SW_LOG_ERROR_ID( "VFXGPUSimulation.VFXParticleLightSlots", "VFX particle light slots (%#) differ from the light buffer's GPU section (%#) - particle lights are not drawn", assignment._slotCount,
                                 _frameInput._lightSlotCount );
                _listLightRange.clear();
            }
            else if ( assignment._overflowEmitterCount > 0 && _bLightOverflowLogged == false )
            {
                _bLightOverflowLogged = SW_TRUE;
                SW_LOG_ERROR_ID( "VFXGPUSimulation.VFXEmittersAskedMore", "%# VFX emitters asked for more particle light slots than the GPU section holds (%#) - they have no lights", assignment._overflowEmitterCount,
                                 GPULightBuffer::kMaxGPULight );
            }
        }
    }

    void VFXGPUSimulation::prepareParticleEmitters( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch )
    {
        for ( uint32 batchIndex = 0; batchIndex < static_cast<uint32>( listBatch.size() ); ++batchIndex )
        {
            const GPUParticleBatch&     batch    = listBatch[batchIndex];
            const VFXGPUEmitterRequest* pRequest = batch.findGPURequest();
            if ( pRequest == nullptr || pRequest->_pDef == nullptr )
                continue;
            EmitterState* pState = prepareEmitterState( device, *pRequest );
            if ( pState == nullptr )
                continue;
            const VFXGPULightRange lightRange = batchIndex < _listLightRange.size() ? _listLightRange[batchIndex] : VFXGPULightRange{};
            appendSimulation( *pState, *pRequest, batch._world, batch._bRibbon == SW_TRUE, lightRange );
            _listBatchDrawSource[batchIndex] = &pState->_drawSource;
            _listBatchState[batchIndex]      = pState;
        }
    }

    void VFXGPUSimulation::prepareParticleSortJobs( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch, vector_reference<const VFXGPUParticleSortJob> listSortJob,
                                                    uint32 gpuOrderCount )
    {
        using Internal = VFXGPUSimulationInternal;
        if ( listSortJob.empty() )
            return;
        // 정렬 PSO 가 없으면 순서 없이 그린다(옛 순서 칸을 읽지 않게 배치를 정렬 안 됨으로 둔다) — 시뮬레이션 PSO 와 같은 규칙으로 한 번 알린다.
        if ( _frameInput._bSortPipelineReady == false )
        {
            SW_LOG_ERROR_ONCE_ID( "VFXGPUSimulation.VFXParticleSortPSO", "The VFX particle sort compute PSO is missing - sorted GPU emitters draw unsorted" );
            return;
        }
        constexpr RHIBufferUsage kOrderUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
        const bool               bOrderReady = _orderBuffer.ensureCapacity( &device, static_cast<uint32>( sizeof( uint32 ) ), MathUtil::max( 1u, gpuOrderCount ), kOrderUsage, true, true, nullptr ) &&
                                 _orderBuffer._uav != kInvalidDescriptorIndex && _orderBuffer._srv != kInvalidDescriptorIndex;
        if ( bOrderReady == false )
        {
            SW_LOG_ERROR_ONCE_ID( "VFXGPUSimulation.FailedCreateOrderBuffer", "Failed to create the GPU particle order buffer (%# entries) - sorted GPU emitters draw unsorted", gpuOrderCount );
            return;
        }
        // 배치마다 일이 하나라도 빠지면(상태를 만들지 못했다) 그 배치는 순서 없이 그린다 — 일부 뷰만 옛 칸을 읽지 않게.
        vector<uint8> listBatchMissing( _listBatchState.size(), SW_FALSE );
        for ( const VFXGPUParticleSortJob& job : listSortJob )
        {
            EmitterState* pState = job._batchIndex < _listBatchState.size() ? _listBatchState[job._batchIndex] : nullptr;
            if ( pState == nullptr )
            {
                if ( job._batchIndex < listBatchMissing.size() )
                    listBatchMissing[job._batchIndex] = SW_TRUE;
                continue;
            }
            const uint32 paramRow = static_cast<uint32>( _listParamWord.size() / Internal::kWordsPerRow );
            const bool   bUseAxis = job._axis.getLengthSquared() > MathUtil::kEpsilon;
            Internal::appendRow( _listParamWord, job._orderFirst, static_cast<uint32>( job._sortMode ), 0u, 0u );
            // 방출기 행렬의 열 0..2(행벡터 규약 — 셰이더가 dot( float4( 로컬, 1 ), 열 ) 로 월드 좌표를 얻는다, particle.hlsl 과 같다).
            const float4x4& world = listBatch[job._batchIndex]._world;
            Internal::appendRow( _listParamWord, BitUtil::floatToBits( world._11 ), BitUtil::floatToBits( world._21 ), BitUtil::floatToBits( world._31 ), BitUtil::floatToBits( world._41 ) );
            Internal::appendRow( _listParamWord, BitUtil::floatToBits( world._12 ), BitUtil::floatToBits( world._22 ), BitUtil::floatToBits( world._32 ), BitUtil::floatToBits( world._42 ) );
            Internal::appendRow( _listParamWord, BitUtil::floatToBits( world._13 ), BitUtil::floatToBits( world._23 ), BitUtil::floatToBits( world._33 ), BitUtil::floatToBits( world._43 ) );
            Internal::appendRow( _listParamWord, BitUtil::floatToBits( job._eye._x ), BitUtil::floatToBits( job._eye._y ), BitUtil::floatToBits( job._eye._z ), bUseAxis ? 1u : 0u );
            Internal::appendRow( _listParamWord, BitUtil::floatToBits( job._axis._x ), BitUtil::floatToBits( job._axis._y ), BitUtil::floatToBits( job._axis._z ), 0u );
            assignReadSlot( *pState );
            SortDispatch& dispatch            = _listSortDispatch.emplace_back();
            dispatch._pState                  = pState;
            dispatch._paramRow                = paramRow;
            _listBatchSorted[job._batchIndex] = SW_TRUE;
        }
        for ( size_t batchIndex = 0; batchIndex < listBatchMissing.size(); ++batchIndex )
        {
            if ( listBatchMissing[batchIndex] != SW_FALSE )
                _listBatchSorted[batchIndex] = SW_FALSE;
        }
    }

    void VFXGPUSimulation::prepareMeshEmitters( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch, const vector<GPUVFXMeshEmitter>& listMeshEmitter )
    {
        // 메시 방출기 — 인스턴스 버퍼 UAV 와 쓰기 PSO 가 있어야 그린다(없으면 한 번 알리고 그리지 않는다).
        const bool bMeshReady         = _frameInput._bMeshPipelineReady && _frameInput._instanceUAV != kInvalidDescriptorIndex && _frameInput._instanceBuffer != 0;
        const bool bReportMissingMesh = listMeshEmitter.empty() == false && bMeshReady == false && _bMeshPipelineMissingLogged == false;
        if ( bReportMissingMesh )
        {
            SW_LOG_ERROR_ID( "VFXGPUSimulation.GPUMeshParticlesNeed", "GPU mesh particles need the VFX mesh instance PSO and a GPUScene instance UAV - GPU mesh emitters are not drawn" );
            _bMeshPipelineMissingLogged = SW_TRUE;
        }
        for ( size_t meshIndex = 0; meshIndex < listMeshEmitter.size(); ++meshIndex )
        {
            const GPUVFXMeshEmitter& emitter       = listMeshEmitter[meshIndex];
            const bool               bEmitterReady = emitter._request._pDef != nullptr && emitter._pListInstanceSlot != nullptr;
            if ( bMeshReady == false || bEmitterReady == false )
                continue;
            EmitterState* pState = prepareEmitterState( device, emitter._request );
            if ( pState == nullptr )
                continue;
            const size_t           rangeIndex = listBatch.size() + meshIndex;
            const VFXGPULightRange lightRange = rangeIndex < _listLightRange.size() ? _listLightRange[rangeIndex] : VFXGPULightRange{};
            appendSimulation( *pState, emitter._request, emitter._world, false, lightRange );
            appendMeshInstance( *pState, emitter );
            assignReadSlot( *pState );
        }
    }

    void VFXGPUSimulation::releaseStaleEmitters( IRHIDevice& device )
    {
        // 재생 토큰을 이 상태만 들고 있으면(다시 재생 · 컴포넌트 제거) 놓는다. 한 프레임 숨은 재생(비활성 컴포넌트)은 토큰이 살아 있어 상태를 이어 쓴다.
        _listScratchStaleRunID.clear();
        for ( auto& [runID, pState] : _mapState )
        {
            const bool bRunEnded = pState->_pRunToken.use_count() <= 1;
            if ( pState->_bSeen == false && bRunEnded )
                _listScratchStaleRunID.push_back( runID );
        }
        for ( const uint64 runID : _listScratchStaleRunID )
        {
            EmitterState& state = *_mapState.find( runID )->second;
            state._stateBuffer.release( &device );
            state._particleBuffer.release( &device );
            state._argsBuffer.release( &device );
            _mapState.erase( runID );
        }
    }

    void VFXGPUSimulation::uploadParamBuffer( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch )
    {
        using Internal              = VFXGPUSimulationInternal;
        const bool bNothingToUpload = _listDispatch.empty() && _listMeshDispatch.empty() && _listSortDispatch.empty();
        if ( bNothingToUpload )
            return;
        const uint32 rowCount = static_cast<uint32>( _listParamWord.size() / Internal::kWordsPerRow );
        if ( _paramBuffer.ensureCapacity( &device, static_cast<uint32>( sizeof( uint32 ) ) * Internal::kWordsPerRow, rowCount,
                                          RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, nullptr ) == false )
        {
            SW_LOG_ERROR_ID( "VFXGPUSimulation.FailedCreateVFXSimulation", "Failed to create the VFX simulation parameter buffer (%# rows) - GPU-simulated emitters are not drawn", rowCount );
            _listDispatch.clear();
            _listMeshDispatch.clear();
            _listSortDispatch.clear();
            _listBatchDrawSource.assign( listBatch.size(), nullptr );
            _listBatchSorted.assign( listBatch.size(), SW_FALSE );
            return;
        }
        _paramBuffer.upload( &device, _listParamWord.data(), static_cast<uint32>( _listParamWord.size() * sizeof( uint32 ) ) );
    }

    VFXGPUSimulation::EmitterState* VFXGPUSimulation::prepareEmitterState( IRHIDevice& device, const VFXGPUEmitterRequest& request )
    {
        using Internal = VFXGPUSimulationInternal;
        // 열쇠는 재생 토큰의 주소다. 상태가 토큰을 들어(`EmitterState::_pRunToken`) 상태가 사는 동안 그 주소가 다른 재생에 다시 쓰이지 않는다.
        const uint64                                              runKey = reinterpret_cast<uint64>( request._pRunToken.get() );
        unordered_map<uint64, unique_ptr<EmitterState>>::iterator found  = _mapState.find( runKey );
        if ( found == _mapState.end() )
        {
            found                     = _mapState.emplace( runKey, sw::make_unique<EmitterState>( request._pDef, request._spawnScale ) ).first;
            found->second->_pRunToken = request._pRunToken;
        }
        EmitterState& state = *found->second;
        state._bSeen        = SW_TRUE;

        const VFXEmitterDef&     def         = state._clock.getDef();
        constexpr RHIBufferUsage kStateUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
        constexpr RHIBufferUsage kArgsUsage =
            RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::Raw | RHIBufferUsage::ShaderResource;
        // 고정 단어 뒤에 빈 인스턴스 칸 스택 · 살아 있는 칸 목록(풀 크기씩 — 셰이더가 재생 시작에 채운다), 다중 그룹 경로는 그 뒤에 그룹마다 한 칸(생존 수 · 시작 위치)이 더 든다.
        const uint32   argsWordCount = SW_VFX_ARGS_WORD_GROUP_FIRST( def._maxParticles ) + ( usesMultiGroup( def ) ? getGroupCount( def ) : 0u );
        vector<uint32> listInitialArgs( argsWordCount, 0u );
        listInitialArgs[0]       = GPUParticle::kVertexCount;
        const uint32 stateCount  = def._maxParticles * Internal::kStateHalfCount;
        const bool   bStateReady = state._stateBuffer.ensureCapacity( &device, Internal::kStateStride, stateCount, kStateUsage, false, true, nullptr ) &&
                                 state._stateBuffer._uav != kInvalidDescriptorIndex;
        const bool bParticleReady = state._particleBuffer.ensureCapacity( &device, static_cast<uint32>( sizeof( GPUParticle ) ), def._maxParticles, kStateUsage, true, true, nullptr ) &&
                                    state._particleBuffer._uav != kInvalidDescriptorIndex && state._particleBuffer._srv != kInvalidDescriptorIndex;
        // 인자 버퍼는 메시 인스턴스 쓰기가 살아 있는 수를 읽으므로 SRV 도 둔다.
        const bool bArgsReady = state._argsBuffer.ensureCapacity( &device, static_cast<uint32>( sizeof( uint32 ) ), argsWordCount, kArgsUsage, true, true, listInitialArgs.data() ) &&
                                state._argsBuffer._uav != kInvalidDescriptorIndex && state._argsBuffer._srv != kInvalidDescriptorIndex;
        const bool bBuffersReady = bStateReady && bParticleReady && bArgsReady;
        if ( bBuffersReady == false )
        {
            SW_LOG_ERROR_ID( "VFXGPUSimulation.FailedCreateGPUBuffers", "Failed to create the GPU buffers of VFX emitter '%#' (%# particles) - it is not drawn", def._name, def._maxParticles );
            return nullptr;
        }
        state._drawSource._particleSRV   = state._particleBuffer._srv;
        state._drawSource._particleCount = state._particleBuffer._capacityElements;
        state._drawSource._argsBuffer    = state._argsBuffer._buffer;
        return &state;
    }

    void VFXGPUSimulation::appendSimulation( EmitterState& state, const VFXGPUEmitterRequest& request, const float4x4& world, bool bRibbon, const VFXGPULightRange& lightRange )
    {
        using Internal         = VFXGPUSimulationInternal;
        const uint32 paramRow  = static_cast<uint32>( _listParamWord.size() / Internal::kWordsPerRow );
        const uint32 stepCount = appendParams( state, request, world, bRibbon, lightRange );
        // 밀린 단계가 없고 이미 한 번 돌았으면 렌더 입력과 인자가 그대로다 — 디스패치하지 않는다(매개변수 구간은 버린다).
        // 파티클 라이트가 있으면 그래도 돈다: 라이트는 월드 좌표로 쓰이고 구간의 위치(CPU 라이트 수)도 프레임마다 바뀌어 매 프레임 다시 써야 한다.
        const bool bLights   = lightRange._count > 0;
        const bool bUpToDate = stepCount == 0 && state._bNeedsReset == false;
        if ( bUpToDate && bLights == false )
        {
            _listParamWord.resize( static_cast<size_t>( paramRow ) * Internal::kWordsPerRow );
            return;
        }
        // 표 칸은 디스패치 번호다(`registerFrameResources` — 시뮬레이션하는 방출기의 이름 `ParticleState` · `Particles` · `ParticleArgs`).
        state._frameSlot    = static_cast<uint32>( _listDispatch.size() );
        state._bSimulated   = SW_TRUE;
        Dispatch& dispatch  = _listDispatch.emplace_back();
        dispatch._pState    = &state;
        dispatch._paramRow  = paramRow;
        dispatch._bLights   = bLights;
        dispatch._bReset    = state._bNeedsReset;
        dispatch._stepCount = stepCount;
        state._bNeedsReset  = SW_FALSE;
    }

    uint32 VFXGPUSimulation::appendParams( EmitterState& state, const VFXGPUEmitterRequest& request, const float4x4& world, bool bRibbon, const VFXGPULightRange& lightRange )
    {
        using Internal                       = VFXGPUSimulationInternal;
        const VFXEmitterDef&        def      = state._clock.getDef();
        const VFXDepthHistoryInput& depth    = getCollisionDepth( def._collision );
        const size_t                first    = _listParamWord.size();
        const float32               stepTime = constant::time::kDefaultFixedStepSeconds;
        // 수명 창보다 오래 밀린 단계(오래 숨었다 다시 실린 재생)는 일정만 옮긴다 — 그 입자는 이미 죽었다. 처음부터 디스패치당 상한씩 빨리감지 않는다.
        state._doneStepCount += state._clock.skipExpiredSteps( state._doneStepCount, request._stepCount, stepTime );
        const uint64 behind         = request._stepCount > state._doneStepCount ? request._stepCount - state._doneStepCount : 0u;
        const uint32 steps          = behind < SW_VFX_MAX_STEPS_PER_DISPATCH ? static_cast<uint32>( behind ) : SW_VFX_MAX_STEPS_PER_DISPATCH;
        const uint64 baseSeed       = HashUtil::mix64( request._seed );
        const uint32 collisionFlags = Internal::computeCollisionFlags( def._collision );
        const bool   bDepthReady    = collisionFlags != 0u && depth._texture != 0 && depth._uav != kInvalidDescriptorIndex;
        const uint32 flags          = ( state._bNeedsReset ? SW_VFX_FLAG_RESET : 0u ) | ( bRibbon ? SW_VFX_FLAG_RIBBON : 0u ) | collisionFlags |
                             ( bDepthReady ? SW_VFX_FLAG_DEPTH_READY : 0u );
        // 네이티브는 RW 텍스처 배열의 번호로, 에뮬은 슬롯 0 의 서수로 고른다(Bindless::loadRwTexture2D).
        const uint32 historyIndex = _frameInput._bNativeBindless ? static_cast<uint32>( depth._uav ) : 0u;

        Internal::appendRow( _listParamWord, def._maxParticles, steps, flags, GPUParticle::kVertexCount );
        Internal::appendRow( _listParamWord, Internal::lowWord( baseSeed ), Internal::highWord( baseSeed ), Internal::lowWord( HashUtil::kGoldenRatio64 ),
                             Internal::highWord( HashUtil::kGoldenRatio64 ) );
        Internal::appendRow( _listParamWord, Internal::lowWord( HashUtil::kSplitMixMultiplier0 ), Internal::highWord( HashUtil::kSplitMixMultiplier0 ),
                             Internal::lowWord( HashUtil::kSplitMixMultiplier1 ), Internal::highWord( HashUtil::kSplitMixMultiplier1 ) );
        Internal::appendRow( _listParamWord, BitUtil::floatToBits( def._velocityMin._x ), BitUtil::floatToBits( def._velocityMin._y ), BitUtil::floatToBits( def._velocityMin._z ),
                             BitUtil::floatToBits( def._lifetimeMin ) );
        Internal::appendRow( _listParamWord, BitUtil::floatToBits( def._velocityMax._x ), BitUtil::floatToBits( def._velocityMax._y ), BitUtil::floatToBits( def._velocityMax._z ),
                             BitUtil::floatToBits( def._lifetimeMax ) );
        Internal::appendRow( _listParamWord, BitUtil::floatToBits( def._gravity._x ), BitUtil::floatToBits( def._gravity._y ), BitUtil::floatToBits( def._gravity._z ), BitUtil::floatToBits( stepTime ) );
        // 커브 행 셋은 키를 붙인 뒤 채운다.
        Internal::appendRow( _listParamWord, 0u, 0u, 0u, 0u );
        Internal::appendRow( _listParamWord, 0u, 0u, 0u, 0u );
        Internal::appendRow( _listParamWord, 0u, 0u, 0u, 0u );
        Internal::appendRow( _listParamWord, BitUtil::floatToBits( def._collision._restitution ), BitUtil::floatToBits( def._collision._friction ),
                             BitUtil::floatToBits( VFXCollisionDef::kSurfaceSkin ),
                             BitUtil::floatToBits( Internal::usesRainOcclusionDepth( def._collision ) ? Internal::kRainOcclusionThickness : def._collision._depthThickness ) );
        Internal::appendRow( _listParamWord, depth._width, depth._height, historyIndex, 0u );
        Internal::appendRow( _listParamWord, BitUtil::floatToBits( depth._eye._x ), BitUtil::floatToBits( depth._eye._y ), BitUtil::floatToBits( depth._eye._z ), 0u );
        Internal::appendMatrixRows( _listParamWord, world );
        Internal::appendMatrixRows( _listParamWord, world.invert() );
        Internal::appendMatrixRows( _listParamWord, depth._viewProj );
        Internal::appendMatrixRows( _listParamWord, depth._viewProj.invert() );
        // 파티클 라이트 — 구간의 첫 라이트 번호(절대) · 슬롯 수 · 반경(방출기 배율을 곱한 월드 반경) · 세기. 슬롯 수 0 이면 셰이더가 쓰지 않는다.
        if ( lightRange._count > 0 )
        {
            const VFXLightDef& light = def._light;
            Internal::appendRow( _listParamWord, _frameInput._lightBase + lightRange._first, lightRange._count, BitUtil::floatToBits( light._radius * world.getMaximumAxisScale() ),
                                 BitUtil::floatToBits( light._intensity ) );
        }
        else
        {
            Internal::appendRow( _listParamWord, 0u, 0u, 0u, 0u );
        }
        // 사용자 값 — 렌더 출력에 곱한다(`VFXCPUSimulation::writeParticles` 와 같은 식).
        const VFXEmitterUserValues& userValues = request._userValues;
        Internal::appendRow( _listParamWord, BitUtil::floatToBits( userValues._tint._x ), BitUtil::floatToBits( userValues._tint._y ), BitUtil::floatToBits( userValues._tint._z ), BitUtil::floatToBits( userValues._tint._w ) );
        Internal::appendRow( _listParamWord, BitUtil::floatToBits( userValues._sizeScale ), 0u, 0u, 0u );
        Internal::appendRow( _listParamWord, BitUtil::floatToBits( def._spawnHalfExtents._x ), BitUtil::floatToBits( def._spawnHalfExtents._y ), BitUtil::floatToBits( def._spawnHalfExtents._z ), 0u );
        Internal::appendMatrixRows( _listParamWord, request._emitterToSimulation ); // 새 입자를 시뮬레이션 공간으로(`VFXCPUSimulation::spawnParticles` 와 같은 식)
        static_assert( SW_VFX_PARAM_ROW_FIRST_STEP - SW_VFX_PARAM_ROW_EMITTER_TO_SIMULATION == SW_VFX_PARAM_ROW_INVERSE_WORLD - SW_VFX_PARAM_ROW_WORLD && SW_VFX_PARAM_ROW_EMITTER_TO_SIMULATION == SW_VFX_PARAM_ROW_SPAWN_BOX + 1u && SW_VFX_PARAM_ROW_SPAWN_BOX == SW_VFX_PARAM_ROW_USER_SIZE + 1u && SW_VFX_PARAM_ROW_USER_SIZE == 30u && SW_VFX_PARAM_ROW_USER_TINT == 29u && SW_VFX_PARAM_ROW_LIGHT == 28u, "the header rows above must match vfx_simulate.hlsli" );

        // 일정 — 게임 스레드와 같은 시계를 같은 단계 수만큼 돌린다.
        for ( uint32 stepIndex = 0; stepIndex < steps; ++stepIndex )
        {
            const VFXSpawnStep spawn = state._clock.spawnStep( stepTime );
            Internal::appendRow( _listParamWord, spawn._count, Internal::lowWord( spawn._firstParticleIndex ), Internal::highWord( spawn._firstParticleIndex ), 0u );
        }
        state._doneStepCount += steps;
        _pendingStepCount = MathUtil::max( _pendingStepCount, behind - steps );

        const uint32 sizeRow  = Internal::appendCurve( _listParamWord, first, def._sizeOverLife );
        const uint32 redRow   = Internal::appendCurve( _listParamWord, first, def._colorOverLife._red );
        const uint32 greenRow = Internal::appendCurve( _listParamWord, first, def._colorOverLife._green );
        const uint32 blueRow  = Internal::appendCurve( _listParamWord, first, def._colorOverLife._blue );
        const uint32 alphaRow = Internal::appendCurve( _listParamWord, first, def._colorOverLife._alpha );
        Internal::setWord( _listParamWord, first, SW_VFX_PARAM_ROW_CURVE_SIZE_RED, 0, sizeRow );
        Internal::setWord( _listParamWord, first, SW_VFX_PARAM_ROW_CURVE_SIZE_RED, 1, static_cast<uint32>( def._sizeOverLife._listKey.size() ) );
        Internal::setWord( _listParamWord, first, SW_VFX_PARAM_ROW_CURVE_SIZE_RED, 2, redRow );
        Internal::setWord( _listParamWord, first, SW_VFX_PARAM_ROW_CURVE_SIZE_RED, 3, static_cast<uint32>( def._colorOverLife._red._listKey.size() ) );
        Internal::setWord( _listParamWord, first, SW_VFX_PARAM_ROW_CURVE_GREEN_BLUE, 0, greenRow );
        Internal::setWord( _listParamWord, first, SW_VFX_PARAM_ROW_CURVE_GREEN_BLUE, 1, static_cast<uint32>( def._colorOverLife._green._listKey.size() ) );
        Internal::setWord( _listParamWord, first, SW_VFX_PARAM_ROW_CURVE_GREEN_BLUE, 2, blueRow );
        Internal::setWord( _listParamWord, first, SW_VFX_PARAM_ROW_CURVE_GREEN_BLUE, 3, static_cast<uint32>( def._colorOverLife._blue._listKey.size() ) );
        Internal::setWord( _listParamWord, first, SW_VFX_PARAM_ROW_CURVE_ALPHA, 0, alphaRow );
        Internal::setWord( _listParamWord, first, SW_VFX_PARAM_ROW_CURVE_ALPHA, 1, static_cast<uint32>( def._colorOverLife._alpha._listKey.size() ) );
        return steps;
    }

    void VFXGPUSimulation::appendMeshInstance( const EmitterState& state, const GPUVFXMeshEmitter& emitter )
    {
        using Internal                 = VFXGPUSimulationInternal;
        const uint32          poolSize = state._clock.getDef()._maxParticles;
        const vector<uint32>& listSlot = *emitter._pListInstanceSlot;
        const uint32          paramRow = static_cast<uint32>( _listParamWord.size() / Internal::kWordsPerRow );
        Internal::appendRow( _listParamWord, poolSize, 0u, BitUtil::floatToBits( emitter._meshRadius ), BitUtil::floatToBits( emitter._sizeScale ) );
        Internal::appendMatrixRows( _listParamWord, emitter._world );
        static_assert( SW_VFX_MESH_ROW_FIRST_SLOT == 5u, "the mesh header rows above must match vfx_simulate.hlsli" );
        // 입자 인스턴스 칸(0 ~ 풀 - 1, 입자가 수명 내내 쥔다 — vfx_simulate.hlsl 의 빈 칸 스택)마다 GPUScene 칸 — 행 하나에 넷. 배치 항목 수는 풀 크기와 같다(`VFXComponent::rebuildMeshEmitters`). 모자란 칸은 쓰지 않는다.
        const uint32 slotRowCount = ( poolSize + Internal::kWordsPerRow - 1u ) / Internal::kWordsPerRow;
        for ( uint32 slotRow = 0; slotRow < slotRowCount; ++slotRow )
        {
            uint32 arrSlot[Internal::kWordsPerRow]{ SW_VFX_INVALID_SLOT, SW_VFX_INVALID_SLOT, SW_VFX_INVALID_SLOT, SW_VFX_INVALID_SLOT };
            for ( uint32 wordIndex = 0; wordIndex < Internal::kWordsPerRow; ++wordIndex )
            {
                const uint32 poolIndex = slotRow * Internal::kWordsPerRow + wordIndex;
                if ( poolIndex < listSlot.size() )
                    arrSlot[wordIndex] = listSlot[poolIndex];
            }
            Internal::appendRow( _listParamWord, arrSlot[0], arrSlot[1], arrSlot[2], arrSlot[3] );
        }
        MeshDispatch& dispatch = _listMeshDispatch.emplace_back();
        dispatch._pState       = &state;
        dispatch._paramRow     = paramRow;
        dispatch._groupCount   = ( poolSize + SW_VFX_MESH_INSTANCE_THREAD_COUNT - 1u ) / SW_VFX_MESH_INSTANCE_THREAD_COUNT;
        for ( const uint32 slot : listSlot )
        {
            if ( slot == SW_VFX_INVALID_SLOT )
                continue;
            dispatch._firstInstanceSlot = slot;
            break;
        }
    }

    void VFXGPUSimulation::collectMeshProbeSources( vector<MeshProbeSource>& outListSource ) const
    {
        outListSource.clear();
        for ( const MeshDispatch& dispatch : _listMeshDispatch )
        {
            if ( dispatch._firstInstanceSlot != SW_VFX_INVALID_SLOT )
                outListSource.push_back( MeshProbeSource{ dispatch._pState->_argsBuffer._buffer, dispatch._firstInstanceSlot } );
        }
    }

    bool VFXGPUSimulation::usesMultiGroup( const VFXEmitterDef& def )
    {
        return def._maxParticles >= SW_VFX_MULTI_GROUP_MIN_POOL;
    }

    uint32 VFXGPUSimulation::getGroupCount( const VFXEmitterDef& def )
    {
        return ( def._maxParticles + SW_VFX_SIMULATE_THREAD_COUNT - 1u ) / SW_VFX_SIMULATE_THREAD_COUNT;
    }

    void VFXGPUSimulation::assignReadSlot( EmitterState& state )
    {
        // 시뮬레이션한 방출기는 디스패치 칸을 그대로 읽는다. 한 방출기를 여러 정렬 일(뷰마다)과 메시 쓰기가 읽어도 칸은 하나다.
        if ( state._frameSlot != constant::invalid_index::kUint32 )
            return;
        state._frameSlot = _readSlotCount;
        ++_readSlotCount;
    }

    RenderGraphBufferUAV VFXGPUSimulation::getLightBufferUAV()
    {
        static const hashed_string kName( "LightBuffer" );
        return RenderGraphBufferUAV{ kName };
    }

    RenderGraphBufferUAV VFXGPUSimulation::getInstanceBufferUAV()
    {
        static const hashed_string kName( "InstanceBuffer" );
        return RenderGraphBufferUAV{ kName };
    }

    bool VFXGPUSimulation::writesParticleLights() const
    {
        const bool bLightBufferReady = _frameInput._lightUAV != kInvalidDescriptorIndex && _frameInput._lightBuffer != 0;
        bool       bAnyLights        = false;
        for ( const Dispatch& dispatch : _listDispatch )
        {
            bAnyLights = bAnyLights || dispatch._bLights;
        }
        return bLightBufferReady && bAnyLights;
    }

    void VFXGPUSimulation::registerFrameResources( FrameBufferTable& table ) const
    {
        // 등록 접근은 프레임 시작이자 끝의 상태다(그래프가 끝에 되돌린다): 상태는 다음 디스패치만 읽어 UAV, 렌더 입력은 투명 패스의 정점 셰이더(t11)가 읽어 셰이더 읽기,
        // 인자는 투명 패스의 간접 그리기 상태다(메시 되읽기 `VFXGPUMeshReadback` 도 그 상태로 되돌린다). 칸은 `appendSimulation` · `assignReadSlot` 이 정했다.
        for ( const Dispatch& dispatch : _listDispatch )
        {
            const EmitterState& state = *dispatch._pState;
            table.registerBuffer( VFXFrameResource::state(), state._frameSlot, state._stateBuffer, RenderGraphAccess::UnorderedAccess );
            table.registerBuffer( VFXFrameResource::particles(), state._frameSlot, state._particleBuffer, RenderGraphAccess::ShaderRead );
            table.registerBuffer( VFXFrameResource::args(), state._frameSlot, state._argsBuffer, RenderGraphAccess::IndirectArgs );
        }
        // 시뮬레이션하지 않고 읽기만 하는 방출기(밀린 단계가 없는 프레임의 정렬 · 메시 쓰기) — 한 방출기를 여러 정렬 일이 읽어도 한 번만 등록한다.
        const auto registerReads = [&table]( const EmitterState& state )
        {
            if ( state._bSimulated != SW_FALSE || table.findBuffer( VFXFrameResource::idleParticles(), state._frameSlot ) != 0 )
                return;
            table.registerBuffer( VFXFrameResource::idleParticles(), state._frameSlot, state._particleBuffer, RenderGraphAccess::ShaderRead );
            table.registerBuffer( VFXFrameResource::idleArgs(), state._frameSlot, state._argsBuffer, RenderGraphAccess::IndirectArgs );
        };
        for ( const SortDispatch& dispatch : _listSortDispatch )
        {
            registerReads( *dispatch._pState );
        }
        for ( const MeshDispatch& dispatch : _listMeshDispatch )
        {
            registerReads( *dispatch._pState );
        }
        if ( _listSortDispatch.empty() == false )
            table.registerBuffer( VFXFrameResource::order(), 0, _orderBuffer, RenderGraphAccess::ShaderRead );
    }

    bool VFXGPUSimulation::hasDeclaredReads( const RenderGraphPassContext& context, const EmitterState& state )
    {
        const bool          bSimulated = state._bSimulated != SW_FALSE;
        const hashed_string particles  = bSimulated ? VFXFrameResource::particles() : VFXFrameResource::idleParticles();
        const hashed_string args       = bSimulated ? VFXFrameResource::args() : VFXFrameResource::idleArgs();
        return context.getBuffer( RenderGraphBufferSRV{ particles }, state._frameSlot ) != 0 && context.getBuffer( RenderGraphBufferSRV{ args }, state._frameSlot ) != 0;
    }

    void VFXGPUSimulation::recordEmitterPasses( const RenderGraphPassContext& context, const Dispatch& dispatch ) const
    {
        IRHIPassCommandList& cmd   = *context.getCommandList();
        const EmitterState&  state = *dispatch._pState;
        const uint32         slot  = state._frameSlot;
        // 루트 상수 둘: 매개변수 구간의 첫 행, 패스 번호 | 단계 번호 << SW_VFX_PASS_STEP_SHIFT(vfx_simulate.hlsl).
        const auto recordPass = [&]( uint32 pass, uint32 stepIndex, uint32 groupCount )
        {
            const uint32 arrRoot[2]{ dispatch._paramRow, pass | ( stepIndex << SW_VFX_PASS_STEP_SHIFT ) };
            cmd.setComputeRootConstants( 0, 2, arrRoot );
            cmd.dispatchCompute( groupCount, 1, 1 );
        };
        if ( usesMultiGroup( state._clock.getDef() ) == false )
        {
            recordPass( SW_VFX_PASS_SINGLE, 0u, 1u );
            return;
        }
        // 다중 그룹: 패스마다 앞 패스가 쓴 상태 · 인자 버퍼를 읽는다 - 패스 사이에 선언한 UAV 핸들의 장벽을 둔다(같은 상태의 연속이라 전이로는 알 수 없다).
        // 한 단계 = 적분 -> 접두사 합 -> 압축.
        const uint32 groupCount = getGroupCount( state._clock.getDef() );
        const auto   barrier    = [&]()
        {
            context.uavBarrier( RenderGraphBufferUAV{ VFXFrameResource::state() }, slot );
            context.uavBarrier( RenderGraphBufferUAV{ VFXFrameResource::args() }, slot );
        };
        if ( dispatch._bReset )
        {
            recordPass( SW_VFX_PASS_BEGIN, 0u, 1u );
            barrier();
        }
        for ( uint32 stepIndex = 0; stepIndex < dispatch._stepCount; ++stepIndex )
        {
            recordPass( SW_VFX_PASS_INTEGRATE, stepIndex, groupCount );
            barrier();
            recordPass( SW_VFX_PASS_SCAN, stepIndex, 1u );
            barrier();
            recordPass( SW_VFX_PASS_COMPACT, stepIndex, groupCount );
            barrier();
        }
        recordPass( SW_VFX_PASS_OUTPUT, 0u, groupCount );
        context.uavBarrier( RenderGraphBufferUAV{ VFXFrameResource::particles() }, slot );
    }

    void VFXGPUSimulation::recordSimulation( const RenderGraphPassContext& context, RHIPipelineStateHandle pso ) const
    {
        const bool bNothingToDispatch = _listDispatch.empty() || pso == 0 || _paramBuffer.isValid() == false || context.getCommandList() == nullptr;
        if ( bNothingToDispatch )
            return;
        SW_PROFILE_SCOPE( "RT.VFX.Simulate" );
        IRHIPassCommandList& cmd = *context.getCommandList();
        cmd.setComputePipelineState( pso );
        // 깊이 히스토리 — 지난 프레임 그 뷰가 남긴 깊이를 옮긴 R32 실수 텍스처. RW 텍스처로 읽는다(에뮬 백엔드의 컴퓨트는 샘플 텍스처를 읽지 못한다).
        // 생산자(`FrameGraphExecutor::recordDepthHistory` — 입력을 채운 판정 `canCopyDepthHistory` 와 같다)가 RW 텍스처 상태로 넘겨 두었다.
        // 비 가림 맵을 읽는 방출기는 그 맵의 사본을 같은 자리에 건다(`getCollisionDepth`).
        // 전이는 그래프 레벨 콜백이 냈다(`registerFrameResources` 가 등록한 칸). 라이트 버퍼는 쓰는 프레임에만 등록했다(`writesParticleLights` — 등록 조건이 곧 실행 조건).
        const bool bLightBufferReady = context.isRegistered( getLightBufferUAV() );
        for ( const Dispatch& dispatch : _listDispatch )
        {
            const EmitterState& state = *dispatch._pState;
            // 선언 · 등록이 없으면 그래프가 전이를 내지 않았다 — 디스패치하지 않는다(오류는 문맥이 남겼다). 파이프라인 XML 의 `VFXSimulation` 이 세 이름을 출력으로 적어야 한다.
            const bool bDeclared = context.getBuffer( RenderGraphBufferUAV{ VFXFrameResource::state() }, state._frameSlot ) != 0 &&
                                   context.getBuffer( RenderGraphBufferUAV{ VFXFrameResource::particles() }, state._frameSlot ) != 0 &&
                                   context.getBuffer( RenderGraphBufferUAV{ VFXFrameResource::args() }, state._frameSlot ) != 0;
            if ( bDeclared == false )
                continue;
            // g_VFXParams(t0) / g_VFXStateRW(u0) / g_VFXParticlesRW(u1) / g_VFXArgsRW(u2) / 깊이 히스토리(RW 텍스처 슬롯 0) / 루트 상수 g_VFXParamBase.
            // vfx_simulate.hlsl 레지스터와 1:1 이다.
            cmd.bindComputeShaderResource( _paramBuffer._srv, 0 );
            cmd.bindComputeUAV( state._stateBuffer._uav, 0 );
            cmd.bindComputeUAV( state._particleBuffer._uav, 1 );
            cmd.bindComputeUAV( state._argsBuffer._uav, 2 );
            // 라이트 버퍼(u3) — 라이트를 쓰는 방출기만 건다. 매개변수의 라이트 행이 슬롯 수 0 이면 셰이더는 읽지도 쓰지도 않는다.
            const bool bWritesLights = dispatch._bLights && bLightBufferReady;
            if ( bWritesLights )
                cmd.bindComputeUAV( _frameInput._lightUAV, 3 );
            const VFXDepthHistoryInput& depth = getCollisionDepth( state._clock.getDef()._collision );
            if ( depth._texture != 0 && depth._uav != kInvalidDescriptorIndex )
                cmd.bindComputeUAV( depth._uav, shaderslot::kComputeTextureUAV0 );
            recordEmitterPasses( context, dispatch );
            if ( bWritesLights )
                context.uavBarrier( getLightBufferUAV() ); // 다음 방출기가 같은 버퍼의 다른 슬롯 범위를 쓴다 — 범위가 겹치지 않아도 상태 추적을 맞춘다
        }
    }

    void VFXGPUSimulation::recordSort( const RenderGraphPassContext& context, RHIPipelineStateHandle pso ) const
    {
        // 정렬 — 방출기(ViewDepth 는 방출기 × 뷰)마다 워크그룹 하나가 살아 있는 입자를 정렬해 순서 버퍼의 그 구간에 쓴다(vfx_particle_sort.hlsl).
        const bool bSortReady = _listSortDispatch.empty() == false && pso != 0 && _orderBuffer.isValid() && _paramBuffer.isValid() && context.getCommandList() != nullptr;
        if ( bSortReady == false || context.getBuffer( RenderGraphBufferUAV{ VFXFrameResource::order() } ) == 0 )
            return;
        SW_PROFILE_SCOPE( "RT.VFX.SortParticles" );
        IRHIPassCommandList& cmd = *context.getCommandList();
        cmd.setComputePipelineState( pso );
        for ( const SortDispatch& dispatch : _listSortDispatch )
        {
            const EmitterState& state = *dispatch._pState;
            if ( hasDeclaredReads( context, state ) == false )
                continue;
            // g_VFXParams(t0) / g_VFXParticles(t1) / g_VFXArgs(t2) / g_VFXOrderRW(u0) / 루트 상수 g_VFXSortParamBase. 레지스터와 1:1 이다.
            cmd.bindComputeShaderResource( _paramBuffer._srv, 0 );
            cmd.bindComputeShaderResource( state._particleBuffer._srv, 1 );
            cmd.bindComputeShaderResource( state._argsBuffer._srv, 2 );
            cmd.bindComputeUAV( _orderBuffer._uav, 0 );
            cmd.setComputeRootConstants( 0, 1, &dispatch._paramRow );
            cmd.dispatchCompute( 1, 1, 1 );
        }
    }

    void VFXGPUSimulation::recordMeshInstances( const RenderGraphPassContext& context, RHIPipelineStateHandle pso ) const
    {
        // 메시 입자 — 렌더 입력을 인스턴스 칸으로 옮긴다. 인스턴스 애니메이션 · 컬링이 이어서 이 버퍼를 읽는다(그 앞 전이도 그래프가 낸다).
        const bool bMeshWrites =
            _listMeshDispatch.empty() == false && pso != 0 && _frameInput._instanceUAV != kInvalidDescriptorIndex && _paramBuffer.isValid() && context.getCommandList() != nullptr;
        if ( bMeshWrites == false || context.getBuffer( getInstanceBufferUAV() ) == 0 )
            return;
        IRHIPassCommandList& cmd = *context.getCommandList();
        cmd.setComputePipelineState( pso );
        for ( const MeshDispatch& dispatch : _listMeshDispatch )
        {
            const EmitterState& state = *dispatch._pState;
            if ( hasDeclaredReads( context, state ) == false )
                continue;
            // g_VFXParams(t0) / g_VFXParticles(t1) / g_VFXArgs(t2) / g_VFXInstancesRW(u0) / 루트 상수 g_VFXMeshParamBase. vfx_mesh_instance.hlsl 과 1:1 이다.
            cmd.bindComputeShaderResource( _paramBuffer._srv, 0 );
            cmd.bindComputeShaderResource( state._particleBuffer._srv, 1 );
            cmd.bindComputeShaderResource( state._argsBuffer._srv, 2 );
            cmd.bindComputeUAV( _frameInput._instanceUAV, 0 );
            cmd.setComputeRootConstants( 0, 1, &dispatch._paramRow );
            cmd.dispatchCompute( dispatch._groupCount, 1, 1 );
            context.uavBarrier( getInstanceBufferUAV() ); // 다음 방출기가 같은 버퍼의 다른 칸을 쓴다 — 칸이 겹치지 않아도 상태 추적을 맞춘다
        }
    }

    const VFXGPUSimulation::DrawSource* VFXGPUSimulation::findDrawSource( uint32 batchIndex ) const
    {
        return batchIndex < _listBatchDrawSource.size() ? _listBatchDrawSource[batchIndex] : nullptr;
    }

    void VFXGPUSimulation::release( IRHIDevice* pDevice )
    {
        for ( auto& [runID, pState] : _mapState )
        {
            (void)runID;
            pState->_stateBuffer.release( pDevice );
            pState->_particleBuffer.release( pDevice );
            pState->_argsBuffer.release( pDevice );
        }
        _mapState.clear();
        _paramBuffer.release( pDevice );
        _orderBuffer.release( pDevice );
        _listSortDispatch.clear();
        _listBatchState.clear();
        _listBatchSorted.clear();
        _listDispatch.clear();
        _listMeshDispatch.clear();
        _listBatchDrawSource.clear();
        _listParamWord.clear();
        _frameInput = VFXSimulationFrameInput{};
    }
} // namespace sw
