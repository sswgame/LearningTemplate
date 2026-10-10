/**
 * @file FakeFontRasterizer.h
 * @brief 글자 시험이 쓰는 결정적인 가짜 래스터라이저입니다(스위트 아님).
 * @details 면은 `loadFace` 의 debugName(= 글꼴 경로)으로 미리 적은 설정을 찾습니다. 글리프 번호 = 코드 포인트(가진 범위 안일 때), 전진 0.5 em
 *          (공백 0.25 em), 상승 0.8 · 하강 -0.2(설정으로 바꾼다), 커닝은 적은 쌍만, SDF 는 4×4 의 128 입니다. 래스터화 횟수를 셉니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Text/FontSystem.h"
#include "Engine/Text/IFontRasterizer.h"

namespace sw::test
{
    /** @brief 가짜 면 하나의 설정입니다. */
    struct FakeFontFaceConfig
    {
        struct CodepointRange
        {
            uint32 _first{ 0 };
            uint32 _last{ 0 };
        };

        vector<CodepointRange> _listRange{};        ///< 가진 코드 포인트 범위(비면 모두 가진다)
        float32                _ascender{ 0.8f };   ///< em 비율
        float32                _descender{ -0.2f }; ///< em 비율
        float32                _lineGap{ 0.0f };    ///< em 비율

        /** @brief 가진 범위를 더합니다(체이닝). */
        FakeFontFaceConfig& addRange( uint32 first, uint32 last )
        {
            _listRange.push_back( CodepointRange{ first, last } );
            return *this;
        }

        bool hasCodepoint( uint32 codepoint ) const
        {
            if ( _listRange.empty() )
                return true;
            for ( const CodepointRange& range : _listRange )
            {
                if ( range._first <= codepoint && codepoint <= range._last )
                    return true;
            }
            return false;
        }
    };
} // namespace sw::test

namespace sw::test
{
    /** @brief 결정적인 가짜 래스터라이저입니다. */
    class FakeFontRasterizer final : public IFontRasterizer
    {
    public:
        static constexpr float32 kGlyphAdvance = 0.5f;  ///< 글자 전진(em)
        static constexpr float32 kSpaceAdvance = 0.25f; ///< 공백 전진(em)
        static constexpr uint32  kSdfSize      = 4;     ///< SDF 한 변의 기본값(래스터 픽셀)

        /** @brief 그 경로로 열릴 면의 설정을 적습니다. 적지 않은 경로는 모든 코드 포인트를 가진 기본 설정입니다. */
        void setFaceConfig( const string& path, const FakeFontFaceConfig& config ) { _mapConfigByPath[path] = config; }
        /** @brief 두 코드 포인트 사이 커닝(em)을 적습니다(모든 면 공통). */
        void setKerning( uint32 leftCodepoint, uint32 rightCodepoint, float32 kerning ) { _mapKerning[makePairKey( leftCodepoint, rightCodepoint )] = kerning; }
        /** @brief SDF 비트맵 한 변을 바꿉니다(아틀라스를 빨리 채우는 시험). */
        void setSdfSize( uint32 size ) { _sdfSize = size; }
        /** @brief 지금까지 래스터화한 횟수입니다. */
        uint32 getRasterizeCount() const { return _rasterizeCount; }
        /** @brief 지금까지 연 면 수입니다(같은 파일을 두 번 열지 않는지 본다). */
        uint32 getLoadCount() const { return _loadCount; }

        FontFaceID loadFace( vector<uint8> fileBytes, uint32 faceIndex, string_view debugName ) override
        {
            (void)faceIndex;
            if ( fileBytes.empty() )
                return kInvalidFontFaceID;
            ++_loadCount;
            const auto       iter   = _mapConfigByPath.find( string( debugName ) );
            const FontFaceID faceID = _nextFaceID++;
            _mapFace[faceID]        = iter != _mapConfigByPath.end() ? iter->second : FakeFontFaceConfig{};
            return faceID;
        }

        void unloadFace( FontFaceID face ) override { _mapFace.erase( face ); }

        bool findFaceMetrics( FontFaceID face, FontFaceMetrics& outMetrics ) const override
        {
            const auto iter = _mapFace.find( face );
            if ( iter == _mapFace.end() )
                return false;
            outMetrics            = FontFaceMetrics{};
            outMetrics._ascender  = iter->second._ascender;
            outMetrics._descender = iter->second._descender;
            outMetrics._lineGap   = iter->second._lineGap;
            return true;
        }

        uint32 findGlyphIndex( FontFaceID face, uint32 codepoint ) const override
        {
            const auto iter = _mapFace.find( face );
            if ( iter == _mapFace.end() || iter->second.hasCodepoint( codepoint ) == false )
                return 0;
            return codepoint;
        }

