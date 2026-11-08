/**
 * @file RHIDescriptors.h
 * @brief RHI 서술체와 백엔드 계약 상수입니다: 정점 레이아웃, 스왑체인, 뷰포트, 인디렉트 인자, 버퍼 · 텍스처 · PSO · 렌더 패스 서술체,
 *        전송 티켓, 포맷 블록 계산.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Foundation/Foundation.h"
#include "Engine/Graphics/RHI/RHIEnums.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/RHI/RHIRenderState.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Window/IRenderSurface.h"

namespace sw
{
    class IRHISwapChain;

    /**
     * @struct RHIVertex
     * @brief CPU 쪽 메시 정점 하나입니다(임포트 · 지형 · 파괴 · 천 등 기하 처리가 쓰는 형식). 위치, 노멀, UV, 색, 탄젠트를 가집니다.
     * @details GPU 에는 이 모양 그대로 올리지 않는다 — 올릴 때 위치 스트림과 속성 스트림으로 나누고 메시의 정점 형식(`RHIMeshVertexFormat`)으로
     *          바꾼다(`RHIMeshVertexStreams`). 언리얼의 빌드 정점(`FStaticMeshBuildVertex`)과 GPU 정점 버퍼의 관계와 같다.
     */
    struct RHIVertex
    {
        float32 _arrPosition[3]; ///< 정점 위치 (X, Y, Z)
        /**
         * @brief 정점 노멀 (X, Y, Z)입니다. 정규화되어 있어야 합니다.
         * @details 주의: 셰이더에서 위치로 노멀을 만들어 내지 않는다. 원점 중심 박스형 도형에만 맞아서 평면, 구, 원뿔이
         *          조용히 틀린 빛을 받는다(`y = 0` 바닥 평면이 옆을 보는 것처럼 칠해진다).
         */
        float32 _arrNormal[3];
        /**
         * @brief 텍스처 좌표 (U, V)입니다.
         * @details 노멀과 **같은 함정**이 있다. 위치로 만들어 내면(`xy * 0.5 + 0.5`) 원점 중심 단위 도형이 아닐 때 알베도가
         *          엉뚱하게 붙고 옆면과 뚜껑이 같은 위치를 읽는다. 도형 생성기가 면마다 UV 를 펼쳐 준다.
         */
        float32 _arrUV[2];
        float32 _arrColor[4]; ///< 정점 색상 (R, G, B, A)
        /**
         * @brief 탄젠트 (X, Y, Z)와 바이탄젠트 부호(W, ±1)입니다. 노멀 맵이 탄젠트 공간을 세울 때 씁니다(glTF · MikkTSpace 규약 — 바이탄젠트 = cross( N, T ) × W).
         * @details 위치 · 노멀 · UV 에서 나오는 값이라 손으로 채우지 않는다. `Mesh::setVertices` 가 `Mesh::computeTangents` 로 구하고,
         *          `.mesh` 는 임포트 때 구한 값을 싣는다(`Mesh::setVerticesWithTangents`). 노멀과 같은 함정 — 셰이더가 다른 값에서 지어내지 않는다.
         */
        float32 _arrTangent[4];
    };

    /**
     * @brief GPU 메시 정점의 속성 스트림 형식입니다. 메시마다 고르고(`Mesh::setVertexFormat`), PSO 가 같은 형식으로 레이아웃을 짓는다(`RHIPipelineStateDesc::_meshVertexFormat`).
     * @details 위치 스트림(슬롯 0, float3)은 두 형식이 같다. 언리얼이 메시 종류마다 vertex factory 로 정점 선언을 고르는 자리다.
     */
    enum class RHIMeshVertexFormat : uint8
    {
        Packed,        ///< 노멀 · 탄젠트 snorm8x4, UV float2, 색 float4(32 B). 노멀 · 탄젠트가 단위 벡터인 일반 메시
        HighPrecision, ///< 노멀 float3, UV float2, 색 float4, 탄젠트 float4(52 B). 색 · 탄젠트 칸에 단위 벡터가 아닌 데이터를 싣는 메시(지형 형태 변환) — 언리얼의 고정밀 탄젠트 옵션 자리
        Count,
    };

    /** @brief `RHIMeshVertexFormat::Packed` 의 속성 스트림 원소입니다(슬롯 2). */
    struct RHIPackedMeshAttributes
    {
        int8    _arrNormal[4];  ///< snorm8 — w 는 0
        int8    _arrTangent[4]; ///< snorm8 — w 는 바이탄젠트 부호(±127 = ±1)
        float32 _arrUV[2];      ///< 물이 월드 좌표를 싣는다 — half 로 줄이지 않는다
        float32 _arrColor[4];
    };
    static_assert( sizeof( RHIPackedMeshAttributes ) == 32, "packed mesh attributes are 32 bytes (imported by four backends' layouts)" );

    /** @brief `RHIMeshVertexFormat::HighPrecision` 의 속성 스트림 원소입니다(슬롯 2). `RHIVertex` 에서 위치를 뺀 모양이다. */
    struct RHIHighPrecisionMeshAttributes
    {
        float32 _arrNormal[3];
        float32 _arrUV[2];
        float32 _arrColor[4];
        float32 _arrTangent[4];
    };
    static_assert( sizeof( RHIHighPrecisionMeshAttributes ) == 52, "full mesh attributes are 52 bytes" );
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
        // ------------------------------------------------------------------------------
        // 백엔드 간에 값이 같아야 하는 계약 상수.
        // 지금 값이 우연히 같더라도 바뀔 수 있는 값이면 반드시 여기로 뺀다. 한쪽 백엔드만
        // 바뀌면 컴파일은 통과하고 런타임에 조용히 깨진다(아래 kBackBufferFormat 사례).
        // 이 블록이 RHISwapChainDesc 앞에 있는 이유는 그 기본값으로 쓰이기 때문이다.
        // ------------------------------------------------------------------------------

        /**
         * @brief 백버퍼 컬러 포맷입니다. 네 백엔드의 스왑체인과 파이프라인 RTV 기본값이 같아야 합니다.
         * @details 파이프라인은 `RHIPipelineStateDesc::_arrRTVFormat` 으로 렌더 패스와 PSO 를 만든다.
         *          스왑체인이 다른 포맷을 고르면 백버퍼에 직접 그리는 패스가 모두 비호환이 된다.
         *          예를 들어 Vulkan 이 `B8G8R8A8_UNORM` 을 고르면 에디터 없이 실행하는 경로가 렌더 패스 비호환으로 깨진다.
         *          백엔드가 이 포맷을 낼 수 없으면 조용히 다른 포맷을 고르지 말고
         *          `IRHIDevice::getBackBufferFormat()` 으로 실제 채택한 값을 보고해야 한다.
         */
        inline constexpr RHIFormat kBackBufferFormat = RHIFormat::R8G8B8A8_UNORM;

        /**
         * @brief 오프스크린 컬러 타깃(에디터 게임 뷰 등)의 기본 포맷입니다.
         * @details Vulkan 은 이 포맷일 때만 공용 오프스크린 렌더 패스를 재사용하고, 나머지는 전용
         *          렌더 패스를 만든다. 값이 갈라지면 렌더 패스가 조용히 늘어나거나 비호환이 된다.
         */
        inline constexpr RHIFormat kOffscreenColorFormat = RHIFormat::R8G8B8A8_UNORM;

        /**
         * @brief 깊이와 스텐실의 기본 포맷입니다.
         * @details DX11, DX12, GL 은 이 값을 그대로 쓴다. Vulkan 은 물리 디바이스가 지원하지 않으면
         *          대체 포맷을 고르고(`selectDepthFormat()`), 파이프라인 렌더 패스도 그 값에 맞춘다.
         */
        inline constexpr RHIFormat kDepthStencilFormat = RHIFormat::D24_UNORM_S8_UINT;

        /**
         * @brief 상수 버퍼 슬롯 정렬(바이트)입니다.
         * @details 네 백엔드가 같은 값으로 슬롯 크기를 계산해야 프레임 링 오프셋이 어긋나지 않는다.
         *          D3D12 의 요구치(256)가 가장 크므로 그것을 공통값으로 쓴다. 텍스처 행 정렬
         *          (`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT`)은 이름만 같은 별개 개념이므로 섞지 않는다.
         */
        inline constexpr uint32 kConstantBufferAlignment = 256;

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
         *          드로우는 버린다(`RHIDrawDiagnostics::checkVertexSlotsBound`).
         * @note 오프셋은 손으로 적지 않는다. `SW_OFFSET_OF` 를 쓰므로 구조체를 바꾸면 자동으로 따라온다.
         */
        inline constexpr RHIVertexAttribute arrPackedMeshVertexAttribute[] = {
            { "POSITION", 0, RHIVertexAttributeFormat::Float3, 0, kMeshPositionSlot, SW_FALSE },
            { "NORMAL", 1, RHIVertexAttributeFormat::Snorm8x4, SW_OFFSET_OF( RHIPackedMeshAttributes, _arrNormal ), kMeshAttributeSlot, SW_FALSE },
            { "TEXCOORD", 2, RHIVertexAttributeFormat::Float2, SW_OFFSET_OF( RHIPackedMeshAttributes, _arrUV ), kMeshAttributeSlot, SW_FALSE },
            { "COLOR", 3, RHIVertexAttributeFormat::Float4, SW_OFFSET_OF( RHIPackedMeshAttributes, _arrColor ), kMeshAttributeSlot, SW_FALSE },
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

        /** @brief submitTextureUpload 가 한 번에 받는 밉 수의 상한입니다. 2^16 텍스처의 전체 체인(17단계)을 덮습니다. */
        inline constexpr uint32 kMaxTextureMipCount = 17;

        /**
         * @brief CPU 가 GPU 를 앞설 수 있는 최대 프레임 수입니다. 프레임별 리소스 링의 슬롯 수와 같습니다.
         * @details **이 개념의 유일한 원본이다.** 프레임마다 따로 있어야 하는 자원은 모두 이 값으로 크기를 잡는다.
         *          DX12 의 커맨드 얼로케이터와 업로드 슬롯, Vulkan 의 커맨드 버퍼, 펜스, 상수 버퍼 슬롯, 디스크립터 셋,
         *          에디터 draw 스냅샷이 여기에 해당한다.
         *          별칭은 두지 않는다. 같은 개념에 상수가 둘이면 섞어 쓰다가 한쪽만 올렸을 때 디스크립터가
         *          버퍼 밖을 가리킨다.
         */
        inline constexpr uint32 kMaxFrameCountInFlight = 3;

        /**
         * @brief 프레임 하나에 찍을 수 있는 GPU 타임스탬프 슬롯 수입니다.
         * @details 패스 인덱스로 슬롯을 고정해 쓴다(패스당 begin/end 두 슬롯). 네 백엔드가 같은 값으로
         *          쿼리 힙, 풀, 쿼리 배열 크기를 잡으므로 한쪽만 바꾸면 다른 쪽이 구간 밖을 읽는다.
         *          64 슬롯은 begin/end 쌍 32개인데, 렌더러가 뒤쪽 세 슬롯을 프레임 전체와 프레임 그래프에 쓰므로 패스는
         *          30개까지다(`FrameRendererUtil`). 렌더 그래프가 그보다 길어지면 뒤쪽 패스는 시간이 빠지고 로드 때 경고한다.
         *          슬롯 비트 집합은 `GPUTimestampSlotMask` 하나로 다룬다 — 슬롯 수를 늘려도 비트 폭이 따라가는지 그 형식이 확인한다.
         */
        inline constexpr uint32 kMaxGPUTimestampSlot = 64;

        /**
         * @brief GPU 리소스를 지연 해제하는 프레임 수입니다(RHIReleaseQueue 기본 frameLatency).
         * @details 네 RHI 백엔드(DX11, DX12, Vulkan, OpenGL)가 모두 같은 값을 써야 하는 계약이다.
         *          한쪽만 바꾸면 GPU 가 아직 참조 중인 리소스를 일찍 해제할 위험이 있다.
         */
        inline constexpr uint32 kGPUReleaseFrameLatency = 3;

        /**
         * @brief 게임 스레드가 만든 프레임 패킷이 렌더 스레드에 소비되기까지 큐에 쌓일 수 있는 최대
         *        프레임 수입니다(RenderThread 패킷 링 깊이).
         * @details 아직 큐에 있는(소비되지 않은) 패킷이 참조할 수 있는 자원은 최소 이 프레임 수만큼
         *          해제를 미뤄야 한다(예: GPUScene 의 머티리얼 원소 회수 지연). GPU 인플라이트
         *          값인 kMaxFrameCountInFlight 와는 별개 개념이므로 혼동하지 않는다.
         */
        inline constexpr uint32 kRenderFrameQueueDepth = 3;

        /**
         * @brief Vulkan 백엔드가 요구하는 최소 API 버전(major.minor)입니다. Vulkan 셰이더 쿠킹 타깃(`-fspv-target-env=vulkan<major>.<minor>`)도 이 값입니다.
         * @details 셰이더 컴파일러(Graphics/Shader)와 Vulkan 물리 디바이스 선택(Graphics/RHI/Vulkan)이 함께 읽는 계약입니다. 쿠킹된 SPIR-V 버전은
         *          이 타깃이 정하므로(vulkan1.2 → SPIR-V 1.5) 한쪽만 바꾸면 디바이스가 받지 못하는 모듈이 남습니다. API 버전 인코딩과 비교는
         *          `VulkanRHIRequiredVersion` 에 있습니다.
         *          1.2 인 까닭: Android 기기 다수와 dzn(D3D12 위의 Vulkan)이 1.2 다. 1.2 타깃의 HLSL `clip` · `discard` 는 OpKill 이라
         *          셰이더가 미분 읽기를 다 마친 뒤 버린다(binding.hlsli 8-1 절) — 1.3 의 shaderDemoteToHelperInvocation 을 요구하지 않는다.
         */
        inline constexpr uint32 kVulkanRequiredMajor = 1;
        inline constexpr uint32 kVulkanRequiredMinor = 2;
    } // namespace constant::render

    /** @brief GPU 타임스탬프 슬롯마다 한 비트(기록됨 · 준비됨)입니다. 비트 폭이 `constant::render::kMaxGPUTimestampSlot` 이상이어야 한다. */
    using GPUTimestampSlotMask = uint64;
    static_assert( sizeof( GPUTimestampSlotMask ) * 8u >= constant::render::kMaxGPUTimestampSlot, "타임스탬프 슬롯 비트 집합이 슬롯 수보다 좁다" );

    /** @brief 슬롯 @p slotIndex 의 비트입니다. 비트 연산은 이 함수로만 만든다(`1u << slot` 은 32 슬롯을 넘으면 정의되지 않는다). */
    [[nodiscard]] constexpr GPUTimestampSlotMask makeGPUTimestampSlotBit( uint32 slotIndex )
    {
        return GPUTimestampSlotMask{ 1 } << slotIndex;
    }

    /**
     * @struct RHISwapChainDesc
     * @brief 창 스왑체인을 만들 때 쓰는 설정입니다.
     */
    REFLECT()
    struct RHISwapChainDesc
    {
        REFLECT_BODY();
        // 창 핸들은 실행마다 다른 런타임 값이므로 저장하지 않는다(`Transient`). 직렬화기가 다룰 수 없는 타입을 그냥 PROPERTY 로 두면
        // 텍스트에는 "null", 바이너리에는 0바이트로 조용히 쓰인다. `ReflectionSerializationTest.EveryPropertyHasATypeTheSerializersCanCarry` 가 이를 잡아낸다.
        PROPERTY( Transient )
        void* _pWindowHandle{ nullptr }; ///< OS 윈도우 핸들 (HWND, Window XID 등)

        PROPERTY( Transient )
        void* _pWindowDisplay{ nullptr }; ///< X11 Display* · wl_display*(리눅스 전용)

        /// @brief 두 핸들의 창 시스템이다(`IRenderSurface::getSurfaceKind`). 리눅스 WSI(xlib · Wayland)와 GL 문맥(GLX)을 이것으로 고른다. 런타임 값이라 리플렉션하지 않는다.
        NativeSurfaceKind _surfaceKind{ NativeSurfaceKind::Win32 };

        PROPERTY()
        uint32 _width{ 1280 }; ///< 스왑체인 너비 (픽셀)

        PROPERTY()
        uint32 _height{ 720 }; ///< 스왑체인 높이 (픽셀)

        PROPERTY()
        uint32 _bufferCount{ 2 }; ///< 프레임버퍼 개수 (Double/Triple Buffering)

        /// @brief 백버퍼 컬러 포맷입니다. 백엔드는 이 값을 존중해야 하고, 못 내면 실제 값을 보고해야 합니다.
        RHIFormat _format{ constant::render::kBackBufferFormat };

        PROPERTY()
        bool _bVSync{ true }; ///< 수직 동기화

        PROPERTY()
        bool _bFullscreen{ false }; ///< 전체 화면

        PROPERTY( Transient )
        bool _bSoftwareAdapter{ SW_FALSE }; ///< 소프트웨어 래스터라이저로 띄운다(DX12, DX11 WARP, Vulkan CPU 디바이스). CI 러너와 같은 조건을 재현하는 진단 스위치다.
        PROPERTY( Transient )
        bool _bPreferMinimumPower{ false }; ///< 하드웨어 어댑터를 저전력 순서로 고른다(하이브리드 노트북의 내장 GPU — 언리얼 -preferIntel 자리). 기본은 고성능 순서(제조사와 무관)

        /// @brief 업로드 스테이징 용량(바이트)입니다(`gv_rhiStagingRingMegabytes`). GL · Vulkan 은 링 크기, DX11 은 GPU 가 아직 복사 중인 스테이징 텍스처의 합의 상한으로 읽고,
        ///        0 이면 그 백엔드가 초기화를 거절합니다. 실행마다 정하는 설정이라 리플렉션하지 않는다.
        uint64 _stagingRingBytes{ 0 };
        /// @brief 이 크기(바이트) 이하의 텍스처 업로드는 스테이징 없이 드라이버 복사 호출로 바로 올립니다(`gv_rhiDirectUploadMaxKilobytes`, 지금은 DX11 만 읽는다). 리플렉션하지 않는다.
        uint64 _directUploadMaxBytes{ 0 };

        /// @brief 텍스처 되읽기 링의 용량(바이트)입니다(`gv_rhiReadbackRingMegabytes`). 링을 쓰는 백엔드(GL · Vulkan)가 읽고, 0 이면 그 백엔드가 초기화를 거절합니다. 리플렉션하지 않는다.
        uint64 _readbackRingBytes{ 0 };
        /// @brief 링보다 큰 전송 한 장을 링 밖의 일회성 버퍼로 받는 상한(바이트)입니다(`gv_rhiStagingSingleMaxMegabytes`). 링을 쓰는 백엔드(GL · Vulkan)가 업로드 · 되읽기 링에 같이 쓰고,
        ///        0 이면 링보다 큰 한 장은 `Rejected` 입니다. 리플렉션하지 않는다.
        uint64 _stagingSoloMaxBytes{ 0 };

        /**
         * @brief Win32 표면의 `HWND` 를 @p outHandle 에 `void*` 로 담고 true 입니다. 창이 없으면(핸들이 nullptr, 오프스크린) @p outHandle 이 nullptr 이고 true 입니다.
         * @details `_pWindowHandle` 은 창 시스템마다 뜻이 다른 `void*` 라서(`_surfaceKind`) DX · WGL 백엔드가 직접 `HWND` 로 읽으면 안 된다.
         *          종류 확인을 이 함수 한 곳에 모은다 — `IWindow::tryGetWin32Handle` 의 스왑체인 서술판이다. 창이 없는 것은 정상 경로(DX12 오프스크린)라 실패로 치지 않는다.
         * @return 핸들이 있는데 Win32 표면이 아니면 false 이고 @p outHandle 은 nullptr 입니다. 호출처는 다른 핸들로 대신하지 않고 오류로 멈춘다.
         */
        [[nodiscard]] bool tryGetWin32Handle( void*& outHandle ) const
        {
            outHandle = nullptr;
            if ( _pWindowHandle == nullptr )
                return true;
            if ( _surfaceKind != NativeSurfaceKind::Win32 )
                return false;
            outHandle = _pWindowHandle;
            return true;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RHIViewport
     * @brief 렌더링 뷰포트 영역입니다. DirectX 규약이며 모든 백엔드가 같습니다.
     * @details 원점은 좌상단이고 픽셀 +Y 는 아래쪽이다. 클립 공간과 NDC 의 +Y 는 위쪽이다(Direct3D 규약).
     *          Vulkan 은 내부에서 negative-height 뷰포트로 바꾸고, OpenGL 은 glClipControl 로 맞춘다.
     *          오프스크린 FBO 는 GL_UPPER_LEFT, 기본 프레임버퍼는 GL_LOWER_LEFT 이고, 둘 다 GL_ZERO_TO_ONE 이다.
     */
    REFLECT()
    struct RHIViewport
    {
        REFLECT_BODY();
        PROPERTY()
        float32 _x{ 0.0f }; ///< 뷰포트 좌상단 X

        PROPERTY()
        float32 _y{ 0.0f }; ///< 뷰포트 좌상단 Y

        PROPERTY()
        float32 _width{ constant::window::kDefaultViewportWidth }; ///< 뷰포트 너비

        PROPERTY()
        float32 _height{ constant::window::kDefaultViewportHeight }; ///< 뷰포트 높이 (양수; Vulkan이 부호 반전)

        PROPERTY()
        float32 _minDepth{ 0.0f }; ///< 최소 깊이 (0.0~1.0)

        PROPERTY()
        float32 _maxDepth{ 1.0f }; ///< 최대 깊이 (0.0~1.0)
    };
} // namespace sw

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
} // namespace sw

