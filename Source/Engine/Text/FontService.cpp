#include "pch.h"

#include "Engine/Text/FontService.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Localization/EngineDefaultAssetsSubsystem.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Text/SystemFontLocator.h"

// 카탈로그의 `_glyphField` 가 값의 정본이다. 이 변수는 카탈로그를 고치지 않고 MSDF 로 켜 SDF 와 견주는 측정 스위치다(AppUITest.MsdfGlyphsMatchSdfDemoScreen).
SW_TEST_GLOBAL_VARIABLE( bool, gv_fontMsdfGlyphs, false, "글리프 아틀라스를 MSDF 로 만든다(카탈로그의 _glyphField 를 덮는다 — SDF 와 큰 글자 모서리를 견주는 측정용)" );

namespace sw
{
    SW_LOG_CALLER( "FontService" );

    namespace
    {
        struct FontServiceInternal
        {
            /**
             * @brief 캐시 맵의 키를 원문으로 짓는 구분자입니다. 이름 · 경로에 나올 수 없는 제어 문자입니다.
             * @details 맵 키를 64 비트 해시 하나로 두면 두 키가 부딪힐 때 다른 패밀리 · 파일의 면을 조용히 돌려준다. 원문 문자열을 키로 두면 부딪혀도 값 비교로 갈린다.
             */
            static constexpr utf8 kKeySeparator = '\x1F';

            /** @brief (패밀리, 굵기, 기울기) 키입니다. 패밀리 이름은 대소문자를 무시합니다. */
            static string makeFamilyFaceKey( string_view family, FontWeight weight, FontSlant slant )
            {
                string key = StringUtil::toLower( string( family ).c_str() );
                key += kKeySeparator;
                key += to_string( static_cast<uint32>( weight ) );
                key += kKeySeparator;
                key += to_string( static_cast<uint32>( slant ) );
                return key;
            }

            /** @brief (문화권, 패밀리 면) 연쇄 키입니다. */
            static string makeChainKey( string_view culture, string_view familyFaceKey )
            {
                string key = StringUtil::toLower( string( culture ).c_str() );
                key += kKeySeparator;
                key += familyFaceKey;
                return key;
            }

            /** @brief (소문자 경로, 면 번호) 키입니다. */
            static string makeFileKey( string_view lowerPath, uint32 faceIndex )
            {
                string key( lowerPath );
                key += kKeySeparator;
                key += to_string( faceIndex );
                return key;
            }

            /** @brief 한 번만 남기는 경고의 키(패밀리 이름, 대소문자 무시)입니다. 원문이라 부딪혀 다른 경고를 삼키지 않는다. */
            static string makeFamilyWarningKey( string_view family )
            {
                string key = "family";
                key += kKeySeparator;
                key += StringUtil::toLower( string( family ).c_str() );
                return key;
            }

            /** @brief 한 번만 남기는 경고의 키(글리프 없는 코드 포인트)입니다. */
            static string makeCodepointWarningKey( uint32 codepoint )
            {
                string key = "codepoint";
                key += kKeySeparator;
                key += to_string( codepoint );
                return key;
            }

            /** @brief 굵기 차이입니다(무게 짝짓기에서 작은 쪽이 이기고, 같으면 무거운 쪽이 이깁니다). */
            static uint32 computeWeightDistance( FontWeight requested, FontWeight candidate )
            {
                const int32 difference = static_cast<int32>( requested ) - static_cast<int32>( candidate );
                return static_cast<uint32>( difference < 0 ? -difference : difference );
            }

            /** @brief 요청은 굵은데(SemiBold 이상) 고른 면은 그렇지 않은지(Medium 이하) 판정합니다. 이 경우 가짜 굵게를 씁니다. */
            static bool needsFauxBold( FontWeight requested, FontWeight chosen )
            {
                return static_cast<uint16>( requested ) >= static_cast<uint16>( FontWeight::SemiBold ) &&
                       static_cast<uint16>( chosen ) <= static_cast<uint16>( FontWeight::Medium );
            }

