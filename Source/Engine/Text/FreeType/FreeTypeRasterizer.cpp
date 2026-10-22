#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Text/IFontRasterizer.h"
#include "Engine/Text/ITextShaper.h"
#include "Engine/Text/Msdf/MsdfGenerator.h"

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftmodapi.h>
#include <freetype/ftoutln.h>

namespace sw
{
    SW_LOG_CALLER( "FreeTypeRasterizer" );

    namespace
    {
        /** @brief 26.6 고정소수점을 픽셀로 바꿉니다. */
        float32 fromFixed26Dot6( FT_Pos value )
        {
            return static_cast<float32>( value ) / 64.0f;
        }

        /** @brief FreeType 윤곽을 훑으며 `MsdfShape` 로 옮기는 상태입니다(`FT_Outline_Decompose` 의 사용자 값). */
        struct FreeTypeOutlineBuilder
        {
            MsdfShape* _pShape{ nullptr };
            float2     _current{};

            static float2 toPoint( const FT_Vector* pVector ) { return float2{ fromFixed26Dot6( pVector->x ), fromFixed26Dot6( pVector->y ) }; }

            MsdfContour& getContour() { return _pShape->_listContour.back(); }

            static int32 moveTo( const FT_Vector* pTo, void* pUser )
            {
                FreeTypeOutlineBuilder& builder = *static_cast<FreeTypeOutlineBuilder*>( pUser );
                builder._pShape->_listContour.emplace_back();
                builder._current = toPoint( pTo );
                return 0;
            }
            static int32 lineTo( const FT_Vector* pTo, void* pUser )
            {
                FreeTypeOutlineBuilder& builder = *static_cast<FreeTypeOutlineBuilder*>( pUser );
                MsdfEdge                edge{};
                edge._kind        = MsdfEdgeKind::Linear;
                edge._arrPoint[0] = builder._current;
                edge._arrPoint[1] = toPoint( pTo );
                builder.pushEdge( edge, edge._arrPoint[1] );
                return 0;
            }
            static int32 conicTo( const FT_Vector* pControl, const FT_Vector* pTo, void* pUser )
            {
                FreeTypeOutlineBuilder& builder = *static_cast<FreeTypeOutlineBuilder*>( pUser );
                MsdfEdge                edge{};
                edge._kind        = MsdfEdgeKind::Quadratic;
                edge._arrPoint[0] = builder._current;
                edge._arrPoint[1] = toPoint( pControl );
                edge._arrPoint[2] = toPoint( pTo );
                builder.pushEdge( edge, edge._arrPoint[2] );
                return 0;
            }
            static int32 cubicTo( const FT_Vector* pControl1, const FT_Vector* pControl2, const FT_Vector* pTo, void* pUser )
            {
                FreeTypeOutlineBuilder& builder = *static_cast<FreeTypeOutlineBuilder*>( pUser );
                MsdfEdge                edge{};
                edge._kind        = MsdfEdgeKind::Cubic;
                edge._arrPoint[0] = builder._current;
                edge._arrPoint[1] = toPoint( pControl1 );
                edge._arrPoint[2] = toPoint( pControl2 );
                edge._arrPoint[3] = toPoint( pTo );
                builder.pushEdge( edge, edge._arrPoint[3] );
                return 0;
            }
            void pushEdge( const MsdfEdge& edge, const float2& end )
            {
                getContour()._listEdge.push_back( edge );
                _current = end;
            }
        };

