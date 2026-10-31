#include "pch.h"

#include "Engine/Asset/Image/ImageFileWriter.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Asset/Image/DDSFormat.h"
#include "Engine/Compression/ZlibCompressionCodec.h"

namespace sw
{
    SW_LOG_CALLER( "ImageFileWriter" );

    namespace
    {
        struct ImageFileWriterInternal
        {
            static constexpr uint32 kDDSFlags           = 0x1u | 0x2u | 0x4u | 0x8u | 0x1000u; // CAPS · HEIGHT · WIDTH · PITCH · PIXELFORMAT
            static constexpr uint32 kDDSCapsTexture     = 0x1000u;
            static constexpr uint32 kDxgiR8G8B8A8Unorm  = 28;
            static constexpr uint32 kDimensionTexture2D = 3;
            /** @brief PNG 의 deflate 레벨입니다 - 캡처는 자주 쓰고 크기보다 시간이 중요하다(zlib `Z_BEST_SPEED`). */
            static constexpr int32 kPNGDeflateLevel = 1;

            static void appendUint32Be( vector<uint8>& inoutBytes, uint32 value )
            {
                for ( int32 shift = 24; shift >= 0; shift -= 8 )
                {
                    inoutBytes.push_back( static_cast<uint8>( ( value >> shift ) & 0xFFu ) );
                }
            }

            /** @brief PNG 청크 하나(길이 · 종류 · 내용 · CRC)를 붙입니다. CRC 는 종류 + 내용에 걸고, 붙인 자리에서 바로 센다(사본을 만들지 않는다). */
            static void appendChunk( vector<uint8>& inoutBytes, const utf8* pType, const vector<uint8>& dataBytes )
            {
                appendUint32Be( inoutBytes, static_cast<uint32>( dataBytes.size() ) );
                const size_t typeOffset = inoutBytes.size();
                inoutBytes.insert( inoutBytes.end(), pType, pType + 4 );
                inoutBytes.insert( inoutBytes.end(), dataBytes.begin(), dataBytes.end() );
                appendUint32Be( inoutBytes, StringUtil::computeCRC32( inoutBytes.data() + typeOffset, inoutBytes.size() - typeOffset ) );
            }