namespace sw
{
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
} // namespace sw

namespace sw
{
    /**
     * @struct RHIBufferCopyRegion
     * @brief 버퍼 부분 갱신의 한 조각입니다. 원본 블롭 안의 위치, 목적 버퍼 안의 위치, 크기를 가집니다.
     * @details 여러 조각을 **한 번의 호출**로 넘기려고 만든 구조체다. 조각마다 따로 부르면 백엔드가
     *          스테이징 확보와 큐 제출을 그만큼 반복한다(DX12 에서 호출당 약 3.3µs).
     */
    struct RHIBufferCopyRegion
    {
        uint32 _srcOffset{ 0 }; ///< 넘긴 원본 포인터 기준 바이트 오프셋
        uint32 _dstOffset{ 0 }; ///< 목적 버퍼 안의 바이트 오프셋
        uint32 _sizeBytes{ 0 }; ///< 옮길 바이트 수
    };

    /**
     * @struct RHIBufferTransition
     * @brief 버퍼 하나의 상태 전이입니다(`IRHICommandList::transitionBuffers` 의 한 원소). 모으는 쪽은 `RHIBufferTransitionBatch` 다.
     */
    struct RHIBufferTransition
    {
        RHIBufferHandle _buffer{ 0 };
        RHIBufferState  _state{ RHIBufferState::Common };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RHIBufferDesc
     * @brief 범용 버퍼를 만들 때 쓰는 서술체입니다.
     */
    struct RHIBufferDesc
    {
        uint32         _sizeBytes{ 0 };
        uint32         _elementSize{ 0 }; ///< 구조 버퍼의 stride 입니다. raw 버퍼와 바이트 버퍼는 0 입니다.
        uint32         _elementCount{ 0 };
        RHIBufferUsage _usage        = RHIBufferUsage::None;
        const void*    _pInitialData = nullptr;
    };
} // namespace sw

namespace sw
{
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

namespace sw
{
    /**
     * @struct RHIPipelineStateDesc
     * @brief 그래픽스 파이프라인과 컴퓨트 파이프라인의 상태를 만들 때 쓰는 서술체입니다.
     */
    struct SW_API RHIPipelineStateDesc
    {
        string _vertexShaderPath;  ///< 버텍스 셰이더 소스 경로
        string _vertexEntryPoint;  ///< 버텍스 셰이더 진입점
        string _pixelShaderPath;   ///< 픽셀 셰이더 소스 경로
        string _pixelEntryPoint;   ///< 픽셀 셰이더 진입점
        string _computeShaderPath; ///< 컴퓨트 셰이더 소스 경로
        string _computeEntryPoint; ///< 컴퓨트 셰이더 진입점
        /// 선택 스테이지의 진입점입니다. 비면 그 스테이지가 없습니다. 소스는 `_vertexShaderPath` 와 같은 파일입니다. 헐과 도메인은 함께 있고, 그때 위상은 `PatchList3` 입니다.
        string _geometryEntryPoint;
        string _hullEntryPoint;
        string _domainEntryPoint;

