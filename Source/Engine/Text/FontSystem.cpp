#include "pch.h"

#include "Engine/Text/FontSystem.h"

#include "Core/Common/HashUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Text/SystemFontLocator.h"

namespace sw
{
    SW_LOG_CALLER( "FontSystem" );

    namespace
    {
        struct FontSystemInternal
        {
            /** @brief 경고 키를 나누는 씨앗입니다 — 가족 경고와 코드 포인트 경고가 같은 수로 겹치지 않게. */
            static constexpr uint64 kFamilyWarningSeed    = 1;
            static constexpr uint64 kCodepointWarningSeed = 2;

            /** @brief 이름 해시(대소문자 무시)입니다. */
            static uint64 hashName( string_view name, uint64 seed = 0 ) { return StringUtil::computeHash64( name.data(), name.size(), true, seed ); }

            static uint64 hashFamilyFace( string_view family, FontWeight weight, FontSlant slant )
            {
                uint64 hash = hashName( family );
                hash        = HashUtil::combine( hash, static_cast<uint64>( weight ) );
                return HashUtil::combine( hash, static_cast<uint64>( slant ) );
            }

            /** @brief 굵기 차이입니다(무게 짝짓기 — 작은 쪽, 같으면 무거운 쪽이 이긴다). */
            static uint32 computeWeightDistance( FontWeight requested, FontWeight candidate )
            {
                const int32 difference = static_cast<int32>( requested ) - static_cast<int32>( candidate );
                return static_cast<uint32>( difference < 0 ? -difference : difference );
            }

            /** @brief 요청은 굵은데(SemiBold 이상) 고른 면은 그렇지 않은지(Medium 이하)입니다 — 가짜 굵게를 쓴다. */
            static bool needsFauxBold( FontWeight requested, FontWeight chosen )
            {
                return static_cast<uint16>( requested ) >= static_cast<uint16>( FontWeight::SemiBold ) &&
                       static_cast<uint16>( chosen ) <= static_cast<uint16>( FontWeight::Medium );
            }

