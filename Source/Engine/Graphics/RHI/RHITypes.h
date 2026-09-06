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
        float32 _arrColor[4];    ///< 정점 색상 (R, G, B, A)
    };

    // ------------------------------------------------------------------------------
    // 2) 백엔드 · 포맷 — API 종류, ImGui Vulkan 핸들, 픽셀 포맷
    // ------------------------------------------------------------------------------
    /**
     * @enum RHIBackend
     * @brief 엔진이 지원하는 크로스 플랫폼 Graphics API
     */
    ENUM()
    enum class RHIBackend : uint32
    {
        DirectX11 = 0, ///< Direct3D 11
        DirectX12 = 1, ///< Direct3D 12 (Bindless)
        Vulkan    = 2, ///< Vulkan 1.3 (Bindless)
        OpenGL    = 3, ///< OpenGL 4.5+
    };

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
         * @brief GPU 리소스 지연 해제 프레임 수 (RHIReleaseQueue 기본 frameLatency).
         * @details 4개 RHI 백엔드(DX11/DX12/Vulkan/OpenGL)가 전부 같은 값을 써야 하는 계약 —
         *          한쪽만 바꾸면 아직 GPU가 참조 중인 리소스를 조기 해제할 위험이 있다.
         */
        inline constexpr uint32 kGpuReleaseFrameLatency = 3;

        /**
         * @brief 게임 스레드가 만든 프레임 패킷이 렌더 스레드에 소비되기까지 큐잉될 수 있는 최대
         *        프레임 수 (RenderThread 패킷 링 깊이).
         * @details 아직 큐잉된(소비되지 않은) 패킷이 참조할 수 있는 자원은 최소 이 프레임 수만큼
         *          해제를 미뤄야 한다 (예: GpuMaterialRetireQueue::kRetireFrameDelay). GPU 인플라이트
         *          값인 kMaxFrameCountInFlight 와는 별개 개념 — 혼동하지 말 것.
         */
        inline constexpr uint32 kRenderFrameQueueDepth = 3;
    } // namespace constant

    /**
     * @struct RHIVulkanImGuiNative
     * @brief Vulkan ImGui init용 opaque 핸들 묶음 (Editor가 concrete VulkanRHIDevice에 의존하지 않도록)
     * @details 이미지 개수 기본값은 디바이스가 실제 스왑체인 값으로 덮어쓴다 — 여기 기본값은 그때까지의
     *          자리표시자라서, 매직 넘버 대신 계약 상수를 쓴다.
     */
    struct RHIVulkanImGuiNative
    {
        void*  _pInstance{ nullptr };
        void*  _pPhysicalDevice{ nullptr };
        void*  _pDevice{ nullptr };
        void*  _pGraphicsQueue{ nullptr };
        void*  _pRenderPass{ nullptr };
        uint32 _queueFamily{ 0 };
        uint32 _minImageCount{ constant::kMaxFrameCountInFlight };
        uint32 _imageCount{ constant::kMaxFrameCountInFlight };
    };

    /**
     * @struct RHIInputElement
     * @brief 정점 레이아웃(Input Layout) 엘리먼트
     */
    struct RHIInputElement
    {
        string    _semanticName;                        ///< 시맨틱 이름 (POSITION, COLOR 등)
        uint32    _semanticIndex{ 0 };                  ///< 시맨틱 인덱스
        RHIFormat _format = RHIFormat::R32G32B32_FLOAT; ///< 엘리먼트 데이터 포맷
        uint32    _alignedByteOffset{ 0 };              ///< 버텍스 구조체 내 바이트 오프셋
        uint32    _inputSlot{ 0 };                      ///< 버텍스 버퍼 입력 슬롯
    };

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
         * @details 실제 백엔드별 용량은 DX11=64(D3D11RHIDevice.h)/OpenGL=64(OpenGLRHIDevice.h)/
         *          Vulkan=32(VulkanRHIDevice.h)/DX12=16(D3D12RHIDevice.h)로 서로 다르다 — 가장 작은
         *          DX12 기준을 공통 안전값으로 둔다. 백엔드별 실제 값은 각자의 헤더에 그대로 둔다
         *          (호환을 위해 값 자체를 맞추지는 않음).
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
        float4                 _clearColor;             ///< 초기화 색상 (16 bytes)
        uint32                 _width;                  ///< 너비 (4 bytes)
        uint32                 _height;                 ///< 높이 (4 bytes)
        uint32                 _depth;                  ///< 깊이 (4 bytes)
        uint32                 _mipLevels;              ///< 밉 레벨 수 (4 bytes)
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
    inline constexpr RHIFormatBlockInfo getRHIFormatBlockInfo( RHIFormat format )
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
    inline constexpr bool isRHIFormatBlockCompressed( RHIFormat format )
    {
        return getRHIFormatBlockInfo( format )._blockWidth > 1;
    }

    /** @brief 비압축 컬러 포맷의 픽셀당 바이트. 압축/깊이/Unknown 은 0. */
    inline constexpr uint32 getRHIFormatBytesPerPixel( RHIFormat format )
    {
        const RHIFormatBlockInfo info = getRHIFormatBlockInfo( format );
        return info._blockWidth == 1 ? info._blockBytes : 0;
    }

    /**
     * @brief 텍스처 밉 하나의 크기와 행 바이트를 계산합니다 — 업로드(빈틈없는 행)와 읽기(readback)가 같은 배치를 쓴다.
     * @details BC 는 행 하나가 블록 한 줄(ceil(w/4) 블록)이고, 밉 크기가 4 미만이어도 블록 하나를 차지한다.
     * @return 포맷이 대상이 아니거나 크기가 0 이면 false.
     */
    inline bool computeRHITextureMipLayout( RHIFormat format, uint32 width, uint32 height, uint32 mip, RHITextureMipSpan& outSpan )
    {
        const RHIFormatBlockInfo info = getRHIFormatBlockInfo( format );
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

    // ------------------------------------------------------------------------------
    // 6) VertexLayoutBuilder — 시맨틱 엘리먼트를 모아 Input Layout 구성
    // ------------------------------------------------------------------------------
    class SW_API VertexLayoutBuilder
    {
    public:
        /** @brief 빈 빌더. */
        VertexLayoutBuilder() = default;
        /** @brief 엘리먼트 목록을 복사합니다. */
        VertexLayoutBuilder( const VertexLayoutBuilder& ) = default;
        /** @brief 엘리먼트 목록을 이동합니다. */
        VertexLayoutBuilder( VertexLayoutBuilder&& ) = default;
        /** @brief 엘리먼트 목록을 복사 대입합니다. */
        VertexLayoutBuilder& operator=( const VertexLayoutBuilder& ) = default;
        /** @brief 엘리먼트 목록을 이동 대입합니다. */
        VertexLayoutBuilder& operator=( VertexLayoutBuilder&& ) = default;
        /** @brief 기본 소멸. */
        ~VertexLayoutBuilder() = default;

        /** @brief 시맨틱 엘리먼트를 추가하고 *this를 반환합니다. */
        VertexLayoutBuilder& addElement( const utf8* pSemanticName, uint32 semanticIndex, RHIFormat format, uint32 offset, uint32 slot = 0 )
        {
            RHIInputElement elem{};
            elem._semanticName      = pSemanticName ? pSemanticName : "";
            elem._semanticIndex     = semanticIndex;
            elem._format            = format;
            elem._alignedByteOffset = offset;
            elem._inputSlot         = slot;
            _listElement.push_back( elem );
            return *this;
        }

        /** @brief 모은 엘리먼트 목록을 반환합니다. */
        const vector<RHIInputElement>& build() const { return _listElement; }

    private:
        vector<RHIInputElement> _listElement;
    };
} // namespace sw
