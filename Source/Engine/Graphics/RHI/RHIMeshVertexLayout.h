/**
 * @file RHIMeshVertexLayout.h
 * @brief 메시 정점 스트림의 계약입니다: 속성 스트림 원소(Packed · HighPrecision), 정점 속성 선언 표, 정점 버퍼 슬롯 상수.
 * @details 네 백엔드의 입력 레이아웃과 메시 업로드만 읽는다 — 서술체 묶음(`RHIDescriptors.h`)에서 떼어 half · 메시 정점 헤더를 그 몇 곳만 끈다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Half.h"

#include "Engine/Graphics/Mesh/MeshVertex.h"

namespace sw
{
    /** @brief `RHIMeshVertexFormat::Packed` 의 속성 스트림 원소입니다(슬롯 2). */
    struct RHIPackedMeshAttributes
    {
        int8    _arrNormal[4];  ///< snorm8 — w 는 0
        int8    _arrTangent[4]; ///< snorm8 — w 는 바이탄젠트 부호 × 이음매 크기(±127, 이음매는 1 + 변 묶음 — RHIVertex::makeSeamTangentW)
        float32 _arrUV[2];      ///< 물이 월드 좌표를 싣는다 — half 로 줄이지 않는다
        /// half — 감사 R-15(float4 16 B → 8 B). 8 비트 unorm(언리얼 `FColor`)이 아닌 까닭: 물이 정점 색에 깊이(1 넘음)와 부호 있는 흐름을 싣는다. 색은 half 의 11 비트 가수로 충분하다
        half _arrColor[4];
    };
    /// @brief Packed 속성 원소 크기(바이트)입니다 — 네 백엔드의 입력 레이아웃이 이 보폭을 받는다.
    inline constexpr uint32 kPackedMeshAttributesBytes = 24;
    static_assert( sizeof( RHIPackedMeshAttributes ) == kPackedMeshAttributesBytes, "packed mesh attributes are 24 bytes (imported by four backends' layouts)" );

    /** @brief `RHIMeshVertexFormat::HighPrecision` 의 속성 스트림 원소입니다(슬롯 2). `RHIVertex` 에서 위치를 뺀 모양이다. */
    struct RHIHighPrecisionMeshAttributes
    {
        float32 _arrNormal[3];
        float32 _arrUV[2];
        float32 _arrColor[4];
        float32 _arrTangent[4];
    };
    /// @brief HighPrecision 속성 원소 크기(바이트)입니다.
    inline constexpr uint32 kHighPrecisionMeshAttributesBytes = 52;
    static_assert( sizeof( RHIHighPrecisionMeshAttributes ) == kHighPrecisionMeshAttributesBytes, "full mesh attributes are 52 bytes" );
} // namespace sw

namespace sw
{
    /**
     * @struct RHIVertexAttribute
     * @brief 정점 속성 하나의 선언입니다. 네 백엔드가 **같은 테이블**을 읽어 각자의 입력 레이아웃을 만듭니다.
     * @details 백엔드마다 테이블을 손으로 적으면 속성을 추가할 때 한 곳을 빠뜨린 백엔드만 조용히 다른 이미지를 낸다.
     *          테이블을 하나로 두면 이런 문제가 생기지 않는다.
     * @note `_location` 은 HLSL 선언 **순서**와 같아야 한다(Vulkan location, GL 정점 속성 번호).
     *       DX 는 시맨틱 이름으로 연결하므로 `_pSemanticName` 이 그 역할을 한다.
     */
    /** @brief 정점 속성 하나의 메모리 형식입니다. 백엔드가 자기 포맷 enum 으로 옮긴다(DXGI · VkFormat · GL 크기 · 형 · 정규화). */
    enum class RHIVertexAttributeFormat : uint8
    {
        Float2,
        Float3,
        Float4,
        Uint1,    ///< 32비트 부호 없는 정수 하나(셰이더는 uint 로 받는다)
        Snorm8x4, ///< 부호 있는 8비트 넷, [-1, 1] 로 정규화(셰이더는 float 로 받는다)
        Half4,    ///< 16비트 부동소수 넷(셰이더는 float 로 받는다) — Packed 메시의 정점 색
    };

    struct RHIVertexAttribute
    {
        const utf8*              _pSemanticName;            ///< DX 시맨틱 이름. HLSL 의 `: POSITION` 등과 같아야 한다.
        uint32                   _location;                 ///< Vulkan location / GL 정점 속성 번호(셰이더의 `SW_VK_LOCATION`).
        RHIVertexAttributeFormat _format;                   ///< 메모리 형식
        uint32                   _byteOffset;               ///< 그 슬롯 원소 안의 바이트 오프셋
        uint32                   _inputSlot{ 0 };           ///< 정점 버퍼 슬롯(`constant::render::kMeshPositionSlot` · `kInstanceSlotStreamSlot` · `kMeshAttributeSlot`)
        uint8                    _bPerInstance{ SW_FALSE }; ///< 인스턴스마다 한 원소를 읽는다 (step rate 1).
    };
} // namespace sw

