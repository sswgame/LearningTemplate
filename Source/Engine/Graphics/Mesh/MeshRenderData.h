/**
 * @file MeshRenderData.h
 * @brief 메시 한 벌의 GPU 정점 · 인덱스 버퍼(렌더 쪽 프록시)입니다. 렌더러 · 업로드 큐만 include 한다 — 게임이 보는 `Mesh.h` 는 CPU 데이터만 든다.
 * @details 언리얼 `UStaticMesh` ↔ `FStaticMeshRenderData` 와 같은 갈림이다. 수명은 메시에 묶인다 — 메시가 만들 때 함께 만들고 소멸할 때 함께 놓으며,
 *          정점이 바뀌면(`Mesh::setGeometry` · `setLODs`) 메시가 `releaseBuffers` 로 버퍼를 놓아 다음 `initRHI` 가 새 내용을 올린다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @struct RHIDeviceBuffer
     * @brief 만들어 준 디바이스를 함께 보관하는 GPU 버퍼 핸들입니다. 디바이스가 죽기 전에 통보가 와서 비워지므로, 값이 남아 있으면 유효합니다. 복사하지 않습니다(두 번 놓는다).
     */
    struct RHIDeviceBuffer
    {
        RHIBufferHandle _buffer{ 0 };        ///< 0 이면 초기화된 적이 없거나 잊은 상태입니다
        IRHIDevice*     _pDevice{ nullptr }; ///< 만들어 준 디바이스. 통보를 받으므로 죽은 뒤에 남아 있지 않습니다

        RHIDeviceBuffer()                                    = default;
        RHIDeviceBuffer( const RHIDeviceBuffer& )            = delete;
        RHIDeviceBuffer& operator=( const RHIDeviceBuffer& ) = delete;

        /** @brief 초기화되어 있으면 true 입니다. 디바이스가 죽으면 통보(`RHIRenderResource`)가 먼저 와서 여기를 비웁니다. */
        bool isResident() const { return _buffer != 0; }
        /** @brief 이 버퍼를 만들어 준 디바이스입니다. 통보 덕에 여기 남아 있다면 아직 살아 있습니다. */
        IRHIDevice* getLiveDevice() const { return isResident() ? _pDevice : nullptr; }
        /** @brief 방금 만든 핸들을 만들어 준 디바이스와 함께 보관합니다. */
        void attach( IRHIDevice* pDevice, RHIBufferHandle buffer )
        {
            _pDevice = pDevice;
            _buffer  = buffer;
        }
        /** @brief 핸들을 잊습니다. 죽은 디바이스의 것을 destroy 하면 해제 후 사용이므로, 잊는 것이 옳을 때 씁니다. */
        void clear() { attach( nullptr, RHIBufferHandle{ 0 } ); }
    };
} // namespace sw

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
        explicit MeshRenderData( Mesh& owner ) noexcept
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
        /**
         * @brief 메시의 CPU 정점을 디바이스에 업로드(또는 재업로드)합니다. 같은 디바이스면 여러 번 불러도 결과가 같습니다.
         * @details 올린 뒤 `MeshCPUAccess::ReleaseAfterUpload` 메시의 CPU 정점을 놓는다. 놓은 뒤 다시 올려야 하면(장치 교체) 원본 파일을 다시 읽는다.
         */
        [[nodiscard]] bool initRHI( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 장치 교체의 워커 준비 — 버퍼가 없고 CPU 정점을 놓은 메시의 원본 파일을 다시 읽습니다. */
        void prepareRHI() override;

        /** @brief 정점 · 인덱스 버퍼를 실제로 해제합니다. 살아 있는 디바이스면 반환하고, 아니면 버립니다. 메시 내용이 바뀔 때 메시가 부릅니다. */
        void releaseBuffers();

        /** @brief GPU 정점 버퍼 핸들을 반환합니다(그릴 정점 흐름 — LOD 0 뒤에 낮은 단계). */
        RHIBufferHandle getVertexBuffer() const { return _vertex._buffer; }
        /** @brief GPU 인덱스 버퍼 핸들을 반환합니다(그릴 인덱스 흐름 — LOD 0 뒤에 낮은 단계, 값은 정점 흐름 번호). 한 칸의 바이트는 `getIndexStride` 다. */
        RHIBufferHandle getIndexBuffer() const { return _index._buffer; }
        /** @brief 올린 인덱스 버퍼 한 칸의 바이트입니다(그릴 정점이 65536 개 이하면 2, 아니면 4 — `MeshAssetFormat::getIndexBytes`, R-15). 거는 쪽은 이 값으로 건다. */
        uint32 getIndexStride() const { return _indexStride; }
        /**
         * @brief 살아 있는 디바이스에 올라가 있는지 반환합니다.
         * @details 옛 디바이스가 죽으면 통보(`RHIRenderResource`)가 먼저 와서 핸들을 비운다. 그래서 값이 남아
         *          있다는 것만으로 "살아 있는 디바이스의 것" 임이 보장된다(업로드 큐는 백엔드 교체 뒤 다시 올릴 메시를 이 값으로 고른다).
         */
        bool isRHIValid() const { return _vertex.isResident() && _index.isResident(); }

    private:
        Mesh& _owner;
        /** @brief 정점 · 인덱스 버퍼입니다. 어느 디바이스의 것인지를 함께 가지고 있습니다(RHIDeviceBuffer). 둘은 함께 만들고 함께 놓는다. */
        RHIDeviceBuffer _vertex;
        RHIDeviceBuffer _index;
        uint32          _indexStride{ sizeof( uint32 ) }; ///< 올릴 때 정한 인덱스 폭(바이트)
    };
} // namespace sw
