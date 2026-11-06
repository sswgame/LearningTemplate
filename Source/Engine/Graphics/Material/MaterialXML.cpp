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
            /** @brief 열거 항목(`MaterialEnumEntryFields`)을 읽습니다. 읽지 못한 칸이 있으면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool parseEnumEntries( XMLNode parent, string_view sourceName, vector<MaterialEnumEntry>& outListEntry )
            {
                outListEntry.clear();
                const XMLNode list = parent.findChild( "_enumEntries" );
                for ( XMLNode item = list.isValid() ? list.findChild( "item" ) : XMLNode{}; item; item = item.findNextSibling( "item" ) )
                {
                    MaterialEnumEntryFields fields;
                    if ( XMLFields::readFields( item, fields, sourceName ) == false )
                        return false;
                    outListEntry.push_back( MaterialEnumEntry{ std::move( fields._name ), fields._value } );
                }
                return true;
            }

            /** @brief 글 목록(`<item>글</item>`)을 읽습니다. 빈 줄은 건너뛴다. */
            static void parseStringListItems( XMLNode list, vector<string>& outListItem )
            {
                outListItem.clear();
                for ( XMLNode item = list.isValid() ? list.findChild( "item" ) : XMLNode{}; item; item = item.findNextSibling( "item" ) )
                {
                    const string_view itemText = item.getText() != nullptr ? StringUtil::trim( item.getText() ) : string_view{};
                    if ( itemText.empty() == false )
                        outListItem.push_back( string( itemText ) );
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
        if ( MaterialXMLInternal::parseEnumEntries( item, string_view{ item.getAttributeText( "name" ) }, prop._listEnumEntry ) == false )
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

    bool MaterialXML::parsePermutationNode( XMLNode root, MaterialPermutationDesc& outDesc )
    {
        outDesc = MaterialPermutationDesc{};
        if ( root.isValid() == false )
            return true;
        XMLNode permutationsNode = root.findChild( "_permutations" );
        if ( permutationsNode.isValid() == false )
            return true;

        const string_view         sourceName{ root.getAttributeText( "name" ) };
        MaterialPermutationFields fields;
        if ( XMLFields::readFields( permutationsNode, fields, sourceName ) == false ) // 적은 값은 읽혀야 한다 — 읽지 못한 값을 기본값으로 바꿔 계속하지 않는다
            return false;
        outDesc._quality   = fields._quality;
        outDesc._shaderLOD = fields._shaderLOD;
        outDesc._usage     = fields._usage;

        XMLNode always = permutationsNode.findChild( "_alwaysDefines" );
        MaterialXMLInternal::parseStringListItems( always, outDesc._listAlwaysDefine );

        XMLNode switches = permutationsNode.findChild( "_staticSwitches" );
        if ( switches.isValid() )
        {
            for ( XMLNode item = switches.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialStaticSwitchFields switchFields;
                if ( XMLFields::readFields( item, switchFields, sourceName ) == false )
                    return false;
                MaterialStaticSwitch entry{};
                entry._name           = switchFields._name.empty() ? switchFields._keyword : switchFields._name;
                entry._keyword        = switchFields._keyword;
                entry._keywordOff     = switchFields._keywordOff;
                entry._bEnabled       = switchFields._bEnabled;
                entry._bShaderFeature = switchFields._bShaderFeature;
                if ( entry._keyword.empty() == false || entry._name.empty() == false )
                    outDesc._listStaticSwitch.push_back( std::move( entry ) );
            }
        }

        XMLNode multiCompileNode = permutationsNode.findChild( "_multiCompiles" );
        if ( multiCompileNode.isValid() )
        {
            for ( XMLNode item = multiCompileNode.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialMultiCompileFields multiCompileFields;
                if ( XMLFields::readFields( item, multiCompileFields, sourceName ) == false )
                    return false;
                MaterialMultiCompile multiCompile{};
                multiCompile._name     = multiCompileFields._name;
                multiCompile._selected = multiCompileFields._selected;
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
        // 순열은 부모 것을 통째로 바꾸므로 기본값을 생략해도 되지만, 차이(`DataInheritance::makeDelta`)는 글을 맞대므로 늘 같은 꼴로 모두 쓴다.
        XMLFields::writeEveryField( node, MaterialPermutationFields{ permutationsNode._quality, permutationsNode._shaderLOD, permutationsNode._usage } );
        MaterialXMLInternal::appendMaterialStringList( node, "_alwaysDefines", permutationsNode._listAlwaysDefine );

        if ( permutationsNode._listStaticSwitch.empty() == false )
        {
            XMLNode list = node.appendChild( "_staticSwitches" );
            for ( const MaterialStaticSwitch& entry : permutationsNode._listStaticSwitch )
            {
                MaterialStaticSwitchFields switchFields;
                switchFields._name           = entry._name;
                switchFields._keyword        = entry._keyword;
                switchFields._keywordOff     = entry._keywordOff;
                switchFields._bEnabled       = entry._bEnabled;
                switchFields._bShaderFeature = entry._bShaderFeature;
                XMLFields::writeFields( list.appendChild( "item" ), switchFields );
            }
        }

        if ( permutationsNode._listMultiCompile.empty() == false )
        {
            XMLNode list = node.appendChild( "_multiCompiles" );
            for ( const MaterialMultiCompile& mc : permutationsNode._listMultiCompile )
            {
                XMLNode item = list.appendChild( "item" );
                XMLFields::writeFields( item, MaterialMultiCompileFields{ mc._name, mc._selected } );
                MaterialXMLInternal::appendMaterialStringList( item, "_options", mc._listOption );
            }
        }
    }
} // namespace sw

namespace sw
{
    DataInheritanceSchema MaterialXML::makeInheritanceSchema()
    {
        DataInheritanceSchema schema{ "MaterialDesc" };
        schema._listKeptAttribute.emplace_back( "formatVersion" ); // 판 · 이름은 자기 에셋의 것이다
        schema._listKeptAttribute.emplace_back( "name" );
        schema._bEmptyClears = SW_FALSE;
        DataInheritanceSchema properties{ "_properties" };
        properties._keyAttribute = "name";
        properties._bEmptyClears = SW_FALSE;
        schema._listField.push_back( std::move( properties ) );
        return schema;
    }

    SW_DATA_INHERITANCE_SCHEMA( MaterialDesc, &MaterialXML::makeInheritanceSchema );
} // namespace sw