        vector<string> _listShaderDefine; ///< 컴파일 매크로("NAME" 또는 "NAME=VALUE"). 셰이더 퍼뮤테이션을 만든다.

        RHIPrimitiveTopology _topology;                           ///< 프리미티브 위상
        uint32               _numRenderTargets;                   ///< 컬러 RT 개수 (MRT)
        RHIFormat            _arrRTVFormat[kMaxColorAttachments]; ///< RT별 포맷
        RHIFormat            _depthStencilFormat;
        RHIRenderState       _renderState; ///< 블렌드 · 깊이 · 래스터 · 스텐실. 칸은 `RHIRenderState` 한 곳에서 더한다
        /**
         * @brief 정점 셰이더가 메시 속성을 읽을 때 쓸 속성 스트림 형식입니다(입력 레이아웃 표를 고른다). 정점 입력이 없는 셰이더는 쓰지 않는다.
         * @details 그리는 메시의 형식과 같아야 한다 — 머티리얼 PSO 는 배치의 형식으로 짓는다(`RenderPSOCache::materialPSOKey`). 다르면 속성 스트림 원소 크기가 달라
         *          드로우가 버려진다(`RHIDrawDiagnostics::checkVertexSlotsBound`). 언리얼에서 정점 선언이 vertex factory 로 PSO 에 드는 자리다.
         */
        RHIMeshVertexFormat _meshVertexFormat;