            /** @brief 사슬 끝에 면을 더합니다(겹치거나 무효면 건너뛰고, 가득 차면 버린다). */
            static void appendFace( FontFaceChain& inoutChain, FontFaceID face )
            {
                if ( face == kInvalidFontFaceID || inoutChain._faceCount >= FontFaceChain::kMaxFaceCount )
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
    FontService::FontService()
        : FontService( nullptr, nullptr )
    {
    }

    FontService::FontService( unique_ptr<IFontRasterizer> rasterizer, const LocalizationManager* pLocalization )
        : _rasterizer{ std::move( rasterizer ) }
        , _glyphCache{}
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

    FontService::~FontService()
    {
        shutdown();
    }

    bool FontService::initialize( string_view catalogPath )
    {
        FontCatalogDesc catalog;
        if ( catalog.loadFromResource( catalogPath ) == false )
            return false;
        if ( gv_fontMsdfGlyphs )
            catalog._glyphField = GlyphFieldKind::Msdf;
        return initializeFromCatalog( std::move( catalog ) );
    }

    bool FontService::initializeFromCatalog( FontCatalogDesc catalog )
    {
        shutdown();
        if ( catalog.validate() == false )
            return false;
        if ( _rasterizer == nullptr )
            _rasterizer = IFontRasterizer::createDefault();
        if ( _rasterizer == nullptr )
            return false;
        _catalog                     = std::move( catalog );
        _glyphCache                  = make_unique<GlyphCache>( *_rasterizer, _catalog._glyphField );
        uint8            bFauxBold   = SW_FALSE;
        uint8            bFauxItalic = SW_FALSE;
        const FontFaceID defaultFace = openFamilyFace( _catalog._defaultFamily, FontWeight::Regular, FontSlant::Upright, bFauxBold, bFauxItalic );
        if ( defaultFace == kInvalidFontFaceID )
        {
            SW_LOG_ERROR( "[Text] The default font family '%#' could not be opened - no text can be drawn", _catalog._defaultFamily.c_str() );
            return false;
        }
        _bInitialized = true;
        return true;
    }

    span<const utf8* const> FontService::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { EngineDefaultAssetsSubsystem::kSubsystemName, AssetManager::kSubsystemName };
        return kArrDependency;
    }

    SubsystemInitResult FontService::initializeSubsystem()
    {
        // 의존 서비스는 시작할 때 한 번 받는다(언리얼 `InitializeDependency`) — 연쇄를 고를 때마다 전역 표를 찾지 않는다. 문화권 표는 서비스 범위에 먼저 묶인다.
        _pLocalization = engine::findSubsystem<LocalizationManager>();
        return initialize( engine::getSubsystem<EngineDefaultAssets>()._fontCatalog.get() ) ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
    }

