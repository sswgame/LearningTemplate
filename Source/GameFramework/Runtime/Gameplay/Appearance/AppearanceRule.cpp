#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceRule.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceXMLUtil.h"

namespace sw
{
    namespace
    {
        struct AppearanceRuleInternal
        {
            // 동작 원소마다 받는 칸이다(그 밖의 칸은 오류 — 칸 구조체 하나를 함께 쓰므로 동작마다 막는다).
            static constexpr const utf8* kArrHideField[]     = { "target", "part", "itemTag", "region" };
            static constexpr const utf8* kArrVariantField[]  = { "target", "name" };
            static constexpr const utf8* kArrMeshField[]     = { "target", "part", "mesh" };
            static constexpr const utf8* kArrMaterialField[] = { "target", "part", "section", "material" };
            static constexpr const utf8* kArrMorphField[]    = { "target", "name", "weight" };
            static constexpr const utf8* kArrSocketField[]   = { "name", "parent", "offset", "rotation" };
            static constexpr const utf8* kArrValueField[]    = { "target", "part", "section", "name", "value" };
            struct ActionFieldSet
            {
                const utf8*        _pElement;
                const utf8* const* _ppField;
                uint32             _fieldCount;
            };
            static constexpr ActionFieldSet kArrActionField[] = {
                {          "Hide",     kArrHideField,     SW_COUNT_OF( kArrHideField )},
                {       "Variant",  kArrVariantField,  SW_COUNT_OF( kArrVariantField )},
                {      "SwapMesh",     kArrMeshField,     SW_COUNT_OF( kArrMeshField )},
                {  "SwapMaterial", kArrMaterialField, SW_COUNT_OF( kArrMaterialField )},
                {         "Morph",    kArrMorphField,    SW_COUNT_OF( kArrMorphField )},
                {"OverrideSocket",   kArrSocketField,   SW_COUNT_OF( kArrSocketField )},
                { "MaterialValue",    kArrValueField,    SW_COUNT_OF( kArrValueField )},
            };

            /** @brief 동작 원소 @p node 의 칸을 읽습니다. 그 동작이 받지 않는 칸은 오류입니다. 모르는 동작이면 false 입니다. */
            [[nodiscard]] static bool readActionProperties( const XMLNode& node, AppearanceRuleActionParams& outParams, AppearanceLoadReport& report, string_view sourceName )
            {
                for ( const ActionFieldSet& set : kArrActionField )
                {
                    if ( StringUtil::equals( node.getName(), set._pElement ) == false )
                        continue;
                    (void)AppearanceXMLUtil::reportUnknownAttributes( node, set._ppField, set._fieldCount, report, sourceName );
                    (void)AppearanceXMLUtil::readProperties( node, outParams, report, sourceName, { "parent", "offset", "rotation", "value" } );
                    return true;
                }
                return false;
            }