            /** @brief 사슬 끝에 면을 더합니다(겹치거나 무효면 건너뛰고, 가득 차면 버린다). */
            static void appendFace( FontFaceChain& inoutChain, FontFaceId face )
            {
                if ( face == kInvalidFontFaceId || inoutChain._faceCount >= FontFaceChain::kMaxFaceCount )
                    return;
                for ( uint32 index = 0; index < inoutChain._faceCount; ++index )
                {
                    if ( inoutChain._arrFace[index] == face )
                        return;
                }
                inoutChain._arrFace[inoutChain._faceCount] = face;
                ++inoutChain._faceCount;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FontSystem::FontSystem()
        : FontSystem( nullptr, nullptr )
    {
    }

    FontSystem::FontSystem( unique_ptr<IFontRasterizer> rasterizer, const LocalizationManager* pLocalization )
        : _rasterizer{ std::move( rasterizer ) }
        , _catalog{}
        , _mapOpenedFamilyFace{}
        , _mapOpenedFile{}
        , _mapChain{}
        , _mapMemoryFile{}
        , _uniqueWarned{}
        , _pLocalization{ pLocalization }
        , _bInitialized{ false }
    {
    }

    FontSystem::~FontSystem() { shutdown(); }

    bool FontSystem::initialize( string_view catalogPath )
    {
        FontCatalogDesc catalog;
        if ( catalog.loadFromResource( catalogPath ) == false )
            return false;
        return initializeFromCatalog( std::move( catalog ) );
    }

    bool FontSystem::initializeFromCatalog( FontCatalogDesc catalog )
    {
        shutdown();
        if ( catalog.validate() == false )
            return false;
        if ( _rasterizer == nullptr )
            _rasterizer = IFontRasterizer::createDefault();
        if ( _rasterizer == nullptr )
            return false;
        _catalog                     = std::move( catalog );
        uint8            bFauxBold   = SW_FALSE;
        uint8            bFauxItalic = SW_FALSE;
        const FontFaceId defaultFace = openFamilyFace( _catalog._defaultFamily, FontWeight::Regular, FontSlant::Upright, bFauxBold, bFauxItalic );
        if ( defaultFace == kInvalidFontFaceId )
        {
            SW_LOG_ERROR( "[Text] The default font family '%#' could not be opened - no text can be drawn", _catalog._defaultFamily.c_str() );
            return false;
        }
        _bInitialized = true;
        return true;
    }

    void FontSystem::shutdown()
    {
        if ( _rasterizer != nullptr )
        {
            for ( const auto& [key, face] : _mapOpenedFile )
            {
                (void)key;
                _rasterizer->unloadFace( face );
            }
        }
        _mapOpenedFamilyFace.clear();
        _mapOpenedFile.clear();
        _mapChain.clear();
        _uniqueWarned.clear();
        _catalog      = FontCatalogDesc{};
        _bInitialized = false;
    }

    void FontSystem::registerMemoryFontFile( string_view path, vector<uint8> bytes )
    {
        _mapMemoryFile[StringUtil::toLower( string( path ).c_str() )] = std::move( bytes );
    }

    FontFaceChain FontSystem::getFaceChain( const FontSpec& spec )
    {
        FontFaceChain chain{};
        if ( _bInitialized == false )
            return chain;
        const LocalizationManager* pLocalization = findLocalization();
        const string               culture       = pLocalization != nullptr ? pLocalization->getCurrentLanguage() : string{};
        const string_view          family        = spec._family.empty() ? string_view{ _catalog._defaultFamily } : string_view{ spec._family };
        const uint64               chainKey      = FontSystemInternal::hashName( culture, FontSystemInternal::hashFamilyFace( family, spec._weight, spec._slant ) );
        const auto                 iter          = _mapChain.find( chainKey );
        if ( iter != _mapChain.end() )
            return iter->second;

        // 1) 고른 가족 — 가짜 굵게 · 기울임은 이 면에서 정한다.
        uint8            bFauxBold   = SW_FALSE;
        uint8            bFauxItalic = SW_FALSE;
        const FontFaceId primaryFace = openFamilyFace( family, spec._weight, spec._slant, bFauxBold, bFauxItalic );
        FontSystemInternal::appendFace( chain, primaryFace );
        if ( primaryFace != kInvalidFontFaceId )
        {
            chain._bFauxBold   = bFauxBold;
            chain._bFauxItalic = bFauxItalic;
        }
        // 2) 지금 문화권의 대체 가족들(문화권 표 `fonts`).
        if ( pLocalization != nullptr )
        {
            for ( const string& fallbackFamily : pLocalization->getFontFallback() )
            {
                uint8 bUnusedBold   = SW_FALSE;
                uint8 bUnusedItalic = SW_FALSE;
                FontSystemInternal::appendFace( chain, openFamilyFace( fallbackFamily, spec._weight, spec._slant, bUnusedBold, bUnusedItalic ) );
            }
        }
        // 3) 카탈로그 기본 가족 — 늘 열린다(initialize 가 확인했다).
        uint8            bDefaultBold   = SW_FALSE;
        uint8            bDefaultItalic = SW_FALSE;
        const FontFaceId defaultFace    = openFamilyFace( _catalog._defaultFamily, spec._weight, spec._slant, bDefaultBold, bDefaultItalic );
        FontSystemInternal::appendFace( chain, defaultFace );
        if ( primaryFace == kInvalidFontFaceId )
        {
            chain._bFauxBold   = bDefaultBold;
            chain._bFauxItalic = bDefaultItalic;
        }
        _mapChain.emplace( chainKey, chain );
        return chain;
    }

    FontFaceId FontSystem::findFaceForCodepoint( const FontFaceChain& chain, uint32 codepoint, uint32& outGlyphIndex )
    {
        outGlyphIndex = 0;
        if ( chain._faceCount == 0 || _rasterizer == nullptr )
            return kInvalidFontFaceId;
        for ( uint32 index = 0; index < chain._faceCount; ++index )
        {
            const uint32 glyphIndex = _rasterizer->findGlyphIndex( chain._arrFace[index], codepoint );
            if ( glyphIndex != 0 )
            {
                outGlyphIndex = glyphIndex;
                return chain._arrFace[index];
            }
        }
        if ( markWarnedOnce( HashUtil::combine( FontSystemInternal::kCodepointWarningSeed, codepoint ) ) )
            SW_LOG_WARNING( "[Text] No font in the fallback chain has a glyph for codepoint %# - it draws as a box (install a system font that covers it)", codepoint );
        return chain._arrFace[0];
    }

    void FontSystem::invalidateFaceChains() { _mapChain.clear(); }

    FontFaceId FontSystem::openFamilyFace( string_view family, FontWeight weight, FontSlant slant, uint8& outFauxBold, uint8& outFauxItalic )
    {
        outFauxBold                = SW_FALSE;
        outFauxItalic              = SW_FALSE;
        const uint64 familyFaceKey = FontSystemInternal::hashFamilyFace( family, weight, slant );
        const auto   iter          = _mapOpenedFamilyFace.find( familyFaceKey );
        if ( iter != _mapOpenedFamilyFace.end() )
        {
            outFauxBold   = iter->second._bFauxBold;
            outFauxItalic = iter->second._bFauxItalic;
            return iter->second._face;
        }

        OpenedFamilyFace            opened{};
        const FontFamilyDesc*       pFamily       = _catalog.findFamily( family );
        const SystemFontFamilyDesc* pSystemFamily = pFamily == nullptr ? _catalog.findSystemFamily( family ) : nullptr;
        if ( pFamily != nullptr )
        {
            // 같은 기울기를 먼저, 그 안에서 굵기가 가장 가까운 면(같으면 무거운 쪽) — CSS 글꼴 짝짓기의 단순판.
            const FontFaceDesc* pBest           = nullptr;
            bool                bBestSlantMatch = false;
            uint32              bestDistance    = invalid_index::kUint32;
            for ( const FontFaceDesc& face : pFamily->_listFace )
            {
                const bool   bSlantMatch = face._slant == slant;
                const uint32 distance    = FontSystemInternal::computeWeightDistance( weight, face._weight );
                const bool   bHeavierTie = pBest != nullptr && distance == bestDistance && static_cast<uint16>( face._weight ) > static_cast<uint16>( pBest->_weight );
                const bool   bBetter     = pBest == nullptr || ( bSlantMatch && bBestSlantMatch == false ) ||
                                     ( bSlantMatch == bBestSlantMatch && ( distance < bestDistance || bHeavierTie ) );
                if ( bBetter )
                {
                    pBest           = &face;
                    bBestSlantMatch = bSlantMatch;
                    bestDistance    = distance;
                }
            }
            if ( pBest != nullptr )
            {
                opened._face        = openFontFile( pBest->_path, pBest->_faceIndex, true );
                opened._bFauxBold   = FontSystemInternal::needsFauxBold( weight, pBest->_weight ) ? SW_TRUE : SW_FALSE;
                opened._bFauxItalic = slant == FontSlant::Italic && pBest->_slant != FontSlant::Italic ? SW_TRUE : SW_FALSE;
            }
        }
        else if ( pSystemFamily != nullptr )
        {
#if defined( SW_PLATFORM_WINDOWS )
            const string& regularName = pSystemFamily->_windowsRegular;
            const string& boldName    = pSystemFamily->_windowsBold;
            const uint32  faceIndex   = pSystemFamily->_windowsFaceIndex;
#else
            const string& regularName = pSystemFamily->_linuxRegular;
            const string& boldName    = pSystemFamily->_linuxBold;
            const uint32  faceIndex   = pSystemFamily->_linuxFaceIndex;
#endif
            const bool    bWantsBold = static_cast<uint16>( weight ) >= static_cast<uint16>( FontWeight::SemiBold );
            const bool    bUseBold   = bWantsBold && boldName.empty() == false;
            const string& fileName   = bUseBold ? boldName : regularName;
            const string  filePath   = SystemFontLocator::findSystemFontFile( fileName );
            if ( filePath.empty() == false )
            {
                opened._face        = openFontFile( filePath, faceIndex, false );
                opened._bFauxBold   = bWantsBold && bUseBold == false ? SW_TRUE : SW_FALSE;
                opened._bFauxItalic = slant == FontSlant::Italic ? SW_TRUE : SW_FALSE;
            }
        }

        if ( opened._face == kInvalidFontFaceId )
        {
            opened = OpenedFamilyFace{};
            if ( markWarnedOnce( FontSystemInternal::hashName( family, FontSystemInternal::kFamilyWarningSeed ) ) )
                SW_LOG_WARNING( "[Text] Font family '%#' is not in the font catalog or not installed on this system - the next family in the fallback chain is used",
                                string( family ).c_str() );
        }
        _mapOpenedFamilyFace.emplace( familyFaceKey, opened );
        outFauxBold   = opened._bFauxBold;
        outFauxItalic = opened._bFauxItalic;
        return opened._face;
    }

    FontFaceId FontSystem::openFontFile( const string& path, uint32 faceIndex, bool bResourcePath )
    {
        const string lowerPath = StringUtil::toLower( path.c_str() );
        const uint64 fileKey   = HashUtil::combine( FontSystemInternal::hashName( lowerPath ), faceIndex );
        const auto   iter      = _mapOpenedFile.find( fileKey );
        if ( iter != _mapOpenedFile.end() )
            return iter->second;

        vector<uint8> bytes;
        const auto    memoryIter = _mapMemoryFile.find( lowerPath );
        bool          bRead      = false;
        if ( memoryIter != _mapMemoryFile.end() )
        {
            bytes = memoryIter->second;
            bRead = true;
        }
        else if ( bResourcePath )
        {
            bRead = ResourceUtil::readBinaryResource( path, bytes );
        }
        else
        {
            bRead = FileUtil::readFile( path, bytes );
        }
        FontFaceId face = kInvalidFontFaceId;
        if ( bRead )
            face = _rasterizer->loadFace( std::move( bytes ), faceIndex, path );
        else
            SW_LOG_ERROR( "[Text] Font file could not be read: %#", path.c_str() );
        _mapOpenedFile.emplace( fileKey, face );
        return face;
    }

    bool FontSystem::markWarnedOnce( uint64 key )
    {
        if ( _uniqueWarned.find( key ) != _uniqueWarned.end() )
            return false;
        _uniqueWarned.insert( key );
        return true;
    }

    const LocalizationManager* FontSystem::findLocalization() const
    {
        if ( _pLocalization != nullptr )
            return _pLocalization;
        return engine::getBoundEngineServices()._pLocalizationManager;
    }
} // namespace sw
