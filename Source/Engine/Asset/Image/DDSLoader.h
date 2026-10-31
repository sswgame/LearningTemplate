#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/FileSystem/AssetID.h"

namespace sw
{
    /**
     * @struct DDSImageData
     * @brief DDS 파일에서 파싱한 텍스처 데이터입니다(헤더 정보와 픽셀 · 블록 데이터).
     */
    struct DDSImageData
    {
        vector<uint8>          _bytes;
        uint32                 _width;
        uint32                 _height;
        uint32                 _depth;
        uint32                 _mipCount;
        uint32                 _dxgiFormat;
        uint8                  _bCompressed : 1;
        uint8                  _bIsBgra     : 1;
        [[maybe_unused]] uint8 _reserved    : 6;

        DDSImageData()
            : _bytes{}
            , _width{ 0 }
            , _height{ 0 }
            , _depth{ 1 }
            , _mipCount{ 1 }
            , _dxgiFormat{ 0 }
            , _bCompressed{ SW_FALSE }
            , _bIsBgra{ SW_FALSE }
            , _reserved{ 0 }
        {
        }

        /**
         * @brief 쓸 수 있는 이미지인지 반환합니다. 픽셀 · 크기 · **포맷**이 모두 있어야 합니다.
         * @details `_dxgiFormat` 0 은 DXGI_FORMAT_UNKNOWN 입니다 — 로더가 알아보지 못한 이미지를 유효하다고 판정하지 않게 포맷도 봅니다.
         */
        bool         isValid() const { return _bytes.empty() == false && _width > 0 && _height > 0 && _dxgiFormat != 0; }
        const uint8* getPixels() const { return _bytes.data(); }
        uint8*       getPixels() { return _bytes.data(); }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct DDSLegacyFourCc
     * @brief 옛 머리 DDS 의 FourCC(네 글자 코드 또는 D3DFMT 정수)와 그것이 가리키는 DXGI 형식 번호 한 쌍입니다.
     */
    struct DDSLegacyFourCc
    {
        uint32 _fourCC;
        uint32 _dxgiFormat;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct DDSLegacyRGBMask
     * @brief 옛 머리 DDS 의 비트 수 · 채널 마스크(압축 없는 RGB)와 그것이 가리키는 DXGI 형식 번호입니다. 다섯 칸이 모두 같아야 맞습니다.
     */
    struct DDSLegacyRGBMask
    {
        uint32 _bitCount;
        uint32 _rMask;
        uint32 _gMask;
        uint32 _bMask;
        uint32 _aMask;
        uint32 _dxgiFormat;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct DDSLoader
     * @brief 외부 라이브러리 없이 표준 DDS 헤더와 바이너리 데이터를 직접 파싱하는 가벼운 로더입니다.
     */
    struct SW_API DDSLoader
    {
        /**
         * @brief VFS 리소스 상대 경로(예: "textures/splash.dds")에서 DDS 텍스처를 로드합니다(.pack 아카이브와 낱개 파일 모두 같은 방식으로).
         */
        [[nodiscard]] static bool loadFromAsset( const AssetID& assetID, DDSImageData& outImage );

        /**
         * @brief 메모리 버퍼에서 DDS 텍스처를 로드합니다.
         */
        [[nodiscard]] static bool loadFromMemory( const uint8* pBuffer, size_t bufferSize, DDSImageData& outImage );

        /**
         * @brief 로더가 아는 옛 머리 FourCC 표입니다(읽기가 쓰는 표 그대로).
         * @details 형식 이름의 정본은 데이터 파일 `Config/Editor/DdsFormats.json` 이고 파이썬 에셋 검증이 그것을 읽는다. 이 표가 그 파일과 같은지
         *          `EditorTexturePipelineTest.DDSFormatFileMatchesTheLoaderTables` 가 본다.
         */
        [[nodiscard]] static vector<DDSLegacyFourCc> getLegacyFourCcTable();
        /** @brief 로더가 아는 옛 머리 RGB 마스크 표입니다(읽기가 쓰는 표 그대로). 파일과 같은지는 `getLegacyFourCcTable` 과 같은 테스트가 본다. */
        [[nodiscard]] static vector<DDSLegacyRGBMask> getLegacyRGBMaskTable();
    };
} // namespace sw
