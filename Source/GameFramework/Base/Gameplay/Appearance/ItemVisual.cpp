#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/ItemVisual.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceXMLUtil.h"

namespace sw
{
    namespace
    {
        struct ItemVisualInternal
        {
            static constexpr const utf8* kArrVisualAttribute[]          = { "id", "occupancy", "customization", "tags", "defaultState" };
            static constexpr const utf8* kArrPartAttribute[]            = { "name", "kind", "mesh", "prefab", "sprite", "material", "skeleton", "sockets",
                                                                            "socket", "offset", "rotation", "deforms", "layer", "breakable", "breakStage", "impulse" };
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
                    if ( StringUtil::equals( text, string_view( toString( kind ) ), true ) )
                    {
                        outKind = kind;
                        return true;
                    }
                }
                return false;
            }

            /** @brief 부품 종류가 쓰는 에셋 속성 이름입니다(몸 수정은 없다). */
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

            /** @brief 종류에 맞지 않는 에셋 속성이 있으면 오류입니다. */
            static void reportMisplacedAssetAttributes( const XMLNode& node, AppearancePartKind kind, AppearanceLoadReport& report, string_view sourceName, const hashed_string& visualID )
            {
                constexpr const utf8* kArrAssetAttribute[] = { "mesh", "prefab", "sprite" };
                const utf8*           pExpected            = getAssetAttribute( kind );
                for ( const utf8* pAttribute : kArrAssetAttribute )
                {
                    if ( node.findAttribute( pAttribute ) != nullptr && ( pExpected == nullptr || StringUtil::equals( pAttribute, pExpected, true ) == false ) )
                        report.addError( "%#: visual '%#' <%#> of kind %# cannot have '%#'", sourceName, visualID.c_str(), node.getName(), toString( kind ), pAttribute );
                }
            }

