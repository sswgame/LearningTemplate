/**
 * @file FrameRendererCompute.cpp
 * @brief 프레임의 컴퓨트 프리패스 여섯 가지를 정의합니다. 인스턴스 애니메이션, 메시 모프, 메시 스킨, GPU 컬링, 인스턴스 정렬, 라이트 그리드입니다.
 * @details 여섯 가지 모두 그래프(RenderGraph)의 패스가 아니다. 그리기 **전에** 프레임 커맨드 리스트에 직접 기록되고, 그 순서가
 *          곧 GPU 타임라인의 순서다. 애니메이션이 바운드를 바꾸고, 컬링이 그 바운드로 거르고, 정렬이 컬링 결과를
 *          되돌린다. 그래서 순서와 배리어가 한 파일 안에 나란히 보여야 한다.
 *
 *          컴퓨트 패스를 감싸는 래퍼 클래스는 두지 않는다. 각자의 상수 버퍼는 `RHIConstantBufferSlot` 이고 소유는
 *          `FrameRenderer` 가 한다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    /**
     * @brief `-gv_gpuCulling=0` 이면 GPU 컬링 컴퓨트 디스패치를 건너뜁니다(인다이렉트 드로우는 그대로 실행합니다).
     * @details 간접 인자는 GPUScene 이 CPU 에서 이미 채워 두므로, 이 디스패치만 빼면 "컴퓨트가 인자를
     *          망치는가" 를 백엔드별로 가를 수 있다. 기본은 켬이다.
     */
    SW_GLOBAL_VARIABLE( int32, gv_gpuCulling, 1, "GPU 컬링 컴퓨트 디스패치 (0=건너뜀, 진단용)" );

    /**
     * @brief `-gv_morphDiag=<0|1|2|3>` 은 GPU 메시 모프 경로를 백엔드 능력 테이블과 **상관없이** 돌려 봅니다.
     * @details 0은 평소대로(`RHICapabilities::_bGPUMeshMorph` 를 따름), 1은 능력 테이블을 무시하고 켬,
     *          2는 켜되 **컴퓨트 디스패치를 건너뛰고 레스트 버퍼를 정점 셰이더에 그대로 바인딩**,
     *          3은 2에 더해 풀 원소의 노멀 위치에 **원소 번호**를 기록한다.
     *
     *          2의 정답은 **레스트 포즈와 같은 이미지**다. 컴퓨트가 아예 돌지 않기 때문이다. 그래서
     *          2만으로 "컴퓨트가 범인인가" 를 가를 수 있다.
     *          3은 거기서 한 걸음 더 간다. 원소 번호를 읽으면 "제 원소를 가리켰는가" 를 위치 값과 **따로**
     *          볼 수 있어서, 인덱싱이 틀린 것인지 내용이 틀린 것인지 이미지 한 장으로 가를 수 있다.
     */
    SW_GLOBAL_VARIABLE( int32, gv_morphDiag, 0, "메시 모프 진단 (0 평소 / 1 강제 켬 / 2 디스패치 생략 / 3 번호표)" );

    bool FrameRenderer::usesGPUGeneratedCommands() const
    {
        // 컴퓨트가 드로우 커맨드를 만드는 경로를 쓰려면 인다이렉트 드로우와 컬링 디스패치가 둘 다 켜져
        // 있고 컬링 PSO 가 실제로 만들어져 있어야 한다. 셋 중 하나라도 없으면 CPU 가 채운 개수로 그린다.
        return gv_gpuCulling != 0 && getEnginePSO( RenderPassType::GPUCull ) != 0;
    }

    void FrameRenderer::dispatchInstanceAnimation( uint32 instanceCount )
    {
        // 컴퓨트 프리패스 둘이다. 인스턴스 애니메이션이 먼저고 컬링이 나중이다. 순서가 뒤집히면 컬링이
        // **이번 프레임에 회전하기 전의** 바운드로 판정한다. 애니메이션이 회전만 바꾸므로 지금 씬에서는
        // 결과가 같지만, 이동을 넣는 순간 한 프레임 늦은 컬링이 된다.
        // 회전을 요청한 인스턴스가 하나도 없으면 디스패치 자체를 건너뛴다. 안 그러면 회전을 쓰지 않는 씬도
        // 매 프레임 인스턴스당 96바이트를 읽고 아무 일도 하지 않는다. 컬링 능력과는 무관하다(DX11 도 돈다).
        const bool bSpinAnimationNeeded = _gpuScene.isUploaded() && instanceCount > 0 && _gpuScene.getSpinInstanceCount() > 0;
        if ( bSpinAnimationNeeded )
        {
            const RHIPipelineStateHandle animationPSO        = getEnginePSO( RenderPassType::InstanceAnimation );
            const bool                   bSpinAnimationReady = animationPSO != 0 && _deform._instanceAnimationCb.isValid() &&
                                             _gpuScene.getInstanceUav() != kInvalidDescriptorIndex;
            if ( bSpinAnimationReady )
            {
                FrameRendererUtil::GPUAnimationParams animationParams{};
                animationParams._time = getAnimationTime();
                // 기준 각속도와 편차 폭(라디안/초). 편차가 기준보다 커야 "다 같은 속도" 로 보이지 않는다.
                animationParams._baseSpeed     = FrameRendererUtil::kGPUSpinBaseSpeed;
                animationParams._speedRange    = FrameRendererUtil::kGPUSpinSpeedRange;
                animationParams._instanceCount = instanceCount;
                _deform._instanceAnimationCb.update( *_pCmd, &animationParams, sizeof( animationParams ) );

                // **쓰기 전에** UAV 상태로 옮긴다. 인스턴스 버퍼는 직전 프레임에 정점 셰이더가 읽던
                // (그리고 방금 CPU 업로드가 쓴) 상태라, 이 전이 없이 UAV 로 쓰면 DX12 와 Vulkan 에서 쓰기가
                // 유효하지 않다. 화면은 조용히 예전 값 그대로다.
                _pCmd->transitionBuffer( _gpuScene.getInstanceBuffer(), RHIBufferState::UnorderedAccess );
                _pCmd->setComputePipelineState( animationPSO );
                // AnimationParams(b0) / g_InstancesRW(u0). instanceanimation.hlsl 레지스터와 1:1 로 대응한다.
                _pCmd->bindComputeConstantBuffer( _deform._instanceAnimationCb._index, 0 );
                _pCmd->bindComputeUav( _gpuScene.getInstanceUav(), 0 );
                const uint32 animationGroups = ( instanceCount + 63u ) / 64u;
                if ( animationGroups > 0 )
                    _pCmd->dispatchCompute( animationGroups, 1, 1 );
                // 다음 디스패치(컬링)와 정점 셰이더가 이 결과를 읽는다. 쓰기가 끝났음을 알린다.
                _pCmd->transitionBuffer( _gpuScene.getInstanceBuffer(), RHIBufferState::ShaderResource );
            }
        }
    }

    int32 FrameRenderer::getEffectiveMeshMorphDiag() const
    {
#if !defined( SW_SHIPPING )
        if ( _debugOverrides._meshMorphDiag >= 0 )
            return _debugOverrides._meshMorphDiag;
#endif
        return gv_morphDiag;
    }

    float32 FrameRenderer::getAnimationTime() const
    {
#if !defined( SW_SHIPPING )
        if ( _debugOverrides._animationTime >= 0.0f )
            return _debugOverrides._animationTime;
#endif
        return _deform._animationTimer.getTotalTime();
    }

    void FrameRenderer::prepareMeshMorphPool()
    {
        const int32 morphDiag = getEffectiveMeshMorphDiag();
        // 백엔드가 못 하면 풀을 만들지도 않는다. 배치의 base 가 kInvalidBase 로 남아 셰이더가
        // 레스트 포즈로 그린다(언리얼의 스킨 캐시 폴백과 같다). 자세한 사연은 RHICapabilities.h 를 참고한다.
        _deform._bMorphBindsRest = SW_FALSE;
        if ( _pDevice == nullptr )
            return;
        if ( _pDevice->getCapabilities()._bGPUMeshMorph == SW_FALSE && morphDiag == 0 )
            return;

        // 풀은 **배치가 가진 메시**에서 만든다. 스냅샷 배치가 소유권을 가지고 있으므로 이 프레임 동안 살아 있다.
        // 스킨드 메시는 스킨 구간(뒤)으로, 모프를 켠 메시는 모프 구간(앞)으로 간다.
        SW_PROFILE_SCOPE( "RT.Deform.preparePool" );
        _deform._listScratchMorphMesh.clear();
        _deform._listScratchSkinMesh.clear();
        _deform._listScratchVertexAnimationMesh.clear();
        _deform._setScratchSeenMesh.clear();
        const auto& listBatch = _gpuScene.getAllBatches();
        for ( const GPUMeshBatch& batch : listBatch )
        {
            Mesh* pMesh = batch._mesh.get();
            if ( pMesh == nullptr )
                continue;
            // 같은 메시가 여러 배치에 나올 수 있다(불투명, 투명, 뷰). 메시의 분류(VAT · 스킨 · 모프 · 없음)는 하나뿐이므로
            // 처음 본 배치에서만 분류한다. 목록 순서는 처음 나온 순서 그대로라 풀 배치가 예전과 같다.
            if ( _deform._setScratchSeenMesh.insert( pMesh ).second == false )
                continue;
            // 정점 애니메이션(VAT) 메시는 컴퓨트 없이 정점 셰이더가 테이블을 읽는다. 테이블 풀로 간다.
            if ( pMesh->findVertexAnimation() != nullptr )
            {
                _deform._listScratchVertexAnimationMesh.push_back( pMesh );
                continue;
            }
            const bool bSkin = pMesh->hasSkin();
            if ( bSkin == false && pMesh->isGPUMorphEnabled() == false )
                continue;
            ( bSkin ? _deform._listScratchSkinMesh : _deform._listScratchMorphMesh ).push_back( pMesh );
        }
        // 중복 제거의 규모다. 배치 수 대비 고유 메시 수를 보면 이 루프가 무엇에 비례하는지 안다.
        SW_PROFILE_COUNT( "RT.Deform.batches", listBatch.size() );
        SW_PROFILE_COUNT( "RT.Deform.uniqueMeshes", _deform._setScratchSeenMesh.size() );

        // 아래 세 구간이 preparePool 안의 무거운 단계다 — 목록이 바뀐 프레임의 풀 재구성(GPU 업로드 포함), 매 프레임의 팔레트 업로드, VAT 테이블 풀.
        {
            SW_PROFILE_SCOPE( "RT.Deform.rebuildMorphPool" );
            _deform._meshMorphPool.update( _pDevice, _deform._listScratchMorphMesh, _deform._listScratchSkinMesh );
            // 변형 프로그램(에디터 모디파이어 미리보기)은 레스트와 따로 바뀐다 - 번호가 바뀐 프레임에만 올린다
            _deform._meshMorphPool.updateDeformPrograms( _pDevice );
        }
        {
            SW_PROFILE_SCOPE( "RT.Deform.uploadSkinPalettes" );
            _deform._meshMorphPool.uploadSkinPalettes( _pDevice, _gpuScene.getSkinPalettes(), _gpuScene.findSkinPaletteRows(), _gpuScene.findMorphWeights() );
        }

        // 배치에 구간을 기록해 둔다. upload() 가 배치 테이블(g_SwBatches)에 싣는다. 풀에 들어가지 못한 메시는 kInvalidBase 라
        // 셰이더가 레스트 포즈로 그린다.
        _gpuScene.assignMorphBases( _deform._meshMorphPool );
        {
            SW_PROFILE_SCOPE( "RT.Deform.rebuildVertexAnimationPool" );
            _deform._vertexAnimationPool.rebuild( _pDevice, _deform._listScratchVertexAnimationMesh );
        }
        _gpuScene.assignVertexAnimationBases( _deform._vertexAnimationPool );
        SW_PROFILE_COUNT( "RT.Skin.instances", _deform._meshMorphPool.getSkinInstanceCount() );
        SW_PROFILE_COUNT( "RT.Skin.vertices", _deform._meshMorphPool.getSkinVertexCount() );
        SW_PROFILE_COUNT( "RT.Skin.sourceVertices", _deform._meshMorphPool.getSkinSourceVertexCount() );
    }

    void FrameRenderer::dispatchMeshMorph()
    {
        const int32 morphDiag = getEffectiveMeshMorphDiag();
        if ( _pDevice == nullptr || _pCmd == nullptr )
            return;
        if ( _pDevice->getCapabilities()._bGPUMeshMorph == SW_FALSE && morphDiag == 0 )
            return;
        if ( _deform._meshMorphPool.isDispatchable() == false )
            return;

        // 진단 2: 컴퓨트를 돌리지 않고 레스트 버퍼를 그대로 정점 셰이더에 바인딩한다(위 gv 주석 참고).
        if ( morphDiag == 2 )
        {
            _deform._bMorphBindsRest = SW_TRUE;
            return;
        }

        // 진단 3: 레스트 버퍼에 **위치는 진짜 값, 노멀 위치에는 원소 번호**를 기록해 올리고 컴퓨트를
        //         건너뛴다. 그러면 이미지 한 장으로 "정점 셰이더가 제 원소를 가리켰는가"(원소 번호)와 "그 원소의
        //         위치가 정점 스트림과 같은가" 를 **따로** 볼 수 있다. 값만 비교해서는 둘을 가를 수 없다.
        if ( morphDiag == 3 )
        {
            // 메시마다 풀 구간(baseOf)에 쓴다 — 구간은 목록 순서로 이어져 있지 않다(C-43 부분 갱신).
            for ( const Mesh* pMesh : _deform._listScratchMorphMesh )
            {
                const uint32 base = ( pMesh != nullptr ) ? _deform._meshMorphPool.baseOf( pMesh ) : GPUMeshMorphPool::kInvalidBase;
                if ( base == GPUMeshMorphPool::kInvalidBase )
                    continue;
                _deform._listScratchMorphTag.clear();
                uint32 element = base;
                for ( const RHIVertex& vertex : pMesh->getVertices() )
                {
                    GPUMorphVertex tagged{};
                    tagged._position = float4{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2], 1.0f };
                    tagged._normal   = float4{ static_cast<float32>( element ), 0.0f, 0.0f, 0.0f };
                    _deform._listScratchMorphTag.push_back( tagged );
                    ++element;
                }
                _deform._meshMorphPool.getRestBuffer().upload( _pDevice, _deform._listScratchMorphTag.data(),
                                                               static_cast<uint32>( _deform._listScratchMorphTag.size() * sizeof( GPUMorphVertex ) ),
                                                               base * static_cast<uint32>( sizeof( GPUMorphVertex ) ) );
            }
            _deform._bMorphBindsRest = SW_TRUE;
            return;
        }

        const RHIPipelineStateHandle morphPSO         = getEnginePSO( RenderPassType::MeshMorph );
        const RHIDescriptorIndex     programSrv       = _deform._meshMorphPool.getDeformProgramBuffer()._srv;
        const bool                   bMissingResource = morphPSO == 0 || _deform._meshMorphCb.isValid() == false || programSrv == kInvalidDescriptorIndex;
        if ( bMissingResource )
            return;

        FrameRendererUtil::GPUMorphParams morphParams{};
        morphParams._time        = getAnimationTime();
        morphParams._amplitude   = FrameRendererUtil::kMeshMorphAmplitude;
        morphParams._frequency   = FrameRendererUtil::kMeshMorphFrequency;
        morphParams._vertexCount = _deform._meshMorphPool.getMorphVertexCount();
        if ( morphParams._vertexCount == 0 )
            return;
        _deform._meshMorphCb.update( *_pCmd, &morphParams, sizeof( morphParams ) );

        // 쓰기 전에 UAV 로, 드로우 전에 다시 SRV 로 전이한다. 정점 셰이더가 이 버퍼를 읽으므로 배리어가 빠지면
        // DX12 와 Vulkan 에서 조용히 예전 값이 나온다(인스턴스 애니메이션과 같은 함정이다).
        _pCmd->transitionBuffer( _deform._meshMorphPool.getMorphBuffer()._buffer, RHIBufferState::UnorderedAccess );
        _pCmd->setComputePipelineState( morphPSO );
        _pCmd->bindComputeConstantBuffer( _deform._meshMorphCb._index, 0 );
        // MorphParams(b0) / g_RestVertices(t0) / g_DeformPrograms(t1, 칸 표 + 변형 프로그램) / g_MorphVerticesRW(u0). meshmorph.hlsl 레지스터와 1:1 로 대응한다.
        _pCmd->bindComputeShaderResource( _deform._meshMorphPool.getRestBuffer()._srv, 0 );
        _pCmd->bindComputeShaderResource( programSrv, 1 );
        _pCmd->bindComputeUav( _deform._meshMorphPool.getMorphBuffer()._uav, 0 );
        const uint32 morphGroups = ( morphParams._vertexCount + 63u ) / 64u;
        if ( morphGroups > 0 )
            _pCmd->dispatchCompute( morphGroups, 1, 1 );
        _pCmd->transitionBuffer( _deform._meshMorphPool.getMorphBuffer()._buffer, RHIBufferState::ShaderResource );
    }

    void FrameRenderer::dispatchMeshSkin()
    {
        if ( _pDevice == nullptr || _pCmd == nullptr )
            return;
        const int32 morphDiag = getEffectiveMeshMorphDiag();
        // 진단 2와 3은 레스트 버퍼를 그대로 바인딩해 "컴퓨트 없이" 를 본다. 스키닝도 돌리지 않는다(바인드 포즈).
        const bool bSkipMorphSkinning = ( _pDevice->getCapabilities()._bGPUMeshMorph == SW_FALSE && morphDiag == 0 ) || morphDiag == 2 || morphDiag == 3;
        if ( bSkipMorphSkinning )
            return;
        if ( _deform._meshMorphPool.isSkinDispatchable() == false )
            return;
        const RHIPipelineStateHandle skinPSO = getEnginePSO( RenderPassType::MeshSkin );
        if ( skinPSO == 0 || _deform._meshSkinCb.isValid() == false )
            return;

        FrameRendererUtil::GPUSkinParams skinParams{};
        skinParams._skinVertexBase    = _deform._meshMorphPool.getSkinVertexBase();
        skinParams._skinVertexCount   = _deform._meshMorphPool.getSkinVertexCount();
        skinParams._skinBoneCount     = _deform._meshMorphPool.getSkinBoneCount();
        skinParams._skinInstanceCount = _deform._meshMorphPool.getSkinInstanceCount();
        skinParams._skinDeltaBase     = _deform._meshMorphPool.getSkinDeltaBase();
        _deform._meshSkinCb.update( *_pCmd, &skinParams, sizeof( skinParams ) );

        // 모프와 같은 결과 버퍼의 뒤 구간에 쓴다. 쓰기 전에 UAV 로, 드로우 전에 다시 SRV 로 전이한다(배리어가 빠지면 DX12 와 Vulkan 에서 예전 값이 나온다).
        _pCmd->transitionBuffer( _deform._meshMorphPool.getMorphBuffer()._buffer, RHIBufferState::UnorderedAccess );
        _pCmd->setComputePipelineState( skinPSO );
        // SkinParams(b0) / g_RestVertices(t0, 원본 레스트) / g_SkinWeights(t1) / g_SkinPalette(t2) / g_SkinInstances(t3) / g_MorphVerticesRW(u0).
        // meshskin.hlsl 레지스터와 1:1 로 대응한다.
        _pCmd->bindComputeConstantBuffer( _deform._meshSkinCb._index, 0 );
        _pCmd->bindComputeShaderResource( _deform._meshMorphPool.getSkinRestBuffer()._srv, 0 );
        _pCmd->bindComputeShaderResource( _deform._meshMorphPool.getSkinWeightBuffer()._srv, 1 );
        _pCmd->bindComputeShaderResource( _deform._meshMorphPool.getSkinPaletteBuffer()._srv, 2 );
        _pCmd->bindComputeShaderResource( _deform._meshMorphPool.getSkinInstanceBuffer()._srv, 3 );
        _pCmd->bindComputeUav( _deform._meshMorphPool.getMorphBuffer()._uav, 0 );
        const uint32 skinGroups = ( skinParams._skinVertexCount + 63u ) / 64u;
        if ( skinGroups > 0 )
            _pCmd->dispatchCompute( skinGroups, 1, 1 );
        _pCmd->transitionBuffer( _deform._meshMorphPool.getMorphBuffer()._buffer, RHIBufferState::ShaderResource );
    }

    void FrameRenderer::dispatchCullAndSort( uint32 instanceCount )
    {
        // 컬링은 GPUScene 이 "개수를 컴퓨트에 맡겼다" 고 답할 때만 돈다. 그래야 인자의 초기 개수(0)와
        // 디스패치 여부가 절대 어긋나지 않는다. 어긋나면 한쪽은 빈 화면, 다른 쪽은 낡은 목록이다.
        //
        // **뷰마다 한 번씩** 돈다. 컬링 결과는 절두체에 종속이라, 메인 카메라로 거른 목록을 그림자 패스가
        // 쓰면 화면 밖에서 화면 안으로 그림자를 드리우는 물체가 사라진다. 언리얼이 뷰마다
        // FInstanceCullingContext 를 두는 것과 같은 이유다. 이번 프레임에 그리는 추가 뷰(CCTV, PiP)도 자기 슬롯을 돈다.
        if ( _gpuScene.isUploaded() == false || _gpuScene.areIndirectCountsGPUFilled() == false )
            return;
        const bool bCullingUnavailable = getEnginePSO( RenderPassType::GPUCull ) == 0 || _gpuScene.getInstanceSrv() == kInvalidDescriptorIndex ||
                                         _gpuScene.getBatchInfoSrv() == kInvalidDescriptorIndex;
        if ( bCullingUnavailable )
            return;

        bool bAllViewsCulled = true;
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
        {
            bAllViewsCulled = dispatchCullView( viewIndex, _arrView[viewIndex], instanceCount, nullptr ) && bAllViewsCulled;
        }
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
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
        _bGPUCullingActive = bAllViewsCulled ? 1u : 0u;
    }

    void FrameRenderer::dispatchLightGrid()
    {
        // 생산자가 GPU 인 프레임에만 뷰 수가 0 보다 크다(GPULightBuffer::updateGPUGrid). 그때 PSO 와 버퍼는 이미 확인됐다(selectLightGridProducer, updateGPUGrid) —
        // 여기서 건너뛰면 채우지 않은 그리드를 셰이더가 읽으므로 더 거르지 않는다.
        const uint32 viewCount = _lightBuffer.getGPUGridViewCount();
        if ( viewCount == 0 || _pCmd == nullptr )
            return;
        const RHIStructuredBufferSlot& gridBuffer = _lightBuffer.getGPUGridBuffer();
        _pCmd->transitionBuffer( gridBuffer._buffer, RHIBufferState::UnorderedAccess );
        _pCmd->setComputePipelineState( getEnginePSO( RenderPassType::LightGrid ) );
        // g_LightGridLights(t0) / g_LightGridViews(t1) / g_LightGridRW(u0). lightgrid.hlsl 레지스터와 1:1 로 대응한다. 상수 버퍼는 없다(뷰마다의 값은 t1).
        _pCmd->bindComputeShaderResource( _lightBuffer.getBuffer()._srv, 0 );
        _pCmd->bindComputeShaderResource( _lightBuffer.getGPUGridViewBuffer()._srv, 1 );
        _pCmd->bindComputeUav( gridBuffer._uav, 0 );
        // 그룹 하나 = 뷰 하나. 그룹 안에서 세기 · 접두사 합 · 채우기를 그룹 동기화로 잇는다.
        _pCmd->dispatchCompute( viewCount, 1, 1 );
        // 조명 패스(포워드 · 디퍼드 · 투명)가 t12 로 읽는다. 쓰기가 끝났음을 알린다(빠지면 DX12 · Vulkan 에서 예전 그리드를 읽는다).
        _pCmd->transitionBuffer( gridBuffer._buffer, RHIBufferState::ShaderResource );
    }

    bool FrameRenderer::canCopyDepthHistory( const ViewTarget& view ) const
    {
        const bool bHasSource = view._bLastFrameDepthValid == SW_TRUE && view._depthHistory != 0 && view._depthHistoryUav != kInvalidDescriptorIndex;
        if ( bHasSource == false || getEnginePSO( RenderPassType::DepthHistory ) == 0 )
            return false;
        return view._transientPool.find( attachmentNames()._sceneDepth.view() )._texture != 0;
    }

    void FrameRenderer::recordDepthHistories()
    {
        if ( _pCmd == nullptr || _pDevice == nullptr )
            return;
        // 주 시점과 추가 뷰 — 장면 깊이 첨부는 프레임 사이에 그대로 남아 있다(이번 프레임 패스가 쓰기 전이다).
        recordDepthHistory( _mainView );
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            recordDepthHistory( *pView );
        }
    }

    void FrameRenderer::recordDepthHistory( ViewTarget& target )
    {
        if ( canCopyDepthHistory( target ) == false )
            return;
        const TransientAttachmentPool::Attachment depth = target._transientPool.find( attachmentNames()._sceneDepth.view() );
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
        _pCmd->setVertexBuffer( 0, 0, 0, 0 );
        _pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        _pCmd->setPipelineState( getEnginePSO( RenderPassType::DepthHistory ) );
        // 원본 깊이 — 네이티브는 bindless 번호(루트 상수), 에뮬은 엔진 텍스처 슬롯 0. PSO 뒤에 건다(setPipelineState 가 슬롯 상태를 비운다).
        const bool   bNativeBindless = _pDevice->supportsNativeBindlessSampling();
        const uint32 sourceIndex     = bNativeBindless ? static_cast<uint32>( depth._srv ) : 0u;
        if ( bNativeBindless == false )
            _pCmd->bindShaderResource( depth._srv, shaderslot::kEngineTexture0 );
        _pCmd->setGraphicsRootConstants( 0, 1, &sourceIndex );
        _pCmd->draw( 3, 0 );
        _pCmd->endRenderPass();
    }

    void FrameRenderer::rememberDepthHistoryViews()
    {
        // 장면 깊이 첨부가 있는 뷰만 — 히스토리가 없으면(2D 파이프라인) 짝을 적을 필요가 없다.
        const RenderView& mainView = view( RenderViewType::Main );
        if ( _mainView._depthHistory != 0 )
        {
            _mainView._depthHistoryViewProj = mainView._viewProj;
            _mainView._depthHistoryEye      = mainView._position;
            _mainView._bLastFrameDepthValid = SW_TRUE;
        }
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_FALSE || pView->_depthHistory == 0 )
                continue;
            pView->_depthHistoryViewProj = pView->_cullInput._viewProj;
            pView->_depthHistoryEye      = pView->_cullInput._position;
            pView->_bLastFrameDepthValid = SW_TRUE;
        }
    }

    VfxSimulationFrameInput FrameRenderer::makeVfxSimulationInput() const
    {
        VfxSimulationFrameInput input{};
        input._bSimulationPipelineReady = getEnginePSO( RenderPassType::VfxSimulation ) != 0;
        input._bMeshPipelineReady       = getEnginePSO( RenderPassType::VfxMeshInstance ) != 0;
        input._bNativeBindless          = _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling();
        input._instanceBuffer           = _gpuScene.getInstanceBuffer();
        input._instanceUav              = _gpuScene.getInstanceUav();
        // 깊이 충돌은 주 시점의 지난 깊이만 본다(VfxCollisionDef 설명의 한계). 옮길 수 없는 프레임(첫 프레임 · 크기가 바뀐 프레임)은 히스토리가 없다.
        if ( canCopyDepthHistory( _mainView ) )
        {
            const TransientAttachmentPool::Attachment depth = _mainView._transientPool.find( attachmentNames()._sceneDepth.view() );
            input._depth._texture                           = _mainView._depthHistory;
            input._depth._uav                               = _mainView._depthHistoryUav;
            input._depth._width                             = depth._width;
            input._depth._height                            = depth._height;
            input._depth._viewProj                          = _mainView._depthHistoryViewProj;
            input._depth._eye                               = _mainView._depthHistoryEye;
        }
        return input;
    }

    bool FrameRenderer::dispatchCullView( uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount, const ViewTarget* pExtraView )
    {
        if ( cullViewIndex >= _gpuScene.getCullViewCount() )
            return false;
        const GPUCullViewResources& view              = _gpuScene.getCullView( cullViewIndex );
        const bool                  bCullViewNotReady = view._indirectArgs._uav == kInvalidDescriptorIndex || view._visibleInstances._uav == kInvalidDescriptorIndex || renderView.isReadyForCulling() == false;
        if ( bCullViewNotReady )
            return false;

        FrameRendererUtil::GPUCullParams cullParams{};
        // 절두체는 **뷰가 이미 가지고 있다.** setViewProjection 이 행렬과 함께 갱신한다.
        // 여기서 다시 뽑으면 행렬만 바뀌고 평면이 바뀌지 않는 상태가 생길 수 있다.
        Memory::copy( cullParams._arrPlane, renderView._frustum._arrPlane, sizeof( cullParams._arrPlane ) );
        cullParams._instanceCount = instanceCount;
        cullParams._batchCount    = _gpuScene.getIndirectCommandCount();
        // 상수 버퍼는 **뷰마다 자기 것**이다. 하나를 나눠 쓰면 뒤 업로드가 앞 디스패치가 읽을 내용을 덮어쓴다.
        renderView._cullCb.update( *_pCmd, &cullParams, sizeof( cullParams ) );

        // 컬링이 쓰는 두 버퍼는 UAV 상태여야 한다. 간접 인자는 직전 프레임에 IndirectArgument 로,
        // 가시 목록은 ShaderResource 로 두고 끝냈다.
        _pCmd->transitionBuffer( view._indirectArgs._buffer, RHIBufferState::UnorderedAccess );
        _pCmd->transitionBuffer( view._visibleInstances._buffer, RHIBufferState::UnorderedAccess );
        // PSO 와 바인딩은 **뷰마다** 다시 설정한다. 아래 정렬 패스가 둘 다 교체하므로 다음 뷰가 정렬 PSO 로 컬링을 돌면 안 된다.
        _pCmd->setComputePipelineState( getEnginePSO( RenderPassType::GPUCull ) );
        // CullParams(b0) / g_Instances(t0) / g_BatchInfo(t1) / g_IndirectArgs(u0) / g_VisibleInstanceIds(u1).
        // gpucull.hlsl 레지스터와 1:1 로 대응한다. 인스턴스와 배치 구간은 뷰가 공유한다(절두체만 다르다).
        _pCmd->bindComputeConstantBuffer( renderView._cullCb._index, 0 );
        _pCmd->bindComputeShaderResource( _gpuScene.getInstanceSrv(), 0 );
        _pCmd->bindComputeShaderResource( _gpuScene.getBatchInfoSrv(), 1 );
        _pCmd->bindComputeUav( view._indirectArgs._uav, 0 );
        _pCmd->bindComputeUav( view._visibleInstances._uav, 1 );
        // **인스턴스마다** 스레드 하나를 쓴다. 그래야 보이는 것을 골라 압축할 수 있다.
        const uint32 groups = ( cullParams._instanceCount + 63u ) / 64u;
        if ( groups > 0 )
            _pCmd->dispatchCompute( groups, 1, 1 );
        // 압축이 끝났으면 투명 배치의 순서를 CPU 가 정한 순서(인스턴스 번호 오름차순)로 되돌린다. 원자 연산이 준 슬롯 번호는
        // 완료 순서라 그대로 두면 블렌딩이 틀린다. 되돌리기가 GPU 에서 돌므로 투명도 압축과 컬링 이득을 받는다(instancesort.hlsl).
        // 컬링이 채운 목록을 정렬이 바로 읽는다. 상태가 그대로라 transitionBuffer 는
        // 아무것도 하지 않으므로 **UAV 배리어**를 따로 걸어야 한다. 없으면 정렬이 아직
        // 채워지지 않은 목록을 읽는다(백엔드마다 결과가 달라 재현이 어렵다).
        _pCmd->uavBarrier( view._indirectArgs._buffer );
        _pCmd->uavBarrier( view._visibleInstances._buffer );

        const RHIPipelineStateHandle sortPSO        = getEnginePSO( RenderPassType::InstanceSort );
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
            const RHIDescriptorIndex rankSrv = bViewRank ? pExtraView->_transparentRank._srv : _transparentRankPlaceholder._srv;
            // 정렬 상수 버퍼도 **뷰마다 자기 것**이다. 뷰마다 다른 값(눈 위치)을 싣으므로 나눠 쓰면 뒤 업로드가 앞 디스패치의 내용을 덮어쓴다.
            renderView._sortCb.update( *_pCmd, &sortParams, sizeof( sortParams ) );

            _pCmd->setComputePipelineState( sortPSO );
            // 바인딩 위치는 컬링과 같다. 인자와 가시 목록을 그대로 읽고 쓴다. g_ViewRank(t2)는 추가 뷰의 순번 테이블이고, 그 밖의 뷰는 자리 표시용이다.
            _pCmd->bindComputeConstantBuffer( renderView._sortCb._index, 0 );
            _pCmd->bindComputeShaderResource( _gpuScene.getInstanceSrv(), 0 );
            _pCmd->bindComputeShaderResource( _gpuScene.getBatchInfoSrv(), 1 );
            if ( rankSrv != kInvalidDescriptorIndex )
                _pCmd->bindComputeShaderResource( rankSrv, 2 );
            _pCmd->bindComputeUav( view._indirectArgs._uav, 0 );
            _pCmd->bindComputeUav( view._visibleInstances._uav, 1 );
            // 배치마다 워크그룹 하나를 쓴다. 그 배치의 목록을 그룹 공유 메모리 안에서 정렬한다.
            _pCmd->dispatchCompute( cullParams._batchCount, 1, 1 );
        }

        _pCmd->transitionBuffer( view._indirectArgs._buffer, RHIBufferState::IndirectArgument );
        _pCmd->transitionBuffer( view._visibleInstances._buffer, RHIBufferState::ShaderResource );
        return true;
    }
} // namespace sw
