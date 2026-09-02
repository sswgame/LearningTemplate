#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw::editor
{
    /**
     * @struct RawImageData
     * @brief 소스 이미지 파일(PNG/JPG/TGA/BMP 등)에서 디코딩된 4채널(RGBA) 픽셀 버퍼 정보
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

    /**
     * @struct ImageUtil
     * @brief 에디터 환경에서 소스 이미지(PNG/JPG/TGA/BMP)를 RGBA 버퍼로 디코딩하는 유틸리티 (stb_image 캡슐화)
     */
    struct ImageUtil
    {
        /**
         * @brief 디스크 파일 경로에서 이미지를 로드하여 4채널(RGBA) 버퍼로 변환합니다.
         */
        static bool loadImage( string_view filePath, RawImageData& outImage );

        /**
         * @brief 메모리 버퍼에서 이미지를 로드하여 4채널(RGBA) 버퍼로 변환합니다.
         */
        static bool loadImageFromMemory( const uint8* pBuffer, size_t bufferSize, RawImageData& outImage );
    };
} // namespace sw::editor