            static bool hasPart( const ItemVisualDef& visual, const hashed_string& part, AppearanceLoadReport& report, string_view sourceName, const utf8* pWhere )
            {
                if ( visual.findPart( part ) != nullptr )
                    return true;
                report.addError( "%#: visual '%#' %# names unknown part '%#'", sourceName, visual._id.c_str(), pWhere, part.c_str() );
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
                    variant._name     = AppearanceXMLUtil::readName( child, "name" );
                    variant._asset    = AppearanceXMLUtil::readName( child, getAssetAttribute( inoutPart._kind ) );
                    variant._material = AppearanceXMLUtil::readName( child, "material" );
                    if ( variant._name.empty() || inoutPart.findVariant( variant._name ) != nullptr )
                        report.addError( "%#: visual '%#' part '%#' has a variant without a name or a duplicate '%#'", sourceName, visualID.c_str(), inoutPart._name.c_str(), variant._name.c_str() );
                    else
                        inoutPart._listVariant.push_back( variant );
                }
                else if ( StringUtil::equals( pName, "MaterialVariant", true ) && bModification == false )
                {
                    (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrMaterialVariantAttribute, report, sourceName );
                    AppearancePartVariantDef variant;
                    variant._name     = AppearanceXMLUtil::readName( child, "name" );
                    variant._material = AppearanceXMLUtil::readName( child, "material" );
                    if ( variant._name.empty() || variant._material.empty() || inoutPart.findMaterialVariant( variant._name ) != nullptr )
                        report.addError( "%#: visual '%#' part '%#' has a bad or duplicate material variant '%#'", sourceName, visualID.c_str(), inoutPart._name.c_str(), variant._name.c_str() );
                    else
                        inoutPart._listMaterialVariant.push_back( variant );
                }
                else if ( StringUtil::equals( pName, "Morph", true ) && bModification )
                {
                    (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrMorphAttribute, report, sourceName );
                    inoutPart._listMorph.push_back( AppearanceMorphDef{ AppearanceXMLUtil::readName( child, "name" ), child.getAttributeFloat( "weight", 1.0f ) } );
                }
                else if ( StringUtil::equals( pName, "Material", true ) && bModification )
                {
                    (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrMaterialAttribute, report, sourceName );
                    AppearanceMaterialValueDef value;
                    value._name  = AppearanceXMLUtil::readName( child, "name" );
                    value._value = AppearanceXMLUtil::readFloat4( child, "value", float4::Zero );
                    inoutPart._listMaterialValue.push_back( value );
                }
                else if ( StringUtil::equals( pName, "HideRegion", true ) && bModification )
                {
                    (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrRegionAttribute, report, sourceName );
                    inoutPart._listHiddenRegion.push_back( AppearanceXMLUtil::readName( child, "name" ) );
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
                part._name = AppearanceXMLUtil::readName( node, "name" );
                if ( parseKind( node.getAttributeText( "kind" ), part._kind ) == false )
                {
                    report.addError( "%#: visual '%#' part '%#' has unknown kind '%#'", sourceName, inoutVisual._id.c_str(), part._name.c_str(), node.getAttributeText( "kind" ) );
                    return;
                }
                if ( part._name.empty() || inoutVisual.findPart( part._name ) != nullptr )
                {
                    report.addError( "%#: visual '%#' has a part without a name or a duplicate '%#'", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                    return;
                }
                reportMisplacedAssetAttributes( node, part._kind, report, sourceName, inoutVisual._id );
                const utf8* pAssetAttribute = getAssetAttribute( part._kind );
                part._asset                 = pAssetAttribute != nullptr ? AppearanceXMLUtil::readName( node, pAssetAttribute ) : hashed_string{};
                part._material              = AppearanceXMLUtil::readName( node, "material" );
                part._skeleton              = AppearanceXMLUtil::readName( node, "skeleton" );
                part._socketSet             = AppearanceXMLUtil::readName( node, "sockets" );
                part._breakStage            = AppearanceXMLUtil::readName( node, "breakStage" );
                part._breakImpulse          = AppearanceXMLUtil::readFloat3( node, "impulse", float3::Zero );
                part._layer                 = node.getAttributeInt( "layer", 0 );
                part._bDeforms              = node.getAttributeBool( "deforms", part._kind == AppearancePartKind::Skinned ) ? SW_TRUE : SW_FALSE;
                part._bBreakable            = node.getAttributeBool( "breakable", part._breakStage.empty() == false ) ? SW_TRUE : SW_FALSE;
                AppearanceXMLUtil::readPlacement( node, "socket", part._placement );
                if ( pAssetAttribute != nullptr && part._asset.empty() )
                    report.addError( "%#: visual '%#' part '%#' needs '%#'", sourceName, inoutVisual._id.c_str(), part._name.c_str(), pAssetAttribute );
                if ( part._kind == AppearancePartKind::SocketPrefab && part._placement.isEmpty() )
                    report.addError( "%#: visual '%#' part '%#' needs a socket", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
                if ( part._kind == AppearancePartKind::BodyModification && ( part._placement.isEmpty() == false || part._bBreakable == SW_TRUE ) )
                    report.addError( "%#: visual '%#' body modification '%#' cannot have a socket or break off", sourceName, inoutVisual._id.c_str(), part._name.c_str() );
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
                state._name = AppearanceXMLUtil::readName( node, "name" );
                if ( state._name.empty() || inoutVisual.findState( state._name ) != nullptr )
                {
                    report.addError( "%#: visual '%#' has a state without a name or a duplicate '%#'", sourceName, inoutVisual._id.c_str(), state._name.c_str() );
                    return;
                }
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Place", true ) )
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrPlaceAttribute, report, sourceName );
                        AppearanceStateDef::Placement placement;
                        placement._part = AppearanceXMLUtil::readName( child, "part" );
                        AppearanceXMLUtil::readPlacement( child, "socket", placement._placement );
                        if ( hasPart( inoutVisual, placement._part, report, sourceName, "<Place>" ) )
                            state._listPlacement.push_back( placement );
                    }
                    else if ( StringUtil::equals( child.getName(), "HidePart", true ) )
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrHidePartAttribute, report, sourceName );
                        const hashed_string part = AppearanceXMLUtil::readName( child, "part" );
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
                stage._name            = AppearanceXMLUtil::readName( node, "name" );
                stage._threshold       = node.getAttributeFloat( "threshold", -1.0f );
                const float32 previous = inoutVisual._listDamageStage.empty() ? 0.0f : inoutVisual._listDamageStage.back()._threshold;
                const bool    bOrdered = previous < stage._threshold && stage._threshold <= 1.0f;
                if ( stage._name.empty() || inoutVisual.findDamageStage( stage._name ) != nullptr || bOrdered == false )
                {
                    report.addError( "%#: visual '%#' damage stage '%#' needs a unique name and a threshold in (previous, 1]", sourceName, inoutVisual._id.c_str(), stage._name.c_str() );
                    return;
                }
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Variant", true ) )
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrStageVariantAttribute, report, sourceName );
                        AppearanceDamageStageDef::PartVariant entry;
                        entry._part                    = AppearanceXMLUtil::readName( child, "part" );
                        entry._variant                 = AppearanceXMLUtil::readName( child, "name" );
                        const AppearancePartDef* pPart = inoutVisual.findPart( entry._part );
                        if ( pPart == nullptr || pPart->findVariant( entry._variant ) == nullptr )
                            report.addError( "%#: visual '%#' damage stage '%#' names unknown part/variant '%#'/'%#'", sourceName, inoutVisual._id.c_str(), stage._name.c_str(), entry._part.c_str(),
                                             entry._variant.c_str() );
                        else
                            stage._listVariant.push_back( entry );
                    }
                    else if ( StringUtil::equals( child.getName(), "Material", true ) )
                    {
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrStageMaterialAttribute, report, sourceName );
                        AppearanceMaterialValueDef value;
                        value._part  = AppearanceXMLUtil::readName( child, "part" );
                        value._name  = AppearanceXMLUtil::readName( child, "name" );
                        value._value = AppearanceXMLUtil::readFloat4( child, "value", float4::Zero );
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
                outVisual._occupancy     = AppearanceXMLUtil::readName( node, "occupancy" );
                outVisual._customization = AppearanceXMLUtil::readName( node, "customization" );
                outVisual._defaultState  = AppearanceXMLUtil::readName( node, "defaultState" );
                AppearanceXMLUtil::readTags( node, "tags", outVisual._tags );
                // 부품을 먼저 모두 읽는다 — 상태 · 피해 단계가 앞뒤 순서와 상관없이 부품을 가리킨다.
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
                    report.addError( "%#: visual '%#' default state '%#' is not one of its states", sourceName, outVisual._id.c_str(), outVisual._defaultState.c_str() );
                if ( outVisual._defaultState.empty() && outVisual._listState.empty() == false )
                    outVisual._defaultState = outVisual._listState.front()._name;
                for ( const AppearancePartDef& part : outVisual._listPart )
                {
                    if ( part._breakStage.empty() == false && outVisual.findDamageStage( part._breakStage ) == nullptr )
                        report.addError( "%#: visual '%#' part '%#' breaks at unknown damage stage '%#'", sourceName, outVisual._id.c_str(), part._name.c_str(), part._breakStage.c_str() );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( AppearancePartKind kind )
    {
        switch ( kind )
        {
            case AppearancePartKind::Skinned:
                return "Skinned";
            case AppearancePartKind::SocketPrefab:
                return "SocketPrefab";
            case AppearancePartKind::BodyModification:
                return "BodyModification";
            case AppearancePartKind::Sprite:
                return "Sprite";
        }
        return "Unknown";
    }

    const AppearancePartVariantDef* AppearancePartDef::findVariant( const hashed_string& name ) const
    {
        for ( const AppearancePartVariantDef& variant : _listVariant )
        {
            if ( variant._name == name )
                return &variant;
        }
        return nullptr;
    }

    const AppearancePartVariantDef* AppearancePartDef::findMaterialVariant( const hashed_string& name ) const
    {
        for ( const AppearancePartVariantDef& variant : _listMaterialVariant )
        {
            if ( variant._name == name )
                return &variant;
        }
        return nullptr;
    }

    const AppearancePartDef* ItemVisualDef::findPart( const hashed_string& name ) const
    {
        for ( const AppearancePartDef& part : _listPart )
        {
            if ( part._name == name )
                return &part;
        }
        return nullptr;
    }

    const AppearanceStateDef* ItemVisualDef::findState( const hashed_string& name ) const
    {
        for ( const AppearanceStateDef& state : _listState )
        {
            if ( state._name == name )
                return &state;
        }
        return nullptr;
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
            visual._id = AppearanceXMLUtil::readName( node, "id" );
            if ( visual._id.empty() || findVisual( visual._id ) != nullptr )
            {
                report.addError( "%#: <ItemVisual> without an id or with a duplicate id '%#'", sourceName, visual._id.c_str() );
                continue;
            }
            ItemVisualInternal::readVisual( node, visual, report, sourceName );
            _listVisual.push_back( visual );
        }
        return report.getErrors().size() == errorCountBefore;
    }

    const ItemVisualDef* ItemVisualCatalog::findVisual( const hashed_string& id ) const
    {
        for ( const ItemVisualDef& visual : _listVisual )
        {
            if ( visual._id == id )
                return &visual;
        }
        return nullptr;
    }
} // namespace sw
