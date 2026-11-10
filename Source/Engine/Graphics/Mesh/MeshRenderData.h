/**
 * @file MeshRenderData.h
 * @brief 메시 한 벌의 GPU 정점 · 인덱스 버퍼(렌더 쪽 프록시)입니다. 렌더러 · 업로드 큐만 include 한다 — 게임이 보는 `Mesh.h` 는 CPU 데이터만 든다.
 * @details 언리얼 `UStaticMesh` ↔ `FStaticMeshRenderData` 와 같은 갈림이다. 수명은 메시에 묶인다 — 메시가 만들 때 함께 만들고 소멸할 때 함께 놓으며,
 *          정점이 바뀌면(`Mesh::setGeometry` · `setLODs`) 메시가 `releaseBuffers` 로 버퍼를 놓아 다음 `initRHI` 가 새 내용을 올린다.
 */
#pragma once
#include "Core/Foundation/Macros.h"

#include "Engine/Graphics/RHI/RHIDeviceBuffer.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class Mesh;

    /**
     * @class MeshRenderData
     * @brief `Mesh` 의 GPU 버퍼입니다. `Mesh::getRenderData` 로 얻는다. 디바이스 수명 통보(`RHIRenderResource`)를 받는다.
     */
    class SW_API MeshRenderData final : public RHIRenderResource
    {
    public:
        /** @brief 메시가 만들 때 부릅니다. @p owner 는 이 객체보다 오래 산다(메시가 소유한다). */
        explicit MeshRenderData( const Mesh& owner ) noexcept
            : _owner{ owner }
        {
        }
        /**
         * @brief 정점 버퍼를 해제합니다. 디바이스가 살아 있으면 반환하고, 이미 없으면 핸들만 버립니다.
         * @note 디바이스가 먼저 죽었다면 통보(`releaseRHI`, `clearRHI`)가 먼저 와서 핸들을 비워 두므로,
         *       소멸 순서와 상관없이 죽은 디바이스로 destroy 하지 않습니다.
         */
        ~MeshRenderData() override;

        /** @brief (RHIRenderResource) 살아 있는 디바이스에 정점 버퍼를 반환합니다. */
        void releaseRHI( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 부릅니다. 핸들만 버립니다. */
        void clearRHI( IRHIDevice* pDevice ) override;
        /** @brief 메시의 CPU 정점을 디바이스에 업로드(또는 재업로드)합니다. 같은 디바이스면 여러 번 불러도 결과가 같습니다. */
        [[nodiscard]] bool initRHI( IRHIDevice* pDevice ) override;

        /** @brief 정점 · 인덱스 버퍼를 실제로 해제합니다. 살아 있는 디바이스면 반환하고, 아니면 버립니다. 메시 내용이 바뀔 때 메시가 부릅니다. */
        void releaseBuffers();

        /** @brief GPU 정점 버퍼 핸들을 반환합니다(그릴 정점 흐름 — LOD 0 뒤에 낮은 단계). */
        RHIBufferHandle getVertexBuffer() const { return _vertex._buffer; }
        /** @brief GPU 인덱스 버퍼(uint32) 핸들을 반환합니다(그릴 인덱스 흐름 — LOD 0 뒤에 낮은 단계, 값은 정점 흐름 번호). */
        RHIBufferHandle getIndexBuffer() const { return _index._buffer; }
        /**
         * @brief 살아 있는 디바이스에 올라가 있는지 반환합니다.
         * @details 옛 디바이스가 죽으면 통보(`RHIRenderResource`)가 먼저 와서 핸들을 비운다. 그래서 값이 남아
         *          있다는 것만으로 "살아 있는 디바이스의 것" 임이 보장된다(업로드 큐는 백엔드 교체 뒤 다시 올릴 메시를 이 값으로 고른다).
         */
        bool isRHIValid() const { return _vertex.isResident() && _index.isResident(); }

    private:
        const Mesh& _owner;
        /** @brief 정점 · 인덱스 버퍼입니다. 어느 디바이스의 것인지를 함께 가지고 있습니다(RHIDeviceBuffer). 둘은 함께 만들고 함께 놓는다. */
        RHIDeviceBuffer _vertex;
        RHIDeviceBuffer _index;
    };
} // namespace sw
