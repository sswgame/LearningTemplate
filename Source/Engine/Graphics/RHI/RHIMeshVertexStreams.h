/**
 * @file RHIMeshVertexStreams.h
 * @brief CPU 정점(`RHIVertex`)을 GPU 메시 정점 스트림(위치 · 속성)으로 바꿉니다. 메시 하나의 정점 버퍼와 씬 정점 풀이 같은 모양을 씁니다.
 *
 * [모양]
 * 정점 버퍼 하나 = [위치 구간: 용량 × 12 B][속성 구간: 용량 × 형식의 원소 크기]. 슬롯 0 은 버퍼 시작에서, 슬롯 2 는 속성 구간 시작에서 건다.
 * 정점 번호(baseVertex 포함)가 두 구간에 같은 칸을 가리키므로 풀의 구간 나누기 · 간접 인자는 그대로다. 언리얼이 정적 메시의 위치 · 탄젠트 · UV 버퍼를
 * 따로 두는 것과 같은 나눔이고, 깊이 · 그림자 패스는 위치 구간만 읽는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"

namespace sw
{
    class IRHICommandList;

    /**
     * @struct RHIMeshVertexStreams
     * @brief 메시 정점 스트림의 크기 계산과 변환입니다.
     */
    struct SW_API RHIMeshVertexStreams
    {
        /** @brief 정점 @p vertexCapacity 개를 담는 버퍼의 바이트 수입니다(위치 구간 + 속성 구간). */
        static uint32 computeBufferBytes( RHIMeshVertexFormat format, uint32 vertexCapacity );
        /** @brief 속성 구간이 시작하는 바이트입니다(위치 구간 바로 뒤). */
        static uint32 computeAttributeOffset( uint32 vertexCapacity ) { return vertexCapacity * constant::render::kMeshPositionStride; }

        /**
         * @brief 정점 @p count 개를 위치 원소(@p pOutPosition, 12 B 씩)와 형식의 속성 원소(@p pOutAttribute)로 바꿔 씁니다.
         * @details `Packed` 는 노멀 · 탄젠트를 snorm8 로 줄인다 — 단위 벡터가 아닌 값(지형이 탄젠트에 싣는 경계)을 넣은 메시는 `Full` 이어야 한다.
         */
        static void write( RHIMeshVertexFormat format, const RHIVertex* pSource, uint32 count, uint8* pOutPosition, uint8* pOutAttribute );

        /** @brief 메시 정점 버퍼의 위치 구간(슬롯 0)과 속성 구간(슬롯 2, @p attributeOffset 부터)을 겁니다. stride 는 형식의 원소 크기다(드로우가 PSO 의 형식과 대조한다). */
        static void bind( IRHICommandList& cmd, RHIBufferHandle vertexBuffer, uint32 attributeOffset, RHIMeshVertexFormat format );

        /** @brief 한 성분을 snorm8 로 줄입니다([-1, 1] 로 자르고 반올림). */
        static int8 packSnorm8( float32 value );
    };
} // namespace sw
