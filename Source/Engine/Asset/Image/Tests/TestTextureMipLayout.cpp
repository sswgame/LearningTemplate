#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/LittleEndianUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Asset/Image/DDSFormat.h"
#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Asset/Image/TextureMipLayout.h"

// 밉 배치(MR-1) — 머리만으로 구한 밉 구간이 파일 전체를 푼 결과(DDSLoader::loadFromMemory)의 같은 자리와 바이트까지 같고, 잘린 파일은 오류다(nogpu).

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

/** @brief [TextureMipLayoutTest] BC1 · BC7 · RGBA8 · ASTC 에서 밉 구간이 이어져 있고, 크기가 블록 셈과 같고, 내용이 전체 풀이의 같은 자리와 같다 */
SW_TEST_CASE( TextureMipLayoutTest, OffsetsMatchWholeFileDecode )
{
    using Util                             = TextureMipLayoutTestUtil;
    const Util::FormatCase arrFormatCase[] = {
        {                          71, 4,  8}, // BC1_UNORM
        {                          98, 4, 16}, // BC7_UNORM
        {                          28, 1,  4}, // R8G8B8A8_UNORM
        {DDSFormat::kDXGIASTC4x4Unorm, 4, 16},
    };
    constexpr uint32 kWidth    = 64;
    constexpr uint32 kHeight   = 32;
    constexpr uint32 kMipCount = 7; // 64 × 32 → 1 × 1
    for ( const Util::FormatCase& format : arrFormatCase )
    {
        uint64                           payloadBytes = 0;
        const vector<uint8>              file         = Util::makeDDS( format, kWidth, kHeight, kMipCount, payloadBytes );
        const expected<TextureMipLayout> layout       = TextureMipLayout::parse( file.data(), TextureMipLayout::kMaxHeaderBytes, file.size() );
        SW_REQUIRE_TRUE( layout.has_value() );
        DDSImageData whole;
        SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( file.data(), file.size(), whole ) );
        SW_REQUIRE_EQUAL( size_t( kMipCount ), layout.value()._listMip.size() );
        SW_EXPECT_EQUAL( format._dxgiFormat, layout.value()._dxgiFormat );

        const uint64 dataOffset = file.size() - whole._bytes.size();
        uint64       nextOffset = dataOffset;
        for ( uint32 mip = 0; mip < kMipCount; ++mip )
        {
            const TextureMipLayout::MipRange& entry = layout.value()._listMip[mip];
            SW_EXPECT_EQUAL( nextOffset, entry._offset );
            SW_EXPECT_EQUAL( Util::computeMipBytes( format, kWidth, kHeight, mip ), uint64( entry._size ) );
            SW_EXPECT_EQUAL( MathUtil::max( kWidth >> mip, 1u ), entry._width );
            for ( uint32 byteIndex = 0; byteIndex < entry._size; ++byteIndex )
            {
                if ( file[entry._offset + byteIndex] != whole._bytes[entry._offset - dataOffset + byteIndex] )
                {
                    SW_EXPECT_TRUE_MSG( false, "mip bytes differ from the whole decode" );
                    break;
                }
            }
            nextOffset += entry._size;
        }
        SW_EXPECT_EQUAL( uint64( file.size() ), nextOffset );

        // 위쪽 밉 셋의 구간은 밉 0 처음부터 밉 2 끝까지다.
        uint64 rangeOffset = 0;
        uint64 rangeSize   = 0;
        layout.value().getRange( 0, 3, rangeOffset, rangeSize );
        SW_EXPECT_EQUAL( dataOffset, rangeOffset );
        SW_EXPECT_EQUAL( Util::computeMipBytes( format, kWidth, kHeight, 0 ) + Util::computeMipBytes( format, kWidth, kHeight, 1 ) + Util::computeMipBytes( format, kWidth, kHeight, 2 ), rangeSize );
    }
}

/** @brief [TextureMipLayoutTest] 파일 길이가 머리가 말하는 바이트와 다르면(잘렸다 · 남는다) 오류다 */
SW_TEST_CASE( TextureMipLayoutTest, TruncatedFileIsError )
{
    SW_EXPECT_LOG( "TextureMipLayout.SizeMismatch" );
    SW_EXPECT_LOG( "DDS.Truncated" );
    using Util                          = TextureMipLayoutTestUtil;
    const Util::FormatCase format       = { 71, 4, 8 };
    uint64                 payloadBytes = 0;
    const vector<uint8>    file         = Util::makeDDS( format, 32, 32, 6, payloadBytes );
    SW_EXPECT_FALSE( TextureMipLayout::parse( file.data(), TextureMipLayout::kMaxHeaderBytes, file.size() - 1 ).has_value() );
    SW_EXPECT_FALSE( TextureMipLayout::parse( file.data(), TextureMipLayout::kMaxHeaderBytes, file.size() + 8 ).has_value() );
    SW_EXPECT_FALSE( TextureMipLayout::parse( file.data(), 40, file.size() ).has_value() ); // 머리가 잘렸다
}
