#include "pch.h"

#include "Engine/Graphics/Upload/GPUUploadQueue.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "GPUUploadQueue" );

    /**
     * @brief `-gv_gpuUploadQueue=0` 이면 업로드를 워커로 앞당기지 않고 렌더 스레드가 그 자리에서 만듭니다.
     * @details 이 최적화가 무엇을 바꿨는지 측정하려면 같은 실행에서 끄고 켜 비교할 수 있어야 합니다(A/B). 스레딩을 건드리는
     *          기능이라 의심스러울 때 끌 수 있는 스위치이기도 합니다. gv_useRenderThread, gv_gpuCulling 과 같은 종류입니다.
     */
    SW_GLOBAL_VARIABLE( bool, gv_gpuUploadQueue, true, "GPU 업로드를 워커로 앞당긴다 (false=렌더 스레드가 그 자리에서 만든다)" );

    void GPUUploadQueue::bindDevice( IRHIDevice* pDevice, TaskManager* pTaskManager )
    {
        // 쌓여 있던 요청은 옛 디바이스의 것이므로 버린다. 필요한 메시는 다음 프레임 GT 가 다시 요청한다.
        _listPendingMesh.clear();

        _pDevice      = pDevice;
        _pTaskManager = pTaskManager;
        _bParallel    = SW_FALSE;
        if ( pDevice == nullptr || pTaskManager == nullptr )
            return;

        // 워커에서 만들어도 되는지는 백엔드가 말한다. OpenGL 은 컨텍스트가 스레드에 묶여 안 된다.
        _bParallel = ( pDevice->getCapabilities()._bThreadSafeResourceCreation != SW_FALSE ) ? SW_TRUE : SW_FALSE;
        SW_LOG_INFO( "GPU upload queue: %# (backend %#)",
                     _bParallel == SW_TRUE ? "parallel on workers" : "off - the render thread creates them in that frame (this backend cannot create on workers)",
                     pDevice->getBackendName() );
    }

    void GPUUploadQueue::requestMesh( const shared_ptr<Mesh>& mesh )
    {
        // 워커 생성을 못 하는 백엔드(OpenGL)에서는 받지 않는다. 렌더 스레드가 그 프레임의 업로드(`GPUScene` 의 `initRHI`)에서 만든다.
        // 주의: 게임 스레드가 대신 만들면 렌더 스레드가 컨텍스트를 오래 점유한 동안(셰이더 실시간 컴파일) 컨텍스트 대기가 시간을 넘겨 메시를 못 만든다.
        if ( _bParallel == SW_FALSE )
            return;
        // 이미 이 디바이스에 업로드되어 있으면 할 일이 없다. 상주 판단은 Mesh 가 세대로 한다.
        const bool bNothingToUpload = mesh == nullptr || _pDevice == nullptr || mesh->isRHIValid();
        if ( bNothingToUpload )
            return;
        if ( mesh->getVertexCount() == 0 )
            return;

        // 같은 프레임에 같은 메시가 여러 배치로 들어온다(메시 하나를 여러 오브젝트가 쓴다). 중복은 여기서 거른다.
        // 안 거르면 워커 둘이 같은 메시를 동시에 만들어 한쪽 버퍼가 누수된다.
        for ( const shared_ptr<Mesh>& pending : _listPendingMesh )
        {
            if ( pending == mesh )
                return;
        }
        _listPendingMesh.push_back( mesh );
    }

    uint32 GPUUploadQueue::flush()
    {
        const bool bNothingToFlush = _listPendingMesh.empty() || _pDevice == nullptr || gv_gpuUploadQueue == false;
        if ( bNothingToFlush )
        {
            _listPendingMesh.clear();
            return 0;
        }

        SW_PROFILE_SCOPE( "GT.GpuUpload.flush" );

        const uint32 count = static_cast<uint32>( _listPendingMesh.size() );
        SW_PROFILE_COUNT( "GT.GpuUpload.meshCount", count );

        IRHIDevice*                     pDevice   = _pDevice;
        const vector<shared_ptr<Mesh>>& listMesh  = _listPendingMesh;
        auto                            uploadOne = [pDevice, &listMesh]( uint32 index )
        {
            const shared_ptr<Mesh>& mesh = listMesh[index];
            if ( mesh != nullptr )
                (void)mesh->initRHI( pDevice );
        };

        const bool bParallelUploadPossible = _bParallel == SW_TRUE && count > 1 && _pTaskManager != nullptr;
        if ( bParallelUploadPossible )
        {
            // 워커가 나눠 만들고 여기서 기다린다. 기다리는 것이 목적이 아니라 **RT 가 만들지 않는 것**이 목적이다.
            TaskStageHandle stage  = _pTaskManager->createStage( "GpuUploadMesh" );
            TaskHandle      handle = _pTaskManager->emplaceParallel( "GpuUploadMesh", count,
                                                                     SW_DELEGATE_LAMBDA( ParallelTaskDelegate, uploadOne ) );
            if ( handle.isValid() )
            {
                stage.addTask( handle );
                handle.submit();
                _pTaskManager->waitStage( stage );
            }
            else
            {
                // 태스크를 만들지 못했으면 여기서 직접 만든다. 조용히 업로드하지 않는 것보다 낫다.
                for ( uint32 index = 0; index < count; ++index )
                {
                    uploadOne( index );
                }
            }
        }
        else
        {
            for ( uint32 index = 0; index < count; ++index )
            {
                uploadOne( index );
            }
        }

        _listPendingMesh.clear();
        return count;
    }
} // namespace sw
