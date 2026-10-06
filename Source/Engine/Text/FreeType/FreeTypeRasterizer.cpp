#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Text/IFontRasterizer.h"

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftmodapi.h>

namespace sw
{
    SW_LOG_CALLER( "FreeTypeRasterizer" );

    namespace
    {
        struct FreeTypeRasterizerInternal
        {
            /** @brief 26.6 고정소수점을 픽셀로 바꿉니다. */
            static float32 fromFixed26Dot6( FT_Pos value ) { return static_cast<float32>( value ) / 64.0f; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @class FreeTypeRasterizer
     * @brief FreeType 로 면을 열고 SDF 를 래스터화합니다. SDF 는 FreeType 2.11+ 의 `sdf` 렌더러(윤곽에서 정확한 거리)입니다.
     * @details 헤더가 없다 — 밖은 `IFontRasterizer::createDefault()` 로만 본다(FreeType 타입이 이 파일 밖으로 새지 않는다).
     */
    class FreeTypeRasterizer final : public IFontRasterizer
    {
    public:
        FreeTypeRasterizer()
            : _mapFace{}
            , _pLibrary{ nullptr }
            , _nextFaceId{ 1 }
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
            // 거리값을 담는 폭 — SdfRasterParams 기본값과 같다(셰이더의 거리 환산이 같은 값을 쓴다).
            FT_Int spread = static_cast<FT_Int>( SdfRasterParams{}._spreadPx );
            (void)FT_Property_Set( _pLibrary, "sdf", "spread", &spread );  // 윤곽 → SDF
            (void)FT_Property_Set( _pLibrary, "bsdf", "spread", &spread ); // 비트맵 → SDF(윤곽 없는 경우의 대체)
            return true;
        }

        void shutdown()
        {
            for ( auto& [faceId, entry] : _mapFace )
            {
                (void)faceId;
                FT_Done_Face( entry._pFace );
            }
            _mapFace.clear();
            if ( _pLibrary != nullptr )
            {
                FT_Done_FreeType( _pLibrary );
                _pLibrary = nullptr;
            }
        }

        FontFaceId loadFace( vector<uint8> fileBytes, uint32 faceIndex, string_view debugName ) override
        {
            FaceEntry entry{};
            entry._bytes         = std::move( fileBytes );
            FT_Face        pFace = nullptr;
            const FT_Error error = FT_New_Memory_Face( _pLibrary, entry._bytes.data(), static_cast<FT_Long>( entry._bytes.size() ), static_cast<FT_Long>( faceIndex ), &pFace );
            if ( error != 0 || pFace == nullptr )
            {
                SW_LOG_ERROR( "[Text] Failed to open font face '%#' (index %#, FreeType error %#)", debugName, faceIndex, static_cast<int32>( error ) );
                return kInvalidFontFaceId;
            }
            if ( FT_IS_SCALABLE( pFace ) == 0 )
            {
                SW_LOG_ERROR( "[Text] Font face '%#' has no outlines; only scalable fonts are supported", debugName );
                FT_Done_Face( pFace );
                return kInvalidFontFaceId;
            }
            entry._pFace            = pFace;
            entry._unitsPerEm       = static_cast<float32>( pFace->units_per_EM );
            const FontFaceId faceId = _nextFaceId++;
            _mapFace[faceId]        = std::move( entry );
            return faceId;
        }

        void unloadFace( FontFaceId face ) override
        {
            auto iter = _mapFace.find( face );
            if ( iter == _mapFace.end() )
                return;
            FT_Done_Face( iter->second._pFace );
            _mapFace.erase( iter );
        }

        bool findFaceMetrics( FontFaceId face, FontFaceMetrics& outMetrics ) const override
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

        uint32 findGlyphIndex( FontFaceId face, uint32 codepoint ) const override
        {
            const FaceEntry* pEntry = findEntry( face );
            return pEntry != nullptr ? static_cast<uint32>( FT_Get_Char_Index( pEntry->_pFace, static_cast<FT_ULong>( codepoint ) ) ) : 0u;
        }

        bool findGlyphMetrics( FontFaceId face, uint32 glyphIndex, GlyphMetrics& outMetrics ) const override
        {
            const FaceEntry* pEntry = findEntry( face );
            if ( pEntry == nullptr )
                return false;
            // 크기 없이 글꼴 단위로 읽어 em 비율로 나눈다 — 래스터 크기와 무관한 배치 값이다.
            if ( FT_Load_Glyph( pEntry->_pFace, glyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING ) != 0 )
                return false;
            const FT_Glyph_Metrics& metrics = pEntry->_pFace->glyph->metrics;
            const float32           inverse = 1.0f / pEntry->_unitsPerEm;
            outMetrics._bearing             = float2{ static_cast<float32>( metrics.horiBearingX ) * inverse, static_cast<float32>( metrics.horiBearingY ) * inverse };
            outMetrics._size                = float2{ static_cast<float32>( metrics.width ) * inverse, static_cast<float32>( metrics.height ) * inverse };
            outMetrics._advance             = static_cast<float32>( metrics.horiAdvance ) * inverse;
            return true;
        }

        float32 getKerning( FontFaceId face, uint32 leftGlyph, uint32 rightGlyph ) const override
        {
            const FaceEntry* pEntry = findEntry( face );
            if ( pEntry == nullptr || FT_HAS_KERNING( pEntry->_pFace ) == 0 )
                return 0.0f;
            FT_Vector delta{};
            if ( FT_Get_Kerning( pEntry->_pFace, leftGlyph, rightGlyph, FT_KERNING_UNSCALED, &delta ) != 0 )
                return 0.0f;
            return static_cast<float32>( delta.x ) / pEntry->_unitsPerEm;
        }

        const vector<uint8>* findFaceBytes( FontFaceId face ) const override
        {
            const FaceEntry* pEntry = findEntry( face );
            return pEntry != nullptr ? &pEntry->_bytes : nullptr;
        }

        bool rasterizeSdf( FontFaceId face, uint32 glyphIndex, const SdfRasterParams& params, SdfGlyphBitmap& outBitmap ) override
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
            outBitmap._advancePx = FreeTypeRasterizerInternal::fromFixed26Dot6( pSlot->advance.x );
            outBitmap._bytes.clear();
            outBitmap._width         = 0;
            outBitmap._height        = 0;
            outBitmap._bearingPx     = float2{};
            const bool bEmptyOutline = pSlot->format == FT_GLYPH_FORMAT_OUTLINE && pSlot->outline.n_points == 0;
            if ( bEmptyOutline )
                return true; // 공백 — 전진만 있다
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
            FT_Face       _pFace{ nullptr };   ///< 면 핸들
            float32       _unitsPerEm{ 1.0f }; ///< 글꼴 단위 → em 비율
        };

        const FaceEntry* findEntry( FontFaceId face ) const
        {
            const auto iter = _mapFace.find( face );
            return iter != _mapFace.end() ? &iter->second : nullptr;
        }

        unordered_map<FontFaceId, FaceEntry> _mapFace;
        FT_Library                           _pLibrary;
        FontFaceId                           _nextFaceId;
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
