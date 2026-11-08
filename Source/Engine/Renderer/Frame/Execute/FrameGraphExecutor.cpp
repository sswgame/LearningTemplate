/**
 * @file FrameGraphExecutor.cpp
 * @brief 프레임 패스 종류마다의 실행입니다 — 깊이 히스토리 · 가림 타일 · GPU 입자 · 인스턴스 애니메이션 · 메시 모프 · 메시 스킨 · GPU 컬링(+ 정렬) · 라이트 그리드 ·
 *        볼륨 안개 · 실시간 프로브 · 화면 마커 투영.
 * @details 순서는 여기에 없다. 파이프라인 XML 의 `_listFramePass` 가 자원(`_listResource`)의 읽기 · 쓰기로 선언하고 프레임 그래프가 정한다
 *          (애니메이션 · 모프 · 스킨이 컬링보다 앞 — 컬링이 이번 프레임 바운드로 판정한다 — 은 `InstanceBuffer` · `DeformVertices` 의 쓰기 · 읽기다).
 *          각 패스는 자기가 쓰는 버퍼의 배리어를 스스로 건다(쓰기 전 UAV, 끝나면 읽기 상태) — 그래프 자원 이름은 순서만 정하고 버퍼를 모른다.
 *          상수 버퍼는 `RHIConstantBuffer` 이고 프레임 데이터(`FrameRenderState::_deform` · 뷰)에 있다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Execute/FrameGraphExecutor.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Animation/Scene/AnimationLOD.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRenderState.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PostProcess/FrameFog.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"

namespace sw
{
    /**
     * @brief `-gv_gpuCulling=0` 이면 GPU 컬링 컴퓨트 디스패치를 건너뜁니다(인다이렉트 드로우는 그대로 실행합니다).
     * @details 간접 인자는 GPUScene 이 CPU 에서 이미 채워 두므로, 이 디스패치만 빼면 "컴퓨트가 인자를
     *          망치는가" 를 백엔드별로 가를 수 있다. 기본은 켬이다.
     */
    SW_GLOBAL_VARIABLE( bool, gv_gpuCulling, true, "GPU 컬링 컴퓨트 디스패치 (false=건너뜀, 진단용)" );

    /**
     * @brief `-gv_meshMorphDebugMode=<Normal|ForceOn|SkipDispatch>` 는 GPU 메시 모프 경로를 백엔드 능력 테이블과 **상관없이** 돌려 봅니다(`MeshMorphDiagMode`).
     * @details Normal 은 평소대로(`RHICapabilities::_bGPUMeshMorph` 를 따름), ForceOn 은 능력 테이블을 무시하고 켬,
     *          SkipDispatch 는 켜되 **컴퓨트 디스패치를 건너뛰고 레스트를 결과 배치로 묶어 결과 버퍼에 올린다**.
     *
     *          SkipDispatch 의 정답은 **레스트 포즈와 같은 이미지**다. 컴퓨트가 아예 돌지 않기 때문이다. 그래서
     *          SkipDispatch 만으로 "컴퓨트가 범인인가" 를 가를 수 있다. 예전의 IndexTag(노멀 칸에 원소 번호)는 결과가 옥타헤드럴로 묶인 뒤
     *          번호를 실을 칸이 없어 지웠다.
     */
    SW_GLOBAL_VARIABLE( MeshMorphDiagMode, gv_meshMorphDebugMode, MeshMorphDiagMode::Normal, "메시 모프 진단 (Normal 평소 / ForceOn 강제 켬 / SkipDispatch 디스패치 생략)" );

    FrameGraphExecutor::FrameGraphExecutor( IRHIDevice* const& pDevice, FrameRenderState& state, FramePSOManager& psoManager, FrameFog& fog )
        : _pDevice{ pDevice }
        , _state{ state }
        , _psoManager{ psoManager }
        , _fog{ fog }
        , _occlusion{}
        , _gpuAssert{}
    {
    }

    void FrameGraphExecutor::ensureResources()
    {
        // 셰이더 assert 링은 PSO 보다 먼저 만든다 — assert 변형 PSO 는 이 버퍼 없이는 돌릴 수 없으므로 만들지 못하면 멈춘다(폴백 없음).
        if ( _gpuAssert.initialize( _pDevice ) == false )
            SW_FATAL( "GPU assert is on but its ring could not be created - see the log above" );
        // 정렬 디스패치의 t2 자리표 — 순번 표가 없는 뷰도 빈 슬롯으로 디스패치하지 않는다(DX12 는 빈 디스크립터를 읽으면 폴트).
        constexpr uint32 kZeroRank = 0;
        if ( _transparentRankPlaceholder.ensureCapacity( _pDevice, sizeof( uint32 ), 1, RHIBufferUsage::ShaderResource, true, false, &kZeroRank ) == false )
            SW_LOG_ERROR_ID( "FrameGraphExecutor.FailedCreateTransparentRank", "Failed to create the transparent rank placeholder - extra views draw transparency in the main view order" );
        _occlusion.ensurePlaceholder( _pDevice );
    }

    void FrameGraphExecutor::releaseResources()
    {
        _transparentRankPlaceholder.release( _pDevice );
        _occlusion.release( _pDevice );
        _gpuAssert.shutdown();
    }

    void FrameGraphExecutor::beginFrame()
    {
        _bGPUCullingActive = 0;
        _gpuAssert.poll();
        // 가림 타일 표시는 OcclusionTiles 패스가 다시 켠다. 패스가 없는 파이프라인에서 지난 파이프라인의 표시가 남아 컬링이 낡은 타일을 읽지 않게 비운다.
        _state._mainView._bOcclusionTilesThisFrame = SW_FALSE;
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            pView->_bOcclusionTilesThisFrame = SW_FALSE;
        }
    }

    void FrameGraphExecutor::executePass( RenderPassType type, IRHICommandList& cmd )
    {
        _pCmd = &cmd;
        switch ( type )
        {
            case RenderPassType::DepthHistory:
            {
                recordDepthHistories();
                break;
            }
            case RenderPassType::OcclusionTiles:
            {
                recordOcclusionTiles();
                break;
            }
            case RenderPassType::VFXSimulation:
            {
                // 메시 입자가 인스턴스 칸에 행렬 · 바운드를 쓰고(VFXMeshInstance — 같은 패스의 두 번째 디스패치), 빌보드 · 띠는 투명 패스가 렌더 입력과 간접 인자를 읽는다.
                _state._particleRenderer.recordSimulation( *_pCmd, _psoManager.getEnginePSO( RenderPassType::VFXSimulation ), _psoManager.getEnginePSO( RenderPassType::VFXMeshInstance ) );
                break;
            }
            case RenderPassType::InstanceAnimation:
            {
                dispatchInstanceAnimation( static_cast<uint32>( _state._gpuScene.getInstances().size() ) );
                break;
            }
            case RenderPassType::MeshMorph:
            {
                dispatchMeshMorph();
                break;
            }
            case RenderPassType::MeshSkin:
            {
                dispatchMeshSkin();
                break;
            }
            case RenderPassType::GPUCull:
            {
                dispatchCullAndSort( static_cast<uint32>( _state._gpuScene.getInstances().size() ) );
                break;
            }
            case RenderPassType::LightGrid:
            {
                dispatchLightGrid();
                break;
            }
            case RenderPassType::VolumetricFog:
            {
                _fog.dispatchVolumetricFog( *_pCmd, _state._lightBuffer, _transparentRankPlaceholder._srv );
                break;
            }
            case RenderPassType::ProbeUpdate:
            {
                dispatchRealtimeProbes();
                break;
            }
            case RenderPassType::CanvasMarker:
            {
                // 캔버스 업로드(기록 전)가 올린 앵커 원소를 GPU 가 덮어쓴다. 주 출력 캔버스를 그리는 패스(뷰 그래프)보다 앞이다.
                _state._canvasRenderer.recordMarkerProjection( *_pCmd, _psoManager.getEnginePSO( RenderPassType::CanvasMarker ), _state._gpuScene.getInstanceBuffer(),
                                                               _state._gpuScene.getInstanceSRV(), _transparentRankPlaceholder._srv, _gpuAssert,
                                                               _state._mainView._settings._viewMode == RenderViewMode::MarkerSlots );
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
        // assert 가 켜진 디스패치(컬링 · 모프 · 마커)가 끝났다 — 링을 읽기 버퍼로 복사하는 명령을 넣는다.
        _gpuAssert.requestReadback( cmd );
    }

    bool FrameGraphExecutor::usesGPUGeneratedCommands() const
    {
        // 컴퓨트가 드로우 커맨드를 만드는 경로를 쓰려면 인다이렉트 드로우와 컬링 디스패치가 둘 다 켜져
        // 있고 컬링 PSO 가 실제로 만들어져 있어야 한다. 셋 중 하나라도 없으면 CPU 가 채운 개수로 그린다.
        return gv_gpuCulling && _psoManager.getEnginePSO( RenderPassType::GPUCull ) != 0;
    }

    void FrameGraphExecutor::dispatchInstanceAnimation( uint32 instanceCount )
    {
        // 인스턴스 애니메이션이 컬링보다 먼저다(XML 의 `InstanceBuffer` 쓰기 · 읽기). 순서가 뒤집히면 컬링이
        // **이번 프레임에 회전하기 전의** 바운드로 판정한다. 애니메이션이 회전만 바꾸므로 지금 씬에서는
        // 결과가 같지만, 이동을 넣는 순간 한 프레임 늦은 컬링이 된다.
        // 회전을 요청한 인스턴스가 하나도 없으면 디스패치 자체를 건너뛴다. 안 그러면 회전을 쓰지 않는 씬도
        // 매 프레임 인스턴스당 96바이트를 읽고 아무 일도 하지 않는다. 컬링 능력과는 무관하다(DX11 도 돈다).
        const bool bSpinAnimationNeeded = _state._gpuScene.isUploaded() && instanceCount > 0 && _state._gpuScene.getSpinInstanceCount() > 0;
        if ( bSpinAnimationNeeded )
        {
            const RHIPipelineStateHandle animationPSO        = _psoManager.getEnginePSO( RenderPassType::InstanceAnimation );
            const bool                   bSpinAnimationReady = animationPSO != 0 && _state._deform._instanceAnimationCb.isValid() &&
                                             _state._gpuScene.getInstanceUAV() != kInvalidDescriptorIndex;
            if ( bSpinAnimationReady )
            {
                FrameRendererUtil::GPUAnimationParams animationParams{};
                animationParams._time = _state.getAnimationTime();
                // 기준 각속도와 편차 폭(라디안/초). 편차가 기준보다 커야 "다 같은 속도" 로 보이지 않는다.
                animationParams._baseSpeed     = FrameRendererUtil::kGPUSpinBaseSpeed;
                animationParams._speedRange    = FrameRendererUtil::kGPUSpinSpeedRange;
                animationParams._instanceCount = instanceCount;
                _state._deform._instanceAnimationCb.update( *_pCmd, &animationParams, sizeof( animationParams ) );

                // **쓰기 전에** UAV 상태로 옮긴다. 인스턴스 버퍼는 직전 프레임에 정점 셰이더가 읽던
                // (그리고 방금 CPU 업로드가 쓴) 상태라, 이 전이 없이 UAV 로 쓰면 DX12 와 Vulkan 에서 쓰기가
                // 유효하지 않다. 화면은 조용히 예전 값 그대로다.
                _pCmd->transitionBuffer( _state._gpuScene.getInstanceBuffer(), RHIBufferState::UnorderedAccess );
                _pCmd->setComputePipelineState( animationPSO );
                // AnimationParams(b0) / g_InstancesRW(u0). instance_animation.hlsl 레지스터와 1:1 로 대응한다.
                _pCmd->bindComputeConstantBuffer( _state._deform._instanceAnimationCb._index, 0 );
                _pCmd->bindComputeUAV( _state._gpuScene.getInstanceUAV(), 0 );
                const uint32 animationGroups = ( instanceCount + 63u ) / 64u;
                if ( animationGroups > 0 )
                    _pCmd->dispatchCompute( animationGroups, 1, 1 );
                // 다음 디스패치(컬링)와 정점 셰이더가 이 결과를 읽는다. 쓰기가 끝났음을 알린다.
                _pCmd->transitionBuffer( _state._gpuScene.getInstanceBuffer(), RHIBufferState::ShaderResource );
            }
        }
    }

    bool FrameGraphExecutor::isDeformPassDeclared() const
    {
        return _psoManager.getEnginePSO( RenderPassType::MeshMorph ) != 0 || _psoManager.getEnginePSO( RenderPassType::MeshSkin ) != 0;
    }

    MeshMorphDiagMode FrameGraphExecutor::getEffectiveMeshMorphDiag() const
    {
        return gv_meshMorphDebugMode;
    }

    bool FrameGraphExecutor::isMeshMorphDiagValid()
    {
        // 모든 열거자를 나열한다 — 열거자를 더하면 여기서 컴파일 경고가 난다. 범위 밖 값은 Normal 로 바꾸지 않고 알린다.
        switch ( gv_meshMorphDebugMode )
        {
            case MeshMorphDiagMode::Normal:
            case MeshMorphDiagMode::ForceOn:
            case MeshMorphDiagMode::SkipDispatch:
            {
                return true;
            }
        }
        SW_LOG_ERROR_ID( "FrameGraphExecutor.GvMeshMorphDebugModeNoMode", "gv_meshMorphDebugMode has no mode for the value %# (Normal, ForceOn or SkipDispatch)", static_cast<uint32>( gv_meshMorphDebugMode ) );
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
            const bool bDrawn = palette._skinSlot < deform._listScratchDrawnSkinSlot.size() && deform._listScratchDrawnSkinSlot[palette._skinSlot] == SW_TRUE;
            const bool bSkin  = palette._mesh != nullptr && palette._mesh->hasSkin() && palette._mesh->findVertexAnimation() == nullptr;
            if ( bDrawn == false || bSkin == false )
                continue;
            deform._listScratchSkinMesh.push_back( palette._mesh.get() );
            deform._listScratchSkinSlot.push_back( palette._skinSlot );
            deform._listScratchSkinLOD.push_back( palette._lodIndex );
        }
        SW_PROFILE_COUNT( "RT.Deform.skinSlots", deform._listScratchSkinSlot.size() );
    }

    void FrameGraphExecutor::dispatchMeshMorph()
    {
        const MeshMorphDiagMode morphDiag = getEffectiveMeshMorphDiag();
        if ( _pDevice == nullptr || _pCmd == nullptr )
            return;
        if ( _pDevice->getCapabilities()._bGPUMeshMorph == SW_FALSE && morphDiag == MeshMorphDiagMode::Normal )
            return;
        if ( _state._deform._meshMorphPool.isDispatchable() == false )
            return;

        // 진단 SkipDispatch: 컴퓨트를 돌리지 않고 레스트를 결과 배치로 묶어 결과 버퍼에 올린다 — 정점 셰이더의 결과 읽기(원소 번호 · 풀기)를 컴퓨트 없이 본다.
        // 메시마다 풀 구간(baseOf)에 쓴다 — 구간은 목록 순서로 이어져 있지 않다(C-43 부분 갱신).
        if ( morphDiag == MeshMorphDiagMode::SkipDispatch )
        {
            for ( const Mesh* pMesh : _state._deform._listScratchMorphMesh )
            {
                const uint32 base = ( pMesh != nullptr ) ? _state._deform._meshMorphPool.baseOf( pMesh ) : GPUMeshMorphPool::kInvalidBase;
                if ( base == GPUMeshMorphPool::kInvalidBase )
                    continue;
                _state._deform._listScratchRestResult.clear();
                for ( const RHIVertex& vertex : pMesh->getVertices() )
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

        const RHIPipelineStateHandle morphPSO         = _psoManager.getEnginePSO( RenderPassType::MeshMorph );
        const RHIDescriptorIndex     programSRV       = _state._deform._meshMorphPool.getDeformProgramBuffer()._srv;
        const bool                   bMissingResource = morphPSO == 0 || _state._deform._meshMorphCb.isValid() == false || programSRV == kInvalidDescriptorIndex;
        if ( bMissingResource )
            return;

        FrameRendererUtil::GPUMorphParams morphParams{};
        morphParams._time        = _state.getAnimationTime();
        morphParams._amplitude   = FrameRendererUtil::kMeshMorphAmplitude;
        morphParams._frequency   = FrameRendererUtil::kMeshMorphFrequency;
        morphParams._vertexCount = _state._deform._meshMorphPool.getMorphVertexCount();
        if ( morphParams._vertexCount == 0 )
            return;
        _state._deform._meshMorphCb.update( *_pCmd, &morphParams, sizeof( morphParams ) );

        // 쓰기 전에 UAV 로, 드로우 전에 다시 SRV 로 전이한다. 정점 셰이더가 이 버퍼를 읽으므로 배리어가 빠지면
        // DX12 와 Vulkan 에서 조용히 예전 값이 나온다(인스턴스 애니메이션과 같은 함정이다).
        _pCmd->transitionBuffer( _state._deform._meshMorphPool.getMorphBuffer()._buffer, RHIBufferState::UnorderedAccess );
        _pCmd->setComputePipelineState( morphPSO );
        _pCmd->bindComputeConstantBuffer( _state._deform._meshMorphCb._index, 0 );
        // MorphParams(b0) / g_RestVertices(t0) / g_DeformPrograms(t1, 칸 표 + 변형 프로그램) / g_MorphVerticesRW(u0). mesh_morph.hlsl 레지스터와 1:1 로 대응한다.
        _pCmd->bindComputeShaderResource( _state._deform._meshMorphPool.getRestBuffer()._srv, 0 );
        _pCmd->bindComputeShaderResource( programSRV, 1 );
        _pCmd->bindComputeUAV( _state._deform._meshMorphPool.getMorphBuffer()._uav, 0 );
        _gpuAssert.bind( *_pCmd ); // mesh_morph.hlsl 의 assert(u4) — assert 변형 PSO 일 때만 버퍼가 있다
        const uint32 morphGroups = ( morphParams._vertexCount + 63u ) / 64u;
        if ( morphGroups > 0 )
            _pCmd->dispatchCompute( morphGroups, 1, 1 );
        _pCmd->transitionBuffer( _state._deform._meshMorphPool.getMorphBuffer()._buffer, RHIBufferState::ShaderResource );
    }

    void FrameGraphExecutor::dispatchMeshSkin()
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
        const RHIPipelineStateHandle skinPSO = _psoManager.getEnginePSO( RenderPassType::MeshSkin );
        if ( skinPSO == 0 || _state._deform._meshSkinCb.isValid() == false )
            return;

        FrameRendererUtil::GPUSkinParams skinParams{};
        skinParams._skinVertexBase = _state._deform._meshMorphPool.getSkinVertexBase() + _state._deform._meshMorphPool.getSkinCurrentShift(); // 이번 벌

        skinParams._skinVertexCount   = _state._deform._meshMorphPool.getSkinVertexCount();
        skinParams._skinBoneCount     = _state._deform._meshMorphPool.getSkinBoneCount();
        skinParams._skinInstanceCount = _state._deform._meshMorphPool.getSkinInstanceCount();
        _state._deform._meshSkinCb.update( *_pCmd, &skinParams, sizeof( skinParams ) );

        // 모프와 같은 결과 버퍼의 뒤 구간에 쓴다. 쓰기 전에 UAV 로, 드로우 전에 다시 SRV 로 전이한다(배리어가 빠지면 DX12 와 Vulkan 에서 예전 값이 나온다).
        _pCmd->transitionBuffer( _state._deform._meshMorphPool.getMorphBuffer()._buffer, RHIBufferState::UnorderedAccess );
        _pCmd->setComputePipelineState( skinPSO );
        // SkinParams(b0) / g_RestVertices(t0, 원본 레스트) / g_SkinWeights(t1) / g_SkinPalette(t2) / g_SkinInstances(t3) / g_MorphVerticesRW(u0).
        // mesh_skin.hlsl 레지스터와 1:1 로 대응한다.
        _pCmd->bindComputeConstantBuffer( _state._deform._meshSkinCb._index, 0 );
        _pCmd->bindComputeShaderResource( _state._deform._meshMorphPool.getSkinRestBuffer()._srv, 0 );
        _pCmd->bindComputeShaderResource( _state._deform._meshMorphPool.getSkinWeightBuffer()._srv, 1 );
        _pCmd->bindComputeShaderResource( _state._deform._meshMorphPool.getSkinPaletteBuffer()._srv, 2 );
        _pCmd->bindComputeShaderResource( _state._deform._meshMorphPool.getSkinInstanceBuffer()._srv, 3 );
        _pCmd->bindComputeUAV( _state._deform._meshMorphPool.getMorphBuffer()._uav, 0 );
        const uint32 skinGroups = ( skinParams._skinVertexCount + 63u ) / 64u;
        if ( skinGroups > 0 )
            _pCmd->dispatchCompute( skinGroups, 1, 1 );
        _pCmd->transitionBuffer( _state._deform._meshMorphPool.getMorphBuffer()._buffer, RHIBufferState::ShaderResource );
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

    void FrameGraphExecutor::dispatchCullAndSort( uint32 instanceCount )
    {
        // 컬링은 GPUScene 이 "개수를 컴퓨트에 맡겼다" 고 답할 때만 돈다. 그래야 인자의 초기 개수(0)와
        // 디스패치 여부가 절대 어긋나지 않는다. 어긋나면 한쪽은 빈 화면, 다른 쪽은 낡은 목록이다.
        //
        // **뷰마다 한 번씩** 돈다. 컬링 결과는 절두체에 종속이라, 메인 카메라로 거른 목록을 그림자 패스가
        // 쓰면 화면 밖에서 화면 안으로 그림자를 드리우는 물체가 사라진다. 언리얼이 뷰마다
        // FInstanceCullingContext 를 두는 것과 같은 이유다. 이번 프레임에 그리는 추가 뷰(CCTV, PiP)도 자기 슬롯을 돈다.
        if ( _state._gpuScene.isUploaded() == false || _state._gpuScene.areIndirectCountsGPUFilled() == false )
            return;
        const bool bCullingUnavailable = _psoManager.getEnginePSO( RenderPassType::GPUCull ) == 0 || _state._gpuScene.getInstanceSRV() == kInvalidDescriptorIndex ||
                                         _state._gpuScene.getBatchInfoSRV() == kInvalidDescriptorIndex;
        if ( bCullingUnavailable )
            return;

        computeLODFadeStep();
        bool bAllViewsCulled = true;
        // 캐스케이드가 각자 슬롯을 돌고 점광 · 스폿 타일이 없으면 합친 칸(주 시점 그림자 슬롯)은 아무 패스도 읽지 않는다 — 돌리지 않는다.
        const bool bMainShadowSlotUnused = _state._cascadeCullCount > 0 && _state._frameShadow._listLocalTile.empty();
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
        {
            if ( bMainShadowSlotUnused && viewIndex == static_cast<uint32>( RenderViewType::Shadow ) )
                continue;
            bAllViewsCulled = dispatchCullView( viewIndex, _state._arrView[viewIndex], instanceCount, nullptr ) && bAllViewsCulled;
        }
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            // 컬링하지 못한 추가 뷰는 그리지 않는다. 가시 목록을 바인딩하는 프레임에 갱신되지 않은 목록을 읽게 된다.
            // 그림자 패스도 이 뷰의 그림자 볼륨으로 따로 거른다 — 주 시점의 그림자 슬롯을 쓰면 이 뷰만 보는 곳의 가리는 물체가 빠진다.
            if ( pView->_bRenderThisFrame == SW_FALSE )
                continue;
            const bool bViewCulled   = dispatchCullView( pView->_cullSlot, pView->_cullInput, instanceCount, pView.get() );
            const bool bShadowCulled = bViewCulled && dispatchCullView( pView->_shadowCullSlot, pView->_shadowCullInput, instanceCount, nullptr );
            if ( bShadowCulled == false )
                pView->_bRenderThisFrame = SW_FALSE;
        }
        // 주 시점 캐스케이드마다 자기 절두체(그 캐스케이드의 그림자 행렬)로 거른다. 정렬은 그림자에 필요 없어 상수 버퍼가 없다(dispatchCullView 가 건너뛴다).
        for ( uint32 cascadeIndex = 0; cascadeIndex < _state._cascadeCullCount; ++cascadeIndex )
        {
            RenderView& cascadeInput = _state._arrCascadeCullInput[cascadeIndex];
            cascadeInput.setViewProjection( _state._mainView._shadow._arrCascadeTile[cascadeIndex]._viewProj );
            bAllViewsCulled = dispatchCullView( _state._cascadeCullSlotBase + cascadeIndex, cascadeInput, instanceCount, nullptr ) && bAllViewsCulled;
        }
        _bGPUCullingActive = bAllViewsCulled ? 1u : 0u;
    }

    void FrameGraphExecutor::dispatchLightGrid()
    {
        // 생산자가 GPU 인 프레임에만 뷰 수가 0 보다 크다(GPULightBuffer::updateGPUGrid). 그때 PSO 와 버퍼는 이미 확인됐다(selectLightGridProducer, updateGPUGrid) —
        // 여기서 건너뛰면 채우지 않은 그리드를 셰이더가 읽으므로 더 거르지 않는다.
        const uint32 viewCount = _state._lightBuffer.getGPUGridViewCount();
        if ( viewCount == 0 || _pCmd == nullptr )
            return;
        const RHIStructuredBuffer& gridBuffer = _state._lightBuffer.getGPUGridBuffer();
        _pCmd->transitionBuffer( gridBuffer._buffer, RHIBufferState::UnorderedAccess );
        _pCmd->setComputePipelineState( _psoManager.getEnginePSO( RenderPassType::LightGrid ) );
        // g_LightGridLights(t0) / g_LightGridViews(t1) / g_LightGridRW(u0). light_grid.hlsl 레지스터와 1:1 로 대응한다. 상수 버퍼는 없다(뷰마다의 값은 t1).
        _pCmd->bindComputeShaderResource( _state._lightBuffer.getBuffer()._srv, 0 );
        _pCmd->bindComputeShaderResource( _state._lightBuffer.getGPUGridViewBuffer()._srv, 1 );
        _pCmd->bindComputeUAV( gridBuffer._uav, 0 );
        // 그룹 하나 = 뷰 하나. 그룹 안에서 세기 · 접두사 합 · 채우기를 그룹 동기화로 잇는다.
        _pCmd->dispatchCompute( viewCount, 1, 1 );
        // 조명 패스(포워드 · 디퍼드 · 투명)가 t12 로 읽는다. 쓰기가 끝났음을 알린다(빠지면 DX12 · Vulkan 에서 예전 그리드를 읽는다).
        _pCmd->transitionBuffer( gridBuffer._buffer, RHIBufferState::ShaderResource );
    }

    bool FrameGraphExecutor::canCopyDepthHistory( const FrameViewTarget& view ) const
    {
        const bool bHasSource = view._bLastFrameDepthValid == SW_TRUE && view._depthHistory != 0 && view._depthHistoryUAV != kInvalidDescriptorIndex;
        if ( bHasSource == false || _psoManager.getEnginePSO( RenderPassType::DepthHistory ) == 0 )
            return false;
        return view._transientPool.find( attachmentNames()._sceneDepth )._texture != 0;
    }

    void FrameGraphExecutor::recordDepthHistories()
    {
        if ( _pCmd == nullptr || _pDevice == nullptr )
            return;
        // 주 시점과 추가 뷰 — 장면 깊이 첨부는 프레임 사이에 그대로 남아 있다(이번 프레임 뷰 그래프가 쓰기 전이다).
        recordDepthHistory( _state._mainView );
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            recordDepthHistory( *pView );
        }
    }

    void FrameGraphExecutor::recordOcclusionTiles()
    {
        if ( _pCmd == nullptr || _pDevice == nullptr )
            return;
        // 가림 타일은 옮긴 깊이 히스토리에서 짓는다 — 컬링(GPUCull 패스)이 `OcclusionTiles` 를 읽어 이 뒤에 온다. 쉬는 추가 뷰는 컬링하지 않으므로 짓지 않는다.
        const RHIPipelineStateHandle occlusionPSO = _psoManager.getEnginePSO( RenderPassType::OcclusionTiles );
        _occlusion.record( _pCmd, *_pDevice, occlusionPSO, canCopyDepthHistory( _state._mainView ), _state._mainView );
        _occlusion.requestReadback( _pCmd, _pDevice, _state._mainView );
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_TRUE )
                _occlusion.record( _pCmd, *_pDevice, occlusionPSO, canCopyDepthHistory( *pView ), *pView );
        }
    }

    void FrameGraphExecutor::recordDepthHistory( FrameViewTarget& target )
    {
        if ( canCopyDepthHistory( target ) == false )
            return;
        const TransientAttachmentPool::Attachment depth = target._transientPool.find( attachmentNames()._sceneDepth );
        _pCmd->prepareTextureForShaderRead( depth._texture );
        _pCmd->prepareTextureForRenderTarget( target._depthHistory );

        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor        = SW_TRUE;
        beginInfo._colorTargetCount  = 1;
        beginInfo._arrColorTarget[0] = target._depthHistory;
        beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::Clear;
        beginInfo._arrClearColor[0]  = float4{ 1.0f, 0.0f, 0.0f, 0.0f };
        beginInfo._depthTarget       = 0;
        beginInfo._width             = depth._width;
        beginInfo._height            = depth._height;
        _pCmd->beginRenderPass( beginInfo );
        _pCmd->setPipelineState( _psoManager.getEnginePSO( RenderPassType::DepthHistory ) );
        // 원본 깊이 — 네이티브는 bindless 번호(루트 상수), 에뮬은 SceneDepth 칸(depth_history.hlsl 이 같은 이름으로 읽는다). PSO 뒤에 건다(setPipelineState 가 슬롯 상태를 비운다).
        const bool   bNativeBindless = _pDevice->supportsNativeBindlessSampling();
        const uint32 sourceIndex     = bNativeBindless ? static_cast<uint32>( depth._srv ) : 0u;
        if ( bNativeBindless == false )
        {
            constexpr uint32 kDepthSlot = shaderslot::emulated::getSlot( "SceneDepth" );
            _pCmd->bindShaderResource( depth._srv, kDepthSlot );
        }
        _pCmd->setGraphicsRootConstants( 0, 1, &sourceIndex );
        _pCmd->draw( 3, 0 );
        _pCmd->endRenderPass();
        // 읽는 쪽(가림 타일 · GPU 입자)의 상태로 넘겨 둔다 — 함수 머리말 참고.
        _pCmd->prepareTextureForUnorderedAccess( target._depthHistory );
    }

    void FrameGraphExecutor::rememberDepthHistoryViews()
    {
        // 장면 깊이 첨부가 있는 뷰만 — 히스토리가 없으면(2D 파이프라인) 짝을 적을 필요가 없다.
        const RenderView& mainView = _state.view( RenderViewType::Main );
        if ( _state._mainView._depthHistory != 0 )
        {
            _state._mainView._depthHistoryViewProj = mainView._viewProj;
            _state._mainView._depthHistoryEye      = mainView._position;
            _state._mainView._bLastFrameDepthValid = SW_TRUE;
        }
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_FALSE || pView->_depthHistory == 0 )
                continue;
            pView->_depthHistoryViewProj = pView->_cullInput._viewProj;
            pView->_depthHistoryEye      = pView->_cullInput._position;
            pView->_bLastFrameDepthValid = SW_TRUE;
        }
    }

    VFXSimulationFrameInput FrameGraphExecutor::makeVFXSimulationInput() const
    {
        VFXSimulationFrameInput input{};
        input._bSimulationPipelineReady = _psoManager.getEnginePSO( RenderPassType::VFXSimulation ) != 0;
        input._bMeshPipelineReady       = _psoManager.getEnginePSO( RenderPassType::VFXMeshInstance ) != 0;
        input._bNativeBindless          = _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling();
        input._instanceBuffer           = _state._gpuScene.getInstanceBuffer();
        input._instanceUAV              = _state._gpuScene.getInstanceUAV();
        // 파티클 라이트는 라이트 버퍼의 GPU 구간에 쓴다. 구간이 없는 프레임(요청 없음 · 생산자가 GPU 가 아님)은 슬롯 수 0 이라 쓰지 않는다.
        input._lightBuffer    = _state._lightBuffer.getBuffer()._buffer;
        input._lightUAV       = _state._lightBuffer.getGPULightUAV();
        input._lightBase      = _state._lightBuffer.getGPULightBase();
        input._lightSlotCount = _state._lightBuffer.getGPULightCount();
        // 깊이 충돌은 주 시점의 지난 깊이만 본다(VFXCollisionDef 설명의 한계). 옮길 수 없는 프레임(첫 프레임 · 크기가 바뀐 프레임)은 히스토리가 없다.
        if ( canCopyDepthHistory( _state._mainView ) )
        {
            const TransientAttachmentPool::Attachment depth = _state._mainView._transientPool.find( attachmentNames()._sceneDepth );
            input._depth._texture                           = _state._mainView._depthHistory;
            input._depth._uav                               = _state._mainView._depthHistoryUAV;
            input._depth._width                             = depth._width;
            input._depth._height                            = depth._height;
            input._depth._viewProj                          = _state._mainView._depthHistoryViewProj;
            input._depth._eye                               = _state._mainView._depthHistoryEye;
        }
        return input;
    }

    bool FrameGraphExecutor::dispatchCullView( uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount, const FrameViewTarget* pExtraView )
    {
        if ( cullViewIndex >= _state._gpuScene.getCullViewCount() )
            return false;
        const GPUCullViewResources& view              = _state._gpuScene.getCullView( cullViewIndex );
        const bool                  bCullViewNotReady = view._indirectArgs._uav == kInvalidDescriptorIndex || view._visibleInstances._uav == kInvalidDescriptorIndex || renderView.isReadyForCulling() == false;
        if ( bCullViewNotReady )
            return false;

        FrameRendererUtil::GPUCullParams cullParams{};
        // 절두체는 **뷰가 이미 가지고 있다.** setViewProjection 이 행렬과 함께 갱신한다.
        // 여기서 다시 뽑으면 행렬만 바뀌고 평면이 바뀌지 않는 상태가 생길 수 있다.
        Memory::copy( cullParams._arrPlane, renderView._frustum._arrPlane, sizeof( cullParams._arrPlane ) );
        cullParams._instanceCount    = instanceCount;
        cullParams._batchCount       = _state._gpuScene.getIndirectCommandCount();
        cullParams._visibleSlotCount = _state._gpuScene.getVisibleSlotCount();
        // 인스턴스 LOD 선택은 그 뷰를 그리는 카메라로 한다. 추가 뷰는 자기 카메라, 그림자 슬롯과 주 시점은 주 카메라다(그림자는 가장 고운 단계가 아니라 카메라가 보는 단계로 그린다).
        // 절두체 밖의 가림 물체는 화면 크기가 0 이 아니라 거리에서 나온 값을 받는다 — CPU 선택(`LODView::computeScreenSize`)은 그 경우 가장 낮은 단계였다.
        const RenderView& lodView = ( pExtraView != nullptr ) ? pExtraView->_cullInput : _state._arrView[static_cast<uint32>( RenderViewType::Main )];
        LODView::computeProjectionTerms( lodView._viewProj, cullParams._arrLODDepthRow, cullParams._arrLODView[3] );
        cullParams._arrLODView[0]    = lodView._position._x;
        cullParams._arrLODView[1]    = lodView._position._y;
        cullParams._arrLODView[2]    = lodView._position._z;
        cullParams._arrLODControl[0] = 1u;
        // 전환 상태 버퍼가 있는 뷰만 크로스페이드한다(없으면 진행량 0 — 단계를 바로 바꾸고 버퍼를 읽지 않는다). 컷 · 처음 그리는 프레임은 상태를 비워 단계를 바로 맞춘다.
        const bool             bHasLODState = _state._gpuScene.hasLODState( cullViewIndex );
        const FrameViewTarget& lodTarget    = ( pExtraView != nullptr ) ? *pExtraView : _state._mainView;
        const bool             bViewCut     = lodTarget._bHasHistory == SW_FALSE || lodTarget._settings._bCut == SW_TRUE;
        const bool             bResetState  = _state._gpuScene.takeLODStateReset( cullViewIndex ) || bViewCut;
        cullParams._arrLODControl[1]        = bResetState ? 1u : 0u;
        cullParams._arrLODControl[2]        = bHasLODState ? _lodFadeStep : 0u;
        // 가림 판정은 장면을 그리는 뷰의 슬롯만 본다(주 시점 · 추가 뷰). 그림자 슬롯은 빛에서 본 것이라 카메라 깊이로 거르면 그림자가 빠진다.
        const FrameViewTarget* pOcclusionView = pExtraView != nullptr ? pExtraView : ( cullViewIndex == static_cast<uint32>( RenderViewType::Main ) ? &_state._mainView : nullptr );
        if ( pOcclusionView != nullptr )
            OcclusionTiles::fillCullParams( *pOcclusionView, cullParams );
        const bool               bOccludes    = cullParams._arrOcclusionGrid[3] != 0u;
        const RHIDescriptorIndex occlusionSRV = bOccludes ? pOcclusionView->_occlusionTiles._srv : _occlusion.getPlaceholderSRV();
        // 상수 버퍼는 **뷰마다 자기 것**이다. 하나를 나눠 쓰면 뒤 업로드가 앞 디스패치가 읽을 내용을 덮어쓴다.
        renderView._cullCb.update( *_pCmd, &cullParams, sizeof( cullParams ) );

        // 컬링이 쓰는 두 버퍼는 UAV 상태여야 한다. 간접 인자는 직전 프레임에 IndirectArgument 로,
        // 가시 목록은 ShaderResource 로 두고 끝냈다.
        _pCmd->transitionBuffer( view._indirectArgs._buffer, RHIBufferState::UnorderedAccess );
        _pCmd->transitionBuffer( view._visibleInstances._buffer, RHIBufferState::UnorderedAccess );
        // PSO 와 바인딩은 **뷰마다** 다시 설정한다. 아래 정렬 패스가 둘 다 교체하므로 다음 뷰가 정렬 PSO 로 컬링을 돌면 안 된다.
        _pCmd->setComputePipelineState( _psoManager.getEnginePSO( RenderPassType::GPUCull ) );
        // CullParams(b0) / g_Instances(t0) / g_BatchInfo(t1) / g_OcclusionTiles(t2) / g_IndirectArgs(u0) / g_VisibleInstanceIDs(u1).
        // gpu_cull.hlsl 레지스터와 1:1 로 대응한다. 인스턴스와 배치 구간은 뷰가 공유한다(절두체와 가림 타일만 다르다).
        _pCmd->bindComputeConstantBuffer( renderView._cullCb._index, 0 );
        _pCmd->bindComputeShaderResource( _state._gpuScene.getInstanceSRV(), 0 );
        _pCmd->bindComputeShaderResource( _state._gpuScene.getBatchInfoSRV(), 1 );
        if ( occlusionSRV != kInvalidDescriptorIndex )
            _pCmd->bindComputeShaderResource( occlusionSRV, 2 );
        _pCmd->bindComputeUAV( view._indirectArgs._uav, 0 );
        _pCmd->bindComputeUAV( view._visibleInstances._uav, 1 );
        // 전환 상태(u2)는 크로스페이드가 켜졌을 때만 읽고 쓴다. 없으면 자리만 채운다 — 셰이더가 진행량 0 이면 건드리지 않는다.
        if ( bHasLODState )
        {
            _pCmd->transitionBuffer( view._lodState._buffer, RHIBufferState::UnorderedAccess );
            _pCmd->bindComputeUAV( view._lodState._uav, 2 );
        }
        else
        {
            _pCmd->bindComputeUAV( view._visibleInstances._uav, 2 );
        }
        _gpuAssert.bind( *_pCmd ); // gpu_cull.hlsl 의 assert(u4) — assert 변형 PSO 일 때만 버퍼가 있다
        // **인스턴스마다** 스레드 하나를 쓴다. 그래야 보이는 것을 골라 압축할 수 있다.
        const uint32 groups = ( cullParams._instanceCount + 63u ) / 64u;
        if ( groups > 0 )
            _pCmd->dispatchCompute( groups, 1, 1 );
        // 압축이 끝났으면 투명 배치의 순서를 CPU 가 정한 순서(인스턴스 번호 오름차순)로 되돌린다. 원자 연산이 준 슬롯 번호는
        // 완료 순서라 그대로 두면 블렌딩이 틀린다. 되돌리기가 GPU 에서 돌므로 투명도 압축과 컬링 이득을 받는다(instance_sort.hlsl).
        // 컬링이 채운 목록을 정렬이 바로 읽는다. 상태가 그대로라 transitionBuffer 는
        // 아무것도 하지 않으므로 **UAV 배리어**를 따로 걸어야 한다. 없으면 정렬이 아직
        // 채워지지 않은 목록을 읽는다(백엔드마다 결과가 달라 재현이 어렵다).
        _pCmd->uavBarrier( view._indirectArgs._buffer );
        _pCmd->uavBarrier( view._visibleInstances._buffer );

        const RHIPipelineStateHandle sortPSO        = _psoManager.getEnginePSO( RenderPassType::InstanceSort );
        const bool                   bSortPassReady = sortPSO != 0 && renderView._sortCb.isValid() && cullParams._batchCount > 0;
        if ( bSortPassReady )
        {
            FrameRendererUtil::GPUSortParams sortParams{};
            // 셰이더는 인스턴스 번호로 정렬한다(정렬 기준은 CPU 한 곳이며 정렬 레이어와 시선 축을 쓴다). 추가 뷰는 CPU 가 그 뷰의 시점으로 다시 정한 순번(t2)으로 정렬한다.
            // 카메라 위치는 상수 버퍼 레이아웃을 지키려고 그대로 싣는다.
            const bool bViewRank             = pExtraView != nullptr && pExtraView->_bHasTransparentRank == SW_TRUE;
            sortParams._arrCameraPos[0]      = renderView._position._x;
            sortParams._arrCameraPos[1]      = renderView._position._y;
            sortParams._arrCameraPos[2]      = renderView._position._z;
            sortParams._instanceCount        = instanceCount;
            sortParams._batchCount           = cullParams._batchCount;
            sortParams._bUseViewRank         = bViewRank ? 1u : 0u;
            sortParams._transparentTailBase  = bViewRank ? pExtraView->_transparentTailBase : 0u;
            const RHIDescriptorIndex rankSRV = bViewRank ? pExtraView->_transparentRank._srv : _transparentRankPlaceholder._srv;
            // 정렬 상수 버퍼도 **뷰마다 자기 것**이다. 뷰마다 다른 값(눈 위치)을 싣으므로 나눠 쓰면 뒤 업로드가 앞 디스패치의 내용을 덮어쓴다.
            renderView._sortCb.update( *_pCmd, &sortParams, sizeof( sortParams ) );

            _pCmd->setComputePipelineState( sortPSO );
            // 바인딩 위치는 컬링과 같다. 인자와 가시 목록을 그대로 읽고 쓴다. g_ViewRank(t2)는 추가 뷰의 순번 테이블이고, 그 밖의 뷰는 자리 표시용이다.
            _pCmd->bindComputeConstantBuffer( renderView._sortCb._index, 0 );
            _pCmd->bindComputeShaderResource( _state._gpuScene.getInstanceSRV(), 0 );
            _pCmd->bindComputeShaderResource( _state._gpuScene.getBatchInfoSRV(), 1 );
            if ( rankSRV != kInvalidDescriptorIndex )
                _pCmd->bindComputeShaderResource( rankSRV, 2 );
            _pCmd->bindComputeUAV( view._indirectArgs._uav, 0 );
            _pCmd->bindComputeUAV( view._visibleInstances._uav, 1 );
            // 배치마다 워크그룹 하나를 쓴다. 그 배치의 목록을 그룹 공유 메모리 안에서 정렬한다.
            _pCmd->dispatchCompute( cullParams._batchCount, 1, 1 );
        }

        _pCmd->transitionBuffer( view._indirectArgs._buffer, RHIBufferState::IndirectArgument );
        _pCmd->transitionBuffer( view._visibleInstances._buffer, RHIBufferState::ShaderResource );
        return true;
    }

    void FrameGraphExecutor::dispatchRealtimeProbes()
    {
        if ( _pCmd == nullptr || _state._probeUpdater.isActive() == false )
            return;
        SW_PROFILE_SCOPE( "RT.Probe.Update" );
        _state._probeUpdater.dispatch( *_pCmd, _psoManager.getEnginePSO( RenderPassType::ProbeUpdate ), _state._lightBuffer, _transparentRankPlaceholder._srv );
    }
} // namespace sw
