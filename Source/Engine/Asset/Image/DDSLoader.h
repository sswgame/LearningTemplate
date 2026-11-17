#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"

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
        uint32                 _layerCount; ///< 배열 원소 × 큐브 면(평면 텍스처는 1) — 바이트는 층마다 밉 사슬 하나씩이다
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
            , _layerCount{ 1 }
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
     * @struct DDSLegacyFourCC
     * @brief 옛 머리 DDS 의 FourCC(네 글자 코드 또는 D3DFMT 정수)와 그것이 가리키는 DXGI 형식 번호 한 쌍입니다.
     */
    struct DDSLegacyFourCC
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
     * @struct DDSFormatSize
     * @brief DXGI 형식 하나의 블록 크기(가로 · 세로 화소)와 블록 바이트입니다. 압축 없는 형식은 1×1 블록 = 화소 하나입니다.
     */
    struct DDSFormatSize
    {
        uint32 _dxgiFormat;
        uint32 _blockWidth;
        uint32 _blockHeight;
        uint32 _blockBytes;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct DDSHeaderInfo
     * @brief DDS 머리(매직 · 머리 · DX10 머리)를 푼 결과입니다 — 픽셀을 읽지 않고 크기 · 형식 · 밉 · 층과 픽셀이 시작하는 자리를 안다(`DDSLoader::parseHeader`).
     */
    struct DDSHeaderInfo
    {
        uint32        _width{ 0 };
        uint32        _height{ 0 };
        uint32        _depth{ 1 };
        uint32        _mipCount{ 1 };
        uint32        _layerCount{ 1 }; ///< 배열 원소 × 큐브 면 — 바이트는 층마다 밉 사슬 하나씩이다
        uint32        _dxgiFormat{ 0 };
        uint32        _dataOffset{ 0 }; ///< 파일 처음에서 픽셀이 시작하는 자리(매직 + 머리 + DX10 머리)
        DDSFormatSize _formatSize{};    ///< 형식의 블록 크기(읽기가 쓰는 표의 줄)
        uint8         _bIsBgra{ 0 };
        uint8         _bCompressed{ 0 };

        /** @brief 층 하나 · 밉 하나의 바이트입니다(블록 수 × 블록 바이트 × 그 밉의 깊이). */
        [[nodiscard]] uint64 computeMipBytes( uint32 mip ) const;
        /** @brief 머리가 말하는 픽셀 바이트 전체입니다(층 × 밉 사슬) — 파일 길이는 `_dataOffset` 에 이것을 더한 것과 같아야 한다. */
        [[nodiscard]] uint64 computePayloadBytes() const;
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
         * @brief 머리만 풉니다(@p bufferSize 는 머리 이상이면 된다 — 픽셀은 읽지 않는다). 전체 읽기(`loadFromMemory`)와 밉 배치(`TextureMipLayout::parse`)가 같은 풀이를 쓴다.
         * @details 실패하면 이유를 로그(`DDS.*`)로 남기고 오류를 돌려준다.
         */
        [[nodiscard]] static expected<DDSHeaderInfo> parseHeader( const uint8* pBuffer, size_t bufferSize );

        /**
         * @brief 로더가 아는 옛 머리 FourCC 표입니다(읽기가 쓰는 표 그대로).
         * @details 형식 이름의 정본은 데이터 파일 `Config/Editor/DdsFormats.json` 이고 파이썬 에셋 검증이 그것을 읽는다. 이 표가 그 파일과 같은지
         *          `DDSFormatFileTest.MatchesTheLoaderTables` 가 본다.
         */
        [[nodiscard]] static vector<DDSLegacyFourCC> getLegacyFourCCTable();
        /** @brief 로더가 아는 옛 머리 RGB 마스크 표입니다(읽기가 쓰는 표 그대로). 파일과 같은지는 `getLegacyFourCCTable` 과 같은 테스트가 본다. */
        [[nodiscard]] static vector<DDSLegacyRGBMask> getLegacyRGBMaskTable();
        /**
         * @brief 로더가 읽는 DXGI 형식과 그 블록 크기의 표입니다(읽기가 쓰는 표 그대로). 이 표에 없는 형식은 읽기가 거절한다.
         * @details 형식 목록은 `Config/Editor/DdsFormats.json` 의 `dxgi_formats` 와 같아야 한다(같은 테스트가 본다). 픽셀 바이트 수는 이 표로 셈한
         *          밉 사슬 × 층 × 깊이와 정확히 같아야 한다 — 짧으면 업로드가 버퍼 밖을 읽고, 길면 형식 · 밉 수를 잘못 읽은 파일이다.
         */
        [[nodiscard]] static vector<DDSFormatSize> getFormatSizeTable();
    };
} // namespace sw
