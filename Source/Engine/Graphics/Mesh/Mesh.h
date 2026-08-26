/**
 * @file Mesh.h
 * @brief CPU 메시 데이터와 GPU 버텍스 버퍼 업로드 (씬 지오메트리, RHI 데모 아님).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
	class IRHIDevice;

	/**
	 * @class Mesh
	 * @brief 삼각형 리스트 메시 (POSITION+COLOR). upload() 후 선택적 GPU VB를 소유합니다.
	 */
	class SW_API Mesh
	{
	public:
		/** @brief 빈 메시입니다. */
		Mesh() = default;
		/**
		 * @brief 핸들만 비웁니다.
		 * @note createUnitCube static 캐시는 RHI 디바이스보다 늦게 파괴될 수 있어
		 *       여기서는 디바이스 경유 destroy를 하지 않습니다. 명시적 해제는 releaseGpu().
		 */
		~Mesh();

		/** @brief 복사를 금지합니다. */
		Mesh( const Mesh& ) = delete;
		/** @brief 대입을 금지합니다. */
		Mesh& operator=( const Mesh& ) = delete;

		/** @brief 원점 중심 단위 큐브(범위 [-0.5,0.5], 면별 색)를 공유 생성합니다. */
		static shared_ptr<Mesh> createUnitCube();

		/** @brief 원점 중심 단위 2D 쿼드(범위 [-0.5,0.5])를 공유 생성합니다. */
		static shared_ptr<Mesh> createRectMesh();
		/**
		 * @brief 프리미티브 id로 내장 메시를 반환합니다.
		 * @details 비어 있거나 "Cube"면 단위 큐브, "Quad"/"Rect"면 쿼드. 모르면 nullptr.
		 */
		static shared_ptr<Mesh> createPrimitive( string_view meshId );

		/** @brief CPU 정점 배열을 설정합니다. */
		void setVertices( const vector<RHIVertex>& vertices );
		void setVertices( vector<RHIVertex>&& vertices );
		/** @brief CPU 정점 배열을 반환합니다. */
		const vector<RHIVertex>& getVertices() const { return _listVertices; }
		/** @brief 정점 개수를 반환합니다. */
		uint32 getVertexCount() const { return static_cast<uint32>( _listVertices.size() ); }

		/** @brief 디바이스에 업로드(또는 재업로드)합니다. 같은 디바이스면 멱등입니다. */
		bool upload( IRHIDevice* pDevice );
		/** @brief GPU 버텍스 버퍼를 해제합니다. 디바이스가 살아 있을 때 호출하세요. */
		void releaseGpu();

		/** @brief GPU 버텍스 버퍼 핸들을 반환합니다. */
		RHIBufferHandle getVertexBuffer() const { return _vertexBuffer; }
		/** @brief GPU에 올라갔는지 반환합니다. */
		bool isUploaded() const { return _vertexBuffer != 0; }

	private:
		vector<RHIVertex> _listVertices;
		RHIBufferHandle	  _vertexBuffer{ 0 };
		IRHIDevice*		  _pUploadDevice{ nullptr };
	};
} // namespace sw
