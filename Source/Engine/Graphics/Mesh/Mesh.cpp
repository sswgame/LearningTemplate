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
        releaseGpu();
    }

    shared_ptr<Mesh> Mesh::createUnitCube()
    {
        auto              mesh     = sw::make_shared<Mesh>();
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
        auto              mesh     = sw::make_shared<Mesh>();
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
        releaseGpu();
        _listVertex = listVertex;
    }

    void Mesh::setVertices( vector<RHIVertex>&& listVertex )
    {
        releaseGpu();
        _listVertex = std::move( listVertex );
    }

    bool Mesh::upload( IRHIDevice* pDevice )
    {
        // GPU 버퍼 생성은 RHI 컨텍스트 스레드에서 한다 (메인 인라인 submit 또는 RenderThread).
        // TaskManager 워커에는 그래픽스 컨텍스트가 없다.
        if ( engine::areEngineServicesBound() )
            SW_ASSERT( engine::getTaskManager().isWorkerThread() == false );

        if ( pDevice == nullptr || _listVertex.empty() )
            return false;
        if ( _vertexBuffer != 0 && _pUploadDevice == pDevice )
            return true;

        // 이전 디바이스가 이미 shutdown된 경우 raw 포인터로 destroy하면 UAF.
        // GPU 버퍼는 디바이스 shutdownInternal 이 소유 해제한다.
        if ( _pUploadDevice == pDevice )
            releaseGpu();
        else
        {
            _vertexBuffer  = 0;
            _pUploadDevice = nullptr;
        }

        const uint32  bytes     = static_cast<uint32>( _listVertex.size() * sizeof( RHIVertex ) );
        IRHIResource* pResource = pDevice->getResource();
        if ( pResource == nullptr )
            return false;
        _vertexBuffer = pResource->createVertexBuffer( _listVertex.data(), bytes );
        if ( _vertexBuffer == 0 )
        {
            SW_LOG_ERROR( "createVertexBuffer failed (%# verts)", _listVertex.size() );
            return false;
        }
        _pUploadDevice          = pDevice;
        _uploadDeviceGeneration = RHI::getDeviceGeneration();
        return true;
    }

    void Mesh::releaseGpu()
    {
        // 소멸자에서도 불린다. 그 시점엔 디바이스가 이미 죽어 있을 수 있고, `_pUploadDevice` 는
        // 생 포인터라 살아 있는지 스스로 알 수 없다 — 세대가 그걸 알려준다.
        // (upload() 는 예전부터 이 함정을 알고 피했지만 소멸자 경로는 그대로였다. 앱에 메시가
        //  올라간 적이 없어서 드러나지 않았을 뿐이다.)
        const bool bDeviceAlive = ( _pUploadDevice != nullptr ) && ( _uploadDeviceGeneration == RHI::getDeviceGeneration() );
        if ( _vertexBuffer != 0 && bDeviceAlive )
        {
            IRHIResource* pResource = _pUploadDevice->getResource();
            if ( pResource != nullptr )
                pResource->destroyBuffer( _vertexBuffer );
        }
        _vertexBuffer           = 0;
        _pUploadDevice          = nullptr;
        _uploadDeviceGeneration = 0;
    }
} // namespace sw
