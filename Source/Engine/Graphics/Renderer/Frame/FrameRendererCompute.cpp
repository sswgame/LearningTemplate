/**
 * @file FrameRendererCompute.cpp
 * @brief 프레임의 컴퓨트 프리패스 다섯입니다: 인스턴스 애니메이션 · 메시 모프 · 메시 스킨 · GPU 컬링 · 인스턴스 정렬.
 * @details 넷 다 그래프(RenderGraph)의 패스가 아닙니다. 그리기 **전에** 프레임 커맨드 리스트에 직접 걸리고, 그 순서가
 *          곧 GPU 타임라인의 순서입니다. 애니메이션이 바운드를 바꾸고, 컬링이 그 바운드로 거르고, 정렬이 컬링 결과를
 *          되돌립니다. 그래서 순서와 배리어가 한 파일 안에 나란히 보여야 합니다.
 *
 *          컴퓨트 패스를 감싸는 래퍼 클래스는 두지 않습니다. 각자의 상수버퍼는 `RHIConstantBufferSlot` 이고 소유는
 *          `FrameRenderer` 입니다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

namespace sw
{
    /**
     * @brief `-gv_gpuCulling=0` 이면 GPU 컬링 컴퓨트 디스패치를 건너뜁니다(인다이렉트 드로우는 그대로).
     * @details 간접 인자는 GpuScene 이 CPU 에서 이미 채워 두므로, 이 디스패치만 빼면 "컴퓨트가 인자를
     *          망치는가" 를 백엔드별로 가를 수 있습니다. 기본은 켬입니다.
     */
    SW_GLOBAL_VARIABLE_INT( gv_gpuCulling, 1, "GPU 컬링 컴퓨트 디스패치 (0=건너뜀, 진단용)" );

    /**
     * @brief `-gv_morphDiag=<0|1|2|3>` 는 GPU 메시 모프 경로를 백엔드 능력표와 **상관없이** 돌려 봅니다.
     * @details 0 = 평소대로(`RHICapabilities::_bGpuMeshMorph` 를 따름), 1 = 능력표를 무시하고 켬,
     *          2 = 켜되 **컴퓨트 디스패치를 건너뛰고 레스트 버퍼를 정점 셰이더에 그대로 물림**,
     *          3 = 2 에 더해 풀 원소의 노멀 자리에 **원소 번호**를 적음.
     *
     *          2 의 정답은 **레스트 포즈와 같은 그림**입니다. 컴퓨트가 아예 안 돌기 때문입니다. 그래서
     *          2 만으로 "컴퓨트가 범인인가" 가 갈립니다.
     *          3 은 거기서 한 걸음 더 갑니다. 번호표를 읽으면 "제 원소를 짚었는가" 를 위치 값과 **따로**
     *          볼 수 있어서, 인덱싱이 틀린 것인지 내용이 틀린 것인지 한 장으로 갈립니다.
     */
    SW_GLOBAL_VARIABLE_INT( gv_morphDiag, 0, "메시 모프 진단 (0 평소 / 1 강제 켬 / 2 디스패치 생략 / 3 번호표)" );

    bool FrameRenderer::usesGpuGeneratedCommands() const
    {
        // 컴퓨트가 드로우 커맨드를 만드는 경로를 쓰려면 인다이렉트 드로우와 컬링 디스패치가 둘 다 켜져
        // 있고 컬링 PSO 가 실제로 만들어져 있어야 한다. 셋 중 하나라도 없으면 CPU 가 채운 개수로 그린다.
        return gv_gpuCulling != 0 && getEnginePso( RenderPassType::GpuCull ) != 0;
    }

    void FrameRenderer::dispatchInstanceAnimation( uint32 instanceCount )
    {
        // 컴퓨트 프리패스 둘. 인스턴스 애니메이션이 먼저고 컬링이 나중이다. 순서가 뒤집히면 컬링이
        // **이번 프레임에 회전하기 전의** 바운드로 판정한다. 애니메이션이 회전만 바꾸므로 지금 씬에서는
        // 결과가 같지만, 이동을 넣는 순간 한 프레임 늦은 컬링이 된다.
        // 회전을 요청한 인스턴스가 하나도 없으면 디스패치 자체를 건너뛴다. 안 그러면 회전을 안 쓰는 씬도
        // 매 프레임 인스턴스당 96 바이트를 읽고 아무 일도 하지 않는다. 컬링 능력과는 무관하다(DX11 도 돈다).
        if ( _gpuScene.isUploaded() && instanceCount > 0 && _gpuScene.getSpinInstanceCount() > 0 )
        {
            const RHIPipelineStateHandle animPso = getEnginePso( RenderPassType::InstanceAnim );
            if ( animPso != 0 && _instanceAnimCb.isValid() &&
                 _gpuScene.getInstanceUav() != kInvalidDescriptorIndex )
            {
                FrameRendererUtil::GpuAnimParams animParams{};
                animParams._time = getAnimationTime();
                // 기준 각속도와 편차 폭(라디안/초). 편차가 기준보다 커야 "다 같은 속도"로 보이지 않는다.
                animParams._baseSpeed     = FrameRendererUtil::kGpuSpinBaseSpeed;
                animParams._speedRange    = FrameRendererUtil::kGpuSpinSpeedRange;
                animParams._instanceCount = instanceCount;
                _instanceAnimCb.update( *_pCmd, &animParams, sizeof( animParams ) );

                // **쓰기 전에** UAV 상태로 옮긴다. 인스턴스 버퍼는 직전 프레임에 정점 셰이더가 읽던
                // (그리고 방금 CPU 업로드가 쓴) 상태라, 이 전이 없이 UAV 로 쓰면 DX12 · Vulkan 에서 쓰기가
                // 유효하지 않다. 화면은 조용히 예전 값 그대로다.
                _pCmd->transitionBuffer( _gpuScene.getInstanceBuffer(), RHIBufferState::UnorderedAccess );
                _pCmd->setComputePipelineState( animPso );
                // AnimParams(b0) / g_InstancesRW(u0). instanceanim.hlsl 레지스터와 1:1 대응.
                _pCmd->bindComputeConstantBuffer( _instanceAnimCb._index, 0 );
                _pCmd->bindComputeUav( _gpuScene.getInstanceUav(), 0 );
                const uint32 animGroups = ( instanceCount + 63u ) / 64u;
                if ( animGroups > 0 )
                    _pCmd->dispatchCompute( animGroups, 1, 1 );
                // 다음 디스패치(컬링)와 정점 셰이더가 이 결과를 읽는다. 쓰기가 끝났음을 알린다.
                _pCmd->transitionBuffer( _gpuScene.getInstanceBuffer(), RHIBufferState::ShaderResource );
            }
        }
    }

    int32 FrameRenderer::getEffectiveMeshMorphDiag() const
    {
        return ( _meshMorphDiagOverride >= 0 ) ? _meshMorphDiagOverride : gv_morphDiag;
    }

    float32 FrameRenderer::getAnimationTime() const
    {
#if !defined( SW_SHIPPING )
        if ( _animationTimeOverride >= 0.0f )
            return _animationTimeOverride;
#endif
        return _animTimer.getTotalTime();
    }

    void FrameRenderer::prepareMeshMorphPool()
    {
        const int32 morphDiag = getEffectiveMeshMorphDiag();
        // 백엔드가 못 하면 풀을 만들지도 않는다. 배치의 base 가 kInvalidBase 로 남아 셰이더가
        // 레스트 포즈로 그린다(언리얼의 스킨 캐시 폴백과 같은 자리). 자세한 사연은 RHICapabilities.h.
        _bMorphBindsRest = SW_FALSE;
        if ( _pDevice == nullptr )
            return;
        if ( _pDevice->getCapabilities()._bGpuMeshMorph == SW_FALSE && morphDiag == 0 )
            return;

        // 풀은 **배치가 든 메시**에서 만든다. 스냅샷 배치가 소유를 들고 있으므로 이 프레임 동안 살아 있다.
        // 스킨드 메시는 스킨 구간(뒤)으로, 모프를 켠 메시는 모프 구간(앞)으로 간다.
        _listScratchMorphMesh.clear();
        _listScratchSkinMesh.clear();
        _listScratchVertexAnimationMesh.clear();
        for ( const GpuMeshBatch& batch : _gpuScene.getAllBatches() )
        {
            Mesh* pMesh = batch._mesh.get();
            if ( pMesh == nullptr )
                continue;
            // 정점 애니메이션(VAT) 메시는 컴퓨트 없이 정점 셰이더가 표를 읽는다 — 표 풀로 간다.
            if ( pMesh->findVertexAnimation() != nullptr )
            {
                if ( std::find( _listScratchVertexAnimationMesh.begin(), _listScratchVertexAnimationMesh.end(), pMesh ) == _listScratchVertexAnimationMesh.end() )
                    _listScratchVertexAnimationMesh.push_back( pMesh );
                continue;
            }
            const bool bSkin = pMesh->hasSkin();
            if ( bSkin == false && pMesh->isGpuMorphEnabled() == false )
                continue;
            // 같은 메시가 여러 배치에 나올 수 있다(불투명 · 투명 · 뷰). 풀에는 한 번만 넣는다.
            vector<Mesh*>& listTarget = bSkin ? _listScratchSkinMesh : _listScratchMorphMesh;
            if ( std::find( listTarget.begin(), listTarget.end(), pMesh ) == listTarget.end() )
                listTarget.push_back( pMesh );
        }

        _meshMorphPool.build( _pDevice, _listScratchMorphMesh, _listScratchSkinMesh );
        _meshMorphPool.uploadSkinPalettes( _pDevice, _gpuScene.getSkinPalettes(), _gpuScene.findSkinPaletteRows(), _gpuScene.findMorphWeights() );

        // 배치에 구간을 적어 둔다. upload() 가 배치 표(g_SwBatches)에 싣는다. 풀에 못 들어간 메시는 kInvalidBase 라
        // 셰이더가 레스트 포즈로 그린다.
        _gpuScene.assignMorphBases( _meshMorphPool );
        _vertexAnimationPool.build( _pDevice, _listScratchVertexAnimationMesh );
        _gpuScene.assignVertexAnimationBases( _vertexAnimationPool );
        SW_PROFILE_COUNT( "RT.Skin.instances", _meshMorphPool.getSkinInstanceCount() );
        SW_PROFILE_COUNT( "RT.Skin.vertices", _meshMorphPool.getSkinVertexCount() );
        SW_PROFILE_COUNT( "RT.Skin.sourceVertices", _meshMorphPool.getSkinSourceVertexCount() );
    }

    void FrameRenderer::dispatchMeshMorph()
    {
        const int32 morphDiag = getEffectiveMeshMorphDiag();
        if ( _pDevice == nullptr || _pCmd == nullptr )
            return;
        if ( _pDevice->getCapabilities()._bGpuMeshMorph == SW_FALSE && morphDiag == 0 )
            return;
        if ( _meshMorphPool.isDispatchable() == false )
            return;

        // 진단 2: 컴퓨트를 돌리지 않고 레스트 버퍼를 그대로 정점 셰이더에 물린다(위 gv 주석 참고).
        if ( morphDiag == 2 )
        {
            _bMorphBindsRest = SW_TRUE;
            return;
        }

        // 진단 3: 레스트 버퍼에 **위치는 진짜 값, 노멀 자리에는 원소 번호**를 적어 올리고 컴퓨트를
        //         건너뛴다. 그러면 한 장으로 "정점 셰이더가 제 원소를 짚었는가"(번호표)와 "그 원소의
        //         위치가 정점 스트림과 같은가" 를 **따로** 볼 수 있다. 값만 비교해서는 둘을 못 가른다.
        if ( morphDiag == 3 )
        {
            _listScratchMorphTag.clear();
            _listScratchMorphTag.reserve( _meshMorphPool.getVertexCount() );
            uint32 element = 0;
            for ( const Mesh* pMesh : _listScratchMorphMesh )
            {
                if ( pMesh == nullptr )
                    continue;
                for ( const RHIVertex& vertex : pMesh->getVertices() )
                {
                    GpuMorphVertex tagged{};
                    tagged._position = float4{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2], 1.0f };
                    tagged._normal   = float4{ static_cast<float32>( element ), 0.0f, 0.0f, 0.0f };
                    _listScratchMorphTag.push_back( tagged );
                    ++element;
                }
            }
            _bMorphBindsRest = SW_TRUE;
            _meshMorphPool.getRestBuffer().upload( _pDevice, _listScratchMorphTag.data(),
                                                   static_cast<uint32>( _listScratchMorphTag.size() * sizeof( GpuMorphVertex ) ) );
            return;
        }

        const RHIPipelineStateHandle morphPso = getEnginePso( RenderPassType::MeshMorph );
        if ( morphPso == 0 || _meshMorphCb.isValid() == false )
            return;

        FrameRendererUtil::GpuMorphParams morphParams{};
        morphParams._time        = getAnimationTime();
        morphParams._amplitude   = FrameRendererUtil::kMeshMorphAmplitude;
        morphParams._frequency   = FrameRendererUtil::kMeshMorphFrequency;
        morphParams._vertexCount = _meshMorphPool.getMorphVertexCount();
        if ( morphParams._vertexCount == 0 )
            return;
        _meshMorphCb.update( *_pCmd, &morphParams, sizeof( morphParams ) );

        // 쓰기 전에 UAV 로, 드로우 전에 다시 SRV 로. 정점 셰이더가 이 버퍼를 읽으므로 배리어가 빠지면
        // DX12 · Vulkan 에서 조용히 예전 값이 나온다(인스턴스 애니메이션과 같은 함정).
        _pCmd->transitionBuffer( _meshMorphPool.getMorphBuffer()._buffer, RHIBufferState::UnorderedAccess );
        _pCmd->setComputePipelineState( morphPso );
        _pCmd->bindComputeConstantBuffer( _meshMorphCb._index, 0 );
        _pCmd->bindComputeShaderResource( _meshMorphPool.getRestBuffer()._srv, 0 );
        _pCmd->bindComputeUav( _meshMorphPool.getMorphBuffer()._uav, 0 );
        const uint32 morphGroups = ( morphParams._vertexCount + 63u ) / 64u;
        if ( morphGroups > 0 )
            _pCmd->dispatchCompute( morphGroups, 1, 1 );
        _pCmd->transitionBuffer( _meshMorphPool.getMorphBuffer()._buffer, RHIBufferState::ShaderResource );
    }

    void FrameRenderer::dispatchMeshSkin()
    {
        if ( _pDevice == nullptr || _pCmd == nullptr )
            return;
        const int32 morphDiag = getEffectiveMeshMorphDiag();
        // 진단 2 · 3 은 레스트 버퍼를 그대로 물려 "컴퓨트 없이" 를 본다 — 스키닝도 돌리지 않는다(바인드 포즈).
        if ( ( _pDevice->getCapabilities()._bGpuMeshMorph == SW_FALSE && morphDiag == 0 ) || morphDiag == 2 || morphDiag == 3 )
            return;
        if ( _meshMorphPool.isSkinDispatchable() == false )
            return;
        const RHIPipelineStateHandle skinPso = getEnginePso( RenderPassType::MeshSkin );
        if ( skinPso == 0 || _meshSkinCb.isValid() == false )
            return;

        FrameRendererUtil::GpuSkinParams skinParams{};
        skinParams._skinVertexBase    = _meshMorphPool.getSkinVertexBase();
        skinParams._skinVertexCount   = _meshMorphPool.getSkinVertexCount();
        skinParams._skinBoneCount     = _meshMorphPool.getSkinBoneCount();
        skinParams._skinInstanceCount = _meshMorphPool.getSkinInstanceCount();
        skinParams._skinDeltaBase     = _meshMorphPool.getSkinDeltaBase();
        _meshSkinCb.update( *_pCmd, &skinParams, sizeof( skinParams ) );

        // 모프와 같은 결과 버퍼의 뒤 구간에 쓴다. 쓰기 전에 UAV 로, 드로우 전에 다시 SRV 로(배리어가 빠지면 DX12 · Vulkan 에서 예전 값이 나온다).
        _pCmd->transitionBuffer( _meshMorphPool.getMorphBuffer()._buffer, RHIBufferState::UnorderedAccess );
        _pCmd->setComputePipelineState( skinPso );
        // SkinParams(b0) / g_RestVertices(t0, 원본 레스트) / g_SkinWeights(t1) / g_SkinPalette(t2) / g_SkinInstances(t3) / g_MorphVerticesRW(u0).
        // meshskin.hlsl 레지스터와 1:1 대응.
        _pCmd->bindComputeConstantBuffer( _meshSkinCb._index, 0 );
        _pCmd->bindComputeShaderResource( _meshMorphPool.getSkinRestBuffer()._srv, 0 );
        _pCmd->bindComputeShaderResource( _meshMorphPool.getSkinWeightBuffer()._srv, 1 );
        _pCmd->bindComputeShaderResource( _meshMorphPool.getSkinPaletteBuffer()._srv, 2 );
        _pCmd->bindComputeShaderResource( _meshMorphPool.getSkinInstanceBuffer()._srv, 3 );
        _pCmd->bindComputeUav( _meshMorphPool.getMorphBuffer()._uav, 0 );
        const uint32 skinGroups = ( skinParams._skinVertexCount + 63u ) / 64u;
        if ( skinGroups > 0 )
            _pCmd->dispatchCompute( skinGroups, 1, 1 );
        _pCmd->transitionBuffer( _meshMorphPool.getMorphBuffer()._buffer, RHIBufferState::ShaderResource );
    }

    void FrameRenderer::dispatchCullAndSort( uint32 instanceCount )
    {
        // 컬링은 GpuScene 이 "개수를 컴퓨트에 맡겼다" 고 답할 때만 돈다. 그래야 인자의 초기 개수(0)와
        // 디스패치 여부가 절대 어긋나지 않는다. 어긋나면 한쪽은 빈 화면, 다른 쪽은 낡은 목록이다.
        //
        // **뷰마다 한 번씩** 돈다. 컬링 결과는 절두체에 종속이라, 메인 카메라로 거른 목록을 그림자 패스가
        // 쓰면 화면 밖에서 화면 안으로 그림자를 드리우는 물체가 사라진다. 언리얼이 뷰마다
        // FInstanceCullingContext 를 두는 것과 같은 이유다. 이번 프레임에 그리는 추가 뷰(CCTV · PiP)도 자기 칸을 돈다.
        if ( _gpuScene.isUploaded() == false || _gpuScene.areIndirectCountsGpuFilled() == false )
            return;
        if ( getEnginePso( RenderPassType::GpuCull ) == 0 || _gpuScene.getInstanceSrv() == kInvalidDescriptorIndex ||
             _gpuScene.getBatchInfoSrv() == kInvalidDescriptorIndex )
            return;

        bool bAllViewsCulled = true;
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
            bAllViewsCulled = dispatchCullView( viewIndex, _arrView[viewIndex], instanceCount ) && bAllViewsCulled;
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            // 컬링 못 한 추가 뷰는 그리지 않는다 — 가시 목록을 거는 프레임에 갱신 안 된 목록을 읽게 된다.
            if ( pView->_bRenderThisFrame == SW_TRUE && dispatchCullView( pView->_cullSlot, pView->_cullInput, instanceCount ) == false )
                pView->_bRenderThisFrame = SW_FALSE;
        }
        _bGpuCullingActive = bAllViewsCulled ? 1u : 0u;
    }

    bool FrameRenderer::dispatchCullView( uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount )
    {
        if ( cullViewIndex >= _gpuScene.getCullViewCount() )
            return false;
        const GpuCullViewResources& view = _gpuScene.getCullView( cullViewIndex );
        if ( view._indirectArgs._uav == kInvalidDescriptorIndex || view._visibleInstances._uav == kInvalidDescriptorIndex || renderView.isReadyForCulling() == false )
            return false;

        FrameRendererUtil::GpuCullParams cullParams{};
        // 절두체는 **뷰가 이미 들고 있다.** setViewProjection 이 행렬과 함께 갱신한다.
        // 여기서 다시 뽑으면 행렬만 바뀌고 평면이 안 바뀌는 상태가 생길 수 있다.
        Memory::copy( cullParams._arrPlane, renderView._frustum._arrPlane, sizeof( cullParams._arrPlane ) );
        cullParams._instanceCount = instanceCount;
        cullParams._batchCount    = _gpuScene.getIndirectCommandCount();
        // 상수버퍼는 **뷰마다 자기 것**이다. 하나를 나눠 쓰면 뒤 업로드가 앞 디스패치가 읽을 내용을 덮어쓴다.
        renderView._cullCb.update( *_pCmd, &cullParams, sizeof( cullParams ) );

        // 컬링이 쓰는 두 버퍼는 UAV 상태여야 한다. 간접 인자는 직전 프레임에 IndirectArgument 로,
        // 가시 목록은 ShaderResource 로 두고 끝냈다.
        _pCmd->transitionBuffer( view._indirectArgs._buffer, RHIBufferState::UnorderedAccess );
        _pCmd->transitionBuffer( view._visibleInstances._buffer, RHIBufferState::UnorderedAccess );
        // PSO 와 바인딩은 **뷰마다** 다시 건다. 아래 정렬 패스가 둘 다 갈아 끼우므로 다음 뷰가 정렬 PSO 로 컬링을 돌면 안 된다.
        _pCmd->setComputePipelineState( getEnginePso( RenderPassType::GpuCull ) );
        // CullParams(b0) / g_Instances(t0) / g_BatchInfo(t1) / g_IndirectArgs(u0) / g_VisibleInstanceIds(u1).
        // gpucull.hlsl 레지스터와 1:1 대응이다. 인스턴스 · 배치 구간은 뷰가 공유한다(절두체만 다르다).
        _pCmd->bindComputeConstantBuffer( renderView._cullCb._index, 0 );
        _pCmd->bindComputeShaderResource( _gpuScene.getInstanceSrv(), 0 );
        _pCmd->bindComputeShaderResource( _gpuScene.getBatchInfoSrv(), 1 );
        _pCmd->bindComputeUav( view._indirectArgs._uav, 0 );
        _pCmd->bindComputeUav( view._visibleInstances._uav, 1 );
        // **인스턴스마다** 스레드 하나다. 그래야 보이는 것을 골라 압축할 수 있다.
        const uint32 groups = ( cullParams._instanceCount + 63u ) / 64u;
        if ( groups > 0 )
            _pCmd->dispatchCompute( groups, 1, 1 );
        // 압축이 끝났으면 투명 배치의 순서를 깊이순으로 되돌린다. 원자 연산이 준 자리 번호는
        // 완료 순서라 그대로 두면 블렌딩이 틀린다. 정렬이 GPU 에서 돌므로 투명도 압축 · 컬링 이득을 받는다.
        // 컬링이 채운 목록을 정렬이 바로 읽는다. 상태가 그대로라 transitionBuffer 는
        // 아무것도 하지 않으므로 **UAV 배리어**를 따로 걸어야 한다. 없으면 정렬이 아직
        // 안 채워진 목록을 읽는다(백엔드마다 결과가 달라 재현이 어렵다).
        _pCmd->uavBarrier( view._indirectArgs._buffer );
        _pCmd->uavBarrier( view._visibleInstances._buffer );

        const RHIPipelineStateHandle sortPso = getEnginePso( RenderPassType::InstanceSort );
        if ( sortPso != 0 && renderView._sortCb.isValid() && cullParams._batchCount > 0 )
        {
            FrameRendererUtil::GpuSortParams sortParams{};
            // 정렬 키는 **그 뷰의 눈까지의 거리**다. 뷰가 자기 위치를 들고 있다.
            sortParams._arrCameraPos[0] = renderView._position._x;
            sortParams._arrCameraPos[1] = renderView._position._y;
            sortParams._arrCameraPos[2] = renderView._position._z;
            sortParams._instanceCount   = instanceCount;
            sortParams._batchCount      = cullParams._batchCount;
            // 정렬 상수버퍼도 **뷰마다 자기 것**이다 — 눈 자리가 뷰마다 다르다(나눠 쓰면 모든 뷰가 마지막 뷰의 눈으로 정렬한다).
            renderView._sortCb.update( *_pCmd, &sortParams, sizeof( sortParams ) );

            _pCmd->setComputePipelineState( sortPso );
            // 바인딩 자리는 컬링과 같다. 인자 · 가시 목록을 그대로 읽고 쓴다.
            _pCmd->bindComputeConstantBuffer( renderView._sortCb._index, 0 );
            _pCmd->bindComputeShaderResource( _gpuScene.getInstanceSrv(), 0 );
            _pCmd->bindComputeShaderResource( _gpuScene.getBatchInfoSrv(), 1 );
            _pCmd->bindComputeUav( view._indirectArgs._uav, 0 );
            _pCmd->bindComputeUav( view._visibleInstances._uav, 1 );
            // 배치마다 워크그룹 하나. 그 배치의 목록을 그룹 공유 메모리 안에서 정렬한다.
            _pCmd->dispatchCompute( cullParams._batchCount, 1, 1 );
        }

        _pCmd->transitionBuffer( view._indirectArgs._buffer, RHIBufferState::IndirectArgument );
        _pCmd->transitionBuffer( view._visibleInstances._buffer, RHIBufferState::ShaderResource );
        return true;
    }
} // namespace sw
