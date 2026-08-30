#include "pch.h"

#include "Engine/Graphics/Mesh/Mesh.h"

#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"

namespace sw
{
	SW_LOG_CALLER( "Mesh" );

	Mesh::~Mesh()
	{
		_vertexBuffer  = 0;
		_pUploadDevice = nullptr;
	}

	shared_ptr<Mesh> Mesh::createUnitCube()
	{
		auto			  mesh		= sw::make_shared<Mesh>();
		vector<RHIVertex> listVerts = {
			// +Z
			{ { -0.5f, -0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
			{  { 0.5f, -0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
			{	  { 0.5f, 0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
			{ { -0.5f, -0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
			{	  { 0.5f, 0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
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
			{	  { 0.5f, 0.5f, 0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
			// -X
			{{ -0.5f, -0.5f, -0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
			{ { -0.5f, -0.5f, 0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
			{  { -0.5f, 0.5f, 0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
			{{ -0.5f, -0.5f, -0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
			{  { -0.5f, 0.5f, 0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
			{ { -0.5f, 0.5f, -0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
			// +Y
			{  { -0.5f, 0.5f, 0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
			{	  { 0.5f, 0.5f, 0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
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
		mesh->setVertices( std::move( listVerts ) );
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
		auto			  mesh		= sw::make_shared<Mesh>();
		vector<RHIVertex> listVerts = {
			{{ -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{ { 0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{  { 0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{{ -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{  { 0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{ { -0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
		};
		mesh->setVertices( std::move( listVerts ) );
		return mesh;
	}

	void Mesh::setVertices( const vector<RHIVertex>& vertices )
	{
		releaseGpu();
		_listVertex = vertices;
	}

	void Mesh::setVertices( vector<RHIVertex>&& vertices )
	{
		releaseGpu();
		_listVertex = std::move( vertices );
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

		const uint32  bytes		= static_cast<uint32>( _listVertex.size() * sizeof( RHIVertex ) );
		IRHIResource* pResource = pDevice->getResource();
		if ( pResource == nullptr )
			return false;
		_vertexBuffer = pResource->createVertexBuffer( _listVertex.data(), bytes );
		if ( _vertexBuffer == 0 )
		{
			SW_LOG_ERROR( "createVertexBuffer failed (%# verts)", _listVertex.size() );
			return false;
		}
		_pUploadDevice = pDevice;
		return true;
	}

	void Mesh::releaseGpu()
	{
		if ( _vertexBuffer != 0 && _pUploadDevice != nullptr )
		{
			IRHIResource* pResource = _pUploadDevice->getResource();
			if ( pResource != nullptr )
				pResource->destroyBuffer( _vertexBuffer );
		}
		_vertexBuffer  = 0;
		_pUploadDevice = nullptr;
	}
} // namespace sw
