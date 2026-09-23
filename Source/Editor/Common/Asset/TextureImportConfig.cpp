#include "pch.h"

#include "Editor/Common/Asset/TextureImportConfig.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Json/JsonDocument.h"

namespace sw::editor
{
    namespace
    {
        bool matchWildcardInternal( string_view pattern, string_view text )
        {
            const utf8* pPat    = pattern.data();
            const utf8* pStr    = text.data();
            const utf8* pPatEnd = pPat + pattern.size();
            const utf8* pStrEnd = pStr + text.size();
            const utf8* pStar   = nullptr;
            const utf8* pMatch  = nullptr;

            while ( pStr < pStrEnd )
            {
                if ( pPat < pPatEnd && ( *pPat == '?' || std::tolower( static_cast<uint8>( *pPat ) ) == std::tolower( static_cast<uint8>( *pStr ) ) ) )
                {
                    ++pPat;
                    ++pStr;
                }
                else if ( pPat < pPatEnd && *pPat == '*' )
                {
                    pStar  = pPat++;
                    pMatch = pStr;
                }
                else if ( pStar != nullptr )
                {
                    pPat = pStar + 1;
                    pStr = ++pMatch;
                }
                else
                {
                    return false;
                }
            }

            while ( pPat < pPatEnd && *pPat == '*' )
            {
                ++pPat;
            }

            return pPat == pPatEnd;
        }

        TextureSwizzle parseSwizzleInternal( string_view swizzleStr )
        {
            if ( swizzleStr == "BGRA" || swizzleStr == "bgra" )
                return TextureSwizzle::BGRA;
            if ( swizzleStr == "ARGB" || swizzleStr == "argb" )
                return TextureSwizzle::ARGB;
            if ( swizzleStr == "RGB1" || swizzleStr == "rgb1" )
                return TextureSwizzle::RGB1;

            return TextureSwizzle::RGBA;
        }

        void parseStringListInternal( const sw::JsonValue& arrValue, vector<string>& outListItem )
        {
            outListItem.clear();
            if ( arrValue.isArray() == false )
                return;

            const size_t count = arrValue.size();
            for ( size_t index = 0; index < count; ++index )
            {
                const JsonValue item = arrValue.at( index );
                if ( item.isString() )
                    outListItem.push_back( item.asString() );
            }
        }
    } // namespace

    SW_LOG_CALLER( "TextureImportConfig" );

    TextureImportConfig::TextureImportConfig()
        : _mapPreset{}
        , _listRule{}
    {
    }

    bool TextureImportConfig::loadFromFile( string_view configPath )
    {
        string text;
        if ( FileUtil::readTextFile( configPath, text ) == false || text.empty() )
        {
            SW_LOG_WARNING( "Failed to read TextureImportConfig file: %#", configPath );
            return false;
        }

        return loadFromJsonString( text );
    }

