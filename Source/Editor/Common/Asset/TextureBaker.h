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
        static bool bakeTexture( string_view sourcePath, string_view outputPath, const TextureImportRule& rule, TextureBakeResult* pOutResult = nullptr );

        /** @brief 스위즐 · 그린 채널 반전 같은 채널 조작을 적용합니다. */
        static void applyChannelManipulations( RawImageData& rawImage, const TextureImportRule& rule, size_t totalPixels );

        /**
         * @brief TextureImportConfig 에서 상대 경로에 맞는 규칙을 골라 굽습니다.
         */
        static bool bakeTextureWithConfig( string_view sourcePath, string_view outputPath, const TextureImportConfig& config, TextureBakeResult* pOutResult = nullptr );
    };
} // namespace sw::editor