        /** @brief 기본 토폴로지로, 컬링 · 깊이 테스트 없이 만듭니다. */
        RHIPipelineStateDesc() noexcept;
    };
} // namespace sw

namespace sw
{
    /** @brief 렌더 타깃 기본 클리어 색(RGBA)입니다. */
    inline constexpr float4 kDefaultClearColor = { 0.1f, 0.1f, 0.1f, 1.0f };

    /** @brief 첨부 하나를 만들고 여는 데 쓰는 값들입니다(`TransientAttachmentPool`). 파이프라인 XML 의 선언에서 온다(`RenderPassAttachment::getOps`). */
    struct RHIAttachmentOps
    {
        float4               _clearColor{};
        float32              _clearDepth{ 1.0f };
        uint8                _clearStencil{ 0 };
        RHIRenderPassLoadOp  _loadOp{ RHIRenderPassLoadOp::Clear };
        RHIRenderPassStoreOp _storeOp{ RHIRenderPassStoreOp::Store };
    };

    /**
     * @struct RHIRenderPassAttachment
     * @brief 렌더 패스의 컬러 어태치먼트와 깊이 어태치먼트입니다.
     */
    struct RHIRenderPassAttachment
    {
        float4               _clearColor = kDefaultClearColor;          ///< 초기화 색상 (RGBA)
        RHIFormat            _format     = RHIFormat::R8G8B8A8_UNORM;   ///< 어태치먼트 포맷
        RHIRenderPassLoadOp  _loadOp     = RHIRenderPassLoadOp::Clear;  ///< 로드 동작
        RHIRenderPassStoreOp _storeOp    = RHIRenderPassStoreOp::Store; ///< 저장 동작
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RHIRenderPassDesc
     * @brief 렌더 패스를 만들 때 쓰는 정보입니다.
     */
    struct SW_API RHIRenderPassDesc
    {
        vector<RHIRenderPassAttachment> _listColorAttachment;  ///< 색상 어태치먼트 목록 (24 bytes)
        float32                         _clearDepth;           ///< 깊이 초기화 값 (4 bytes)
        uint8                           _clearStencil;         ///< 스텐실 초기화 값 (1 byte)
        uint8                           _bHasDepthStencil : 1; ///< 깊이/스텐실 어태치먼트 포함 (1 byte)
        [[maybe_unused]] uint8          _reservedFlags    : 7;
        uint8                           _arrReserved[2]; ///< 8바이트 정렬 패딩 (2 bytes)

