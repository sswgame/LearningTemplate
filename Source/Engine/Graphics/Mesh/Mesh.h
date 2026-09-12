/**
 * @file Mesh.h
 * @brief CPU 메시 데이터와 GPU 버텍스 버퍼 업로드 (씬 지오메트리, RHI 데모 아님).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/RHIResidentBuffer.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class Mesh
     * @brief 삼각형 리스트 메시 (POSITION+COLOR). upload() 후 선택적 GPU VB를 소유합니다.
     */
    class SW_API Mesh final : public RHIRenderResource
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
        /** @brief create() 전용 생성자 — 빈 메시. */
        explicit Mesh( CreateKey ) noexcept {}
        /**
         * @brief 핸들만 비웁니다.
         * @note createUnitCube static 캐시는 RHI 디바이스보다 늦게 파괴될 수 있어
         *       여기서는 디바이스 경유 destroy를 하지 않습니다. 해제는 디바이스 통보(releaseRhi)가 맡습니다.
         */
        ~Mesh() override;

        /** @brief (RHIRenderResource) 살아 있는 디바이스에 정점 버퍼를 돌려줍니다. */
        void releaseRhi( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 — 핸들만 잊습니다. */
        void forgetRhi( IRHIDevice* pDevice ) override;

        /** @brief 복사를 금지합니다. */
        Mesh( const Mesh& ) = delete;
        /** @brief 대입을 금지합니다. */
        Mesh& operator=( const Mesh& ) = delete;

        /**
         * @brief 빈 메시를 Engine.dll 안에서 shared_ptr 로 만듭니다.
         * @details 도형을 만드는 것은 이 클래스의 일이 아니다 — `MeshUtil` 이 이걸로 만들어 정점을 채운다.
         *          Mesh 가 책임지는 것은 **정점 버퍼와 그 수명**뿐이다.
         */
        static shared_ptr<Mesh> create();

        /** @brief CPU 정점 배열을 설정합니다. */
        void setVertices( const vector<RHIVertex>& listVertex );
        void setVertices( vector<RHIVertex>&& listVertex );
        /** @brief CPU 정점 배열입니다. `setVertices` 의 짝 — GPU 업로드 뒤에도 원본은 여기 남습니다. */
        const vector<RHIVertex>& getVertices() const { return _listVertex; }

        /**
         * @brief 이 메시의 정점을 GPU 가 변형해도 되는지 표시합니다(기본 꺼짐).
         * @details 유니티의 `Mesh.vertexBufferTarget |= Raw` 옵트인과 같은 자리다 — 켠 메시만 모프 풀에
         *          들어간다. 전부 넣으면 풀이 쓸데없이 커지고, 예산을 넘긴 메시는 어차피 레스트로 그려진다.
         * @note **GPU 쪽 변형은 CPU 사본(`getVertices`)에 반영되지 않는다.** 유니티 문서도 같은 주의를 준다 —
         *       CPU 는 레스트 포즈만 안다(피킹·바운드는 그 값을 본다).
         */
        void setGpuMorphEnabled( bool bEnabled ) { _bGpuMorph = bEnabled ? SW_TRUE : SW_FALSE; }
        /** @brief GPU 모프를 요청했는가. */
        bool isGpuMorphEnabled() const { return _bGpuMorph != SW_FALSE; }
        /** @brief 정점 개수를 반환합니다. */
        uint32 getVertexCount() const { return static_cast<uint32>( _listVertex.size() ); }

        /** @brief 디바이스에 업로드(또는 재업로드)합니다. 같은 디바이스면 멱등입니다. */
        bool initRhi( IRHIDevice* pDevice ) override;

        /** @brief GPU 버텍스 버퍼 핸들을 반환합니다. */
        RHIBufferHandle getVertexBuffer() const { return _vertex._buffer; }
        /**
         * @brief 살아 있는 디바이스에 올라가 있는지 반환합니다.
         * @details 옛 디바이스가 죽으면 통보(`RHIRenderResource`)가 먼저 와서 핸들을 비운다 — 그래서 값이 남아
         *          있다는 것만으로 "살아 있는 디바이스의 것" 임이 보장된다. 예전에는 그 통보가 없어서 교체 뒤에도
         *          옛 핸들이 "올라갔다" 고 답했고, 업로드 큐가 아무것도 다시 올리지 않았다.
         */
        bool isRhiValid() const { return _vertex.isResident(); }

    private:
        /** @brief 정점 버퍼를 실제로 놓습니다. 살아 있는 디바이스면 돌려주고, 아니면 잊습니다. */
        void releaseVertexBuffer();

        vector<RHIVertex> _listVertex;
        /// @brief setGpuMorphEnabled 참고 — 이 메시가 모프 풀에 들어갈지.
        uint8 _bGpuMorph{ SW_FALSE };
        /** @brief 정점 버퍼 — 어느 디바이스의 것인지를 함께 든다 (RHIResidentBuffer). */
        RHIResidentBuffer _vertex;
    };
} // namespace sw
