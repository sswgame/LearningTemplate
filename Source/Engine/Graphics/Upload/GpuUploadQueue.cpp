#include "pch.h"

#include "Engine/Graphics/Upload/GpuUploadQueue.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "GpuUploadQueue" );

    /**
     * @brief `-gv_gpuUploadQueue=0` — 업로드를 워커로 앞당기지 않고 예전처럼 렌더 스레드가 그 자리에서 만들게 합니다.
     * @details 이 최적화가 무엇을 바꿨는지 재려면 같은 실행에서 끄고 켜 비교할 수 있어야 한다(A/B). 스레딩을 건드리는
     *          기능이라 의심스러울 때 끌 수 있는 스위치이기도 하다 — gv_useRenderThread · gv_gpuCulling 과 같은 자리다.
     */
    SW_GLOBAL_VARIABLE_INT( gv_gpuUploadQueue, 1, "GPU 업로드를 워커로 앞당긴다 (0=렌더 스레드가 그 자리에서 만든다)" );

    void GpuUploadQueue::bindDevice( IRHIDevice* pDevice, TaskManager* pTaskManager )
    {
        // 쌓여 있던 요청은 옛 디바이스의 것이다 — 버린다. 필요한 메시는 다음 프레임 GT 가 다시 요청한다.
        _listPendingMesh.clear();

        _pDevice      = pDevice;
        _pTaskManager = pTaskManager;
        _bParallel    = SW_FALSE;
        if ( pDevice == nullptr || pTaskManager == nullptr )
            return;

        // 워커에서 만들어도 되는지는 백엔드가 말한다 — OpenGL 은 컨텍스트가 스레드에 묶여 안 된다.
        _bParallel = ( pDevice->getCapabilities()._bThreadSafeResourceCreation != 0 ) ? SW_TRUE : SW_FALSE;
        SW_LOG_INFO( "GPU 업로드 큐: %# (백엔드 %#)",
                     _bParallel == SW_TRUE ? "워커 병렬" : "인라인(이 백엔드는 워커 생성 불가)",
                     pDevice->getBackendName() );
    }

    void GpuUploadQueue::requestMesh( const shared_ptr<Mesh>& mesh )
    {
        // 이미 이 디바이스에 올라가 있으면 할 일이 없다 — 상주 판단은 Mesh 가 세대로 한다.
        if ( mesh == nullptr || _pDevice == nullptr || mesh->isUploaded() )
            return;
        if ( mesh->getVertexCount() == 0 )
            return;

        // 같은 프레임에 같은 메시가 여러 배치로 들어온다(메시 하나를 여러 오브젝트가 쓴다). 중복은 여기서 거른다 —
        // 안 거르면 워커 둘이 같은 메시를 동시에 만들어 한쪽 버퍼가 그대로 새어 나간다.
        for ( const shared_ptr<Mesh>& pending : _listPendingMesh )
        {
            if ( pending == mesh )
                return;
        }
        _listPendingMesh.push_back( mesh );
    }

    uint32 GpuUploadQueue::flush()
    {
        if ( _listPendingMesh.empty() || _pDevice == nullptr || gv_gpuUploadQueue == 0 )
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
                (void)mesh->upload( pDevice );
        };

        if ( _bParallel == SW_TRUE && count > 1 && _pTaskManager != nullptr )
        {
            // 워커가 나눠 만들고 여기서 기다린다. 기다리는 것이 목적이 아니라 **RT 가 만들지 않는 것**이 목적이다.
            TaskStageHandle stage  = _pTaskManager->createAnonymousStage( "GpuUploadStage" );
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
                // 태스크를 못 만들었으면 여기서 직접 만든다 — 조용히 안 올리는 것보다 낫다.
                for ( uint32 index = 0; index < count; ++index )
                    uploadOne( index );
            }
        }
        else
        {
            for ( uint32 index = 0; index < count; ++index )
                uploadOne( index );
        }

        _listPendingMesh.clear();
        return count;
    }
} // namespace sw
