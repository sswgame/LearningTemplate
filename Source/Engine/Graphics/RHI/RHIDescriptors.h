/**
 * @file RHIDescriptors.h
 * @brief RHI 서술체와 백엔드 계약 상수입니다: 스왑체인, 뷰포트, 인디렉트 인자, 버퍼 · 텍스처 · 렌더 패스 서술체, 전송 티켓.
 * @details 무거운 의존을 끄는 몫은 따로 둔다 — 메시 정점 스트림 `RHIMeshVertexLayout.h`, PSO 서술체 `RHIPipelineStateDesc.h`, 밉 배치 계산 `RHITextureMipLayout.h`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Foundation/Foundation.h"
#include "Engine/Graphics/RHI/RHIEnums.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Window/IRenderSurface.h"

namespace sw
{
    class IRHISwapChain;
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
         *          128 슬롯을 렌더러가 뷰 패스 쌍 · 프레임 패스 쌍 · 뒤쪽 예약 세 칸(프레임 전체 · 프레임 그래프)으로 나눈다(`GPUTimestampSlotLayout`).
         *          그래프가 그 몫보다 길어지면 뒤쪽 패스는 시간이 빠지고 로드 때 경고한다. 64 에서 늘린 까닭: 프레임 패스(컬링 · 그리드 · 스킨 …)를 하나씩 재려면
         *          쌍이 열 몇 개 더 필요하다. 쿼리 힙 크기는 칸 × 프레임 수(128 × 3)라 비용이 없다.
         *          슬롯 비트 집합은 `GPUTimestampSlotMask`(64 비트 단어 배열) 하나로 다룬다 — 슬롯 수를 늘려도 단어 수가 따라간다.
         */
        inline constexpr uint32 kMaxGPUTimestampSlot = 128;

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

    /**
     * @brief 색 공간 @p colorSpace 의 스왑체인 백버퍼 포맷입니다. SDR 은 @p sdrFormat(EngineConfig `_rhi._backBufferFormat` 이 고른 RGBA8 · BGRA8) 그대로다.
     * @details 포맷을 색 공간에서만 정해 맞지 않는 짝(HDR10 + RGBA8 등)을 만들 길이 없다. 언리얼 `GetDefaultBackBufferPixelFormat`(HDR 출력이면 A2B10G10R10 · FloatRGBA)과 같은 자리다.
     */
    constexpr RHIFormat getSwapChainFormat( RHIColorSpace colorSpace, RHIFormat sdrFormat )
    {
        switch ( colorSpace )
        {
            case RHIColorSpace::SRGBNonLinear:
                return sdrFormat;
            case RHIColorSpace::HDR10ST2084:
                return RHIFormat::R10G10B10A2_UNORM;
            case RHIColorSpace::ExtendedSRGBLinear:
                return RHIFormat::R16G16B16A16_FLOAT;
        }
        return RHIFormat::Unknown;
    }

    /**
     * @struct RHIDisplayMetadata
     * @brief HDR10 스왑체인에 알리는 마스터링 정보입니다(DXGI `SetHDRMetaData` · Vulkan `vkSetHdrMetadataEXT`). 원색은 BT.2020 · 흰점 D65 로 백엔드가 채운다.
     * @details 언리얼 `HDRSetMetaData` 와 같이 최대 휘도는 출력 장치 값(`DisplayOutputDesc::_maxLuminanceNits`), 프레임 평균은 종이 흰색이다.
     */
    struct RHIDisplayMetadata
    {
        float32 _maxLuminanceNits{ 1000.0f };   ///< 마스터링 최대 휘도 · MaxCLL(nits)
        float32 _minLuminanceNits{ 0.001f };    ///< 마스터링 최소 휘도(nits)
        float32 _maxFrameAverageNits{ 200.0f }; ///< MaxFALL(nits)

        // 원색 · 흰점 색도(CIE 1931 xy) — BT.2020 원색, D65 흰점(ITU-R BT.2020 · SMPTE ST 2086). 두 백엔드가 같은 값을 알린다.
        static constexpr float32 kRedPrimaryX   = 0.708f;
        static constexpr float32 kRedPrimaryY   = 0.292f;
        static constexpr float32 kGreenPrimaryX = 0.170f;
        static constexpr float32 kGreenPrimaryY = 0.797f;
        static constexpr float32 kBluePrimaryX  = 0.131f;
        static constexpr float32 kBluePrimaryY  = 0.046f;
        static constexpr float32 kWhitePointX   = 0.3127f;
        static constexpr float32 kWhitePointY   = 0.3290f;
    };

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

        /// @brief SDR(`RHIColorSpace::SRGBNonLinear`)일 때의 백버퍼 컬러 포맷입니다. 백엔드는 이 값을 존중해야 하고, 못 내면 실제 값을 보고해야 합니다.
        RHIFormat _sdrFormat{ constant::render::kBackBufferFormat };
        /// @brief 화면에 내는 색 공간입니다. 백버퍼 포맷은 이것에서 정한다(`getFormat`). 주 스왑체인은 SDR 로 만들고 `IRHIDevice::setBackBufferColorSpace` 가 바꾼다.
        RHIColorSpace _colorSpace{ RHIColorSpace::SRGBNonLinear };

        PROPERTY()
        bool _bVSync{ true }; ///< 수직 동기화

        PROPERTY()
        bool _bFullscreen{ false }; ///< 전체 화면

        /** @brief 백버퍼 포맷입니다 — 색 공간에서 정한다(`getSwapChainFormat`). */
        [[nodiscard]] RHIFormat getFormat() const { return getSwapChainFormat( _colorSpace, _sdrFormat ); }

        /**
         * @brief Win32 표면의 `HWND` 를 @p outHandle 에 `void*` 로 담고 true 입니다. 창이 없으면(핸들이 nullptr, 오프스크린) @p outHandle 이 nullptr 이고 true 입니다.
         * @details `_pWindowHandle` 은 창 시스템마다 뜻이 다른 `void*` 라서(`_surfaceKind`) DX · WGL 백엔드가 직접 `HWND` 로 읽으면 안 된다.
         *          종류 확인을 이 함수 한 곳에 모은다 — `GenericWindow::tryGetWin32Handle` 의 스왑체인 서술판이다. 창이 없는 것은 정상 경로(DX12 오프스크린)라 실패로 치지 않는다.
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

    /**
     * @struct RHIDeviceDesc
     * @brief 장치를 기동할 때만 쓰는 설정입니다 — 주 스왑체인(`_swapChain`)과 장치 고유의 칸(어댑터 · 스테이징 링 · 검증 층 · DRED · 파이프라인 캐시)을 함께 든다.
     * @details 예전에는 이 칸들이 `RHISwapChainDesc` 에 들어 있어 창 스왑체인(`IRHIDevice::createSwapChain`)도 쓰지 않는 장치 칸을 날랐다.
     *          언리얼은 장치 기동 설정(`-d3ddebug` · 어댑터 선택)을 스왑체인 서술과 따로 둔다(기억에 의존한 비교). 실행마다 정하는 기동 값이라 리플렉션하지 않는다.
     */
    struct RHIDeviceDesc
    {
        RHISwapChainDesc _swapChain; ///< 주 스왑체인 서술

        /// @brief 업로드 스테이징 용량(바이트)입니다(`_rhi._stagingRingMegabytes`). GL · Vulkan 은 링 크기, DX11 은 GPU 가 아직 복사 중인 스테이징 텍스처의 합의 상한으로 읽고,
        ///        0 이면 그 백엔드가 초기화를 거절합니다. 실행마다 정하는 설정이라 리플렉션하지 않는다.
        uint64 _stagingRingBytes{ 0 };
        /// @brief 이 크기(바이트) 이하의 텍스처 업로드는 스테이징 없이 드라이버 복사 호출로 바로 올립니다(`_rhi._directUploadMaxKilobytes`, 지금은 DX11 만 읽는다). 리플렉션하지 않는다.
        uint64 _directUploadMaxBytes{ 0 };

        /// @brief 텍스처 되읽기 링의 용량(바이트)입니다(`_rhi._readbackRingMegabytes`). 링을 쓰는 백엔드(GL · Vulkan)가 읽고, 0 이면 그 백엔드가 초기화를 거절합니다. 리플렉션하지 않는다.
        uint64 _readbackRingBytes{ 0 };
        /// @brief 링보다 큰 전송 한 장을 링 밖의 일회성 버퍼로 받는 상한(바이트)입니다(`_rhi._stagingSingleMaxMegabytes`). 링을 쓰는 백엔드(GL · Vulkan)가 업로드 · 되읽기 링에 같이 쓰고,
        ///        0 이면 링보다 큰 한 장은 `Rejected` 입니다. 리플렉션하지 않는다.
        uint64 _stagingSoloMaxBytes{ 0 };
        /// @brief 장치 메모리 할당 수 한도에서 남겨 둘 칸입니다(`EngineConfig::_rhi._allocationCountReserve`). 할당을 세는 백엔드(Vulkan)는 0 이면 초기화를 거절한다. 리플렉션하지 않는다.
        uint32 _allocationCountReserve{ 0 };
        /// @brief 업로드 스테이징 블록 크기(바이트)입니다(`EngineConfig::_rhi._uploadStagingMegabytes`). 블록을 나눠 쓰는 백엔드(Vulkan)는 0 이면 초기화를 거절한다. 리플렉션하지 않는다.
        uint64 _uploadStagingBytes{ 0 };
        /// @brief 쓰지 않은 업로드 스테이징 블록을 놓기까지의 프레임 수입니다(`EngineConfig::_rhi._uploadStagingIdleFrames`). 블록을 나눠 쓰는 백엔드(Vulkan)는 0 이면 초기화를 거절한다.
        uint32 _uploadStagingIdleFrames{ 0 };
        /// @brief 파이프라인 캐시 저장 주기(초)입니다(`EngineConfig::_rhi._pipelineCacheSaveIntervalSeconds`). 캐시를 디스크에 두는 백엔드(Vulkan)는 0 이하면 초기화를 거절한다. 리플렉션하지 않는다.
        float64 _pipelineCacheSaveIntervalSeconds{ 0.0 };
        uint8   _bSoftwareAdapter    : 1; ///< 소프트웨어 래스터라이저로 띄운다(DX12, DX11 WARP, Vulkan CPU 디바이스). CI 러너와 같은 조건을 재현하는 진단 스위치다.
        uint8   _bPreferMinimumPower : 1; ///< 하드웨어 어댑터를 저전력 순서로 고른다(하이브리드 노트북의 내장 GPU — 언리얼 -preferIntel 자리). 기본은 고성능 순서(제조사와 무관)
        /// @brief 그래픽 API 검증 층(D3D 디버그 레이어 · Vulkan 검증 레이어)을 켭니다(`resolveRHIDiagnosticSwitch` — EngineConfig `_rhi._validationLayer`, `-rhi-validation`).
        ///        배포가 아닌 구성(`SW_DO_CHECKS`)에서만 백엔드가 듣는다. 리플렉션하지 않는다.
        uint8 _bValidationLayer : 1;
        /// @brief D3D12 DRED(자동 브레드크럼 · 페이지 폴트)를 켭니다(`resolveRHIDiagnosticSwitch` — EngineConfig `_rhi._dred`, `-rhi-dred`). 배포 구성에서도 듣는다. 리플렉션하지 않는다.
        uint8 _bDRED : 1;
        /// @brief GPU 기반 검증을 켭니다(`EngineConfig::_rhi._bGPUBasedValidation`, 디버그 레이어가 켜진 실행만). 리플렉션하지 않는다.
        uint8 _bGPUBasedValidation : 1;
        /// @brief Vulkan 동기화 검증을 켭니다(`EngineConfig::_rhi._bSynchronizationValidation`, 검증 레이어가 켜진 실행만). 리플렉션하지 않는다.
        uint8 _bSynchronizationValidation : 1;

        RHIDeviceDesc() noexcept
            : _bSoftwareAdapter{ SW_FALSE }
            , _bPreferMinimumPower{ SW_FALSE }
            , _bValidationLayer{ SW_FALSE }
            , _bDRED{ SW_FALSE }
            , _bGPUBasedValidation{ SW_FALSE }
            , _bSynchronizationValidation{ SW_FALSE }
        {
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

    /**
     * @struct RHISubresourceRange
     * @brief 텍스처의 밉 · 면(배열 칸) 구간입니다. 개수 `kAll` 은 "처음부터 끝까지" 다(기본값 — 자원 전체).
     * @details 언리얼 `FRHITransitionInfo` 의 MipIndex · ArraySlice(전부는 `AllSubresources`), Godot RD 의 슬라이스 범위와 같은 자리다.
     */
    struct RHISubresourceRange
    {
        static constexpr uint16 kAll = 0xFFFFu;

        uint16 _firstMip{ 0 };
        uint16 _mipCount{ kAll };
        uint16 _firstSlice{ 0 };
        uint16 _sliceCount{ kAll };

        /** @brief 밉 하나 · 면 하나입니다. */
        static constexpr RHISubresourceRange makeSingle( uint16 mip, uint16 slice ) { return RHISubresourceRange{ mip, 1u, slice, 1u }; }
    };

    /**
     * @struct RHITextureTransition
     * @brief 텍스처 하나의 상태 전이입니다(`IRHICommandList::transitionTextures` 의 한 원소). 그래프 레벨 프롤로그가 레벨의 장벽을 모은다.
     */
    struct RHITextureTransition
    {
        RHITextureHandle    _texture{ 0 };
        RHITextureState     _state{ RHITextureState::ShaderResource };
        RHISubresourceRange _range{}; ///< 옮길 밉 · 면(기본은 전체). 밉 하나를 그리며 다른 밉을 읽는 체인이 쓴다(R-10)
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
     * @struct RHIRenderPassBeginInfo
     * @brief 렌더 패스를 바인딩하고 시작할 때 넘기는 인자입니다.
     */
    struct SW_API RHIRenderPassBeginInfo
    {
        IRHISwapChain*         _pSwapChain;                                ///< 컬러 타깃 `kRHIBackBufferTexture` 가 가리킬 백버퍼의 스왑체인. nullptr 이면 메인 창이다 (8 bytes)
        RHITextureHandle       _arrColorTarget[kMaxColorAttachments];      ///< RT별 핸들. 백버퍼는 `kRHIBackBufferTexture`, 0 은 오류(패스를 열지 않는다) (32 bytes)
        RHITextureHandle       _depthTarget;                               ///< 깊이/스텐실 핸들. 0 이면 깊이 없음 (8 bytes)
        float4                 _arrClearColor[kMaxColorAttachments];       ///< RT별 클리어 색상 (64 bytes)
        uint32                 _colorTargetCount;                          ///< 컬러 RT 개수 (4 bytes)
        uint32                 _width;                                     ///< 렌더 영역 너비 (4 bytes)
        uint32                 _height;                                    ///< 렌더 영역 높이 (4 bytes)
        float32                _clearDepth;                                ///< 깊이 클리어 값 (4 bytes)
        RHIRenderPassLoadOp    _arrLoadOp[kMaxColorAttachments];           ///< RT별 로드 동작 (16 bytes)
        RHIRenderPassLoadOp    _depthLoadOp;                               ///< 깊이 로드 동작 (4 bytes)
        uint16                 _arrColorTargetSlice[kMaxColorAttachments]; ///< RT별로 그릴 면(배열 원소, 큐브 면). 2D 는 0 (8 bytes)
        uint16                 _depthTargetSlice;                          ///< 깊이 타깃의 그릴 면 (2 bytes)
        uint8                  _arrColorTargetMip[kMaxColorAttachments];   ///< RT별로 그릴 밉(다운샘플 체인 — R-10). 0 이 가장 큰 밉 (4 bytes)
        uint8                  _depthTargetMip;                            ///< 깊이 타깃의 그릴 밉 (1 byte)
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