        /** @brief 깊이 클리어 기본값으로 만듭니다. */
        RHIRenderPassDesc() noexcept;
    };
} // namespace sw

namespace sw
{
    /** @brief 큐브 텍스처의 면 수입니다. */
    inline constexpr uint32 kCubeFaceCount = 6;

    /**
     * @struct RHITextureDesc
     * @brief 텍스처를 만들 때 쓰는 서술체입니다.
     */
    struct SW_API RHITextureDesc
    {
        float4 _clearColor; ///< 초기화 색상 (16 bytes)
        uint32 _width;      ///< 너비 (4 bytes)
        uint32 _height;     ///< 높이 (4 bytes)
        uint32 _arraySize;  ///< 면 수 (4 bytes). Texture2D 는 1, TextureCube 는 6(`kCubeFaceCount`), Texture2DArray 는 1 이상
        /**
         * @brief 밉 레벨 수 (4 bytes)입니다. **1 이상을 주세요. 0 은 백엔드마다 뜻이 다릅니다.**
         * @details 바로 아래 `RHITextureUploadDesc::_mipLevels` 는 0 을 "텍스처가 가진 밉 전부" 로
         *          정의하지만 **이쪽에는 그런 약속이 없다.** D3D11 은 0 을 "전체 밉 체인을 만들어라"
         *          로 읽고, GL 은 `mipLevels > 0 ? mipLevels : 1` 로 접어 **1단계만** 만든다.
         *          같은 값이 백엔드마다 다른 텍스처를 만든다. 기본 생성자가 1 을 넣고 호출하는 쪽도
         *          모두 1 이상을 주므로 지금은 문제가 없다. 하지만 두 필드의 이름이 같아서 한쪽 규약을
         *          다른 쪽에 잘못 옮겨 적기 쉽다.
         */
        uint32                 _mipLevels;
        RHIFormat              _format;                 ///< 텍스처 포맷 (4 bytes)
        float32                _clearDepth;             ///< 초기화 깊이 (4 bytes)
        uint8                  _clearStencil;           ///< 초기화 스텐실 (1 byte)
        uint8                  _bIsRenderTarget    : 1; ///< 렌더 타깃 지원 여부 (1 byte)
        uint8                  _bIsDepthStencil    : 1; ///< 깊이/스텐실 지원 여부
        uint8                  _bIsShaderResource  : 1; ///< 셰이더 리소스 지원 여부
        uint8                  _bIsUnorderedAccess : 1; ///< UAV 지원 여부
        uint8                  _bIsTransient       : 1; ///< 트랜지언트 첨부 풀이 만든 것이다. 만드는 방법은 같고 GPU 메모리 추적기의 항목만 구분한다(`RHIMemoryTracker::classifyTexture`).
        [[maybe_unused]] uint8 _reservedFlags      : 3;
        RHITextureDimension    _dimension;      ///< 형태 (1 byte)
        uint8                  _arrReserved[1]; ///< 4바이트 정렬 패딩 (1 byte)

