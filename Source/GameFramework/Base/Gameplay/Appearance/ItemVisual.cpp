#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/ItemVisual.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceXMLUtil.h"

namespace sw
{
    namespace
    {
        struct ItemVisualInternal
        {
            static constexpr const utf8* kArrVisualAttribute[]          = { "id", "occupancy", "customization", "tags", "defaultState" };
            static constexpr const utf8* kArrPartAttribute[]            = { "name", "kind", "mesh", "prefab", "sprite", "material", "skeleton", "sockets", "rig",
                                                                            "fit", "fitTables", "bodyShapes", "socket", "offset", "rotation", "deforms", "layer", "breakable", "breakStage", "impulse" };
            static constexpr const utf8* kArrVariantAttribute[]         = { "name", "mesh", "prefab", "sprite", "material" };
            static constexpr const utf8* kArrMaterialVariantAttribute[] = { "name", "material" };
            static constexpr const utf8* kArrMorphAttribute[]           = { "name", "weight" };
            static constexpr const utf8* kArrMaterialAttribute[]        = { "name", "value" };
            static constexpr const utf8* kArrRegionAttribute[]          = { "name" };
            static constexpr const utf8* kArrStateAttribute[]           = { "name" };
            static constexpr const utf8* kArrPlaceAttribute[]           = { "part", "socket", "offset", "rotation" };
            static constexpr const utf8* kArrHidePartAttribute[]        = { "part" };
            static constexpr const utf8* kArrStageAttribute[]           = { "name", "threshold" };
            static constexpr const utf8* kArrStageVariantAttribute[]    = { "part", "name" };
            static constexpr const utf8* kArrStageMaterialAttribute[]   = { "part", "name", "value" };

            [[nodiscard]] static bool parseKind( string_view text, AppearancePartKind& outKind )
            {
                constexpr AppearancePartKind kArrKind[] = { AppearancePartKind::Skinned, AppearancePartKind::SocketPrefab, AppearancePartKind::BodyModification, AppearancePartKind::Sprite };
                for ( const AppearancePartKind kind : kArrKind )
                {
                    if ( StringUtil::equals( text, string_view( getEnumName( kind ) ) ) )
                    {
                        outKind = kind;
                        return true;
                    }
                }
                return false;
            }

            /** @brief 부품 종류가 쓰는 에셋 속성 이름입니다. 몸 수정에는 없습니다. */
            static const utf8* getAssetAttribute( AppearancePartKind kind )
            {
                switch ( kind )
                {
                    case AppearancePartKind::Skinned:
                        return "mesh";
                    case AppearancePartKind::SocketPrefab:
                        return "prefab";
                    case AppearancePartKind::Sprite:
                        return "sprite";
                    case AppearancePartKind::BodyModification:
                        return nullptr;
                }
                return nullptr;
            }

            /** @brief 종류에 맞지 않는 에셋 속성이 있으면 오류로 처리합니다. */
            static void reportMisplacedAssetAttributes( const XMLNode& node, AppearancePartKind kind, AppearanceLoadReport& report, string_view sourceName, const hashed_string& visualID )
            {
                constexpr const utf8* kArrAssetAttribute[] = { "mesh", "prefab", "sprite" };
                const utf8*           pExpected            = getAssetAttribute( kind );
                for ( const utf8* pAttribute : kArrAssetAttribute )
                {
                    if ( node.findAttribute( pAttribute ) != nullptr && ( pExpected == nullptr || StringUtil::equals( pAttribute, pExpected, true ) == false ) )
                        report.addErrorWithID( "Appearance.PartAttributeNotAllowed", "%#: visual '%#' <%#> of kind %# cannot have '%#'", sourceName, visualID.c_str(), node.getName(), getEnumName( kind ), pAttribute );
                }
            }

            static bool hasPart( const ItemVisualDef& visual, const hashed_string& part, AppearanceLoadReport& report, string_view sourceName, const utf8* pWhere )
            {
                if ( visual.findPart( part ) != nullptr )
                    return true;
                report.addErrorWithID( "Appearance.PartUnknown", "%#: visual '%#' %# names unknown part '%#'", sourceName, visual._id.c_str(), pWhere, part.c_str() );
                return false;
            }