namespace sw
{
    namespace constant::render
    {
        /**
         * @brief 인스턴스 슬롯 스트림이 바인딩되는 정점 버퍼 슬롯입니다. 원소는 uint 하나이고, 그 드로우 인스턴스의 **전역 인스턴스 위치**입니다.
         * @details 씬 드로우는 인스턴스마다 자기 위치(가시 목록 슬롯)를 알아야 인스턴스 버퍼를 읽을 수 있다. 배치마다 루트 상수로
         *          시작 오프셋을 실으면 같은 PSO 의 배치들을 멀티 드로우 하나로 낼 수 없다. 그래서 슬롯 1 에 `0,1,2,…` 스트림을
         *          인스턴스 스텝으로 바인딩하고 간접 인자의 startInstance 를 배치 시작으로 둔다. 그러면 입력 어셈블러가 네 API 모두에서
         *          `startInstance + i` 번째 원소를 읽는다.
         *          SV_InstanceID 가 startInstance 를 포함하는지는 API 마다 다르므로 여기에 기대지 않는다. 언리얼 D3D11 경로의
         *          인스턴스 ID 스트림에 해당한다.
         */
        inline constexpr uint32 kInstanceSlotStreamSlot   = 1;
        inline constexpr uint32 kInstanceSlotStreamStride = static_cast<uint32>( sizeof( uint32 ) );
        /** @brief 메시 위치 스트림(float3)의 슬롯 · 원소 크기입니다. 깊이 · 그림자 패스는 이 스트림만 읽는다(언리얼의 위치 전용 스트림). */
        inline constexpr uint32 kMeshPositionSlot   = 0;
        inline constexpr uint32 kMeshPositionStride = static_cast<uint32>( sizeof( float32 ) * 3u );
        /** @brief 메시 속성 스트림(노멀 · UV · 색 · 탄젠트)의 슬롯입니다. 원소 크기는 메시 정점 형식이 정한다(`getMeshAttributeStride`). */
        inline constexpr uint32 kMeshAttributeSlot = 2;
        /** @brief 정점 버퍼 슬롯 수입니다(위치 · 인스턴스 슬롯 스트림 · 속성). */
        inline constexpr uint32 kVertexBufferSlotCount = 3;