        /** @brief 윤곽의 진행 방향을 뒤집습니다(간선 순서와 간선 안의 점 순서). */
        void reverseContour( MsdfContour& inoutContour )
        {
            std::reverse( inoutContour._listEdge.begin(), inoutContour._listEdge.end() );
            for ( MsdfEdge& edge : inoutContour._listEdge )
            {
                const size_t pointCount = edge._kind == MsdfEdgeKind::Linear ? 2 : ( edge._kind == MsdfEdgeKind::Quadratic ? 3 : 4 );
                std::reverse( edge._arrPoint, edge._arrPoint + pointCount );
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @class FreeTypeRasterizer
     * @brief FreeType 로 면을 열고 SDF 를 래스터화합니다. SDF 는 FreeType 2.11+ 의 `sdf` 렌더러(윤곽에서 정확한 거리)입니다.
     * @details 헤더가 없다. 밖에서는 `IFontRasterizer::createDefault()` 로만 본다(FreeType 타입이 이 파일 밖으로 새지 않는다).
     */
    class FreeTypeRasterizer final : public IFontRasterizer
    {
    public:
        FreeTypeRasterizer()
            : _mapFace{}
            , _pLibrary{ nullptr }
            , _nextFaceID{ 1 }
        {
        }

        ~FreeTypeRasterizer() override { shutdown(); }

        FreeTypeRasterizer( const FreeTypeRasterizer& )            = delete;
        FreeTypeRasterizer& operator=( const FreeTypeRasterizer& ) = delete;

        [[nodiscard]] bool initialize()
        {
            FT_Library     pLibrary = nullptr;
            const FT_Error error    = FT_Init_FreeType( &pLibrary );
            if ( error != 0 )
            {
                SW_LOG_ERROR( "[Text] FreeType initialization failed (error %#)", static_cast<int32>( error ) );
                return false;
            }
            _pLibrary = pLibrary;
            // 거리값을 담는 폭이다. SdfRasterParams 기본값과 같다(셰이더의 거리 환산이 같은 값을 쓴다).
            FT_Int spread = static_cast<FT_Int>( SdfRasterParams{}._spreadPx );
            (void)FT_Property_Set( _pLibrary, "sdf", "spread", &spread );  // 윤곽 → SDF
            (void)FT_Property_Set( _pLibrary, "bsdf", "spread", &spread ); // 비트맵 → SDF(윤곽 없는 경우의 대체)
            return true;
        }

        void shutdown()
        {
            for ( auto& [faceID, entry] : _mapFace )
            {
                (void)faceID;
                FT_Done_Face( entry._pFace );
            }
            _mapFace.clear();
            if ( _pLibrary != nullptr )
            {
                FT_Done_FreeType( _pLibrary );
                _pLibrary = nullptr;
            }
        }

        FontFaceID loadFace( vector<uint8> fileBytes, uint32 faceIndex, string_view debugName ) override
        {
            FaceEntry entry{};
            entry._bytes         = std::move( fileBytes );
            FT_Face        pFace = nullptr;
            const FT_Error error = FT_New_Memory_Face( _pLibrary, entry._bytes.data(), static_cast<FT_Long>( entry._bytes.size() ), static_cast<FT_Long>( faceIndex ), &pFace );
            if ( error != 0 || pFace == nullptr )
            {
                SW_LOG_ERROR( "[Text] Failed to open font face '%#' (index %#, FreeType error %#)", debugName, faceIndex, static_cast<int32>( error ) );
                return kInvalidFontFaceID;
            }
            if ( FT_IS_SCALABLE( pFace ) == 0 )
            {
                SW_LOG_ERROR( "[Text] Font face '%#' has no outlines; only scalable fonts are supported", debugName );
                FT_Done_Face( pFace );
                return kInvalidFontFaceID;
            }
            entry._pFace            = pFace;
            entry._unitsPerEm       = static_cast<float32>( pFace->units_per_EM );
            const FontFaceID faceID = _nextFaceID++;
            _mapFace[faceID]        = std::move( entry );
            return faceID;
        }

        unique_ptr<ITextShaper> createShaper() const override { return ITextShaper::createDefault(); }

        bool rasterizeMsdf( FontFaceID face, uint32 glyphIndex, const SdfRasterParams& params, SdfGlyphBitmap& outBitmap ) override
        {
            const FaceEntry* pEntry = findEntry( face );
            if ( pEntry == nullptr )
                return false;
            FT_Face pFace = pEntry->_pFace;
            if ( FT_Set_Pixel_Sizes( pFace, 0, params._pixelSize ) != 0 )
                return false;
            if ( FT_Load_Glyph( pFace, glyphIndex, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP ) != 0 )
            {
                SW_LOG_ERROR( "[Text] Failed to load glyph %# for MSDF", glyphIndex );
                return false;
            }
            FT_GlyphSlot pSlot   = pFace->glyph;
            outBitmap._advancePx = fromFixed26Dot6( pSlot->advance.x );
            outBitmap._bytes.clear();
            outBitmap._width     = 0;
            outBitmap._height    = 0;
            outBitmap._bearingPx = float2{};
            if ( pSlot->format != FT_GLYPH_FORMAT_OUTLINE )
            {
                SW_LOG_ERROR( "[Text] Glyph %# has no outline - MSDF needs vector outlines", glyphIndex );
                return false;
            }
            if ( pSlot->outline.n_points == 0 )
                return true; // 공백이라 전진만 있다

            MsdfShape              shape{};
            FreeTypeOutlineBuilder builder{ &shape, float2{} };
            FT_Outline_Funcs       funcs{};
            funcs.move_to  = &FreeTypeOutlineBuilder::moveTo;
            funcs.line_to  = &FreeTypeOutlineBuilder::lineTo;
            funcs.conic_to = &FreeTypeOutlineBuilder::conicTo;
            funcs.cubic_to = &FreeTypeOutlineBuilder::cubicTo;
            if ( FT_Outline_Decompose( &pSlot->outline, &funcs, &builder ) != 0 )
            {
                SW_LOG_ERROR( "[Text] Failed to read the outline of glyph %# for MSDF", glyphIndex );
                return false;
            }
            // MSDF 는 안쪽이 간선 왼쪽이어야 한다. TrueType 윤곽은 안쪽이 오른쪽(시계 방향)이라 뒤집는다. PostScript 윤곽은 이미 왼쪽이다.
            if ( FT_Outline_Get_Orientation( &pSlot->outline ) == FT_ORIENTATION_TRUETYPE )
            {
                for ( MsdfContour& contour : shape._listContour )
                {
                    reverseContour( contour );
                }
            }
            MsdfGenerator::colorEdges( shape );

            FT_BBox box{};
            FT_Outline_Get_CBox( &pSlot->outline, &box );
            const int32 spread = static_cast<int32>( params._spreadPx );
            const int32 left   = static_cast<int32>( MathUtil::floor( fromFixed26Dot6( box.xMin ) ) ) - spread;
            const int32 right  = static_cast<int32>( MathUtil::ceil( fromFixed26Dot6( box.xMax ) ) ) + spread;
            const int32 bottom = static_cast<int32>( MathUtil::floor( fromFixed26Dot6( box.yMin ) ) ) - spread;
            const int32 top    = static_cast<int32>( MathUtil::ceil( fromFixed26Dot6( box.yMax ) ) ) + spread;
            MsdfParams  msdfParams{};
            msdfParams._width   = static_cast<uint32>( right - left );
            msdfParams._height  = static_cast<uint32>( top - bottom );
            msdfParams._origin  = float2{ static_cast<float32>( left ), static_cast<float32>( top ) };
            msdfParams._rangePx = static_cast<float32>( spread );
            MsdfGenerator::computeBitmap( shape, msdfParams, outBitmap._bytes );
            outBitmap._width     = msdfParams._width;
            outBitmap._height    = msdfParams._height;
            outBitmap._bearingPx = float2{ static_cast<float32>( left ), static_cast<float32>( top ) };
            return true;
        }

        void unloadFace( FontFaceID face ) override
        {
            auto iter = _mapFace.find( face );
            if ( iter == _mapFace.end() )
                return;
            FT_Done_Face( iter->second._pFace );
            _mapFace.erase( iter );
        }

        bool findFaceMetrics( FontFaceID face, FontFaceMetrics& outMetrics ) const override
        {
            const FaceEntry* pEntry = findEntry( face );
            if ( pEntry == nullptr )
                return false;
            const FT_Face pFace            = pEntry->_pFace;
            const float32 inverseEm        = 1.0f / pEntry->_unitsPerEm;
            outMetrics._ascender           = static_cast<float32>( pFace->ascender ) * inverseEm;
            outMetrics._descender          = static_cast<float32>( pFace->descender ) * inverseEm;
            outMetrics._lineGap            = static_cast<float32>( pFace->height - pFace->ascender + pFace->descender ) * inverseEm;
            outMetrics._underlinePosition  = static_cast<float32>( pFace->underline_position ) * inverseEm;
            outMetrics._underlineThickness = static_cast<float32>( pFace->underline_thickness ) * inverseEm;
            return true;
        }

        uint32 findGlyphIndex( FontFaceID face, uint32 codepoint ) const override
        {
            const FaceEntry* pEntry = findEntry( face );
            return pEntry != nullptr ? static_cast<uint32>( FT_Get_Char_Index( pEntry->_pFace, static_cast<FT_ULong>( codepoint ) ) ) : 0u;
        }

        bool findGlyphMetrics( FontFaceID face, uint32 glyphIndex, GlyphMetrics& outMetrics ) const override
        {
            const FaceEntry* pEntry = findEntry( face );
            if ( pEntry == nullptr )
                return false;
            // 크기 없이 폰트 단위로 읽어 em 비율로 나눈다. 래스터 크기와 무관한 배치 값이다.
            if ( FT_Load_Glyph( pEntry->_pFace, glyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING ) != 0 )
                return false;
            const FT_Glyph_Metrics& metrics = pEntry->_pFace->glyph->metrics;
            const float32           inverse = 1.0f / pEntry->_unitsPerEm;
            outMetrics._bearing             = float2{ static_cast<float32>( metrics.horiBearingX ) * inverse, static_cast<float32>( metrics.horiBearingY ) * inverse };
            outMetrics._size                = float2{ static_cast<float32>( metrics.width ) * inverse, static_cast<float32>( metrics.height ) * inverse };
            outMetrics._advance             = static_cast<float32>( metrics.horiAdvance ) * inverse;
            return true;
        }

        float32 getKerning( FontFaceID face, uint32 leftGlyph, uint32 rightGlyph ) const override
        {
            const FaceEntry* pEntry = findEntry( face );
            if ( pEntry == nullptr || FT_HAS_KERNING( pEntry->_pFace ) == 0 )
                return 0.0f;
            FT_Vector delta{};
            if ( FT_Get_Kerning( pEntry->_pFace, leftGlyph, rightGlyph, FT_KERNING_UNSCALED, &delta ) != 0 )
                return 0.0f;
            return static_cast<float32>( delta.x ) / pEntry->_unitsPerEm;
        }

        const vector<uint8>* findFaceBytes( FontFaceID face ) const override
        {
            const FaceEntry* pEntry = findEntry( face );
            return pEntry != nullptr ? &pEntry->_bytes : nullptr;
        }

        bool rasterizeSdf( FontFaceID face, uint32 glyphIndex, const SdfRasterParams& params, SdfGlyphBitmap& outBitmap ) override
        {
            const FaceEntry* pEntry = findEntry( face );
            if ( pEntry == nullptr )
                return false;
            FT_Face pFace = pEntry->_pFace;
            if ( FT_Set_Pixel_Sizes( pFace, 0, params._pixelSize ) != 0 )
                return false;
            if ( FT_Load_Glyph( pFace, glyphIndex, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP ) != 0 )
            {
                SW_LOG_ERROR( "[Text] Failed to load glyph %# for SDF", glyphIndex );
                return false;
            }
            FT_GlyphSlot pSlot   = pFace->glyph;
            outBitmap._advancePx = fromFixed26Dot6( pSlot->advance.x );
            outBitmap._bytes.clear();
            outBitmap._width         = 0;
            outBitmap._height        = 0;
            outBitmap._bearingPx     = float2{};
            const bool bEmptyOutline = pSlot->format == FT_GLYPH_FORMAT_OUTLINE && pSlot->outline.n_points == 0;
            if ( bEmptyOutline )
                return true; // 공백이라 전진만 있다
            const FT_Error error = FT_Render_Glyph( pSlot, FT_RENDER_MODE_SDF );
            if ( error != 0 )
            {
                SW_LOG_ERROR( "[Text] FT_Render_Glyph(SDF) failed for glyph %# (error %#) - is the FreeType 'sdf' module built?", glyphIndex, static_cast<int32>( error ) );
                return false;
            }
            const FT_Bitmap& bitmap = pSlot->bitmap;
            outBitmap._width        = bitmap.width;
            outBitmap._height       = bitmap.rows;
            outBitmap._bearingPx    = float2{ static_cast<float32>( pSlot->bitmap_left ), static_cast<float32>( pSlot->bitmap_top ) };
            outBitmap._bytes.resize( static_cast<size_t>( bitmap.width ) * bitmap.rows );
            // pitch 가 음수(아래 → 위 행)여도 맞게 행마다 시작 주소를 계산한다.
            for ( uint32 row = 0; row < bitmap.rows; ++row )
            {
                const uint8* pSource = bitmap.buffer + static_cast<int64>( row ) * bitmap.pitch;
                Memory::copy( outBitmap._bytes.data() + static_cast<size_t>( row ) * bitmap.width, pSource, bitmap.width );
            }
            return true;
        }

    private:
        struct FaceEntry
        {
            vector<uint8> _bytes{};            ///< FreeType 가 읽는 동안 살아 있어야 하는 파일 바이트(메모리 면)
            FT_Face       _pFace{ nullptr };   ///< 면 핸들입니다.
            float32       _unitsPerEm{ 1.0f }; ///< 폰트 단위에서 em 비율로 바꾸는 값입니다.
        };

        const FaceEntry* findEntry( FontFaceID face ) const
        {
            const auto iter = _mapFace.find( face );
            return iter != _mapFace.end() ? &iter->second : nullptr;
        }

        unordered_map<FontFaceID, FaceEntry> _mapFace;
        FT_Library                           _pLibrary;
        FontFaceID                           _nextFaceID;
    };
} // namespace sw

namespace sw
{
    unique_ptr<IFontRasterizer> IFontRasterizer::createDefault()
    {
        unique_ptr<FreeTypeRasterizer> rasterizer = make_unique<FreeTypeRasterizer>();
        if ( rasterizer->initialize() == false )
            return nullptr;
        return rasterizer;
    }
} // namespace sw
