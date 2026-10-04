#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw::editor
{
    /**
     * @struct RawImageData
     * @brief 소스 이미지 파일(PNG/JPG/TGA/BMP 등)에서 디코딩한 4채널(RGBA) 픽셀 버퍼입니다.
     */
    struct RawImageData
    {
        vector<uint8> _bytes;
        int32         _width;
        int32         _height;
        int32         _channels;

        RawImageData()
            : _bytes{}
            , _width{ 0 }
            , _height{ 0 }
            , _channels{ 4 }
        {
        }

        bool         isValid() const { return _bytes.empty() == false && _width > 0 && _height > 0; }
        const uint8* getPixels() const { return _bytes.data(); }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ImageUtil
     * @brief 에디터에서 소스 이미지(PNG/JPG/TGA/BMP)를 RGBA 버퍼로 디코딩하는 유틸리티입니다(stb_image 를 감쌉니다).
     */
    struct ImageUtil
    {
        /**
         * @brief 파일 경로의 이미지를 읽어 4채널(RGBA) 버퍼로 디코딩합니다.
         */
        [[nodiscard]] static bool loadImage( string_view filePath, RawImageData& outImage );

        /**
         * @brief 메모리 버퍼의 이미지를 4채널(RGBA) 버퍼로 디코딩합니다.
         */
        [[nodiscard]] static bool loadImageFromMemory( const uint8* pBuffer, size_t bufferSize, RawImageData& outImage );

        /**
         * @brief 메모리 버퍼의 이미지를 16 비트 회색 한 채널로 디코딩합니다(높이장 원본). 8 비트 원본은 × 257 로 늘어납니다.
         * @param outListSample 행 우선 너비 × 높이 개입니다.
         */
        [[nodiscard]] static bool loadGray16FromMemory( const uint8* pBuffer, size_t bufferSize, vector<uint16>& outListSample, int32& outWidth, int32& outHeight );
    };
} // namespace sw::editor
