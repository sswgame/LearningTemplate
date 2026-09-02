#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    struct TextureImportRule;

    class TextureImportConfig;
    /**
     * @struct TextureBakeResult
     * @brief 텍스처 베이킹 작업 결과 정보
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
     * @brief DirectXTex를 활용하여 소스 이미지(PNG/JPG 등)를 최적화된 DDS 텍스처로 베이킹하는 에디터 에셋 베이커
     */
    struct TextureBaker
    {
        /**
         * @brief 단일 텍스처 파일을 지정된 임포트 규칙에 따라 DDS로 베이킹합니다.
         * @param sourcePath 원본 소스 이미지 경로
         * @param outputPath 출력 DDS 경로
         * @param rule 적용할 임포트 규칙
         * @param pOutResult 베이킹 결과 세부 정보 (선택적)
         * @return 성공 시 true
         */
        static bool bakeTexture( string_view sourcePath, string_view outputPath, const TextureImportRule& rule, TextureBakeResult* pOutResult = nullptr );

        /**
         * @brief TextureImportConfig를 참조하여 상대 경로에 맞는 규칙을 자동 선택하고 베이킹합니다.
         */
        static bool bakeTextureWithConfig( string_view sourcePath, string_view outputPath, const TextureImportConfig& config, TextureBakeResult* pOutResult = nullptr );
    };
} // namespace sw::editor
