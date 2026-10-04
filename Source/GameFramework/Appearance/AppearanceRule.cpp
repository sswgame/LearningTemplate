#include "pch.h"

#include "GameFramework/Appearance/AppearanceRule.h"

#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Appearance/AppearanceXmlUtil.h"

namespace sw
{
    namespace
    {
        struct AppearanceRuleInternal
        {
            static constexpr const utf8* kArrRuleAttribute[]     = { "id", "priority" };
            static constexpr const utf8* kArrWhenAttribute[]     = { "target", "tag", "characterTag", "occupied", "bodyShape", "bodyType", "not" };
            static constexpr const utf8* kArrHideAttribute[]     = { "target", "itemTag", "region" };
            static constexpr const utf8* kArrVariantAttribute[]  = { "target", "name" };
            static constexpr const utf8* kArrMeshAttribute[]     = { "target", "part", "mesh" };
            static constexpr const utf8* kArrMaterialAttribute[] = { "target", "part", "material" };
            static constexpr const utf8* kArrMorphAttribute[]    = { "target", "name", "weight" };
            static constexpr const utf8* kArrSocketAttribute[]   = { "name", "parent", "offset", "rotation" };

            [[nodiscard]] static bool readCondition( const XmlNode& node, AppearanceRuleCondition& outCondition, AppearanceLoadReport& report, string_view sourceName, const hashed_string& ruleId )
            {
                (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrWhenAttribute, report, sourceName );
                outCondition._bNegate      = node.getAttributeBool( "not", false ) ? SW_TRUE : SW_FALSE;
                outCondition._target       = AppearanceXmlUtil::readName( node, "target" );
                const utf8* pTag           = node.findAttribute( "tag" );
                const utf8* pCharacterTag  = node.findAttribute( "characterTag" );
                const utf8* pOccupied      = node.findAttribute( "occupied" );
                const utf8* pBodyShape     = node.findAttribute( "bodyShape" );
                const utf8* pBodyType      = node.findAttribute( "bodyType" );
                const int32 predicateCount = ( pTag != nullptr ? 1 : 0 ) + ( pCharacterTag != nullptr ? 1 : 0 ) + ( pOccupied != nullptr ? 1 : 0 ) + ( pBodyShape != nullptr ? 1 : 0 ) + ( pBodyType != nullptr ? 1 : 0 );
                const bool  bTargetUsed    = pTag != nullptr;
                if ( predicateCount != 1 || ( outCondition._target.empty() == false && bTargetUsed == false ) )
                {
                    report.addError( "%#: rule '%#' <When> needs exactly one of tag / characterTag / occupied / bodyShape / bodyType (target only with tag)", sourceName, ruleId.c_str() );
                    return false;
                }
                if ( pTag != nullptr )
                {
                    outCondition._kind = outCondition._target.empty() ? AppearanceRuleConditionKind::AnyTag : AppearanceRuleConditionKind::TargetTag;
                    outCondition._tag  = TagID::request( pTag );
                }
                else if ( pCharacterTag != nullptr )
                {
                    outCondition._kind = AppearanceRuleConditionKind::CharacterTag;
                    outCondition._tag  = TagID::request( pCharacterTag );
                }
                else if ( pOccupied != nullptr )
                {
                    outCondition._kind   = AppearanceRuleConditionKind::Occupied;
                    outCondition._target = AppearanceXmlUtil::readName( node, "occupied" );
                }
                else
                {
                    outCondition._kind = pBodyShape != nullptr ? AppearanceRuleConditionKind::BodyShape : AppearanceRuleConditionKind::BodyType;
                    AppearanceXmlUtil::readNameList( node, pBodyShape != nullptr ? "bodyShape" : "bodyType", outCondition._listName );
                }
                return true;
            }