        /** @brief 기본 크기, 포맷, 클리어 값으로 만듭니다(Texture2D, 면 하나). */
        RHITextureDesc() noexcept;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 서술체의 형태와 면 수가 맞는지 확인합니다. Texture2D 는 1, TextureCube 는 6 이면서 정사각형, Texture2DArray 는 1 이상이어야 합니다.
     * @details 네 백엔드의 `createTexture2D` 가 이 판정 하나로 거부한다. 맞지 않는 값을 그대로 두면 백엔드마다 다르게 깨진다.
     */
    inline constexpr bool isRHITextureShapeValid( const RHITextureDesc& desc )
    {
        switch ( desc._dimension )
        {
            case RHITextureDimension::Texture2D:
                return desc._arraySize == 1;
            case RHITextureDimension::Texture2DArray:
                return desc._arraySize >= 1;
            case RHITextureDimension::TextureCube:
                return desc._arraySize == kCubeFaceCount && desc._width == desc._height;
        }
        return false;
    }

    /**
     * @struct RHITextureUploadDesc
     * @brief IRHIResourceFactory::submitTextureUpload 의 입력입니다. 밉 0 부터 차례로, 각 밉의 행이 빈틈없이 이어진 바이트 블록입니다.
     * @details DDS 파일의 픽셀 배치와 같다(DDSImageData::_bytes 를 그대로 넘길 수 있다). 행 패딩은
     *          백엔드가 필요하면 스스로 맞춘다(DX12 는 256 정렬 풋프린트로 다시 배치하고, GL 은 UNPACK_ALIGNMENT 1 을 쓴다).
     */
    struct SW_API RHITextureUploadDesc
    {
        const void* _pData;      ///< 밉 0 첫 행부터 (8 bytes)
        uint32      _sizeBytes;  ///< _pData 전체 길이 (4 bytes)
        uint32      _mipLevels;  ///< 업로드할 밉 수. 0 이면 텍스처가 가진 밉 전부 (4 bytes)
        uint32      _arraySlice; ///< 업로드할 면(배열 원소, 큐브 면). 한 번에 면 하나다. (4 bytes)

        /** @brief 빈 업로드(데이터 없음, 밉 전부)로 만듭니다. */
        RHITextureUploadDesc() noexcept;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RHITextureRegionUploadDesc
     * @brief `IRHIResourceFactory::submitTextureRegionUpload` 의 입력입니다. 밉 하나와 면 하나 안의 사각형 구간이며, 행은 빈틈없이(`_width × 픽셀 바이트`) 이어집니다.
     */
    struct RHITextureRegionUploadDesc
    {
        const void* _pData{ nullptr }; ///< 구간 첫 행부터
        uint32      _sizeBytes{ 0 };   ///< _pData 전체 길이. 행 바이트 × _height 이상이어야 한다.
        uint32      _x{ 0 };           ///< 밉 안의 왼쪽 위 X (px)
        uint32      _y{ 0 };           ///< 밉 안의 왼쪽 위 Y (px)
        uint32      _width{ 0 };       ///< 구간 너비 (px)
        uint32      _height{ 0 };      ///< 구간 높이 (px)
        uint32      _mip{ 0 };         ///< 업로드할 밉
        uint32      _arraySlice{ 0 };  ///< 업로드할 면(배열 원소, 큐브 면)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RHITransferTicket
     * @brief 제출한 전송 하나를 가리키는 단조 증가 시리얼과, 그것을 발급한 장치의 세대입니다. 시리얼 0 은 무효입니다.
     * @details 정수 별칭이 아니라 구조체라서 `uint64` 를 티켓 자리에 실수로 넘기는 일은 컴파일에서 막힌다. 멤버가 공개라 값을 직접 짓는 코드까지 막지는 못한다 — 그 틈은
     *          백엔드가 막는다: 시리얼 0, 다른 세대, 발급한 적 없는 시리얼은 `pollTransfer` 가 오류를 남기고 `Failed` 로 돌려준다(`RHITransferSerials::classify`).
     *          해제 함수가 없다 — 완료 워터마크가 시리얼을 덮는다. 읽기 티켓의 결과는 `takeTextureReadback` 으로 가져가거나 `discardTextureReadback` 으로 버린다.
     */
    struct RHITransferTicket
    {
        uint64 _serial{ 0 };           ///< 0 이면 무효
        uint32 _deviceGeneration{ 0 }; ///< 발급한 장치의 세대(`RHIBackendRegistry::createDevice` 가 붙인다). 장치가 바뀐 뒤의 옛 티켓을 가려낸다.

        /** @brief 제출이 받아들여져 발급된 티켓이면 true 입니다. */
        [[nodiscard]] constexpr bool isValid() const noexcept { return _serial != 0; }
        friend constexpr bool        operator==( const RHITransferTicket& lhs, const RHITransferTicket& rhs ) noexcept
        {
            return lhs._serial == rhs._serial && lhs._deviceGeneration == rhs._deviceGeneration;
        }
        friend constexpr bool operator!=( const RHITransferTicket& lhs, const RHITransferTicket& rhs ) noexcept { return ( lhs == rhs ) == false; }
    };

