#include "pch.h"

#include "Engine/Graphics/Mesh/Mesh.h"

#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"

namespace sw
{
	Mesh::~Mesh()
	{
		// createUnitCube/createRectMesh static 캐시는 프로세스 종료 시 RHI 디바이스보다 늦게
		// 파괴될 수 있다. 디바이스 경유 destroy는 하지 않고 핸들만 비운다.
		_vertexBuffer  = 0;
		_pUploadDevice = nullptr;
	}

	shared_ptr<Mesh> Mesh::createUnitCube()
	{
		static shared_ptr<Mesh> s_cube;
		if ( s_cube != nullptr )
			return s_cube;

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
		s_cube = std::move( mesh );
		return s_cube;
	}

	shared_ptr<Mesh> Mesh::createPrimitive( string_view meshId )
	{
		if ( meshId.empty() || StringUtil::equalsIgnoreCase( meshId, "Cube" ) )
			return createUnitCube();
		if ( StringUtil::equalsIgnoreCase( meshId, "Quad" ) || StringUtil::equalsIgnoreCase( meshId, "Rect" ) )
			return createRectMesh();
		return {};
	}

	shared_ptr<Mesh> Mesh::createRectMesh()
	{
		static shared_ptr<Mesh> s_rect;
		if ( s_rect != nullptr )
			return s_rect;

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
		s_rect = std::move( mesh );
		return s_rect;
	}

	void Mesh::setVertices( const vector<RHIVertex>& vertices )
	{
		releaseGpu();
		_listVertices = vertices;
	}

	void Mesh::setVertices( vector<RHIVertex>&& vertices )
	{
		releaseGpu();
		_listVertices = std::move( vertices );
	}

	bool Mesh::upload( IRHIDevice* pDevice )
	{
		// GPU 버퍼 생성은 RHI 컨텍스트 스레드에서 한다 (메인 인라인 submit 또는 RenderThread).
		// TaskManager 워커에는 그래픽스 컨텍스트가 없다.
		if ( engine::areEngineServicesBound() )
			SW_ASSERT( engine::getTaskManager().isWorkerThread() == false );

		if ( pDevice == nullptr || _listVertices.empty() )
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

		const uint32  bytes		= static_cast<uint32>( _listVertices.size() * sizeof( RHIVertex ) );
		IRHIResource* pResource = pDevice->getResource();
		if ( pResource == nullptr )
			return false;
		_vertexBuffer = pResource->createVertexBuffer( _listVertices.data(), bytes );
		if ( _vertexBuffer == 0 )
		{
			SW_LOG_ERROR( "[Mesh] createVertexBuffer failed (%# verts)", _listVertices.size() );
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
