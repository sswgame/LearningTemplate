/**
 * @file RHITypes.h
 * @brief RHI 공통 핸들, 열거형, 서술체
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Common/Common.h"
#include "Engine/Common/EngineDefines.h"
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 핸들 — 버퍼/텍스처/디스크립터, 무효 인덱스
    // ------------------------------------------------------------------------------
    /** @brief GPU 버퍼 리소스 64비트 핸들 */
    using RHIBufferHandle = uint64;

    /** @brief GPU 텍스처 리소스 64비트 핸들 */
    using RHITextureHandle = uint64;

    /** @brief Bindless 리소스 인덱스 (Descriptor Heap / Set 내 바인딩 인덱스) */
    using RHIDescriptorIndex = uint32;

    /** @brief 유효하지 않은 Descriptor 인덱스 */
    constexpr RHIDescriptorIndex kInvalidDescriptorIndex = invalid_index::kUint32;

    /**
     * @struct RHIVertex
     * @brief 기본 3D 정점 (직접 그리기 예제용)
     */
    struct RHIVertex
    {
        float32 _arrPosition[3]; ///< 정점 위치 (X, Y, Z)
        /**
         * @brief 정점 노멀 (X, Y, Z) — 정규화되어 있어야 합니다.
         * @details 예전에는 없었고, 셰이더가 위치로 **지어내고** 있었다(`DemoCubeNormal`) — 원점 중심
         *          박스형 도형에만 맞는 함수라 평면·구·원뿔은 조용히 틀린 빛을 받았다. 바닥 평면을
         *          `y = 0` 에 두면 `|y|` 가 0 이라 ±X/±Z 노멀이 나와 바닥이 옆을 보는 것처럼 칠해졌다.
         */
        float32 _arrNormal[3];
        /**
         * @brief 텍스처 좌표 (U, V).
         * @details 노멀과 **같은 함정**이었다 — 셰이더가 `localPos.xy * 0.5 + 0.5` 로 지어내고 있어서,
         *          원점 중심 단위 도형이 아니면 알베도 텍스처가 엉뚱하게 붙었고 도형의 옆면·뚜껑은
         *          아예 같은 자리를 물고 있었다. 도형 생성기가 면마다 제대로 펼쳐 준다.
         */
        float32 _arrUv[2];
        float32 _arrColor[4]; ///< 정점 색상 (R, G, B, A)
    };

    /**
     * @struct RHIVertexAttribute
     * @brief 정점 속성 하나의 선언 — 네 백엔드가 **같은 표**를 읽어 각자의 입력 레이아웃을 만듭니다.
     * @details 예전에는 DX11·DX12·Vulkan·GL 이 이 표를 각자 손으로 적고 있었다. 속성을 하나 더하려면
     *          네 곳을 같이 고쳐야 하고, 한 곳을 빠뜨리면 그 백엔드만 조용히 다른 그림을 낸다 —
     *          이 저장소에서 가장 비싼 종류의 버그다. 표를 하나로 두면 그럴 자리가 없다.
     * @note `_location` 은 HLSL 선언 **순서**와 같아야 한다(Vulkan location · GL 정점 속성 번호).
     *       DX 는 시맨틱 이름으로 묶으므로 `_pSemanticName` 이 그 역할을 한다.
     */
    struct RHIVertexAttribute
    {
        const utf8* _pSemanticName;            ///< DX 시맨틱 이름. HLSL 의 `: POSITION` 등과 같아야 한다.
        uint32      _location;                 ///< Vulkan location / GL 정점 속성 번호.
        uint32      _componentCount;           ///< 원소 개수 (1·2·3·4). 백엔드가 자기 포맷 enum 으로 옮긴다.
        uint32      _byteOffset;               ///< 그 슬롯 원소 안의 바이트 오프셋 (슬롯 0 은 `RHIVertex`).
        uint32      _inputSlot{ 0 };           ///< 정점 버퍼 슬롯. 0 = 메시 정점, 1 = 인스턴스 슬롯 스트림 (`constant::kInstanceSlotStreamSlot`).
        uint8       _bPerInstance{ SW_FALSE }; ///< 인스턴스마다 한 원소를 읽는다 (step rate 1).
        uint8       _bUint{ SW_FALSE };        ///< 32비트 부호 없는 정수(R32_UINT). 아니면 float.
    };

    // ------------------------------------------------------------------------------
    // 2) 백엔드 · 포맷 — API 종류, 픽셀 포맷
    // ------------------------------------------------------------------------------
    /**
     * @enum RHIFormat
     * @brief 텍스처·렌더 타깃·픽셀 데이터 포맷
     */
    ENUM()
    enum class RHIFormat : uint32
    {
        R8G8B8A8_UNORM     = 0, ///< 8비트 RGBA 정규화
        B8G8R8A8_UNORM     = 1, ///< 8비트 BGRA 정규화 (DirectX 기본)
        R16G16B16A16_FLOAT = 2, ///< 16비트 부동소수점 RGBA (HDR)
        D24_UNORM_S8_UINT  = 3, ///< 24비트 깊이 + 8비트 스텐실
        R32G32B32_FLOAT    = 4, ///< 32비트 부동소수점 RGB (위치/노멀)
        R32G32_FLOAT       = 5, ///< 32비트 부동소수점 RG (UV)
        R32_FLOAT          = 6, ///< 32비트 단일 부동소수점
        /// @brief 첨부 없음. 뎁스를 쓰지 않는 패스의 PSO 가 "DSV 없음" 을 선언하는 데 쓴다 —
        ///        DX12 는 null DSV 를 PSO 뎁스 포맷이 UNKNOWN 일 때만 허용한다.
        ///        기존 값의 번호는 바꾸지 않는다 — 새 포맷은 아래처럼 뒤에 이어 붙인다.
        Unknown = 7,
        // 블록 압축(4x4 블록). 렌더 타깃 불가, 샘플링·업로드·읽기 전용. DDS 의 DXT1/3/5·BC4/5/7 과 1:1.
        BC1_UNORM = 8,  ///< 8 B/블록, RGB + 1비트 알파
        BC2_UNORM = 9,  ///< 16 B/블록, 명시 알파
        BC3_UNORM = 10, ///< 16 B/블록, 보간 알파
        BC4_UNORM = 11, ///< 8 B/블록, 단일 채널
        BC5_UNORM = 12, ///< 16 B/블록, 2채널(노멀맵)
        BC7_UNORM = 13, ///< 16 B/블록, 고품질 RGBA
    };

    namespace constant
    {
        // ------------------------------------------------------------------------------
        // 백엔드 간 "값이 같아야 하는" 계약 상수.
        // 지금 값이 우연히 같더라도 바뀔 수 있는 값이면 반드시 여기로 뺀다 — 한쪽 백엔드만
        // 바뀌면 컴파일은 통과하고 런타임에 조용히 깨진다(아래 kBackBufferFormat 사례).
        // 이 블록이 RHIFormat 바로 뒤에 있는 이유는 RHISwapChainDesc 의 기본값으로 쓰이기 때문이다.
        // ------------------------------------------------------------------------------

        /**
         * @brief 백버퍼 컬러 포맷 — 4개 백엔드 스왑체인과 파이프라인 RTV 기본값이 같아야 한다.
         * @details 파이프라인은 `RHIPipelineStateDesc::_arrRtvFormat` 으로 렌더패스/PSO 를 만든다.
         *          스왑체인이 다른 포맷을 고르면 백버퍼에 직접 그리는 패스가 전부 비호환이 된다 —
         *          실제로 Vulkan 만 `B8G8R8A8_UNORM` 을 고르고 있어서, 에디터 없이 실행하는 경로가
         *          렌더패스 비호환으로 깨져 있었다(docs/05_RHI_FrameContract.md 실패기록 5차).
         *          백엔드가 이 포맷을 낼 수 없으면 조용히 다른 걸 고르지 말고
         *          `IRHIDevice::getBackBufferFormat()` 으로 실제 채택한 값을 보고해야 한다.
         */
        inline constexpr RHIFormat kBackBufferFormat = RHIFormat::R8G8B8A8_UNORM;

        /**
         * @brief 오프스크린 컬러 타깃(에디터 게임뷰 등) 기본 포맷.
         * @details Vulkan 은 이 포맷일 때만 공용 오프스크린 렌더패스를 재사용하고 나머지는 전용
         *          렌더패스를 만든다 — 값이 갈라지면 조용히 렌더패스가 늘어나거나 비호환이 된다.
         */
        inline constexpr RHIFormat kOffscreenColorFormat = RHIFormat::R8G8B8A8_UNORM;

        /**
         * @brief 깊이/스텐실 기본 포맷.
         * @details DX11/DX12/GL 은 이 값을 그대로 쓴다. Vulkan 은 물리 디바이스가 미지원이면
         *          대체 포맷을 고르고(`depthFormat()`), 파이프라인 렌더패스도 그 값으로 맞춘다.
         */
        inline constexpr RHIFormat kDepthStencilFormat = RHIFormat::D24_UNORM_S8_UINT;

        /**
         * @brief 상수버퍼 슬롯 정렬(바이트).
         * @details 4개 백엔드가 같은 값으로 슬롯 크기를 계산해야 프레임 링 오프셋이 어긋나지 않는다.
         *          D3D12 의 요구치(256)가 가장 크므로 그걸 공통값으로 쓴다. 텍스처 행 정렬
         *          (`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT`)은 이름만 같은 별개 개념이니 섞지 말 것.
         */
        inline constexpr uint32 kConstantBufferAlignment = 256;

        /**
         * @brief 인스턴스 슬롯 스트림이 걸리는 정점 버퍼 슬롯. 원소는 uint 하나 — 그 드로우 인스턴스의 **전역 인스턴스 자리**.
         * @details 씬 드로우는 인스턴스마다 자기 자리(가시 목록 슬롯)를 알아야 인스턴스 버퍼를 읽는다. 예전엔 배치마다 루트 상수로
         *          시작 오프셋을 싣고 SV_InstanceID 를 더했는데, 그러면 배치마다 루트 상수를 바꿔야 해서 같은 PSO 의 배치들을
         *          멀티 드로우 하나로 낼 수 없었다. 대신 슬롯 1 에 `0,1,2,…` 스트림을 인스턴스 스텝으로 걸고 간접 인자의
         *          startInstance 를 배치 시작으로 두면, 입력 어셈블러가 네 API 모두에서 `startInstance + i` 번째 원소를 준다 —
         *          SV_InstanceID 가 startInstance 를 포함하는지(API 마다 다르다)에 기대지 않는다. 언리얼 D3D11 경로의
         *          인스턴스 ID 스트림과 같은 자리다.
         */
        inline constexpr uint32 kInstanceSlotStreamSlot   = 1;
        inline constexpr uint32 kInstanceSlotStreamStride = static_cast<uint32>( sizeof( uint32 ) );

        /**
         * @brief 정점 입력 레이아웃의 **정본** — 네 백엔드가 이 표만 읽는다.
         * @details 순서가 곧 HLSL 의 선언 순서이고 `_location` 이다. 속성을 더하려면 여기 한 줄과
         *          `RHIVertex` 멤버 하나만 고치면 되고, 백엔드는 손대지 않는다.
         * @note 오프셋을 손으로 적지 않는다 — `SW_OFFSET_OF` 라 구조체를 바꾸면 자동으로 따라온다.
         *       예전에 네 백엔드가 각자 `0` 과 `12` 를 적어 두고 있었다.
         */
        inline constexpr RHIVertexAttribute arrVertexAttribute[] = {
            { "POSITION", 0, 3, SW_OFFSET_OF( RHIVertex, _arrPosition ), 0, SW_FALSE, SW_FALSE },
            { "NORMAL", 1, 3, SW_OFFSET_OF( RHIVertex, _arrNormal ), 0, SW_FALSE, SW_FALSE },
            { "TEXCOORD", 2, 2, SW_OFFSET_OF( RHIVertex, _arrUv ), 0, SW_FALSE, SW_FALSE },
            { "COLOR", 3, 4, SW_OFFSET_OF( RHIVertex, _arrColor ), 0, SW_FALSE, SW_FALSE },
            { "SW_INSTANCESLOT", 4, 1, 0, kInstanceSlotStreamSlot, SW_TRUE, SW_TRUE },
        };
        /// @brief 정점 속성 수. 백엔드가 배열 크기를 직접 세지 않도록 함께 둔다.
        inline constexpr uint32 kVertexAttributeCount = static_cast<uint32>( sizeof( arrVertexAttribute ) / sizeof( arrVertexAttribute[0] ) );

        /** @brief uploadTexture2D 가 한 번에 받는 밉 수 상한 — 2^16 텍스처의 전체 체인(17단) 을 덮는다. */
        inline constexpr uint32 kMaxTextureMipCount = 17;

        /**
         * @brief CPU 가 GPU 를 앞서갈 수 있는 최대 프레임 수 (= 프레임별 리소스 링 슬롯 수).
         * @details **이 개념의 유일한 출처다.** 프레임마다 따로 있어야 하는 자원 — DX12 커맨드
         *          얼로케이터·업로드 슬롯, Vulkan 커맨드버퍼·펜스·상수버퍼 슬롯·디스크립터 셋,
         *          에디터 draw 스냅샷 — 이 전부 이 값으로 크기를 잡는다.
         *          예전엔 이 값과 `FrameResourceRing::kFrameCount` 두 상수가 각각 2/3 으로 따로
         *          있었고 Vulkan 이 둘을 섞어 썼다(상수버퍼는 3슬롯, 커맨드버퍼는 2개) — 값이 작아서
         *          우연히 맞았을 뿐이고, 한쪽만 올리면 디스크립터가 버퍼 밖을 가리켰다. 그래서 별칭도
         *          두지 않는다 — 같은 개념에 이름이 둘이면 같은 사고가 다시 난다.
         */
        inline constexpr uint32 kMaxFrameCountInFlight = 3;

        /**
         * @brief 프레임 하나에 찍을 수 있는 GPU 타임스탬프 칸 수.
         * @details 패스 인덱스로 칸을 고정해 쓴다(패스당 begin/end 두 칸) — 4개 백엔드가 같은 값으로
         *          쿼리 힙·풀·쿼리 배열 크기를 잡으므로 한쪽만 바꾸면 다른 쪽이 구간 밖을 읽는다.
         *          32 칸 = 패스 16 개까지. 렌더 그래프가 그보다 길어지면 뒤쪽 패스는 조용히 빠진다.
         */
        inline constexpr uint32 kMaxGpuTimestampSlot = 32;

        /**
         * @brief GPU 리소스 지연 해제 프레임 수 (RHIReleaseQueue 기본 frameLatency).
         * @details 4개 RHI 백엔드(DX11/DX12/Vulkan/OpenGL)가 전부 같은 값을 써야 하는 계약 —
         *          한쪽만 바꾸면 아직 GPU가 참조 중인 리소스를 조기 해제할 위험이 있다.
         */
        inline constexpr uint32 kGpuReleaseFrameLatency = 3;

        /**
         * @brief 게임 스레드가 만든 프레임 패킷이 렌더 스레드에 소비되기까지 큐잉될 수 있는 최대
         *        프레임 수 (RenderThread 패킷 링 깊이).
         * @details 아직 큐잉된(소비되지 않은) 패킷이 참조할 수 있는 자원은 최소 이 프레임 수만큼
         *          해제를 미뤄야 한다 (예: GpuScene 의 머티리얼 원소 회수 지연). GPU 인플라이트
         *          값인 kMaxFrameCountInFlight 와는 별개 개념 — 혼동하지 말 것.
         */
        inline constexpr uint32 kRenderFrameQueueDepth = 3;
    } // namespace constant

    // ------------------------------------------------------------------------------
    // 3) 스왑체인 · 뷰포트 · 인디렉트 커맨드
    // ------------------------------------------------------------------------------
    /**
     * @struct RHISwapChainDesc
     * @brief 윈도우 스왑체인 생성 설정
     */
    REFLECT()
    struct RHISwapChainDesc
    {
        REFLECT_BODY();
        PROPERTY()
        void* _pWindowHandle{ nullptr }; ///< OS 윈도우 핸들 (HWND, Window XID 등)

        PROPERTY()
        void* _pWindowDisplay{ nullptr }; ///< X11 Display 포인터 (리눅스 전용)

        PROPERTY()
        uint32 _width{ 1280 }; ///< 스왑체인 너비 (픽셀)

        PROPERTY()
        uint32 _height{ 720 }; ///< 스왑체인 높이 (픽셀)

        PROPERTY()
        uint32 _bufferCount{ 2 }; ///< 프레임버퍼 개수 (Double/Triple Buffering)

        /// @brief 백버퍼 컬러 포맷. 백엔드는 이 값을 존중해야 하고, 못 내면 실제 값을 보고해야 한다.
        RHIFormat _format{ constant::kBackBufferFormat };

        PROPERTY()
        bool _bVSync{ true }; ///< 수직 동기화

        PROPERTY()
        bool _bFullscreen{ false }; ///< 전체 화면
    };

    /**
     * @struct RHIViewport
     * @brief 렌더링 뷰포트 영역 (DirectX 규약 — 전 백엔드 동일)
     * @details 원점은 좌상단, 픽셀 +Y는 아래. 클립/NDC +Y는 위(Direct3D).
     *          Vulkan은 내부에서 negative-height viewport로 변환하고,
     *          OpenGL은 glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)로 맞춘다.
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
        float32 _width{ constant::kDefaultViewportWidth }; ///< 뷰포트 너비

        PROPERTY()
        float32 _height{ constant::kDefaultViewportHeight }; ///< 뷰포트 높이 (양수; Vulkan이 부호 반전)

        PROPERTY()
        float32 _minDepth{ 0.0f }; ///< 최소 깊이 (0.0~1.0)

        PROPERTY()
        float32 _maxDepth{ 1.0f }; ///< 최대 깊이 (0.0~1.0)
    };

    /**
     * @struct RHIDrawIndirectCommand
     * @brief 간접 드로우 파라미터
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
     * @brief 간접 컴퓨트 디스패치 파라미터
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

    /** @brief 파이프라인 상태 객체(PSO) 64비트 핸들 */
    using RHIPipelineStateHandle = uint64;

    /** @brief 렌더 패스 객체 64비트 핸들 */
    using RHIRenderPassHandle = uint64;

    // ------------------------------------------------------------------------------
    // 4) 버퍼 — usage 플래그, 상태, 서술체, 인덱스 인디렉트
    // ------------------------------------------------------------------------------
    /**
     * @enum RHIBufferUsage
     * @brief 범용 GPU 버퍼 usage 플래그 (createBuffer)
     */
    ENUM( Flags )
    enum class RHIBufferUsage : uint8
    {
        None            = 0,
        Vertex          = SW_BIT( 0 ),
        Index           = SW_BIT( 1 ),
        Constant        = SW_BIT( 2 ),
        Structured      = SW_BIT( 3 ),
        UnorderedAccess = SW_BIT( 4 ),
        ShaderResource  = SW_BIT( 5 ),
        IndirectArgs    = SW_BIT( 6 ),
        Raw             = SW_BIT( 7 ), ///< 바이트 주소 / RAW UAV (DX11 DRAWINDIRECT_ARGS 호환)
    };

    /**
     * @enum RHIBufferState
     * @brief transitionBuffer 배리어용 논리 버퍼 상태
     */
    enum class RHIBufferState : uint32
    {
        Common           = 0,
        ShaderResource   = 1,
        UnorderedAccess  = 2,
        IndirectArgument = 3,
        CopyDest         = 4,
        VertexOrConstant = 5,
        Index            = 6,
    };

    /**
     * @struct RHIBufferCopyRegion
     * @brief 버퍼 부분 갱신의 한 조각 — 원본 블롭 안의 위치와 목적 버퍼 안의 위치, 그리고 크기.
     * @details 여러 조각을 **한 번의 호출**로 넘기기 위한 것이다. 조각마다 따로 부르면 백엔드가
     *          스테이징 확보와 큐 제출을 그만큼 되풀이한다 (DX12 에서 호출당 ~3.3 us 였다).
     */
    struct RHIBufferCopyRegion
    {
        uint32 _srcOffset{ 0 }; ///< 넘긴 원본 포인터 기준 바이트 오프셋
        uint32 _dstOffset{ 0 }; ///< 목적 버퍼 안의 바이트 오프셋
        uint32 _size{ 0 };      ///< 옮길 바이트 수
    };

    /**
     * @struct RHIBufferDesc
     * @brief 범용 버퍼 생성 서술체
     */
    struct RHIBufferDesc
    {
        uint32         _sizeBytes{ 0 };
        uint32         _elementSize{ 0 }; ///< Structured stride. raw/바이트 버퍼는 0
        uint32         _elementCount{ 0 };
        RHIBufferUsage _usage        = RHIBufferUsage::None;
        const void*    _pInitialData = nullptr;
    };

    /**
     * @struct RHIDrawIndexedIndirectCommand
     * @brief 인덱스 인디렉트 드로우 인자 (D3D12/Vulkan 레이아웃과 동일)
     */
    struct RHIDrawIndexedIndirectCommand
    {
        uint32 _indexCountPerInstance{ 0 };
        uint32 _instanceCount{ 1 };
        uint32 _startIndexLocation{ 0 };
        int32  _baseVertexLocation{ 0 };
        uint32 _startInstanceLocation{ 0 };
    };

    // ------------------------------------------------------------------------------
    // 5) 파이프라인 상태 — 블렌드, 토폴로지, 컬링, 로드/스토어
    // ------------------------------------------------------------------------------
    /**
     * @enum RHIBlendMode
     * @brief 머티리얼/패스 블렌드 분류
     */
    enum class RHIBlendMode : uint8
    {
        Opaque      = 0,
        Transparent = 1, ///< SrcAlpha / InvSrcAlpha
    };

    /**
     * @enum RHIPrimitiveTopology
     * @brief 도형 출력 위상
     */
    enum class RHIPrimitiveTopology
    {
        TriangleList, ///< 삼각형 리스트
        LineList,     ///< 선 리스트
        PointList     ///< 점 리스트
    };

    /**
     * @enum RHIFillMode
     * @brief 래스터라이저 와이어프레임 / 솔리드
     */
    enum class RHIFillMode
    {
        Solid,    ///< 일반 채우기
        Wireframe ///< 와이어프레임
    };

    /**
     * @enum RHICullMode
     * @brief 페이스 컬링
     */
    enum class RHICullMode
    {
        None,  ///< 컬링 없음
        Front, ///< 전면 컬링
        Back   ///< 후면 컬링
    };

    /**
     * @enum RHIRenderPassLoadOp
     * @brief 렌더 패스 시작 시 프레임버퍼 로드
     */
    enum class RHIRenderPassLoadOp
    {
        Clear,   ///< 기존 데이터 지우기
        Load,    ///< 기존 데이터 유지
        DontCare ///< 이전 내용 무시 (최적화)
    };

    /**
     * @enum RHIRenderPassStoreOp
     * @brief 렌더 패스 완료 시 프레임버퍼 저장
     */
    enum class RHIRenderPassStoreOp
    {
        Store,   ///< 메모리에 최종 결과 저장
        DontCare ///< 결과 보존 안 함
    };

    /** @brief beginRenderPass / PSO에서 동시 컬러 RT 최대 개수 (MRT). */
    inline constexpr uint32 kMaxColorAttachments = 4;

    namespace constant
    {
        /**
         * @brief setComputeRootConstants가 4개 백엔드 모두에서 안전하게 쓸 수 있는 최대 dword 수.
         * @details DX11=64(D3D11RHIDevice.h)/OpenGL=64(OpenGLRHIDevice.h)는 UBO 로 에뮬한다. DX12/Vulkan 은
         *          루트/푸시 상수 16 dword 다(bindingslots.hlsli 의 SW_ROOT_DWORD_COUNT). 가장 작은 값을 공통 안전값으로 둔다.
         */
        inline constexpr uint32 kMinComputeRootConstantDwords = 16;
    } // namespace constant

    /**
     * @struct RHIPipelineStateDesc
     * @brief Graphics & Compute 파이프라인 상태 생성 서술체
     */
    struct SW_API RHIPipelineStateDesc
    {
        string _vertexShaderPath;  ///< 버텍스 셰이더 소스 경로
        string _vertexEntryPoint;  ///< 버텍스 셰이더 진입점
        string _pixelShaderPath;   ///< 픽셀 셰이더 소스 경로
        string _pixelEntryPoint;   ///< 픽셀 셰이더 진입점
        string _computeShaderPath; ///< 컴퓨트 셰이더 소스 경로
        string _computeEntryPoint; ///< 컴퓨트 셰이더 진입점

        vector<string> _listShaderDefine; ///< 컴파일 매크로 ("NAME" 또는 "NAME=VALUE") — 셰이더 permutation

        RHIPrimitiveTopology   _topology;                           ///< 프리미티브 위상
        RHIFillMode            _fillMode;                           ///< 채우기 모드
        RHICullMode            _cullMode;                           ///< 컬링 모드
        uint32                 _numRenderTargets;                   ///< 컬러 RT 개수 (MRT)
        RHIFormat              _arrRtvFormat[kMaxColorAttachments]; ///< RT별 포맷
        RHIFormat              _depthStencilFormat;
        uint8                  _bEnableDepthTest  : 1; ///< 깊이 테스트
        uint8                  _bEnableDepthWrite : 1; ///< 깊이 쓰기 (Transparent=0)
        uint8                  _bEnableBlend      : 1; ///< 알파 블렌딩 (SrcAlpha/InvSrcAlpha)
        [[maybe_unused]] uint8 _reservedFlags     : 5;

        /** @brief 기본 토폴로지/컬링/깊이 플래그. */
        RHIPipelineStateDesc() noexcept;
    };

    /** @brief 기본 렌더 타깃 초기화 색상 (RGBA) */
    inline constexpr float4 kDefaultClearColor = { 0.1f, 0.1f, 0.1f, 1.0f };

    /**
     * @struct RHIRenderPassAttachment
     * @brief 렌더 패스 색상/깊이 어태치먼트
     */
    struct RHIRenderPassAttachment
    {
        float4               _clearColor = kDefaultClearColor;          ///< 초기화 색상 (RGBA)
        RHIFormat            _format     = RHIFormat::R8G8B8A8_UNORM;   ///< 어태치먼트 포맷
        RHIRenderPassLoadOp  _loadOp     = RHIRenderPassLoadOp::Clear;  ///< 로드 동작
        RHIRenderPassStoreOp _storeOp    = RHIRenderPassStoreOp::Store; ///< 저장 동작
    };

    /**
     * @struct RHIRenderPassDesc
     * @brief 렌더 패스 생성 정보
     */
    struct SW_API RHIRenderPassDesc
    {
        vector<RHIRenderPassAttachment> _listColorAttachment;  ///< 색상 어태치먼트 목록 (24 bytes)
        float32                         _clearDepth;           ///< 깊이 초기화 값 (4 bytes)
        uint8                           _clearStencil;         ///< 스텐실 초기화 값 (1 byte)
        uint8                           _bHasDepthStencil : 1; ///< 깊이/스텐실 어태치먼트 포함 (1 byte)
        [[maybe_unused]] uint8          _reservedFlags    : 7;
        uint8                           _arrReserved[2]; ///< 8바이트 정렬 패딩 (2 bytes)

        /** @brief 깊이 클리어 기본값. */
        RHIRenderPassDesc() noexcept;
    };

    /**
     * @struct RHITextureDesc
     * @brief 텍스처 생성 서술체
     */
    struct SW_API RHITextureDesc
    {
        float4 _clearColor; ///< 초기화 색상 (16 bytes)
        uint32 _width;      ///< 너비 (4 bytes)
        uint32 _height;     ///< 높이 (4 bytes)
        uint32 _depth;      ///< 깊이 (4 bytes)
        /**
         * @brief 밉 레벨 수 (4 bytes). **1 이상을 주십시오 — 0 은 백엔드마다 뜻이 다릅니다.**
         * @details 바로 아래 `RHITextureUploadDesc::_mipLevels` 는 0 을 "텍스처가 가진 밉 전부" 로
         *          정의하지만 **이쪽은 그런 약속이 없다.** D3D11 은 0 을 "전체 밉 체인을 만들어라"
         *          로 읽고, GL 은 `mipLevels > 0 ? mipLevels : 1` 로 접어 **1단계만** 만든다 —
         *          같은 값이 백엔드마다 다른 텍스처를 낳는다. 기본 생성자가 1 을 넣고 호출부도
         *          전부 1 이상을 주므로 지금은 닿지 않지만, 두 필드의 이름이 같아서 한쪽 규약을
         *          다른 쪽에 옮겨 적기 쉽다.
         */
        uint32                 _mipLevels;
        RHIFormat              _format;                 ///< 텍스처 포맷 (4 bytes)
        float32                _clearDepth;             ///< 초기화 깊이 (4 bytes)
        uint8                  _clearStencil;           ///< 초기화 스텐실 (1 byte)
        uint8                  _bIsRenderTarget    : 1; ///< 렌더 타깃 지원 여부 (1 byte)
        uint8                  _bIsDepthStencil    : 1; ///< 깊이/스텐실 지원 여부
        uint8                  _bIsShaderResource  : 1; ///< 셰이더 리소스 지원 여부
        uint8                  _bIsUnorderedAccess : 1; ///< UAV 지원 여부
        [[maybe_unused]] uint8 _reservedFlags      : 4;
        uint8                  _arrReserved[2]; ///< 4바이트 정렬 패딩 (2 bytes)

        /** @brief 기본 크기/포맷/클리어. */
        RHITextureDesc() noexcept;
    };

    /**
     * @struct RHITextureUploadDesc
     * @brief IRHIResource::uploadTexture2D 입력 — 밉 0 부터 차례로, 각 밉의 행이 빈틈없이 이어진 바이트 블록
     * @details DDS 파일의 픽셀 배치 그대로다(DdsImageData::_bytes 를 그대로 넘길 수 있다). 행 패딩은
     *          백엔드가 필요하면 스스로 맞춘다(DX12 는 256 정렬 풋프린트로 다시 배치, GL 은 UNPACK_ALIGNMENT 1).
     */
    struct SW_API RHITextureUploadDesc
    {
        const void* _pData;     ///< 밉 0 첫 행부터 (8 bytes)
        uint32      _sizeBytes; ///< _pData 전체 길이 (4 bytes)
        uint32      _mipLevels; ///< 올릴 밉 수. 0 이면 텍스처가 가진 밉 전부 (4 bytes)

        /** @brief 빈 업로드(데이터 없음, 밉 전부). */
        RHITextureUploadDesc() noexcept;
    };

    /**
     * @struct RHITextureMipSpan
     * @brief resolveTextureUploadMips 가 풀어낸 밉 하나의 위치와 크기
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

    /**
     * @struct RHIFormatBlockInfo
     * @brief 포맷의 저장 단위 — 비압축은 1x1 블록에 픽셀 바이트, BC 는 4x4 블록에 8/16 바이트
     */
    struct RHIFormatBlockInfo
    {
        uint32 _blockWidth{ 1 };
        uint32 _blockHeight{ 1 };
        uint32 _blockBytes{ 0 }; ///< 0 = 업로드·읽기 대상이 아님(깊이/Unknown)
    };

    /** @brief 포맷의 블록 정보. 4개 백엔드가 밉 크기·행 바이트를 같은 규칙으로 계산하는 유일한 출처다. */
    inline constexpr RHIFormatBlockInfo getRhiFormatBlockInfo( RHIFormat format )
    {
        switch ( format )
        {
            case RHIFormat::R8G8B8A8_UNORM:
            case RHIFormat::B8G8R8A8_UNORM:
            case RHIFormat::R32_FLOAT:
                return RHIFormatBlockInfo{ 1, 1, 4 };
            case RHIFormat::R16G16B16A16_FLOAT:
            case RHIFormat::R32G32_FLOAT:
                return RHIFormatBlockInfo{ 1, 1, 8 };
            case RHIFormat::R32G32B32_FLOAT:
                return RHIFormatBlockInfo{ 1, 1, 12 };
            case RHIFormat::BC1_UNORM:
            case RHIFormat::BC4_UNORM:
                return RHIFormatBlockInfo{ 4, 4, 8 };
            case RHIFormat::BC2_UNORM:
            case RHIFormat::BC3_UNORM:
            case RHIFormat::BC5_UNORM:
            case RHIFormat::BC7_UNORM:
                return RHIFormatBlockInfo{ 4, 4, 16 };
            case RHIFormat::D24_UNORM_S8_UINT:
            case RHIFormat::Unknown:
            default:
                return RHIFormatBlockInfo{ 1, 1, 0 };
        }
    }

    /** @brief 블록 압축 포맷인가. */
    inline constexpr bool isRhiFormatBlockCompressed( RHIFormat format )
    {
        return getRhiFormatBlockInfo( format )._blockWidth > 1;
    }

    /** @brief 비압축 컬러 포맷의 픽셀당 바이트. 압축/깊이/Unknown 은 0. */
    inline constexpr uint32 getRhiFormatBytesPerPixel( RHIFormat format )
    {
        const RHIFormatBlockInfo info = getRhiFormatBlockInfo( format );
        return info._blockWidth == 1 ? info._blockBytes : 0;
    }

    /**
     * @brief 텍스처 밉 하나의 크기와 행 바이트를 계산합니다 — 업로드(빈틈없는 행)와 읽기(readback)가 같은 배치를 쓴다.
     * @details BC 는 행 하나가 블록 한 줄(ceil(w/4) 블록)이고, 밉 크기가 4 미만이어도 블록 하나를 차지한다.
     * @return 포맷이 대상이 아니거나 크기가 0 이면 false.
     */
    inline bool computeRhiTextureMipLayout( RHIFormat format, uint32 width, uint32 height, uint32 mip, RHITextureMipSpan& outSpan )
    {
        const RHIFormatBlockInfo info = getRhiFormatBlockInfo( format );
        if ( info._blockBytes == 0 || width == 0 || height == 0 )
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
     * @brief 업로드 서술체를 밉 배열로 풉니다. 4개 백엔드가 같은 규칙으로 밉 크기·오프셋을 계산해야 하므로 여기 한 곳에 둔다.
     * @return 채운 밉 수. 포맷이 업로드 불가이거나, 요청 밉이 텍스처 밉보다 많거나, 데이터가 모자라면 0.
     */
    inline uint32 resolveTextureUploadMips( const RHITextureUploadDesc& desc, RHIFormat format, uint32 width, uint32 height,
                                            uint32 textureMipCount, RHITextureMipSpan* pOutSpan, uint32 outCapacity )
    {
        if ( desc._pData == nullptr || desc._sizeBytes == 0 || pOutSpan == nullptr )
            return 0;

        const uint32 mipCount = ( desc._mipLevels == 0 ) ? textureMipCount : desc._mipLevels;
        if ( mipCount == 0 || mipCount > textureMipCount || mipCount > outCapacity )
            return 0;

        const uint8* pBase  = static_cast<const uint8*>( desc._pData );
        uint32       offset = 0;
        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            RHITextureMipSpan& span = pOutSpan[mip];
            if ( computeRhiTextureMipLayout( format, width, height, mip, span ) == false )
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
     * @brief 렌더 패스 바인딩 및 시작 인자
     */
    struct SW_API RHIRenderPassBeginInfo
    {
        RHIRenderPassHandle    _renderPass;                           ///< 렌더 패스 핸들 (8 bytes)
        RHITextureHandle       _arrColorTarget[kMaxColorAttachments]; ///< RT별 핸들 (32 bytes)
        RHITextureHandle       _depthTarget;                          ///< 깊이/스텐실 핸들 (8 bytes)
        float4                 _arrClearColor[kMaxColorAttachments];  ///< RT별 클리어 색상 (64 bytes)
        uint32                 _colorTargetCount;                     ///< 컬러 RT 개수 (4 bytes)
        uint32                 _width;                                ///< 렌더 영역 너비 (4 bytes)
        uint32                 _height;                               ///< 렌더 영역 높이 (4 bytes)
        float32                _clearDepth;                           ///< 깊이 클리어 값 (4 bytes)
        RHIRenderPassLoadOp    _arrLoadOp[kMaxColorAttachments];      ///< RT별 로드 동작 (16 bytes)
        RHIRenderPassLoadOp    _depthLoadOp;                          ///< 깊이 로드 동작 (4 bytes)
        uint8                  _bBindColor    : 1;                    ///< 컬러 바인딩 여부 (1 byte)
        [[maybe_unused]] uint8 _reservedFlags : 7;
        uint8                  _arrReserved[3]; ///< 8바이트 정렬 패딩 (3 bytes)

        /** @brief 스왑체인 RT0, Load 클리어 기본값. */
        RHIRenderPassBeginInfo() noexcept;

        /** @brief 단일 RT 설정 헬퍼 */
        void setColorTarget( RHITextureHandle target, const float4& clearColor = kDefaultClearColor, RHIRenderPassLoadOp loadOp = RHIRenderPassLoadOp::Clear );
    };

} // namespace sw
