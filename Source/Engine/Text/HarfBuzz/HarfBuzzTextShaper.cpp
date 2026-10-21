#include "pch.h"

#if defined( SW_TEXT_COMPLEX_SHAPING )
    #include "Engine/Text/HarfBuzz/ComplexTextShaper.h"

    #include "Core/Container/StringUtil.h"
    #include "Core/Container/unordered_map.h"
    #include "Core/Log/Logger.h"

    #include "Engine/Text/IFontRasterizer.h"

    #include <hb.h>

namespace sw
{
    SW_LOG_CALLER( "HarfBuzzTextShaper" );

    namespace
    {
        /**
         * @class HarfBuzzTextShaper
         * @brief `ITextShaper` 의 HarfBuzz 구현입니다. 면마다 `hb_font` 하나를 캐시합니다(게임 스레드 — 래스터라이저와 같은 규칙).
         * @details 결과는 `ITextShaper` 계약대로 **논리 순서**다. HarfBuzz 는 RTL 런을 눈에 보이는 순서로 내므로 뒤집어 붙인다(배치가 다시 뒤집는다).
         *          길이는 em 비율이다 — 글꼴 단위(upem)로 셰이핑하고 upem 으로 나눈다. y 오프셋은 HarfBuzz 의 위 + 를 배치의 아래 + 로 뒤집는다.
         */
        class HarfBuzzTextShaper final : public ITextShaper
        {
        public:
            HarfBuzzTextShaper() = default;
            ~HarfBuzzTextShaper() override
            {
                for ( auto& [face, entry] : _mapFont )
                {
                    (void)face;
                    hb_font_destroy( entry._pFont );
                }
            }

            void shape( const IFontRasterizer& rasterizer, const ShapingRun& run, vector<ShapedGlyph>& inoutListGlyph ) const override
            {
                const FontEntry* pEntry = findOrCreateFont( rasterizer, run._face );
                if ( pEntry == nullptr || run._text.empty() )
                    return;

                hb_buffer_t* pBuffer = hb_buffer_create();
                hb_buffer_add_utf8( pBuffer, run._text.data(), static_cast<int32>( run._text.size() ), 0, static_cast<int32>( run._text.size() ) );
                hb_buffer_set_direction( pBuffer, run._direction == TextDirection::RightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR );
                hb_buffer_guess_segment_properties( pBuffer ); // 문자 체계 · 언어(방향은 위에서 정했다)
                hb_shape( pEntry->_pFont, pBuffer, nullptr, 0 );

                uint32                     glyphCount = 0;
                const hb_glyph_info_t*     pInfo      = hb_buffer_get_glyph_infos( pBuffer, &glyphCount );
                const hb_glyph_position_t* pPosition  = hb_buffer_get_glyph_positions( pBuffer, &glyphCount );
                const float32              invUpem    = 1.0f / static_cast<float32>( pEntry->_upem );
                for ( uint32 glyphIndex = 0; glyphIndex < glyphCount; ++glyphIndex )
                {
                    // RTL 은 눈에 보이는 순서로 나왔다 — 끝에서부터 읽어 논리 순서로 붙인다.
                    const uint32 source = run._direction == TextDirection::RightToLeft ? glyphCount - 1 - glyphIndex : glyphIndex;
                    size_t       cursor = pInfo[source].cluster;
                    ShapedGlyph  glyph{};
                    glyph._face       = run._face;
                    glyph._glyphIndex = pInfo[source].codepoint; // 셰이핑 뒤의 codepoint 칸은 글리프 번호다
                    glyph._cluster    = run._byteOffset + pInfo[source].cluster;
                    glyph._codepoint  = cursor < run._text.size() ? StringUtil::decodeUtf8( run._text, cursor ) : 0u;
                    glyph._advance    = static_cast<float32>( pPosition[source].x_advance ) * invUpem;
                    glyph._offset     = float2{ static_cast<float32>( pPosition[source].x_offset ) * invUpem, -static_cast<float32>( pPosition[source].y_offset ) * invUpem };
                    inoutListGlyph.push_back( glyph );
                }
                hb_buffer_destroy( pBuffer );
            }

        private:
            struct FontEntry
            {
                hb_font_t*   _pFont{ nullptr };
                const uint8* _pBytes{ nullptr }; ///< 면을 만든 바이트 — 같은 번호에 다른 바이트가 오면(면을 닫고 다시 열었다) 다시 만든다
                size_t       _byteCount{ 0 };
                uint32       _upem{ 1 };
            };

            const FontEntry* findOrCreateFont( const IFontRasterizer& rasterizer, FontFaceID face ) const
            {
                const vector<uint8>* pBytes = rasterizer.findFaceBytes( face );
                if ( pBytes == nullptr || pBytes->empty() )
                {
                    SW_LOG_ERROR( "Text face %# has no font bytes to shape with", static_cast<uint32>( face ) );
                    return nullptr;
                }
                const auto it         = _mapFont.find( face );
                const bool bSameBytes = it != _mapFont.end() && it->second._pBytes == pBytes->data() && it->second._byteCount == pBytes->size();
                if ( bSameBytes )
                    return &it->second;
                if ( it != _mapFont.end() )
                {
                    hb_font_destroy( it->second._pFont );
                    _mapFont.erase( it );
                }
                // 바이트는 래스터라이저가 면이 사는 동안 들고 있다 — 읽기 전용으로 빌린다(복사하지 않는다).
                hb_blob_t* pBlob = hb_blob_create( reinterpret_cast<const utf8*>( pBytes->data() ), static_cast<uint32>( pBytes->size() ), HB_MEMORY_MODE_READONLY,
                                                   nullptr, nullptr );
                hb_face_t* pFace = hb_face_create( pBlob, 0 );
                hb_blob_destroy( pBlob );
                FontEntry entry;
                entry._upem  = hb_face_get_upem( pFace );
                entry._pFont = hb_font_create( pFace );
                hb_face_destroy( pFace );
                if ( entry._upem == 0 )
                {
                    SW_LOG_ERROR( "Text face %# has no units per em - it cannot be shaped", static_cast<uint32>( face ) );
                    hb_font_destroy( entry._pFont );
                    return nullptr;
                }
                hb_font_set_scale( entry._pFont, static_cast<int32>( entry._upem ), static_cast<int32>( entry._upem ) );
                entry._pBytes    = pBytes->data();
                entry._byteCount = pBytes->size();
                return &_mapFont.emplace( face, entry ).first->second;
            }

            mutable unordered_map<FontFaceID, FontEntry> _mapFont;
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<ITextShaper> ComplexTextShaperUtil::create()
    {
        return make_unique<HarfBuzzTextShaper>();
    }
} // namespace sw
#endif