    /**
     * @struct RHITransferSubmit
     * @brief 전송 제출 함수의 반환값입니다. 결과를 버리면 컴파일 경고이고, 티켓은 `Accepted` 일 때만 유효합니다.
     * @details 결과를 보지 않으면 티켓도 얻을 수 없는 모양이라 "제출하고 잊기"가 쓰이지 않는다. `Busy` 의 재시도는 호출자가 다음 프레임에 한다.
     */
    struct [[nodiscard]] RHITransferSubmit
    {
        RHISubmitResult   _result{ RHISubmitResult::Rejected };
        RHITransferTicket _ticket{};

        /** @brief 받아들여졌고 티켓이 유효하면 true 입니다. */
        [[nodiscard]] constexpr bool isAccepted() const noexcept { return _result == RHISubmitResult::Accepted && _ticket.isValid(); }

        /**
         * @brief 받아들인 제출을 만듭니다. 시리얼 0 은 무효 티켓이라 받아들였다고 할 수 없다 — 백엔드가 0 을 넘기면 Debug 에서 단언이 걸린다.
         * @param deviceGeneration 발급한 장치의 세대입니다(`RHIBackendRegistry::createDevice` 가 붙인 값, 0 이 아니다).
         */
        [[nodiscard]] static RHITransferSubmit accepted( uint64 serial, uint32 deviceGeneration ) noexcept
        {
            SW_ASSERT( serial != 0 && deviceGeneration != 0 );
            return RHITransferSubmit{
                RHISubmitResult::Accepted, RHITransferTicket{ serial, deviceGeneration }
            };
        }
        /** @brief 스테이징이 모자라 받지 못한 결과입니다. */
        [[nodiscard]] static constexpr RHITransferSubmit busy() noexcept { return RHITransferSubmit{ RHISubmitResult::Busy, RHITransferTicket{} }; }
        /** @brief 검사에 걸려 거절한 결과입니다. */
        [[nodiscard]] static constexpr RHITransferSubmit rejected() noexcept { return RHITransferSubmit{ RHISubmitResult::Rejected, RHITransferTicket{} }; }
        /**
         * @brief 스테이징 할당이 `Accepted` 가 아닐 때 그 결과를 제출 결과로 옮깁니다. `Busy` 는 `busy()`, 나머지는 `rejected()` 입니다.
         * @details 네 백엔드가 `allocateStagingSpan` 의 결과를 이 한 함수로 옮긴다. `Accepted` 는 티켓이 필요해 부르는 쪽이 시리얼로 만든다(`RHITransferSerials::makeAccepted`) —
         *          `Accepted` 를 넘기면 Debug 에서 단언이 걸리고 `rejected()` 를 돌려준다(티켓 없는 `Accepted` 가 호출자에게 새지 않는다).
         */
        [[nodiscard]] static RHITransferSubmit fromUnaccepted( RHISubmitResult result ) noexcept
        {
            SW_ASSERT( result != RHISubmitResult::Accepted );
            return result == RHISubmitResult::Busy ? busy() : rejected();
        }
    };

