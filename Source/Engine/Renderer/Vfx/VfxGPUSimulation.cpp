#include "pch.h"

#include "Engine/Renderer/Vfx/VfxGPUSimulation.h"

#include "Core/Common/HashUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
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
            static_assert( SW_VFX_ARGS_WORD_DROPPED < SW_VFX_ARGS_WORD_COUNT, "the dropped counter must sit inside the arguments buffer" );

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

            /** @brief 행렬 @p matrix 의 행 넷(행벡터 규약 — _11.._14 가 첫 행)을 붙입니다. */
            static void appendMatrixRows( vector<uint32>& inoutListWord, const float4x4& matrix )
            {
                appendRow( inoutListWord, toBits( matrix._11 ), toBits( matrix._12 ), toBits( matrix._13 ), toBits( matrix._14 ) );
                appendRow( inoutListWord, toBits( matrix._21 ), toBits( matrix._22 ), toBits( matrix._23 ), toBits( matrix._24 ) );
                appendRow( inoutListWord, toBits( matrix._31 ), toBits( matrix._32 ), toBits( matrix._33 ), toBits( matrix._34 ) );
                appendRow( inoutListWord, toBits( matrix._41 ), toBits( matrix._42 ), toBits( matrix._43 ), toBits( matrix._44 ) );
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

            /** @brief 깊이 충돌의 플래그입니다. 깊이 방법이 아니면 0 이다(물리 레이캐스트는 GPU 에 오지 않는다 — 모듈 표가 막는다). */
            static uint32 computeCollisionFlags( const VfxCollisionDef& collision )
            {
                const bool bSceneDepth = collision._mode != VfxCollisionMode::None && collision._method == VfxCollisionMethod::SceneDepth;
                if ( bSceneDepth == false )
                    return 0u;
                return collision._mode == VfxCollisionMode::Kill ? SW_VFX_FLAG_COLLIDE_KILL : SW_VFX_FLAG_COLLIDE_BOUNCE;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VfxGPUSimulation::EmitterState::EmitterState( shared_ptr<const VfxEmitterDef> pDef, float32 spawnScale )
        : _clock{ std::move( pDef ), spawnScale }
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
        , _listMeshDispatch{}
        , _listBatchDrawSource{}
        , _listParamWord{}
        , _listScratchStaleRunID{}
        , _frameInput{}
        , _paramBuffer{}
        , _bPipelineMissingLogged{ false }
        , _bMeshPipelineMissingLogged{ false }
    {
    }

    VfxGPUSimulation::~VfxGPUSimulation() = default;

    void VfxGPUSimulation::prepareFrame( IRHIDevice& device, const vector<GPUParticleBatch>& listBatch, const vector<GPUVfxMeshEmitter>& listMeshEmitter,
                                         const VfxSimulationFrameInput& input )
    {
        using Internal = VfxGPUSimulationInternal;
        _frameInput    = input;
        _listDispatch.clear();
        _listMeshDispatch.clear();
        _listParamWord.clear();
        _listBatchDrawSource.assign( listBatch.size(), nullptr );
        for ( auto& [runID, pState] : _mapState )
        {
            (void)runID;
            pState->_bSeen = false;
        }

        bool bAnyGPUBatch = listMeshEmitter.empty() == false;
        for ( const GPUParticleBatch& batch : listBatch )
        {
            bAnyGPUBatch = bAnyGPUBatch || batch.findGPURequest() != nullptr;
        }
        const bool bReportMissingPipeline = bAnyGPUBatch && input._bSimulationPipelineReady == false && _bPipelineMissingLogged == false;
        if ( bReportMissingPipeline )
        {
            // 대신 CPU 로 그리지 않는다 — 시뮬레이션 대상은 데이터와 빌드가 정했고, 바꿔 그리면 CPU 와 GPU 를 비교하는 화면이 거짓이 된다.
            SW_LOG_ERROR( "The VFX simulation compute PSO is missing - GPU-simulated emitters are not drawn" );
            _bPipelineMissingLogged = true;
        }

        if ( input._bSimulationPipelineReady )
        {
            SW_PROFILE_SCOPE( "RT.Vfx.SimulationPrepare" );
            for ( uint32 batchIndex = 0; batchIndex < static_cast<uint32>( listBatch.size() ); ++batchIndex )
            {
                const GPUParticleBatch&     batch    = listBatch[batchIndex];
                const VfxGPUEmitterRequest* pRequest = batch.findGPURequest();
                if ( pRequest == nullptr || pRequest->_pDef == nullptr )
                    continue;
                EmitterState* pState = prepareEmitterState( device, *pRequest );
                if ( pState == nullptr )
                    continue;
                appendSimulation( *pState, *pRequest, batch._world, batch._bRibbon == SW_TRUE );
                _listBatchDrawSource[batchIndex] = &pState->_drawSource;
            }

            // 메시 방출기 — 인스턴스 버퍼 UAV 와 쓰기 PSO 가 있어야 그린다(없으면 한 번 알리고 그리지 않는다).
            const bool bMeshReady         = input._bMeshPipelineReady && input._instanceUav != kInvalidDescriptorIndex && input._instanceBuffer != 0;
            const bool bReportMissingMesh = listMeshEmitter.empty() == false && bMeshReady == false && _bMeshPipelineMissingLogged == false;
            if ( bReportMissingMesh )
            {
                SW_LOG_ERROR( "GPU mesh particles need the VFX mesh instance PSO and a GPUScene instance UAV - GPU mesh emitters are not drawn" );
                _bMeshPipelineMissingLogged = true;
            }
            for ( const GPUVfxMeshEmitter& emitter : listMeshEmitter )
            {
                const bool bEmitterReady = emitter._request._pDef != nullptr && emitter._pListInstanceSlot != nullptr;
                if ( bMeshReady == false || bEmitterReady == false )
                    continue;
                EmitterState* pState = prepareEmitterState( device, emitter._request );
                if ( pState == nullptr )
                    continue;
                appendSimulation( *pState, emitter._request, emitter._world, false );
                appendMeshInstance( *pState, emitter );
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

        const bool bNothingToUpload = _listDispatch.empty() && _listMeshDispatch.empty();
        if ( bNothingToUpload )
            return;
        const uint32 rowCount = static_cast<uint32>( _listParamWord.size() / Internal::kWordsPerRow );
        if ( _paramBuffer.ensureCapacity( &device, static_cast<uint32>( sizeof( uint32 ) ) * Internal::kWordsPerRow, rowCount,
                                          RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, nullptr ) == false )
        {
            SW_LOG_ERROR( "Failed to create the VFX simulation parameter buffer (%# rows) - GPU-simulated emitters are not drawn", rowCount );
            _listDispatch.clear();
            _listMeshDispatch.clear();
            _listBatchDrawSource.assign( listBatch.size(), nullptr );
            return;
        }
        _paramBuffer.upload( &device, _listParamWord.data(), static_cast<uint32>( _listParamWord.size() * sizeof( uint32 ) ) );
    }

    VfxGPUSimulation::EmitterState* VfxGPUSimulation::prepareEmitterState( IRHIDevice& device, const VfxGPUEmitterRequest& request )
    {
        using Internal                                                  = VfxGPUSimulationInternal;
        unordered_map<uint64, unique_ptr<EmitterState>>::iterator found = _mapState.find( request._runID );
        if ( found == _mapState.end() )
            found = _mapState.emplace( request._runID, sw::make_unique<EmitterState>( request._pDef, request._spawnScale ) ).first;
        EmitterState& state = *found->second;
        state._bSeen        = true;

        const VfxEmitterDef&     def         = state._clock.getDef();
        constexpr RHIBufferUsage kStateUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
        constexpr RHIBufferUsage kArgsUsage =
            RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::Raw | RHIBufferUsage::ShaderResource;
        uint32 arrInitialArgs[SW_VFX_ARGS_WORD_COUNT]{};
        arrInitialArgs[0]        = GPUParticle::kVertexCount;
        const uint32 stateCount  = def._maxParticles * Internal::kStateHalfCount;
        const bool   bStateReady = state._stateBuffer.ensureCapacity( &device, Internal::kStateStride, stateCount, kStateUsage, false, true, nullptr ) &&
                                 state._stateBuffer._uav != kInvalidDescriptorIndex;
        const bool bParticleReady = state._particleBuffer.ensureCapacity( &device, static_cast<uint32>( sizeof( GPUParticle ) ), def._maxParticles, kStateUsage, true, true, nullptr ) &&
                                    state._particleBuffer._uav != kInvalidDescriptorIndex && state._particleBuffer._srv != kInvalidDescriptorIndex;
        // 인자 버퍼는 메시 인스턴스 쓰기가 살아 있는 수를 읽으므로 SRV 도 둔다.
        const bool bArgsReady = state._argsBuffer.ensureCapacity( &device, static_cast<uint32>( sizeof( uint32 ) ), SW_VFX_ARGS_WORD_COUNT, kArgsUsage, true, true, arrInitialArgs ) &&
                                state._argsBuffer._uav != kInvalidDescriptorIndex && state._argsBuffer._srv != kInvalidDescriptorIndex;
        const bool bBuffersReady = bStateReady && bParticleReady && bArgsReady;
        if ( bBuffersReady == false )
        {
            SW_LOG_ERROR( "Failed to create the GPU buffers of VFX emitter '%#' (%# particles) - it is not drawn", def._name, def._maxParticles );
            return nullptr;
        }
        state._drawSource._particleSrv = state._particleBuffer._srv;
        state._drawSource._argsBuffer  = state._argsBuffer._buffer;
        return &state;
    }

    void VfxGPUSimulation::appendSimulation( EmitterState& state, const VfxGPUEmitterRequest& request, const float4x4& world, bool bRibbon )
    {
        using Internal         = VfxGPUSimulationInternal;
        const uint32 paramRow  = static_cast<uint32>( _listParamWord.size() / Internal::kWordsPerRow );
        const uint32 stepCount = appendParams( state, request._seed, request._stepCount, world, bRibbon );
        // 밀린 단계가 없고 이미 한 번 돌았으면 렌더 입력과 인자가 그대로다 — 디스패치하지 않는다(매개변수 구간은 버린다).
        if ( stepCount == 0 && state._bNeedsReset == false )
        {
            _listParamWord.resize( static_cast<size_t>( paramRow ) * Internal::kWordsPerRow );
            return;
        }
        Dispatch& dispatch = _listDispatch.emplace_back();
        dispatch._pState   = &state;
        dispatch._paramRow = paramRow;
        state._bNeedsReset = false;
    }

    uint32 VfxGPUSimulation::appendParams( EmitterState& state, uint64 seed, uint64 targetStepCount, const float4x4& world, bool bRibbon )
    {
        using Internal                             = VfxGPUSimulationInternal;
        const VfxEmitterDef&        def            = state._clock.getDef();
        const VfxDepthHistoryInput& depth          = _frameInput._depth;
        const size_t                first          = _listParamWord.size();
        const uint64                behind         = targetStepCount > state._doneStepCount ? targetStepCount - state._doneStepCount : 0u;
        const uint32                steps          = behind < SW_VFX_MAX_STEPS_PER_DISPATCH ? static_cast<uint32>( behind ) : SW_VFX_MAX_STEPS_PER_DISPATCH;
        const uint64                baseSeed       = HashUtil::mix64( seed );
        const uint32                collisionFlags = Internal::computeCollisionFlags( def._collision );
        const bool                  bDepthReady    = collisionFlags != 0u && depth._texture != 0 && depth._uav != kInvalidDescriptorIndex;
        const uint32                flags          = ( state._bNeedsReset ? SW_VFX_FLAG_RESET : 0u ) | ( bRibbon ? SW_VFX_FLAG_RIBBON : 0u ) | collisionFlags |
                             ( bDepthReady ? SW_VFX_FLAG_DEPTH_READY : 0u );
        const float32 stepTime = VfxEmitterClock::kFixedStepSeconds;
        // 네이티브는 RW 텍스처 배열의 번호로, 에뮬은 슬롯 0 의 서수로 고른다(swLoadRwTexture2D).
        const uint32 historyIndex = _frameInput._bNativeBindless ? static_cast<uint32>( depth._uav ) : 0u;

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
        Internal::appendRow( _listParamWord, Internal::toBits( def._collision._restitution ), Internal::toBits( def._collision._friction ),
                             Internal::toBits( VfxCollisionDef::kSurfaceSkin ), Internal::toBits( def._collision._depthThickness ) );
        Internal::appendRow( _listParamWord, depth._width, depth._height, historyIndex, 0u );
        Internal::appendRow( _listParamWord, Internal::toBits( depth._eye._x ), Internal::toBits( depth._eye._y ), Internal::toBits( depth._eye._z ), 0u );
        Internal::appendMatrixRows( _listParamWord, world );
        Internal::appendMatrixRows( _listParamWord, world.invert() );
        Internal::appendMatrixRows( _listParamWord, depth._viewProj );
        Internal::appendMatrixRows( _listParamWord, depth._viewProj.invert() );
        static_assert( SW_VFX_PARAM_ROW_FIRST_STEP == 28u, "the header rows above must match vfxsimulate.hlsli" );

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

    void VfxGPUSimulation::appendMeshInstance( const EmitterState& state, const GPUVfxMeshEmitter& emitter )
    {
        using Internal                 = VfxGPUSimulationInternal;
        const uint32          poolSize = state._clock.getDef()._maxParticles;
        const vector<uint32>& listSlot = *emitter._pListInstanceSlot;
        const uint32          paramRow = static_cast<uint32>( _listParamWord.size() / Internal::kWordsPerRow );
        Internal::appendRow( _listParamWord, poolSize, 0u, Internal::toBits( emitter._meshRadius ), Internal::toBits( emitter._sizeScale ) );
        Internal::appendMatrixRows( _listParamWord, emitter._world );
        static_assert( SW_VFX_MESH_ROW_FIRST_SLOT == 5u, "the mesh header rows above must match vfxsimulate.hlsli" );
        // 풀 자리마다 칸 — 행 하나에 넷. 배치 항목 수는 풀 크기와 같다(`VfxComponent::rebuildMeshEmitters`). 모자란 자리는 쓰지 않는다.
        const uint32 slotRowCount = ( poolSize + Internal::kWordsPerRow - 1u ) / Internal::kWordsPerRow;
        for ( uint32 slotRow = 0; slotRow < slotRowCount; ++slotRow )
        {
            uint32 arrSlot[Internal::kWordsPerRow]{ SW_VFX_INVALID_SLOT, SW_VFX_INVALID_SLOT, SW_VFX_INVALID_SLOT, SW_VFX_INVALID_SLOT };
            for ( uint32 lane = 0; lane < Internal::kWordsPerRow; ++lane )
            {
                const uint32 poolIndex = slotRow * Internal::kWordsPerRow + lane;
                if ( poolIndex < listSlot.size() )
                    arrSlot[lane] = listSlot[poolIndex];
            }
            Internal::appendRow( _listParamWord, arrSlot[0], arrSlot[1], arrSlot[2], arrSlot[3] );
        }
        MeshDispatch& dispatch = _listMeshDispatch.emplace_back();
        dispatch._pState       = &state;
        dispatch._paramRow     = paramRow;
        dispatch._groupCount   = ( poolSize + SW_VFX_MESH_INSTANCE_THREAD_COUNT - 1u ) / SW_VFX_MESH_INSTANCE_THREAD_COUNT;
    }

    void VfxGPUSimulation::recordDispatches( IRHICommandList& cmd, RHIPipelineStateHandle simulationPSO, RHIPipelineStateHandle meshInstancePSO ) const
    {
        const bool bNothingToDispatch = ( _listDispatch.empty() && _listMeshDispatch.empty() ) || simulationPSO == 0 || _paramBuffer.isValid() == false;
        if ( bNothingToDispatch )
            return;
        SW_PROFILE_SCOPE( "RT.Vfx.Simulate" );
        const VfxDepthHistoryInput& depth = _frameInput._depth;
        cmd.setComputePipelineState( simulationPSO );
        // 깊이 히스토리 — 지난 프레임 그 뷰가 남긴 깊이를 옮긴 R32 실수 텍스처. RW 텍스처로 읽는다(에뮬 백엔드의 컴퓨트는 샘플 텍스처를 읽지 못한다).
        const bool bBindDepth = depth._texture != 0 && depth._uav != kInvalidDescriptorIndex;
        if ( bBindDepth )
            cmd.prepareTextureForUnorderedAccess( depth._texture );
        for ( const Dispatch& dispatch : _listDispatch )
        {
            const EmitterState& state = *dispatch._pState;
            // 쓰기 전에 UAV 로 — 렌더 입력과 인자는 지난 프레임에 정점 셰이더 · 간접 드로우 · 메시 쓰기가 읽던 상태다(빠지면 DX12 · Vulkan 에서 쓰기가 무효다).
            cmd.transitionBuffer( state._stateBuffer._buffer, RHIBufferState::UnorderedAccess );
            cmd.transitionBuffer( state._particleBuffer._buffer, RHIBufferState::UnorderedAccess );
            cmd.transitionBuffer( state._argsBuffer._buffer, RHIBufferState::UnorderedAccess );
            // g_VfxParams(t0) / g_VfxStateRW(u0) / g_VfxParticlesRW(u1) / g_VfxArgsRW(u2) / 깊이 히스토리(RW 텍스처 슬롯 0) / 루트 상수 g_VfxParamBase.
            // vfxsimulate.hlsl 레지스터와 1:1 이다.
            cmd.bindComputeShaderResource( _paramBuffer._srv, 0 );
            cmd.bindComputeUav( state._stateBuffer._uav, 0 );
            cmd.bindComputeUav( state._particleBuffer._uav, 1 );
            cmd.bindComputeUav( state._argsBuffer._uav, 2 );
            if ( bBindDepth )
                cmd.bindComputeUav( depth._uav, shaderslot::kComputeTextureUav0 );
            cmd.setComputeRootConstants( 0, 1, &dispatch._paramRow );
            cmd.dispatchCompute( 1, 1, 1 );
            // 투명 패스가 t11 로 읽고 간접 인자로 그린다. 상태 버퍼는 다음 디스패치만 읽으므로 UAV 로 둔다.
            cmd.transitionBuffer( state._particleBuffer._buffer, RHIBufferState::ShaderResource );
            cmd.transitionBuffer( state._argsBuffer._buffer, RHIBufferState::IndirectArgument );
        }

        // 메시 입자 — 렌더 입력을 인스턴스 칸으로 옮긴다. 인스턴스 애니메이션 · 컬링이 이어서 이 버퍼를 읽는다.
        const bool bMeshWrites = _listMeshDispatch.empty() == false && meshInstancePSO != 0 && _frameInput._instanceUav != kInvalidDescriptorIndex;
        if ( bMeshWrites == false )
            return;
        cmd.transitionBuffer( _frameInput._instanceBuffer, RHIBufferState::UnorderedAccess );
        cmd.setComputePipelineState( meshInstancePSO );
        for ( const MeshDispatch& dispatch : _listMeshDispatch )
        {
            const EmitterState& state = *dispatch._pState;
            // 살아 있는 수는 인자 버퍼(SRV)에서 읽는다 — 이번 프레임에 시뮬레이션을 건너뛴 방출기도 지난 값이 그대로 맞다.
            cmd.transitionBuffer( state._argsBuffer._buffer, RHIBufferState::ShaderResource );
            // g_VfxParams(t0) / g_VfxParticles(t1) / g_VfxArgs(t2) / g_VfxInstancesRW(u0) / 루트 상수 g_VfxMeshParamBase. vfxmeshinstance.hlsl 과 1:1 이다.
            cmd.bindComputeShaderResource( _paramBuffer._srv, 0 );
            cmd.bindComputeShaderResource( state._particleBuffer._srv, 1 );
            cmd.bindComputeShaderResource( state._argsBuffer._srv, 2 );
            cmd.bindComputeUav( _frameInput._instanceUav, 0 );
            cmd.setComputeRootConstants( 0, 1, &dispatch._paramRow );
            cmd.dispatchCompute( dispatch._groupCount, 1, 1 );
            cmd.uavBarrier( _frameInput._instanceBuffer ); // 다음 방출기가 같은 버퍼의 다른 칸을 쓴다 — 칸이 겹치지 않아도 상태 추적을 맞춘다
        }
        cmd.transitionBuffer( _frameInput._instanceBuffer, RHIBufferState::ShaderResource );
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
        _listMeshDispatch.clear();
        _listBatchDrawSource.clear();
        _listParamWord.clear();
        _frameInput = VfxSimulationFrameInput{};
    }
} // namespace sw