            static void readPartChild( const XMLNode& child, const XMLNode& partNode, AppearancePartDef& inoutPart, AppearanceLoadReport& report, string_view sourceName, const hashed_string& visualID )
            {
                const utf8* pName         = child.getName();
                const bool  bModification = inoutPart._kind == AppearancePartKind::BodyModification;
                if ( StringUtil::equals( pName, "Variant", true ) && bModification == false )
                {
                    (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrVariantAttribute, report, sourceName );
                    reportMisplacedAssetAttributes( child, inoutPart._kind, report, sourceName, visualID );
                    AppearancePartVariantDef variant;
                    variant._name     = AppearanceXMLUtil::readRequiredName( child, "name", report, sourceName );
                    variant._asset    = XMLReadUtil::readOptionalName( child, getAssetAttribute( inoutPart._kind ) );
                    variant._material = XMLReadUtil::readOptionalName( child, "material" );
                    if ( variant._name.empty() || inoutPart.findVariant( variant._name ) != nullptr )
                        report.addErrorWithID( "Appearance.VariantNameInvalid", "%#: visual '%#' part '%#' has a variant without a name or a duplicate '%#'", sourceName, visualID.c_str(), inoutPart._name.c_str(), variant._name.c_str() );
                    else
                        inoutPart._listVariant.push_back( variant );
                }
                else if ( StringUtil::equals( pName, "MaterialVariant", true ) && bModification == false )
                {
                    (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrMaterialVariantAttribute, report, sourceName );
                    AppearancePartVariantDef variant;
                    variant._name              = AppearanceXMLUtil::readRequiredName( child, "name", report, sourceName );
                    variant._material          = AppearanceXMLUtil::readRequiredName( child, "material", report, sourceName );
                    const bool bInvalidVariant = variant._name.empty() || variant._material.empty() || inoutPart.findMaterialVariant( variant._name ) != nullptr;
                    if ( bInvalidVariant )
                        report.addErrorWithID( "Appearance.VariantNameInvalid", "%#: visual '%#' part '%#' has a bad or duplicate material variant '%#'", sourceName, visualID.c_str(), inoutPart._name.c_str(), variant._name.c_str() );
                    else
                        inoutPart._listMaterialVariant.push_back( variant );
                }
                else if ( StringUtil::equals( pName, "Morph", true ) && bModification )
                {
                    (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrMorphAttribute, report, sourceName );
                    AppearanceMorphDef morph;
                    morph._name = AppearanceXMLUtil::readRequiredName( child, "name", report, sourceName );
                    AppearanceXMLUtil::readOptionalFloat( child, "weight", -MathUtil::kMaxFloat, MathUtil::kMaxFloat, morph._weight, report, sourceName );
                    inoutPart._listMorph.push_back( morph );
                }
                else if ( StringUtil::equals( pName, "Material", true ) && bModification )
                {
                    (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrMaterialAttribute, report, sourceName );
                    AppearanceMaterialValueDef value;
                    value._name = AppearanceXMLUtil::readRequiredName( child, "name", report, sourceName );
                    AppearanceXMLUtil::readRequiredShaderValue( child, "value", value._value, report, sourceName );
                    inoutPart._listMaterialValue.push_back( value );
                }
                else if ( StringUtil::equals( pName, "HideRegion", true ) && bModification )
                {
                    (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrRegionAttribute, report, sourceName );
                    const hashed_string region = AppearanceXMLUtil::readRequiredName( child, "name", report, sourceName );
                    if ( region.empty() == false )
                        inoutPart._listHiddenRegion.push_back( region ); // 이름 없는 구역은 보고서에 오류로 남았다
                }
                else
                {
                    AppearanceXMLUtil::reportUnknownChild( partNode, child, report, sourceName );
                }
            }

