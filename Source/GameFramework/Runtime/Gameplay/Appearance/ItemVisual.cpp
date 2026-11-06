#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/ItemVisual.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceXMLUtil.h"

namespace sw
{
    namespace
    {
        struct ItemVisualInternal
        {
            static constexpr const utf8* kArrRegionAttribute[]       = { "name" };
            static constexpr const utf8* kArrStateAttribute[]        = { "name" };
            static constexpr const utf8* kArrPlaceAttribute[]        = { "part", "socket", "offset", "rotation" };
            static constexpr const utf8* kArrHidePartAttribute[]     = { "part" };
            static constexpr const utf8* kArrStageVariantAttribute[] = { "part", "name" };

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

            /** @brief LOD 규칙(`lodDetail` · `lodDropLevel`)을 적었는지입니다 — 메시 · 머티리얼 변형은 구간이 아니라 받지 않는다. */
            static bool hasLODRule( const AppearancePartVariantDef& def ) { return def._lodDetail != 1.0f || def._lodDropLevel != 0; }

            static void readPartChild( const XMLNode& child, const XMLNode& partNode, AppearancePartDef& inoutPart, AppearanceLoadReport& report, string_view sourceName, const hashed_string& visualID )
            {
                const utf8* pName         = child.getName();
                const bool  bModification = inoutPart._kind == AppearancePartKind::BodyModification;
                if ( StringUtil::equals( pName, "Variant" ) && bModification == false )
                {
                    reportMisplacedAssetAttributes( child, inoutPart._kind, report, sourceName, visualID );
                    AppearancePartVariantDef variant;
                    (void)AppearanceXMLUtil::readFields( child, variant, report, sourceName, { "mesh", "prefab", "sprite" } ); // 에셋 속성은 부품 종류가 정한다
                    variant._asset = XMLReadUtil::readOptionalName( child, getAssetAttribute( inoutPart._kind ) );
                    if ( variant._skeleton.empty() == false && inoutPart._kind != AppearancePartKind::Skinned )
                        report.addErrorWithID( "Appearance.VariantSkeletonNotSkinned", "%#: visual '%#' part '%#' variant '%#' names a skeleton but the part is not Skinned", sourceName, visualID.c_str(), inoutPart._name.c_str(), variant._name.c_str() );
                    if ( hasLODRule( variant ) )
                        report.addErrorWithID( "Appearance.LODRuleNotAllowed", "%#: visual '%#' part '%#' variant '%#' cannot have lodDetail / lodDropLevel (only <Part> and <Section>)", sourceName, visualID.c_str(), inoutPart._name.c_str(), variant._name.c_str() );
                    if ( variant._name.empty() || inoutPart.findVariant( variant._name ) != nullptr )
                        report.addErrorWithID( "Appearance.VariantNameInvalid", "%#: visual '%#' part '%#' has a variant without a name or a duplicate '%#'", sourceName, visualID.c_str(), inoutPart._name.c_str(), variant._name.c_str() );
                    else
                        inoutPart._listVariant.push_back( variant );
                }
                else if ( StringUtil::equals( pName, "MaterialVariant" ) && bModification == false )
                {
                    AppearancePartVariantDef variant;
                    (void)AppearanceXMLUtil::readFields( child, variant, report, sourceName, {} );
                    const bool bInvalidVariant = variant._name.empty() || variant._material.empty() || variant._skeleton.empty() == false || hasLODRule( variant ) ||
                                                 inoutPart.findMaterialVariant( variant._name ) != nullptr;
                    if ( bInvalidVariant )
                        report.addErrorWithID( "Appearance.VariantNameInvalid", "%#: visual '%#' part '%#' has a bad or duplicate material variant '%#'", sourceName, visualID.c_str(), inoutPart._name.c_str(), variant._name.c_str() );
                    else
                        inoutPart._listMaterialVariant.push_back( variant );
                }
                else if ( StringUtil::equals( pName, "Section" ) && inoutPart._kind == AppearancePartKind::Skinned )
                {
                    AppearancePartVariantDef section;
                    (void)AppearanceXMLUtil::readFields( child, section, report, sourceName, {} );
                    const bool bDuplicateSection = VectorUtil::containsIf( inoutPart._listSection, [&]( const AppearancePartVariantDef& other )
                    { return other._name == section._name; } );
                    if ( section._name.empty() || section._material.empty() || section._skeleton.empty() == false || bDuplicateSection )
                        report.addErrorWithID( "Appearance.SectionInvalid", "%#: visual '%#' part '%#' has a section without a name or material, or a duplicate '%#'", sourceName, visualID.c_str(),
                                               inoutPart._name.c_str(), section._name.c_str() );
                    else
                        inoutPart._listSection.push_back( section );
                }
                else if ( StringUtil::equals( pName, "Morph" ) && bModification )
                {
                    AppearanceMorphDef morph;
                    (void)AppearanceXMLUtil::readFields( child, morph, report, sourceName, {} );
                    inoutPart._listMorph.push_back( morph );
                }
                else if ( StringUtil::equals( pName, "Material" ) && bModification )
                {
                    AppearanceMaterialValueDef value;
                    (void)AppearanceXMLUtil::readFields( child, value, report, sourceName, { "value" } ); // 값은 스칼라 · 벡터 네 모양이다(`readRequiredShaderValue`)
                    AppearanceXMLUtil::readRequiredShaderValue( child, "value", value._value, report, sourceName );
                    inoutPart._listMaterialValue.push_back( value );
                }
                else if ( StringUtil::equals( pName, "HideRegion" ) && bModification )
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
                AppearancePartDef part;
                // 에셋은 종류가 속성을 정하고, 배치 · 충격 · 깃발은 적었는지가 기본값을 가른다.
                if ( AppearanceXMLUtil::readFields( node, part, report, sourceName, { "mesh", "prefab", "sprite", "socket", "offset", "rotation", "deforms", "breakable", "impulse" } ) == false )
                    return;
                if ( part._name.empty() || inoutVisual.findPart( part._name ) != nullptr )
                {
                    report.addErrorWithID( "Appearance.PartNameInvalid", "%#: visual '%#' has a part without a name or a duplicate '%#'", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                    return;
                }
                reportMisplacedAssetAttributes( node, part._kind, report, sourceName, inoutVisual._id );
                const utf8* pAssetAttribute = getAssetAttribute( part._kind );
                part._asset                 = pAssetAttribute != nullptr ? XMLReadUtil::readOptionalName( node, pAssetAttribute ) : hashed_string{};
                // 자기 리그는 자기 뼈를 움직인다 — 스킨드 부품이고 몸과 다른 자기 스켈레톤을 적어야 한다(몸 스켈레톤을 빌리면 몸 리더 포즈와 다툰다).
                const bool bRigWithoutOwnSkeleton = part._rig.empty() == false && ( part._kind != AppearancePartKind::Skinned || part._skeleton.empty() );
                if ( bRigWithoutOwnSkeleton )
                    report.addErrorWithID( "Appearance.PartRigNeedsSkeleton", "%#: visual '%#' part '%#' has a 'rig' but is not a Skinned part with its own 'skeleton'", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                const bool bFitOnRigidPart = ( part._fit.empty() == false || part._fitTables.empty() == false ) && part._kind != AppearancePartKind::Skinned;
                if ( bFitOnRigidPart )
                    report.addErrorWithID( "Appearance.PartFitNotAllowed", "%#: visual '%#' part '%#' of kind %# cannot have 'fit' or 'fitTables' (only Skinned parts are fitted)", sourceName, inoutVisual._id.c_str(),
                                           part._name.c_str(), getEnumName( part._kind ) );
                if ( part._bodyShapes.empty() == false && part._kind != AppearancePartKind::Skinned )
                    report.addErrorWithID( "Appearance.PartFitNotAllowed", "%#: visual '%#' part '%#' of kind %# cannot have 'bodyShapes' (only the Skinned body has body shape axes)", sourceName, inoutVisual._id.c_str(),
                                           part._name.c_str(), getEnumName( part._kind ) );
                // 적지 않으면 스킨 파츠만 변형하고, 부서지는 단계를 적은 파츠만 떨어진다(이 파츠의 데이터에서 온 값).
                part._bDeforms   = part._kind == AppearancePartKind::Skinned ? SW_TRUE : SW_FALSE;
                part._bBreakable = part._breakStage.empty() == false ? SW_TRUE : SW_FALSE;
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
                const bool bLODRuleOnUnmerged = ( part._lodDetail != 1.0f || part._lodDropLevel != 0 ) && part._kind != AppearancePartKind::Skinned;
                if ( bLODRuleOnUnmerged )
                    report.addErrorWithID( "Appearance.LODRuleNotAllowed", "%#: visual '%#' part '%#' of kind %# cannot have lodDetail / lodDropLevel (only merged Skinned parts)", sourceName, inoutVisual._id.c_str(),
                                           part._name.c_str(), getEnumName( part._kind ) );
                if ( part._listSection.empty() == false && part._material.empty() == false )
                    report.addErrorWithID( "Appearance.SectionWithPartMaterial", "%#: visual '%#' part '%#' has both 'material' and <Section> - give each section its material", sourceName,
                                           inoutVisual._id.c_str(), part._name.c_str() );
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
                    if ( StringUtil::equals( child.getName(), "Place" ) )
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrPlaceAttribute, report, sourceName );
                        AppearanceStateDef::Placement placement;
                        placement._part = AppearanceXMLUtil::readRequiredName( child, "part", report, sourceName );
                        AppearanceXMLUtil::readPlacement( child, "socket", placement._placement, report, sourceName );
                        if ( hasPart( inoutVisual, placement._part, report, sourceName, "<Place>" ) )
                            state._listPlacement.push_back( placement );
                    }
                    else if ( StringUtil::equals( child.getName(), "HidePart" ) )
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
                AppearanceDamageStageDef stage;
                if ( AppearanceXMLUtil::readFields( node, stage, report, sourceName, {} ) == false )
                    return;
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
                    if ( StringUtil::equals( child.getName(), "Variant" ) )
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
                    else if ( StringUtil::equals( child.getName(), "Material" ) )
                    {
                        AppearanceMaterialValueDef value;
                        (void)AppearanceXMLUtil::readFields( child, value, report, sourceName, { "value" } );
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
                (void)AppearanceXMLUtil::readFields( node, outVisual, report, sourceName, { "id", "tags" } );
                AppearanceXMLUtil::readTags( node, "tags", outVisual._tags );
                // 부품을 먼저 모두 읽는다. 상태와 피해 단계가 파일 안의 순서와 상관없이 부품을 가리킬 수 있게 하기 위해서다.
                for ( XMLNode child = node.findChild( "Part" ); child; child = child.findNextSibling( "Part" ) )
                {
                    readPart( child, outVisual, report, sourceName );
                }
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    const utf8* pName = child.getName();
                    if ( StringUtil::equals( pName, "Part" ) )
                        continue;
                    if ( StringUtil::equals( pName, "State" ) )
                        readState( child, outVisual, report, sourceName );
                    else if ( StringUtil::equals( pName, "DamageStage" ) )
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
            if ( StringUtil::equals( node.getName(), "ItemVisual" ) == false )
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
