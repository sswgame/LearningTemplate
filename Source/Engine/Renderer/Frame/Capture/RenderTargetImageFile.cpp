#include "pch.h"

#include "Engine/Renderer/Frame/Capture/RenderTargetImageFile.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/OctahedralNormal.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Image/ImageFileWriter.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    // 로그 접두어는 옮기기 전과 같게 둔다(로그를 읽는 스크립트와 사람이 같은 이름으로 찾는다).
    SW_LOG_CALLER( "FrameRenderer" );

    namespace
    {
        /** @brief RGBA8 을 PPM(P6: 아스키 헤더 + RGB 8bit)으로 씁니다. */
        [[nodiscard]] bool writePpm( string_view outFilePath, const vector<uint8>& rgbaBytes, uint32 width, uint32 height )
        {
            StringBuilder<constant::kMaxBuffer64> header;
            // PPM 헤더의 구분자는 아무 공백이면 된다. 공백만 써서 이스케이프 없이 적는다.
            header.appendFormat( "P6 %# %# 255 ", width, height );
            const size_t  pixelCount = static_cast<size_t>( width ) * height;
            vector<uint8> outBytes( header.size() + pixelCount * 3 );
            Memory::copy( outBytes.data(), header.c_str(), header.size() );
            // 크기를 한 번 잡고 포인터로 쓴다 - 픽셀마다 push_back 세 번은 Debug 의 검사 반복자로 스크린샷 한 장에 수백 ms 였다(`unpackRGBA8` 과 같다).
            uint8*       pOut = outBytes.data() + header.size();
            const uint8* pIn  = rgbaBytes.data();
            for ( size_t pixel = 0; pixel < pixelCount; ++pixel, pOut += 3, pIn += 4 )
            {
                pOut[0] = pIn[0];
                pOut[1] = pIn[1];
                pOut[2] = pIn[2];
            }
            return FileUtil::writeFile( outFilePath, outBytes.data(), outBytes.size() );
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool RenderTargetImageFile::unpackRGBA8( const vector<uint8>& bytes, const RHITextureMipSpan& layout, RHIFormat format, vector<uint8>& outRGBABytes )
    {
        const uint32 bytesPerPixel = getRHIFormatBytesPerPixel( format );
        // 단일 채널(깊이 읽기 레이아웃 R32_FLOAT, 마스크 R8)은 회색으로 푼다. 세 채널에 같은 값이다.
        const bool bGrayFloat = ( format == RHIFormat::R32_FLOAT );
        const bool bGrayByte  = ( format == RHIFormat::R8_UNORM );
        if ( bGrayFloat || bGrayByte )
        {
            outRGBABytes.clear();
            outRGBABytes.reserve( static_cast<size_t>( layout._width ) * layout._height * 4 );
            for ( uint32 row = 0; row < layout._height; ++row )
            {
                const uint8* pRow = bytes.data() + static_cast<size_t>( row ) * layout._rowBytes;
                for ( uint32 column = 0; column < layout._width; ++column )
                {
                    const uint8* pPixel = pRow + static_cast<size_t>( column ) * bytesPerPixel;
                    uint8        gray   = pPixel[0];
                    if ( bGrayFloat )
                    {
                        float32 value = 0.0f;
                        Memory::copy( &value, pPixel, sizeof( value ) );
                        gray = static_cast<uint8>( MathUtil::clamp( value, 0.0f, 1.0f ) * 255.0f + 0.5f );
                    }
                    outRGBABytes.push_back( gray );
                    outRGBABytes.push_back( gray );
                    outRGBABytes.push_back( gray );
                    outRGBABytes.push_back( 255 );
                }
            }
            return true;
        }
        if ( format == RHIFormat::R16G16_SNORM )
        {
            // G버퍼 법선(옥타헤드럴 좌표) — 방향으로 풀어 `xyz * 0.5 + 0.5` 색으로 낸다(옛 첨부가 담던 값과 같은 모양).
            outRGBABytes.clear();
            outRGBABytes.reserve( static_cast<size_t>( layout._width ) * layout._height * 4 );
            for ( uint32 row = 0; row < layout._height; ++row )
            {
                const uint8* pRow = bytes.data() + static_cast<size_t>( row ) * layout._rowBytes;
                for ( uint32 column = 0; column < layout._width; ++column )
                {
                    int16 arrCoordinate[2]{};
                    Memory::copy( arrCoordinate, pRow + static_cast<size_t>( column ) * bytesPerPixel, sizeof( arrCoordinate ) );
                    const float3 direction = OctahedralNormal{ snorm16::fromRaw( arrCoordinate[0] ), snorm16::fromRaw( arrCoordinate[1] ) }.toNormal();
                    outRGBABytes.push_back( unorm8::fromFloat( direction._x * 0.5f + 0.5f ).getRaw() );
                    outRGBABytes.push_back( unorm8::fromFloat( direction._y * 0.5f + 0.5f ).getRaw() );
                    outRGBABytes.push_back( unorm8::fromFloat( direction._z * 0.5f + 0.5f ).getRaw() );
                    outRGBABytes.push_back( 255 );
                }
            }
            return true;
        }
        if ( bytesPerPixel < 3 )
            return false;
        const bool bHalf = ( format == RHIFormat::R16G16B16A16_FLOAT );
        const bool bBgra = ( format == RHIFormat::B8G8R8A8_UNORM );
        // 크기를 한 번 잡고 포인터로 쓴다 - 픽셀마다 push_back 네 번은 Debug 의 검사 반복자로 1600x900 한 장에 0.25 초였다(편집기 UI 캡처가 장마다 푼다).
        outRGBABytes.resize( static_cast<size_t>( layout._width ) * layout._height * 4 );
        uint8* pOut = outRGBABytes.data();
        for ( uint32 row = 0; row < layout._height; ++row )
        {
            const uint8* pRow = bytes.data() + static_cast<size_t>( row ) * layout._rowBytes;
            for ( uint32 column = 0; column < layout._width; ++column, pOut += 4 )
            {
                const uint8* pPixel = pRow + static_cast<size_t>( column ) * bytesPerPixel;
                if ( bHalf )
                {
                    const uint16* pHalf = reinterpret_cast<const uint16*>( pPixel );
                    for ( uint32 channel = 0; channel < 3; ++channel )
                    {
                        pOut[channel] = FrameRendererUtil::halfToUnorm8( pHalf[channel] );
                    }
                }
                else
                {
                    pOut[0] = bBgra ? pPixel[2] : pPixel[0];
                    pOut[1] = pPixel[1];
                    pOut[2] = bBgra ? pPixel[0] : pPixel[2];
                }
                pOut[3] = 255;
            }
        }
        return true;
    }

    bool RenderTargetImageFile::writeImageFile( const vector<uint8>& bytes, const RHITextureMipSpan& layout, RHIFormat format, string_view outFilePath )
    {
        vector<uint8> rgbaBytes;
        if ( unpackRGBA8( bytes, layout, format, rgbaBytes ) == false )
        {
            SW_LOG_ERROR( "writeImageFile: format %# cannot be written as an image.", static_cast<uint32>( format ) );
            return false;
        }
        return writeRGBA8File( rgbaBytes, layout._width, layout._height, outFilePath );
    }

    bool RenderTargetImageFile::writeRGBA8File( const vector<uint8>& rgbaBytes, uint32 width, uint32 height, string_view outFilePath )
    {
        bool bWritten = false;
        if ( StringUtil::endsWith( outFilePath, ".png", true ) )
            bWritten = ImageFileWriter::writePNGRGBA8( outFilePath, rgbaBytes, width, height );
        else
            bWritten = writePpm( outFilePath, rgbaBytes, width, height );
        if ( bWritten == false )
        {
            SW_LOG_ERROR( "writeImageFile: could not write %#.", string( outFilePath ).c_str() );
            return false;
        }
        SW_LOG_INFO( "Screenshot: %#×%# -> %#", width, height, string( outFilePath ).c_str() );
        return true;
    }
} // namespace sw