    bool TextureImportConfig::loadFromJsonString( string_view jsonString )
    {
        JsonDocument doc;
        if ( doc.parse( jsonString ) == false )
        {
            SW_LOG_ERROR( "Failed to parse TextureImportConfig JSON." );
            return false;
        }

        const JsonValue root = doc.getRoot();
        if ( root.isObject() == false )
        {
            SW_LOG_ERROR( "TextureImportConfig root is not an object." );
            return false;
        }

        _mapPreset.clear();
        _listRule.clear();

        // 1) 프리셋 파싱
        const JsonValue presetsVal = root.get( "presets" );
        if ( presetsVal.isObject() )
        {
            const vector<string> listName = presetsVal.getMemberNames();
            for ( const auto& name : listName )
            {
                const JsonValue   presetObj = presetsVal.get( name );
                TextureImportRule rule;
                rule._name = name;

                applyInheritance( presetObj, rule );
                rule._name = name; // 상속이 부모 이름을 덮어썼을 수 있다. 이 프리셋의 이름이 정본이다.

                parseRuleObject( presetObj, rule );
                _mapPreset[name] = rule;
            }
        }

        // 2) 규칙 파싱
        //
        // **`forEachObjectInArray` 로 바꾸지 말 것. 일부러 이 모양이다.** 그 도우미는 객체가 아닌 원소를 건너뛰지만, 이 루프는
        // **모든 원소를 그대로 통과시킨다.** 둘의 차이는 겉보기보다 크다. 객체가 아닌 원소는 `parseRuleObject` 가 아무 필드도
        // 읽지 못해 **기본 규칙**이 되고, 기본 규칙은 include 목록이 비어 있어 `findMatchingRule` 의 검사를 모두 통과한다. 즉
        // **무엇에나 매칭되는 규칙**이 되고, 그 함수는 "첫 매칭이 이긴다" 는 규칙이라 **그 뒤의 규칙이 모두 가려진다.**
        //
        // 그런데도 이대로 두는 이유는 이 설정이 **에디터에서만 쓰는, 손으로 적는 파일**이기 때문이다(런타임 · 배포 경로는 읽지
        // 않는다). 망가진 원소를 넣으면 곧바로 모든 텍스처가 기본 설정으로 임포트되므로 적은 사람이 바로 알아챈다. 조용히 틀리는
        // 종류의 실패가 아니다. 엄격하게 바꾸는 것은 동작 변경이고, 그것을 지켜 줄 테스트가 아직 없다. (2026-09-19 결정)
        const JsonValue rulesVal = root.get( "rules" );
        if ( rulesVal.isArray() )
        {
            const size_t count = rulesVal.size();
            for ( size_t index = 0; index < count; ++index )
            {
                const JsonValue   ruleObj = rulesVal.at( index );
                TextureImportRule rule;

                applyInheritance( ruleObj, rule );

                parseRuleObject( ruleObj, rule );
                _listRule.push_back( rule );
            }
        }

        SW_LOG_INFO( "Loaded TextureImportConfig: %# presets, %# rules.", _mapPreset.size(), _listRule.size() );

        // 규칙을 적어 두었는데 아무 일도 일어나지 않는 것이 이 설정의 **유일한 조용한 실패**다. 위 파싱이 관대해서(객체가 아닌
        // 원소도 규칙이 된다) 더 쉽게 일어나므로, 그 자리를 이름으로 짚어 준다.
        const size_t shadowingIndex = findShadowingRuleIndex();
        if ( shadowingIndex < _listRule.size() )
        {
            const TextureImportRule& shadowingRule = _listRule[shadowingIndex];
            SW_LOG_WARNING( "TextureImportConfig: %#번 규칙('%#')이 조건 없이 모든 경로에 매칭되어 뒤의 %#개 규칙이 절대 선택되지 않습니다. "
                            "조건 없는 규칙은 목록 맨 끝에 두십시오.",
                            shadowingIndex, shadowingRule._name.empty() ? "이름 없음" : shadowingRule._name.c_str(),
                            _listRule.size() - shadowingIndex - 1 );
        }

        return true;
    }

    void TextureImportConfig::applyInheritance( const sw::JsonValue& jsonValue, TextureImportRule& inoutRule ) const
    {
        if ( jsonValue.has( "inherits" ) == false )
            return;

        const string inheritName = jsonValue.get( "inherits" ).asString();
        const auto   itParent    = _mapPreset.find( inheritName );
        if ( itParent == _mapPreset.end() )
        {
            // **조용히 넘어가지 않는다.** 여기서 아무 말도 하지 않으면 상속이 통째로 사라진 채 기본값으로 구워지고, JSON 을 고친
            // 사람은 그것을 알 방법이 없다. 부모를 아래쪽에 적어도 여기로 온다. 찾기는 **그 시점까지 파싱된 프리셋만** 보기 때문이다.
            SW_LOG_WARNING( "TextureImportConfig: inherits '%#' 를 찾지 못했습니다 — 기본값으로 갑니다. "
                            "(이름 오타이거나, 부모 프리셋을 아래쪽에 적었을 수 있습니다)",
                            inheritName.c_str() );
            return;
        }

        inoutRule           = itParent->second;
        inoutRule._inherits = inheritName;
    }