        /**
         * @brief 정점 입력 레이아웃의 **기준**입니다 — 메시 정점 형식마다 하나. 네 백엔드가 PSO 의 형식(`RHIPipelineStateDesc::_meshVertexFormat`)으로 표를 고른다.
         * @details 두 표는 순서 · 시맨틱 · location · 슬롯이 같고 형식 · 오프셋만 다르다(아래 static_assert) — 그래서 리플렉션이 준 "읽는 속성" 마스크가
         *          어느 표에도 같은 칸을 가리킨다. location 은 셰이더의 `SW_VK_LOCATION` 과 같다(선언 순서에 기대지 않는다).
         *          PSO 의 입력 레이아웃은 표에서 **그 정점 셰이더가 읽는 속성만** 골라 만든다(`RHIShaderRequest::computeConsumedVertexAttributeMask`).
         *          정점을 SV_VertexID 로 만드는 셰이더(`VertexInput` 을 받지 않는다 — 풀스크린 · 캔버스 · 입자 · 에디터 UI)는 레이아웃이 비어 정점 버퍼 없이
         *          몇 정점이든 그린다(언리얼의 빈 정점 선언 · Godot 의 빈 정점 포맷). 읽는 슬롯에 버퍼가 없거나 속성 스트림의 원소 크기가 PSO 의 형식과 다른
         *          드로우는 버린다(`RHIDrawDiagnostics::validateVertexSlotsBound`).
         * @note 오프셋은 손으로 적지 않는다. `SW_OFFSET_OF` 를 쓰므로 구조체를 바꾸면 자동으로 따라온다.
         */
        inline constexpr RHIVertexAttribute arrPackedMeshVertexAttribute[] = {
            { "POSITION", 0, RHIVertexAttributeFormat::Float3, 0, kMeshPositionSlot, SW_FALSE },
            { "NORMAL", 1, RHIVertexAttributeFormat::Snorm8x4, SW_OFFSET_OF( RHIPackedMeshAttributes, _arrNormal ), kMeshAttributeSlot, SW_FALSE },
            { "TEXCOORD", 2, RHIVertexAttributeFormat::Float2, SW_OFFSET_OF( RHIPackedMeshAttributes, _arrUV ), kMeshAttributeSlot, SW_FALSE },
            { "COLOR", 3, RHIVertexAttributeFormat::Half4, SW_OFFSET_OF( RHIPackedMeshAttributes, _arrColor ), kMeshAttributeSlot, SW_FALSE },
            { "TANGENT", 4, RHIVertexAttributeFormat::Snorm8x4, SW_OFFSET_OF( RHIPackedMeshAttributes, _arrTangent ), kMeshAttributeSlot, SW_FALSE },
            { "SW_INSTANCESLOT", 5, RHIVertexAttributeFormat::Uint1, 0, kInstanceSlotStreamSlot, SW_TRUE },
        };
        inline constexpr RHIVertexAttribute arrHighPrecisionMeshVertexAttribute[] = {
            { "POSITION", 0, RHIVertexAttributeFormat::Float3, 0, kMeshPositionSlot, SW_FALSE },
            { "NORMAL", 1, RHIVertexAttributeFormat::Float3, SW_OFFSET_OF( RHIHighPrecisionMeshAttributes, _arrNormal ), kMeshAttributeSlot, SW_FALSE },
            { "TEXCOORD", 2, RHIVertexAttributeFormat::Float2, SW_OFFSET_OF( RHIHighPrecisionMeshAttributes, _arrUV ), kMeshAttributeSlot, SW_FALSE },
            { "COLOR", 3, RHIVertexAttributeFormat::Float4, SW_OFFSET_OF( RHIHighPrecisionMeshAttributes, _arrColor ), kMeshAttributeSlot, SW_FALSE },
            { "TANGENT", 4, RHIVertexAttributeFormat::Float4, SW_OFFSET_OF( RHIHighPrecisionMeshAttributes, _arrTangent ), kMeshAttributeSlot, SW_FALSE },
            { "SW_INSTANCESLOT", 5, RHIVertexAttributeFormat::Uint1, 0, kInstanceSlotStreamSlot, SW_TRUE },
        };
        /// @brief 정점 속성 수입니다(두 표가 같다). 백엔드가 배열 크기를 직접 세지 않도록 함께 둡니다.
        inline constexpr uint32 kVertexAttributeCount = static_cast<uint32>( sizeof( arrPackedMeshVertexAttribute ) / sizeof( arrPackedMeshVertexAttribute[0] ) );
        static_assert( kVertexAttributeCount == sizeof( arrHighPrecisionMeshVertexAttribute ) / sizeof( arrHighPrecisionMeshVertexAttribute[0] ), "the two mesh vertex tables must list the same attributes" );

        /** @brief 메시 정점 형식의 속성 표입니다(`kVertexAttributeCount` 칸). */
        constexpr const RHIVertexAttribute* getMeshVertexAttributes( RHIMeshVertexFormat format )
        {
            return format == RHIMeshVertexFormat::HighPrecision ? arrHighPrecisionMeshVertexAttribute : arrPackedMeshVertexAttribute;
        }
        /** @brief 메시 정점 형식의 속성 스트림 원소 크기(바이트)입니다. */
        constexpr uint32 getMeshAttributeStride( RHIMeshVertexFormat format )
        {
            return format == RHIMeshVertexFormat::HighPrecision ? static_cast<uint32>( sizeof( RHIHighPrecisionMeshAttributes ) ) : static_cast<uint32>( sizeof( RHIPackedMeshAttributes ) );
        }
        /** @brief 정점 버퍼 슬롯 하나의 원소 크기(바이트)입니다. 속성 슬롯은 메시 정점 형식이 정한다. */
        constexpr uint32 getVertexSlotStride( RHIMeshVertexFormat format, uint32 slot )
        {
            return slot == kMeshPositionSlot ? kMeshPositionStride : ( slot == kInstanceSlotStreamSlot ? kInstanceSlotStreamStride : getMeshAttributeStride( format ) );
        }
        constexpr bool areMeshVertexTablesAligned()
        {
            for ( uint32 index = 0; index < kVertexAttributeCount; ++index )
            {
                const RHIVertexAttribute& packed = arrPackedMeshVertexAttribute[index];
                const RHIVertexAttribute& full   = arrHighPrecisionMeshVertexAttribute[index];
                if ( packed._location != full._location || packed._inputSlot != full._inputSlot || packed._bPerInstance != full._bPerInstance || packed._location != index )
                    return false;
            }
            return true;
        }
        static_assert( areMeshVertexTablesAligned(), "the mesh vertex tables must agree on location (= row) and slot, and differ only in format and offset" );
    } // namespace constant::render
} // namespace sw
