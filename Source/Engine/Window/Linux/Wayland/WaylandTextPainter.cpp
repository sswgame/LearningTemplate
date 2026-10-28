#include "pch.h"

#include "Engine/Window/Linux/Wayland/WaylandTextPainter.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Text/IFontRasterizer.h"
#include "Engine/Text/SystemFontLocator.h"

namespace sw
{
    SW_LOG_CALLER( "WaylandTextPainter" );

    namespace
    {
        struct WaylandTextPainterInternal
        {
            /**
             * @brief 라틴 고정폭 글꼴 후보(시스템 글꼴 폴더의 파일 이름, 앞의 것부터 찾는다). `Config/Editor/editortooldefaults.json` 의 `_listBaseFont`(에디터 ImGui 가 쓰는 같은 후보)와 같은 이름이다 —
             *        에디터 설정은 에디터 DLL 의 것이라 엔진 창 계층이 읽지 못해 여기에 이름만 따로 둔다. 후보가 바뀌면 둘을 함께 고친다.
             */
            static constexpr string_view kLatinFonts[] = { "DejaVuSansMono.ttf", "LiberationMono-Regular.ttf", "NotoSansMono-Regular.ttf", "UbuntuMono-R.ttf", "FreeMono.ttf" };
            /** @brief 한글 · 일본어 글꼴 후보입니다(`_listKoreanFont` 의 리눅스 이름). 없으면 그 글자는 건너뛴다. */
            static constexpr string_view kFallbackFonts[] = { "NotoSansCJK-Regular.ttc", "NotoSansCJKkr-Regular.otf", "NotoSansKR-Regular.otf", "NanumGothic.ttf", "NanumBarunGothic.ttf", "DroidSansFallbackFull.ttf" };
            /** @brief 윤곽 둘레로 거리값을 담는 픽셀 수입니다(덮임 비율을 구하는 데 1 픽셀이면 된다). */
            static constexpr uint32 kSpreadPx = 2;
            /** @brief SDF 바이트에서 윤곽(거리 0)의 값과 거리 한 칸(spread 픽셀)이 바이트 몇을 움직이는지입니다(`SdfGlyphBitmap`: 128 = 윤곽, 큰 값 = 안쪽). */
            static constexpr float32 kSdfEdgeValue    = 128.0f;
            static constexpr float32 kSdfValueSpan    = 127.0f;
            static constexpr float32 kCoverageMaxByte = 255.0f;
        };
    } // namespace

    WaylandTextPainter::WaylandTextPainter()
        : _pRasterizer{ nullptr }
        , _latinFace{ kInvalidFontFaceID }
        , _fallbackFace{ kInvalidFontFaceID }
        , _pixelSize{ 0 }
        , _ascent{ 0 }
        , _lineHeight{ 0 }
        , _mapGlyph{}
    {
    }

    WaylandTextPainter::~WaylandTextPainter()
    {
        if ( _pRasterizer != nullptr )
        {
            if ( _fallbackFace != kInvalidFontFaceID )
                _pRasterizer->unloadFace( _fallbackFace );
            if ( _latinFace != kInvalidFontFaceID )
                _pRasterizer->unloadFace( _latinFace );
        }
    }

    FontFaceID WaylandTextPainter::openFirstFace( vector_reference<const string_view> listFileName )
    {
        for ( const string_view fileName : listFileName )
        {
            const string path = SystemFontLocator::findSystemFontFile( fileName );
            if ( path.empty() )
                continue;
            vector<uint8> bytes;
            if ( FileUtil::readFile( path, bytes ) == false )
                continue;
            const FontFaceID face = _pRasterizer->loadFace( std::move( bytes ), 0, fileName );
            if ( face != kInvalidFontFaceID )
                return face;
        }
        return kInvalidFontFaceID;
    }

    bool WaylandTextPainter::initialize( uint32 pixelSize )
    {
        if ( pixelSize == 0 )
            return false;
        _pRasterizer = IFontRasterizer::createDefault();
        if ( _pRasterizer == nullptr )
        {
            SW_LOG_ERROR( "Wayland text: the font rasterizer could not start" );
            return false;
        }
        _pixelSize = pixelSize;
        _latinFace = openFirstFace( WaylandTextPainterInternal::kLatinFonts );
        if ( _latinFace == kInvalidFontFaceID )
        {
            SW_LOG_ERROR( "Wayland text: no Latin font from the system font folders (DejaVuSansMono, LiberationMono, NotoSansMono ...) - text is not drawn" );
            return false;
        }
        _fallbackFace = openFirstFace( WaylandTextPainterInternal::kFallbackFonts );
        if ( _fallbackFace == kInvalidFontFaceID )
            SW_LOG_INFO( "Wayland text: no Korean/Japanese font was found - those letters are skipped" );

        FontFaceMetrics metrics{};
        if ( _pRasterizer->findFaceMetrics( _latinFace, metrics ) == false )
        {
            SW_LOG_ERROR( "Wayland text: the Latin font has no metrics" );
            return false;
        }
        const float32 size  = static_cast<float32>( pixelSize );
        _ascent             = static_cast<int32>( metrics._ascender * size + 0.5f );
        const int32 descent = static_cast<int32>( -metrics._descender * size + 0.5f );
        _lineHeight         = _ascent + descent + 2;
        return true;
    }

