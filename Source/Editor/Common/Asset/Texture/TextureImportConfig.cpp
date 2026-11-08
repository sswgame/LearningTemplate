#include "pch.h"

#include "Editor/Common/Asset/Texture/TextureImportConfig.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONFields.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 형식 표의 한 줄입니다 — DXGI 이름(로그에 쓰는 이름)과, 임포터가 써 내는 DXGI 번호와 sRGB 짝을 함께 든다.
         * @details sRGB 짝이 없으면 `_srgbName` 이 비고 `_srgbDXGIFormat` 이 `_dxgiFormat` 과 같다. 번호는 `dxgiformat.h` 의 값이다(이 파일은 DirectXTex 를 끌어오지 않는다).
         */
        struct TextureImportFormatName
        {
            string_view         _dxgiName;
            TextureImportFormat _format;
            uint32              _dxgiFormat;
            string_view         _srgbName;
            uint32              _srgbDXGIFormat;
        };
        static constexpr TextureImportFormatName kArrTextureImportFormatName[] = {
            {         "BC1_UNORM",               TextureImportFormat::BC1, 71,      "BC1_UNORM_SRGB", 72},
            {         "BC3_UNORM",               TextureImportFormat::BC3, 77,      "BC3_UNORM_SRGB", 78},
            {         "BC4_UNORM",               TextureImportFormat::BC4, 80,                    "", 80},
            {         "BC5_UNORM",               TextureImportFormat::BC5, 83,                    "", 83},
            {         "BC6H_UF16",              TextureImportFormat::BC6H, 95,                    "", 95},
            {         "BC7_UNORM",               TextureImportFormat::BC7, 98,      "BC7_UNORM_SRGB", 99},
            {    "B8G8R8A8_UNORM",          TextureImportFormat::B8G8R8A8, 87, "B8G8R8A8_UNORM_SRGB", 91},
            {    "R8G8B8A8_UNORM",          TextureImportFormat::R8G8B8A8, 28, "R8G8B8A8_UNORM_SRGB", 29},
            {"R16G16B16A16_FLOAT", TextureImportFormat::R16G16B16A16Float, 10,                    "", 10},
            {          "R8_UNORM",                TextureImportFormat::R8, 61,                    "", 61},
        };

        /** @brief @p format 의 표 줄입니다. 표는 열거자를 모두 담는다. */
        [[nodiscard]] const TextureImportFormatName& findFormatEntryInternal( TextureImportFormat format )
        {
            for ( const TextureImportFormatName& entry : kArrTextureImportFormatName )
            {
                if ( entry._format == format )
                    return entry;
            }
            SW_ASSERT( false ); // 표는 열거자를 모두 담는다
            return kArrTextureImportFormatName[0];
        }

    } // namespace

    TextureImportConfig::TextureImportConfig()
        : _mapPreset{}
        , _listRule{}
    {
    }

    ConfigReadResult TextureImportConfig::loadFromFile( string_view configPath )
    {
        string                 text;
        const OptionalFileRead read = FileUtil::readOptionalTextFile( configPath, text );
        if ( read == OptionalFileRead::Missing )
            return ConfigReadResult::Missing;
        if ( read == OptionalFileRead::Failed || text.empty() )
        {
            SW_LOG_ERROR_ID( "TextureImportConfig.FailedReadTextureImportConfigFile", "Failed to read TextureImportConfig file: %#", configPath );
            return ConfigReadResult::Invalid;
        }
        return loadFromJSONString( text ) ? ConfigReadResult::Loaded : ConfigReadResult::Invalid;
    }

    bool TextureImportConfig::loadFromJSONString( string_view jsonString )
    {
        JSONDocument doc;
        if ( doc.parse( jsonString ) == false )
        {
            SW_LOG_ERROR_ID( "TextureImportConfig.FailedParseTextureImportConfigJSON", "Failed to parse TextureImportConfig JSON." );
            return false;
        }

        const JSONValue root = doc.getRoot();
        if ( root.isObject() == false )
        {
            SW_LOG_ERROR_ID( "TextureImportConfig.TextureImportConfigRootNotObject", "TextureImportConfig root is not an object." );
            return false;
        }

        _mapPreset.clear();
        _listRule.clear();
        if ( JSONFields::hasOnlyKnownKeys( root, { "presets", "rules" }, "TextureImportConfig" ) == false )
            return false;

        // 1) 프리셋 파싱
        const JSONValue presetsVal = root.get( "presets" );
        if ( presetsVal.isObject() )
        {
            const vector<string> listName = presetsVal.getMemberNames();
            for ( const auto& name : listName )
            {
                const JSONValue   presetObj = presetsVal.get( name );
                TextureImportRule rule;
                rule._name = name;
                if ( applyInheritance( presetObj, rule ) == false )
                    return clearAndFail();
                rule._name = name; // 상속이 부모 이름을 덮어썼을 수 있다. 이 프리셋의 이름이 원본이다.
                if ( JSONFields::readFields( presetObj, rule, "TextureImportConfig preset '" + name + "'" ) == false )
                    return clearAndFail();
                _mapPreset[name] = rule;
            }
        }

        // 2) 규칙 파싱 — 객체 아닌 원소 · 모르는 키 · 모르는 swizzle · 없는 inherits 는 로드 오류다(객체 아닌 원소를 규칙으로 받으면
        //    무엇에나 맞는 규칙이 되어 뒤 규칙을 모두 가린다).
        const JSONValue rulesVal = root.get( "rules" );
        if ( rulesVal.isArray() )
        {
            const size_t count = rulesVal.size();
            for ( size_t index = 0; index < count; ++index )
            {
                const JSONValue                        ruleObj = rulesVal.at( index );
                TextureImportRule                      rule;
                fixed_string<constant::buffer::kMax64> sourceName;
                formatstring( sourceName.data(), sourceName.capacity(), "TextureImportConfig rule %#", index );
                if ( applyInheritance( ruleObj, rule ) == false || JSONFields::readFields( ruleObj, rule, sourceName.view() ) == false )
                    return clearAndFail();
                _listRule.push_back( rule );
            }
        }

        SW_LOG_INFO_ID( "TextureImportConfig.LoadedTextureImportConfigPresetsRules", "Loaded TextureImportConfig: %# presets, %# rules.", _mapPreset.size(), _listRule.size() );

        // 조건 없는 규칙은 나머지를 받는 규칙이라 맨 끝에 하나만 둔다. 중간에 있으면 순서가 뜻을 갖는 설정으로 읽히므로 오류로 멈춘다 — 그 위치를 이름으로 짚어 준다.
        const optional<uint32> misplacedIndex = findShadowingRuleIndex();
        if ( misplacedIndex.has_value() )
        {
            const TextureImportRule& misplacedRule = _listRule[*misplacedIndex];
            SW_LOG_ERROR_ID( "TextureImportConfig.TextureImportConfigRuleNoCondition", "TextureImportConfig: rule #%# ('%#') has no condition, so it takes every path no other rule matches. "
                                                                                       "Put the unconditional rule at the very end of the list, once.",
                             *misplacedIndex, misplacedRule._name.empty() ? "(unnamed)" : misplacedRule._name.c_str() );
            return clearAndFail();
        }

        return true;
    }

    bool TextureImportConfig::clearAndFail()
    {
        _mapPreset.clear();
        _listRule.clear();
        return false;
    }

    bool TextureImportConfig::applyInheritance( const sw::JSONValue& jsonValue, TextureImportRule& inoutRule ) const
    {
        if ( jsonValue.isObject() == false || jsonValue.has( "inherits" ) == false )
            return true;

        const string inheritName = jsonValue.get( "inherits" ).asString();
        const auto   itParent    = _mapPreset.find( inheritName );
        if ( itParent == _mapPreset.end() )
        {
            // 찾기는 그 시점까지 파싱된 프리셋만 본다 — 부모를 아래쪽에 적어도 여기로 온다.
            SW_LOG_ERROR_ID( "Texture.ImportPresetInheritsUnknown", "TextureImportConfig: inherits '%#' is not a preset defined above (typo, or the parent is written below)", inheritName.c_str() );
            return false;
        }

        inoutRule           = itParent->second;
        inoutRule._inherits = inheritName;
        return true;
    }

    string_view TextureImportFormatUtil::getName( TextureImportFormat format )
    {
        return findFormatEntryInternal( format )._dxgiName;
    }

    uint32 TextureImportFormatUtil::getDXGIFormat( TextureImportFormat format, bool bSRGB )
    {
        const TextureImportFormatName& entry = findFormatEntryInternal( format );
        return bSRGB ? entry._srgbDXGIFormat : entry._dxgiFormat;
    }

    TextureFormatUsage TextureImportFormatUtil::getUsage( TextureImportFormat format )
    {
        switch ( format )
        {
            case TextureImportFormat::BC1:
            case TextureImportFormat::BC3:
            case TextureImportFormat::BC7:
                return TextureFormatUsage::Color;
            case TextureImportFormat::BC4:
                return TextureFormatUsage::Mask;
            case TextureImportFormat::BC5:
                return TextureFormatUsage::Normal;
            case TextureImportFormat::BC6H:
                return TextureFormatUsage::HDR;
            case TextureImportFormat::B8G8R8A8:
            case TextureImportFormat::R8G8B8A8:
            case TextureImportFormat::R16G16B16A16Float:
            case TextureImportFormat::R8:
                return TextureFormatUsage::Raw;
        }
        return TextureFormatUsage::Raw;
    }

    bool TextureImportConfig::isCatchAllRule( const TextureImportRule& rule )
    {
        return rule._filter.isCatchAll();
    }

    optional<uint32> TextureImportConfig::findShadowingRuleIndex() const
    {
        // 마지막 규칙은 캐치올이어도 정상이다. 그래서 하나 앞까지만 본다.
        return AssetImportRuleUtil::findMisplacedCatchAllIndex( _listRule );
    }

    TextureImportRuleMatch TextureImportConfig::findMatchingRule( string_view relativePath ) const
    {
        // 공유 원본은 폴더에 적힌 규칙으로 임포트한다 — 패턴은 모델 옆 자리로 이미 골랐다. 폴더의 규칙이 설정에서 사라졌으면 다른 규칙으로 읽지 않고 오류다.
        const string sharedRule = findSharedRuleName( relativePath );
        if ( sharedRule.empty() )
            return AssetImportRuleUtil::select( _listRule, relativePath, "TextureImportConfig" );
        TextureImportRuleMatch match{};
        if ( sharedRule == kSharedDefaultRuleName )
            return match;
        for ( const TextureImportRule& rule : _listRule )
        {
            if ( StringUtil::toLower( rule._name.c_str() ) == sharedRule )
            {
                match._rule     = rule;
                match._bMatched = true;
                return match;
            }
        }
        match._error = "TextureImportConfig: shared source '" + string( relativePath ) + "' names rule '" + sharedRule + "' which is not in the config. Import its model again.";
        return match;
    }

    string TextureImportConfig::makeSharedSourcePath( string_view domainFolder, string_view ruleName, string_view key, string_view extension )
    {
        const string rule = ruleName.empty() ? string( kSharedDefaultRuleName ) : StringUtil::toLower( string( ruleName ).c_str() );
        return string( domainFolder ) + "/textures_raw/" + string( kSharedFolderName ) + "/" + rule + "/" + string( key ) + string( extension );
    }

    string TextureImportConfig::findSharedRuleName( string_view relativePath )
    {
        const string normalized = "/" + FileUtil::normalizeSeparators( relativePath );
        const string marker     = "/textures_raw/" + string( kSharedFolderName ) + "/";
        const size_t at         = normalized.find( marker );
        if ( at == string::npos )
            return {};
        const size_t begin = at + marker.size();
        const size_t end   = normalized.find( '/', begin );
        return end == string::npos ? string{} : normalized.substr( begin, end - begin );
    }
} // namespace sw::editor
