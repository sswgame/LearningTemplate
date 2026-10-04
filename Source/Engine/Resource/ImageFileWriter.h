/**
 * @file ImageFileWriter.h
 * @brief RGBA8 그림을 파일로 씁니다 — DDS(엔진이 텍스처로 다시 읽는다)와 PNG(사람이 본다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    /**
     * @struct ImageFileWriter
     * @brief 빈틈없이 이어진 RGBA8 픽셀(행 = 너비 × 4 바이트, 위 행부터)을 씁니다. 썸네일 · 초상화 굽기가 씁니다.
     * @details DDS 는 압축하지 않은 `DXGI_FORMAT_R8G8B8A8_UNORM`(DX10 머리말)이라 `DdsLoader` · `Texture2D` 가 그대로 읽습니다. PNG 는 zlib "저장"
     *          블록(압축 없음) 한 줄씩 필터 0 이라 외부 압축기 없이 씁니다 — 크기보다 의존이 없는 것을 골랐다.
     */
    struct SW_API ImageFileWriter
    {
        /** @brief RGBA8 을 DDS 로 씁니다. 실패하면 false 입니다. */
        [[nodiscard]] static bool writeDdsRgba8( string_view path, const vector<uint8>& rgbaBytes, uint32 width, uint32 height );
        /** @brief RGBA8 을 PNG 로 씁니다. 실패하면 false 입니다. */
        [[nodiscard]] static bool writePngRgba8( string_view path, const vector<uint8>& rgbaBytes, uint32 width, uint32 height );
        /** @brief RGBA8 의 PNG 바이트입니다(파일 없이 — 시험 · 업로드). */
        static void encodePngRgba8( const vector<uint8>& rgbaBytes, uint32 width, uint32 height, vector<uint8>& outBytes );
    };
} // namespace sw
