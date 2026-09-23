#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct DdsImageData
     * @brief DDS 파일에서 파싱한 텍스처 데이터입니다(헤더 정보와 픽셀 · 블록 데이터).
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

        /**
         * @brief 쓸 수 있는 이미지인지 반환합니다. 픽셀 · 크기 · **포맷**이 모두 있어야 합니다.
         * @details `_dxgiFormat` 0 은 DXGI_FORMAT_UNKNOWN 입니다. 예전에는 이 검사가 포맷을 보지
         *          않아서, 로더가 알아보지 못한 이미지를 부르는 쪽이 유효하다고 판정했습니다.
         */
        bool         isValid() const { return _bytes.empty() == false && _width > 0 && _height > 0 && _dxgiFormat != 0; }
        const uint8* getPixels() const { return _bytes.data(); }
        uint8*       getPixels() { return _bytes.data(); }
    };

    /**
     * @struct DdsLoader
     * @brief 외부 라이브러리 없이 표준 DDS 헤더와 바이너리 데이터를 직접 파싱하는 가벼운 로더입니다.
     */
    struct SW_API DdsLoader
    {
        /**
         * @brief 디스크의 실제 파일 경로에서 DDS 텍스처를 로드합니다.
         */
        static bool loadFromFile( string_view filePath, DdsImageData& outImage );

        /**
         * @brief VFS 리소스 상대 경로(예: "textures/splash.dds")에서 DDS 텍스처를 로드합니다(.pack 아카이브와 낱개 파일 모두 같은 방식으로).
         */
        static bool loadFromResource( string_view relativePath, DdsImageData& outImage );

        /**
         * @brief 메모리 버퍼에서 DDS 텍스처를 로드합니다.
         */
        static bool loadFromMemory( const uint8* pBuffer, size_t bufferSize, DdsImageData& outImage );
    };
} // namespace sw
