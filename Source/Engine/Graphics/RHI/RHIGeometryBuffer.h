/**
 * @file RHIGeometryBuffer.h
 * @brief 정점 · 인덱스 버퍼 하나의 소유자입니다(`RHIStructuredBuffer` 의 형제 — 뷰가 없다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @struct RHIGeometryBuffer
     * @brief 정점 · 인덱스 버퍼의 만들기와 놓기를 한 곳에 모읍니다. 다시 만들면 앞 버퍼를 놓습니다(지난 프레임이 읽는 동안의 반환은 디바이스가 미룬다). 옮기기만 됩니다.
     */
    struct SW_API RHIGeometryBuffer
    {
        RHIBufferHandle _buffer{ 0 };

        RHIGeometryBuffer() = default;
        RHIGeometryBuffer( RHIGeometryBuffer&& other ) noexcept { swap( other ); }
        RHIGeometryBuffer& operator=( RHIGeometryBuffer&& other ) noexcept
        {
            swap( other );
            return *this;
        }
        RHIGeometryBuffer( const RHIGeometryBuffer& )            = delete;
        RHIGeometryBuffer& operator=( const RHIGeometryBuffer& ) = delete;

        /** @brief 두 칸의 내용을 맞바꿉니다. 옮기기가 쓴다 — 대입한 쪽의 옛 내용은 @p other 에 남는다(임시면 잊는다). */
        void swap( RHIGeometryBuffer& other ) noexcept { std::swap( _buffer, other._buffer ); }
        /** @brief 버퍼가 만들어져 있는지 확인합니다. */
        bool isValid() const { return _buffer != 0; }
        /** @brief 내용을 담은 정점 버퍼로 다시 만듭니다. 못 만들면 비우고 false 입니다(알리는 것은 부르는 쪽). */
        [[nodiscard]] bool createVertex( IRHIDevice& device, const void* pData, uint32 sizeBytes );
        /** @brief 복사의 도착이 되는 빈 GPU 정점 버퍼로 다시 만듭니다(`IRHIResourceFactory::createGPUVertexBuffer`). */
        [[nodiscard]] bool createGPUVertex( IRHIDevice& device, uint32 sizeBytes );
        /** @brief 내용을 담은 인덱스 버퍼로 다시 만듭니다. */
        [[nodiscard]] bool createIndex( IRHIDevice& device, const void* pData, uint32 sizeBytes );
        /** @brief 버퍼를 놓습니다. 디바이스가 없으면 값만 잊습니다. 여러 번 불러도 안전합니다. */
        void release( IRHIDevice* pDevice );
    };
} // namespace sw
