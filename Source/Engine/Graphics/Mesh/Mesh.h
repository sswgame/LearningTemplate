/**
 * @file Mesh.h
 * @brief CPU 메시 데이터와 GPU 정점 버퍼 업로드입니다(씬 지오메트리용이고 RHI 데모가 아닙니다).
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
     * @brief 삼각형 리스트 메시입니다(정점 = 위치 · 노멀 · UV · 색). initRhi() 로 올린 GPU 정점 버퍼를 소유합니다.
     */
    class SW_API Mesh final : public RHIRenderResource
    {
    public:
        /**
         * @brief 생성 열쇠입니다. create() 만 만들 수 있습니다.
         * @details 생성자가 이 열쇠를 요구하므로 `make_shared<Mesh>()` 도 스택의 `Mesh x;` 도 **컴파일되지 않습니다.**
         *          모든 Mesh 가 Engine 안에서 shared_ptr 로 태어난다는 것을 컴파일러가 보장합니다. 렌더 패킷이
         *          소유를 빌릴 수 있고, 제어 블록이 모듈 DLL 에 사는 일이 없습니다. 린트가 아니라 타입이 지킵니다.
         */
        struct CreateKey
        {
        private:
            CreateKey() = default;
            friend class Mesh;
        };
        /** @brief create() 전용 생성자입니다. 빈 메시로 만듭니다. */
        explicit Mesh( CreateKey ) noexcept {}
        /**
         * @brief 정점 버퍼를 놓습니다. 디바이스가 살아 있으면 돌려주고, 이미 없으면 핸들만 잊습니다.
         * @note 디바이스가 먼저 죽었다면 통보(`releaseRhi` · `forgetRhi`)가 먼저 와서 핸들을 비워 두므로,
         *       소멸 순서와 상관없이 죽은 디바이스로 destroy 하지 않습니다.
         */
        ~Mesh() override;

        /** @brief (RHIRenderResource) 살아 있는 디바이스에 정점 버퍼를 돌려줍니다. */
        void releaseRhi( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 부릅니다. 핸들만 잊습니다. */
        void forgetRhi( IRHIDevice* pDevice ) override;

        /** @brief 복사를 금지합니다. */
        Mesh( const Mesh& ) = delete;
        /** @brief 대입을 금지합니다. */
        Mesh& operator=( const Mesh& ) = delete;

        /**
         * @brief 빈 메시를 Engine.dll 안에서 shared_ptr 로 만듭니다.
         * @details 도형을 만드는 것은 이 클래스의 일이 아닙니다. `MeshUtil` 이 이것으로 만들어 정점을 채웁니다.
         *          Mesh 가 책임지는 것은 **정점 버퍼와 그 수명**뿐입니다.
         */
        static shared_ptr<Mesh> create();

        /** @brief CPU 정점 배열을 설정합니다. */
        void setVertices( const vector<RHIVertex>& listVertex );
        void setVertices( vector<RHIVertex>&& listVertex );
        /** @brief CPU 정점 배열입니다. `setVertices` 의 짝이고, GPU 업로드 뒤에도 원본은 여기 남습니다. */
        const vector<RHIVertex>& getVertices() const { return _listVertex; }

        /**
         * @brief 이 메시의 정점을 GPU 가 변형해도 되는지 표시합니다(기본 꺼짐).
         * @details 유니티의 `Mesh.vertexBufferTarget |= Raw` 옵트인과 같은 자리입니다. 켠 메시만 모프 풀에
         *          들어갑니다. 모두 넣으면 풀이 쓸데없이 커지고, 예산을 넘긴 메시는 어차피 레스트로 그려집니다.
         * @note **GPU 쪽 변형은 CPU 사본(`getVertices`)에 반영되지 않습니다.** 유니티 문서도 같은 주의를 줍니다.
         *       CPU 는 레스트 포즈만 압니다(피킹 · 바운드는 그 값을 봅니다).
         */
        void setGpuMorphEnabled( bool bEnabled ) { _bGpuMorph = bEnabled ? SW_TRUE : SW_FALSE; }
        /** @brief GPU 모프를 요청했는지 반환합니다. */
        bool isGpuMorphEnabled() const { return _bGpuMorph != SW_FALSE; }
        /** @brief 정점 개수를 반환합니다. */
        uint32 getVertexCount() const { return static_cast<uint32>( _listVertex.size() ); }
        /**
         * @brief 메시 공간 원점에서 가장 먼 정점까지의 거리입니다(원점 중심 경계 구의 반지름). `setVertices` 가 구합니다.
         * @details 컴포넌트의 경계(`MeshComponent::getBoundsRadius`)와 GPU 컬링이 이것을 덮는다. 고정 반지름(단위 상자의 0.866)을 쓰면
         *          그보다 큰 도형(캡슐 끝 · 평면)이 화면 가장자리에서 보이는데도 잘린다.
         */
        float32 getBoundingRadius() const { return _boundingRadius; }
        /** @brief 메시 공간의 축 정렬 경계(최소)입니다. 정점이 없으면 원점입니다. `setVertices` 가 구합니다. */
        const float3& getLocalBoundsMin() const { return _localBoundsMin; }
        /** @brief 메시 공간의 축 정렬 경계(최대)입니다. */
        const float3& getLocalBoundsMax() const { return _localBoundsMax; }
        /**
         * @brief 이 메시 **내용**의 프로세스 고유 번호입니다. 만들 때와 `setVertices` 때마다 새 번호를 받습니다.
         * @details 정점 · 모프 풀은 메시 집합이 그대로인지를 이것으로 봅니다. 포인터만 보면 메시가 지워진 자리에 새 메시가
         *          생기거나(할당기는 같은 크기의 자리를 곧바로 다시 준다) 같은 메시의 정점을 바꿀 때 "같은 집합" 으로 보여 옛 정점을 그립니다.
         */
        uint64 getContentId() const { return _contentId; }

        /** @brief 디바이스에 업로드(또는 재업로드)합니다. 같은 디바이스면 멱등입니다. */
        bool initRhi( IRHIDevice* pDevice ) override;

        /** @brief GPU 정점 버퍼 핸들을 반환합니다. */
        RHIBufferHandle getVertexBuffer() const { return _vertex._buffer; }
        /**
         * @brief 살아 있는 디바이스에 올라가 있는지 반환합니다.
         * @details 옛 디바이스가 죽으면 통보(`RHIRenderResource`)가 먼저 와서 핸들을 비웁니다. 그래서 값이 남아
         *          있다는 것만으로 "살아 있는 디바이스의 것" 임이 보장됩니다(업로드 큐가 백엔드 교체 뒤 다시 올릴 것을 이것으로 고릅니다).
         */
        bool isRhiValid() const { return _vertex.isResident(); }

    private:
        /** @brief 정점 버퍼를 실제로 놓습니다. 살아 있는 디바이스면 돌려주고, 아니면 잊습니다. */
        void releaseVertexBuffer();
        /** @brief 새 내용 번호를 냅니다(프로세스 전역, 0 은 쓰지 않습니다). */
        static uint64 allocateContentId();
        /** @brief 정점에서 경계 반지름을 다시 구합니다. */
        void refreshBoundingRadius();

        vector<RHIVertex> _listVertex;
        /// @brief getBoundingRadius 참고. setVertices 가 구합니다.
        float32 _boundingRadius{ 0.0f };
        float3  _localBoundsMin{};
        float3  _localBoundsMax{};
        /// @brief getContentId 참고. 만들 때와 setVertices 때 새로 받습니다.
        uint64 _contentId{ allocateContentId() };
        /// @brief setGpuMorphEnabled 참고. 이 메시가 모프 풀에 들어갈지 여부입니다.
        uint8 _bGpuMorph{ SW_FALSE };
        /** @brief 정점 버퍼입니다. 어느 디바이스의 것인지를 함께 듭니다(RHIResidentBuffer). */
        RHIResidentBuffer _vertex;
    };
} // namespace sw
