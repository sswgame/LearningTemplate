#include "pch.h"

#include "Engine/Renderer/Frame/Capture/RenderTargetImageFile.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Resource/Image/ImageFileWriter.h"

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
            vector<uint8> outBytes;
            outBytes.reserve( header.size() + pixelCount * 3 );
            outBytes.insert( outBytes.end(), reinterpret_cast<const uint8*>( header.c_str() ), reinterpret_cast<const uint8*>( header.c_str() ) + header.size() );
            for ( size_t pixel = 0; pixel < pixelCount; ++pixel )
            {
                outBytes.push_back( rgbaBytes[pixel * 4 + 0] );
                outBytes.push_back( rgbaBytes[pixel * 4 + 1] );
                outBytes.push_back( rgbaBytes[pixel * 4 + 2] );
            }
            return FileUtil::writeFile( outFilePath, outBytes.data(), outBytes.size() );
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool RenderTargetImageFile::unpackRgba8( const vector<uint8>& bytes, const RHITextureMipSpan& layout, RHIFormat format, vector<uint8>& outRgbaBytes )
    {
        const uint32 bytesPerPixel = getRHIFormatBytesPerPixel( format );
        // 단일 채널(깊이 읽기 레이아웃 R32_FLOAT, 마스크 R8)은 회색으로 푼다. 세 채널에 같은 값이다.
        const bool bGrayFloat = ( format == RHIFormat::R32_FLOAT );
        const bool bGrayByte  = ( format == RHIFormat::R8_UNORM );
        if ( bGrayFloat || bGrayByte )
        {
            outRgbaBytes.clear();
            outRgbaBytes.reserve( static_cast<size_t>( layout._width ) * layout._height * 4 );
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
                    outRgbaBytes.push_back( gray );
                    outRgbaBytes.push_back( gray );
                    outRgbaBytes.push_back( gray );
                    outRgbaBytes.push_back( 255 );
                }
            }
            return true;
        }
        if ( bytesPerPixel < 3 )
            return false;
        const bool bHalf = ( format == RHIFormat::R16G16B16A16_FLOAT );
        const bool bBgra = ( format == RHIFormat::B8G8R8A8_UNORM );
        outRgbaBytes.clear();
        outRgbaBytes.reserve( static_cast<size_t>( layout._width ) * layout._height * 4 );
        for ( uint32 row = 0; row < layout._height; ++row )
        {
            const uint8* pRow = bytes.data() + static_cast<size_t>( row ) * layout._rowBytes;
            for ( uint32 column = 0; column < layout._width; ++column )
            {
                const uint8* pPixel = pRow + static_cast<size_t>( column ) * bytesPerPixel;
                if ( bHalf )
                {
                    const uint16* pHalf = reinterpret_cast<const uint16*>( pPixel );
                    for ( uint32 channel = 0; channel < 3; ++channel )
                    {
                        outRgbaBytes.push_back( FrameRendererUtil::halfToUnorm8( pHalf[channel] ) );
                    }
                }
                else
                {
                    outRgbaBytes.push_back( bBgra ? pPixel[2] : pPixel[0] );
                    outRgbaBytes.push_back( pPixel[1] );
                    outRgbaBytes.push_back( bBgra ? pPixel[0] : pPixel[2] );
                }
                outRgbaBytes.push_back( 255 );
            }
        }
        return true;
    }

    bool RenderTargetImageFile::writeImageFile( const vector<uint8>& bytes, const RHITextureMipSpan& layout, RHIFormat format, string_view outFilePath )
    {
        vector<uint8> rgbaBytes;
        if ( unpackRgba8( bytes, layout, format, rgbaBytes ) == false )
        {
            SW_LOG_ERROR( "writeImageFile: format %# cannot be written as an image.", static_cast<uint32>( format ) );
            return false;
        }

        bool bWritten = false;
        if ( StringUtil::endsWith( outFilePath, ".png", true ) )
            bWritten = ImageFileWriter::writePngRgba8( outFilePath, rgbaBytes, layout._width, layout._height );
        else
            bWritten = writePpm( outFilePath, rgbaBytes, layout._width, layout._height );
        if ( bWritten == false )
        {
            SW_LOG_ERROR( "writeImageFile: could not write %#.", string( outFilePath ).c_str() );
            return false;
        }
        SW_LOG_INFO( "Screenshot: %#×%# -> %#", layout._width, layout._height, string( outFilePath ).c_str() );
        return true;
    }
} // namespace sw
