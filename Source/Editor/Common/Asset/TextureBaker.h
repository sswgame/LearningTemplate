#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    struct RawImageData;
    struct TextureImportRule;

    class TextureImportConfig;
    /**
     * @struct TextureBakeResult
     * @brief 텍스처 베이킹 결과입니다.
     */
    struct TextureBakeResult
    {
        string                 _sourcePath;
        string                 _outputPath;
        uint64                 _sourceSizeBytes;
        uint64                 _outputSizeBytes;
        uint32                 _width;
        uint32                 _height;
        uint32                 _mipCount;
        uint8                  _bSuccess : 1;
        [[maybe_unused]] uint8 _reserved : 7;

        TextureBakeResult()
            : _sourcePath{}
            , _outputPath{}
            , _sourceSizeBytes{ 0 }
            , _outputSizeBytes{ 0 }
            , _width{ 0 }
            , _height{ 0 }
            , _mipCount{ 0 }
            , _bSuccess{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };

    /**
     * @struct TextureBaker
     * @brief DirectXTex 로 소스 이미지(PNG/JPG 등)를 DDS 텍스처(밉맵 · 압축)로 굽는 에디터 애셋 베이커입니다.
     */
    struct TextureBaker
    {
        /**
         * @brief 텍스처 파일 하나를 주어진 임포트 규칙에 따라 DDS 로 굽습니다.
         * @param sourcePath 원본 소스 이미지 경로
         * @param outputPath 출력 DDS 경로
         * @param rule 적용할 임포트 규칙
         * @param pOutResult 베이킹 결과 세부 정보(선택)
         * @return 성공하면 true
         */
        [[nodiscard]] static bool bakeTexture( string_view sourcePath, string_view outputPath, const TextureImportRule& rule, TextureBakeResult* pOutResult = nullptr );

        /** @brief 스위즐 · 그린 채널 반전 같은 채널 조작을 적용합니다. */
        static void applyChannelManipulations( RawImageData& rawImage, const TextureImportRule& rule, size_t totalPixels );

        /**
         * @brief TextureImportConfig 에서 상대 경로에 맞는 규칙을 골라 굽습니다.
         */
        [[nodiscard]] static bool bakeTextureWithConfig( string_view sourcePath, string_view outputPath, const TextureImportConfig& config, TextureBakeResult* pOutResult = nullptr );

        /**
         * @brief 핫 리로드가 넘긴 텍스처 파일이 소스 이미지면 굽습니다(`textures_raw/` 아래 → 옆 `textures/` 의 DDS).
         * @details 런타임은 DDS 만 읽으므로 소스 이미지는 굽는 것이 리로드입니다. 구운 DDS 의 쓰기가 다음 감시 이벤트로 와서
         *          텍스처 캐시가 다시 읽습니다. 임포트 설정은 매번 읽습니다(규칙을 고치면 재시작 없이 반영됩니다).
         *          `.hdr` 는 굽지 않고 경고합니다 — 디코더가 8비트(stb_image)라 값이 잘립니다. `textures_raw/` 밖의 소스 이미지도 경고만 합니다.
         * @return 소스 이미지였으면(구웠든 경고했든) true 이고, 호출자는 캐시를 다시 읽지 않습니다. `.dds` 면 false 입니다.
         */
        [[nodiscard]] static bool importChangedSourceImage( string_view relativePath );
    };
} // namespace sw::editor
