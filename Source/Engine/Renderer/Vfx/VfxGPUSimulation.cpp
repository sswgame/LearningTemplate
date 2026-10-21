#include "pch.h"

#include "Engine/Renderer/Vfx/VfxGPUSimulation.h"

#include "Core/Common/HashUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"
#include "Engine/Vfx/VfxEffectDef.h"

#include "vfxsimulate.hlsli"

namespace sw
{
    SW_LOG_CALLER( "VfxGPUSimulation" );

    namespace
    {
        struct VfxGPUSimulationInternal
        {
            static constexpr uint32 kWordsPerRow = 4;
            /** @brief 입자 상태 원소 하나의 바이트 수입니다(vfxsimulate.hlsl 의 VfxParticleState). */
            static constexpr uint32 kStateStride = static_cast<uint32>( sizeof( float4 ) ) * SW_VFX_STATE_FLOAT4_COUNT;
            /** @brief 입자 상태는 두 반쪽이다 — 단계마다 한쪽에서 읽어 다른 쪽에 압축한다. */
            static constexpr uint32 kStateHalfCount = 2;
            static_assert( SW_VFX_ARGS_WORD_ALIVE * sizeof( uint32 ) >= sizeof( RHIDrawIndirectCommand ), "the alive count must sit after the draw arguments" );

            static uint32 toBits( float32 value )
            {
                uint32 bits = 0;
                Memory::copy( &bits, &value, sizeof( bits ) );
                return bits;
            }

            static uint32 lowWord( uint64 value ) { return static_cast<uint32>( value ); }
            static uint32 highWord( uint64 value ) { return static_cast<uint32>( value >> 32 ); }

            static void appendRow( vector<uint32>& inoutListWord, uint32 x, uint32 y, uint32 z, uint32 w )
            {
                inoutListWord.push_back( x );
                inoutListWord.push_back( y );
                inoutListWord.push_back( z );
                inoutListWord.push_back( w );
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
                    appendRow( inoutListWord, toBits( key._time ), toBits( key._value ), toBits( key._arriveTangent ), toBits( key._leaveTangent ) );
                    appendRow( inoutListWord, constantWord, linearWord, 0u, 0u );
                }
                return firstRow;
            }