    bool WaylandTextPainter::rasterizeGlyph( uint32 codepoint, Glyph& outGlyph )
    {
        FontFaceID face       = _latinFace;
        uint32     glyphIndex = _pRasterizer->findGlyphIndex( face, codepoint );
        if ( glyphIndex == 0 && _fallbackFace != kInvalidFontFaceID )
        {
            face       = _fallbackFace;
            glyphIndex = _pRasterizer->findGlyphIndex( face, codepoint );
        }
        if ( glyphIndex == 0 )
            return false; // 두 글꼴 어디에도 없다
        SdfRasterParams params{};
        params._pixelSize = _pixelSize;
        params._spreadPx  = WaylandTextPainterInternal::kSpreadPx;
        SdfGlyphBitmap bitmap{};
        if ( _pRasterizer->rasterizeSdf( face, glyphIndex, params, bitmap ) == false )
            return false;
        outGlyph._advance = static_cast<int32>( bitmap._advancePx + 0.5f );
        outGlyph._width   = bitmap._width;
        outGlyph._height  = bitmap._height;
        outGlyph._left    = static_cast<int32>( bitmap._bearingPx._x );
        outGlyph._top     = static_cast<int32>( bitmap._bearingPx._y );
        outGlyph._listCoverage.resize( bitmap._bytes.size() );
        // 거리 d(픽셀, 안쪽 +)는 바이트 128 + 127 * d / spread 다 — 가장자리 픽셀의 덮임 비율은 0.5 + d 를 0 ~ 1 로 자른 값이다.
        const float32 slope = static_cast<float32>( WaylandTextPainterInternal::kSpreadPx ) / WaylandTextPainterInternal::kSdfValueSpan;
        for ( size_t index = 0; index < bitmap._bytes.size(); ++index )
        {
            float32 coverage              = 0.5f + ( static_cast<float32>( bitmap._bytes[index] ) - WaylandTextPainterInternal::kSdfEdgeValue ) * slope;
            coverage                      = coverage < 0.0f ? 0.0f : ( coverage > 1.0f ? 1.0f : coverage );
            outGlyph._listCoverage[index] = static_cast<uint8>( coverage * WaylandTextPainterInternal::kCoverageMaxByte + 0.5f );
        }
        return true;
    }

    const WaylandTextPainter::Glyph& WaylandTextPainter::findGlyph( uint32 codepoint )
    {
        const auto found = _mapGlyph.find( codepoint );
        if ( found != _mapGlyph.end() )
            return found->second;
        Glyph glyph{};
        glyph._bAvailable = rasterizeGlyph( codepoint, glyph );
        Glyph& slot       = _mapGlyph[codepoint];
        slot              = std::move( glyph );
        return slot;
    }

    int32 WaylandTextPainter::drawText( uint32* pPixels, uint32 width, uint32 height, int32 originX, int32 baselineY, string_view text, uint32 rgb )
    {
        const bool bCannotDraw = pPixels == nullptr || _pRasterizer == nullptr || _latinFace == kInvalidFontFaceID;
        if ( bCannotDraw )
            return 0;
        const int32  targetWidth  = static_cast<int32>( width );
        const int32  targetHeight = static_cast<int32>( height );
        const uint32 foreRed      = ( rgb >> 16 ) & 0xFFu;
        const uint32 foreGreen    = ( rgb >> 8 ) & 0xFFu;
        const uint32 foreBlue     = rgb & 0xFFu;

        int32  penX   = originX;
        size_t offset = 0;
        while ( offset < text.size() )
        {
            const uint32 codepoint = StringUtil::decodeUtf8( text, offset );
            const Glyph& glyph     = findGlyph( codepoint );
            if ( glyph._bAvailable == false )
                continue; // 글꼴에 없는 글자는 건너뛴다
            const int32 boxLeft = penX + glyph._left;
            const int32 boxTop  = baselineY - glyph._top;
            for ( uint32 row = 0; row < glyph._height; ++row )
            {
                const int32 targetY = boxTop + static_cast<int32>( row );
                if ( targetY < 0 || targetY >= targetHeight )
                    continue;
                uint32* pRow = pPixels + static_cast<size_t>( targetY ) * static_cast<size_t>( width );
                for ( uint32 column = 0; column < glyph._width; ++column )
                {
                    const int32 targetX = boxLeft + static_cast<int32>( column );
                    if ( targetX < 0 || targetX >= targetWidth )
                        continue;
                    const uint32 alpha = glyph._listCoverage[static_cast<size_t>( row ) * glyph._width + column];
                    if ( alpha == 0 )
                        continue;
                    const uint32 back      = pRow[targetX];
                    const uint32 backRed   = ( back >> 16 ) & 0xFFu;
                    const uint32 backGreen = ( back >> 8 ) & 0xFFu;
                    const uint32 backBlue  = back & 0xFFu;
                    const uint32 red       = ( foreRed * alpha + backRed * ( 255u - alpha ) ) / 255u;
                    const uint32 green     = ( foreGreen * alpha + backGreen * ( 255u - alpha ) ) / 255u;
                    const uint32 blue      = ( foreBlue * alpha + backBlue * ( 255u - alpha ) ) / 255u;
                    pRow[targetX]          = ( red << 16 ) | ( green << 8 ) | blue;
                }
            }
            penX += glyph._advance;
        }
        return penX - originX;
    }
} // namespace sw
