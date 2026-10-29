#include "pch.h"

#include "Engine/Automation/Runner/AutomationImageMetric.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/string_splitter.h"

namespace sw
{
    namespace
    {
        struct AutomationImageMetricInternal
        {
            /** @brief PPM 헤더의 다음 수를 읽습니다(공백과 `#` 주석은 건너뜁니다). */
            [[nodiscard]] static bool readHeaderNumber( const uint8* pData, size_t size, size_t& inOutCursor, uint32& outValue )
            {
                while ( inOutCursor < size )
                {
                    const uint8 character   = pData[inOutCursor];
                    const bool  bWhitespace = character == ' ' || character == '\t' || character == '\r' || character == '\n';
                    if ( character == '#' )
                    {
                        while ( inOutCursor < size && pData[inOutCursor] != '\n' )
                        {
                            ++inOutCursor;
                        }
                    }
                    else if ( bWhitespace )
                    {
                        ++inOutCursor;
                    }
                    else
                    {
                        break;
                    }
                }
                uint64 value  = 0;
                size_t digits = 0;
                while ( true )
                {
                    const bool bReadingDigit = inOutCursor < size && pData[inOutCursor] >= '0' && pData[inOutCursor] <= '9' && digits < 9;
                    if ( bReadingDigit == false )
                        break;
                    value = value * 10u + static_cast<uint64>( pData[inOutCursor] - '0' );
                    ++inOutCursor;
                    ++digits;
                }
                outValue = static_cast<uint32>( value );
                return digits > 0;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AutomationImageMetric::loadPpm( string_view path, AutomationImage& outImage, string& outError )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( path, bytes ) == false )
        {
            outError = "could not read " + string( path );
            return false;
        }
        return parsePpm( bytes.data(), bytes.size(), outImage, outError );
    }

    bool AutomationImageMetric::parsePpm( const uint8* pData, size_t size, AutomationImage& outImage, string& outError )
    {
        using Internal          = AutomationImageMetricInternal;
        size_t     cursor       = 2;
        uint32     width        = 0;
        uint32     height       = 0;
        uint32     maxVal       = 0;
        const bool bNotPpmMagic = pData == nullptr || size < 2 || pData[0] != 'P' || pData[1] != '6';
        if ( bNotPpmMagic )
        {
            outError = "not a binary PPM (P6)";
            return false;
        }
        const bool bBrokenPpmHeader = Internal::readHeaderNumber( pData, size, cursor, width ) == false || Internal::readHeaderNumber( pData, size, cursor, height ) == false ||
                                      Internal::readHeaderNumber( pData, size, cursor, maxVal ) == false || width == 0 || height == 0 || maxVal != 255 || cursor >= size;
        if ( bBrokenPpmHeader )
        {
            outError = "the PPM header is broken (needs width height 255)";
            return false;
        }
        ++cursor; // 헤더 끝의 공백 하나
        const uint64 pixelBytes = static_cast<uint64>( width ) * height * 3u;
        if ( size - cursor < pixelBytes )
        {
            outError = "the PPM is shorter than its header says";
            return false;
        }
        outImage._width  = width;
        outImage._height = height;
        outImage._listRGB.assign( pData + cursor, pData + cursor + pixelBytes );
        return true;
    }