            /** @brief 행 @p row 의 단어 @p word 를 고칩니다(구간 첫 단어 기준). */
            static void setWord( vector<uint32>& inoutListWord, size_t sectionFirstWord, uint32 row, uint32 word, uint32 value )
            {
                inoutListWord[sectionFirstWord + static_cast<size_t>( row ) * kWordsPerRow + word] = value;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VfxGPUSimulation::EmitterState::EmitterState( shared_ptr<const VfxEmitterDef> pDef )
        : _clock{ std::move( pDef ) }
        , _doneStepCount{ 0 }
        , _stateBuffer{}
        , _particleBuffer{}
        , _argsBuffer{}
        , _drawSource{}
        , _bNeedsReset{ true }
        , _bSeen{ false }
    {
    }

    VfxGPUSimulation::VfxGPUSimulation()
        : _mapState{}
        , _listDispatch{}
        , _listBatchDrawSource{}
        , _listParamWord{}
        , _listScratchStaleRunID{}
        , _paramBuffer{}
        , _bPipelineMissingLogged{ false }
    {
    }

    VfxGPUSimulation::~VfxGPUSimulation() = default;

    void VfxGPUSimulation::prepareFrame( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch, bool bPipelineReady )
    {
        using Internal = VfxGPUSimulationInternal;
        _listDispatch.clear();
        _listParamWord.clear();
        _listBatchDrawSource.assign( listBatch.size(), nullptr );
        for ( auto& [runID, pState] : _mapState )
        {
            (void)runID;
            pState->_bSeen = false;
        }

        bool bAnyGPUBatch = false;
        for ( const GPUParticleBatch& batch : listBatch )
        {
            bAnyGPUBatch = bAnyGPUBatch || batch.findGPURequest() != nullptr;
        }
        const bool bReportMissingPipeline = bAnyGPUBatch && bPipelineReady == false && _bPipelineMissingLogged == false;
        if ( bReportMissingPipeline )
        {
            // 대신 CPU 로 그리지 않는다 — 시뮬레이션 대상은 데이터와 빌드가 정했고, 바꿔 그리면 CPU 와 GPU 를 비교하는 화면이 거짓이 된다.
            SW_LOG_ERROR( "The VFX simulation compute PSO is missing - GPU-simulated emitters are not drawn" );
            _bPipelineMissingLogged = true;
        }

        if ( bPipelineReady )
        {
            SW_PROFILE_SCOPE( "RT.Vfx.SimulationPrepare" );
            for ( uint32 batchIndex = 0; batchIndex < static_cast<uint32>( listBatch.size() ); ++batchIndex )
            {
                const VfxGPUEmitterRequest* pRequest = listBatch[batchIndex].findGPURequest();
                if ( pRequest == nullptr || pRequest->_pDef == nullptr )
                    continue;
                unordered_map<uint64, unique_ptr<EmitterState>>::iterator found = _mapState.find( pRequest->_runID );
                if ( found == _mapState.end() )
                    found = _mapState.emplace( pRequest->_runID, sw::make_unique<EmitterState>( pRequest->_pDef ) ).first;
                EmitterState& state = *found->second;
                state._bSeen        = true;

                const VfxEmitterDef&     def         = state._clock.getDef();
                constexpr RHIBufferUsage kStateUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
                constexpr RHIBufferUsage kArgsUsage  = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::Raw | RHIBufferUsage::ShaderResource;
                uint32                   arrInitialArgs[SW_VFX_ARGS_WORD_COUNT]{};
                arrInitialArgs[0]      = GPUParticle::kVertexCount;
                const bool bStateReady = state._stateBuffer.ensureCapacity( &device, Internal::kStateStride, def._maxParticles * Internal::kStateHalfCount, kStateUsage, false, true, nullptr ) &&
                                         state._stateBuffer._uav != kInvalidDescriptorIndex;
                const bool bParticleReady = state._particleBuffer.ensureCapacity( &device, static_cast<uint32>( sizeof( GPUParticle ) ), def._maxParticles, kStateUsage, true, true, nullptr ) &&
                                            state._particleBuffer._uav != kInvalidDescriptorIndex && state._particleBuffer._srv != kInvalidDescriptorIndex;
                const bool bArgsReady = state._argsBuffer.ensureCapacity( &device, static_cast<uint32>( sizeof( uint32 ) ), SW_VFX_ARGS_WORD_COUNT, kArgsUsage, false, true, arrInitialArgs ) &&
                                        state._argsBuffer._uav != kInvalidDescriptorIndex;
                const bool bBuffersReady = bStateReady && bParticleReady && bArgsReady;
                if ( bBuffersReady == false )
                {
                    SW_LOG_ERROR( "Failed to create the GPU buffers of VFX emitter '%#' (%# particles) - it is not drawn", def._name, def._maxParticles );
                    continue;
                }
                state._drawSource._particleSrv = state._particleBuffer._srv;
                state._drawSource._argsBuffer  = state._argsBuffer._buffer;

                const uint32 paramRow  = static_cast<uint32>( _listParamWord.size() / Internal::kWordsPerRow );
                const uint32 stepCount = appendParams( state, listBatch[batchIndex], pRequest->_seed, pRequest->_stepCount );
                // 밀린 단계가 없고 이미 한 번 돌았으면 렌더 입력과 인자가 그대로다 — 디스패치하지 않는다(매개변수 구간은 버린다).
                if ( stepCount == 0 && state._bNeedsReset == false )
                {
                    _listParamWord.resize( static_cast<size_t>( paramRow ) * Internal::kWordsPerRow );
                }
                else
                {
                    Dispatch& dispatch = _listDispatch.emplace_back();
                    dispatch._pState   = &state;
                    dispatch._paramRow = paramRow;
                    state._bNeedsReset = false;
                }
                _listBatchDrawSource[batchIndex] = &state._drawSource;
            }
        }

        // 이번 프레임에 없는 재생은 놓는다(멈춤 · 다시 재생 · 컴포넌트 제거).
        _listScratchStaleRunID.clear();
        for ( auto& [runID, pState] : _mapState )
        {
            if ( pState->_bSeen == false )
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

        if ( _listDispatch.empty() )
            return;
        const uint32 rowCount = static_cast<uint32>( _listParamWord.size() / Internal::kWordsPerRow );
        if ( _paramBuffer.ensureCapacity( &device, static_cast<uint32>( sizeof( uint32 ) ) * Internal::kWordsPerRow, rowCount,
                                          RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, nullptr ) == false )
        {
            SW_LOG_ERROR( "Failed to create the VFX simulation parameter buffer (%# rows) - GPU-simulated emitters are not drawn", rowCount );
            _listDispatch.clear();
            _listBatchDrawSource.assign( listBatch.size(), nullptr );
            return;
        }
        _paramBuffer.upload( &device, _listParamWord.data(), static_cast<uint32>( _listParamWord.size() * sizeof( uint32 ) ) );
    }

    uint32 VfxGPUSimulation::appendParams( EmitterState& state, const GPUParticleBatch& batch, uint64 seed, uint64 targetStepCount )
    {
        using Internal                = VfxGPUSimulationInternal;
        const VfxEmitterDef& def      = state._clock.getDef();
        const size_t         first    = _listParamWord.size();
        const uint64         behind   = targetStepCount > state._doneStepCount ? targetStepCount - state._doneStepCount : 0u;
        const uint32         steps    = behind < SW_VFX_MAX_STEPS_PER_DISPATCH ? static_cast<uint32>( behind ) : SW_VFX_MAX_STEPS_PER_DISPATCH;
        const uint64         baseSeed = HashUtil::mix64( seed );
        const uint32         flags    = ( state._bNeedsReset ? SW_VFX_FLAG_RESET : 0u ) | ( batch._bRibbon == SW_TRUE ? SW_VFX_FLAG_RIBBON : 0u );
        const float32        stepTime = VfxEmitterClock::kFixedStepSeconds;

        Internal::appendRow( _listParamWord, def._maxParticles, steps, flags, GPUParticle::kVertexCount );
        Internal::appendRow( _listParamWord, Internal::lowWord( baseSeed ), Internal::highWord( baseSeed ), Internal::lowWord( HashUtil::kGoldenRatio64 ),
                             Internal::highWord( HashUtil::kGoldenRatio64 ) );
        Internal::appendRow( _listParamWord, Internal::lowWord( HashUtil::kSplitMixMultiplier0 ), Internal::highWord( HashUtil::kSplitMixMultiplier0 ),
                             Internal::lowWord( HashUtil::kSplitMixMultiplier1 ), Internal::highWord( HashUtil::kSplitMixMultiplier1 ) );
        Internal::appendRow( _listParamWord, Internal::toBits( def._velocityMin._x ), Internal::toBits( def._velocityMin._y ), Internal::toBits( def._velocityMin._z ),
                             Internal::toBits( def._lifetimeMin ) );
        Internal::appendRow( _listParamWord, Internal::toBits( def._velocityMax._x ), Internal::toBits( def._velocityMax._y ), Internal::toBits( def._velocityMax._z ),
                             Internal::toBits( def._lifetimeMax ) );
        Internal::appendRow( _listParamWord, Internal::toBits( def._gravity._x ), Internal::toBits( def._gravity._y ), Internal::toBits( def._gravity._z ), Internal::toBits( stepTime ) );
        // 커브 행 셋은 키를 붙인 뒤 채운다.
        Internal::appendRow( _listParamWord, 0u, 0u, 0u, 0u );
        Internal::appendRow( _listParamWord, 0u, 0u, 0u, 0u );
        Internal::appendRow( _listParamWord, 0u, 0u, 0u, 0u );
        static_assert( SW_VFX_PARAM_ROW_FIRST_STEP == 9u, "the header rows above must match vfxsimulate.hlsli" );

        // 일정 — 게임 스레드와 같은 시계를 같은 단계 수만큼 돌린다.
        for ( uint32 stepIndex = 0; stepIndex < steps; ++stepIndex )
        {
            const VfxSpawnStep spawn = state._clock.spawnStep( stepTime );
            Internal::appendRow( _listParamWord, spawn._count, Internal::lowWord( spawn._firstParticleIndex ), Internal::highWord( spawn._firstParticleIndex ), 0u );
        }
        state._doneStepCount += steps;

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

    void VfxGPUSimulation::recordDispatches( IRHICommandList& cmd, RHIPipelineStateHandle pso ) const
    {
        const bool bNothingToDispatch = _listDispatch.empty() || pso == 0 || _paramBuffer.isValid() == false;
        if ( bNothingToDispatch )
            return;
        SW_PROFILE_SCOPE( "RT.Vfx.Simulate" );
        cmd.setComputePipelineState( pso );
        for ( const Dispatch& dispatch : _listDispatch )
        {
            const EmitterState& state = *dispatch._pState;
            // 쓰기 전에 UAV 로 — 렌더 입력과 인자는 지난 프레임에 정점 셰이더 · 간접 드로우가 읽던 상태다(빠지면 DX12 · Vulkan 에서 쓰기가 무효다).
            cmd.transitionBuffer( state._stateBuffer._buffer, RHIBufferState::UnorderedAccess );
            cmd.transitionBuffer( state._particleBuffer._buffer, RHIBufferState::UnorderedAccess );
            cmd.transitionBuffer( state._argsBuffer._buffer, RHIBufferState::UnorderedAccess );
            // g_VfxParams(t0) / g_VfxStateRW(u0) / g_VfxParticlesRW(u1) / g_VfxArgsRW(u2) / 루트 상수 g_VfxParamBase. vfxsimulate.hlsl 레지스터와 1:1 이다.
            cmd.bindComputeShaderResource( _paramBuffer._srv, 0 );
            cmd.bindComputeUav( state._stateBuffer._uav, 0 );
            cmd.bindComputeUav( state._particleBuffer._uav, 1 );
            cmd.bindComputeUav( state._argsBuffer._uav, 2 );
            cmd.setComputeRootConstants( 0, 1, &dispatch._paramRow );
            cmd.dispatchCompute( 1, 1, 1 );
            // 투명 패스가 t11 로 읽고 간접 인자로 그린다. 상태 버퍼는 다음 디스패치만 읽으므로 UAV 로 둔다.
            cmd.transitionBuffer( state._particleBuffer._buffer, RHIBufferState::ShaderResource );
            cmd.transitionBuffer( state._argsBuffer._buffer, RHIBufferState::IndirectArgument );
        }
    }

    const VfxGPUSimulation::DrawSource* VfxGPUSimulation::findDrawSource( uint32 batchIndex ) const
    {
        return batchIndex < _listBatchDrawSource.size() ? _listBatchDrawSource[batchIndex] : nullptr;
    }

    void VfxGPUSimulation::release( IRHIDevice* pDevice )
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
        _listDispatch.clear();
        _listBatchDrawSource.clear();
        _listParamWord.clear();
    }
} // namespace sw