            [[nodiscard]] static bool readCondition( const XMLNode& node, AppearanceRuleCondition& outCondition, AppearanceLoadReport& report, string_view sourceName, const hashed_string& ruleID )
            {
                AppearanceRuleWhenParams params;
                (void)AppearanceXMLUtil::readProperties( node, params, report, sourceName, { "bodyShape", "bodyType" } ); // 오류는 보고서에 쌓인다
                outCondition._bNegate      = params._bNot ? SW_TRUE : SW_FALSE;
                outCondition._target       = params._target; // 태그 조건에서만 쓰는 선택 이름
                const utf8* pTag           = node.findAttribute( "tag" );
                const utf8* pCharacterTag  = node.findAttribute( "characterTag" );
                const utf8* pOccupied      = node.findAttribute( "occupied" );
                const utf8* pBodyShape     = node.findAttribute( "bodyShape" );
                const utf8* pBodyType      = node.findAttribute( "bodyType" );
                const int32 predicateCount = ( pTag != nullptr ? 1 : 0 ) + ( pCharacterTag != nullptr ? 1 : 0 ) + ( pOccupied != nullptr ? 1 : 0 ) + ( pBodyShape != nullptr ? 1 : 0 ) + ( pBodyType != nullptr ? 1 : 0 );
                const bool  bTargetUsed    = pTag != nullptr;
                if ( predicateCount != 1 || ( outCondition._target.empty() == false && bTargetUsed == false ) )
                {
                    report.addErrorWithID( "Appearance.RuleWhenShape", "%#: rule '%#' <When> needs exactly one of tag / characterTag / occupied / bodyShape / bodyType (target only with tag)", sourceName, ruleID.c_str() );
                    return false;
                }
                if ( pTag != nullptr )
                {
                    outCondition._kind = outCondition._target.empty() ? AppearanceRuleConditionKind::AnyTag : AppearanceRuleConditionKind::TargetTag;
                    outCondition._tag  = TagID::request( params._tag );
                }
                else if ( pCharacterTag != nullptr )
                {
                    outCondition._kind = AppearanceRuleConditionKind::CharacterTag;
                    outCondition._tag  = TagID::request( params._characterTag );
                }
                else if ( pOccupied != nullptr )
                {
                    outCondition._kind   = AppearanceRuleConditionKind::Occupied;
                    outCondition._target = params._occupied;
                    if ( outCondition._target.empty() )
                        report.addErrorWithID( "Appearance.NameEmpty", "%#: rule '%#' <When occupied> needs a slot name", sourceName, ruleID.c_str() );
                }
                else
                {
                    outCondition._kind = pBodyShape != nullptr ? AppearanceRuleConditionKind::BodyShape : AppearanceRuleConditionKind::BodyType;
                    AppearanceXMLUtil::readNameList( node, pBodyShape != nullptr ? "bodyShape" : "bodyType", outCondition._listName );
                }
                // 속성은 공통 어휘의 줄임말이다.
                string text = outCondition._bNegate == SW_TRUE ? "!" : "";
                switch ( outCondition._kind )
                {
                    case AppearanceRuleConditionKind::TargetTag:
                    {
                        StringBuilder<constant::buffer::kMax256> textPart4;
                        textPart4.appendFormat( "targetTag(%#, %#)", outCondition._target.c_str(), params._tag.c_str() );
                        text += textPart4.c_str();
                    }
                    break;
                    case AppearanceRuleConditionKind::AnyTag:
                    {
                        StringBuilder<constant::buffer::kMax256> textPart3;
                        textPart3.appendFormat( "anyTag(%#)", params._tag.c_str() );
                        text += textPart3.c_str();
                    }
                    break;
                    case AppearanceRuleConditionKind::CharacterTag:
                    {
                        StringBuilder<constant::buffer::kMax256> textPart2;
                        textPart2.appendFormat( "tag(%#)", params._characterTag.c_str() );
                        text += textPart2.c_str();
                    }
                    break;
                    case AppearanceRuleConditionKind::Occupied:
                    {
                        StringBuilder<constant::buffer::kMax256> textPart;
                        textPart.appendFormat( "occupied(%#)", outCondition._target.c_str() );
                        text += textPart.c_str();
                    }
                    break;
                    case AppearanceRuleConditionKind::BodyShape:
                    case AppearanceRuleConditionKind::BodyType:
                    {
                        text += outCondition._kind == AppearanceRuleConditionKind::BodyShape ? "bodyShape(" : "bodyType(";
                        for ( size_t nameIndex = 0; nameIndex < outCondition._listName.size(); ++nameIndex )
                        {
                            text += nameIndex == 0 ? "" : ", ";
                            text += outCondition._listName[nameIndex].c_str();
                        }
                        text += ")";
                        break;
                    }
                }
                if ( outCondition._kind == AppearanceRuleConditionKind::Occupied && outCondition._target.empty() )
                    return false; // 빈 슬롯 이름은 위에서 알렸다
                if ( ConditionExpr::compile( text, sourceName, outCondition._expr ) == false )
                {
                    report.addErrorWithID( "Appearance.RuleConditionInvalid", "%#: rule '%#' <When> could not be read", sourceName, ruleID.c_str() );
                    return false;
                }
                return true;
            }

