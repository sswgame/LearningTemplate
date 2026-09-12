/**
 * @file Mesh.h
 * @brief CPU 메시 데이터와 GPU 버텍스 버퍼 업로드 (씬 지오메트리, RHI 데모 아님).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIResidentBuffer.h"
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
        /**
         * @brief 생성 열쇠 — create() 만 만들 수 있다.
         * @details 생성자가 이 열쇠를 요구하므로 `make_shared<Mesh>()` 도 스택의 `Mesh x;` 도 **컴파일되지 않는다.**
         *          모든 Mesh 이 Engine 안에서 shared_ptr 로 태어난다는 것을 컴파일러가 보장한다 — 렌더 패킷이
         *          소유를 빌릴 수 있고, 제어 블록이 모듈 DLL 에 사는 일이 없다. 린트가 아니라 타입이 지킨다.
         */
        struct CreateKey
        {
        private:
            CreateKey() = default;
            friend class Mesh;
        };
        /** @brief create*() 전용 생성자 — 빈 메시. */
        explicit Mesh( CreateKey ) noexcept {}
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
        void setVertices( const vector<RHIVertex>& listVertex );
        void setVertices( vector<RHIVertex>&& listVertex );
        /** @brief 정점 개수를 반환합니다. */
        uint32 getVertexCount() const { return static_cast<uint32>( _listVertex.size() ); }

        /** @brief 디바이스에 업로드(또는 재업로드)합니다. 같은 디바이스면 멱등입니다. */
        bool upload( IRHIDevice* pDevice );
        /** @brief GPU 버텍스 버퍼를 해제합니다. 디바이스가 살아 있을 때 호출하세요. */
        void releaseGpu();

        /** @brief GPU 버텍스 버퍼 핸들을 반환합니다. */
        RHIBufferHandle getVertexBuffer() const { return _vertex._buffer; }
        /** @brief GPU에 올라갔는지 반환합니다. */
        bool isUploaded() const { return _vertex._buffer != 0; }

    private:
        vector<RHIVertex> _listVertex;
        /** @brief 정점 버퍼 — 어느 디바이스의 것인지를 세대로 안다 (RHIResidentBuffer). */
        RHIResidentBuffer _vertex;
    };
} // namespace sw
