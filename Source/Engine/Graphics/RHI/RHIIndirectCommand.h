/**
 * @file RHIIndirectCommand.h
 * @brief 간접 드로우 · 디스패치 인자입니다(GPU 가 읽는 바이트 배치).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct RHIDrawIndirectCommand
     * @brief 간접 드로우 인자입니다.
     */
    REFLECT()
    struct RHIDrawIndirectCommand
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _vertexCount = 3; ///< 정점 개수

        PROPERTY()
        uint32 _instanceCount{ 1 }; ///< 인스턴스 개수

        PROPERTY()
        uint32 _startVertexLocation{ 0 }; ///< 시작 정점 위치

        PROPERTY()
        uint32 _startInstanceLocation{ 0 }; ///< 시작 인스턴스 위치
    };

    /**
     * @struct RHIDispatchIndirectCommand
     * @brief 간접 컴퓨트 디스패치 인자입니다.
     */
    REFLECT()
    struct RHIDispatchIndirectCommand
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _threadGroupCountX{ 1 }; ///< X축 스레드 그룹 개수

        PROPERTY()
        uint32 _threadGroupCountY{ 1 }; ///< Y축 스레드 그룹 개수

        PROPERTY()
        uint32 _threadGroupCountZ{ 1 }; ///< Z축 스레드 그룹 개수
    };

    /**
     * @struct RHIDrawIndexedIndirectCommand
     * @brief 인덱스 버퍼를 쓰는 간접 드로우 인자입니다. D3D12 와 Vulkan 의 레이아웃이 같습니다.
     */
    struct RHIDrawIndexedIndirectCommand
    {
        uint32 _indexCountPerInstance{ 0 };
        uint32 _instanceCount{ 1 };
        uint32 _startIndexLocation{ 0 };
        int32  _baseVertexLocation{ 0 };
        uint32 _startInstanceLocation{ 0 };
    };
} // namespace sw
