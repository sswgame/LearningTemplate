/**
 * @file RHISwapChainDesc.h
 * @brief 스왑체인 서술체와 HDR 표시 메타데이터, 색 공간에 맞는 백버퍼 포맷입니다.
 */
#pragma once
#include "Engine/Graphics/RHI/RHIConstants.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Window/IRenderSurface.h"

namespace sw
{
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
} // namespace sw