            static void readPart( const XMLNode& node, ItemVisualDef& inoutVisual, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrPartAttribute, report, sourceName );
                AppearancePartDef part;
                part._name = AppearanceXMLUtil::readRequiredName( node, "name", report, sourceName );
                if ( parseKind( node.getAttributeText( "kind" ), part._kind ) == false )
                {
                    report.addErrorWithID( "Appearance.PartKindUnknown", "%#: visual '%#' part '%#' has unknown kind '%#'", sourceName, inoutVisual._id.c_str(), part._name.c_str(), node.getAttributeText( "kind" ) );
                    return;
                }
                if ( part._name.empty() || inoutVisual.findPart( part._name ) != nullptr )
                {
                    report.addErrorWithID( "Appearance.PartNameInvalid", "%#: visual '%#' has a part without a name or a duplicate '%#'", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                    return;
                }
                reportMisplacedAssetAttributes( node, part._kind, report, sourceName, inoutVisual._id );
                const utf8* pAssetAttribute = getAssetAttribute( part._kind );
                part._asset                 = pAssetAttribute != nullptr ? XMLReadUtil::readOptionalName( node, pAssetAttribute ) : hashed_string{};
                part._material              = XMLReadUtil::readOptionalName( node, "material" );
                part._skeleton              = XMLReadUtil::readOptionalName( node, "skeleton" );
                part._socketSet             = XMLReadUtil::readOptionalName( node, "sockets" );
                part._rig                   = XMLReadUtil::readOptionalName( node, "rig" );
                part._breakStage            = XMLReadUtil::readOptionalName( node, "breakStage" );
                // 자기 리그는 자기 뼈를 움직인다 — 스킨드 부품이고 몸과 다른 자기 스켈레톤을 적어야 한다(몸 스켈레톤을 빌리면 몸 리더 포즈와 다툰다).
                const bool bRigWithoutOwnSkeleton = part._rig.empty() == false && ( part._kind != AppearancePartKind::Skinned || part._skeleton.empty() );
                if ( bRigWithoutOwnSkeleton )
                    report.addErrorWithID( "Appearance.PartRigNeedsSkeleton", "%#: visual '%#' part '%#' has a 'rig' but is not a Skinned part with its own 'skeleton'", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                part._fit                  = XMLReadUtil::readOptionalName( node, "fit" );
                part._fitTables            = XMLReadUtil::readOptionalName( node, "fitTables" );
                const bool bFitOnRigidPart = ( part._fit.empty() == false || part._fitTables.empty() == false ) && part._kind != AppearancePartKind::Skinned;
                if ( bFitOnRigidPart )
                    report.addErrorWithID( "Appearance.PartFitNotAllowed", "%#: visual '%#' part '%#' of kind %# cannot have 'fit' or 'fitTables' (only Skinned parts are fitted)", sourceName, inoutVisual._id.c_str(),
                                           part._name.c_str(), getEnumName( part._kind ) );
                part._bodyShapes = XMLReadUtil::readOptionalName( node, "bodyShapes" );
                if ( part._bodyShapes.empty() == false && part._kind != AppearancePartKind::Skinned )
                    report.addErrorWithID( "Appearance.PartFitNotAllowed", "%#: visual '%#' part '%#' of kind %# cannot have 'bodyShapes' (only the Skinned body has body shape axes)", sourceName, inoutVisual._id.c_str(),
                                           part._name.c_str(), getEnumName( part._kind ) );
                // 적지 않으면 스킨 파츠만 변형하고, 부서지는 단계를 적은 파츠만 떨어진다(이 파츠의 데이터에서 온 값).
                part._bDeforms   = part._kind == AppearancePartKind::Skinned ? SW_TRUE : SW_FALSE;
                part._bBreakable = part._breakStage.empty() == false ? SW_TRUE : SW_FALSE;
                AppearanceXMLUtil::readOptionalInt( node, "layer", MathUtil::kMinInt32, MathUtil::kMaxInt32, part._layer, report, sourceName );
                if ( XMLReadUtil::readOptionalFlag( node, "deforms", part._bDeforms ) == false || XMLReadUtil::readOptionalFlag( node, "breakable", part._bBreakable ) == false )
                    report.addErrorWithID( "Appearance.PartDeformsInvalid", "%#: visual '%#' part '%#' has an invalid deforms / breakable", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                AppearanceXMLUtil::readOptionalFloat3( node, "impulse", part._breakImpulse, report, sourceName );
                AppearanceXMLUtil::readPlacement( node, "socket", part._placement, report, sourceName );
                if ( pAssetAttribute != nullptr && part._asset.empty() )
                    report.addErrorWithID( "Appearance.PartAssetMissing", "%#: visual '%#' part '%#' needs '%#'", sourceName, inoutVisual._id.c_str(), part._name.c_str(), pAssetAttribute );
                if ( part._kind == AppearancePartKind::SocketPrefab && part._placement.isEmpty() )
                    report.addErrorWithID( "Appearance.PartSocketMissing", "%#: visual '%#' part '%#' needs a socket", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                if ( part._kind == AppearancePartKind::BodyModification && ( part._placement.isEmpty() == false || part._bBreakable == SW_TRUE ) )
                    report.addErrorWithID( "Appearance.PartBodyModificationInvalid", "%#: visual '%#' body modification '%#' cannot have a socket or break off", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    readPartChild( child, node, part, report, sourceName, inoutVisual._id );
                }
                inoutVisual._listPart.push_back( part );
            }

            static void readState( const XMLNode& node, ItemVisualDef& inoutVisual, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrStateAttribute, report, sourceName );
                AppearanceStateDef state;
                state._name = AppearanceXMLUtil::readRequiredName( node, "name", report, sourceName );
                if ( state._name.empty() || inoutVisual.findState( state._name ) != nullptr )
                {
                    report.addErrorWithID( "Appearance.StateNameInvalid", "%#: visual '%#' has a state without a name or a duplicate '%#'", sourceName, inoutVisual._id.c_str(), state._name.c_str() );
                    return;
                }
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Place", true ) )
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrPlaceAttribute, report, sourceName );
                        AppearanceStateDef::Placement placement;
                        placement._part = AppearanceXMLUtil::readRequiredName( child, "part", report, sourceName );
                        AppearanceXMLUtil::readPlacement( child, "socket", placement._placement, report, sourceName );
                        if ( hasPart( inoutVisual, placement._part, report, sourceName, "<Place>" ) )
                            state._listPlacement.push_back( placement );
                    }
                    else if ( StringUtil::equals( child.getName(), "HidePart", true ) )
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrHidePartAttribute, report, sourceName );
                        const hashed_string part = AppearanceXMLUtil::readRequiredName( child, "part", report, sourceName );
                        if ( hasPart( inoutVisual, part, report, sourceName, "<HidePart>" ) )
                            state._listHiddenPart.push_back( part );
                    }
                    else
                    {
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                    }
                }
                inoutVisual._listState.push_back( state );
            }

            static void readDamageStage( const XMLNode& node, ItemVisualDef& inoutVisual, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrStageAttribute, report, sourceName );
                AppearanceDamageStageDef stage;
                stage._name                            = AppearanceXMLUtil::readRequiredName( node, "name", report, sourceName );
                const std::optional<float32> threshold = node.readRequiredAttribute<float32>( "threshold" );
                if ( threshold.has_value() == false )
                {
                    report.addErrorWithID( "Appearance.DamageStageThreshold", "%#: visual '%#' damage stage '%#' needs a threshold", sourceName, inoutVisual._id.c_str(), stage._name.c_str() );
                    return;
                }
                stage._threshold                  = *threshold;
                const float32 previous            = inoutVisual._listDamageStage.empty() ? 0.0f : inoutVisual._listDamageStage.back()._threshold;
                const bool    bOrdered            = previous < stage._threshold && stage._threshold <= 1.0f;
                const bool    bInvalidDamageStage = stage._name.empty() || inoutVisual.findDamageStage( stage._name ) != nullptr || bOrdered == false;
                if ( bInvalidDamageStage )
                {
                    report.addErrorWithID( "Appearance.DamageStageThreshold", "%#: visual '%#' damage stage '%#' needs a unique name and a threshold in (previous, 1]", sourceName, inoutVisual._id.c_str(), stage._name.c_str() );
                    return;
                }
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Variant", true ) )
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrStageVariantAttribute, report, sourceName );
                        AppearanceDamageStageDef::PartVariant entry;
                        entry._part                    = AppearanceXMLUtil::readRequiredName( child, "part", report, sourceName );
                        entry._variant                 = AppearanceXMLUtil::readRequiredName( child, "name", report, sourceName );
                        const AppearancePartDef* pPart = inoutVisual.findPart( entry._part );
                        if ( pPart == nullptr || pPart->findVariant( entry._variant ) == nullptr )
                            report.addErrorWithID( "Appearance.PartUnknown", "%#: visual '%#' damage stage '%#' names unknown part/variant '%#'/'%#'", sourceName, inoutVisual._id.c_str(), stage._name.c_str(), entry._part.c_str(),
                                                   entry._variant.c_str() );
                        else
                            stage._listVariant.push_back( entry );
                    }
                    else if ( StringUtil::equals( child.getName(), "Material", true ) )
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrStageMaterialAttribute, report, sourceName );
                        AppearanceMaterialValueDef value;
                        value._part = XMLReadUtil::readOptionalName( child, "part" );
                        value._name = AppearanceXMLUtil::readRequiredName( child, "name", report, sourceName );
                        AppearanceXMLUtil::readRequiredShaderValue( child, "value", value._value, report, sourceName );
                        if ( value._part.empty() || hasPart( inoutVisual, value._part, report, sourceName, "damage <Material>" ) )
                            stage._listMaterialValue.push_back( value );
                    }
                    else
                    {
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                    }
                }
                inoutVisual._listDamageStage.push_back( stage );
            }

            static void readVisual( const XMLNode& node, ItemVisualDef& outVisual, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrVisualAttribute, report, sourceName );
                outVisual._occupancy     = XMLReadUtil::readOptionalName( node, "occupancy" );
                outVisual._customization = XMLReadUtil::readOptionalName( node, "customization" );
                outVisual._defaultState  = XMLReadUtil::readOptionalName( node, "defaultState" );
                AppearanceXMLUtil::readTags( node, "tags", outVisual._tags );
                // 부품을 먼저 모두 읽는다. 상태와 피해 단계가 파일 안의 순서와 상관없이 부품을 가리킬 수 있게 하기 위해서다.
                for ( XMLNode child = node.findChild( "Part" ); child; child = child.findNextSibling( "Part" ) )
                {
                    readPart( child, outVisual, report, sourceName );
                }
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    const utf8* pName = child.getName();
                    if ( StringUtil::equals( pName, "Part", true ) )
                        continue;
                    if ( StringUtil::equals( pName, "State", true ) )
                        readState( child, outVisual, report, sourceName );
                    else if ( StringUtil::equals( pName, "DamageStage", true ) )
                        readDamageStage( child, outVisual, report, sourceName );
                    else
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                }
                if ( outVisual._defaultState.empty() == false && outVisual.findState( outVisual._defaultState ) == nullptr )
                    report.addErrorWithID( "Appearance.DefaultStateUnknown", "%#: visual '%#' default state '%#' is not one of its states", sourceName, outVisual._id.c_str(), outVisual._defaultState.c_str() );
                if ( outVisual._defaultState.empty() && outVisual._listState.empty() == false )
                    outVisual._defaultState = outVisual._listState.front()._name;
                for ( const AppearancePartDef& part : outVisual._listPart )
                {
                    if ( part._breakStage.empty() == false && outVisual.findDamageStage( part._breakStage ) == nullptr )
                        report.addErrorWithID( "Appearance.DamageStageUnknown", "%#: visual '%#' part '%#' breaks at unknown damage stage '%#'", sourceName, outVisual._id.c_str(), part._name.c_str(), part._breakStage.c_str() );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const AppearancePartVariantDef* AppearancePartDef::findVariant( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AppearancePartVariantDef::_name>( _listVariant, name );
    }

    const AppearancePartVariantDef* AppearancePartDef::findMaterialVariant( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AppearancePartVariantDef::_name>( _listMaterialVariant, name );
    }

    const AppearancePartDef* ItemVisualDef::findPart( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AppearancePartDef::_name>( _listPart, name );
    }

    const AppearanceStateDef* ItemVisualDef::findState( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AppearanceStateDef::_name>( _listState, name );
    }

    const AppearanceDamageStageDef* ItemVisualDef::findDamageStage( const hashed_string& name ) const
    {
        const int32 index = findDamageStageIndex( name );
        return index >= 0 ? &_listDamageStage[static_cast<size_t>( index )] : nullptr;
    }

    int32 ItemVisualDef::findDamageStageIndex( const hashed_string& name ) const
    {
        for ( size_t index = 0; index < _listDamageStage.size(); ++index )
        {
            if ( _listDamageStage[index]._name == name )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 ItemVisualDef::computeDamageStageIndex( float32 damage ) const
    {
        int32 stageIndex = -1;
        for ( size_t index = 0; index < _listDamageStage.size(); ++index )
        {
            if ( _listDamageStage[index]._threshold <= damage )
                stageIndex = static_cast<int32>( index );
        }
        return stageIndex;
    }

    bool ItemVisualCatalog::loadFromNode( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXMLUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "ItemVisual", true ) == false )
            {
                AppearanceXMLUtil::reportUnknownChild( root, node, report, sourceName );
                continue;
            }
            ItemVisualDef visual;
            visual._id = AppearanceXMLUtil::readRequiredName( node, "id", report, sourceName );
            if ( visual._id.empty() )
                continue; // 빠진 id 는 위 읽기가 보고서에 남겼다
            if ( findVisual( visual._id ) != nullptr )
            {
                report.addErrorWithID( "Appearance.IDDuplicate", "%#: <ItemVisual> has a duplicate id '%#'", sourceName, visual._id.c_str() );
                continue;
            }
            ItemVisualInternal::readVisual( node, visual, report, sourceName );
            _listVisual.push_back( visual );
        }
        return report.getErrors().size() == errorCountBefore;
    }

    const ItemVisualDef* ItemVisualCatalog::findVisual( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&ItemVisualDef::_id>( _listVisual, id );
    }
} // namespace sw
