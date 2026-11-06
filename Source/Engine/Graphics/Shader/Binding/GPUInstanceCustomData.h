/**
 * @file GPUInstanceCustomData.h
 * @brief 인스턴스마다 다른 사용자 값 float4 둘입니다(언리얼 Custom Primitive Data · 유니티 MaterialPropertyBlock 의 값 칸). 파일도 머티리얼 인스턴스도 없이 배치를 깨지 않습니다.
 * @details HLSL 쪽은 `instance_data.hlsli` 의 `_customData0` · `_customData1` 이고, 머티리얼 셰이더가 `context._instance.getCustomData( i )` 로 읽는다(정점 단계 — 픽셀로는 보간 칸으로 넘긴다).
 *
 *          겹치는 일과 나눈 자리:
 *          - `MaterialInstance` 는 값 · 텍스처 · 키워드를 덮는 드로우 단위 객체다. 인스턴스마다 하나면 머티리얼 원소가 늘고 슬롯 바인딩 백엔드(DX11 · GL)에서 배치가 갈린다. 프레임마다 다른 값을 수천 개에 줄 길이 아니다.
 *          - `GPUSpriteInstanceData` 는 스프라이트 프레임과 8 비트 색이다(뜻이 정해진 칸). 이 칸은 뜻을 머티리얼 셰이더가 정하는 실수이고 범위 제한이 없다(HDR 색 · 데미지 양 · 위상).
 *          값은 `DrawCandidate` 의 내용이고 배치 키가 아니다 — 바뀌면 그 인스턴스만 더티 구간으로 올라간다.
 *          칸 수는 언리얼의 36 실수보다 적다: 인스턴스 원소가 모든 인스턴스에 드는 비용이라 원소 안에 둘만 둔다. 더 필요해지면 번호로 가리키는 별도 버퍼를 둔다(슬롯을 쓰는 변경이다).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /** @struct GPUInstanceCustomData @brief 인스턴스 하나의 사용자 값입니다. 기본은 모두 0 입니다(기본값을 쓰는 셰이더가 0 의 뜻을 정한다). */
    struct GPUInstanceCustomData
    {
        /** @brief 칸 수입니다. `instance_data.hlsli` 의 `_customData#` 수와 같습니다. */
        static constexpr uint32 kCount = 2;

        float4 _arrValue[kCount]{};

        /** @brief 비트로 비교합니다(더티 판정 — 엡실론이면 작은 변화가 올라가지 않는다). */
        bool operator==( const GPUInstanceCustomData& other ) const { return Memory::compare( _arrValue, other._arrValue, sizeof( _arrValue ) ) == 0; }
        /** @brief operator== 의 부정입니다. */
        bool operator!=( const GPUInstanceCustomData& other ) const { return ( *this == other ) == false; }
    };
    static_assert( sizeof( GPUInstanceCustomData ) == sizeof( float4 ) * GPUInstanceCustomData::kCount, "GPUInstanceCustomData must be exactly the float4 slots of InstanceData" );
} // namespace sw
