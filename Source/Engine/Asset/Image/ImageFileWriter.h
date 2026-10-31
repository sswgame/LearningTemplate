/**
 * @file ImageFileWriter.h
 * @brief RGBA8 그림을 파일로 씁니다 — DDS(엔진이 텍스처로 다시 읽는다)와 PNG(사람이 본다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    /**
     * @struct ImageFileWriter
     * @brief 빈틈없이 이어진 RGBA8 픽셀(행 = 너비 × 4 바이트, 위 행부터)을 씁니다. 썸네일 · 초상화 베이크가 씁니다.
     * @details DDS 는 압축하지 않은 `DXGI_FORMAT_R8G8B8A8_UNORM`(DX10 머리말)이라 `DDSLoader` · `Texture2D` 가 그대로 읽습니다. PNG 는 필터 0 의 줄을
     *          엔진의 zlib 코덱(`ZlibCompressionCodec`, 속도 우선 레벨)으로 압축합니다.
     */
    struct SW_API ImageFileWriter
    {
        /** @brief RGBA8 을 DDS 로 씁니다. 실패하면 false 입니다. */
        [[nodiscard]] static bool writeDDSRGBA8( string_view path, const vector<uint8>& rgbaBytes, uint32 width, uint32 height );
        /** @brief RGBA8 을 PNG 로 씁니다. 실패하면 false 입니다. */
        [[nodiscard]] static bool writePNGRGBA8( string_view path, const vector<uint8>& rgbaBytes, uint32 width, uint32 height );
        /** @brief RGBA8 의 PNG 바이트입니다(파일 없이 — 테스트 · 업로드). */
        static void encodePNGRGBA8( const vector<uint8>& rgbaBytes, uint32 width, uint32 height, vector<uint8>& outBytes );
    };
} // namespace sw