            [[nodiscard]] static bool readAction( const XmlNode& node, AppearanceRuleAction& outAction, AppearanceLoadReport& report, string_view sourceName, const hashed_string& ruleId )
            {
                const utf8* pName = node.getName();
                outAction._target = AppearanceXmlUtil::readName( node, "target" );
                outAction._part   = AppearanceXmlUtil::readName( node, "part" );
                if ( StringUtil::equals( pName, "Hide", true ) )
                {
                    (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrHideAttribute, report, sourceName );
                    const utf8* pItemTag  = node.findAttribute( "itemTag" );
                    const utf8* pRegion   = node.findAttribute( "region" );
                    const int32 kindCount = ( outAction._target.empty() ? 0 : 1 ) + ( pItemTag != nullptr ? 1 : 0 ) + ( pRegion != nullptr ? 1 : 0 );
                    if ( kindCount != 1 )
                    {
                        report.addError( "%#: rule '%#' <Hide> needs exactly one of target / itemTag / region", sourceName, ruleId.c_str() );
                        return false;
                    }
                    outAction._kind = AppearanceRuleActionKind::HideTarget;
                    if ( pItemTag != nullptr )
                    {
                        outAction._kind = AppearanceRuleActionKind::HideItemTag;
                        outAction._tag  = TagID::request( pItemTag );
                    }
                    else if ( pRegion != nullptr )
                    {
                        outAction._kind = AppearanceRuleActionKind::HideRegion;
                        outAction._name = AppearanceXmlUtil::readName( node, "region" );
                    }
                    return true;
                }
                if ( StringUtil::equals( pName, "Variant", true ) )
                {
                    (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrVariantAttribute, report, sourceName );
                    outAction._kind = AppearanceRuleActionKind::ChooseVariant;
                    outAction._name = AppearanceXmlUtil::readName( node, "name" );
                    return outAction._name.empty() == false;
                }
                if ( StringUtil::equals( pName, "SwapMesh", true ) )
                {
                    (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrMeshAttribute, report, sourceName );
                    outAction._kind  = AppearanceRuleActionKind::SwapMesh;
                    outAction._value = AppearanceXmlUtil::readName( node, "mesh" );
                    return outAction._value.empty() == false && outAction._target.empty() == false;
                }
                if ( StringUtil::equals( pName, "SwapMaterial", true ) )
                {
                    (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrMaterialAttribute, report, sourceName );
                    outAction._kind  = AppearanceRuleActionKind::SwapMaterial;
                    outAction._value = AppearanceXmlUtil::readName( node, "material" );
                    return outAction._value.empty() == false && outAction._target.empty() == false;
                }
                if ( StringUtil::equals( pName, "Morph", true ) )
                {
                    (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrMorphAttribute, report, sourceName );
                    outAction._kind   = AppearanceRuleActionKind::ApplyMorph;
                    outAction._name   = AppearanceXmlUtil::readName( node, "name" );
                    outAction._weight = node.getAttributeFloat( "weight", 1.0f );
                    return outAction._name.empty() == false;
                }
                if ( StringUtil::equals( pName, "OverrideSocket", true ) )
                {
                    (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrSocketAttribute, report, sourceName );
                    outAction._kind = AppearanceRuleActionKind::OverrideSocket;
                    outAction._name = AppearanceXmlUtil::readName( node, "name" );
                    AppearanceXmlUtil::readPlacement( node, "parent", outAction._placement );
                    return outAction._name.empty() == false && outAction._placement.isEmpty() == false;
                }
                report.addError( "%#: rule '%#' has unknown element <%#>", sourceName, ruleId.c_str(), pName );
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( AppearanceRuleActionKind kind )
    {
        switch ( kind )
        {
            case AppearanceRuleActionKind::HideTarget:
                return "HideTarget";
            case AppearanceRuleActionKind::HideItemTag:
                return "HideItemTag";
            case AppearanceRuleActionKind::HideRegion:
                return "HideRegion";
            case AppearanceRuleActionKind::ChooseVariant:
                return "ChooseVariant";
            case AppearanceRuleActionKind::SwapMesh:
                return "SwapMesh";
            case AppearanceRuleActionKind::SwapMaterial:
                return "SwapMaterial";
            case AppearanceRuleActionKind::ApplyMorph:
                return "ApplyMorph";
            case AppearanceRuleActionKind::OverrideSocket:
                return "OverrideSocket";
        }
        return "Unknown";
    }

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
            case AppearanceRuleActionKind::SwapMaterial:
                return _part == other._part;
            case AppearanceRuleActionKind::ApplyMorph:
            case AppearanceRuleActionKind::OverrideSocket:
                return _name == other._name;
            default:
                return true;
        }
    }

    bool AppearanceRuleAction::hasSameValue( const AppearanceRuleAction& other ) const
    {
        return _name == other._name && _value == other._value && _weight == other._weight && _placement == other._placement;
    }

    bool AppearanceRuleTable::loadFromNode( const XmlNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXmlUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XmlNode ruleNode = root.findChild(); ruleNode; ruleNode = ruleNode.findNextSibling() )
        {
            if ( StringUtil::equals( ruleNode.getName(), "Rule", true ) == false )
            {
                AppearanceXmlUtil::reportUnknownChild( root, ruleNode, report, sourceName );
                continue;
            }
            (void)AppearanceXmlUtil::reportUnknownAttributes( ruleNode, AppearanceRuleInternal::kArrRuleAttribute, report, sourceName );
            AppearanceRuleDef rule;
            rule._id       = AppearanceXmlUtil::readName( ruleNode, "id" );
            rule._priority = ruleNode.getAttributeInt( "priority", 0 );
            if ( rule._id.empty() || findRule( rule._id ) != nullptr )
            {
                report.addError( "%#: <Rule> without an id or with a duplicate id '%#'", sourceName, rule._id.c_str() );
                continue;
            }
            for ( XmlNode child = ruleNode.findChild(); child; child = child.findNextSibling() )
            {
                if ( StringUtil::equals( child.getName(), "When", true ) )
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
                    report.addError( "%#: rule '%#' <%#> is missing a required attribute", sourceName, rule._id.c_str(), child.getName() );
            }
            if ( rule._listAction.empty() )
                report.addError( "%#: rule '%#' has no action", sourceName, rule._id.c_str() );
            _listRule.push_back( rule );
        }
        reportConflicts( report, sourceName );
        return report.getErrors().size() == errorCountBefore;
    }

    const AppearanceRuleDef* AppearanceRuleTable::findRule( const hashed_string& id ) const
    {
        for ( const AppearanceRuleDef& rule : _listRule )
        {
            if ( rule._id == id )
                return &rule;
        }
        return nullptr;
    }

    bool AppearanceRuleTable::areExclusive( const AppearanceRuleDef& lhs, const AppearanceRuleDef& rhs )
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

    void AppearanceRuleTable::reportConflicts( AppearanceLoadReport& report, string_view sourceName ) const
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
                            report.addError( "%#: rules '%#' and '%#' both %# '%#' differently at priority %# - give one a higher priority", sourceName, lhs._id.c_str(), rhs._id.c_str(),
                                             toString( left._kind ), left._target.empty() ? left._name.c_str() : left._target.c_str(), lhs._priority );
                    }
                }
            }
        }
    }
} // namespace sw