        bool findGlyphMetrics( FontFaceID face, uint32 glyphIndex, GlyphMetrics& outMetrics ) const override
        {
            if ( _mapFace.find( face ) == _mapFace.end() )
                return false;
            outMetrics          = GlyphMetrics{};
            outMetrics._advance = glyphIndex == ' ' ? kSpaceAdvance : kGlyphAdvance;
            outMetrics._size    = float2{ outMetrics._advance, 0.7f };
            outMetrics._bearing = float2{ 0.0f, 0.7f };
            return true;
        }

        float32 getKerning( FontFaceID face, uint32 leftGlyph, uint32 rightGlyph ) const override
        {
            (void)face;
            const auto iter = _mapKerning.find( makePairKey( leftGlyph, rightGlyph ) );
            return iter != _mapKerning.end() ? iter->second : 0.0f;
        }

        const vector<uint8>* findFaceBytes( FontFaceID face ) const override
        {
            (void)face;
            return nullptr;
        }

        bool rasterizeSdf( FontFaceID face, uint32 glyphIndex, const SdfRasterParams& params, SdfGlyphBitmap& outBitmap ) override
        {
            if ( _mapFace.find( face ) == _mapFace.end() )
                return false;
            ++_rasterizeCount;
            const float32 advance = glyphIndex == ' ' ? kSpaceAdvance : kGlyphAdvance;
            outBitmap._advancePx  = advance * static_cast<float32>( params._pixelSize );
            if ( glyphIndex == ' ' )
            {
                outBitmap._bytes.clear();
                outBitmap._width  = 0;
                outBitmap._height = 0;
                return true;
            }
            outBitmap._bytes.assign( static_cast<size_t>( _sdfSize ) * _sdfSize, static_cast<uint8>( 128 ) );
            outBitmap._width     = _sdfSize;
            outBitmap._height    = _sdfSize;
            outBitmap._bearingPx = float2{ 0.0f, static_cast<float32>( _sdfSize ) };
            return true;
        }

    private:
        static uint64 makePairKey( uint32 left, uint32 right ) { return ( static_cast<uint64>( left ) << 32 ) | right; }

        unordered_map<string, FakeFontFaceConfig>     _mapConfigByPath{};
        unordered_map<FontFaceID, FakeFontFaceConfig> _mapFace{};
        unordered_map<uint64, float32>                _mapKerning{};
        FontFaceID                                    _nextFaceID{ 1 };
        uint32                                        _rasterizeCount{ 0 };
        uint32                                        _sdfSize{ kSdfSize };
        uint32                                        _loadCount{ 0 };
    };
} // namespace sw::test

namespace sw::test
{
    /**
     * @brief 가짜 래스터라이저를 든 FontSystem 을 세우는 도우미입니다. 카탈로그 면 경로마다 메모리 글꼴 한 바이트를 등록해 "파일" 이 읽히게 합니다.
     * @details 래스터라이저는 FontSystem 이 소유하므로, 설정을 고칠 포인터(`_pRasterizer`)를 따로 듭니다.
     */
    struct FakeFontSystemFixture
    {
        FakeFontRasterizer*    _pRasterizer; ///< FontSystem 이 소유한 가짜(설정을 고칠 때)
        unique_ptr<FontSystem> _fontSystem;  ///< 시험 대상

        explicit FakeFontSystemFixture( const LocalizationManager* pLocalization = nullptr )
            : _pRasterizer{ nullptr }
            , _fontSystem{}
        {
            unique_ptr<FakeFontRasterizer> rasterizer = sw::make_unique<FakeFontRasterizer>();
            _pRasterizer                              = rasterizer.get();
            _fontSystem                               = sw::make_unique<FontSystem>( std::move( rasterizer ), pLocalization );
        }

        /** @brief 카탈로그의 모든 저장소 면 경로를 메모리 글꼴로 등록하고 시작합니다. */
        [[nodiscard]] bool initialize( const FontCatalogDesc& catalog )
        {
            for ( const FontFamilyDesc& family : catalog._listFamily )
            {
                for ( const FontFaceDesc& face : family._listFace )
                {
                    _fontSystem->registerMemoryFontFile( face._path, vector<uint8>( 1, static_cast<uint8>( 1 ) ) );
                }
            }
            return _fontSystem->initializeFromCatalog( catalog );
        }

        /** @brief 저장소 가족 하나(면 하나)를 카탈로그에 더합니다. */
        static void addFamily( FontCatalogDesc& inoutCatalog, const string& name, const string& path, FontWeight weight = FontWeight::Regular )
        {
            FontFamilyDesc family{};
            family._name = name;
            FontFaceDesc face{};
            face._path   = path;
            face._weight = weight;
            family._listFace.push_back( face );
            inoutCatalog._listFamily.push_back( family );
        }
    };
} // namespace sw::test
