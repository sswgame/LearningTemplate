#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialPacking.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

namespace sw
{
    namespace
    {
        /** @brief 머티리얼 XML 의 반복 구조(enum 항목 · 문자열 목록)를 읽고 쓰는 TU 로컬 도우미입니다. */
        struct MaterialXMLInternal
        {
            /** @brief 블렌드 모드 이름 표의 한 줄입니다. 모드마다 이름은 하나다(별칭 없음). 읽기(`parseBlendMode`)와 쓰기(`blendModeToString`)가 이 표 하나를 본다. */
            struct BlendModeName
            {
                string_view  _name;
                RHIBlendMode _mode;
            };
            static constexpr BlendModeName kArrBlendModeName[] = {
                {     "Opaque",      RHIBlendMode::Opaque},
                {"Transparent", RHIBlendMode::Transparent},
            };

            static string nodeText( XMLNode node )
            {
                if ( node.isValid() == false || node.getText() == nullptr )
                    return {};
                return string{ StringUtil::trim( node.getText() ) };
            }

            /** @brief 열거 항목을 읽습니다. 값이 부호 없는 정수가 아니면 false 입니다(0 으로 읽지 않는다 — 두 항목이 같은 값이 된다). */
            [[nodiscard]] static bool parseEnumEntries( XMLNode parent, vector<MaterialEnumEntry>& outListEntry )
            {
                outListEntry.clear();
                if ( parent.isValid() == false )
                    return true;
                XMLNode list = parent.findChild( "_enumEntries" );
                if ( list.isValid() == false )
                    list = parent.findChild( "enumEntries" );
                if ( list.isValid() == false )
                    return true;
                for ( XMLNode item = list.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
                {
                    MaterialEnumEntry entry{};
                    entry._name            = MaterialXML::fieldText( item, "name" );
                    const string valueText = MaterialXML::fieldText( item, "value" );
                    if ( valueText.empty() == false )
                    {
                        uint64 val{ 0 };
                        if ( StringUtil::parseUint64( valueText, val, 0 ) == false || val > MathUtil::kMaxUInt32 )
                        {
                            SW_LOG_ERROR_ID( "Material.EnumValueUnreadable", "Material enum entry '%#' has value '%#', which is not a 32-bit unsigned integer", entry._name,
                                             valueText );
                            return false;
                        }
                        entry._value = static_cast<uint32>( val );
                    }
                    if ( entry._name.empty() == false )
                        outListEntry.push_back( std::move( entry ) );
                }
                return true;
            }

            static void parseStringListItems( XMLNode list, vector<string>& outListItem )
            {
                outListItem.clear();
                if ( list.isValid() == false )
                    return;
                for ( XMLNode item = list.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
                {
                    string itemText = nodeText( item );
                    if ( itemText.empty() )
                        itemText = MaterialXML::fieldText( item, "value" );
                    if ( itemText.empty() )
                        itemText = MaterialXML::fieldText( item, "name" );
                    if ( itemText.empty() == false )
                        outListItem.push_back( std::move( itemText ) );
                }
            }

            static void appendMaterialStringList( XMLNode parent, const utf8* pTag, const vector<string>& listValue )
            {
                if ( listValue.empty() )
                    return;
                XMLNode list = parent.appendChild( pTag );
                for ( const string& valueStr : listValue )
                {
                    XMLNode item = list.appendChild( "item" );
                    item.setValue( valueStr );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /** @brief 속성을 먼저 보고, 없으면 같은 이름의 자식 요소를 봅니다. */
    string MaterialXML::fieldText( XMLNode node, const utf8* pName )
    {
        if ( node.isValid() == false || pName == nullptr )
            return {};

        const utf8* pAttr = node.findAttribute( pName );
        if ( pAttr != nullptr )
            return string{ StringUtil::trim( pAttr ) };
        const utf8* pText = node.findChildText( pName );
        if ( pText != nullptr )
            return string{ StringUtil::trim( pText ) };
        return {};
    }

    bool MaterialXML::parseOptionalBoolField( XMLNode node, const utf8* pName, bool absentValue, bool& outValue )
    {
        const string text = MaterialXML::fieldText( node, pName );
        if ( text.empty() )
        {
            outValue = absentValue;
            return true;
        }
        return MaterialPacking::parseBoolToken( text, pName != nullptr ? pName : "", "Material.BooleanFieldUnreadable", outValue );
    }

    bool MaterialXML::parsePropertyNode( XMLNode item, MaterialProperty& outProp )
    {
        outProp                = MaterialProperty{};
        MaterialProperty& prop = outProp;
        prop._name             = MaterialXML::fieldText( item, "name" );
        // 아래 읽기가 보는 속성 이름 전부다. 철자가 다른 속성(bSRGB · bHDR 처럼)이 조용히 기본값이 되지 않게 모르는 이름은 로드 오류다.
        static constexpr const utf8* kArrKnownAttribute[] = { "name", "type", "cpuType", "shaderType", "defaultValue", "value",
                                                              "assetPath", "enumType", "displayName", "group", "tooltip", "shaderKeyword",
                                                              "min", "max", "bHdr", "bSrgb", "bHidden", "bAdvanced" };
        if ( XMLNameCheck::reportUnknownAttributes( item, kArrKnownAttribute, string_view{ prop._name } ) == false )
            return false;
        uint32 size{ 0 };
        string typeStr = MaterialXML::fieldText( item, "type" );
        if ( typeStr.empty() )
            typeStr = MaterialXML::fieldText( item, "cpuType" );
        if ( typeStr.empty() == false )
            prop._type = MaterialPacking::stringToType( typeStr, size );
        const string shaderTypeStr = MaterialXML::fieldText( item, "shaderType" );
        if ( shaderTypeStr.empty() == false )
            prop._shaderType = MaterialPacking::stringToType( shaderTypeStr, size );
        prop._defaultValue = MaterialXML::fieldText( item, "defaultValue" );
        prop._value        = MaterialXML::fieldText( item, "value" );
        if ( prop._defaultValue.empty() && prop._value.empty() == false )
            prop._defaultValue = prop._value;
        if ( prop._value.empty() && prop._defaultValue.empty() == false )
            prop._value = prop._defaultValue;
        prop._assetPath                = MaterialXML::fieldText( item, "assetPath" );
        prop._enumType                 = MaterialXML::fieldText( item, "enumType" );
        prop._displayName              = MaterialXML::fieldText( item, "displayName" );
        prop._group                    = MaterialXML::fieldText( item, "group" );
        prop._tooltip                  = MaterialXML::fieldText( item, "tooltip" );
        prop._shaderKeyword            = MaterialXML::fieldText( item, "shaderKeyword" );
        const string minStr            = MaterialXML::fieldText( item, "min" );
        const string maxStr            = MaterialXML::fieldText( item, "max" );
        const bool   bBoundsUnreadable = ( minStr.empty() == false && StringUtil::parseFloat( minStr, prop._min ) == false ) ||
                                       ( maxStr.empty() == false && StringUtil::parseFloat( maxStr, prop._max ) == false );
        if ( bBoundsUnreadable )
        {
            SW_LOG_ERROR_ID( "Material.BoundUnreadable", "Material parameter '%#' has an unreadable bound (min '%#', max '%#')", prop._name, minStr, maxStr );
            return false;
        }
        bool       bHDR{ false };
        bool       bSRGB{ false };
        bool       bHidden{ false };
        bool       bAdvanced{ false };
        const bool bFlagsRead = MaterialXML::parseOptionalBoolField( item, "bHdr", false, bHDR ) &&
                                MaterialXML::parseOptionalBoolField( item, "bSrgb", true, bSRGB ) &&
                                MaterialXML::parseOptionalBoolField( item, "bHidden", false, bHidden ) &&
                                MaterialXML::parseOptionalBoolField( item, "bAdvanced", false, bAdvanced );
        if ( bFlagsRead == false )
            return false;
        prop._bHDR      = bHDR;
        prop._bSRGB     = bSRGB;
        prop._bHidden   = bHidden;
        prop._bAdvanced = bAdvanced;
        if ( MaterialXMLInternal::parseEnumEntries( item, prop._listEnumEntry ) == false )
            return false;
        // 불리언 · 키워드 값은 읽을 때 확인한다 — 틀린 글이 패킹 · 순열 계산에서 조용히 false 가 되지 않게.
        const bool bBooleanValue = prop._type == MaterialPropertyType::Bool || ( prop._type == MaterialPropertyType::Keyword && prop._shaderKeyword.empty() == false );
        bool       bUnused{ false };
        if ( bBooleanValue && MaterialPacking::parseBoolToken( prop._value, prop._name, "Material.BooleanValueUnreadable", bUnused ) == false )
            return false;
        // 숫자 값도 읽을 때 확인한다 — 패커가 못 읽는 글을 0 으로 쓰지 않고 거절하므로 로드를 멈춘다. 같은 패커로 확인해 규칙이 하나다.
        const bool bNumericValue = prop._type <= MaterialPropertyType::Int4 || prop._type == MaterialPropertyType::Range || prop._type == MaterialPropertyType::Color;
        if ( bNumericValue == false )
            return true;
        MaterialProperty probe = prop;
        vector<uint8>    listScratch;
        return MaterialPacking::packPropertyIntoBuffer( probe, listScratch );
    }

    void MaterialXML::appendAttribute( XMLNode parent, const utf8* pName, string_view value )
    {
        const bool bNothingToAppend = parent.isValid() == false || pName == nullptr || value.empty();
        if ( bNothingToAppend )
            return;
        parent.appendAttribute( pName, value );
    }

    void MaterialXML::appendBoolAttribute( XMLNode parent, const utf8* pName, bool value )
    {
        parent.appendAttribute( pName, value );
    }

    bool MaterialXML::parseBlendMode( string_view modeName, RHIBlendMode& outMode )
    {
        for ( const MaterialXMLInternal::BlendModeName& entry : MaterialXMLInternal::kArrBlendModeName )
        {
            if ( modeName == entry._name ) // 철자는 표 그대로만 받는다 — 대소문자를 무시하면 이름마다 별칭이 생긴다
            {
                outMode = entry._mode;
                return true;
            }
        }
        SW_LOG_ERROR_ID( "Material.BlendModeUnknown", "Material blend mode '%#' is unknown (Opaque, Transparent)", modeName );
        return false;
    }

    const utf8* MaterialXML::blendModeToString( RHIBlendMode mode )
    {
        return mode == RHIBlendMode::Transparent ? "Transparent" : "Opaque";
    }

    bool MaterialXML::parseQuality( string_view qualityName, MaterialQualityLevel& outQuality )
    {
        const EnumInfo* pInfo = engine::getSubsystem<TypeRegistry>().findEnum( hashed_string( "sw::MaterialQualityLevel" ) );
        int64           value{ 0 };
        if ( pInfo == nullptr || pInfo->tryParse( qualityName, value ) == false )
        {
            SW_LOG_ERROR_ID( "Material.QualityUnknown", "Material quality '%#' is not a quality level", qualityName );
            return false;
        }
        outQuality = static_cast<MaterialQualityLevel>( value );
        return true;
    }

    const utf8* MaterialXML::qualityToString( MaterialQualityLevel quality )
    {
        const EnumInfo* pInfo = engine::getSubsystem<TypeRegistry>().findEnum( hashed_string( "sw::MaterialQualityLevel" ) );
        const utf8*     pStr  = pInfo != nullptr ? pInfo->valueToCString( static_cast<int64>( quality ) ) : nullptr;
        if ( pStr != nullptr )
            return pStr;
        return "High";
    }

    bool MaterialXML::parsePermutationNode( XMLNode root, MaterialPermutationDesc& outDesc )
    {
        outDesc = MaterialPermutationDesc{};
        if ( root.isValid() == false )
            return true;
        XMLNode permutationsNode = root.findChild( "_permutations" );
        if ( permutationsNode.isValid() == false )
            return true;

        // 적은 값은 읽혀야 한다 — 읽지 못한 값을 기본값으로 바꿔 계속하지 않는다(적지 않은 칸만 형식의 값이다).
        const string quality = MaterialXML::fieldText( permutationsNode, "quality" );
        if ( quality.empty() == false && MaterialXML::parseQuality( quality, outDesc._quality ) == false )
            return false;
        const string lod = MaterialXML::fieldText( permutationsNode, "shaderLOD" );
        if ( lod.empty() == false )
        {
            uint64 lodVal{ 0 };
            if ( StringUtil::parseUint64( lod, lodVal, 10 ) == false )
            {
                SW_LOG_ERROR_ID( "Material.ShaderLODUnreadable", "Material shader LOD '%#' is not a number", lod );
                return false;
            }
            outDesc._shaderLOD = static_cast<uint32>( lodVal );
        }
        const string usage = MaterialXML::fieldText( permutationsNode, "usage" );
        if ( usage.empty() == false )
        {
            const EnumInfo* pUsageEnum = engine::getSubsystem<TypeRegistry>().findEnum( hashed_string( "sw::MaterialUsageFlags" ) );
            int64           usageValue{ 0 };
            if ( pUsageEnum == nullptr || pUsageEnum->tryParseText( usage, usageValue ) == false )
            {
                SW_LOG_ERROR_ID( "Material.UsageFlagUnknown", "Material usage '%#' has an unknown flag", usage );
                return false;
            }
            outDesc._usage = static_cast<MaterialUsageFlags>( usageValue );
        }

        XMLNode always = permutationsNode.findChild( "_alwaysDefines" );
        MaterialXMLInternal::parseStringListItems( always, outDesc._listAlwaysDefine );

        XMLNode switches = permutationsNode.findChild( "_staticSwitches" );
        if ( switches.isValid() )
        {
            for ( XMLNode item = switches.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialStaticSwitch entry{};
                entry._name       = MaterialXML::fieldText( item, "name" );
                entry._keyword    = MaterialXML::fieldText( item, "keyword" );
                entry._keywordOff = MaterialXML::fieldText( item, "keywordOff" );
                bool bEnabled{ false };
                bool bShaderFeature{ false };
                if ( MaterialXML::parseOptionalBoolField( item, "bEnabled", false, bEnabled ) == false ||
                     MaterialXML::parseOptionalBoolField( item, "bShaderFeature", true, bShaderFeature ) == false )
                    return false;
                entry._bEnabled       = bEnabled;
                entry._bShaderFeature = bShaderFeature;
                if ( entry._name.empty() && entry._keyword.empty() == false )
                    entry._name = entry._keyword;
                if ( entry._keyword.empty() == false || entry._name.empty() == false )
                    outDesc._listStaticSwitch.push_back( std::move( entry ) );
            }
        }

        XMLNode multiCompileNode = permutationsNode.findChild( "_multiCompiles" );
        if ( multiCompileNode.isValid() )
        {
            for ( XMLNode item = multiCompileNode.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialMultiCompile multiCompile{};
                multiCompile._name     = MaterialXML::fieldText( item, "name" );
                multiCompile._selected = MaterialXML::fieldText( item, "selected" );
                XMLNode optionsNode    = item.findChild( "_options" );
                MaterialXMLInternal::parseStringListItems( optionsNode, multiCompile._listOption );
                if ( multiCompile._selected.empty() == false || multiCompile._listOption.empty() == false )
                    outDesc._listMultiCompile.push_back( std::move( multiCompile ) );
            }
        }
        return true;
    }

    void MaterialXML::appendPermutationNode( XMLNode root, const MaterialPermutationDesc& permutationsNode )
    {
        XMLNode node = root.appendChild( "_permutations" );
        MaterialXML::appendAttribute( node, "quality", MaterialXML::qualityToString( permutationsNode._quality ) );
        node.appendAttribute( "shaderLOD", permutationsNode._shaderLOD );
        {
            const EnumInfo* pUsageEnum = engine::getSubsystem<TypeRegistry>().findEnum( hashed_string( "sw::MaterialUsageFlags" ) );
            const utf8*     pUsageStr  = pUsageEnum != nullptr ? pUsageEnum->valueToCString( static_cast<int64>( permutationsNode._usage ) ) : nullptr;
            MaterialXML::appendAttribute( node, "usage", pUsageStr != nullptr ? pUsageStr : "None" );
        }
        MaterialXMLInternal::appendMaterialStringList( node, "_alwaysDefines", permutationsNode._listAlwaysDefine );

        if ( permutationsNode._listStaticSwitch.empty() == false )
        {
            XMLNode list = node.appendChild( "_staticSwitches" );
            for ( const MaterialStaticSwitch& entry : permutationsNode._listStaticSwitch )
            {
                XMLNode item = list.appendChild( "item" );
                MaterialXML::appendAttribute( item, "name", entry._name );
                MaterialXML::appendAttribute( item, "keyword", entry._keyword );
                if ( entry._keywordOff.empty() == false )
                    MaterialXML::appendAttribute( item, "keywordOff", entry._keywordOff );
                MaterialXML::appendBoolAttribute( item, "bEnabled", entry._bEnabled );
                MaterialXML::appendBoolAttribute( item, "bShaderFeature", entry._bShaderFeature );
            }
        }

        if ( permutationsNode._listMultiCompile.empty() == false )
        {
            XMLNode list = node.appendChild( "_multiCompiles" );
            for ( const MaterialMultiCompile& mc : permutationsNode._listMultiCompile )
            {
                XMLNode item = list.appendChild( "item" );
                MaterialXML::appendAttribute( item, "name", mc._name );
                MaterialXML::appendAttribute( item, "selected", mc._selected );
                MaterialXMLInternal::appendMaterialStringList( item, "_options", mc._listOption );
            }
        }
    }
<<<<<<< HEAD
=======

    bool Material::loadFromFile( const AssetID& assetID )
    {
        XMLDocument doc;
        if ( doc.loadResource( assetID ) == false )
            return false;
        return loadFromXML( doc.saveToString() );
    }

    bool Material::loadFromDiskFile( string_view diskPath )
    {
        XMLDocument doc;
        if ( doc.loadFile( diskPath ) == false )
            return false;
        return loadFromXML( doc.saveToString() );
    }

    TaskHandle Material::loadFromFileAsync( string_view assetRelativePath )
    {
        TaskHandle handle = engine::getSubsystem<TaskManager>().emplaceTask(
            "LoadMaterialAsync",
            SW_DELEGATE_FUNCTION( TaskArgsDelegate, Material::loadFromFileAsyncJob ),
            MakeTaskArgs( _asyncLoadState, string( assetRelativePath ) ) );
        handle.submit();
        return handle;
    }

    void Material::loadFromFileAsyncJob( const TaskArgs& args )
    {
        shared_ptr<AsyncLoadState> state = args.get<shared_ptr<AsyncLoadState>>( 0 );
        if ( state == nullptr )
            return;

        ScopedLock<mutex> lock{ state->_mutex };
        if ( state->_pMaterial == nullptr )
            return;
        if ( state->_pMaterial->loadFromFile( AssetID{ args.get<string>( 1 ) } ) == false )
            SW_LOG_WARNING_ID( "Material.CommandLoadFailed", "Could not load material '%#'", args.get<string>( 1 ) );
    }

    bool Material::saveToFile( const AbsolutePath& absPath ) const
    {

        XMLDocument doc;
        if ( doc.parse( saveToString() ) == false )
            return false;
        return doc.saveFile( absPath.str() );
    }

    string Material::saveToString() const
    {
        this->syncDescFromRuntime();

        XMLDocument doc;
        XMLNode     root = doc.appendRoot( "MaterialDesc" );

        AssetXMLFormat::writeXMLVersion( root, Material::kAssetType._formatVersion );
        MaterialUtil::appendAttribute( root, "name", _desc._name );
        MaterialUtil::appendAttribute( root, "shaderPath", _desc._shaderPath );
        MaterialUtil::appendAttribute( root, "blendMode", MaterialUtil::blendModeToString( _blendMode ) );

        XMLNode props = root.appendChild( "_properties" );

        for ( const MaterialProperty& prop : _data._listProperty )
        {
            XMLNode item = props.appendChild( "item" );

            MaterialUtil::appendAttribute( item, "name", prop._name );
            MaterialUtil::appendAttribute( item, "type", MaterialUtil::typeToString( prop._type ) );
            if ( prop._shaderType != MaterialPropertyType::Unknown )
                MaterialUtil::appendAttribute( item, "shaderType", MaterialUtil::typeToString( prop._shaderType ) );
            if ( prop._defaultValue.empty() == false )
                MaterialUtil::appendAttribute( item, "defaultValue", prop._defaultValue );
            if ( prop._value.empty() == false && prop._value != prop._defaultValue )
                MaterialUtil::appendAttribute( item, "value", prop._value );
            if ( prop._assetPath.empty() == false )
                MaterialUtil::appendAttribute( item, "assetPath", prop._assetPath );
            if ( prop._enumType.empty() == false )
                MaterialUtil::appendAttribute( item, "enumType", prop._enumType );
            if ( prop._displayName.empty() == false )
                MaterialUtil::appendAttribute( item, "displayName", prop._displayName );
            if ( prop._group.empty() == false )
                MaterialUtil::appendAttribute( item, "group", prop._group );
            if ( prop._tooltip.empty() == false )
                MaterialUtil::appendAttribute( item, "tooltip", prop._tooltip );
            if ( prop._shaderKeyword.empty() == false )
                MaterialUtil::appendAttribute( item, "shaderKeyword", prop._shaderKeyword );
            if ( prop._type == MaterialPropertyType::Range )
            {
                item.appendAttribute( "min", prop._min );
                item.appendAttribute( "max", prop._max );
            }
            if ( prop._type == MaterialPropertyType::Color )
            {
                MaterialUtil::appendBoolAttribute( item, "bHdr", prop._bHDR );
                MaterialUtil::appendBoolAttribute( item, "bSrgb", prop._bSRGB );
            }
            if ( MaterialUtil::isTextureType( prop._type ) )
                MaterialUtil::appendBoolAttribute( item, "bSrgb", prop._bSRGB );
            if ( prop._bHidden )
                MaterialUtil::appendBoolAttribute( item, "bHidden", true );
            if ( prop._bAdvanced )
                MaterialUtil::appendBoolAttribute( item, "bAdvanced", true );

            if ( prop._listEnumEntry.empty() == false )
            {
                XMLNode list = item.appendChild( "_enumEntries" );
                for ( const MaterialEnumEntry& enumEntry : prop._listEnumEntry )
                {
                    XMLNode eItem = list.appendChild( "item" );
                    MaterialUtil::appendAttribute( eItem, "name", enumEntry._name );
                    eItem.appendAttribute( "value", enumEntry._value );
                }
            }
        }

        MaterialUtil::appendPermutationNode( root, _desc._permutations );

        return doc.saveToString();
    }

    bool Material::loadFromXML( string_view xmlText )
    {
        XMLDocument doc;
        if ( doc.parse( xmlText ) == false )
            return false;

        XMLNode root = doc.getRoot( "MaterialDesc" );
        if ( root.isValid() == false )
            return false;

        if ( AssetXMLFormat::isCurrentXMLVersion( Material::kAssetType, root ) == false )
            return false;

        _desc       = MaterialDesc{};
        _desc._name = MaterialUtil::fieldText( root, "name" );
        setShaderPath( MaterialUtil::fieldText( root, "shaderPath" ) );
        // 블렌드 모드는 읽을 때 푼다 — 적지 않으면 불투명이고, 모르는 이름은 로드 오류다(불투명으로 조용히 가지 않는다).
        const string blendModeText = MaterialUtil::fieldText( root, "blendMode" );
        RHIBlendMode blendMode{ RHIBlendMode::Opaque };
        if ( blendModeText.empty() == false && MaterialUtil::parseBlendMode( blendModeText, blendMode ) == false )
            return false;
        _desc._blendMode = MaterialUtil::blendModeToString( blendMode );

        XMLNode props = root.findChild( "_properties" );
        if ( props.isValid() )
        {
            for ( XMLNode item = props.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialProperty prop{};
                if ( MaterialUtil::parsePropertyNode( item, prop ) == false )
                    return false;
                if ( prop._name.empty() == false )
                    _desc._listProperty.push_back( std::move( prop ) );
            }
        }

        if ( MaterialUtil::parsePermutationNode( root, _desc._permutations ) == false )
            return false;

        _blendMode = blendMode;
        applyDescToRuntime();
        return true;
    }

    void Material::applyDescToRuntime()
    {
        _data._listProperty = _desc._listProperty;
        // 다시 로드하면 프로퍼티가 XML 순서로 다시 쌓인다 — 셰이더 레이아웃에 맞춘 상태도 함께 풀어야 다음 ensureShaderLayout 이 다시 맞춘다.
        // 표시가 남으면 XML 순서 바이트와 옛 stride 가 함께 GPUScene 에 올라간다(셰이더가 읽는 자리에 다른 프로퍼티 값).
        _bShaderLayoutSynced = SW_FALSE;
        _elementStride       = 0;
        rebuildPackedBuffer();
    }

    void Material::syncDescFromRuntime() const
    {
        auto self                 = const_cast<Material*>( this );
        self->_desc._listProperty = _data._listProperty;
        self->_desc._blendMode    = MaterialUtil::blendModeToString( _blendMode );
        self->_desc._name         = _desc._name;
        self->setShaderPath( _desc._shaderPath );
    }
>>>>>>> 03b9044fed (WIP - XML 읽기의 조용한 대신 값 없애기 · id 붙이기(작업자가 남긴 미완성 변경, 통합에서 검토))
} // namespace sw
