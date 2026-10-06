#include "pch.h"

#include "Editor/Common/Asset/TextureImportConfig.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Utility/Json/ConfigKeyDoc.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 파일 뿌리입니다. */
        static constexpr ConfigKeyDoc kArrTextureImportRootKeyDoc[] = {
            {"presets",   "object", "", "프리셋 이름 → 규칙(아래 키). 위에서 아래로 읽으니 부모 프리셋을 먼저 적는다"},
            {  "rules", "object[]", "",             "규칙 목록(아래 키). 첫 매칭이 이긴다 — 조건 없는 규칙은 맨 끝에"},
        };
        /** @brief 프리셋 · 규칙 하나입니다. 적지 않은 칸은 `inherits` 의 값, 그것도 없으면 기본값입니다. */
        static constexpr ConfigKeyDoc kArrTextureImportRuleKeyDoc[] = {
            {            "name",   "string",          "",                               "규칙 이름(로그 · 경고에 나온다). 프리셋은 키가 이름이다"},
            {        "inherits",   "string",          "",                       "값을 물려받을 프리셋 이름 — 위에 정의되지 않은 이름은 로드 오류"},
            {          "format",   "string", "BC7_UNORM", "DXGI 포맷 이름(BC7_UNORM · BC5_UNORM · BC6H_UF16 · B8G8R8A8_UNORM · R8G8B8A8_UNORM …)"},
            {         "swizzle",   "string",      "RGBA",                              "채널 순서: RGBA · BGRA · ARGB · RGB1 — 그 밖은 로드 오류"},
            {   "generate_mips",     "bool",      "true",                                                                         "밉맵을 만든다"},
            {            "srgb",     "bool",      "true",                                 "sRGB 색 공간으로 읽는다(노멀 · 데이터 텍스처는 false)"},
            {    "invert_green",     "bool",     "false",                                           "G 채널을 뒤집는다(DirectX ↔ OpenGL 노멀 맵)"},
            {"include_patterns", "string[]",          "",           "맞아야 하는 와일드카드(`*` · `?`, 대소문자 무시, 파일 이름이나 리소스 경로)"},
            {"exclude_patterns", "string[]",          "",                                            "맞으면 빼는 와일드카드(포함보다 먼저 본다)"},
            {   "include_paths", "string[]",          "",                                                            "들어 있어야 하는 경로 조각"},
            {   "exclude_paths", "string[]",          "",                                                            "들어 있으면 빼는 경로 조각"},
        };

        /** @brief 채널 순서 이름을 읽습니다. 모르는 이름이면 false 입니다(기본 RGBA 로 조용히 가지 않는다). */
        [[nodiscard]] bool parseSwizzleInternal( string_view swizzleStr, TextureSwizzle& outSwizzle )
        {
            if ( swizzleStr == "RGBA" || swizzleStr == "rgba" )
                outSwizzle = TextureSwizzle::RGBA;
            else if ( swizzleStr == "BGRA" || swizzleStr == "bgra" )
                outSwizzle = TextureSwizzle::BGRA;
            else if ( swizzleStr == "ARGB" || swizzleStr == "argb" )
                outSwizzle = TextureSwizzle::ARGB;
            else if ( swizzleStr == "RGB1" || swizzleStr == "rgb1" )
                outSwizzle = TextureSwizzle::RGB1;
            else
                return false;
            return true;
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
        if ( ConfigKeyDocUtil::hasOnlyKnownKeys( root, kArrTextureImportRootKeyDoc, "TextureImportConfig" ) == false )
            return false;

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
                if ( applyInheritance( presetObj, rule ) == false )
                    return clearAndFail();
                rule._name = name; // 상속이 부모 이름을 덮어썼을 수 있다. 이 프리셋의 이름이 정본이다.
                if ( parseRuleObject( presetObj, rule, "TextureImportConfig preset '" + name + "'" ) == false )
                    return clearAndFail();
                _mapPreset[name] = rule;
            }
        }

        // 2) 규칙 파싱 — 객체 아닌 원소 · 모르는 키 · 모르는 swizzle · 없는 inherits 는 로드 오류다(객체 아닌 원소를 규칙으로 받으면
        //    무엇에나 맞는 규칙이 되어 뒤 규칙을 모두 가린다).
        const JsonValue rulesVal = root.get( "rules" );
        if ( rulesVal.isArray() )
        {
            const size_t count = rulesVal.size();
            for ( size_t index = 0; index < count; ++index )
            {
                const JsonValue   ruleObj = rulesVal.at( index );
                TextureImportRule rule;
                if ( applyInheritance( ruleObj, rule ) == false || parseRuleObject( ruleObj, rule, "TextureImportConfig rule " + to_string( index ) ) == false )
                    return clearAndFail();
                _listRule.push_back( rule );
            }
        }

        SW_LOG_INFO( "Loaded TextureImportConfig: %# presets, %# rules.", _mapPreset.size(), _listRule.size() );

        // 규칙을 적어 두었는데 아무 일도 일어나지 않는 것(조건 없는 규칙이 중간에 있다)은 로드 오류가 아니라 경고다 — 그 자리를 이름으로 짚어 준다.
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

    bool TextureImportConfig::clearAndFail()
    {
        _mapPreset.clear();
        _listRule.clear();
        return false;
    }

    bool TextureImportConfig::applyInheritance( const sw::JsonValue& jsonValue, TextureImportRule& inoutRule ) const
    {
        if ( jsonValue.isObject() == false || jsonValue.has( "inherits" ) == false )
            return true;

        const string inheritName = jsonValue.get( "inherits" ).asString();
        const auto   itParent    = _mapPreset.find( inheritName );
        if ( itParent == _mapPreset.end() )
        {
            // 찾기는 그 시점까지 파싱된 프리셋만 본다 — 부모를 아래쪽에 적어도 여기로 온다.
            SW_LOG_ERROR( "TextureImportConfig: inherits '%#' is not a preset defined above (typo, or the parent is written below)", inheritName.c_str() );
            return false;
        }

        inoutRule           = itParent->second;
        inoutRule._inherits = inheritName;
        return true;
    }

    bool TextureImportConfig::parseRuleObject( const sw::JsonValue& jsonValue, TextureImportRule& inoutRule, string_view context )
    {
        if ( jsonValue.isObject() == false )
        {
            SW_LOG_ERROR( "%#: must be an object", context );
            return false;
        }
        if ( ConfigKeyDocUtil::hasOnlyKnownKeys( jsonValue, kArrTextureImportRuleKeyDoc, context ) == false )
            return false;

        if ( jsonValue.has( "name" ) )
            inoutRule._name = jsonValue.get( "name" ).asString();

        if ( jsonValue.has( "inherits" ) )
            inoutRule._inherits = jsonValue.get( "inherits" ).asString();

        if ( jsonValue.has( "format" ) )
            inoutRule._format = jsonValue.get( "format" ).asString();

        if ( jsonValue.has( "swizzle" ) )
        {
            const string swizzleName = jsonValue.get( "swizzle" ).asString();
            if ( parseSwizzleInternal( swizzleName, inoutRule._swizzle ) == false )
            {
                SW_LOG_ERROR( "%#: unknown swizzle '%#' (RGBA, BGRA, ARGB, RGB1)", context, swizzleName.c_str() );
                return false;
            }
        }

        if ( jsonValue.has( "generate_mips" ) )
            inoutRule._bGenerateMips = jsonValue.get( "generate_mips" ).asBool() ? SW_TRUE : SW_FALSE;

        if ( jsonValue.has( "srgb" ) )
            inoutRule._bSrgb = jsonValue.get( "srgb" ).asBool() ? SW_TRUE : SW_FALSE;

        if ( jsonValue.has( "invert_green" ) )
            inoutRule._bInvertGreen = jsonValue.get( "invert_green" ).asBool() ? SW_TRUE : SW_FALSE;

        inoutRule._filter.parse( jsonValue );
        return true;
    }

    bool TextureImportConfig::isCatchAllRule( const TextureImportRule& rule )
    {
        return rule._filter.isCatchAll();
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
        // 첫 매칭이 이긴다.
        for ( const TextureImportRule& rule : _listRule )
        {
            if ( rule._filter.matchesPath( relativePath ) == false )
                continue;
            outRule = rule;
            return true;
        }
        return false;
    }
} // namespace sw::editor
