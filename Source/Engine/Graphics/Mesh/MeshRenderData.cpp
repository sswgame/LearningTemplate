#include "pch.h"

#include "Engine/Graphics/Mesh/MeshRenderData.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"

namespace sw
{
    MeshRenderData::~MeshRenderData()
    {
        releaseBuffers();
    }

    bool MeshRenderData::initRHI( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || _owner.getIndices().empty() )
            return false;

        // 워커에서 만들어도 되는지는 **백엔드가 알려 준다**. DX12, DX11, Vulkan 은 버퍼 생성이 디바이스 레벨이고
        // 핸들 테이블도 잠겨 있어 안전하다. OpenGL 은 glGen* 이 현재 컨텍스트를 필요로 해서 안 된다. 그 백엔드에서
        // 워커가 여기 들어왔다면 부른 쪽이 틀린 것이다. GPUUploadQueue 는 그 백엔드에서 요청을 받지 않고 렌더 스레드가 만든다.
        if ( engine::areEngineServicesBound() && pDevice->getCapabilities()._bThreadSafeResourceCreation == SW_FALSE )
            SW_ASSERT( engine::getSubsystem<TaskManager>().isWorkerThread() == false );
        // 이미 GPU 에 있으면 그대로 둔다. 이전 디바이스가 죽었다면 통보(`releaseRHI`, `clearRHI`)가 먼저 와서
        // 여기를 비워 놓았으므로, 값이 남아 있다는 것은 곧 살아 있는 디바이스의 것이라는 뜻이다.
        if ( isRHIValid() )
            return true;
        releaseBuffers();

        IRHIResourceFactory* pResource = pDevice->getResourceFactory();
        if ( pResource == nullptr )
            return false;
        // 업로드는 CPU 사본을 읽기만 한다 - 원본 메시를 워커(외형 조립)가 함께 읽으므로 const 로 받아 경합 검출기가 쓰기로 보지 않게 한다.
        const vector<uint32>&    listIndex     = _owner.getIndices();
        const vector<uint32>&    listLODIndex  = _owner.getLODIndices();
        const vector<RHIVertex>& listVertex    = _owner.getVertices();
        const vector<RHIVertex>& listLODVertex = _owner.getLODVertices();
        vector<uint32>           listDrawIndex;
        if ( listLODIndex.empty() == false )
        {
            listDrawIndex.reserve( _owner.getDrawIndexCount() );
            listDrawIndex.insert( listDrawIndex.end(), listIndex.begin(), listIndex.end() );
            listDrawIndex.insert( listDrawIndex.end(), listLODIndex.begin(), listLODIndex.end() );
        }
        const uint32* pDrawIndex = listDrawIndex.empty() ? listIndex.data() : listDrawIndex.data();
        // GPU 정점 = [위치 구간][속성 구간](RHIMeshVertexStreams) — 형식은 메시가 정한다. 그릴 정점 흐름은 LOD 0 뒤에 낮은 단계다(배치가 단계의 범위로 그린다).
        const RHIMeshVertexFormat format          = _owner.getVertexFormat();
        const uint32              drawVertexCount = _owner.getDrawVertexCount();
        const uint32              lod0Count       = static_cast<uint32>( listVertex.size() );
        vector<uint8>             listStreamByte( RHIMeshVertexStreams::computeBufferBytes( format, drawVertexCount ) );
        RHIMeshVertexStreams::write( format, listVertex.data(), lod0Count, 0, drawVertexCount, listStreamByte.data() );
        RHIMeshVertexStreams::write( format, listLODVertex.data(), drawVertexCount - lod0Count, lod0Count, drawVertexCount, listStreamByte.data() );
        const uint32          indexBytes   = _owner.getDrawIndexCount() * static_cast<uint32>( sizeof( uint32 ) );
        const RHIBufferHandle vertexBuffer = pResource->createVertexBuffer( listStreamByte.data(), static_cast<uint32>( listStreamByte.size() ) );
        const RHIBufferHandle indexBuffer  = indexBytes > 0 ? pResource->createIndexBuffer( pDrawIndex, indexBytes ) : 0;
        if ( vertexBuffer == 0 || indexBuffer == 0 )
        {
            SW_LOG_ERROR_ID( "Mesh.CreateVertexBufferFailedVerts", "createVertexBuffer / createIndexBuffer failed (%# verts, %# indices)", listVertex.size(), listIndex.size() );
            if ( vertexBuffer != 0 )
                pResource->destroyBuffer( vertexBuffer );
            if ( indexBuffer != 0 )
                pResource->destroyBuffer( indexBuffer );
            return false;
        }
        _vertex.attach( pDevice, vertexBuffer );
        _index.attach( pDevice, indexBuffer );
        return true;
    }

    void MeshRenderData::releaseRHI( IRHIDevice* pDevice )
    {
        // 디바이스가 죽기 **전에** 오는 통보다. 정상적으로 반환한다. 다른 디바이스의 것이면 내 것이 아니다.
        if ( pDevice == nullptr || _vertex._pDevice != pDevice )
            return;
        releaseBuffers(); // 인덱스 버퍼는 정점 버퍼와 같은 디바이스의 것이다(함께 만든다)
    }

    void MeshRenderData::clearRHI( IRHIDevice* pDevice )
    {
        // 다른 디바이스가 죽었다는 통보다. 내 버퍼는 멀쩡하다.
        if ( _vertex._pDevice != pDevice )
            return;
        // 디바이스가 이미 없다. 버퍼는 그와 함께 갔다. destroy 하면 해제 후 사용이다.
        _vertex.clear();
        _index.clear();
    }

    void MeshRenderData::releaseBuffers()
    {
        // 소멸자에서도 불린다. 그 시점에 디바이스가 이미 죽었다면 통보가 먼저 와서 여기를 비워 놓았으므로,
        // getLiveDevice() 는 널을 반환하고 반환 호출로 들어가지 않는다.
        // 마지막 소유는 게임 스레드가 아무 때나 놓는다(지형 LOD 교체, 씬 교체). 렌더 스레드가 프레임을 기록하는 동안이면 반환을 그 프레임 뒤로
        // 미룬다. 팩터리를 직접 부르면 기록이 읽는 백엔드 테이블(DX11 버퍼 SRV, 기록 상태에 바인딩된 정점 버퍼)을 기록과 동시에 바꾸게 된다.
        if ( IRHIDevice* pLiveDevice = _vertex.getLiveDevice() )
            pLiveDevice->releaseHandle( RHIHandleKind::Buffer, _vertex._buffer );
        if ( IRHIDevice* pLiveDevice = _index.getLiveDevice() )
            pLiveDevice->releaseHandle( RHIHandleKind::Buffer, _index._buffer );
        _vertex.clear();
        _index.clear();
    }
} // namespace sw
