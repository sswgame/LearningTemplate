/**
 * @file FrameGraphExecutor.cpp
 * @brief 프레임 패스 종류마다의 실행입니다 — 깊이 히스토리 · HZB · GPU 입자 · 인스턴스 애니메이션 · 메시 모프 · 메시 스킨 · GPU 컬링(+ 정렬 · 2 단계 가림) · 라이트 그리드 ·
 *        볼륨 안개 · 실시간 프로브 · 화면 마커 투영.
 * @details 순서는 여기에 없다. 파이프라인 XML 의 `_listFramePass` 가 자원(`_listResource`)의 읽기 · 쓰기로 선언하고 프레임 그래프가 정한다
 *          (애니메이션 · 모프 · 스킨이 컬링보다 앞 — 컬링이 이번 프레임 바운드로 판정한다 — 은 `InstanceBuffer` · `DeformVertices` 의 쓰기 · 읽기다).
 *          각 패스는 자기가 쓰는 버퍼의 배리어를 스스로 건다(쓰기 전 UAV, 끝나면 읽기 상태) — 그래프 자원 이름은 순서만 정하고 버퍼를 모른다.
 *          상수 버퍼는 `RHIConstantBuffer` 이고 프레임 데이터(`FrameRenderState::_deform` · 뷰)에 있다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Execute/FrameGraphExecutor.h"

#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Animation/Scene/AnimationLOD.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/Debug/GPUPixelInspector.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Renderer/Frame/Execute/ViewGraphResource.h"
#include "Engine/Renderer/Frame/FrameRenderState.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PostProcess/FrameFog.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Graph/FrameBufferTable.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 프레임 그래프 자원 이름(파이프라인 XML `framecomputerenderpass.xml` 의 `_listResource`)입니다. 한 번만 intern 한다(전역 문자열 표의 뮤텍스를 프레임마다 잡지 않는다).
         * @details 컬링 버퍼는 등록 접근(프레임 시작이자 끝)이 달라 이름을 나눈다 — 인자는 간접 인자, 가시 목록은 셰이더 읽기, 전환 상태 · 차례 표시는 UAV.
         */
        struct FrameResource
        {
            static hashed_string deformVertices()
            {
                static const hashed_string kName( "DeformVertices" );
                return kName;
            }
            static hashed_string lightGrid()
            {
                static const hashed_string kName( "LightGrid" );
                return kName;
            }
            static hashed_string cullArgs()
            {
                static const hashed_string kName( "CullArgs" );
                return kName;
            }
            static hashed_string cullVisibleInstances()
            {
                static const hashed_string kName( "CullVisibleInstances" );
                return kName;
            }
            static hashed_string cullLODState()
            {
                static const hashed_string kName( "CullLODState" );
                return kName;
            }
            static hashed_string occludedOrders()
            {
                static const hashed_string kName( "OccludedOrders" );
                return kName;
            }
            static hashed_string fogVolume()
            {
                static const hashed_string kName( "VolumetricFogVolume" );
                return kName;
            }
            static hashed_string sceneVoxelBuffer()
            {
                static const hashed_string kName( "SceneVoxels" );
                return kName;
            }
            static hashed_string probeIrradiance()
            {
                static const hashed_string kName( "ProbeIrradiance" );
                return kName;
            }
            static hashed_string probeHistory()
            {
                static const hashed_string kName( "ProbeHistory" );
                return kName;
            }
            static hashed_string canvasMarkers()
            {
                static const hashed_string kName( "CanvasMarkers" );
                return kName;
            }
            /** @brief 뷰마다 지난 프레임 장면 깊이(첨부) — 깊이 히스토리 패스가 셰이더 읽기로 읽는다. */
            static RenderGraphTextureSRV previousSceneDepth()
            {
                static const hashed_string kName( "PreviousSceneDepth" );
                return RenderGraphTextureSRV{ kName };
            }
            /** @brief 뷰마다 깊이 히스토리(R32) — 깊이 히스토리 패스가 그리고(렌더 타깃) HZB 짓기 · GPU 입자가 RW 텍스처로 읽는다(UAV 읽기). */
            static RenderGraphRenderTarget depthHistory()
            {
                static const hashed_string kName( "DepthHistory" );
                return RenderGraphRenderTarget{ kName };
            }
        };
    } // namespace

    FrameGraphExecutor::FrameGraphExecutor( IRHIDevice* const& pDevice, FrameRenderState& state, FramePSOManager& psoManager, FrameFog& fog )
        : _pDevice{ pDevice }
        , _state{ state }
        , _psoManager{ psoManager }
        , _fog{ fog }
        , _occlusion{}
    {
    }

    void FrameGraphExecutor::ensureResources()
    {
        // 셰이더 메시지 링은 PSO 보다 먼저 만든다 — 메시지 변형 PSO 는 이 버퍼 없이는 돌릴 수 없으므로 만들지 못하면 멈춘다(폴백 없음).
        if ( _state._gpuMessages.initialize( _pDevice ) == false )
            reportFatalError( "GPU messages are on but the message ring could not be created - see the log above" );
        // 정렬 디스패치의 t2 자리표 — 순번 표가 없는 뷰도 빈 슬롯으로 디스패치하지 않는다(DX12 는 빈 디스크립터를 읽으면 폴트).
        constexpr uint32 kZeroRank = 0;
        if ( _transparentRankPlaceholder.ensureCapacity( _pDevice, sizeof( uint32 ), 1, RHIBufferUsage::ShaderResource, true, false, &kZeroRank ) == false )
            SW_LOG_ERROR_ID( "FrameGraphExecutor.FailedCreateTransparentRank", "Failed to create the transparent rank placeholder - extra views draw transparency in the main view order" );
        _occlusion.ensurePlaceholder( _pDevice );
        // 가림 판정 설정은 필수다 — 없거나 틀리면 기본값으로 대신하지 않고 멈춘다(설정 칸이 문턱의 정본이다).
        if ( ConfigManager::readConfigFileInto( OcclusionCullingConfig::kConfigFile, _occlusionConfig ) != ConfigReadResult::Loaded )
            reportFatalError( "%# is missing or unreadable - the occlusion culling settings have no default (the log above names the key)", OcclusionCullingConfig::kConfigFile );
        const string reason = _occlusionConfig.validate();
        if ( reason.empty() == false )
            reportFatalError( "%# is invalid: %#", OcclusionCullingConfig::kConfigFile, reason );
        if ( _lateCullCb.isValid() == false && _lateCullCb.create( _pDevice, sizeof( FrameRendererUtil::GPUCullParams ) ) == false )
            SW_LOG_ERROR_ID( "FrameGraphExecutor.FailedCreateLateCullCb", "Failed to create the late occlusion cull constant buffer - two-phase occlusion runs only its first phase" );
    }

    void FrameGraphExecutor::releaseResources()
    {
        _transparentRankPlaceholder.release( _pDevice );
        _occlusion.release( _pDevice );
        _lateCullCb.release( _pDevice );
        _occludedOrders.release( _pDevice );
        _bLatePhasePending = 0;
        _state._gpuMessages.shutdown();
        _state._vfxMeshReadback.release( _pDevice );
    }

    void FrameGraphExecutor::beginFrame( IRHICommandList& cmd, FrameBufferTable& table )
    {
        _bGPUCullingActive = 0;
        _state._gpuMessages.poll();
        _state._vfxMeshReadback.poll();
        // 이번 프레임의 출력 선택(`gpu.inspect` · `gpu.thread`)과 로그 예산 — 프레임의 기록보다 먼저 큐에 들어간다.
        const GPUPixelInspector* const pInspector = engine::findSubsystem<GPUPixelInspector>();
        _state._gpuMessages.updateControl( pInspector != nullptr ? pInspector->getSelection() : GPUMessageSelection{}, _state._settings._diagnostics._gpuMessageLogBudget );
        _state._gpuMessages.transitionForWrite( cmd ); // 위 업로드가 읽기 상태로 남긴다 — 패스 몸통은 전이하지 않는다
        // HZB · 가림 타일 표시는 짓는 쪽(HZBBuild 패스 · 프리패스 뒤 · 반사 앞)이 다시 켠다. 패스가 없는 파이프라인에서 지난 프레임의 표시가 남아 컬링이 낡은 피라미드를 읽지 않게 비운다.
        _bLatePhasePending = 0;
        _bLatePhaseRan     = 0;
        for ( unique_ptr<FrameViewTarget>& pView : _state._listView )
        {
            pView->_bOcclusionTilesThisFrame = SW_FALSE;
            pView->_bHZBValid                = SW_FALSE;
        }
        // 컬링 일감은 프레임 시작에 모은다 — 등록(아래 `registerCullResources`)과 디스패치(GPUCull 패스)가 같은 목록을 본다. 준비되지 않은 추가 뷰를 쉬게 하므로 HZB 등록보다 앞이다.
        collectCullJobs( _state._gpuScene.getInstanceOrderCount() );
        // 이번 프레임에 짓는 뷰의 피라미드 · 가림 타일 — 그래프가 짓기 앞(UAV) · 컬링 앞(SRV) 전이를 낸다. 칸 번호는 `recordFrameHZBs` 와 같다.
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( _state._listView.size() ); ++viewIndex )
        {
            const FrameViewTarget& view = *_state._listView[viewIndex];
            if ( view._bRenderThisFrame == SW_TRUE && shouldBuildFrameHZB( view ) )
                HZBOcclusion::registerFrameResources( table, viewIndex, view );
        }
        // GPU 입자 — 방출기 버퍼(시뮬레이션 · 정렬 · 메시 쓰기의 세 프레임 패스)와, 이번 프레임에 쓸 때만 라이트 버퍼 GPU 구간 · 인스턴스 칸(쓰지 않는 프레임에 UAV 로 오가지 않게).
        const VFXGPUSimulation& simulation = _state._particleRenderer.getGPUSimulation();
        simulation.registerFrameResources( table );
        if ( simulation.writesParticleLights() )
            table.registerBuffer( VFXGPUSimulation::getLightBufferUAV()._name, 0, _state._lightBuffer.getBuffer(), RenderGraphAccess::ShaderRead );
        // 이번 프레임에 쓰는 패스가 있는 버퍼만 등록한다 — 쓰지 않는 프레임은 등록 접근(셰이더 읽기)에서 쉬어 쓰는 패스 앞뒤의 전이가 나지 않는다(읽는 패스는 이미 그 상태를 본다).
        if ( shouldAnimateInstances() || simulation.writesMeshInstances() )
            table.registerBuffer( VFXGPUSimulation::getInstanceBufferUAV()._name, 0, _state._gpuScene.getInstanceStructuredBuffer(), RenderGraphAccess::ShaderRead );
        const GPUMeshMorphPool& morphPool = _state._deform._meshMorphPool;
        if ( morphPool.getMorphBuffer().isValid() && ( morphPool.isDispatchable() || morphPool.isSkinDispatchable() ) )
            table.registerBuffer( FrameResource::deformVertices(), 0, morphPool.getMorphBuffer(), RenderGraphAccess::ShaderRead );
        if ( _state._lightBuffer.getGPUGridViewCount() > 0 )
            table.registerBuffer( FrameResource::lightGrid(), 0, _state._lightBuffer.getGPUGridBuffer(), RenderGraphAccess::ShaderRead );
        registerCullResources( table );
        registerDepthHistoryResources( table );
        // 볼륨 안개 · 실시간 프로브 · 화면 마커 — 도는 프레임에만(등록 접근: 안개 볼륨 · 프로브 · 사각형은 그리기가 읽는 셰이더 읽기, 프로브 기록은 컴퓨트만 읽고 쓴다).
        if ( _fog.isFogVolumeActive() && _fog.getFogVolumeBuffer().isValid() )
            table.registerBuffer( FrameResource::fogVolume(), 0, _fog.getFogVolumeBuffer(), RenderGraphAccess::ShaderRead );
        if ( shouldUpdateRealtimeProbes() )
        {
            table.registerBuffer( FrameResource::probeIrradiance(), 0, *_state._lightBuffer.findProbeBuffer(), RenderGraphAccess::ShaderRead );
            table.registerBuffer( FrameResource::probeHistory(), 0, _state._probeUpdater.getHistoryBuffer(), RenderGraphAccess::UnorderedAccess );
            // 복셀은 칠하는 프레임에만 등록한다 — 바뀐 것이 없으면 셰이더 읽기에서 쉰다.
            if ( _state._probeUpdater.hasPendingPaint() )
                table.registerBuffer( FrameResource::sceneVoxelBuffer(), 0, _state._probeUpdater.getVoxelBuffer(), RenderGraphAccess::ShaderRead );
        }
        if ( _state._canvasRenderer.getMarkerCount() > 0 && _state._canvasRenderer.getQuadBuffer().isValid() )
            table.registerBuffer( FrameResource::canvasMarkers(), 0, _state._canvasRenderer.getQuadBuffer(), RenderGraphAccess::ShaderRead );
    }

    void FrameGraphExecutor::executePass( RenderPassKind type, const RenderGraphPassContext& graphContext )
    {
        _pCmd = graphContext.getCommandList(); // 패스 기록 면 — 전이는 이 패스가 쓰기로 선언한 자원의 `RenderGraphPassContext::transition` 뿐이다
        switch ( type )
        {
            case RenderPassKind::DepthHistory:
            {
                recordDepthHistories( graphContext );
                break;
            }
            case RenderPassKind::HZBBuild:
            {
                recordFrameHZBs( graphContext );
                break;
            }
            case RenderPassKind::VFXSimulation:
            {
                // 빌보드 · 띠는 투명 패스가 렌더 입력과 간접 인자를 읽는다(그 상태는 그래프 끝에 그래프가 되돌린다 — `RenderGraph::importResources`).
                _state._particleRenderer.getGPUSimulation().recordSimulation( graphContext, _psoManager.getEnginePSO( RenderPassKind::VFXSimulation ) );
                break;
            }
            case RenderPassKind::VFXParticleSort:
            {
                _state._particleRenderer.getGPUSimulation().recordSort( graphContext, _psoManager.getEnginePSO( RenderPassKind::VFXParticleSort ) );
                break;
            }
            case RenderPassKind::VFXMeshInstance:
            {
                // 메시 입자가 인스턴스 칸에 행렬 · 바운드를 쓴다 — 인스턴스 애니메이션 · 컬링 앞(XML 의 `InstanceBuffer` 쓰기 순서).
                _state._particleRenderer.getGPUSimulation().recordMeshInstances( graphContext, _psoManager.getEnginePSO( RenderPassKind::VFXMeshInstance ) );
                break;
            }
            case RenderPassKind::InstanceAnimation:
            {
                dispatchInstanceAnimation( graphContext ); // 칸 순 버퍼를 칸마다 돈다(구멍은 시드가 0 이라 건너뛴다)
                break;
            }
            case RenderPassKind::MeshMorph:
            {
                dispatchMeshMorph( graphContext );
                break;
            }
            case RenderPassKind::MeshSkin:
            {
                dispatchMeshSkin( graphContext );
                break;
            }
            case RenderPassKind::GPUCull:
            {
                dispatchCullAndSort( graphContext, _state._gpuScene.getInstanceOrderCount() ); // 차례마다 스레드 하나(인스턴스 버퍼는 칸 순 — 차례 → 칸 목록으로 찾는다)
                break;
            }
            case RenderPassKind::LightGrid:
            {
                dispatchLightGrid( graphContext );
                break;
            }
            case RenderPassKind::VolumetricFog:
            {
                // 볼륨은 도는 프레임에만 등록했다(`beginFrame` — 등록 조건이 곧 실행 조건). 등록이 없으면 그래프가 볼륨 전이를 내지 않았다 — 기록하지 않는다.
                if ( graphContext.isRegistered( RenderGraphBufferUAV{ FrameResource::fogVolume() } ) )
                    _fog.dispatchVolumetricFog( *_pCmd, _state._lightBuffer, _transparentRankPlaceholder._srv );
                break;
            }
            case RenderPassKind::SceneVoxelize:
            {
                // 칠하는 프레임에만 등록했다(`beginFrame` — 등록 조건이 곧 실행 조건).
                if ( graphContext.isRegistered( RenderGraphBufferUAV{ FrameResource::sceneVoxelBuffer() } ) )
                {
                    SW_PROFILE_SCOPE( "RT.Probe.Voxelize" );
                    const RHIPipelineStateHandle voxelizePSO = _psoManager.getEnginePSO( RenderPassKind::SceneVoxelize );
                    for ( uint32 dispatchIndex = 0; dispatchIndex < RealtimeProbeUpdater::kVoxelDispatchCount; ++dispatchIndex )
                    {
                        if ( _state._probeUpdater.hasVoxelDispatch( dispatchIndex ) == false )
                            continue;
                        if ( dispatchIndex > 0 )
                            graphContext.uavBarrier( RenderGraphBufferUAV{ FrameResource::sceneVoxelBuffer() } ); // 앞 디스패치가 쓴 칸 위에 칠한다
                        _state._probeUpdater.dispatchVoxelPass( *_pCmd, voxelizePSO, dispatchIndex );
                    }
                    _state._probeUpdater.finishVoxelPaint();
                }
                break;
            }
            case RenderPassKind::ProbeUpdate:
            {
                dispatchRealtimeProbes( graphContext );
                break;
            }
            case RenderPassKind::CanvasMarker:
            {
                // 캔버스 업로드(기록 전)가 올린 앵커 원소를 GPU 가 덮어쓴다. 주 출력 캔버스를 그리는 패스(뷰 그래프)보다 앞이다. 전이는 그래프가 낸다(`CanvasMarkers`).
                // 마커가 있는 프레임에만 등록했다(`beginFrame`) — 판정을 다시 하지 않는다.
                if ( graphContext.isRegistered( RenderGraphBufferUAV{ FrameResource::canvasMarkers() } ) == false )
                    break;
                _state._canvasRenderer.recordMarkerProjection( *_pCmd, _psoManager.getEnginePSO( RenderPassKind::CanvasMarker ), _state._gpuScene.getInstanceBuffer(),
                                                               _state._gpuScene.getInstanceSRV(), _transparentRankPlaceholder._srv, _state._gpuMessages,
                                                               _state.getMainView()._settings._viewMode == RenderViewMode::MarkerSlots );
                break;
            }
            default:
            {
                // 로드 검증(`RenderPipelineAsset::validate`)이 프레임 패스 목록에 다른 종류를 받지 않는다.
                SW_ASSERT( isFramePassType( type ) );
                break;
            }
        }
        _pCmd = nullptr;
    }

    void FrameGraphExecutor::endFrame( IRHICommandList& cmd )
    {
        // 메시지가 켜진 디스패치(컬링 · 모프 · 마커)가 끝났다 — 링을 읽기 버퍼로 복사하는 명령을 넣는다(그래픽스 패스의 기록은 다음 프레임 사본에 실린다).
        _state._gpuMessages.requestReadback( cmd );
        // 가림 타일의 되읽기 — 짓기(HZBBuild 패스)와 컬링이 끝난 프레임 끝이다. 컬링이 없는 백엔드(DX11)는 짓지 않는다.
        const bool bOcclusion = _pDevice != nullptr && _state._settings._bOcclusionCulling != SW_FALSE && _pDevice->getCapabilities()._bGPUCulling != SW_FALSE;
        _occlusion.requestReadback( cmd, _pDevice, bOcclusion, _state.getMainView() );
        // GPU 메시 입자의 살아 있는 수 · 주 시점이 그린 수 — 컬링과 그리기가 끝난 프레임 끝이다(자동화 감시 값 `VFX.GPUMesh*`).
        if ( _pDevice != nullptr )
        {
            _state._particleRenderer.collectGPUMeshProbeSources( _listScratchMeshProbeSource );
            _state._vfxMeshReadback.requestReadback( *_pDevice, cmd, _listScratchMeshProbeSource, _state._gpuScene.getInstances(), _state._gpuScene.getIndirectArgsBuffer(),
                                                     isGPUCullingActive() );
        }
    }

    bool FrameGraphExecutor::usesGPUGeneratedCommands() const
    {
        // 컴퓨트가 드로우 커맨드를 만드는 경로를 쓰려면 인다이렉트 드로우와 컬링 디스패치가 둘 다 켜져
        // 있고 컬링 PSO 가 실제로 만들어져 있어야 한다. 셋 중 하나라도 없으면 CPU 가 채운 개수로 그린다.
        // 개수를 되돌리는 커널이 없으면 컴퓨트가 센 개수가 프레임마다 쌓인다(N, 2N ...) · 압축 커널이 없으면 모은 인자가 빈 채로 그려진다 — 셋이 다 있어야 GPU 가 개수를 맡는다.
        return _state._settings._bGPUCulling != SW_FALSE && _psoManager.getEnginePSO( RenderPassKind::GPUCull ) != 0 && _psoManager.getEnginePSO( RenderPassKind::IndirectArgsReset ) != 0 &&
               _psoManager.getEnginePSO( RenderPassKind::IndirectArgsCompact ) != 0;
    }

    bool FrameGraphExecutor::shouldAnimateInstances() const
    {
        // 회전을 요청한 인스턴스가 하나도 없으면 디스패치 자체를 건너뛴다. 안 그러면 회전을 쓰지 않는 씬도
        // 매 프레임 인스턴스당 96바이트를 읽고 아무 일도 하지 않는다. 컬링 능력과는 무관하다(DX11 도 돈다).
        const bool bSpinAnimationNeeded =
            _state._gpuScene.isUploaded() && _state._gpuScene.getInstanceSlotCount() > 0 && _state._gpuScene.getSpinInstanceCount() > 0;
        return bSpinAnimationNeeded && _psoManager.getEnginePSO( RenderPassKind::InstanceAnimation ) != 0 && _state._deform._instanceAnimationCb.isValid() &&
               _state._gpuScene.getInstanceUAV() != kInvalidDescriptorIndex;
    }

    void FrameGraphExecutor::dispatchInstanceAnimation( const RenderGraphPassContext& graphContext )
    {
        // 인스턴스 애니메이션이 컬링보다 먼저다(XML 의 `InstanceBuffer` 쓰기 · 읽기). 순서가 뒤집히면 컬링이
        // **이번 프레임에 회전하기 전의** 바운드로 판정한다. 애니메이션이 회전만 바꾸므로 지금 씬에서는
        // 결과가 같지만, 이동을 넣는 순간 한 프레임 늦은 컬링이 된다.
        // 쓰기 앞 UAV · 읽는 쪽(컬링 · 정점 셰이더) 앞 셰이더 읽기는 그래프가 낸다(`beginFrame` 이 같은 판정 `shouldAnimateInstances` 로 등록했다).
        // 등록은 인스턴스 칸을 쓰는 패스(애니메이션 · 메시 입자) 어느 하나가 도는 프레임이다 — 이 패스의 판정은 그중 애니메이션 몫이다.
        if ( shouldAnimateInstances() == false || graphContext.isRegistered( VFXGPUSimulation::getInstanceBufferUAV() ) == false )
            return;
        const uint32                          instanceCount = _state._gpuScene.getInstanceSlotCount();
        FrameRendererUtil::GPUAnimationParams animationParams{};
        animationParams._time = _state.getAnimationTime();
        // 기준 각속도와 편차 폭(라디안/초). 편차가 기준보다 커야 "다 같은 속도" 로 보이지 않는다.
        animationParams._baseSpeed     = FrameRendererUtil::kGPUSpinBaseSpeed;
        animationParams._speedRange    = FrameRendererUtil::kGPUSpinSpeedRange;
        animationParams._instanceCount = instanceCount;
        _state._deform._instanceAnimationCb.update( *_pCmd, &animationParams, sizeof( animationParams ) );
        _pCmd->setComputePipelineState( _psoManager.getEnginePSO( RenderPassKind::InstanceAnimation ) );
        // AnimationParams(b0) / g_InstancesRW(u0). instance_animation.hlsl 레지스터와 1:1 로 대응한다.
        _pCmd->bindComputeConstantBuffer( _state._deform._instanceAnimationCb._index, 0 );
        _pCmd->bindComputeUAV( _state._gpuScene.getInstanceUAV(), 0 );
        _pCmd->dispatchThreads( instanceCount, 1, 1 );
    }

    bool FrameGraphExecutor::isDeformPassDeclared() const
    {
        return _psoManager.getEnginePSO( RenderPassKind::MeshMorph ) != 0 || _psoManager.getEnginePSO( RenderPassKind::MeshSkin ) != 0;
    }

    MeshMorphDiagMode FrameGraphExecutor::getEffectiveMeshMorphDiag() const
    {
        return _state._settings._meshMorphDebugMode;
    }

    bool FrameGraphExecutor::isMeshMorphDiagValid( MeshMorphDiagMode mode )
    {
        // 모든 열거자를 나열한다 — 열거자를 더하면 여기서 컴파일 경고가 난다. 범위 밖 값은 Normal 로 바꾸지 않고 알린다.
        switch ( mode )
        {
            case MeshMorphDiagMode::Normal:
            case MeshMorphDiagMode::ForceOn:
            case MeshMorphDiagMode::SkipDispatch:
            {
                return true;
            }
        }
        SW_LOG_ERROR_ID( "FrameGraphExecutor.GvMeshMorphDebugModeNoMode", "gv_meshMorphDebugMode has no mode for the value %# (Normal, ForceOn or SkipDispatch)", static_cast<uint32>( mode ) );
        return false;
    }

    void FrameGraphExecutor::prepareMeshMorphPool()
    {
        const MeshMorphDiagMode morphDiag = getEffectiveMeshMorphDiag();
        // 백엔드가 못 하면 풀을 만들지도 않는다. 배치의 base 가 kInvalidBase 로 남아 셰이더가
        // 레스트 포즈로 그린다(언리얼의 스킨 캐시 폴백과 같다). 자세한 사연은 RHICapabilities.h 를 참고한다.
        _state._deform._bDeformDispatchSkipped = SW_FALSE;
        if ( _pDevice == nullptr )
            return;
        if ( _pDevice->getCapabilities()._bGPUMeshMorph == SW_FALSE && morphDiag == MeshMorphDiagMode::Normal )
            return;
        // 결과 버퍼를 채우는 패스가 이 파이프라인에 없다 — 능력이 없을 때와 같이 풀을 만들지 않는다(배치는 레스트 포즈로 그린다).
        if ( isDeformPassDeclared() == false )
            return;

        // 풀은 **배치가 가진 메시**에서 만든다. 스냅샷 배치가 소유권을 가지고 있으므로 이 프레임 동안 살아 있다.
        // 스킨드 메시는 스킨 구간(뒤)으로, 모프를 켠 메시는 모프 구간(앞)으로 간다.
        SW_PROFILE_SCOPE( "RT.Deform.preparePool" );
        _state._deform._listScratchMorphMesh.clear();
        _state._deform._listScratchSkinMesh.clear();
        _state._deform._listScratchSkinSlot.clear();
        _state._deform._listScratchSkinLOD.clear();
        _state._deform._listScratchVertexAnimationMesh.clear();
        _state._deform._setScratchSeenMesh.clear();
        const auto& listBatch = _state._gpuScene.getAllBatches();
        for ( const GPUMeshBatch& batch : listBatch )
        {
            Mesh* pMesh = batch._mesh.get();
            if ( pMesh == nullptr )
                continue;
            // 같은 메시가 여러 배치에 나올 수 있다(불투명, 투명, 뷰). 메시의 분류(VAT · 스킨 · 모프 · 없음)는 하나뿐이므로
            // 처음 본 배치에서만 분류한다. 목록 순서는 처음 나온 순서 그대로라 풀 배치가 예전과 같다.
            if ( _state._deform._setScratchSeenMesh.insert( pMesh ).second == false )
                continue;
            // 정점 애니메이션(VAT) 메시는 컴퓨트 없이 정점 셰이더가 테이블을 읽는다. 테이블 풀로 간다.
            if ( pMesh->findVertexAnimation() != nullptr )
            {
                _state._deform._listScratchVertexAnimationMesh.push_back( pMesh );
                continue;
            }
            // 스킨 메시는 칸마다 결과다 — 아래에서 팔레트(칸)로 모은다. 모프를 켠 메시만 메시마다 결과(모프 구간)다.
            if ( pMesh->hasSkin() == false && pMesh->isGPUMorphEnabled() )
                _state._deform._listScratchMorphMesh.push_back( pMesh );
        }
        collectSkinInstances();
        // 중복 제거의 규모다. 배치 수 대비 고유 메시 수를 보면 이 루프가 무엇에 비례하는지 안다.
        SW_PROFILE_COUNT( "RT.Deform.batches", listBatch.size() );
        SW_PROFILE_COUNT( "RT.Deform.uniqueMeshes", _state._deform._setScratchSeenMesh.size() );

        // 아래 세 구간이 preparePool 안의 무거운 단계다 — 목록이 바뀐 프레임의 풀 재구성(GPU 업로드 포함), 매 프레임의 팔레트 업로드, VAT 테이블 풀.
        {
            SW_PROFILE_SCOPE( "RT.Deform.rebuildMorphPool" );
            _state._deform._meshMorphPool.update( _pDevice, _state._deform._listScratchMorphMesh, _state._deform._listScratchSkinMesh, _state._deform._listScratchSkinSlot,
                                                  _state._deform._listScratchSkinLOD );
            // 변형 프로그램(에디터 모디파이어 미리보기)은 레스트와 따로 바뀐다 - 번호가 바뀐 프레임에만 올린다
            _state._deform._meshMorphPool.updateDeformPrograms( _pDevice );
        }
        {
            SW_PROFILE_SCOPE( "RT.Deform.uploadSkinPalettes" );
            _state._deform._meshMorphPool.uploadSkinPalettes( _pDevice, _state._gpuScene.getSkinPalettes(), _state._gpuScene.findSkinPaletteRows(), _state._gpuScene.findMorphWeights() );
            // 스킨 결과의 쓰는 벌을 바꾼다 — 다른 벌에 지난 프레임 결과가 남아 화면 속도의 지난 위치가 된다(디스패치가 이번 벌에 쓴다).
            _state._deform._meshMorphPool.advanceSkinHistory();
        }

        // 배치에 구간을 기록해 둔다. upload() 가 배치 테이블(g_SwBatches)에 싣는다. 풀에 들어가지 못한 메시(상한 초과 · 버퍼 실패)는
        // kInvalidBase 와 _bDeformRefused 를 받아 그리지 않는다 — 레스트 포즈로 대신 그리지 않는다(풀이 오류를 남겼다).
        _state._gpuScene.assignMorphBases( _state._deform._meshMorphPool );
        {
            SW_PROFILE_SCOPE( "RT.Deform.rebuildVertexAnimationPool" );
            _state._deform._vertexAnimationPool.rebuild( _pDevice, _state._deform._listScratchVertexAnimationMesh );
        }
        _state._gpuScene.assignVertexAnimationBases( _state._deform._vertexAnimationPool );
        SW_PROFILE_COUNT( "RT.Skin.instances", _state._deform._meshMorphPool.getSkinInstanceCount() );
        SW_PROFILE_COUNT( "RT.Skin.vertices", _state._deform._meshMorphPool.getSkinVertexCount() );
        SW_PROFILE_COUNT( "RT.Skin.sourceVertices", _state._deform._meshMorphPool.getSkinSourceVertexCount() );
    }

    void FrameGraphExecutor::collectSkinInstances()
    {
        FrameMeshDeformState& deform = _state._deform;
        deform._listScratchDrawnSkinSlot.clear();
        for ( const GPUInstance& instance : _state._gpuScene.getInstances() )
        {
            const uint32 skinSlot = instance._skinSlot;
            if ( skinSlot == constant::invalid_index::kUint32 )
                continue;
            if ( skinSlot >= deform._listScratchDrawnSkinSlot.size() )
                deform._listScratchDrawnSkinSlot.resize( static_cast<size_t>( skinSlot ) + 1u, SW_FALSE );
            deform._listScratchDrawnSkinSlot[skinSlot] = SW_TRUE;
        }
        for ( const GPUSkinPalette& palette : _state._gpuScene.getSkinPalettes() )
        {
            const uint32 skinSlot = palette._skinSlot._slot;
            const bool   bDrawn   = skinSlot < deform._listScratchDrawnSkinSlot.size() && deform._listScratchDrawnSkinSlot[skinSlot] == SW_TRUE;
            const bool   bSkin    = palette._mesh != nullptr && palette._mesh->hasSkin() && palette._mesh->findVertexAnimation() == nullptr;
            if ( bDrawn == false || bSkin == false )
                continue;
            deform._listScratchSkinMesh.push_back( palette._mesh.get() );
            deform._listScratchSkinSlot.push_back( palette._skinSlot );
            deform._listScratchSkinLOD.push_back( palette._lodIndex );
        }
        SW_PROFILE_COUNT( "RT.Deform.skinSlots", deform._listScratchSkinSlot.size() );
    }

    void FrameGraphExecutor::dispatchMeshMorph( const RenderGraphPassContext& graphContext )
    {
        const MeshMorphDiagMode morphDiag = getEffectiveMeshMorphDiag();
        if ( _pDevice == nullptr || _pCmd == nullptr )
            return;
        if ( _pDevice->getCapabilities()._bGPUMeshMorph == SW_FALSE && morphDiag == MeshMorphDiagMode::Normal )
            return;
        if ( _state._deform._meshMorphPool.isDispatchable() == false )
            return;

        // 진단 SkipDispatch: 컴퓨트를 돌리지 않고 레스트를 결과 배치로 묶어 결과 버퍼에 올린다 — 정점 셰이더의 결과 읽기(원소 번호 · 풀기)를 컴퓨트 없이 본다.
        // 메시마다 풀 구간(getBaseOf)에 쓴다 — 구간은 목록 순서로 이어져 있지 않다(C-43 부분 갱신).
        if ( morphDiag == MeshMorphDiagMode::SkipDispatch )
        {
            for ( const Mesh* pMesh : _state._deform._listScratchMorphMesh )
            {
                const uint32 base = ( pMesh != nullptr ) ? _state._deform._meshMorphPool.getBaseOf( pMesh ) : GPUMeshMorphPool::kInvalidBase;
                if ( base == GPUMeshMorphPool::kInvalidBase )
                    continue;
                _state._deform._listScratchRestResult.clear();
                for ( const RHIVertex& vertex : pMesh->getCPUView().getVertices() )
                {
                    _state._deform._listScratchRestResult.push_back( GPUDeformResult::fromVertex( vertex ) );
                }
                _state._deform._meshMorphPool.getMorphBuffer().upload( _pDevice, _state._deform._listScratchRestResult.data(),
                                                                       static_cast<uint32>( _state._deform._listScratchRestResult.size() * sizeof( GPUDeformResult ) ),
                                                                       base * static_cast<uint32>( sizeof( GPUDeformResult ) ) );
            }
            _state._deform._bDeformDispatchSkipped = SW_TRUE;
            return;
        }

        const RHIPipelineStateHandle morphPSO         = _psoManager.getEnginePSO( RenderPassKind::MeshMorph );
        const RHIBufferSRVIndex      programSRV       = _state._deform._meshMorphPool.getDeformProgramBuffer()._srv;
        const bool                   bMissingResource = morphPSO == 0 || _state._deform._meshMorphCb.isValid() == false || programSRV == kInvalidDescriptorIndex;
        if ( bMissingResource )
            return;

        FrameRendererUtil::GPUMorphParams morphParams{};
        morphParams._time        = _state.getAnimationTime();
        morphParams._amplitude   = FrameRendererUtil::kMeshMorphAmplitude;
        morphParams._frequency   = FrameRendererUtil::kMeshMorphFrequency;
        morphParams._vertexCount = _state._deform._meshMorphPool.getMorphVertexCount();
        // 쓰기 앞 UAV · 드로우 앞 셰이더 읽기는 그래프가 낸다(`DeformVertices` — `beginFrame` 이 등록했다). 정점 셰이더가 이 버퍼를 읽으므로 전이가 빠지면
        // DX12 와 Vulkan 에서 조용히 예전 값이 나온다 — 선언 · 등록이 없으면 디스패치하지 않는다(오류는 문맥이 남겼다).
        if ( morphParams._vertexCount == 0 || graphContext.getBuffer( RenderGraphBufferUAV{ FrameResource::deformVertices() } ) == 0 )
            return;
        _state._deform._meshMorphCb.update( *_pCmd, &morphParams, sizeof( morphParams ) );
        _pCmd->setComputePipelineState( morphPSO );
        _pCmd->bindComputeConstantBuffer( _state._deform._meshMorphCb._index, 0 );
        // MorphParams(b0) / g_RestVertices(t0) / g_DeformPrograms(t1, 칸 표 + 변형 프로그램) / g_MorphVerticesRW(u0). mesh_morph.hlsl 레지스터와 1:1 로 대응한다.
        _pCmd->bindComputeShaderResource( _state._deform._meshMorphPool.getRestBuffer()._srv, 0 );
        _pCmd->bindComputeShaderResource( programSRV, 1 );
        _pCmd->bindComputeUAV( _state._deform._meshMorphPool.getMorphBuffer()._uav, 0 );
        _state._gpuMessages.bindCompute( *_pCmd ); // mesh_morph.hlsl 의 메시지(u4) — 메시지 변형 PSO 일 때만 버퍼가 있다
        _pCmd->dispatchThreads( morphParams._vertexCount, 1, 1 );
    }

    void FrameGraphExecutor::dispatchMeshSkin( const RenderGraphPassContext& graphContext )
    {
        if ( _pDevice == nullptr || _pCmd == nullptr )
            return;
        const MeshMorphDiagMode morphDiag = getEffectiveMeshMorphDiag();
        // 진단 SkipDispatch 는 "컴퓨트 없이" 를 본다. 스키닝도 돌리지 않는다(칸 표를 걸지 않아 스킨 메시는 그리지 않는다).
        const bool bSkipMorphSkinning = ( _pDevice->getCapabilities()._bGPUMeshMorph == SW_FALSE && morphDiag == MeshMorphDiagMode::Normal ) ||
                                        morphDiag == MeshMorphDiagMode::SkipDispatch;
        if ( bSkipMorphSkinning )
            return;
        if ( _state._deform._meshMorphPool.isSkinDispatchable() == false )
            return;
        const RHIPipelineStateHandle skinPSO = _psoManager.getEnginePSO( RenderPassKind::MeshSkin );
        // 모프와 같은 결과 버퍼의 뒤 구간에 쓴다 — 전이와 모프 뒤 UAV 배리어(같은 이름의 UAV 연속)는 그래프가 낸다.
        if ( skinPSO == 0 || _state._deform._meshSkinCb.isValid() == false || graphContext.getBuffer( RenderGraphBufferUAV{ FrameResource::deformVertices() } ) == 0 )
            return;

        FrameRendererUtil::GPUSkinParams skinParams{};
        skinParams._skinVertexBase = _state._deform._meshMorphPool.getSkinVertexBase() + _state._deform._meshMorphPool.getSkinCurrentShift(); // 이번 벌

        skinParams._skinVertexCount   = _state._deform._meshMorphPool.getSkinVertexCount();
        skinParams._skinBoneCount     = _state._deform._meshMorphPool.getSkinBoneCount();
        skinParams._skinInstanceCount = _state._deform._meshMorphPool.getSkinInstanceCount();
        _state._deform._meshSkinCb.update( *_pCmd, &skinParams, sizeof( skinParams ) );

        _pCmd->setComputePipelineState( skinPSO );
        // SkinParams(b0) / g_RestVertices(t0, 원본 레스트) / g_SkinWeights(t1) / g_SkinPalette(t2) / g_SkinInstances(t3) / g_MorphVerticesRW(u0).
        // mesh_skin.hlsl 레지스터와 1:1 로 대응한다.
        _pCmd->bindComputeConstantBuffer( _state._deform._meshSkinCb._index, 0 );
        _pCmd->bindComputeShaderResource( _state._deform._meshMorphPool.getSkinRestBuffer()._srv, 0 );
        _pCmd->bindComputeShaderResource( _state._deform._meshMorphPool.getSkinWeightBuffer()._srv, 1 );
        _pCmd->bindComputeShaderResource( _state._deform._meshMorphPool.getSkinPaletteBuffer()._srv, 2 );
        _pCmd->bindComputeShaderResource( _state._deform._meshMorphPool.getSkinInstanceBuffer()._srv, 3 );
        _pCmd->bindComputeUAV( _state._deform._meshMorphPool.getMorphBuffer()._uav, 0 );
        _pCmd->dispatchThreads( skinParams._skinVertexCount, 1, 1 );
        _state._deform._meshMorphPool.markSkinDispatched();
    }

    void FrameGraphExecutor::computeLODFadeStep()
    {
        // 전환 진행량은 렌더 프레임 간격(벽시계)에서 센다 — 진행이 게임 시간이 아니라 화면에서 보이는 시간이다. 간격이 길어도(정지 · 히치) 한 프레임에 끝내지 않도록 0.1 초로 막는다.
        constexpr float32 kMaxFrameSeconds = 0.1f;
        const int64       nowNanoseconds   = MonotonicClock::nowNanoseconds();
        const float32     frameSeconds     = ( _lodFadePreviousNanoseconds == 0 ) ? 0.0f : static_cast<float32>( nowNanoseconds - _lodFadePreviousNanoseconds ) * 1.0e-9f;
        _lodFadePreviousNanoseconds        = nowNanoseconds;
        const float32 fadeSeconds          = _state._gpuScene.getLODCrossfadeSeconds();
        if ( fadeSeconds <= 0.0f )
        {
            _lodFadeStep = 0;
            return;
        }
        // 진행은 0 ~ 255 로 센다. 한 프레임에 적어도 1 은 간다(멈춘 전환이 남지 않는다).
        const float32 progress = MathUtil::min( frameSeconds, kMaxFrameSeconds ) / fadeSeconds * 255.0f;
        _lodFadeStep           = static_cast<uint32>( MathUtil::clamp( progress, 1.0f, 255.0f ) );
    }

    void FrameGraphExecutor::collectCullJobs( uint32 instanceCount )
    {
        _listCullJob.clear();
        _bCullJobsCoverAllViews = 0;
        _bOccludedOrdersReady   = 0;
        // 컬링은 GPUScene 이 "개수를 컴퓨트에 맡겼다" 고 답할 때만 돈다. 그래야 인자의 초기 개수(0)와
        // 디스패치 여부가 절대 어긋나지 않는다. 어긋나면 한쪽은 빈 화면, 다른 쪽은 낡은 목록이다.
        //
        // **뷰마다 한 번씩** 돈다. 컬링 결과는 절두체에 종속이라, 메인 카메라로 거른 목록을 그림자 패스가
        // 쓰면 화면 밖에서 화면 안으로 그림자를 드리우는 물체가 사라진다. 언리얼이 뷰마다
        // FInstanceCullingContext 를 두는 것과 같은 이유다. 이번 프레임에 그리는 추가 뷰(CCTV, PiP)도 자기 슬롯을 돈다.
        if ( _state._gpuScene.isUploaded() == false || _state._gpuScene.areIndirectCountsGPUFilled() == false )
            return;
        const bool bCullingUnavailable = _psoManager.getEnginePSO( RenderPassKind::GPUCull ) == 0 || _psoManager.getEnginePSO( RenderPassKind::IndirectArgsReset ) == 0 ||
                                         _state._gpuScene.getInstanceSRV() == kInvalidDescriptorIndex ||
                                         _state._gpuScene.getBatchInfoSRV() == kInvalidDescriptorIndex ||
                                         _state._gpuScene.getInstanceOrderBuffer()._srv == kInvalidDescriptorIndex;
        if ( bCullingUnavailable )
            return;

        computeLODFadeStep();
        // 돌릴 슬롯을 모은다. 돌 수 있는지는 디스패치 전에 정해진다(isCullViewReady) — 그래서 추가 뷰의 그림자 슬롯 조건도 여기서 가린다.
        bool bAllViewsCulled = true;
        // 캐스케이드가 각자 슬롯을 돌고 점광 · 스폿이 모두 자기 슬롯을 가지면 합친 칸(주 시점 그림자 슬롯)은 아무 패스도 읽지 않는다 — 돌리지 않는다.
        const bool bMainShadowSlotUnused =
            _state._cascadeCullCount > 0 && _state._localShadowCullCount == static_cast<uint32>( _state._frameShadow._listLocalCullViewProj.size() );
        for ( unique_ptr<FrameViewTarget>& pView : _state._listView )
        {
            // 그림자 패스도 이 뷰의 그림자 볼륨으로 따로 거른다 — 주 시점의 그림자 슬롯을 쓰면 이 뷰만 보는 곳의 가리는 물체가 빠진다.
            if ( pView->_bRenderThisFrame == SW_FALSE )
                continue;
            const bool bMainView  = pView.get() == &_state.getMainView();
            const bool bBothReady = isCullViewReady( pView->_cullSlot, pView->_cullInput ) && isCullViewReady( pView->_shadowCullSlot, pView->_shadowCullInput );
            // 컬링하지 못한 추가 뷰는 그리지 않는다. 가시 목록을 바인딩하는 프레임에 갱신되지 않은 목록을 읽게 된다. 주 시점은 그리되 이 프레임의 GPU 컬링 목록을 쓰지 않는다.
            if ( bMainView == false && bBothReady == false )
            {
                pView->_bRenderThisFrame = SW_FALSE;
                continue;
            }
            const bool bSceneQueued  = enqueueCullView( pView->_cullSlot, pView->_cullInput, pView.get() );
            const bool bShadowQueued = ( bMainView && bMainShadowSlotUnused ) || enqueueCullView( pView->_shadowCullSlot, pView->_shadowCullInput, nullptr );
            if ( bMainView )
                bAllViewsCulled = bSceneQueued && bShadowQueued;
        }
        // 주 시점 캐스케이드마다 자기 절두체(그 캐스케이드의 그림자 행렬)로 거른다. 정렬은 그림자에 필요 없어 상수 버퍼가 없다(dispatchCullView 가 건너뛴다).
        for ( uint32 cascadeIndex = 0; cascadeIndex < _state._cascadeCullCount; ++cascadeIndex )
        {
            RenderView& cascadeInput = _state._arrCascadeCullInput[cascadeIndex];
            cascadeInput.setViewProjection( _state.getMainView()._shadow._arrCascadeTile[cascadeIndex]._viewProj );
            bAllViewsCulled = enqueueCullView( _state._cascadeCullSlotBase + cascadeIndex, cascadeInput, nullptr ) && bAllViewsCulled;
        }
        // 점광 · 스폿마다 자기 절두체로 거른다(감사 RN-5). 타일은 모든 뷰가 같아 프레임에 한 번이다. 절두체는 prepareLocalShadowCullInputs 가 맞췄다.
        for ( uint32 lightIndex = 0; lightIndex < _state._localShadowCullCount; ++lightIndex )
        {
            bAllViewsCulled = enqueueCullView( _state._localShadowCullSlotBase + lightIndex, _state._listLocalShadowCullInput[lightIndex], nullptr ) && bAllViewsCulled;
        }

        // 비 가림 맵도 자기 정사영 상자로 거른다.
        if ( _state._rainOcclusion.isActive() )
            bAllViewsCulled = enqueueCullView( _state._rainOcclusion.getCullSlot(), _state._rainOcclusion.getCullInput(), nullptr ) && bAllViewsCulled;

        // 2 단계 가림의 차례 표시 — 주 시점 앞 단계가 쓰고 프리패스 뒤 뒤 단계가 읽는다. 차례 수만큼(만들지 못하면 한 단계로 돈다 — dispatchCullView 가 본다).
        _lateInstanceCount = instanceCount;
        const bool bOrdersReady =
            isTwoPhaseOcclusionEnabled() &&
            _occludedOrders.ensureCapacity( _pDevice, sizeof( uint32 ), MathUtil::max( instanceCount, 1u ), RHIBufferUsage::Structured | RHIBufferUsage::UnorderedAccess, false, true, nullptr );
        _bOccludedOrdersReady   = bOrdersReady ? 1u : 0u;
        _bCullJobsCoverAllViews = bAllViewsCulled ? 1u : 0u;
    }

    void FrameGraphExecutor::registerCullResources( FrameBufferTable& table ) const
    {
        // 등록 접근은 프레임 시작이자 끝의 상태다 — 인자는 간접 그리기, 가시 목록은 정점 셰이더 읽기(그래프 끝에 표가 되돌린다), 전환 상태 · 차례 표시는 컬링만 읽고 쓴다(UAV).
        // 칸은 컬링 슬롯 번호다. 이번 프레임에 돌릴 슬롯만 등록한다(`collectCullJobs`).
        for ( const CullJob& job : _listCullJob )
        {
            const GPUCullViewResources& view = _state._gpuScene.getCullView( job._cullViewIndex );
            table.registerBuffer( FrameResource::cullArgs(), job._cullViewIndex, view._indirectArgs, RenderGraphAccess::IndirectArgs );
            table.registerBuffer( FrameResource::cullVisibleInstances(), job._cullViewIndex, view._visibleInstances, RenderGraphAccess::ShaderRead );
            if ( _state._gpuScene.hasLODState( job._cullViewIndex ) )
                table.registerBuffer( FrameResource::cullLODState(), job._cullViewIndex, view._lodState, RenderGraphAccess::UnorderedAccess );
        }
        if ( _listCullJob.empty() == false && _bOccludedOrdersReady != 0 )
            table.registerBuffer( FrameResource::occludedOrders(), 0, _occludedOrders, RenderGraphAccess::UnorderedAccess );
    }

    void FrameGraphExecutor::dispatchCullAndSort( const RenderGraphPassContext& graphContext, uint32 instanceCount )
    {
        // 일감과 전이: `beginFrame` 이 모으고(`collectCullJobs`) 등록했다 — 슬롯 버퍼의 UAV 전이(앞)와 그리는 쪽 상태(인자 간접 인자 · 가시 목록 셰이더 읽기)는 그래프가 낸다.
        if ( _listCullJob.empty() )
            return;
        bool bAllViewsCulled = _bCullJobsCoverAllViews != 0;
        for ( const CullJob& job : _listCullJob )
        {
            // 선언 · 등록이 없으면 그래프가 전이를 내지 않았다 — 돌리지 않는다(오류는 문맥이 남겼다). 파이프라인 XML 의 `GPUCull` 이 컬링 버퍼 이름을 출력으로 적어야 한다.
            const bool bDeclared = graphContext.getBuffer( RenderGraphBufferUAV{ FrameResource::cullArgs() }, job._cullViewIndex ) != 0 &&
                                   graphContext.getBuffer( RenderGraphBufferUAV{ FrameResource::cullVisibleInstances() }, job._cullViewIndex ) != 0;
            if ( bDeclared == false )
            {
                bAllViewsCulled = false;
                continue;
            }
            dispatchCullView( graphContext, job._cullViewIndex, *job._pRenderView, instanceCount, job._pView );
        }
        _bGPUCullingActive = bAllViewsCulled ? 1u : 0u;
    }

    void FrameGraphExecutor::dispatchLightGrid( const RenderGraphPassContext& graphContext )
    {
        // 생산자가 GPU 인 프레임에만 뷰 수가 0 보다 크다(GPULightBuffer::updateGPUGrid). 그때 PSO 와 버퍼는 이미 확인됐다(selectLightGridProducer, updateGPUGrid) —
        // 여기서 건너뛰면 채우지 않은 그리드를 셰이더가 읽으므로 더 거르지 않는다.
        // 쓰기 앞 UAV · 읽는 쪽(조명 패스 t12 · 볼륨 안개 · 프로브) 앞 셰이더 읽기는 그래프가 낸다(`LightGrid` — `beginFrame` 이 같은 판정으로 등록했다).
        const uint32 viewCount = _state._lightBuffer.getGPUGridViewCount();
        if ( viewCount == 0 || _pCmd == nullptr || graphContext.getBuffer( RenderGraphBufferUAV{ FrameResource::lightGrid() } ) == 0 )
            return;
        const RHIStructuredBuffer& gridBuffer = _state._lightBuffer.getGPUGridBuffer();
        _pCmd->setComputePipelineState( _psoManager.getEnginePSO( RenderPassKind::LightGrid ) );
        // g_LightGridLights(t0) / g_LightGridViews(t1) / g_LightGridRW(u0). light_grid.hlsl 레지스터와 1:1 로 대응한다. 상수 버퍼는 없다(뷰마다의 값은 t1).
        _pCmd->bindComputeShaderResource( _state._lightBuffer.getBuffer()._srv, 0 );
        _pCmd->bindComputeShaderResource( _state._lightBuffer.getGPUGridViewBuffer()._srv, 1 );
        _pCmd->bindComputeUAV( gridBuffer._uav, 0 );
        // 그룹 하나 = 뷰 하나. 그룹 안에서 세기 · 접두사 합 · 채우기를 그룹 동기화로 잇는다.
        _pCmd->dispatchCompute( viewCount, 1, 1 );
    }

    bool FrameGraphExecutor::canCopyDepthHistory( const FrameViewTarget& view ) const
    {
        const bool bHasSource = view._bLastFrameDepthValid == SW_TRUE && view._depthHistory.isValid();
        if ( bHasSource == false || _psoManager.getEnginePSO( RenderPassKind::DepthHistory ) == 0 )
            return false;
        return view._transientPool.find( attachmentNames()._sceneDepth )._texture != 0;
    }

    bool FrameGraphExecutor::canCopySceneDepth( const FrameViewTarget& view ) const
    {
        const bool bHasTarget = view._depthHistory.isValid() && _psoManager.getEnginePSO( RenderPassKind::DepthHistory ) != 0;
        return bHasTarget && view._transientPool.find( attachmentNames()._sceneDepth )._texture != 0;
    }

    void FrameGraphExecutor::recordDepthHistories( const RenderGraphPassContext& graphContext )
    {
        if ( _pCmd == nullptr || _pDevice == nullptr )
            return;
        // 주 시점과 추가 뷰 — 장면 깊이 첨부는 프레임 사이에 그대로 남아 있다(이번 프레임 뷰 그래프가 쓰기 전이다). 칸은 HZB 와 같다(뷰 번호 — 주 시점이 0).
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( _state._listView.size() ); ++viewIndex )
        {
            recordDepthHistory( graphContext, *_state._listView[viewIndex], viewIndex );
        }
        // 비 가림 맵 — 지난 프레임에 그린 맵을 GPU 강수가 읽는 R32 로 옮긴다(뷰 다음 칸). 상태 규칙은 장면 깊이와 같다.
        const uint32 rainSlot = static_cast<uint32>( _state._listView.size() );
        if ( canCopyRainOcclusion() && graphContext.isRegistered( FrameResource::depthHistory(), rainSlot ) )
        {
            drawDepthCopy( *_pCmd, _state._rainOcclusion.getDepth()._srv, _state._rainOcclusion.getParticleDepth() );
            graphContext.transition( FrameResource::depthHistory(), RenderGraphAccess::UnorderedAccessRead, rainSlot );
        }
    }

    bool FrameGraphExecutor::canCopyRainOcclusion() const
    {
        return _state._rainOcclusion.hasParticleDepth() && _psoManager.getEnginePSO( RenderPassKind::DepthHistory ) != 0;
    }

    void FrameGraphExecutor::registerDepthHistoryResources( FrameBufferTable& table ) const
    {
        // 깊이 히스토리 패스가 이번 프레임에 옮기는 뷰만(`canCopyDepthHistory` — 짓기 · 복사가 같은 판정을 본다). 처음 접근: 지난 장면 깊이는 뷰 그래프가 남긴 깊이 쓰기,
        // 히스토리는 읽는 쪽 상태(RW 텍스처) — 그래프 끝에 그 상태로 되돌린다(`RenderGraph::importResources`).
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( _state._listView.size() ); ++viewIndex )
        {
            const FrameViewTarget& view = *_state._listView[viewIndex];
            if ( canCopyDepthHistory( view ) == false )
                continue;
            table.registerTexture( FrameResource::previousSceneDepth()._name, viewIndex, view._transientPool.find( attachmentNames()._sceneDepth )._texture, RenderGraphAccess::DepthWrite );
            table.registerTexture( FrameResource::depthHistory()._name, viewIndex, view._depthHistory._texture, RenderGraphAccess::UnorderedAccessRead );
        }
        // 비 가림 맵 — 깊이는 지난 프레임의 그리기가 셰이더 읽기로 남겼다(뷰 다음 칸).
        if ( canCopyRainOcclusion() )
        {
            const uint32 rainSlot = static_cast<uint32>( _state._listView.size() );
            table.registerTexture( FrameResource::previousSceneDepth()._name, rainSlot, _state._rainOcclusion.getDepth()._texture, RenderGraphAccess::ShaderRead );
            table.registerTexture( FrameResource::depthHistory()._name, rainSlot, _state._rainOcclusion.getParticleDepth()._texture, RenderGraphAccess::UnorderedAccessRead );
        }
    }

    bool FrameGraphExecutor::shouldBuildFrameHZB( const FrameViewTarget& view ) const
    {
        // 컬링이 없는 백엔드(DX11)는 읽을 쪽이 없다. 기록을 버리는 프레임은 지난 깊이가 다른 장면이라 짓지 않는다. 깊이 히스토리 복사가 없으면 읽을 사본이 없다
        // (복사 PSO 는 파이프라인이 DepthHistory 패스를 선언했을 때만 있다 — 그 패스가 같은 판정으로 이번 프레임에 사본을 RW 상태로 채웠다).
        const bool bEnabled = _pDevice != nullptr && _state._settings._bOcclusionCulling != SW_FALSE && _pDevice->getCapabilities()._bGPUCulling != SW_FALSE;
        return bEnabled && view._bHistoryReset == SW_FALSE && canCopyDepthHistory( view );
    }

    void FrameGraphExecutor::recordFrameHZBs( const RenderGraphPassContext& graphContext )
    {
        if ( _pCmd == nullptr || _pDevice == nullptr )
            return;
        // HZB 는 옮긴 깊이 히스토리(지난 장면 깊이)에서 짓는다 — 컬링(GPUCull 패스)이 `HZB` 를 읽어 이 뒤에 온다. 전이는 그래프가 냈다(`beginFrame` 이 등록한 칸).
        const RHIPipelineStateHandle pso = _psoManager.getEnginePSO( RenderPassKind::HZBBuild );
        // 지난 프레임 사각형만 읽는다(자동 렌더 배율 — 사각형 밖은 옛 깊이다).
        const auto recordView = [this, &graphContext, pso]( FrameViewTarget& view, uint32 viewSlot, bool bWriteTiles )
        {
            if ( shouldBuildFrameHZB( view ) == false )
                return false;
            const TransientAttachmentPool::Attachment depth = view._transientPool.find( attachmentNames()._sceneDepth );
            return _occlusion.record( graphContext, viewSlot, *_pDevice, pso, view.scaleToPreviousRect( depth._width, true ), view.scaleToPreviousRect( depth._height, false ),
                                      view._depthHistoryViewProj, bWriteTiles, view );
        };
        // 주 시점만 가림 타일(16 px 밉)도 쓴다 — 게임 스레드의 가림 질의가 되읽는다(복사는 그래프 뒤 `endFrame`).
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( _state._listView.size() ); ++viewIndex )
        {
            FrameViewTarget& view = *_state._listView[viewIndex];
            if ( view._bRenderThisFrame == SW_FALSE )
                continue;
            const bool bMainView = &view == &_state.getMainView();
            if ( recordView( view, viewIndex, bMainView ) && bMainView )
                view._bOcclusionTilesThisFrame = SW_TRUE;
        }
    }

    void FrameGraphExecutor::recordDepthHistory( const RenderGraphPassContext& graphContext, FrameViewTarget& target, uint32 slot )
    {
        // 등록(`registerDepthHistoryResources`)과 같은 판정이다 — 등록한 칸은 레벨 콜백이 지난 장면 깊이를 셰이더 읽기, 히스토리를 렌더 타깃으로 옮겼다.
        if ( canCopyDepthHistory( target ) == false || graphContext.isRegistered( FrameResource::depthHistory(), slot ) == false )
            return;
        drawSceneDepthCopy( *_pCmd, target );
        // 읽는 쪽(HZB 짓기 · GPU 입자 — UAV 읽기로 선언)의 상태로 넘겨 둔다 — 함수 머리말 참고. 이 패스가 쓰기로 선언한 자원이라 패스 안에서 옮긴다.
        graphContext.transition( FrameResource::depthHistory(), RenderGraphAccess::UnorderedAccessRead, slot );
    }

    void FrameGraphExecutor::drawSceneDepthCopy( IRHIPassCommandList& cmd, const FrameViewTarget& target ) const
    {
        drawDepthCopy( cmd, target._transientPool.find( attachmentNames()._sceneDepth )._srv, target._depthHistory );
    }

    void FrameGraphExecutor::drawDepthCopy( IRHIPassCommandList& cmd, RHITextureSRVIndex sourceSRV, const RHITextureSlot& target ) const
    {
        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor        = SW_TRUE;
        beginInfo._colorTargetCount  = 1;
        beginInfo._arrColorTarget[0] = target._texture;
        beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::Clear;
        beginInfo._arrClearColor[0]  = float4{ 1.0f, 0.0f, 0.0f, 0.0f };
        beginInfo._depthTarget       = 0;
        beginInfo._width             = target._width;
        beginInfo._height            = target._height;
        cmd.beginRenderPass( beginInfo );
        cmd.setPipelineState( _psoManager.getEnginePSO( RenderPassKind::DepthHistory ) );
        // 원본 깊이 — 네이티브는 bindless 번호(루트 상수), 에뮬은 SceneDepth 칸(depth_history.hlsl 이 같은 이름으로 읽는다). PSO 뒤에 건다(setPipelineState 가 슬롯 상태를 비운다).
        const bool   bNativeBindless = _pDevice->supportsNativeBindlessSampling();
        const uint32 sourceIndex     = bNativeBindless ? static_cast<uint32>( sourceSRV ) : 0u;
        if ( bNativeBindless == false )
        {
            constexpr uint32 kDepthSlot = shaderslot::emulated::getSlot( "SceneDepth" );
            cmd.bindShaderResource( sourceSRV, kDepthSlot );
        }
        cmd.setGraphicsRootConstants( 0, 1, &sourceIndex );
        cmd.draw( 3, 0 );
        cmd.endRenderPass();
    }

    void FrameGraphExecutor::rememberDepthHistoryViews()
    {
        // 장면 깊이 첨부가 있는 뷰만 — 히스토리가 없으면(2D 파이프라인) 짝을 적을 필요가 없다.
        for ( unique_ptr<FrameViewTarget>& pView : _state._listView )
        {
            if ( pView->_bRenderThisFrame == SW_FALSE || pView->_depthHistory.isValid() == false )
                continue;
            pView->_depthHistoryViewProj = pView->_cullInput._viewProj;
            pView->_depthHistoryEye      = pView->_cullInput._position;
            pView->_bLastFrameDepthValid = SW_TRUE;
        }
    }

    VFXSimulationFrameInput FrameGraphExecutor::makeVFXSimulationInput() const
    {
        VFXSimulationFrameInput input{};
        input._bSimulationPipelineReady = _psoManager.getEnginePSO( RenderPassKind::VFXSimulation ) != 0;
        input._bMeshPipelineReady       = _psoManager.getEnginePSO( RenderPassKind::VFXMeshInstance ) != 0;
        input._bSortPipelineReady       = _psoManager.getEnginePSO( RenderPassKind::VFXParticleSort ) != 0;
        input._bNativeBindless          = _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling();
        input._instanceBuffer           = _state._gpuScene.getInstanceBuffer();
        input._instanceUAV              = _state._gpuScene.getInstanceUAV();
        // 파티클 라이트는 라이트 버퍼의 GPU 구간에 쓴다. 구간이 없는 프레임(요청 없음 · 생산자가 GPU 가 아님)은 슬롯 수 0 이라 쓰지 않는다.
        input._lightBuffer    = _state._lightBuffer.getBuffer()._buffer;
        input._lightUAV       = _state._lightBuffer.getGPULightUAV();
        input._lightBase      = _state._lightBuffer.getGPULightBase();
        input._lightSlotCount = _state._lightBuffer.getGPULightCount();
        // 깊이 충돌은 주 시점의 지난 깊이만 본다(VFXCollisionDef 설명의 한계). 옮길 수 없는 프레임(첫 프레임 · 크기가 바뀐 프레임)은 히스토리가 없다.
        if ( canCopyDepthHistory( _state.getMainView() ) )
        {
            const TransientAttachmentPool::Attachment depth = _state.getMainView()._transientPool.find( attachmentNames()._sceneDepth );
            input._depth._texture                           = _state.getMainView()._depthHistory._texture;
            input._depth._uav                               = _state.getMainView()._depthHistory._uav;
            // 지난 프레임 사각형 크기다(자동 렌더 배율) — 셰이더가 화면 UV 에 곱해 사각형 안 텍셀만 읽는다.
            input._depth._width    = _state.getMainView().scaleToPreviousRect( depth._width, true );
            input._depth._height   = _state.getMainView().scaleToPreviousRect( depth._height, false );
            input._depth._viewProj = _state.getMainView()._depthHistoryViewProj;
            input._depth._eye      = _state.getMainView()._depthHistoryEye;
        }
        // 비 가림 맵을 읽는 강수는 지난 프레임의 맵이다(없는 프레임은 가려지지 않는다 — 장면 깊이와 같은 규칙).
        if ( canCopyRainOcclusion() )
            input._rainOcclusion = _state._rainOcclusion.makeParticleInput();
        return input;
    }

    bool FrameGraphExecutor::isCullViewReady( uint32 cullViewIndex, const RenderView& renderView ) const
    {
        if ( cullViewIndex >= _state._gpuScene.getCullViewCount() )
            return false;
        const GPUCullViewResources& view = _state._gpuScene.getCullView( cullViewIndex );
        return view._indirectArgs._uav != kInvalidDescriptorIndex && view._visibleInstances._uav != kInvalidDescriptorIndex && renderView.isReadyForCulling();
    }

    bool FrameGraphExecutor::enqueueCullView( uint32 cullViewIndex, const RenderView& renderView, const FrameViewTarget* pView )
    {
        if ( isCullViewReady( cullViewIndex, renderView ) == false )
            return false;
        _listCullJob.push_back( CullJob{ cullViewIndex, &renderView, pView } );
        return true;
    }

    void FrameGraphExecutor::dispatchCullView( const RenderGraphPassContext& graphContext, uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount,
                                               const FrameViewTarget* pView )
    {
        const GPUCullViewResources& view = _state._gpuScene.getCullView( cullViewIndex );

        // 컷 · 처음 그리는 프레임은 전환 상태를 비워 단계를 바로 맞춘다(`FrameViewTarget::beginViewHistory` 가 컬링 전에 정했다). 비우기 신호는 한 번만 꺼낸다 — 뒤 단계가 같은 값을 받는다.
        const FrameViewTarget&           lodTarget   = ( pView != nullptr ) ? *pView : _state.getMainView();
        const bool                       bResetState = _state._gpuScene.takeLODStateReset( cullViewIndex ) || lodTarget._bHistoryReset == SW_TRUE;
        FrameRendererUtil::GPUCullParams cullParams{};
        fillCullParams( cullViewIndex, renderView, instanceCount, pView, bResetState, cullParams );
        const bool bHasLODState = _state._gpuScene.hasLODState( cullViewIndex );
        // 가림 판정은 장면을 그리는 뷰의 슬롯만 본다(fillCullParams). 주 시점은 2 단계 가림이면 앞 단계로 돌아 빠진 차례를 적는다 — 지난 HZB 가 있어야 적을 것이 있다.
        const bool              bOccludes    = cullParams._arrHZBControl[3] != 0u;
        const RHIBufferSRVIndex occlusionSRV = bOccludes ? pView->_hzb._srv : _occlusion.getPlaceholderSRV();
        const bool              bMainView    = pView == &_state.getMainView();
        const bool              bOrdersReady = _occludedOrders._uav != kInvalidDescriptorIndex && _occludedOrders._capacityElements >= instanceCount;
        const bool              bEarly       = bMainView && bOccludes && isTwoPhaseOcclusionEnabled() && bOrdersReady && _lateCullCb.isValid();
        cullParams._arrCullPhase[0]          = bEarly ? FrameRendererUtil::kCullPhaseEarly : FrameRendererUtil::kCullPhaseSingle;
        if ( bMainView )
        {
            _bLatePhasePending = bEarly ? 1u : 0u;
            _bLateLODReset     = bResetState ? 1u : 0u;
        }
        // 상수 버퍼는 **뷰마다 자기 것**이다. 하나를 나눠 쓰면 뒤 업로드가 앞 디스패치가 읽을 내용을 덮어쓴다.
        renderView._cullCb.update( *_pCmd, &cullParams, sizeof( cullParams ) );

        // 컬링이 쓰는 버퍼(인자 · 가시 목록 · 전환 상태 · 차례 표시)는 그래프 레벨 콜백이 모든 슬롯을 묶어 UAV 로 옮겨 두었다(`registerCullResources`).
        // 개수를 GPU 에서 되돌린다 — 배치마다 0. 예전에는 CPU 가 프레임마다 모든 뷰의 인자 전체를 다시 올렸다.
        // 나머지 칸(인덱스 수 · 시작 · 정점 시작 · 인스턴스 시작)은 배치 표가 바뀔 때만 CPU 가 올린다. 바인딩은 컬링과 같은 자리(b0 · t1 · u0)라 그대로 이어 쓴다.
        _pCmd->setComputePipelineState( _psoManager.getEnginePSO( RenderPassKind::IndirectArgsReset ) );
        _pCmd->bindComputeConstantBuffer( renderView._cullCb._index, 0 );
        _pCmd->bindComputeShaderResource( _state._gpuScene.getBatchInfoSRV(), 1 );
        _pCmd->bindComputeUAV( view._indirectArgs._uav, 0 );
        _pCmd->dispatchThreads( cullParams._batchCount, 1, 1 );
        graphContext.uavBarrier( RenderGraphBufferUAV{ FrameResource::cullArgs() }, cullViewIndex ); // 되돌린 개수 위에서 컬링이 InterlockedAdd 로 센다
        // PSO 와 바인딩은 **뷰마다** 다시 설정한다. 아래 정렬 패스가 둘 다 교체하므로 다음 뷰가 정렬 PSO 로 컬링을 돌면 안 된다.
        _pCmd->setComputePipelineState( _psoManager.getEnginePSO( RenderPassKind::GPUCull ) );
        // CullParams(b0) / g_Instances(t0) / g_BatchInfo(t1) / g_HZB(t2) / g_InstanceOrder(t3) / g_IndirectArgs(u0) / g_VisibleInstanceIDs(u1) / g_LODState(u2) / g_OccludedOrders(u3).
        // gpu_cull.hlsl 레지스터와 1:1 로 대응한다. 인스턴스(칸 순) · 배치 구간 · 차례 → 칸 목록은 뷰가 공유한다(절두체와 HZB 만 다르다).
        _pCmd->bindComputeConstantBuffer( renderView._cullCb._index, 0 );
        _pCmd->bindComputeShaderResource( _state._gpuScene.getInstanceSRV(), 0 );
        _pCmd->bindComputeShaderResource( _state._gpuScene.getBatchInfoSRV(), 1 );
        if ( occlusionSRV != kInvalidDescriptorIndex )
            _pCmd->bindComputeShaderResource( occlusionSRV, 2 );
        _pCmd->bindComputeShaderResource( _state._gpuScene.getInstanceOrderBuffer()._srv, 3 );
        _pCmd->bindComputeUAV( view._indirectArgs._uav, 0 );
        _pCmd->bindComputeUAV( view._visibleInstances._uav, 1 );
        // 전환 상태(u2)는 크로스페이드가 켜졌을 때만 읽고 쓴다. 없으면 자리만 채운다 — 셰이더가 진행량 0 이면 건드리지 않는다.
        if ( bHasLODState )
        {
            _pCmd->bindComputeUAV( view._lodState._uav, 2 );
        }
        else
        {
            _pCmd->bindComputeUAV( view._visibleInstances._uav, 2 );
        }
        // 차례 표시(u3)는 앞 단계만 쓴다. 한 단계는 자리만 채운다 — 셰이더가 단계 0 이면 건드리지 않는다(같은 uint 버퍼라 형식이 맞다).
        _pCmd->bindComputeUAV( bEarly ? _occludedOrders._uav : view._visibleInstances._uav, 3 );
        _state._gpuMessages.bindCompute( *_pCmd ); // gpu_cull.hlsl 의 메시지(u4) — 메시지 변형 PSO 일 때만 버퍼가 있다
        // **그리는 차례마다** 스레드 하나를 쓴다(차례의 칸은 t3 에서 읽는다). 그래야 보이는 것을 골라 압축할 수 있다.
        _pCmd->dispatchThreads( cullParams._instanceCount, 1, 1 );
        // 압축이 끝났으면 투명 배치의 순서를 CPU 가 정한 순서(인스턴스 번호 오름차순)로 되돌린다. 원자 연산이 준 슬롯 번호는
        // 완료 순서라 그대로 두면 블렌딩이 틀린다. 되돌리기가 GPU 에서 돌므로 투명도 압축과 컬링 이득을 받는다(instance_sort.hlsl).
        // 컬링이 채운 목록을 정렬이 바로 읽는다. 상태가 그대로라 전이로는 알 수 없으므로
        // 선언한 UAV 핸들의 **UAV 배리어**를 따로 건다. 없으면 정렬이 아직
        // 채워지지 않은 목록을 읽는다(백엔드마다 결과가 달라 재현이 어렵다).
        graphContext.uavBarrier( RenderGraphBufferUAV{ FrameResource::cullArgs() }, cullViewIndex );
        graphContext.uavBarrier( RenderGraphBufferUAV{ FrameResource::cullVisibleInstances() }, cullViewIndex );

        const RHIPipelineStateHandle sortPSO        = _psoManager.getEnginePSO( RenderPassKind::InstanceSort );
        const bool                   bSortPassReady = sortPSO != 0 && renderView._sortCb.isValid() && cullParams._batchCount > 0;
        if ( bSortPassReady )
        {
            FrameRendererUtil::GPUSortParams sortParams{};
            // 셰이더는 인스턴스 번호로 정렬한다(정렬 기준은 CPU 한 곳이며 정렬 레이어와 시선 축을 쓴다). 추가 뷰는 CPU 가 그 뷰의 시점으로 다시 정한 순번(t2)으로 정렬한다.
            // 카메라 위치는 상수 버퍼 레이아웃을 지키려고 그대로 싣는다.
            const bool bViewRank            = pView != nullptr && pView->_bHasTransparentRank == SW_TRUE;
            sortParams._arrCameraPos[0]     = renderView._position._x;
            sortParams._arrCameraPos[1]     = renderView._position._y;
            sortParams._arrCameraPos[2]     = renderView._position._z;
            sortParams._instanceCount       = instanceCount;
            sortParams._batchCount          = cullParams._batchCount;
            sortParams._bUseViewRank        = bViewRank ? 1u : 0u;
            sortParams._transparentTailBase = bViewRank ? pView->_transparentTailBase : 0u;
            const RHIBufferSRVIndex rankSRV = bViewRank ? pView->_transparentRank._srv : _transparentRankPlaceholder._srv;
            // 정렬 상수 버퍼도 **뷰마다 자기 것**이다. 뷰마다 다른 값(눈 위치)을 싣으므로 나눠 쓰면 뒤 업로드가 앞 디스패치의 내용을 덮어쓴다.
            renderView._sortCb.update( *_pCmd, &sortParams, sizeof( sortParams ) );

            _pCmd->setComputePipelineState( sortPSO );
            bindSortResources( cullViewIndex, renderView, rankSRV );
            // 배치마다 워크그룹 하나를 쓴다. 그 배치의 목록을 그룹 공유 메모리 안에서 정렬한다.
            _pCmd->dispatchCompute( cullParams._batchCount, 1, 1 );
            dispatchMultiPassSort( graphContext, cullViewIndex, renderView, rankSRV );
        }
        dispatchArgsCompact( *_pCmd, cullViewIndex, renderView._cullCb, cullParams._batchCount );
    }

    void FrameGraphExecutor::dispatchArgsCompact( IRHIPassCommandList& cmd, uint32 cullViewIndex, const RHIConstantBuffer& cullConstants, uint32 batchCount ) const
    {
        if ( _state._gpuScene.hasCompactSegments() == false )
            return;
        // 컬링이 쓴 개수를 읽는다 — 컬링 뒤의 UAV 배리어는 부르는 쪽이 냈다. 쓰는 구간은 읽는 구간과 겹치지 않는다(gpu_cull.hlsl SW_CULL_COMPACT_ARGS).
        cmd.setComputePipelineState( _psoManager.getEnginePSO( RenderPassKind::IndirectArgsCompact ) );
        cmd.bindComputeConstantBuffer( cullConstants._index, 0 );
        cmd.bindComputeShaderResource( _state._gpuScene.getBatchInfoSRV(), 1 );
        cmd.bindComputeUAV( _state._gpuScene.getCullView( cullViewIndex )._indirectArgs._uav, 0 );
        cmd.dispatchThreads( batchCount, 1, 1 );
    }

    void FrameGraphExecutor::bindSortResources( uint32 cullViewIndex, const RenderView& renderView, RHIBufferSRVIndex rankSRV )
    {
        const GPUCullViewResources& view = _state._gpuScene.getCullView( cullViewIndex );
        // 바인딩 위치는 컬링과 같다. 인자와 가시 목록을 그대로 읽고 쓴다. g_ViewRank(t2)는 추가 뷰의 순번 테이블이고, 그 밖의 뷰는 자리 표시용이다.
        _pCmd->bindComputeConstantBuffer( renderView._sortCb._index, 0 );
        _pCmd->bindComputeShaderResource( _state._gpuScene.getInstanceSRV(), 0 );
        _pCmd->bindComputeShaderResource( _state._gpuScene.getBatchInfoSRV(), 1 );
        if ( rankSRV != kInvalidDescriptorIndex )
            _pCmd->bindComputeShaderResource( rankSRV, 2 );
        // 정렬 배치는 컬링이 차례를 적어 두었다 — 정렬한 뒤 차례 → 칸(t3)으로 바꿔 쓴다.
        _pCmd->bindComputeShaderResource( _state._gpuScene.getInstanceOrderBuffer()._srv, 3 );
        _pCmd->bindComputeUAV( view._indirectArgs._uav, 0 );
        _pCmd->bindComputeUAV( view._visibleInstances._uav, 1 );
    }

    void FrameGraphExecutor::dispatchMultiPassSort( const RenderGraphPassContext& graphContext, uint32 cullViewIndex, const RenderView& renderView, RHIBufferSRVIndex rankSRV )
    {
        // 셰이더의 SwRootConstants(instance_sort.hlsl `SW_SORT_MULTI_PASS`)와 같은 배치다.
        struct RootConstants
        {
            uint32 _batch{ 0 };
            uint32 _stage{ 0 };
            uint32 _span{ 0 };
        };
        static_assert( sizeof( RootConstants ) / sizeof( uint32 ) <= shaderslot::kRootConstantDwords, "multi pass sort root constants exceed the root block" );
        const vector<uint32>&        listBatch = _state._gpuScene.getMultiPassSortBatches();
        const RHIPipelineStateHandle pso       = _psoManager.getEnginePSO( RenderPassKind::InstanceSortMultiPass );
        if ( listBatch.empty() || pso == 0 )
            return;
        _pCmd->setComputePipelineState( pso );
        bindSortResources( cullViewIndex, renderView, rankSRV );
        vector<GPUSortStage> listStage;
        for ( const uint32 batchIndex : listBatch )
        {
            GPUScene::collectSortStages( _state._gpuScene.getAllBatches()[batchIndex]._instanceCount, listStage );
            for ( const GPUSortStage& stage : listStage )
            {
                // 앞 단계가 쓴 목록을 다음 단계가 읽는다(상태는 그대로 UAV).
                graphContext.uavBarrier( RenderGraphBufferUAV{ FrameResource::cullVisibleInstances() }, cullViewIndex );
                const RootConstants constants{ batchIndex, static_cast<uint32>( stage._kind ), stage._span };
                _pCmd->setComputeRootConstants( 0, sizeof( constants ) / sizeof( uint32 ), &constants );
                _pCmd->dispatchThreads( stage._threadCount, 1, 1 );
            }
        }
    }

    void FrameGraphExecutor::fillCullParams( uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount, const FrameViewTarget* pView, bool bResetLODState,
                                             FrameRendererUtil::GPUCullParams& outParams ) const
    {
        // 절두체는 **뷰가 이미 가지고 있다.** setViewProjection 이 행렬과 함께 갱신한다.
        // 여기서 다시 뽑으면 행렬만 바뀌고 평면이 바뀌지 않는 상태가 생길 수 있다.
        Memory::copy( outParams._arrPlane, renderView._frustum._arrPlane, sizeof( outParams._arrPlane ) );
        outParams._instanceCount     = instanceCount;
        outParams._batchCount        = _state._gpuScene.getIndirectCommandCount();
        outParams._visibleSlotCount  = _state._gpuScene.getVisibleSlotCount();
        outParams._instanceSlotCount = _state._gpuScene.getInstanceSlotCount();
        // 인스턴스 LOD 선택은 그 뷰를 그리는 카메라로 한다. 추가 뷰는 자기 카메라, 그림자 슬롯과 주 시점은 주 카메라다(그림자는 가장 고운 단계가 아니라 카메라가 보는 단계로 그린다).
        // 절두체 밖의 가림 물체는 화면 크기가 0 이 아니라 거리에서 나온 값을 받는다 — CPU 선택(`LODView::computeScreenSize`)은 그 경우 가장 낮은 단계였다.
        const RenderView& lodView = ( pView != nullptr ) ? pView->_cullInput : _state.getMainView()._cullInput;
        LODView::computeProjectionTerms( lodView._viewProj, outParams._arrLODDepthRow, outParams._arrLODView[3] );
        outParams._arrLODView[0]    = lodView._position._x;
        outParams._arrLODView[1]    = lodView._position._y;
        outParams._arrLODView[2]    = lodView._position._z;
        outParams._arrLODControl[0] = 1u;
        // 전환 상태 버퍼가 있는 뷰만 크로스페이드한다(없으면 진행량 0 — 단계를 바로 바꾸고 버퍼를 읽지 않는다).
        outParams._arrLODControl[1] = bResetLODState ? 1u : 0u;
        outParams._arrLODControl[2] = _state._gpuScene.hasLODState( cullViewIndex ) ? _lodFadeStep : 0u;
        // 가림 판정은 장면을 그리는 뷰의 슬롯만 본다(주 시점 · 추가 뷰). 그림자 슬롯은 빛에서 본 것이라 카메라 깊이로 거르면 그림자가 빠진다.
        if ( pView != nullptr )
            HZBOcclusion::fillCullParams( *pView, _occlusionConfig, outParams );
    }

    bool FrameGraphExecutor::isTwoPhaseOcclusionEnabled() const
    {
        // 간접 멀티 드로우가 없는 백엔드(WebGPU)는 끈다 — 배치마다 따로 내는 간접 드로우에 뒤 단계의 컬링 · 피라미드 재짓기를 더하면 웹의 한 스레드 기록이 이득보다 비싸다(웹 깃발).
        // DX11 은 컬링 자체가 없다(`_bGPUCulling`).
        if ( _pDevice == nullptr || _state._settings._bOcclusionCulling == SW_FALSE || _occlusionConfig._bTwoPhase == false )
            return false;
        const RHICapabilities& caps = _pDevice->getCapabilities();
        return caps._bGPUCulling != SW_FALSE && caps._bMultiDrawIndirect != SW_FALSE;
    }

    bool FrameGraphExecutor::canRunLateOcclusion() const
    {
        const FrameViewTarget& mainView = _state.getMainView();
        return _bLatePhasePending != 0 && _pDevice != nullptr && _psoManager.getEnginePSO( RenderPassKind::GPUCull ) != 0 && isCullViewReady( mainView._cullSlot, mainView._cullInput ) &&
               _occludedOrders._uav != kInvalidDescriptorIndex && _lateCullCb.isValid() && canCopySceneDepth( mainView );
    }

    void FrameGraphExecutor::registerViewGraphResources( FrameBufferTable& table, const FrameViewTarget& view, bool bBuildsSceneDepthHZB )
    {
        // 뒤 단계는 주 시점에서 앞 단계가 차례를 적은 프레임에만 돈다 — 그 판정이 곧 등록이고 "뒤 단계를 기록했는가" 다(패스 몸통은 판정을 다시 하지 않는다).
        const bool bMainView      = &view == &_state.getMainView();
        const bool bHZBReady      = view._hzb.isValid() && view._occlusionTiles.isValid() && canCopySceneDepth( view );
        const bool bLateOcclusion = bMainView && bHZBReady && canRunLateOcclusion();
        if ( bMainView )
            _bLatePhaseRan = bLateOcclusion ? 1u : 0u;
        // 이번 장면 깊이로 HZB 를 짓는 패스(뒤 단계 · 화면 공간 반사)가 쓰는 깊이 사본 · 피라미드 · 가림 타일 — 짓는 패스가 없는 뷰는 등록하지 않는다(그래프가 쓰지 않는 자원에 전이를 내지 않게).
        // 등록 접근은 프레임 그래프가 남긴 상태다 — 뷰 그래프가 들이고 끝에 되돌린다(`RenderGraph::importResources`).
        if ( ( bLateOcclusion || bBuildsSceneDepthHZB ) && bHZBReady )
        {
            table.registerTexture( ViewGraphResource::depthHistory()._name, 0, view._depthHistory._texture, RenderGraphAccess::UnorderedAccessRead );
            table.registerBuffer( ViewGraphResource::hzb()._name, 0, view._hzb, RenderGraphAccess::ShaderRead );
            table.registerBuffer( ViewGraphResource::occlusionTiles()._name, 0, view._occlusionTiles, RenderGraphAccess::ShaderRead );
        }
        if ( bLateOcclusion == false )
            return;
        // 주 시점 컬링 슬롯(0)의 산출물과 차례 표시 — 뒤 단계가 UAV 로 더하고 그리는 쪽 상태로 돌린다. 장면 깊이는 그 패스 안에서 셰이더 읽기로 갔다가 깊이 쓰기로 돌아온다.
        const uint32                cullViewIndex = view._cullSlot;
        const GPUCullViewResources& cullView      = _state._gpuScene.getCullView( cullViewIndex );
        table.registerBuffer( ViewGraphResource::cullArgs()._name, 0, cullView._indirectArgs, RenderGraphAccess::IndirectArgs );
        table.registerBuffer( ViewGraphResource::cullVisibleInstances()._name, 0, cullView._visibleInstances, RenderGraphAccess::ShaderRead );
        if ( _state._gpuScene.hasLODState( cullViewIndex ) )
            table.registerBuffer( ViewGraphResource::cullLODState()._name, 0, cullView._lodState, RenderGraphAccess::UnorderedAccess );
        table.registerBuffer( ViewGraphResource::occludedOrders()._name, 0, _occludedOrders, RenderGraphAccess::UnorderedAccess );
        table.registerAttachment( attachmentNames()._sceneDepth, view._transientPool.find( attachmentNames()._sceneDepth )._texture );
    }

    bool FrameGraphExecutor::recordCurrentDepthHZB( const RenderGraphPassContext& graphContext, FrameViewTarget& view ) const
    {
        // 깊이 사본 · 피라미드는 부르는 패스가 선언했고 짓는 뷰만 등록했다(`registerViewGraphResources`) — 등록이 없으면 짓지 않는다(뒤 단계는 판정 없이 돈다 — 발자국 0).
        const bool bRegistered = graphContext.isRegistered( ViewGraphResource::depthHistory() ) && graphContext.isRegistered( ViewGraphResource::hzb() ) &&
                                 graphContext.isRegistered( ViewGraphResource::occlusionTiles() );
        if ( graphContext.getCommandList() == nullptr || _pDevice == nullptr || bRegistered == false )
        {
            view._bHZBValid = SW_FALSE;
            return false;
        }
        // 깊이 히스토리 텍스처를 사본 자리로 다시 쓴다 — 지난 깊이는 프레임 처음 프레임 패스(입자 충돌 · 지난 HZB)가 다 읽었고, 다음 프레임 처음에 다시 채운다.
        // 장면 깊이는 셰이더 읽기, 사본은 렌더 타깃이다(레벨 콜백 · 부르는 패스가 옮겼다). 그린 뒤 사본을 짓기가 읽는 RW 텍스처로 옮긴다.
        drawSceneDepthCopy( *graphContext.getCommandList(), view );
        graphContext.transition( ViewGraphResource::depthHistory(), RenderGraphAccess::UnorderedAccessRead );
        // 이번 프레임 사각형만 읽는다(자동 렌더 배율 — 사각형 밖은 옛 깊이다).
        const TransientAttachmentPool::Attachment depth = view._transientPool.find( attachmentNames()._sceneDepth );
        return _occlusion.recordInViewPass( graphContext, *_pDevice, _psoManager.getEnginePSO( RenderPassKind::HZBBuild ), view.scaleToRect( depth, true ), view.scaleToRect( depth, false ),
                                            view._cullInput._viewProj, view );
    }

    bool FrameGraphExecutor::recordSceneDepthHZB( const RenderGraphPassContext& graphContext, FrameViewTarget& view ) const
    {
        return recordCurrentDepthHZB( graphContext, view );
    }

    void FrameGraphExecutor::recordLateOcclusion( const RenderGraphPassContext& graphContext, FrameViewTarget& view ) const
    {
        // 뒤 단계를 돌리는 프레임 · 뷰에만 차례 표시를 등록했다(`registerViewGraphResources` — 주 시점, 앞 단계가 적은 프레임). 판정을 여기서 다시 하지 않는다.
        if ( graphContext.isRegistered( ViewGraphResource::occludedOrders() ) == false || graphContext.getCommandList() == nullptr )
            return;
        IRHIPassCommandList&        cmd = *graphContext.getCommandList();
        const RenderGraphDepthWrite sceneDepth{ attachmentNames()._sceneDepth };

        // 1) 이번 프레임 깊이(앞 단계가 그린 것)로 HZB 를 다시 짓는다. 못 지으면 판정 없이(발자국 0) 돌아 적힌 차례를 모두 그린다 — 빠뜨리는 것보다 더 그리는 쪽이다.
        //    장면 깊이는 이 패스가 깊이 쓰기로 선언했다 — 복사 동안만 셰이더 읽기로 옮기고 끝나면 되돌려 그래프가 아는 상태(깊이 쓰기)와 맞춘다. 뒤 단계가 돌지 않는 프레임에는 전이가 없다.
        graphContext.transition( sceneDepth, RenderGraphAccess::ShaderRead );
        std::ignore = recordCurrentDepthHZB( graphContext, view );
        graphContext.transition( sceneDepth, RenderGraphAccess::DepthWrite );

        // 2) 적힌 차례만 다시 판정한다. 개수는 되돌리지 않는다 — 앞 단계 개수 위에 InterlockedAdd 로 더해 같은 간접 인자가 둘을 함께 그린다. 정렬은 다시 돌지 않는다(투명은 더하지 않는다).
        //    컬링 인자 · 가시 목록 · LOD 상태 · 차례 표시는 레벨 콜백이 UAV 로 옮겼다(이 패스의 선언).
        const uint32                     cullViewIndex = view._cullSlot;
        const RenderView&                renderView    = view._cullInput;
        const GPUCullViewResources&      cullView      = _state._gpuScene.getCullView( cullViewIndex );
        const bool                       bLOD          = graphContext.isRegistered( ViewGraphResource::cullLODState() );
        FrameRendererUtil::GPUCullParams params{};
        fillCullParams( cullViewIndex, renderView, _lateInstanceCount, &view, _bLateLODReset != 0, params );
        params._arrCullPhase[0]        = FrameRendererUtil::kCullPhaseLate;
        const RHIBufferSRVIndex hzbSRV = params._arrHZBControl[3] != 0u ? view._hzb._srv : _occlusion.getPlaceholderSRV();
        _lateCullCb.update( cmd, &params, sizeof( params ) );
        // 바인딩은 앞 단계와 같은 자리다(dispatchCullView 의 표).
        cmd.setComputePipelineState( _psoManager.getEnginePSO( RenderPassKind::GPUCull ) );
        cmd.bindComputeConstantBuffer( _lateCullCb._index, 0 );
        cmd.bindComputeShaderResource( _state._gpuScene.getInstanceSRV(), 0 );
        cmd.bindComputeShaderResource( _state._gpuScene.getBatchInfoSRV(), 1 );
        if ( hzbSRV != kInvalidDescriptorIndex )
            cmd.bindComputeShaderResource( hzbSRV, 2 );
        cmd.bindComputeShaderResource( _state._gpuScene.getInstanceOrderBuffer()._srv, 3 );
        cmd.bindComputeUAV( cullView._indirectArgs._uav, 0 );
        cmd.bindComputeUAV( cullView._visibleInstances._uav, 1 );
        cmd.bindComputeUAV( bLOD ? cullView._lodState._uav : cullView._visibleInstances._uav, 2 );
        cmd.bindComputeUAV( _occludedOrders._uav, 3 );
        _state._gpuMessages.bindCompute( cmd );
        cmd.dispatchThreads( _lateInstanceCount, 1, 1 );
        // 더한 개수로 다시 모은다 — 앞 단계가 모은 인자는 깊이 프리패스가 이미 그렸다.
        graphContext.uavBarrier( ViewGraphResource::cullArgs() );
        dispatchArgsCompact( cmd, cullViewIndex, _lateCullCb, params._batchCount );

        // 3) 그리는 쪽이 읽는 상태(등록 접근)로 — 뒤 패스(G버퍼 · 포워드 불투명)가 더한 개수로 그린다. LOD 상태 · 차례 표시는 등록 접근이 UAV 라 그대로 둔다.
        graphContext.transition( ViewGraphResource::cullArgs(), RenderGraphAccess::IndirectArgs );
        graphContext.transition( ViewGraphResource::cullVisibleInstances(), RenderGraphAccess::ShaderRead );
    }

    bool FrameGraphExecutor::shouldUpdateRealtimeProbes() const
    {
        return _state._probeUpdater.isActive() && _state._lightBuffer.findProbeBuffer() != nullptr && _state._lightBuffer.getProbeUAV() != kInvalidDescriptorIndex &&
               _state._probeUpdater.getHistoryBuffer().isValid();
    }

    void FrameGraphExecutor::dispatchRealtimeProbes( const RenderGraphPassContext& graphContext )
    {
        // 두 쓰기 버퍼는 도는 프레임에만 등록했다(`beginFrame` 의 `shouldUpdateRealtimeProbes` — 등록 조건이 곧 실행 조건). 등록이 없으면(2D 씬 · 프로브 없는 씬) 기록하지 않는다.
        const bool bRegistered = graphContext.isRegistered( RenderGraphBufferUAV{ FrameResource::probeIrradiance() } ) &&
                                 graphContext.isRegistered( RenderGraphBufferUAV{ FrameResource::probeHistory() } );
        if ( _pCmd == nullptr || bRegistered == false )
            return;
        SW_PROFILE_SCOPE( "RT.Probe.Update" );
        _state._probeUpdater.dispatch( *_pCmd, _psoManager.getEnginePSO( RenderPassKind::ProbeUpdate ), _state._lightBuffer, _transparentRankPlaceholder._srv );
    }
} // namespace sw
