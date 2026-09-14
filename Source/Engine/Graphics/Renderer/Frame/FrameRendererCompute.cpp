/**
 * @file FrameRendererCompute.cpp
 * @brief 프레임의 컴퓨트 프리패스 넷 — 인스턴스 애니메이션 · 메시 모프 · GPU 컬링 · 인스턴스 정렬.
 * @details 넷 다 그래프(RenderGraph)의 패스가 아니다. 그리기 **전에** 프레임 커맨드 리스트에 직접 걸리고, 그 순서가
 *          곧 GPU 타임라인의 순서다 — 애니메이션이 바운드를 바꾸고, 컬링이 그 바운드로 거르고, 정렬이 컬링 결과를
 *          되돌린다. 그래서 순서와 배리어가 한 파일 안에 나란히 보여야 한다.
 *
 *          래퍼 클래스는 일부러 두지 않는다 — 예전에 있던 `ComputePass` 는 아무도 쓰지 않은 채 남아 있어 지웠다
 *          (Renderer/README). 각자의 상수버퍼는 `RHIConstantBufferSlot` 이고 소유는 `FrameRenderer` 다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    /**
     * @brief `-gv_gpuCulling=0` — GPU 컬링 컴퓨트 디스패치를 건너뜁니다(인다이렉트 드로우는 그대로).
     * @details 간접 인자는 GpuScene 이 CPU 에서 이미 채워 두므로, 이 디스패치만 빼면 "컴퓨트가 인자를
     *          망치는가" 를 백엔드별로 가를 수 있다. 기본은 켬.
     */
    SW_GLOBAL_VARIABLE_INT( gv_gpuCulling, 1, "GPU 컬링 컴퓨트 디스패치 (0=건너뜀, 진단용)" );

    /**
     * @brief `-gv_morphDiag=<0|1|2|3>` — GPU 메시 모프 경로를 백엔드 능력표와 **무관하게** 돌려 봅니다.
     * @details 0 = 평소대로(`RHICapabilities::_bGpuMeshMorph` 를 따른다), 1 = 능력표를 무시하고 켠다,
     *          2 = 켜되 **컴퓨트 디스패치를 건너뛰고 레스트 버퍼를 정점 셰이더에 그대로 물린다**,
     *          3 = 2 에 더해 풀 원소의 노멀 자리에 **원소 번호**를 적는다.
     *
     *          2 의 정답은 **레스트 포즈와 같은 그림**이다 — 컴퓨트가 아예 안 돌기 때문이다. 그래서
     *          2 만으로 "컴퓨트가 범인인가" 가 갈린다(백로그 1-4 는 이 모드로 컴퓨트를 무죄로 만들었다).
     *          3 은 거기서 한 걸음 더 간다: 번호표를 읽으면 "제 원소를 짚었는가" 를 위치 값과 **따로**
     *          볼 수 있어서, 인덱싱이 틀린 것인지 내용이 틀린 것인지 한 장으로 갈린다.
     */
    SW_GLOBAL_VARIABLE_INT( gv_morphDiag, 0, "메시 모프 진단 (0 평소 / 1 강제 켬 / 2 디스패치 생략 / 3 번호표)" );

    bool FrameRenderer::wantsGpuGeneratedCommands() const
    {
        // 컴퓨트가 드로우 커맨드를 만드는 경로를 쓰려면 인다이렉트 드로우와 컬링 디스패치가 둘 다 켜져
        // 있고 컬링 PSO 가 실제로 만들어져 있어야 한다. 셋 중 하나라도 없으면 CPU 가 채운 개수로 그린다.
        return gv_gpuCulling != 0 && getEnginePso( RenderPassType::GpuCull ) != 0;
    }

    void FrameRenderer::dispatchInstanceAnimation( uint32 instanceCount )
    {
        // 컴퓨트 프리패스 둘 — 인스턴스 애니메이션이 먼저고 컬링이 나중이다. 순서가 뒤집히면 컬링이
        // **이번 프레임에 회전하기 전의** 바운드로 판정한다. 애니메이션이 회전만 바꾸므로 지금 씬에서는
        // 결과가 같지만, 이동을 넣는 순간 한 프레임 늦은 컬링이 된다.
        // 회전을 요청한 인스턴스가 하나도 없으면 디스패치 자체를 건너뛴다 — 안 그러면 회전을 안 쓰는 씬도
        // 매 프레임 인스턴스당 96 바이트를 읽고 아무 일도 하지 않는다. 컬링 능력과는 무관하다(DX11 도 돈다).
        if ( _gpuScene.isUploaded() && instanceCount > 0 && _gpuScene.getSpinInstanceCount() > 0 )
        {
            const RHIPipelineStateHandle animPso = getEnginePso( RenderPassType::InstanceAnim );
            if ( animPso != 0 && _instanceAnimCb.isValid() &&
                 _gpuScene.getInstanceUav() != kInvalidDescriptorIndex )
            {
                struct GpuAnimParams
                {
                    float32 _time{ 0.0f };
                    float32 _baseSpeed{ 0.0f };
                    float32 _speedRange{ 0.0f };
                    uint32  _instanceCount{ 0 };
                } animParams{};
                animParams._time = _animTimer.getTotalTime();
                // 기준 각속도와 편차 폭(라디안/초). 편차가 기준보다 커야 "다 같은 속도"로 보이지 않는다.
                animParams._baseSpeed     = FrameRendererUtil::kGpuSpinBaseSpeed;
                animParams._speedRange    = FrameRendererUtil::kGpuSpinSpeedRange;
                animParams._instanceCount = instanceCount;
                _instanceAnimCb.update( _pDevice, &animParams, sizeof( animParams ) );

                // **쓰기 전에** UAV 상태로 옮긴다. 인스턴스 버퍼는 직전 프레임에 정점 셰이더가 읽던
                // (그리고 방금 CPU 업로드가 쓴) 상태라, 이 전이 없이 UAV 로 쓰면 DX12/Vulkan 에서 쓰기가
                // 유효하지 않다 — 화면은 조용히 예전 값 그대로다.
                _pCmd->transitionBuffer( _gpuScene.getInstanceBuffer(), RHIBufferState::UnorderedAccess );
                _pCmd->setComputePipelineState( animPso );
                // AnimParams(b0) / g_InstancesRW(u0) — instanceanim.hlsl 레지스터와 1:1 대응.
                _pCmd->bindComputeConstantBuffer( _instanceAnimCb._index, 0 );
                _pCmd->bindComputeUav( _gpuScene.getInstanceUav(), 0 );
                const uint32 animGroups = ( instanceCount + 63u ) / 64u;
                if ( animGroups > 0 )
                    _pCmd->dispatchCompute( animGroups, 1, 1 );
                // 다음 디스패치(컬링)와 정점 셰이더가 이 결과를 읽는다 — 쓰기가 끝났음을 알린다.
                _pCmd->transitionBuffer( _gpuScene.getInstanceBuffer(), RHIBufferState::ShaderResource );
            }
        }
    }

    int32 FrameRenderer::getEffectiveMeshMorphDiag() const
    {
        return ( _meshMorphDiagOverride >= 0 ) ? _meshMorphDiagOverride : gv_morphDiag;
    }

    void FrameRenderer::prepareMeshMorphPool()
    {
        const int32 morphDiag = getEffectiveMeshMorphDiag();
        // 백엔드가 못 하면 풀을 만들지도 않는다 — 배치의 base 가 kInvalidBase 로 남아 셰이더가
        // 레스트 포즈로 그린다(언리얼의 스킨 캐시 폴백과 같은 자리). 자세한 사연은 RHICapabilities.h.
        _bMorphBindsRest = SW_FALSE;
        if ( _pDevice == nullptr )
            return;
        if ( _pDevice->getCapabilities()._bGpuMeshMorph == SW_FALSE && morphDiag == 0 )
            return;

        // 풀은 **배치가 든 메시**에서 만든다. 스냅샷 배치가 소유를 들고 있으므로 이 프레임 동안 살아 있다.
        _listScratchMorphMesh.clear();
        for ( const GpuMeshBatch& batch : _gpuScene.getAllBatches() )
        {
            Mesh* pMesh = batch._mesh.get();
            if ( pMesh == nullptr || pMesh->isGpuMorphEnabled() == false )
                continue;
            // 같은 메시가 여러 배치에 나올 수 있다(불투명·투명·뷰) — 풀에는 한 번만 넣는다.
            bool bAlready = false;
            for ( const Mesh* pExisting : _listScratchMorphMesh )
            {
                if ( pExisting == pMesh )
                {
                    bAlready = true;
                    break;
                }
            }
            if ( bAlready == false )
                _listScratchMorphMesh.push_back( pMesh );
        }

        _meshMorphPool.build( _pDevice, _listScratchMorphMesh );

        // 배치에 구간을 적어 둔다 — upload() 가 배치 표(g_SwBatches)에 싣는다. 풀에 못 들어간 메시는 kInvalidBase 라
        // 셰이더가 레스트 포즈로 그린다.
        _gpuScene.assignMorphBases( _meshMorphPool );
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

        // 진단 2 — 컴퓨트를 돌리지 않고 레스트 버퍼를 그대로 정점 셰이더에 물린다(위 gv 주석 참고).
        if ( morphDiag == 2 )
        {
            _bMorphBindsRest = SW_TRUE;
            return;
        }

        // 진단 3 — 레스트 버퍼에 **위치는 진짜 값, 노멀 자리에는 원소 번호**를 적어 올리고 컴퓨트를
        //          건너뛴다. 그러면 한 장으로 "정점 셰이더가 제 원소를 짚었는가"(번호표)와 "그 원소의
        //          위치가 정점 스트림과 같은가"를 **따로** 볼 수 있다 — 값만 비교해서는 둘을 못 가른다.
        //          백로그 1-4 의 OpenGL 증상을 좁힌 것이 이 모드다.
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

        struct GpuMorphParams
        {
            float32 _time{ 0.0f };
            float32 _amplitude{ 0.0f };
            float32 _frequency{ 0.0f };
            uint32  _vertexCount{ 0 };
        } morphParams{};
        morphParams._time        = _animTimer.getTotalTime();
        morphParams._amplitude   = FrameRendererUtil::kMeshMorphAmplitude;
        morphParams._frequency   = FrameRendererUtil::kMeshMorphFrequency;
        morphParams._vertexCount = _meshMorphPool.getVertexCount();
        _meshMorphCb.update( _pDevice, &morphParams, sizeof( morphParams ) );

        // 쓰기 전에 UAV 로, 드로우 전에 다시 SRV 로. 정점 셰이더가 이 버퍼를 읽으므로 배리어가 빠지면
        // DX12/Vulkan 에서 조용히 예전 값이 나온다(인스턴스 애니메이션과 같은 함정).
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

    void FrameRenderer::dispatchCullAndSort( uint32 instanceCount )
    {
        // 컬링은 GpuScene 이 "개수를 컴퓨트에 맡겼다"고 답할 때만 돈다. 그래야 인자의 초기 개수(0)와
        // 디스패치 여부가 절대 어긋나지 않는다 — 어긋나면 한쪽은 빈 화면, 다른 쪽은 낡은 목록이다.
        //
        // **뷰마다 한 번씩** 돈다. 컬링 결과는 절두체에 종속이라, 메인 카메라로 거른 목록을 그림자 패스가
        // 쓰면 화면 밖에서 화면 안으로 그림자를 드리우는 물체가 사라진다. 언리얼이 뷰마다
        // FInstanceCullingContext 를 두는 것과 같은 이유다.
        if ( _gpuScene.isUploaded() && _gpuScene.areIndirectCountsGpuFilled() )
        {
            const RHIPipelineStateHandle cullPso = getEnginePso( RenderPassType::GpuCull );
            if ( cullPso != 0 && _gpuScene.getInstanceSrv() != kInvalidDescriptorIndex &&
                 _gpuScene.getBatchInfoSrv() != kInvalidDescriptorIndex )
            {
                bool bAllViewsCulled = true;
                for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
                {
                    const GpuCullViewResources& view       = _gpuScene.getCullView( static_cast<RenderViewType>( viewIndex ) );
                    const RenderView&           renderView = _arrView[viewIndex];
                    if ( view._indirectArgs._uav == kInvalidDescriptorIndex || view._visibleInstances._uav == kInvalidDescriptorIndex ||
                         renderView.isReadyForCulling() == false )
                    {
                        bAllViewsCulled = false;
                        continue;
                    }

                    struct GpuCullParams
                    {
                        float32 _planes[6][4]{};
                        uint32  _instanceCount{ 0 };
                        uint32  _batchCount{ 0 };
                        uint32  _pad[2]{};
                    } cullParams{};
                    // 절두체는 **뷰가 이미 들고 있다** — setViewProjection 이 행렬과 함께 갱신한다.
                    // 여기서 다시 뽑으면 행렬만 바뀌고 평면이 안 바뀌는 상태가 생길 수 있다.
                    Memory::copy( cullParams._planes, renderView._arrFrustumPlane, sizeof( cullParams._planes ) );
                    cullParams._instanceCount = instanceCount;
                    cullParams._batchCount    = _gpuScene.getIndirectCommandCount();
                    renderView._cullCb.update( _pDevice, &cullParams, sizeof( cullParams ) );

                    // 컬링이 쓰는 두 버퍼는 UAV 상태여야 한다. 간접 인자는 직전 프레임에 IndirectArgument 로,
                    // 가시 목록은 ShaderResource 로 두고 끝냈다.
                    _pCmd->transitionBuffer( view._indirectArgs._buffer, RHIBufferState::UnorderedAccess );
                    _pCmd->transitionBuffer( view._visibleInstances._buffer, RHIBufferState::UnorderedAccess );
                    // PSO 와 바인딩은 **루프 안에서** 다시 건다. 아래 정렬 패스가 둘 다 갈아 끼우므로
                    // 다음 뷰가 정렬 PSO 로 컬링을 돌면 안 된다.
                    _pCmd->setComputePipelineState( cullPso );
                    // CullParams(b0) / g_Instances(t0) / g_BatchInfo(t1) / g_IndirectArgs(u0) / g_VisibleInstanceIds(u1)
                    // — gpucull.hlsl 레지스터와 1:1 대응. 인스턴스·배치 구간은 뷰가 공유한다(절두체만 다르다).
                    _pCmd->bindComputeConstantBuffer( renderView._cullCb._index, 0 );
                    _pCmd->bindComputeShaderResource( _gpuScene.getInstanceSrv(), 0 );
                    _pCmd->bindComputeShaderResource( _gpuScene.getBatchInfoSrv(), 1 );
                    _pCmd->bindComputeUav( view._indirectArgs._uav, 0 );
                    _pCmd->bindComputeUav( view._visibleInstances._uav, 1 );
                    // **인스턴스마다** 스레드 하나다(예전엔 배치마다 하나였다) — 그래야 보이는 것을 골라 압축할 수 있다.
                    const uint32 groups = ( cullParams._instanceCount + 63u ) / 64u;
                    if ( groups > 0 )
                        _pCmd->dispatchCompute( groups, 1, 1 );
                    // 압축이 끝났으면 투명 배치의 순서를 깊이순으로 되돌린다. 원자 연산이 준 자리 번호는
                    // 완료 순서라 그대로 두면 블렌딩이 틀린다 — 예전엔 그래서 투명이 압축을 통째로
                    // 포기했다. 정렬을 GPU 로 옮기면 투명도 컬링 이득을 받는다.
                    // 컬링이 채운 목록을 정렬이 바로 읽는다 — 상태가 그대로라 transitionBuffer 는
                    // 아무것도 하지 않으므로 **UAV 배리어**를 따로 걸어야 한다. 없으면 정렬이 아직
                    // 안 채워진 목록을 읽는다(백엔드마다 결과가 달라 재현이 어렵다).
                    _pCmd->uavBarrier( view._indirectArgs._buffer );
                    _pCmd->uavBarrier( view._visibleInstances._buffer );

                    const RHIPipelineStateHandle sortPso = getEnginePso( RenderPassType::InstanceSort );
                    if ( sortPso != 0 && _instanceSortCb.isValid() &&
                         cullParams._batchCount > 0 )
                    {
                        struct GpuSortParams
                        {
                            float32 _cameraPos[4]{};
                            uint32  _instanceCount{ 0 };
                            uint32  _batchCount{ 0 };
                            uint32  _pad[2]{};
                        } sortParams{};
                        // 정렬 키는 **그 뷰의 눈까지의 거리**다 — 뷰가 자기 위치를 들고 있다.
                        sortParams._cameraPos[0]  = renderView._position._x;
                        sortParams._cameraPos[1]  = renderView._position._y;
                        sortParams._cameraPos[2]  = renderView._position._z;
                        sortParams._instanceCount = instanceCount;
                        sortParams._batchCount    = cullParams._batchCount;
                        // 값이 뷰마다 같으므로 버퍼 하나로 충분하다 — 다르게 만들 일이 생기면 컬링 CB 처럼
                        // 뷰마다 하나로 나눠야 한다(하나를 나눠 쓰면 뒤 업로드가 앞 디스패치를 덮어쓴다).
                        _instanceSortCb.update( _pDevice, &sortParams, sizeof( sortParams ) );

                        _pCmd->setComputePipelineState( sortPso );
                        // 바인딩 자리는 컬링과 같다 — 인자·가시 목록을 그대로 읽고 쓴다.
                        _pCmd->bindComputeConstantBuffer( _instanceSortCb._index, 0 );
                        _pCmd->bindComputeShaderResource( _gpuScene.getInstanceSrv(), 0 );
                        _pCmd->bindComputeShaderResource( _gpuScene.getBatchInfoSrv(), 1 );
                        _pCmd->bindComputeUav( view._indirectArgs._uav, 0 );
                        _pCmd->bindComputeUav( view._visibleInstances._uav, 1 );
                        // 배치마다 워크그룹 하나 — 그 배치의 목록을 그룹공유 안에서 정렬한다.
                        _pCmd->dispatchCompute( cullParams._batchCount, 1, 1 );
                    }

                    _pCmd->transitionBuffer( view._indirectArgs._buffer, RHIBufferState::IndirectArgument );
                    _pCmd->transitionBuffer( view._visibleInstances._buffer, RHIBufferState::ShaderResource );
                }
                _bGpuCullingActive = bAllViewsCulled ? 1u : 0u;
            }
        }
    }
} // namespace sw
