#include "pch.h"

#include "Engine/Automation/AutomationImageMetric.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

namespace sw
{
    namespace
    {
        struct AutomationImageMetricInternal
        {
            /** @brief PPM 머리의 다음 수(공백 · `#` 주석을 건너뛴다)를 읽습니다. */
            [[nodiscard]] static bool readHeaderNumber( const uint8* pData, size_t size, size_t& inOutCursor, uint32& outValue )
            {
                while ( inOutCursor < size )
                {
                    const uint8 character = pData[inOutCursor];
                    if ( character == '#' )
                    {
                        while ( inOutCursor < size && pData[inOutCursor] != '\n' )
                        {
                            ++inOutCursor;
                        }
                    }
                    else if ( character == ' ' || character == '\t' || character == '\r' || character == '\n' )
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
                while ( inOutCursor < size && pData[inOutCursor] >= '0' && pData[inOutCursor] <= '9' && digits < 9 )
                {
                    value = value * 10u + static_cast<uint64>( pData[inOutCursor] - '0' );
                    ++inOutCursor;
                    ++digits;
                }
                outValue = static_cast<uint32>( value );
                return digits > 0;
            }

            /** @brief 픽셀의 Rec.709 휘도(0..1)입니다. */
            static float64 lumaAt( const uint8* pPixel )
            {
                return ( 0.2126 * pPixel[0] + 0.7152 * pPixel[1] + 0.0722 * pPixel[2] ) / 255.0;
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
        using Internal = AutomationImageMetricInternal;
        size_t cursor  = 2;
        uint32 width   = 0;
        uint32 height  = 0;
        uint32 maxVal  = 0;
        if ( pData == nullptr || size < 2 || pData[0] != 'P' || pData[1] != '6' )
        {
            outError = "not a binary PPM (P6)";
            return false;
        }
        if ( Internal::readHeaderNumber( pData, size, cursor, width ) == false || Internal::readHeaderNumber( pData, size, cursor, height ) == false ||
             Internal::readHeaderNumber( pData, size, cursor, maxVal ) == false || width == 0 || height == 0 || maxVal != 255 || cursor >= size )
        {
            outError = "the PPM header is broken (needs width height 255)";
            return false;
        }
        ++cursor; // 머리 끝의 공백 하나
        const uint64 pixelBytes = static_cast<uint64>( width ) * height * 3u;
        if ( size - cursor < pixelBytes )
        {
            outError = "the PPM is shorter than its header says";
            return false;
        }
        outImage._width  = width;
        outImage._height = height;
        outImage._listRgb.assign( pData + cursor, pData + cursor + pixelBytes );
        return true;
    }

    bool AutomationImageMetric::tryParseKind( string_view text, AutomationImageMetricKind& outKind )
    {
        if ( text == "meanLuma" )
            outKind = AutomationImageMetricKind::MeanLuma;
        else if ( text == "darkFraction" )
            outKind = AutomationImageMetricKind::DarkFraction;
        else if ( text == "meanRedMinusBlue" )
            outKind = AutomationImageMetricKind::MeanRedMinusBlue;
        else if ( text == "differentFrom" )
            outKind = AutomationImageMetricKind::DifferentFrom;
        else
            return false;
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
            if ( StringUtil::parseFloat( StringUtil::trim( parts.getSplitList()[index] ), arrValue[index] ) == false || arrValue[index] < 0.0f || arrValue[index] > 1.0f )
                return false;
        }
        if ( arrValue[0] >= arrValue[2] || arrValue[1] >= arrValue[3] )
            return false;
        outRegion = AutomationImageRegion{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
        return true;
    }

    bool AutomationImageMetric::measure( const AutomationImage& image, const AutomationImageRegion& region, AutomationImageMetricKind kind, float32 darkRatio,
                                         const AutomationImage* pReference, float64& outValue, string& outError )
    {
        using Internal  = AutomationImageMetricInternal;
        const uint32 x0 = static_cast<uint32>( region._x0 * static_cast<float32>( image._width ) );
        const uint32 y0 = static_cast<uint32>( region._y0 * static_cast<float32>( image._height ) );
        const uint32 x1 = MathUtil::min( static_cast<uint32>( region._x1 * static_cast<float32>( image._width ) + 0.5f ), image._width );
        const uint32 y1 = MathUtil::min( static_cast<uint32>( region._y1 * static_cast<float32>( image._height ) + 0.5f ), image._height );
        if ( x0 >= x1 || y0 >= y1 )
        {
            outError = "the region covers no pixel";
            return false;
        }
        if ( kind == AutomationImageMetricKind::DifferentFrom &&
             ( pReference == nullptr || pReference->_width != image._width || pReference->_height != image._height ) )
        {
            outError = "differentFrom needs a reference image of the same size";
            return false;
        }

        const float64   pixelCount = static_cast<float64>( x1 - x0 ) * static_cast<float64>( y1 - y0 );
        float64         sum        = 0.0;
        vector<float64> listLuma;
        if ( kind == AutomationImageMetricKind::DarkFraction )
            listLuma.reserve( static_cast<size_t>( pixelCount ) );
        for ( uint32 y = y0; y < y1; ++y )
        {
            for ( uint32 x = x0; x < x1; ++x )
            {
                const size_t offset = ( static_cast<size_t>( y ) * image._width + x ) * 3u;
                const uint8* pPixel = image._listRgb.data() + offset;
                switch ( kind )
                {
                    case AutomationImageMetricKind::MeanLuma:
                    {
                        sum += Internal::lumaAt( pPixel );
                        break;
                    }
                    case AutomationImageMetricKind::DarkFraction:
                    {
                        listLuma.push_back( Internal::lumaAt( pPixel ) );
                        break;
                    }
                    case AutomationImageMetricKind::MeanRedMinusBlue:
                    {
                        sum += ( static_cast<float64>( pPixel[0] ) - static_cast<float64>( pPixel[2] ) ) / 255.0;
                        break;
                    }
                    case AutomationImageMetricKind::DifferentFrom:
                    {
                        const uint8* pOther = pReference->_listRgb.data() + offset;
                        for ( uint32 channel = 0; channel < 3; ++channel )
                        {
                            sum += MathUtil::abs( static_cast<float64>( pPixel[channel] ) - static_cast<float64>( pOther[channel] ) ) / ( 255.0 * 3.0 );
                        }
                        break;
                    }
                }
            }
        }
        if ( kind != AutomationImageMetricKind::DarkFraction )
        {
            outValue = sum / pixelCount;
            return true;
        }
        // 중앙값 대비 — 조명 · 톤맵 · 클리어 색이 바뀌어도 "영역의 보통보다 어두운 몫" 은 그대로다.
        vector<float64> listSorted = listLuma;
        const size_t    middle     = listSorted.size() / 2;
        std::nth_element( listSorted.begin(), listSorted.begin() + static_cast<ptrdiff_t>( middle ), listSorted.end() );
        const float64 threshold = listSorted[middle] * static_cast<float64>( darkRatio );
        size_t        darkCount = 0;
        for ( const float64 luma : listLuma )
        {
            darkCount += luma < threshold ? 1u : 0u;
        }
        outValue = static_cast<float64>( darkCount ) / pixelCount;
        return true;
    }
} // namespace sw
