#include "pch.h"

#include "Engine/Graphics/Material/MaterialXML.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialPacking.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
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
        // 칸은 스키마(`MaterialPropertyFields`)가 정한다 — 철자가 다른 속성(bSRGB · bHDR 처럼)이 조용히 기본값이 되지 않게 모르는 이름은 로드 오류다.
        MaterialPropertyFields fields;
        if ( XMLFields::readFields( item, fields, string_view{ item.getAttributeText( "name" ) } ) == false )
            return false;
        prop._name = fields._name;
        uint32 size{ 0 };
        if ( fields._type.empty() == false )
            prop._type = MaterialPacking::stringToType( fields._type, size );
        if ( fields._shaderType.empty() == false )
            prop._shaderType = MaterialPacking::stringToType( fields._shaderType, size );
        // 기본값과 현재 값은 한쪽만 적으면 다른 쪽이 따라간다.
        prop._defaultValue  = fields._defaultValue.empty() ? fields._value : fields._defaultValue;
        prop._value         = fields._value.empty() ? prop._defaultValue : fields._value;
        prop._assetPath     = fields._assetPath;
        prop._enumType      = fields._enumType;
        prop._displayName   = fields._displayName;
        prop._group         = fields._group;
        prop._tooltip       = fields._tooltip;
        prop._shaderKeyword = fields._shaderKeyword;
        prop._min           = fields._min;
        prop._max           = fields._max;
        prop._bHDR          = fields._bHdr ? SW_TRUE : SW_FALSE;
        prop._bSRGB         = fields._bSrgb ? SW_TRUE : SW_FALSE;
        prop._bHidden       = fields._bHidden ? SW_TRUE : SW_FALSE;
        prop._bAdvanced     = fields._bAdvanced ? SW_TRUE : SW_FALSE;
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
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 부모 상속(`MaterialXML::flattenParent` · `makeParentDelta`)의 TU 로컬 도우미입니다. */
        struct MaterialParentInternal
        {
            static constexpr const utf8* kRootName       = "MaterialDesc";
            static constexpr const utf8* kParentAttr     = "parent";
            static constexpr const utf8* kPropertiesName = "_properties";
            static constexpr const utf8* kItemName       = "item";
            static constexpr const utf8* kPermutations   = "_permutations";

            static XMLNode findItem( XMLNode properties, string_view name )
            {
                for ( XMLNode item = properties.findChild( kItemName ); item; item = item.findNextSibling( kItemName ) )
                {
                    if ( item.getAttributeText( "name" ) == name )
                        return item;
                }
                return XMLNode{};
            }

            static void copyAttributes( XMLNode source, XMLNode target, bool bSkipParent )
            {
                for ( XMLAttribute attribute = source.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    if ( bSkipParent && string_view( attribute.getName() ) == kParentAttr )
                        continue;
                    target.setAttribute( attribute.getName(), attribute.getValue() );
                }
            }

            /** @brief 원소가 하위 원소를 가지는지 봅니다(`_enumEntries` 같은 목록). */
            static bool hasChildElement( XMLNode node ) { return node.findChild().isValid(); }

            static bool sameAttributes( XMLNode a, XMLNode b )
            {
                uint32 countA = 0;
                for ( XMLAttribute attribute = a.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    ++countA;
                    const utf8* pOther = b.findAttribute( attribute.getName() );
                    if ( pOther == nullptr || string_view( pOther ) != string_view( attribute.getValue() ) )
                        return false;
                }
                uint32 countB = 0;
                for ( XMLAttribute attribute = b.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    ++countB;
                }
                return countA == countB;
            }

            /** @brief @p text 를 사슬 끝까지 펼칩니다. @p inoutChain 은 지나온 부모 경로(순환 검사)입니다. */
            static bool flatten( string_view text, vector<string>& inoutChain, string& outText, string& outParentPath )
            {
                XMLDocument child;
                if ( child.parse( text ) == false )
                    return false;
                const XMLNode childRoot = child.getRoot( kRootName );
                if ( childRoot.isValid() == false )
                    return false;
                outParentPath = string( childRoot.getAttributeText( kParentAttr ) );
                if ( outParentPath.empty() )
                {
                    outText = string( text );
                    return true;
                }
                for ( const string& visited : inoutChain )
                {
                    if ( visited == outParentPath )
                    {
                        SW_LOG_ERROR_ID( "Material.ParentCycle", "Material parent chain loops at '%#' - the material is not loaded", outParentPath );
                        return false;
                    }
                }
                if ( inoutChain.size() >= MaterialXML::kMaxParentDepth )
                {
                    SW_LOG_ERROR_ID( "Material.ParentTooDeep", "Material parent chain is deeper than %# at '%#' - the material is not loaded", MaterialXML::kMaxParentDepth,
                                     outParentPath );
                    return false;
                }
                inoutChain.push_back( outParentPath );

                XMLDocument parentSource;
                if ( parentSource.loadResource( AssetID{ outParentPath } ) == false )
                {
                    SW_LOG_ERROR_ID( "Material.ParentNotFound", "Material parent '%#' could not be read - the material is not loaded", outParentPath );
                    return false;
                }
                string parentText;
                string grandParentPath;
                if ( flatten( parentSource.saveToString(), inoutChain, parentText, grandParentPath ) == false )
                    return false;
                XMLDocument parent;
                if ( parent.parse( parentText ) == false )
                    return false;
                const XMLNode parentRoot = parent.getRoot( kRootName );
                if ( parentRoot.isValid() == false )
                {
                    SW_LOG_ERROR_ID( "Material.ParentNotMaterial", "Material parent '%#' is not a MaterialDesc - the material is not loaded", outParentPath );
                    return false;
                }

                XMLDocument   merged;
                const XMLNode mergedRoot = merged.appendRoot( kRootName );
                copyAttributes( parentRoot, mergedRoot, true );
                copyAttributes( childRoot, mergedRoot, true );

                // 프로퍼티: 부모 줄 순서를 지키고 자식이 적은 속성만 덮는다. 부모에 없는 이름은 뒤에 더한다.
                const XMLNode parentProps = parentRoot.findChild( kPropertiesName );
                const XMLNode childProps  = childRoot.findChild( kPropertiesName );
                const XMLNode mergedProps = mergedRoot.appendChild( kPropertiesName );
                for ( XMLNode item = parentProps.findChild( kItemName ); item; item = item.findNextSibling( kItemName ) )
                {
                    const XMLNode overrideItem = findItem( childProps, item.getAttributeText( "name" ) );
                    if ( overrideItem.isValid() && hasChildElement( overrideItem ) )
                    {
                        (void)mergedProps.appendClone( overrideItem );
                        continue;
                    }
                    const XMLNode copy = mergedProps.appendClone( item );
                    if ( overrideItem.isValid() )
                        copyAttributes( overrideItem, copy, false );
                }
                for ( XMLNode item = childProps.findChild( kItemName ); item; item = item.findNextSibling( kItemName ) )
                {
                    if ( findItem( parentProps, item.getAttributeText( "name" ) ).isValid() == false )
                        (void)mergedProps.appendClone( item );
                }
                const XMLNode childPermutations = childRoot.findChild( kPermutations );
                const XMLNode permutations      = childPermutations.isValid() ? childPermutations : parentRoot.findChild( kPermutations );
                if ( permutations.isValid() )
                    (void)mergedRoot.appendClone( permutations );
                outText = merged.saveToString();
                return true;
            }
        };
    } // namespace

    bool MaterialXML::flattenParent( string_view xmlText, string& outText, string& outParentPath )
    {
        vector<string> listChain;
        return MaterialParentInternal::flatten( xmlText, listChain, outText, outParentPath );
    }

    bool MaterialXML::makeParentDelta( string_view fullText, string_view parentFullText, string_view parentPath, string& outText )
    {
        using Internal = MaterialParentInternal;
        const string parentID( parentPath );
        XMLDocument  full;
        XMLDocument  parent;
        if ( full.parse( fullText ) == false || parent.parse( parentFullText ) == false )
            return false;
        const XMLNode fullRoot   = full.getRoot( Internal::kRootName );
        const XMLNode parentRoot = parent.getRoot( Internal::kRootName );
        if ( fullRoot.isValid() == false || parentRoot.isValid() == false )
            return false;

        XMLDocument   delta;
        const XMLNode deltaRoot = delta.appendRoot( Internal::kRootName );
        for ( XMLAttribute attribute = fullRoot.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            const string_view name = attribute.getName();
            if ( name == Internal::kParentAttr )
                continue;
            // 판 · 이름은 늘 적는다(자기 에셋이다). 나머지(셰이더 · 블렌드)는 부모와 다를 때만 적는다.
            const utf8* pInherit       = parentRoot.findAttribute( attribute.getName() );
            const bool  bAlwaysWritten = name == "formatVersion" || name == "name";
            if ( bAlwaysWritten || pInherit == nullptr || string_view( pInherit ) != string_view( attribute.getValue() ) )
                deltaRoot.setAttribute( attribute.getName(), attribute.getValue() );
            if ( name == "name" )
                deltaRoot.setAttribute( Internal::kParentAttr, string_view( parentID ) );
        }
        const XMLNode parentProps = parentRoot.findChild( Internal::kPropertiesName );
        const XMLNode fullProps   = fullRoot.findChild( Internal::kPropertiesName );
        XMLNode       deltaProps{};
        for ( XMLNode item = fullProps.findChild( Internal::kItemName ); item; item = item.findNextSibling( Internal::kItemName ) )
        {
            const XMLNode inherited = Internal::findItem( parentProps, item.getAttributeText( "name" ) );
            const bool    bChildren = Internal::hasChildElement( item ) || ( inherited.isValid() && Internal::hasChildElement( inherited ) );
            const bool    bSameList = bChildren == false || ( inherited.isValid() && item.toString() == inherited.toString() );
            if ( inherited.isValid() && bSameList && Internal::sameAttributes( item, inherited ) )
                continue;
            if ( deltaProps.isValid() == false )
                deltaProps = deltaRoot.appendChild( Internal::kPropertiesName );
            if ( inherited.isValid() == false || bSameList == false )
            {
                (void)deltaProps.appendClone( item );
                continue;
            }
            const XMLNode line = deltaProps.appendChild( Internal::kItemName );
            line.setAttribute( "name", item.getAttributeText( "name" ) );
            for ( XMLAttribute attribute = item.getFirstAttribute(); attribute; attribute = attribute.getNext() )
            {
                const utf8* pInherit = inherited.findAttribute( attribute.getName() );
                if ( pInherit == nullptr || string_view( pInherit ) != string_view( attribute.getValue() ) )
                    line.setAttribute( attribute.getName(), attribute.getValue() );
            }
        }
        const XMLNode fullPermutations   = fullRoot.findChild( Internal::kPermutations );
        const XMLNode parentPermutations = parentRoot.findChild( Internal::kPermutations );
        const bool    bSamePermutations  = fullPermutations.isValid() == parentPermutations.isValid() &&
                                       ( fullPermutations.isValid() == false || fullPermutations.toString() == parentPermutations.toString() );
        if ( bSamePermutations == false && fullPermutations.isValid() )
            (void)deltaRoot.appendClone( fullPermutations );
        outText = delta.saveToString();
        return true;
    }
} // namespace sw