    /**
     * @brief 영역 업로드를 검사하고 행 바이트를 구합니다. 네 백엔드가 같은 규칙을 쓰는 유일한 지점입니다.
     * @param mipLevels 텍스처가 가진 밉 수와 면 수(`arraySize`)입니다.
     * @return 압축, 깊이, Unknown 포맷이거나, 범위 밖의 밉, 면, 사각형이거나, 빈 구간이거나, 데이터가 모자라면 false 입니다(이유는 로그에 남깁니다).
     */
    [[nodiscard]] SW_API bool validateTextureRegionUpload( RHIFormat format, uint32 textureWidth, uint32 textureHeight, uint32 mipLevels, uint32 arraySize,
                                                           const RHITextureRegionUploadDesc& desc, uint32& outRowBytes );
} // namespace sw

namespace sw
{
    /**
     * @struct RHITextureMipSpan
     * @brief resolveTextureUploadMips 가 풀어낸 밉 하나의 위치와 크기입니다.
     */
    struct RHITextureMipSpan
    {
        const uint8* _pData{ nullptr }; ///< 이 밉의 첫 바이트
        uint32       _offsetBytes{ 0 }; ///< RHITextureUploadDesc::_pData 기준 오프셋
        uint32       _sizeBytes{ 0 };   ///< _rowBytes * _height
        uint32       _rowBytes{ 0 };    ///< 빈틈없는 한 행의 바이트
        uint32       _width{ 0 };
        uint32       _height{ 0 };
        uint32       _mip{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 텍스처 밉 하나의 크기와 행 바이트를 계산합니다. 업로드(빈틈없는 행)와 읽기(readback)가 같은 배치를 씁니다.
     * @details BC 는 행 하나가 블록 한 줄(ceil(w/4) 블록)이고, 밉 크기가 4 미만이어도 블록 하나를 차지한다.
     * @return 포맷이 대상이 아니거나 크기가 0 이면 false 입니다.
     */
    inline bool computeRHITextureMipLayout( RHIFormat format, uint32 width, uint32 height, uint32 mip, RHITextureMipSpan& outSpan )
    {
        const RHIFormatBlockInfo info                = getRHIFormatBlockInfo( format );
        const bool               bInvalidBlockLayout = info._blockBytes == 0 || width == 0 || height == 0;
        if ( bInvalidBlockLayout )
            return false;
        const uint32 mipWidth  = ( width >> mip ) > 0 ? ( width >> mip ) : 1u;
        const uint32 mipHeight = ( height >> mip ) > 0 ? ( height >> mip ) : 1u;
        const uint32 blocksX   = ( mipWidth + info._blockWidth - 1 ) / info._blockWidth;
        const uint32 blocksY   = ( mipHeight + info._blockHeight - 1 ) / info._blockHeight;
        outSpan._pData         = nullptr;
        outSpan._offsetBytes   = 0;
        outSpan._rowBytes      = blocksX * info._blockBytes;
        outSpan._sizeBytes     = outSpan._rowBytes * blocksY;
        outSpan._width         = mipWidth;
        outSpan._height        = mipHeight;
        outSpan._mip           = mip;
        return true;
    }

    /**
     * @brief 깊이 텍스처를 `submitTextureReadback` 으로 읽을 때 채우는 배치의 포맷입니다. 텍셀마다 0..1 깊이 float32 하나이고 스텐실은 버립니다.
     * @details 네 백엔드가 같은 배치를 내야 읽는 쪽(에디터 렌더 타깃 창)이 백엔드를 묻지 않는다. Vulkan 이 D32 계열 깊이를 골라도 같다.
     */
    inline constexpr RHIFormat kRHIDepthReadbackFormat = RHIFormat::R32_FLOAT;

    /**
     * @brief D24 깊이 텍셀(32비트, 아래 24비트가 깊이) @p count 개를 제자리에서 0..1 float32 로 바꿉니다. 위 8비트(스텐실 또는 미정)는 버립니다.
     * @details D3D11 과 D3D12 의 깊이 평면 복사, Vulkan `D24_UNORM_S8_UINT` 의 깊이 aspect 복사가 이 배치를 낸다.
     */
    inline void convertRHIDepth24ToFloat( uint8* pTexel, uint32 count )
    {
        constexpr uint32  kDepth24Mask = 0x00FFFFFFu;
        constexpr float32 kDepth24Max  = static_cast<float32>( kDepth24Mask );
        for ( uint32 texelIndex = 0; texelIndex < count; ++texelIndex )
        {
            uint32 packed = 0;
            Memory::copy( &packed, pTexel + static_cast<size_t>( texelIndex ) * 4u, sizeof( packed ) );
            const float32 depth = static_cast<float32>( packed & kDepth24Mask ) / kDepth24Max;
            Memory::copy( pTexel + static_cast<size_t>( texelIndex ) * 4u, &depth, sizeof( depth ) );
        }
    }

    /**
     * @brief 업로드 서술체를 밉 배열로 풀어냅니다. 네 백엔드가 같은 규칙으로 밉 크기와 오프셋을 계산해야 하므로 여기 한 곳에 둡니다.
     * @return 채운 밉 수입니다. 포맷이 업로드할 수 없거나, 요청한 밉이 텍스처의 밉보다 많거나, 데이터가 모자라면 0 입니다.
     */
    inline uint32 resolveTextureUploadMips( const RHITextureUploadDesc& desc, RHIFormat format, uint32 width, uint32 height,
                                            uint32 textureMipCount, RHITextureMipSpan* pOutSpan, uint32 outCapacity )
    {
        const bool bNoUploadData = desc._pData == nullptr || desc._sizeBytes == 0 || pOutSpan == nullptr;
        if ( bNoUploadData )
            return 0;

        const uint32 mipCount         = ( desc._mipLevels == 0 ) ? textureMipCount : desc._mipLevels;
        const bool   bInvalidMipCount = mipCount == 0 || mipCount > textureMipCount || mipCount > outCapacity;
        if ( bInvalidMipCount )
            return 0;

        const uint8* pBase  = static_cast<const uint8*>( desc._pData );
        uint32       offset = 0;
        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            RHITextureMipSpan& span = pOutSpan[mip];
            if ( computeRHITextureMipLayout( format, width, height, mip, span ) == false )
                return 0;
            if ( offset + span._sizeBytes > desc._sizeBytes )
                return 0;
            span._pData       = pBase + offset;
            span._offsetBytes = offset;
            offset += span._sizeBytes;
        }
        return mipCount;
    }

    /**
     * @struct RHIRenderPassBeginInfo
     * @brief 렌더 패스를 바인딩하고 시작할 때 넘기는 인자입니다.
     */
    struct SW_API RHIRenderPassBeginInfo
    {
        RHIRenderPassHandle    _renderPass;                                ///< 렌더 패스 핸들 (8 bytes)
        IRHISwapChain*         _pSwapChain;                                ///< 컬러 타깃 핸들 0 이 가리킬 백버퍼의 스왑체인. nullptr 이면 메인 창이다 (8 bytes)
        RHITextureHandle       _arrColorTarget[kMaxColorAttachments];      ///< RT별 핸들 (32 bytes)
        RHITextureHandle       _depthTarget;                               ///< 깊이/스텐실 핸들 (8 bytes)
        float4                 _arrClearColor[kMaxColorAttachments];       ///< RT별 클리어 색상 (64 bytes)
        uint32                 _colorTargetCount;                          ///< 컬러 RT 개수 (4 bytes)
        uint32                 _width;                                     ///< 렌더 영역 너비 (4 bytes)
        uint32                 _height;                                    ///< 렌더 영역 높이 (4 bytes)
        float32                _clearDepth;                                ///< 깊이 클리어 값 (4 bytes)
        RHIRenderPassLoadOp    _arrLoadOp[kMaxColorAttachments];           ///< RT별 로드 동작 (16 bytes)
        RHIRenderPassLoadOp    _depthLoadOp;                               ///< 깊이 로드 동작 (4 bytes)
        uint16                 _arrColorTargetSlice[kMaxColorAttachments]; ///< RT별로 그릴 면(배열 원소, 큐브 면). 2D 는 0 (8 bytes)
        uint16                 _depthTargetSlice;                          ///< 깊이 타깃의 그릴 면 (2 bytes)
        uint8                  _clearStencil;                              ///< 스텐실 클리어 값. 깊이 로드가 Clear 일 때 함께 쓴다 (1 byte)
        uint8                  _bBindColor    : 1;                         ///< 컬러 바인딩 여부 (1 byte)
        [[maybe_unused]] uint8 _reservedFlags : 7;
        RHIRenderPassStoreOp   _arrStoreOp[kMaxColorAttachments]; ///< RT별 저장 동작 (16 bytes)
        RHIRenderPassStoreOp   _depthStoreOp;                     ///< 깊이 · 스텐실 저장 동작 (4 bytes)

        /** @brief 기본값(렌더 패스 0, 컬러 타깃 0개, 로드 동작 Clear, 깊이 클리어 1.0)으로 만듭니다. */
        RHIRenderPassBeginInfo() noexcept;

        /** @brief 렌더 타깃 하나를 설정하는 헬퍼입니다. */
        void setColorTarget( RHITextureHandle target, const float4& clearColor = kDefaultClearColor, RHIRenderPassLoadOp loadOp = RHIRenderPassLoadOp::Clear );
    };

} // namespace sw
