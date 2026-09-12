#include "pch.h"

#include "Engine/Graphics/Mesh/Mesh.h"

#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RHI/RHI.h"

namespace sw
{
    SW_LOG_CALLER( "Mesh" );

    Mesh::~Mesh()
    {
        releaseVertexBuffer();
    }

    shared_ptr<Mesh> Mesh::createUnitCube()
    {
        auto              mesh     = sw::make_shared<Mesh>( CreateKey{} );
        vector<RHIVertex> listVert = {
            // +Z
            { { -0.5f, -0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            {  { 0.5f, -0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            {   { 0.5f, 0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            { { -0.5f, -0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            {   { 0.5f, 0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            {  { -0.5f, 0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            // -Z
            { { 0.5f, -0.5f, -0.5f }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            {{ -0.5f, -0.5f, -0.5f }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            { { -0.5f, 0.5f, -0.5f }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            { { 0.5f, -0.5f, -0.5f }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            { { -0.5f, 0.5f, -0.5f }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            {  { 0.5f, 0.5f, -0.5f }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            // +X
            {  { 0.5f, -0.5f, 0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            { { 0.5f, -0.5f, -0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            {  { 0.5f, 0.5f, -0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            {  { 0.5f, -0.5f, 0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            {  { 0.5f, 0.5f, -0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            {   { 0.5f, 0.5f, 0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            // -X
            {{ -0.5f, -0.5f, -0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            { { -0.5f, -0.5f, 0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            {  { -0.5f, 0.5f, 0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            {{ -0.5f, -0.5f, -0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            {  { -0.5f, 0.5f, 0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            { { -0.5f, 0.5f, -0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            // +Y
            {  { -0.5f, 0.5f, 0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            {   { 0.5f, 0.5f, 0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            {  { 0.5f, 0.5f, -0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            {  { -0.5f, 0.5f, 0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            {  { 0.5f, 0.5f, -0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            { { -0.5f, 0.5f, -0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            // -Y
            {{ -0.5f, -0.5f, -0.5f }, { 0.45f, 0.45f, 0.50f, 1.0f }},
            { { 0.5f, -0.5f, -0.5f }, { 0.45f, 0.45f, 0.50f, 1.0f }},
            {  { 0.5f, -0.5f, 0.5f }, { 0.45f, 0.45f, 0.50f, 1.0f }},
            {{ -0.5f, -0.5f, -0.5f }, { 0.45f, 0.45f, 0.50f, 1.0f }},
            {  { 0.5f, -0.5f, 0.5f }, { 0.45f, 0.45f, 0.50f, 1.0f }},
            { { -0.5f, -0.5f, 0.5f }, { 0.45f, 0.45f, 0.50f, 1.0f }},
        };
        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }

    shared_ptr<Mesh> Mesh::createPrimitive( string_view meshId )
    {
        if ( meshId.empty() || StringUtil::equals( meshId, "Cube", true ) )
            return createUnitCube();
        if ( StringUtil::equals( meshId, "Quad", true ) || StringUtil::equals( meshId, "Rect", true ) )
            return createRectMesh();
        return {};
    }

    shared_ptr<Mesh> Mesh::createRectMesh()
    {
        auto              mesh     = sw::make_shared<Mesh>( CreateKey{} );
        vector<RHIVertex> listVert = {
            {{ -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            { { 0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            {  { 0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            {{ -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            {  { 0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            { { -0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
        };
        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }

    void Mesh::setVertices( const vector<RHIVertex>& listVertex )
    {
        releaseVertexBuffer();
        _listVertex = listVertex;
    }

    void Mesh::setVertices( vector<RHIVertex>&& listVertex )
    {
        releaseVertexBuffer();
        _listVertex = std::move( listVertex );
    }

    bool Mesh::initRhi( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || _listVertex.empty() )
            return false;

        // 워커에서 만들어도 되는지는 **백엔드가 말한다**. DX12 · DX11 · Vulkan 은 버퍼 생성이 디바이스 레벨이고
        // 핸들 테이블도 잠겨 있어 안전하다. OpenGL 은 glGen* 이 현재 컨텍스트를 필요로 해서 안 된다 — 그 백엔드에서
        // 워커가 여기 들어왔다면 부른 쪽이 틀린 것이다(GpuUploadQueue 는 그 경우 인라인으로 돈다).
        if ( engine::areEngineServicesBound() && pDevice->getCapabilities()._bThreadSafeResourceCreation == 0 )
            SW_ASSERT( engine::getTaskManager().isWorkerThread() == false );
        // 이미 올라가 있으면 그대로 둔다. 옛 디바이스가 죽었다면 통보(`releaseRhi` · `forgetRhi`)가 먼저 와서
        // 여기를 비워 놓았으므로, 값이 남아 있다는 것은 곧 살아 있는 디바이스의 것이라는 뜻이다.
        if ( _vertex.isResident() )
            return true;
        _vertex.forget();

        const uint32  bytes     = static_cast<uint32>( _listVertex.size() * sizeof( RHIVertex ) );
        IRHIResource* pResource = pDevice->getResource();
        if ( pResource == nullptr )
            return false;
        const RHIBufferHandle vertexBuffer = pResource->createVertexBuffer( _listVertex.data(), bytes );
        if ( vertexBuffer == 0 )
        {
            SW_LOG_ERROR( "createVertexBuffer failed (%# verts)", _listVertex.size() );
            return false;
        }
        _vertex.adopt( pDevice, vertexBuffer );
        return true;
    }

    void Mesh::releaseRhi( IRHIDevice* pDevice )
    {
        // 디바이스가 죽기 **전에** 오는 통보다 — 제대로 돌려준다. 남의 디바이스 것이면 내 것이 아니다.
        if ( pDevice == nullptr || _vertex._pDevice != pDevice )
            return;
        releaseVertexBuffer();
    }

    void Mesh::forgetRhi( IRHIDevice* pDevice )
    {
        // 남의 디바이스가 죽었다는 통보다 — 내 버퍼는 멀쩡하다.
        if ( _vertex._pDevice != pDevice )
            return;
        // 디바이스가 이미 없다 — 버퍼는 그와 함께 갔다. destroy 하면 해제 후 사용이다.
        _vertex.forget();
    }

    void Mesh::releaseVertexBuffer()
    {
        // 소멸자에서도 불린다. 그 시점에 디바이스가 이미 죽었다면 통보가 먼저 와서 여기를 비워 놓았으므로,
        // getLiveDevice() 는 널을 돌려주고 destroy 로 뛰어들지 않는다.
        if ( IRHIDevice* pLiveDevice = _vertex.getLiveDevice() )
        {
            IRHIResource* pResource = pLiveDevice->getResource();
            if ( pResource != nullptr )
                pResource->destroyBuffer( _vertex._buffer );
        }
        _vertex.forget();
    }
} // namespace sw
