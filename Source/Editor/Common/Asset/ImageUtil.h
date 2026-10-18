#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @struct RawImageData
     * @brief 소스 이미지 파일(PNG/JPG/TGA/BMP 등)에서 디코딩한 4채널(RGBA) 픽셀 버퍼와 원본의 채널 수입니다.
     * @details 버퍼는 원본이 몇 채널이든 늘 RGBA(`kChannelCount`)로 펼쳐진다. 원본이 몇 채널이었는지(1 회색 · 2 회색+알파 · 3 RGB · 4 RGBA)는
     *          `_sourceChannelCount` 가 따로 들고, 압축 형식(BC4 · BC5 · BC7)과 알파 유무는 그 값으로 고른다. 버퍼 배치와 원본 채널 수를 한 이름에 두지 않는다.
     *          디코드하지 않은 값은 `_sourceChannelCount` 가 0 이라 `isValid()` 가 거짓이다.
     */
    struct RawImageData
    {
        static constexpr int32 kChannelCount = 4; ///< `_bytes` 의 텍셀당 바이트 수(RGBA)

        vector<uint8> _bytes;
        int32         _width;
        int32         _height;
        int32         _sourceChannelCount; ///< 디코드 전 원본 파일의 채널 수(1..4)

        RawImageData()
            : _bytes{}
            , _width{ 0 }
            , _height{ 0 }
            , _sourceChannelCount{ 0 }
        {
        }

        bool         isValid() const { return _bytes.empty() == false && _width > 0 && _height > 0 && 0 < _sourceChannelCount && _sourceChannelCount <= kChannelCount; }
        bool         hasSourceAlpha() const { return _sourceChannelCount == 2 || _sourceChannelCount == 4; }
        const uint8* getPixels() const { return _bytes.data(); }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ImageUtil
     * @brief 에디터에서 소스 이미지(PNG/JPG/TGA/BMP)를 RGBA 버퍼로 디코딩하는 유틸리티입니다(stb_image 를 감쌉니다).
     */
    struct SW_EDITOR_API ImageUtil
    {
        /**
         * @brief 파일 경로의 이미지를 읽어 4채널(RGBA) 버퍼로 디코딩합니다. 원본 채널 수는 `RawImageData::_sourceChannelCount` 에 채웁니다.
         */
        [[nodiscard]] static bool loadImage( string_view filePath, RawImageData& outImage );

        /**
         * @brief 메모리 버퍼의 이미지를 4채널(RGBA) 버퍼로 디코딩합니다. 원본 채널 수는 `RawImageData::_sourceChannelCount` 에 채웁니다.
         */
        [[nodiscard]] static bool loadImageFromMemory( const uint8* pBuffer, size_t bufferSize, RawImageData& outImage );

        /**
         * @brief 메모리 버퍼의 이미지를 16 비트 회색 한 채널로 디코딩합니다(높이장 원본). 8 비트 원본은 × 257 로 늘어납니다.
         * @param outListSample 행 우선 너비 × 높이 개입니다.
         */
        [[nodiscard]] static bool loadGray16FromMemory( const uint8* pBuffer, size_t bufferSize, vector<uint16>& outListSample, int32& outWidth, int32& outHeight );

        /**
         * @brief 메모리 버퍼의 이미지를 float4 텍셀(행 우선)로 디코딩합니다. HDR 원본은 값을 그대로, 나머지는 8 비트를 255 로 나눠 0..1 로 채웁니다.
         */
        [[nodiscard]] static bool loadFloat4FromMemory( const uint8* pBuffer, size_t bufferSize, vector<float4>& outListTexel, int32& outWidth, int32& outHeight );
    };
} // namespace sw::editor