    void TextureImportConfig::parseRuleObject( const sw::JsonValue& jsonValue, TextureImportRule& inoutRule )
    {
        if ( jsonValue.has( "name" ) )
            inoutRule._name = jsonValue.get( "name" ).asString();

        if ( jsonValue.has( "inherits" ) )
            inoutRule._inherits = jsonValue.get( "inherits" ).asString();

        if ( jsonValue.has( "format" ) )
            inoutRule._format = jsonValue.get( "format" ).asString();

        if ( jsonValue.has( "swizzle" ) )
            inoutRule._swizzle = parseSwizzleInternal( jsonValue.get( "swizzle" ).asString() );

        if ( jsonValue.has( "generate_mips" ) )
            inoutRule._bGenerateMips = jsonValue.get( "generate_mips" ).asBool() ? SW_TRUE : SW_FALSE;

        if ( jsonValue.has( "srgb" ) )
            inoutRule._bSrgb = jsonValue.get( "srgb" ).asBool() ? SW_TRUE : SW_FALSE;

        if ( jsonValue.has( "invert_green" ) )
            inoutRule._bInvertGreen = jsonValue.get( "invert_green" ).asBool() ? SW_TRUE : SW_FALSE;

        if ( jsonValue.has( "include_patterns" ) )
            parseStringListInternal( jsonValue.get( "include_patterns" ), inoutRule._listIncludePattern );

        if ( jsonValue.has( "exclude_patterns" ) )
            parseStringListInternal( jsonValue.get( "exclude_patterns" ), inoutRule._listExcludePattern );

        if ( jsonValue.has( "include_paths" ) )
            parseStringListInternal( jsonValue.get( "include_paths" ), inoutRule._listIncludePath );

        if ( jsonValue.has( "exclude_paths" ) )
            parseStringListInternal( jsonValue.get( "exclude_paths" ), inoutRule._listExcludePath );
    }

    bool TextureImportConfig::isCatchAllRule( const TextureImportRule& rule )
    {
        return rule._listIncludePattern.empty() && rule._listIncludePath.empty() &&
               rule._listExcludePattern.empty() && rule._listExcludePath.empty();
    }

    size_t TextureImportConfig::findShadowingRuleIndex() const
    {
        if ( _listRule.size() < 2 )
            return _listRule.size();

        // 마지막 규칙은 캐치올이어도 가리는 것이 없다. 그래서 하나 앞까지만 본다.
        for ( size_t ruleIndex = 0; ruleIndex + 1 < _listRule.size(); ++ruleIndex )
        {
            if ( isCatchAllRule( _listRule[ruleIndex] ) )
                return ruleIndex;
        }
        return _listRule.size();
    }

    bool TextureImportConfig::findMatchingRule( string_view relativePath, TextureImportRule& outRule ) const
    {
        const string normalized = FileUtil::normalizeSeparators( relativePath );
        const string fileName   = FileUtil::getFileNamePart( normalized );
        const string dirName    = FileUtil::getDirectoryPart( normalized );

        for ( const auto& rule : _listRule )
        {
            // 1) 제외 경로 검사
            bool bExcludedPath = false;
            for ( const auto& exPath : rule._listExcludePath )
            {
                if ( dirName.find( exPath ) != string::npos || normalized.find( exPath ) != string::npos )
                {
                    bExcludedPath = true;
                    break;
                }
            }
            if ( bExcludedPath )
                continue;

            // 2) 제외 패턴 검사
            bool bExcludedPattern = false;
            for ( const auto& exPattern : rule._listExcludePattern )
            {
                if ( matchWildcardInternal( exPattern, fileName ) || matchWildcardInternal( exPattern, normalized ) )
                {
                    bExcludedPattern = true;
                    break;
                }
            }
            if ( bExcludedPattern )
                continue;

            // 3) 포함 경로 검사
            if ( rule._listIncludePath.empty() == false )
            {
                bool bPathMatched = false;
                for ( const auto& inPath : rule._listIncludePath )
                {
                    if ( dirName.find( inPath ) != string::npos || normalized.find( inPath ) != string::npos )
                    {
                        bPathMatched = true;
                        break;
                    }
                }
                if ( bPathMatched == false )
                    continue;
            }

            // 4) 포함 패턴 검사
            if ( rule._listIncludePattern.empty() == false )
            {
                bool bPatternMatched = false;
                for ( const auto& inPattern : rule._listIncludePattern )
                {
                    if ( matchWildcardInternal( inPattern, fileName ) || matchWildcardInternal( inPattern, normalized ) )
                    {
                        bPatternMatched = true;
                        break;
                    }
                }
                if ( bPatternMatched == false )
                    continue;
            }

            // 모든 조건을 만족했다(첫 매칭이 이긴다)
            outRule = rule;
            return true;
        }

        return false;
    }
} // namespace sw::editor
