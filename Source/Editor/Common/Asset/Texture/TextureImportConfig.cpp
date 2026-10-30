#include "pch.h"

#include "Editor/Common/Asset/Texture/TextureImportConfig.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Serialization/JSON/ConfigKeyDoc.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 파일 루트입니다. */
        static constexpr ConfigKeyDoc kArrTextureImportRootKeyDoc[] = {
            {"presets",   "object", "",                                                                                   "프리셋 이름 → 규칙(아래 키). 위에서 아래로 읽으니 부모 프리셋을 먼저 적는다"},
            {  "rules", "object[]", "", "규칙 목록(아래 키). 한 원본에는 조건 있는 규칙이 하나만 맞아야 한다(둘 이상이면 임포트 오류). 조건 없는 규칙(나머지 규칙)은 맨 끝에 — 중간에 있으면 로드 오류"},
        };
        /** @brief 프리셋 · 규칙 하나입니다. 적지 않은 필드는 `inherits` 의 값, 그것도 없으면 기본값입니다. */
        static constexpr ConfigKeyDoc kArrTextureImportRuleKeyDoc[] = {
            {            "name",   "string",          "",                                                                                                                     "규칙 이름(로그 · 경고에 나온다). 프리셋은 키가 이름이다"},
            {        "inherits",   "string",          "",                                                                                                             "값을 물려받을 프리셋 이름 — 위에 정의되지 않은 이름은 로드 오류"},
            {          "format",   "string", "BC7_UNORM", "DXGI 포맷 이름(BC1_UNORM · BC3_UNORM · BC4_UNORM · BC5_UNORM · BC6H_UF16 · BC7_UNORM · B8G8R8A8_UNORM · R8G8B8A8_UNORM · R16G16B16A16_FLOAT · R8_UNORM) — 그 밖은 로드 오류"},
            {         "swizzle",   "string",      "RGBA",                                                                                                                    "채널 순서: RGBA · BGRA · ARGB · RGB1 — 그 밖은 로드 오류"},
            {   "generate_mips",     "bool",      "true",                                                                                                                                                               "밉맵을 만든다"},
            {            "srgb",     "bool",      "true",                                                                                                                       "sRGB 색 공간으로 읽는다(노멀 · 데이터 텍스처는 false)"},
            {    "invert_green",     "bool",     "false",                                                                                                                                 "G 채널을 뒤집는다(DirectX ↔ OpenGL 노멀 맵)"},
            {"include_patterns", "string[]",          "",                           "맞아야 하는 와일드카드(`*` · `?`, 대소문자 무시). `/` 없으면 파일 이름에, 있으면 경로 마디에 맞춘다 — `*` 는 폴더를 넘지 않고 마디 `**` 만 넘는다"},
            {"exclude_patterns", "string[]",          "",                                                                                                                                  "맞으면 빼는 와일드카드(포함보다 먼저 본다)"},
            {   "include_paths", "string[]",          "",                                                                                                                     "들어 있어야 하는 폴더 마디(`ui/` 는 `…/ui/…` 에 맞는다)"},
            {   "exclude_paths", "string[]",          "",                                                                                                                                                  "들어 있으면 빼는 폴더 마디"},
        };

        /**
         * @brief `format` 이름 표의 한 줄입니다 — DXGI 이름(로그에 쓰는 이름)과 짧은 이름이 같은 형식이고, 임포터가 써 내는 DXGI 번호와 sRGB 짝을 함께 든다.
         * @details sRGB 짝이 없으면 `_srgbName` 이 비고 `_srgbDxgiFormat` 이 `_dxgiFormat` 과 같다. 번호는 `dxgiformat.h` 의 값이다(이 파일은 DirectXTex 를 끌어오지 않는다).
         */
        struct TextureImportFormatName
        {
            string_view         _dxgiName;
            string_view         _shortName;
            TextureImportFormat _format;
            uint32              _dxgiFormat;
            string_view         _srgbName;
            uint32              _srgbDxgiFormat;
        };
        static constexpr TextureImportFormatName kArrTextureImportFormatName[] = {
            {         "BC1_UNORM",     "bc1",               TextureImportFormat::BC1, 71,      "BC1_UNORM_SRGB", 72},
            {         "BC3_UNORM",     "bc3",               TextureImportFormat::BC3, 77,      "BC3_UNORM_SRGB", 78},
            {         "BC4_UNORM",     "bc4",               TextureImportFormat::BC4, 80,                    "", 80},
            {         "BC5_UNORM",     "bc5",               TextureImportFormat::BC5, 83,                    "", 83},
            {         "BC6H_UF16",    "bc6h",              TextureImportFormat::BC6H, 95,                    "", 95},
            {         "BC7_UNORM",     "bc7",               TextureImportFormat::BC7, 98,      "BC7_UNORM_SRGB", 99},
            {    "B8G8R8A8_UNORM",   "bgra8",          TextureImportFormat::B8G8R8A8, 87, "B8G8R8A8_UNORM_SRGB", 91},
            {    "R8G8B8A8_UNORM",   "rgba8",          TextureImportFormat::R8G8B8A8, 28, "R8G8B8A8_UNORM_SRGB", 29},
            {"R16G16B16A16_FLOAT", "rgba16f", TextureImportFormat::R16G16B16A16Float, 10,                    "", 10},
            {          "R8_UNORM",      "r8",                TextureImportFormat::R8, 61,                    "", 61},
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

    ConfigReadResult TextureImportConfig::loadFromFile( string_view configPath )
    {
        string                 text;
        const OptionalFileRead read = FileUtil::readOptionalTextFile( configPath, text );
        if ( read == OptionalFileRead::Missing )
            return ConfigReadResult::Missing;
        if ( read == OptionalFileRead::Failed || text.empty() )
        {
            SW_LOG_ERROR( "Failed to read TextureImportConfig file: %#", configPath );
            return ConfigReadResult::Invalid;
        }
        return loadFromJSONString( text ) ? ConfigReadResult::Loaded : ConfigReadResult::Invalid;
    }

    bool TextureImportConfig::loadFromJSONString( string_view jsonString )
    {
        JSONDocument doc;
        if ( doc.parse( jsonString ) == false )
        {
            SW_LOG_ERROR( "Failed to parse TextureImportConfig JSON." );
            return false;
        }

        const JSONValue root = doc.getRoot();
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
                if ( parseRuleObject( presetObj, rule, "TextureImportConfig preset '" + name + "'" ) == false )
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
                const JSONValue   ruleObj = rulesVal.at( index );
                TextureImportRule rule;
                if ( applyInheritance( ruleObj, rule ) == false || parseRuleObject( ruleObj, rule, formatToString( "TextureImportConfig rule %#", index ) ) == false )
                    return clearAndFail();
                _listRule.push_back( rule );
            }
        }

        SW_LOG_INFO( "Loaded TextureImportConfig: %# presets, %# rules.", _mapPreset.size(), _listRule.size() );

        // 조건 없는 규칙은 나머지를 받는 규칙이라 맨 끝에 하나만 둔다. 중간에 있으면 순서가 뜻을 갖는 설정으로 읽히므로 오류로 멈춘다 — 그 위치를 이름으로 짚어 준다.
        const size_t misplacedIndex = findShadowingRuleIndex();
        if ( misplacedIndex < _listRule.size() )
        {
            const TextureImportRule& misplacedRule = _listRule[misplacedIndex];
            SW_LOG_ERROR( "TextureImportConfig: rule #%# ('%#') has no condition, so it takes every path no other rule matches. "
                          "Put the unconditional rule at the very end of the list, once.",
                          misplacedIndex, misplacedRule._name.empty() ? "(unnamed)" : misplacedRule._name.c_str() );
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

    bool TextureImportConfig::parseRuleObject( const sw::JSONValue& jsonValue, TextureImportRule& inoutRule, string_view context )
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
        {
            const string formatName = jsonValue.get( "format" ).asString();
            if ( TextureImportFormatUtil::parse( formatName, inoutRule._format ) == false )
            {
                SW_LOG_ERROR( "%#: unknown format '%#' (see TextureImportConfig key doc)", context, formatName.c_str() );
                return false;
            }
        }

        if ( jsonValue.has( "swizzle" ) )
        {
            const string swizzleName = jsonValue.get( "swizzle" ).asString();
            if ( parseSwizzleInternal( swizzleName, inoutRule._swizzle ) == false )
            {
                SW_LOG_ERROR( "%#: unknown swizzle '%#' (RGBA, BGRA, ARGB, RGB1)", context, swizzleName.c_str() );
                return false;
            }
        }

        // 적지 않은 칸은 상속받은(또는 규칙 초기) 값 그대로다. 불리언이 아닌 값은 규칙째 거절한다(읽기가 키와 함께 알린다).
        bool       bGenerateMips = inoutRule._bGenerateMips != SW_FALSE;
        bool       bSRGB         = inoutRule._bSRGB != SW_FALSE;
        bool       bInvertGreen  = inoutRule._bInvertGreen != SW_FALSE;
        const bool bFlagsRead    = jsonValue.readOptionalInto( "generate_mips", bGenerateMips ) && jsonValue.readOptionalInto( "srgb", bSRGB ) &&
                                jsonValue.readOptionalInto( "invert_green", bInvertGreen );
        if ( bFlagsRead == false )
        {
            SW_LOG_ERROR( "%#: generate_mips, srgb and invert_green must be booleans", context );
            return false;
        }
        inoutRule._bGenerateMips = bGenerateMips ? SW_TRUE : SW_FALSE;
        inoutRule._bSRGB         = bSRGB ? SW_TRUE : SW_FALSE;
        inoutRule._bInvertGreen  = bInvertGreen ? SW_TRUE : SW_FALSE;

        inoutRule._filter.parse( jsonValue );
        return true;
    }

    bool TextureImportFormatUtil::parse( string_view name, TextureImportFormat& outFormat )
    {
        for ( const TextureImportFormatName& entry : kArrTextureImportFormatName )
        {
            if ( name == entry._dxgiName || name == entry._shortName )
            {
                outFormat = entry._format;
                return true;
            }
        }
        return false;
    }

    string_view TextureImportFormatUtil::getName( TextureImportFormat format )
    {
        return findFormatEntryInternal( format )._dxgiName;
    }

    uint32 TextureImportFormatUtil::getDxgiFormat( TextureImportFormat format, bool bSRGB )
    {
        const TextureImportFormatName& entry = findFormatEntryInternal( format );
        return bSRGB ? entry._srgbDxgiFormat : entry._dxgiFormat;
    }

    bool TextureImportConfig::isCatchAllRule( const TextureImportRule& rule )
    {
        return rule._filter.isCatchAll();
    }

    size_t TextureImportConfig::findShadowingRuleIndex() const
    {
        // 마지막 규칙은 캐치올이어도 정상이다. 그래서 하나 앞까지만 본다.
        return AssetImportRuleUtil::findMisplacedCatchAllIndex( _listRule );
    }

    TextureImportRuleMatch TextureImportConfig::findMatchingRule( string_view relativePath ) const
    {
        return AssetImportRuleUtil::select( _listRule, relativePath, "TextureImportConfig" );
    }
} // namespace sw::editor