    bool AutomationImageMetric::tryParseRegion( string_view text, AutomationImageRegion& outRegion )
    {
        const string_splitter parts( text, { "," } );
        if ( parts.getSplitList().size() != 4 )
            return false;
        float32 arrValue[4]{};
        for ( size_t index = 0; index < 4; ++index )
        {
            const bool bInvalidRectComponent = StringUtil::parseFloat( StringUtil::trim( parts.getSplitList()[index] ), arrValue[index] ) == false || arrValue[index] < 0.0f || arrValue[index] > 1.0f;
            if ( bInvalidRectComponent )
                return false;
        }
        if ( arrValue[0] >= arrValue[2] || arrValue[1] >= arrValue[3] )
            return false;
        outRegion = AutomationImageRegion{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
        return true;
    }

    void AutomationImageMetric::encodePpm( const AutomationImage& image, vector<uint8>& outBytes )
    {
        const string header = "P6\n" + to_string( image._width ) + " " + to_string( image._height ) + "\n255\n";
        outBytes.assign( header.begin(), header.end() );
        outBytes.insert( outBytes.end(), image._listRGB.begin(), image._listRGB.end() );
    }

    bool AutomationImageMetric::reduce( const AutomationImage& image, const AutomationImageRegion& region, AutomationImage& outReduced, string& outError )
    {
        const uint32 x0 = static_cast<uint32>( region._x0 * static_cast<float32>( image._width ) );
        const uint32 y0 = static_cast<uint32>( region._y0 * static_cast<float32>( image._height ) );
        const uint32 x1 = std::min( image._width, static_cast<uint32>( region._x1 * static_cast<float32>( image._width ) + 0.5f ) );
        const uint32 y1 = std::min( image._height, static_cast<uint32>( region._y1 * static_cast<float32>( image._height ) + 0.5f ) );
        if ( x1 <= x0 || y1 <= y0 )
        {
            outError = "the region covers no pixel of the " + to_string( image._width ) + "x" + to_string( image._height ) + " image";
            return false;
        }
        const uint32 sourceWidth  = x1 - x0;
        const uint32 sourceHeight = y1 - y0;
        const uint32 width        = std::min( sourceWidth, kGoldenMaxWidth );
        const uint32 height       = std::max( 1u, static_cast<uint32>( static_cast<uint64>( sourceHeight ) * width / sourceWidth ) );
        outReduced._width         = width;
        outReduced._height        = height;
        outReduced._listRGB.assign( static_cast<size_t>( width ) * height * 3u, 0 );
        for ( uint32 y = 0; y < height; ++y )
        {
            const uint32 sy0 = y0 + static_cast<uint32>( static_cast<uint64>( y ) * sourceHeight / height );
            const uint32 sy1 = std::max( sy0 + 1, y0 + static_cast<uint32>( static_cast<uint64>( y + 1 ) * sourceHeight / height ) );
            for ( uint32 x = 0; x < width; ++x )
            {
                const uint32 sx0       = x0 + static_cast<uint32>( static_cast<uint64>( x ) * sourceWidth / width );
                const uint32 sx1       = std::max( sx0 + 1, x0 + static_cast<uint32>( static_cast<uint64>( x + 1 ) * sourceWidth / width ) );
                uint64       arrSum[3] = { 0, 0, 0 };
                for ( uint32 sy = sy0; sy < sy1; ++sy )
                {
                    for ( uint32 sx = sx0; sx < sx1; ++sx )
                    {
                        const uint8* pPixel = &image._listRGB[( static_cast<size_t>( sy ) * image._width + sx ) * 3u];
                        arrSum[0] += pPixel[0];
                        arrSum[1] += pPixel[1];
                        arrSum[2] += pPixel[2];
                    }
                }
                const uint64 count = static_cast<uint64>( sy1 - sy0 ) * ( sx1 - sx0 );
                uint8*       pOut  = &outReduced._listRGB[( static_cast<size_t>( y ) * width + x ) * 3u];
                for ( uint32 channel = 0; channel < 3; ++channel )
                {
                    pOut[channel] = static_cast<uint8>( ( arrSum[channel] + count / 2 ) / count );
                }
            }
        }
        return true;
    }

    bool AutomationImageMetric::compare( const AutomationImage& actual, const AutomationImage& golden, AutomationImageDifference& outDifference, string& outError )
    {
        if ( actual._width != golden._width || actual._height != golden._height )
        {
            outError = "the image is " + to_string( actual._width ) + "x" + to_string( actual._height ) + " but the golden is " + to_string( golden._width ) + "x" +
                       to_string( golden._height ) + " (region or window size changed - record the golden again)";
            return false;
        }
        const size_t pixelCount     = static_cast<size_t>( actual._width ) * actual._height;
        size_t       differingCount = 0;
        uint64       distanceSum    = 0;
        for ( size_t pixel = 0; pixel < pixelCount; ++pixel )
        {
            const uint8* pA       = &actual._listRGB[pixel * 3u];
            const uint8* pB       = &golden._listRGB[pixel * 3u];
            const uint32 distance = static_cast<uint32>( std::abs( pA[0] - pB[0] ) + std::abs( pA[1] - pB[1] ) + std::abs( pA[2] - pB[2] ) );
            distanceSum += distance;
            if ( distance >= kDifferingMinDistance )
                ++differingCount;
        }
        outDifference._differingFraction = pixelCount > 0 ? static_cast<float64>( differingCount ) / static_cast<float64>( pixelCount ) : 0.0;
        outDifference._meanDistance      = pixelCount > 0 ? static_cast<float64>( distanceSum ) / ( static_cast<float64>( pixelCount ) * 765.0 ) : 0.0;
        return true;
    }

} // namespace sw
