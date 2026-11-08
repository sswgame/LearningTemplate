/**
 * @file DDSFormat.h
 * @brief DDS 파일 머리의 고정 값입니다. 읽는 쪽(`DDSLoader`)과 쓰는 쪽(`ImageFileWriter`)이 같은 값을 봅니다.
 */
#pragma once
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct DDSFormat
     * @brief DDS 명세의 머리 상수입니다(Microsoft DDS_HEADER · DDS_PIXELFORMAT).
     */
    struct DDSFormat
    {
        /** @brief 파일 앞 네 바이트 "DDS " 입니다. */
        static constexpr uint32 kMagic = FourCCUtil::make( "DDS " );
        /** @brief DX10 확장 머리가 뒤따른다는 픽셀 형식 FourCC 입니다. */
        static constexpr uint32 kDx10FourCC = FourCCUtil::make( "DX10" );
        /** @brief DDS_HEADER 의 크기(바이트)입니다. */
        static constexpr uint32 kHeaderSize = 124;
        /** @brief DDS_PIXELFORMAT 의 크기(바이트)입니다. */
        static constexpr uint32 kPixelFormatSize = 32;
        /** @brief 픽셀 형식 플래그 DDPF_FOURCC 입니다. */
        static constexpr uint32 kPixelFormatFourCCFlag = 0x4u;
        /** @brief 픽셀 형식 플래그 DDPF_RGB 입니다. */
        static constexpr uint32 kPixelFormatRGBFlag = 0x40u;
        /** @brief caps2 플래그 DDSCAPS2_CUBEMAP(옛 머리의 큐브 맵 — 면 여섯)입니다. */
        static constexpr uint32 kCaps2CubemapFlag = 0x200u;
        /** @brief DX10 머리 misc 플래그 D3D11_RESOURCE_MISC_TEXTURECUBE(배열 원소마다 면 여섯)입니다. */
        static constexpr uint32 kDx10MiscTextureCubeFlag = 0x4u;
        /** @brief DX10 머리 형식 번호 DXGI_FORMAT_ASTC_4X4_UNORM 입니다(옛 dxgiformat.h · NVTT 와 같은 값 — 기억에 의존). 쿠킹한 형식 변형만 쓴다. */
        static constexpr uint32 kDxgiASTC4x4Unorm = 134;
        /** @brief 쿠킹한 형식 변형 전용 번호입니다. DXGI 에 ETC2 · EAC 가 없어 엔진이 정한다 — 파일은 이 엔진의 쿠킹본뿐이라 바깥 도구와 맞출 일이 없다. */
        static constexpr uint32 kEngineDxgiETC2RGBA8 = 1001;
        static constexpr uint32 kEngineDxgiEACR11    = 1002;
        static constexpr uint32 kEngineDxgiEACRG11   = 1003;
    };
} // namespace sw