            static bool isSizeValid( const vector<uint8>& rgbaBytes, uint32 width, uint32 height )
            {
                return width > 0 && height > 0 && rgbaBytes.size() >= static_cast<size_t>( width ) * height * 4u;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ImageFileWriter::writeDDSRGBA8( string_view path, const vector<uint8>& rgbaBytes, uint32 width, uint32 height )
    {
        using Internal = ImageFileWriterInternal;
        if ( Internal::isSizeValid( rgbaBytes, width, height ) == false )
            return false;
        vector<uint8> bytes;
        bytes.reserve( 4 + DDSFormat::kHeaderSize + 20 + static_cast<size_t>( width ) * height * 4u );
        LittleEndianUtil::appendUint32( bytes, DDSFormat::kMagic );
        LittleEndianUtil::appendUint32( bytes, DDSFormat::kHeaderSize );
        LittleEndianUtil::appendUint32( bytes, Internal::kDDSFlags );
        LittleEndianUtil::appendUint32( bytes, height );
        LittleEndianUtil::appendUint32( bytes, width );
        LittleEndianUtil::appendUint32( bytes, width * 4u ); // 행 바이트 수
        LittleEndianUtil::appendUint32( bytes, 0 );          // 깊이
        LittleEndianUtil::appendUint32( bytes, 1 );          // 밉 수
        for ( uint32 index = 0; index < 11; ++index )
        {
            LittleEndianUtil::appendUint32( bytes, 0 ); // 예약
        }
        LittleEndianUtil::appendUint32( bytes, DDSFormat::kPixelFormatSize );
        LittleEndianUtil::appendUint32( bytes, DDSFormat::kPixelFormatFourCcFlag );
        LittleEndianUtil::appendUint32( bytes, DDSFormat::kDx10FourCc );
        for ( uint32 index = 0; index < 5; ++index )
        {
            LittleEndianUtil::appendUint32( bytes, 0 ); // 비트 수 · 마스크(DX10 머리말이 포맷을 말한다)
        }
        LittleEndianUtil::appendUint32( bytes, Internal::kDDSCapsTexture );
        for ( uint32 index = 0; index < 4; ++index )
        {
            LittleEndianUtil::appendUint32( bytes, 0 ); // caps2 · caps3 · caps4 · 예약
        }
        LittleEndianUtil::appendUint32( bytes, Internal::kDxgiR8G8B8A8Unorm );
        LittleEndianUtil::appendUint32( bytes, Internal::kDimensionTexture2D );
        LittleEndianUtil::appendUint32( bytes, 0 ); // misc
        LittleEndianUtil::appendUint32( bytes, 1 ); // 배열 크기
        LittleEndianUtil::appendUint32( bytes, 0 ); // misc2
        bytes.insert( bytes.end(), rgbaBytes.begin(), rgbaBytes.begin() + static_cast<ptrdiff_t>( static_cast<size_t>( width ) * height * 4u ) );
        if ( FileUtil::writeFile( path, bytes.data(), bytes.size() ) == false )
        {
            SW_LOG_ERROR( "Failed to write DDS '%#'", string( path ).c_str() );
            return false;
        }
        return true;
    }

    void ImageFileWriter::encodePNGRGBA8( const vector<uint8>& rgbaBytes, uint32 width, uint32 height, vector<uint8>& outBytes )
    {
        using Internal = ImageFileWriterInternal;
        outBytes.clear();
        if ( Internal::isSizeValid( rgbaBytes, width, height ) == false )
            return;
        static constexpr uint8 kArrSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        outBytes.insert( outBytes.end(), kArrSignature, kArrSignature + 8 );

        vector<uint8> headerBytes;
        Internal::appendUint32Be( headerBytes, width );
        Internal::appendUint32Be( headerBytes, height );
        headerBytes.push_back( 8 ); // 채널당 비트
        headerBytes.push_back( 6 ); // RGBA
        headerBytes.push_back( 0 ); // 압축 방식(deflate)
        headerBytes.push_back( 0 ); // 필터 방식
        headerBytes.push_back( 0 ); // 비월 없음
        Internal::appendChunk( outBytes, "IHDR", headerBytes );

        // 원본 줄: 행마다 필터 바이트(0 = 없음) + RGBA.
        const size_t  rowBytes = static_cast<size_t>( width ) * 4u;
        vector<uint8> rawBytes;
        rawBytes.reserve( ( rowBytes + 1 ) * height );
        for ( uint32 row = 0; row < height; ++row )
        {
            rawBytes.push_back( 0 );
            const uint8* pRow = rgbaBytes.data() + row * rowBytes;
            rawBytes.insert( rawBytes.end(), pRow, pRow + rowBytes );
        }

        // IDAT 는 zlib 스트림(머리 · deflate · Adler-32)이다 - 엔진의 zlib 코덱을 속도 우선 레벨로 쓴다. 예전의 압축 없는 저장 블록은 1600x900 캡처가 5.7 MB 였고
        // 바이트마다 Adler 를 세느라 Debug 에서 장마다 1.4 초가 들어 편집기 UI 캡처 시나리오 시간의 대부분이었다.
        ZlibCompressionCodec codec;
        vector<uint8>        zlibBytes( codec.compressBound( rawBytes.size() ) );
        size_t               compressedSize{ 0 };
        if ( codec.compress( rawBytes.data(), rawBytes.size(), zlibBytes.data(), zlibBytes.size(), compressedSize, Internal::kPNGDeflateLevel ) == false )
        {
            outBytes.clear(); // 코덱이 까닭을 남긴다 - 빈 결과는 쓰는 쪽이 실패로 받는다
            return;
        }
        zlibBytes.resize( compressedSize );
        Internal::appendChunk( outBytes, "IDAT", zlibBytes );
        Internal::appendChunk( outBytes, "IEND", vector<uint8>{} );
    }

    bool ImageFileWriter::writePNGRGBA8( string_view path, const vector<uint8>& rgbaBytes, uint32 width, uint32 height )
    {
        vector<uint8> bytes;
        encodePNGRGBA8( rgbaBytes, width, height, bytes );
        if ( bytes.empty() )
            return false;
        if ( FileUtil::writeFile( path, bytes.data(), bytes.size() ) == false )
        {
            SW_LOG_ERROR( "Failed to write PNG '%#'", string( path ).c_str() );
            return false;
        }
        return true;
    }
} // namespace sw
