#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct DdsImageData
     * @brief DDS 파일에서 파싱된 텍스처 데이터 (헤더 정보 및 픽셀/블록 데이터)
     */
    struct DdsImageData
    {
        vector<uint8>          _bytes;
        uint32                 _width;
        uint32                 _height;
        uint32                 _depth;
        uint32                 _mipCount;
        uint32                 _dxgiFormat;
        uint32                 _bitsPerPixel;
        uint8                  _bCompressed : 1;
        uint8                  _bIsBgra     : 1;
        [[maybe_unused]] uint8 _reserved    : 6;

        DdsImageData()
            : _bytes{}
            , _width{ 0 }
            , _height{ 0 }
            , _depth{ 1 }
            , _mipCount{ 1 }
            , _dxgiFormat{ 0 }
            , _bitsPerPixel{ 32 }
            , _bCompressed{ SW_FALSE }
            , _bIsBgra{ SW_FALSE }
            , _reserved{ 0 }
        {
        }

        bool         isValid() const { return _bytes.empty() == false && _width > 0 && _height > 0; }
        const uint8* getPixels() const { return _bytes.data(); }
        uint8*       getPixels() { return _bytes.data(); }
    };

    /**
     * @struct DdsLoader
     * @brief 외부 서드파티 의존성 없이 표준 DDS 헤더 및 바이너리 데이터를 직접 파싱하는 경량 로더
     */
    struct SW_API DdsLoader
    {
        /**
         * @brief 디스크 파일 경로에서 DDS 텍스처를 로드합니다.
         */
        static bool loadFromFile( string_view filePath, DdsImageData& outImage );

        /**
         * @brief 메모리 버퍼에서 DDS 텍스처를 로드합니다.
         */
        static bool loadFromMemory( const uint8* pBuffer, size_t bufferSize, DdsImageData& outImage );
    };
} // namespace sw
