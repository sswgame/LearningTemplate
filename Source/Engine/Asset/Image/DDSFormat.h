/**
 * @file DDSFormat.h
 * @brief DDS 파일 머리의 고정 값입니다. 읽는 쪽(`DDSLoader`)과 쓰는 쪽(`ImageFileWriter`)이 같은 값을 봅니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"

namespace sw
{
    /**
     * @struct DDSFormat
     * @brief DDS 명세의 머리 상수입니다(Microsoft DDS_HEADER · DDS_PIXELFORMAT).
     */
    struct DDSFormat
    {
        /** @brief 파일 앞 네 바이트 "DDS " 입니다. */
        static constexpr uint32 kMagic = FourCcUtil::make( "DDS " );
        /** @brief DX10 확장 머리가 뒤따른다는 픽셀 형식 FourCC 입니다. */
        static constexpr uint32 kDx10FourCc = FourCcUtil::make( "DX10" );
        /** @brief DDS_HEADER 의 크기(바이트)입니다. */
        static constexpr uint32 kHeaderSize = 124;
        /** @brief DDS_PIXELFORMAT 의 크기(바이트)입니다. */
        static constexpr uint32 kPixelFormatSize = 32;
        /** @brief 픽셀 형식 플래그 DDPF_FOURCC 입니다. */
        static constexpr uint32 kPixelFormatFourCcFlag = 0x4u;
        /** @brief 픽셀 형식 플래그 DDPF_RGB 입니다. */
        static constexpr uint32 kPixelFormatRGBFlag = 0x40u;
        /** @brief caps2 플래그 DDSCAPS2_CUBEMAP(옛 머리의 큐브 맵 — 면 여섯)입니다. */
        static constexpr uint32 kCaps2CubemapFlag = 0x200u;
        /** @brief DX10 머리 misc 플래그 D3D11_RESOURCE_MISC_TEXTURECUBE(배열 원소마다 면 여섯)입니다. */
        static constexpr uint32 kDx10MiscTextureCubeFlag = 0x4u;
    };
} // namespace sw