    void FontService::shutdown()
    {
        _glyphCache.reset();
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

    void FontService::registerMemoryFontFile( string_view path, vector<uint8> bytes )
    {
        _mapMemoryFile[StringUtil::toLower( string( path ).c_str() )] = std::move( bytes );
    }

    FontFaceChain FontService::getFaceChain( const FontSpec& spec )
    {
        FontFaceChain chain{};
        if ( _bInitialized == false )
            return chain;
        const string      culture  = _pLocalization != nullptr ? _pLocalization->getCurrentLanguage() : string{};
        const string_view family   = spec._family.empty() ? string_view{ _catalog._defaultFamily } : string_view{ spec._family };
        const string      chainKey = FontServiceInternal::makeChainKey( culture, FontServiceInternal::makeFamilyFaceKey( family, spec._weight, spec._slant ) );
        const auto        iter     = _mapChain.find( chainKey );
        if ( iter != _mapChain.end() )
        {
            chain = iter->second;
            return chain;
        }

        // 1) 고른 패밀리: 가짜 굵게와 기울임은 이 면에서 정한다.
        uint8            bFauxBold   = SW_FALSE;
        uint8            bFauxItalic = SW_FALSE;
        const FontFaceID primaryFace = openFamilyFace( family, spec._weight, spec._slant, bFauxBold, bFauxItalic );
        FontServiceInternal::appendFace( chain, primaryFace );
        if ( primaryFace != kInvalidFontFaceID )
        {
            chain._bFauxBold   = bFauxBold;
            chain._bFauxItalic = bFauxItalic;
        }
        // 2) 지금 문화권의 대체 패밀리들(문화권 테이블 `fonts`).
        if ( _pLocalization != nullptr )
        {
            for ( const string& fallbackFamily : _pLocalization->getFontFallback() )
            {
                uint8 bUnusedBold   = SW_FALSE;
                uint8 bUnusedItalic = SW_FALSE;
                FontServiceInternal::appendFace( chain, openFamilyFace( fallbackFamily, spec._weight, spec._slant, bUnusedBold, bUnusedItalic ) );
            }
        }
        // 3) 카탈로그 기본 패밀리: 항상 열린다(initialize 가 확인했다).
        uint8            bDefaultBold   = SW_FALSE;
        uint8            bDefaultItalic = SW_FALSE;
        const FontFaceID defaultFace    = openFamilyFace( _catalog._defaultFamily, spec._weight, spec._slant, bDefaultBold, bDefaultItalic );
        FontServiceInternal::appendFace( chain, defaultFace );
        if ( primaryFace == kInvalidFontFaceID )
        {
            chain._bFauxBold   = bDefaultBold;
            chain._bFauxItalic = bDefaultItalic;
        }
        _mapChain.emplace( chainKey, chain );
        return chain;
    }

    FontFaceID FontService::findFaceForCodepoint( const FontFaceChain& chain, uint32 codepoint, uint32& outGlyphIndex )
    {
        outGlyphIndex = 0;
        if ( chain._faceCount == 0 || _rasterizer == nullptr )
            return kInvalidFontFaceID;
        for ( uint32 index = 0; index < chain._faceCount; ++index )
        {
            const uint32 glyphIndex = _rasterizer->findGlyphIndex( chain._arrFace[index], codepoint );
            if ( glyphIndex != 0 )
            {
                outGlyphIndex = glyphIndex;
                return chain._arrFace[index];
            }
        }
        if ( markWarnedOnce( FontServiceInternal::makeCodepointWarningKey( codepoint ) ) )
            SW_LOG_WARNING( "[Text] No font in the fallback chain has a glyph for codepoint %# - it draws as a box (install a system font that covers it)", codepoint );
        return chain._arrFace[0];
    }

    void FontService::invalidateFaceChains()
    {
        _mapChain.clear();
    }

    FontFaceID FontService::openFamilyFace( string_view family, FontWeight weight, FontSlant slant, uint8& outFauxBold, uint8& outFauxItalic )
    {
        outFauxBold                = SW_FALSE;
        outFauxItalic              = SW_FALSE;
        const string familyFaceKey = FontServiceInternal::makeFamilyFaceKey( family, weight, slant );
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
            // 같은 기울기를 먼저 보고, 그 안에서 굵기가 가장 가까운 면(같으면 무거운 쪽)을 고른다. CSS 폰트 매칭의 단순 버전이다.
            const FontFaceDesc* pBest           = nullptr;
            bool                bBestSlantMatch = false;
            uint32              bestDistance    = invalid_index::kUint32;
            for ( const FontFaceDesc& face : pFamily->_listFace )
            {
                const bool   bSlantMatch = face._slant == slant;
                const uint32 distance    = FontServiceInternal::computeWeightDistance( weight, face._weight );
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
                opened._bFauxBold   = FontServiceInternal::needsFauxBold( weight, pBest->_weight ) ? SW_TRUE : SW_FALSE;
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

        if ( opened._face == kInvalidFontFaceID )
        {
            opened = OpenedFamilyFace{};
            // 카탈로그의 시스템 패밀리는 설치가 기계마다 다르다. 없으면 대체 연쇄의 다음 패밀리를 쓰는 것이 정상이라 Info 로 남긴다.
            // 카탈로그에 없거나 저장소 폰트를 열지 못한 것은 데이터 잘못이라 Warning 으로 남긴다.
            if ( markWarnedOnce( FontServiceInternal::makeFamilyWarningKey( family ) ) )
            {
                if ( pSystemFamily != nullptr )
                    SW_LOG_INFO( "[Text] System font family '%#' is not installed on this machine - the next family in the fallback chain is used", string( family ).c_str() );
                else
                    SW_LOG_WARNING_ID( "Text.FontFamilyMissing", "[Text] Font family '%#' is not in the font catalog or could not be opened - the next family in the fallback chain is used",
                                       string( family ).c_str() );
            }
        }
        _mapOpenedFamilyFace.emplace( familyFaceKey, opened );
        outFauxBold   = opened._bFauxBold;
        outFauxItalic = opened._bFauxItalic;
        return opened._face;
    }

    FontFaceID FontService::openFontFile( const string& path, uint32 faceIndex, bool bResourcePath )
    {
        const string lowerPath = StringUtil::toLower( path.c_str() );
        const string fileKey   = FontServiceInternal::makeFileKey( lowerPath, faceIndex );
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
            bRead = ResourceIO::readBinaryResource( path, bytes );
        }
        else
        {
            bRead = FileUtil::readFile( path, bytes );
        }
        FontFaceID face = kInvalidFontFaceID;
        if ( bRead )
            face = _rasterizer->loadFace( std::move( bytes ), faceIndex, path );
        else
            SW_LOG_ERROR( "[Text] Font file could not be read: %#", path.c_str() );
        _mapOpenedFile.emplace( fileKey, face );
        return face;
    }

    bool FontService::markWarnedOnce( const string& key )
    {
        return _uniqueWarned.insert( key ).second;
    }

} // namespace sw
