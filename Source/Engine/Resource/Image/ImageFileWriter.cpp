#include "pch.h"

#include "Engine/Resource/Image/ImageFileWriter.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Resource/Image/DdsFormat.h"

namespace sw
{
    SW_LOG_CALLER( "ImageFileWriter" );

    namespace
    {
        struct ImageFileWriterInternal
        {
            static constexpr uint32 kDdsFlags           = 0x1u | 0x2u | 0x4u | 0x8u | 0x1000u; // CAPS · HEIGHT · WIDTH · PITCH · PIXELFORMAT
            static constexpr uint32 kDdsCapsTexture     = 0x1000u;
            static constexpr uint32 kDxgiR8G8B8A8Unorm  = 28;
            static constexpr uint32 kDimensionTexture2D = 3;
            /** @brief zlib "저장" 블록 하나의 최대 길이입니다(16 비트 길이 칸). */
            static constexpr uint32 kStoredBlockMax = 65535;

            static void appendUint32Le( vector<uint8>& inoutBytes, uint32 value )
            {
                for ( uint32 shift = 0; shift < 32; shift += 8 )
                {
                    inoutBytes.push_back( static_cast<uint8>( ( value >> shift ) & 0xFFu ) );
                }
            }

            static void appendUint32Be( vector<uint8>& inoutBytes, uint32 value )
            {
                for ( int32 shift = 24; shift >= 0; shift -= 8 )
                {
                    inoutBytes.push_back( static_cast<uint8>( ( value >> shift ) & 0xFFu ) );
                }
            }

            /** @brief PNG 청크 하나(길이 · 종류 · 내용 · CRC)를 붙입니다. CRC 는 종류 + 내용에 겁니다. */
            static void appendChunk( vector<uint8>& inoutBytes, const utf8* pType, const vector<uint8>& dataBytes )
            {
                appendUint32Be( inoutBytes, static_cast<uint32>( dataBytes.size() ) );
                vector<uint8> typeAndDataBytes;
                typeAndDataBytes.reserve( dataBytes.size() + 4 );
                for ( uint32 index = 0; index < 4; ++index )
                {
                    typeAndDataBytes.push_back( static_cast<uint8>( pType[index] ) );
                }
                typeAndDataBytes.insert( typeAndDataBytes.end(), dataBytes.begin(), dataBytes.end() );
                inoutBytes.insert( inoutBytes.end(), typeAndDataBytes.begin(), typeAndDataBytes.end() );
                appendUint32Be( inoutBytes, StringUtil::computeCrc32( typeAndDataBytes.data(), typeAndDataBytes.size() ) );
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
    bool ImageFileWriter::writeDdsRgba8( string_view path, const vector<uint8>& rgbaBytes, uint32 width, uint32 height )
    {
        using Internal = ImageFileWriterInternal;
        if ( Internal::isSizeValid( rgbaBytes, width, height ) == false )
            return false;
        vector<uint8> bytes;
        bytes.reserve( 4 + DdsFormat::kHeaderSize + 20 + static_cast<size_t>( width ) * height * 4u );
        Internal::appendUint32Le( bytes, DdsFormat::kMagic );
        Internal::appendUint32Le( bytes, DdsFormat::kHeaderSize );
        Internal::appendUint32Le( bytes, Internal::kDdsFlags );
        Internal::appendUint32Le( bytes, height );
        Internal::appendUint32Le( bytes, width );
        Internal::appendUint32Le( bytes, width * 4u ); // 행 바이트 수
        Internal::appendUint32Le( bytes, 0 );          // 깊이
        Internal::appendUint32Le( bytes, 1 );          // 밉 수
        for ( uint32 index = 0; index < 11; ++index )
        {
            Internal::appendUint32Le( bytes, 0 ); // 예약
        }
        Internal::appendUint32Le( bytes, DdsFormat::kPixelFormatSize );
        Internal::appendUint32Le( bytes, DdsFormat::kPixelFormatFourCcFlag );
        Internal::appendUint32Le( bytes, DdsFormat::kDx10FourCc );
        for ( uint32 index = 0; index < 5; ++index )
        {
            Internal::appendUint32Le( bytes, 0 ); // 비트 수 · 마스크(DX10 머리말이 포맷을 말한다)
        }
        Internal::appendUint32Le( bytes, Internal::kDdsCapsTexture );
        for ( uint32 index = 0; index < 4; ++index )
        {
            Internal::appendUint32Le( bytes, 0 ); // caps2 · caps3 · caps4 · 예약
        }
        Internal::appendUint32Le( bytes, Internal::kDxgiR8G8B8A8Unorm );
        Internal::appendUint32Le( bytes, Internal::kDimensionTexture2D );
        Internal::appendUint32Le( bytes, 0 ); // misc
        Internal::appendUint32Le( bytes, 1 ); // 배열 크기
        Internal::appendUint32Le( bytes, 0 ); // misc2
        bytes.insert( bytes.end(), rgbaBytes.begin(), rgbaBytes.begin() + static_cast<ptrdiff_t>( static_cast<size_t>( width ) * height * 4u ) );
        if ( FileUtil::writeFile( path, bytes.data(), bytes.size() ) == false )
        {
            SW_LOG_ERROR( "Failed to write DDS '%#'", string( path ).c_str() );
            return false;
        }
        return true;
    }

    void ImageFileWriter::encodePngRgba8( const vector<uint8>& rgbaBytes, uint32 width, uint32 height, vector<uint8>& outBytes )
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

        // zlib: 머리 0x78 0x01, 저장 블록들, Adler-32.
        vector<uint8> zlibBytes;
        zlibBytes.reserve( rawBytes.size() + rawBytes.size() / Internal::kStoredBlockMax * 5 + 16 );
        zlibBytes.push_back( 0x78 );
        zlibBytes.push_back( 0x01 );
        size_t offset = 0;
        do
        {
            const uint32 blockSize = static_cast<uint32>( rawBytes.size() - offset < Internal::kStoredBlockMax ? rawBytes.size() - offset : Internal::kStoredBlockMax );
            const bool   bFinal    = offset + blockSize >= rawBytes.size();
            zlibBytes.push_back( bFinal ? 1 : 0 );
            zlibBytes.push_back( static_cast<uint8>( blockSize & 0xFFu ) );
            zlibBytes.push_back( static_cast<uint8>( ( blockSize >> 8 ) & 0xFFu ) );
            zlibBytes.push_back( static_cast<uint8>( ~blockSize & 0xFFu ) );
            zlibBytes.push_back( static_cast<uint8>( ( ~blockSize >> 8 ) & 0xFFu ) );
            zlibBytes.insert( zlibBytes.end(), rawBytes.begin() + static_cast<ptrdiff_t>( offset ), rawBytes.begin() + static_cast<ptrdiff_t>( offset + blockSize ) );
            offset += blockSize;
        } while ( offset < rawBytes.size() );
        uint32 adlerA = 1;
        uint32 adlerB = 0;
        for ( const uint8 value : rawBytes )
        {
            adlerA = ( adlerA + value ) % 65521u;
            adlerB = ( adlerB + adlerA ) % 65521u;
        }
        Internal::appendUint32Be( zlibBytes, ( adlerB << 16 ) | adlerA );
        Internal::appendChunk( outBytes, "IDAT", zlibBytes );
        Internal::appendChunk( outBytes, "IEND", vector<uint8>{} );
    }

    bool ImageFileWriter::writePngRgba8( string_view path, const vector<uint8>& rgbaBytes, uint32 width, uint32 height )
    {
        vector<uint8> bytes;
        encodePngRgba8( rgbaBytes, width, height, bytes );
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
