#include "pch.h"

#include "Engine/Graphics/Material/MaterialXML.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialPacking.h"
#include "Engine/Graphics/Material/RenderBlendPreset.h"
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
            static constexpr const utf8* kBlendTargetListName = "_listBlendTarget";

            /** @brief 열거 항목(`MaterialEnumEntryParams`)을 읽습니다. 읽지 못한 칸이 있으면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool parseEnumEntries( XMLNode parent, string_view sourceName, vector<MaterialEnumEntry>& outListEntry )
            {
                outListEntry.clear();
                const XMLNode list = parent.findChild( "_enumEntries" );
                for ( XMLNode item = list.isValid() ? list.findChild( "item" ) : XMLNode{}; item; item = item.findNextSibling( "item" ) )
                {
                    MaterialEnumEntryParams params;
                    if ( XMLFields::readFields( item, params, sourceName ) == false )
                        return false;
                    outListEntry.push_back( MaterialEnumEntry{ std::move( params._name ), params._value } );
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
        // 칸은 스키마(`MaterialPropertyParams`)가 정한다 — 철자가 다른 속성(bSRGB · bHDR 처럼)이 조용히 기본값이 되지 않게 모르는 이름은 로드 오류다.
        MaterialPropertyParams params;
        if ( XMLFields::readFields( item, params, string_view{ item.getAttributeText( "name" ) } ) == false )
            return false;
        prop._name = params._name;
        uint32 size{ 0 };
        if ( params._type.empty() == false )
            prop._type = MaterialPacking::stringToType( params._type, size );
        // 기본값과 현재 값은 한쪽만 적으면 다른 쪽이 따라간다.
        prop._defaultValue        = params._defaultValue.empty() ? params._value : params._defaultValue;
        prop._value               = params._value.empty() ? prop._defaultValue : params._value;
        prop._assetPath           = params._assetPath;
        prop._enumType            = params._enumType;
        prop._displayName         = params._displayName;
        prop._group               = params._group;
        prop._tooltip             = params._tooltip;
        prop._shaderKeyword       = params._shaderKeyword;
        prop._collectionPath      = params._collection;
        prop._collectionParameter = params._collectionParameter;
        if ( prop._collectionPath.empty() != prop._collectionParameter.empty() )
        {
            SW_LOG_ERROR_ID( "Material.CollectionBindingHalf", "Material property '%#' needs both collection and collectionParameter", prop._name );
            return false;
        }
        prop._min       = params._min;
        prop._max       = params._max;
        prop._bHDR      = params._bHdr ? SW_TRUE : SW_FALSE;
        prop._bSRGB     = params._bSrgb ? SW_TRUE : SW_FALSE;
        prop._bHidden   = params._bHidden ? SW_TRUE : SW_FALSE;
        prop._bAdvanced = params._bAdvanced ? SW_TRUE : SW_FALSE;
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

    bool MaterialXML::parsePermutationNode( XMLNode root, MaterialPermutationDef& outDesc )
    {
        outDesc = MaterialPermutationDef{};
        if ( root.isValid() == false )
            return true;
        XMLNode permutationsNode = root.findChild( "_permutations" );
        if ( permutationsNode.isValid() == false )
            return true;

        const string_view         sourceName{ root.getAttributeText( "name" ) };
        MaterialPermutationParams params;
        if ( XMLFields::readFields( permutationsNode, params, sourceName ) == false ) // 적은 값은 읽혀야 한다 — 읽지 못한 값을 기본값으로 바꿔 계속하지 않는다
            return false;
        outDesc._quality   = params._quality;
        outDesc._shaderLOD = params._shaderLOD;
        outDesc._usage     = params._usage;

        XMLNode always = permutationsNode.findChild( "_alwaysDefines" );
        MaterialXMLInternal::parseStringListItems( always, outDesc._listAlwaysDefine );

        XMLNode switches = permutationsNode.findChild( "_staticSwitches" );
        if ( switches.isValid() )
        {
            for ( XMLNode item = switches.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                MaterialStaticSwitchParams switchFields;
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
                MaterialMultiCompileParams multiCompileFields;
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

    void MaterialXML::appendPermutationNode( XMLNode root, const MaterialPermutationDef& permutationsNode )
    {
        XMLNode node = root.appendChild( "_permutations" );
        // 순열은 부모 것을 통째로 바꾸므로 기본값을 생략해도 되지만, 차이(`DataInheritance::makeDelta`)는 글을 맞대므로 늘 같은 꼴로 모두 쓴다.
        XMLFields::writeEveryField( node, MaterialPermutationParams{ permutationsNode._quality, permutationsNode._shaderLOD, permutationsNode._usage } );
        MaterialXMLInternal::appendMaterialStringList( node, "_alwaysDefines", permutationsNode._listAlwaysDefine );

        if ( permutationsNode._listStaticSwitch.empty() == false )
        {
            XMLNode list = node.appendChild( "_staticSwitches" );
            for ( const MaterialStaticSwitch& entry : permutationsNode._listStaticSwitch )
            {
                MaterialStaticSwitchParams switchFields;
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
                XMLFields::writeFields( item, MaterialMultiCompileParams{ mc._name, mc._selected } );
                MaterialXMLInternal::appendMaterialStringList( item, "_options", mc._listOption );
            }
        }
    }
} // namespace sw

namespace sw
{
    DataInheritanceSchema MaterialXML::makeInheritanceSchema()
    {
        DataInheritanceSchema schema{ "MaterialDef" };
        schema._listKeptAttribute.emplace_back( "formatVersion" ); // 판 · 이름은 자기 에셋의 것이다
        schema._listKeptAttribute.emplace_back( "name" );
        schema._bEmptyClears = SW_FALSE;
        DataInheritanceSchema properties{ "_properties" };
        properties._keyAttribute = "name";
        properties._bEmptyClears = SW_FALSE;
        schema._listField.push_back( std::move( properties ) );
        return schema;
    }

    SW_DATA_INHERITANCE_SCHEMA( MaterialDef, &MaterialXML::makeInheritanceSchema );

    bool MaterialXML::isRenderStateAttribute( const utf8* pName )
    {
        for ( const PropertyInfo& prop : FixedRenderState::StaticType()->getPropertiesWithBase() )
        {
            if ( XMLFields::getAttributeName( prop ) == pName )
                return true;
        }
        return false;
    }

    bool MaterialXML::parseRenderFields( XMLNode root, const MaterialRootParams& rootParams, string_view sourceName, MaterialDef& inoutDesc )
    {
        // 렌더 상태 칸을 0 으로 채운 값과 1 로 채운 값 위에 두 번 읽으면 두 값이 같은 비트가 적은 칸이다(`RenderStateOverride::makeFromReads`). 다른 스키마의 속성은 모르는 칸이 아니다.
        const auto isOtherAttribute = []( const utf8* pName )
        { return isRenderStateAttribute( pName ) == false; };
        const TypeInfo&  stateType = *FixedRenderState::StaticType();
        FixedRenderState overZeros = RenderStateOverride::makeFilledState( 0x00 );
        FixedRenderState overOnes  = RenderStateOverride::makeFilledState( 0xFF );
        if ( XMLFields::readFields( root, &overZeros, stateType, sourceName, {}, isOtherAttribute ) == false || XMLFields::readFields( root, &overOnes, stateType, sourceName, {}, isOtherAttribute ) == false )
            return false;
        RenderStateOverride stateOverride = RenderStateOverride::makeFromReads( overZeros, overOnes );

        inoutDesc._alphaMode         = rootParams._alphaMode;
        inoutDesc._alphaCutoff       = rootParams._alphaCutoff;
        inoutDesc._renderQueue       = rootParams._renderQueue;
        inoutDesc._bTwoSidedLighting = rootParams._bTwoSidedLighting ? SW_TRUE : SW_FALSE;
        inoutDesc._blendPreset       = rootParams._blendPreset;
        inoutDesc._listBlendTarget.clear();

        bool       bValid = true;
        const auto fail   = [&bValid]()
        { bValid = false; };
        // 깊이 테스트는 첨부와 함께 움직인다 — 패스의 PSO 깊이 형식과 렌더 패스가 어긋난다.
        if ( stateOverride.hasDepthTestField() )
        {
            SW_LOG_ERROR_ID( "Material.DepthTestNotOverridable", "%#: enableDepthTest belongs to the pass (it binds the depth attachment) - write depthCompare=\"Always\" to pass every pixel", sourceName );
            fail();
        }
        const bool bNoCull = static_cast<uint8>( stateOverride._mask._cullMode ) != 0 && stateOverride._values._cullMode == CullMode::None;
        if ( rootParams._bTwoSidedLighting && bNoCull == false )
        {
            SW_LOG_ERROR_ID( "Material.TwoSidedLightingNeedsNoCull", "%#: twoSidedLighting lights the back faces - it needs cullMode=\"None\" too", sourceName );
            fail();
        }
        if ( rootParams._alphaMode != MaterialAlphaMode::Mask && rootParams._alphaCutoff != MaterialRootParams{}._alphaCutoff )
        {
            SW_LOG_ERROR_ID( "Material.AlphaCutoffWithoutMask", "%#: alphaCutoff needs alphaMode=\"Mask\"", sourceName );
            fail();
        }

        // 블렌드 칸: 프리셋(모든 타깃) 또는 타깃 목록이다.
        const XMLNode blendTargets = root.findChild( MaterialXMLInternal::kBlendTargetListName );
        if ( rootParams._blendPreset.empty() == false && blendTargets.isValid() )
        {
            SW_LOG_ERROR_ID( "Material.BlendPresetAndTargets", "%#: blendPreset and _listBlendTarget go separately - write one of them", sourceName );
            fail();
        }
        if ( rootParams._blendPreset.empty() == false )
        {
            const RenderBlendPreset* const pPreset = RenderBlendPresetTable::find( rootParams._blendPreset );
            if ( pPreset != nullptr )
            {
                stateOverride._arrBlendTarget[0] = *pPreset;
                stateOverride._blendTargetCount  = 1;
            }
            else
            {
                SW_LOG_ERROR_ID( "Material.BlendPresetUnknown", "%#: blendPreset '%#' is not in %#", sourceName, rootParams._blendPreset, RenderBlendPresetTable::kAssetPath );
                fail();
            }
        }
        for ( XMLNode item = blendTargets.isValid() ? blendTargets.findChild( "BlendTargetState" ) : XMLNode{}; item; item = item.findNextSibling( "BlendTargetState" ) )
        {
            BlendTargetState target{};
            if ( XMLFields::readFields( item, target, sourceName ) == false )
            {
                fail();
                continue;
            }
            const utf8* const pReason = target.findInvalidReason();
            if ( pReason != nullptr )
            {
                SW_LOG_ERROR_ID( "Material.BlendTargetInvalid", "%#: blend target %#: %#", sourceName, static_cast<uint32>( inoutDesc._listBlendTarget.size() ), pReason );
                fail();
            }
            if ( inoutDesc._listBlendTarget.size() >= kMaxColorAttachments )
            {
                SW_LOG_ERROR_ID( "Material.TooManyBlendTargets", "%#: more than %# blend targets", sourceName, kMaxColorAttachments );
                fail();
                break;
            }
            stateOverride._arrBlendTarget[inoutDesc._listBlendTarget.size()] = target;
            inoutDesc._listBlendTarget.push_back( target );
            stateOverride._blendTargetCount = static_cast<uint8>( inoutDesc._listBlendTarget.size() );
        }
        for ( uint32 targetIndex = 0; targetIndex < stateOverride._blendTargetCount; ++targetIndex )
        {
            // 쓰기 마스크만 바꾸는 것은 불투명도 받는다. 켜진 블렌드만 반투명 배치 목록의 일이다.
            if ( stateOverride._arrBlendTarget[targetIndex]._bEnableBlend != SW_FALSE && rootParams._alphaMode != MaterialAlphaMode::Blend )
            {
                SW_LOG_ERROR_ID( "Material.BlendWithoutAlphaModeBlend", "%#: a blending target needs alphaMode=\"Blend\" - it is what sends the material to the transparent batches", sourceName );
                fail();
                break;
            }
        }

        // 두 칸의 define 은 칸이 낸다 — 직접 적으면 칸과 갈라져 컬링 · 깊이 패스와 맞지 않는다.
        const auto isFieldDefine = []( string_view define )
        { return define == kMaterialAlphaCutoffDefine || define == kMaterialTwoSidedDefine; };
        bool bDefineFound = false;
        for ( const string& define : inoutDesc._permutations._listAlwaysDefine )
        {
            bDefineFound = bDefineFound || isFieldDefine( define );
        }
        for ( const MaterialStaticSwitch& entry : inoutDesc._permutations._listStaticSwitch )
        {
            bDefineFound = bDefineFound || isFieldDefine( entry._keyword );
        }
        if ( bDefineFound )
        {
            SW_LOG_ERROR_ID( "Material.DefineBelongsToField", "%#: %# and %# come from alphaMode=\"Mask\" and twoSidedLighting - do not write them as defines or switches", sourceName, kMaterialAlphaCutoffDefine, kMaterialTwoSidedDefine );
            fail();
        }
        inoutDesc._renderStateOverride = stateOverride;
        return bValid;
    }

    void MaterialXML::appendRenderFields( XMLNode root, const MaterialDef& desc )
    {
        // 적은 칸의 비트를 뒤집은 값을 "기본값" 으로 넘기면 적은 칸만 다르다 — 적지 않은 칸은 쓰이지 않는다.
        const FixedRenderState& values   = desc._renderStateOverride._values;
        const FixedRenderState& mask     = desc._renderStateOverride._mask;
        FixedRenderState        flipped  = values;
        const uint8*            pMask    = reinterpret_cast<const uint8*>( &mask );
        uint8*                  pFlipped = reinterpret_cast<uint8*>( &flipped );
        for ( size_t byteIndex = 0; byteIndex < sizeof( FixedRenderState ); ++byteIndex )
        {
            pFlipped[byteIndex] = static_cast<uint8>( pFlipped[byteIndex] ^ pMask[byteIndex] );
        }
        XMLFields::writeFields( root, &values, &flipped, *FixedRenderState::StaticType() );
        if ( desc._listBlendTarget.empty() )
            return;
        const XMLNode list = root.appendChild( MaterialXMLInternal::kBlendTargetListName );
        for ( const BlendTargetState& target : desc._listBlendTarget )
        {
            XMLFields::writeEveryField( list.appendChild( "BlendTargetState" ), target );
        }
    }
} // namespace sw