            [[nodiscard]] static bool readAction( const XMLNode& node, AppearanceRuleAction& outAction, AppearanceLoadReport& report, string_view sourceName, const hashed_string& ruleID )
            {
                const utf8*                pName = node.getName();
                AppearanceRuleActionParams params;
                if ( readActionProperties( node, params, report, sourceName ) == false )
                {
                    report.addErrorWithID( "Appearance.UnknownElement", "%#: rule '%#' has unknown element <%#>", sourceName, ruleID.c_str(), pName );
                    return false;
                }
                outAction._target  = params._target; // 동작 종류마다 필수 여부가 달라 아래에서 확인한다
                outAction._part    = params._part;
                outAction._section = params._section; // SwapMaterial · MaterialValue 만 받는다(kArrActionField)
                if ( StringUtil::equals( pName, "Hide" ) )
                {
                    const bool  bItemTag  = node.findAttribute( "itemTag" ) != nullptr;
                    const bool  bRegion   = node.findAttribute( "region" ) != nullptr;
                    const int32 kindCount = ( outAction._target.empty() ? 0 : 1 ) + ( bItemTag ? 1 : 0 ) + ( bRegion ? 1 : 0 );
                    if ( kindCount != 1 )
                    {
                        report.addErrorWithID( "Appearance.RuleHideShape", "%#: rule '%#' <Hide> needs exactly one of target / itemTag / region", sourceName, ruleID.c_str() );
                        return false;
                    }
                    outAction._kind = AppearanceRuleActionKind::HideTarget;
                    if ( outAction._part.empty() == false && outAction._target.empty() )
                    {
                        report.addErrorWithID( "Appearance.RuleHidePartNoTarget", "%#: rule '%#' <Hide part> needs a target (the owner whose part is hidden)", sourceName, ruleID.c_str() );
                        return false;
                    }
                    if ( bItemTag )
                    {
                        outAction._kind = AppearanceRuleActionKind::HideItemTag;
                        outAction._tag  = TagID::request( params._itemTag );
                    }
                    else if ( bRegion )
                    {
                        outAction._kind = AppearanceRuleActionKind::HideRegion;
                        outAction._name = params._region;
                        return outAction._name.empty() == false;
                    }
                    return true;
                }
                if ( StringUtil::equals( pName, "Variant" ) )
                {
                    outAction._kind = AppearanceRuleActionKind::ChooseVariant;
                    outAction._name = params._name;
                    return outAction._name.empty() == false;
                }
                if ( StringUtil::equals( pName, "SwapMesh" ) )
                {
                    outAction._kind  = AppearanceRuleActionKind::SwapMesh;
                    outAction._value = params._mesh;
                    return outAction._value.empty() == false && outAction._target.empty() == false;
                }
                if ( StringUtil::equals( pName, "SwapMaterial" ) )
                {
                    outAction._kind  = AppearanceRuleActionKind::SwapMaterial;
                    outAction._value = params._material;
                    return outAction._value.empty() == false && outAction._target.empty() == false;
                }
                if ( StringUtil::equals( pName, "Morph" ) )
                {
                    outAction._kind   = AppearanceRuleActionKind::ApplyMorph;
                    outAction._name   = params._name;
                    outAction._weight = params._weight;
                    return outAction._name.empty() == false;
                }
                if ( StringUtil::equals( pName, "MaterialValue" ) )
                {
                    // 주인이 비면 몸이다(`Morph` 와 같다). 값은 스칼라 · 벡터 네 모양이다 — 빠지면 읽기가 보고서에 남긴다.
                    outAction._kind = AppearanceRuleActionKind::MaterialValue;
                    outAction._name = params._name;
                    AppearanceXMLUtil::readRequiredShaderValue( node, "value", outAction._materialValue, report, sourceName );
                    return outAction._name.empty() == false;
                }
                // OverrideSocket(남은 하나 — readActionProperties 가 모르는 원소를 걸렀다)
                outAction._kind = AppearanceRuleActionKind::OverrideSocket;
                outAction._name = params._name;
                AppearanceXMLUtil::readPlacement( node, "parent", outAction._placement, report, sourceName );
                return outAction._name.empty() == false && outAction._placement.isEmpty() == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AppearanceRuleCondition::isSameQuestion( const AppearanceRuleCondition& other ) const
    {
        return _kind == other._kind && _target == other._target && _tag == other._tag && _listName == other._listName;
    }

    bool AppearanceRuleAction::isHide() const
    {
        return _kind == AppearanceRuleActionKind::HideTarget || _kind == AppearanceRuleActionKind::HideItemTag || _kind == AppearanceRuleActionKind::HideRegion;
    }

    bool AppearanceRuleAction::hasSameTarget( const AppearanceRuleAction& other ) const
    {
        if ( _kind != other._kind || _target != other._target )
            return false;
        switch ( _kind )
        {
            case AppearanceRuleActionKind::SwapMesh:
                return _part == other._part;
            case AppearanceRuleActionKind::SwapMaterial:
                return _part == other._part && _section == other._section; // 구간 하나를 바꾸는 것과 부품 전체를 바꾸는 것은 겹쳐 건다(넓은 것 먼저)
            case AppearanceRuleActionKind::MaterialValue:
                return _part == other._part && _section == other._section && _name == other._name;
            case AppearanceRuleActionKind::ApplyMorph:
            case AppearanceRuleActionKind::OverrideSocket:
                return _name == other._name;
            default:
                return true;
        }
    }

    bool AppearanceRuleAction::hasSameValue( const AppearanceRuleAction& other ) const
    {
        return _name == other._name && _value == other._value && _weight == other._weight && _placement == other._placement && _materialValue == other._materialValue;
    }

    bool AppearanceRuleCatalog::loadFromXML( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXMLUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XMLNode ruleNode = root.findChild(); ruleNode; ruleNode = ruleNode.findNextSibling() )
        {
            if ( StringUtil::equals( ruleNode.getName(), "Rule" ) == false )
            {
                AppearanceXMLUtil::reportUnknownChild( root, ruleNode, report, sourceName );
                continue;
            }
            AppearanceRuleDef rule;
            (void)AppearanceXMLUtil::readProperties( ruleNode, rule, report, sourceName );
            if ( rule._id.empty() )
                continue; // 빠진 id 는 위 읽기가 보고서에 남겼다
            if ( findRule( rule._id ) != nullptr )
            {
                report.addErrorWithID( "Appearance.IDDuplicate", "%#: <Rule> has a duplicate id '%#'", sourceName, rule._id.c_str() );
                continue;
            }
            for ( XMLNode child = ruleNode.findChild(); child; child = child.findNextSibling() )
            {
                if ( StringUtil::equals( child.getName(), "When" ) )
                {
                    AppearanceRuleCondition condition;
                    if ( AppearanceRuleInternal::readCondition( child, condition, report, sourceName, rule._id ) )
                        rule._listCondition.push_back( condition );
                    continue;
                }
                AppearanceRuleAction action;
                const size_t         errorCountBeforeAction = report.getErrors().size();
                if ( AppearanceRuleInternal::readAction( child, action, report, sourceName, rule._id ) )
                    rule._listAction.push_back( action );
                else if ( report.getErrors().size() == errorCountBeforeAction )
                    report.addErrorWithID( "Appearance.RuleActionAttributeMissing", "%#: rule '%#' <%#> is missing a required attribute", sourceName, rule._id.c_str(), child.getName() );
            }
            if ( rule._listAction.empty() )
                report.addErrorWithID( "Appearance.RuleNoAction", "%#: rule '%#' has no action", sourceName, rule._id.c_str() );
            _listRule.push_back( rule );
        }
        reportConflicts( report, sourceName );
        return report.getErrors().size() == errorCountBefore;
    }

