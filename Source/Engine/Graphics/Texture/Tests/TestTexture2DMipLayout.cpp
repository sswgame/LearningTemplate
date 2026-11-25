/**
 * @file TestTexture2DMipLayout.cpp
 * @brief 읽은 이미지로 센 밉 구간(`Texture2D::makeMipLayout`)이 파일 머리로 센 구간(`TextureMipLayout::parse`)과 같은가입니다(MR-2 — 올리기 줄이 다시 읽는 구간).
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Serialization/LittleEndianUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Asset/Image/DDSFormat.h"
#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Asset/Image/TextureMipLayout.h"
#include "Engine/Graphics/Texture/Texture2D.h"

using namespace sw;

namespace
{
    struct TextureMipLayoutTestUtil
    {
        struct FormatCase
        {
            uint32 _dxgiFormat;
            uint32 _blockSize;  ///< 블록 한 변(압축 없으면 1)
            uint32 _blockBytes; ///< 블록 하나의 바이트
        };

        /** @brief DX10 머리 DDS(@p width × @p height, 밉 @p mipCount)를 만듭니다. 픽셀 바이트는 자리마다 다른 값이다(구간이 어긋나면 내용이 다르다). */
        static vector<uint8> makeDDS( const FormatCase& format, uint32 width, uint32 height, uint32 mipCount, uint64& outPayloadBytes )
        {
            vector<uint8> bytes;
            LittleEndianUtil::appendUint32( bytes, DDSFormat::kMagic );
            LittleEndianUtil::appendUint32( bytes, DDSFormat::kHeaderSize );
            LittleEndianUtil::appendUint32( bytes, 0 );
            LittleEndianUtil::appendUint32( bytes, height );
            LittleEndianUtil::appendUint32( bytes, width );
            LittleEndianUtil::appendUint32( bytes, 0 );
            LittleEndianUtil::appendUint32( bytes, 0 );
            LittleEndianUtil::appendUint32( bytes, mipCount );
            for ( uint32 index = 0; index < 11; ++index )
                LittleEndianUtil::appendUint32( bytes, 0 );
            LittleEndianUtil::appendUint32( bytes, DDSFormat::kPixelFormatSize );
            LittleEndianUtil::appendUint32( bytes, DDSFormat::kPixelFormatFourCCFlag );
            LittleEndianUtil::appendUint32( bytes, DDSFormat::kDx10FourCC );
            for ( uint32 index = 0; index < 5; ++index )
                LittleEndianUtil::appendUint32( bytes, 0 );
            for ( uint32 index = 0; index < 5; ++index )
                LittleEndianUtil::appendUint32( bytes, 0 ); // caps · caps2 · caps3 · caps4 · 예약
            LittleEndianUtil::appendUint32( bytes, format._dxgiFormat );
            LittleEndianUtil::appendUint32( bytes, 3 ); // TEXTURE2D
            LittleEndianUtil::appendUint32( bytes, 0 );
            LittleEndianUtil::appendUint32( bytes, 1 );
            LittleEndianUtil::appendUint32( bytes, 0 );
            outPayloadBytes = 0;
            for ( uint32 mip = 0; mip < mipCount; ++mip )
                outPayloadBytes += computeMipBytes( format, width, height, mip );
            const size_t headerBytes = bytes.size();
            bytes.resize( headerBytes + static_cast<size_t>( outPayloadBytes ) );
            for ( size_t index = headerBytes; index < bytes.size(); ++index )
                bytes[index] = static_cast<uint8>( ( index * 31u ) & 0xFFu );
            return bytes;
        }

        /** @brief 테스트가 따로 셈한 밉 하나의 바이트입니다(블록 수 × 블록 바이트). */
        static uint64 computeMipBytes( const FormatCase& format, uint32 width, uint32 height, uint32 mip )
        {
            const uint64 mipWidth  = ( width >> mip ) > 0 ? ( width >> mip ) : 1u;
            const uint64 mipHeight = ( height >> mip ) > 0 ? ( height >> mip ) : 1u;
            const uint64 blocksX   = ( mipWidth + format._blockSize - 1 ) / format._blockSize;
            const uint64 blocksY   = ( mipHeight + format._blockSize - 1 ) / format._blockSize;
            return blocksX * blocksY * format._blockBytes;
        }
    };
} // namespace

/**
 * @brief [Texture2DMipLayoutTest] 읽은 이미지로 센 밉 구간(`Texture2D::makeMipLayout`)이 머리로 센 구간과 같다 — 올리기 줄이 다시 읽는 구간이 파일의 그 밉이다(MR-2)
 * @details 변이: `DDSImageData::_dataOffset` 를 적지 않으면 첫 구간이 머리만큼 앞으로 어긋나 진다.
 */
SW_TEST_CASE( Texture2DMipLayoutTest, ImageLayoutMatchesHeaderLayout )
{
    using Util                             = TextureMipLayoutTestUtil;
    const Util::FormatCase arrFormatCase[] = {
        {71, 4,  8}, // BC1_UNORM
        {98, 4, 16}, // BC7_UNORM
        {28, 1,  4}, // R8G8B8A8_UNORM
    };
    constexpr uint32 kWidth    = 128;
    constexpr uint32 kHeight   = 64;
    constexpr uint32 kMipCount = 8; // 128 × 64 → 1 × 1
    for ( const Util::FormatCase& format : arrFormatCase )
    {
        uint64                           payloadBytes = 0;
        const vector<uint8>              file         = Util::makeDDS( format, kWidth, kHeight, kMipCount, payloadBytes );
        const expected<TextureMipLayout> header       = TextureMipLayout::parse( file.data(), TextureMipLayout::kMaxHeaderBytes, file.size() );
        SW_REQUIRE_TRUE( header.has_value() );
        DDSImageData image;
        SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( file.data(), file.size(), image ) );
        const TextureMipLayout fromImage = Texture2D::makeMipLayout( image );
        SW_REQUIRE_EQUAL( header.value()._listMip.size(), fromImage._listMip.size() );
        SW_EXPECT_EQUAL( header.value()._dxgiFormat, fromImage._dxgiFormat );
        for ( size_t mip = 0; mip < fromImage._listMip.size(); ++mip )
        {
            SW_EXPECT_EQUAL( header.value()._listMip[mip]._offset, fromImage._listMip[mip]._offset );
            SW_EXPECT_EQUAL( header.value()._listMip[mip]._size, fromImage._listMip[mip]._size );
            SW_EXPECT_EQUAL( header.value()._listMip[mip]._width, fromImage._listMip[mip]._width );
            SW_EXPECT_EQUAL( header.value()._listMip[mip]._height, fromImage._listMip[mip]._height );
        }
    }
}