    const AppearanceRuleDef* AppearanceRuleCatalog::findRule( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&AppearanceRuleDef::_id>( _listRule, id );
    }

    bool AppearanceRuleCatalog::areExclusive( const AppearanceRuleDef& lhs, const AppearanceRuleDef& rhs )
    {
        for ( const AppearanceRuleCondition& left : lhs._listCondition )
        {
            for ( const AppearanceRuleCondition& right : rhs._listCondition )
            {
                if ( left.isSameQuestion( right ) && left._bNegate != right._bNegate )
                    return true;
            }
        }
        return false;
    }

    void AppearanceRuleCatalog::reportConflicts( AppearanceLoadReport& report, string_view sourceName ) const
    {
        for ( size_t lhsIndex = 0; lhsIndex < _listRule.size(); ++lhsIndex )
        {
            for ( size_t rhsIndex = lhsIndex + 1; rhsIndex < _listRule.size(); ++rhsIndex )
            {
                const AppearanceRuleDef& lhs = _listRule[lhsIndex];
                const AppearanceRuleDef& rhs = _listRule[rhsIndex];
                if ( lhs._priority != rhs._priority || areExclusive( lhs, rhs ) )
                    continue;
                for ( const AppearanceRuleAction& left : lhs._listAction )
                {
                    for ( const AppearanceRuleAction& right : rhs._listAction )
                    {
                        const bool bConflict = left.isHide() == false && left.hasSameTarget( right ) && left.hasSameValue( right ) == false;
                        if ( bConflict )
                            report.addErrorWithID( "Appearance.RuleConflict", "%#: rules '%#' and '%#' both %# '%#' differently at priority %# - give one a higher priority", sourceName, lhs._id.c_str(), rhs._id.c_str(),
                                                   getEnumName( left._kind ), left._target.empty() ? left._name.c_str() : left._target.c_str(), lhs._priority );
                    }
                }
            }
        }
    }
} // namespace sw
