#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/EquipmentSetCatalog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceXMLUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Equipment.h"

namespace sw
{
    namespace
    {
        struct EquipmentSetCatalogInternal
        {

            [[nodiscard]] static bool readPiece( const XMLNode& node, EquipmentSetPieceDef& outPiece, AppearanceLoadReport& report, string_view sourceName, const hashed_string& setID )
            {
                (void)AppearanceXMLUtil::readProperties( node, outPiece, report, sourceName, { "items" } ); // 오류는 보고서에 쌓인다
                AppearanceXMLUtil::readNameList( node, "items", outPiece._listItem );
                if ( outPiece._slot.empty() || outPiece._listItem.empty() )
                {
                    report.addErrorWithID( "Appearance.SetPieceShape", "%#: set '%#' <Piece> needs a slot and items", sourceName, setID.c_str() );
                    return false;
                }
                return true;
            }

            static bool hasPieceSlot( const vector<EquipmentSetPieceDef>& listPiece, const hashed_string& slot )
            {
                return VectorUtil::containsBy<&EquipmentSetPieceDef::_slot>( listPiece, slot );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool EquipmentSetCatalog::loadFromXML( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXMLUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XMLNode setNode = root.findChild(); setNode; setNode = setNode.findNextSibling() )
        {
            if ( StringUtil::equals( setNode.getName(), "Set" ) == false )
            {
                AppearanceXMLUtil::reportUnknownChild( root, setNode, report, sourceName );
                continue;
            }
            EquipmentSetDef def;
            (void)AppearanceXMLUtil::readProperties( setNode, def, report, sourceName );
            if ( def._id.empty() )
                continue; // 빠진 id 는 위 읽기가 보고서에 남겼다
            if ( findSet( def._id ) != nullptr )
            {
                report.addErrorWithID( "Appearance.IDDuplicate", "%#: <Set> has a duplicate id '%#'", sourceName, def._id.c_str() );
                continue;
            }
            for ( XMLNode child = setNode.findChild(); child; child = child.findNextSibling() )
            {
                const utf8* pName = child.getName();
                if ( StringUtil::equals( pName, "Piece" ) )
                {
                    EquipmentSetPieceDef piece;
                    if ( EquipmentSetCatalogInternal::readPiece( child, piece, report, sourceName, def._id ) == false )
                        continue;
                    if ( EquipmentSetCatalogInternal::hasPieceSlot( def._listPiece, piece._slot ) )
                        report.addErrorWithID( "Appearance.SetPieceRepeated", "%#: set '%#' has two pieces for slot '%#'", sourceName, def._id.c_str(), piece._slot.c_str() );
                    else
                        def._listPiece.push_back( piece );
                }
                else if ( StringUtil::equals( pName, "Variant" ) )
                {
                    EquipmentSetVariantDef variant;
                    (void)AppearanceXMLUtil::readProperties( child, variant, report, sourceName );
                    if ( variant._bodyType.empty() )
                        report.addErrorWithID( "Appearance.SetVariantNoBodyType", "%#: set '%#' <Variant> without a bodyType", sourceName, def._id.c_str() );
                    for ( XMLNode pieceNode = child.findChild(); pieceNode; pieceNode = pieceNode.findNextSibling() )
                    {
                        EquipmentSetPieceDef piece;
                        if ( StringUtil::equals( pieceNode.getName(), "Piece" ) == false )
                            AppearanceXMLUtil::reportUnknownChild( child, pieceNode, report, sourceName );
                        else if ( EquipmentSetCatalogInternal::readPiece( pieceNode, piece, report, sourceName, def._id ) )
                            variant._listPiece.push_back( piece );
                    }
                    def._listVariant.push_back( variant );
                }
                else if ( StringUtil::equals( pName, "Complete" ) )
                {
                    EquipmentSetCompleteParams complete;
                    (void)AppearanceXMLUtil::readProperties( child, complete, report, sourceName, { "slots" } );
                    def._completeVisual = complete._visual;
                    AppearanceXMLUtil::readNameList( child, "slots", def._listCompleteSlot );
                    if ( def._completeVisual.empty() || def._listCompleteSlot.empty() )
                        report.addErrorWithID( "Appearance.SetCompleteShape", "%#: set '%#' <Complete> needs a visual and slots", sourceName, def._id.c_str() );
                }
                else
                {
                    AppearanceXMLUtil::reportUnknownChild( setNode, child, report, sourceName );
                }
            }
            if ( def._listPiece.empty() )
                report.addErrorWithID( "Appearance.SetNoPiece", "%#: set '%#' has no <Piece>", sourceName, def._id.c_str() );
            for ( const EquipmentSetVariantDef& variant : def._listVariant )
            {
                for ( const EquipmentSetPieceDef& piece : variant._listPiece )
                {
                    if ( EquipmentSetCatalogInternal::hasPieceSlot( def._listPiece, piece._slot ) == false )
                        report.addErrorWithID( "Appearance.SetVariantSlotUnknown", "%#: set '%#' variant '%#' replaces slot '%#' the set has no piece for", sourceName, def._id.c_str(), variant._bodyType.c_str(), piece._slot.c_str() );
                }
            }
            for ( const hashed_string& slot : def._listCompleteSlot )
            {
                if ( EquipmentSetCatalogInternal::hasPieceSlot( def._listPiece, slot ) == false )
                    report.addErrorWithID( "Appearance.SetCompleteSlotUnknown", "%#: set '%#' <Complete> names slot '%#' the set has no piece for", sourceName, def._id.c_str(), slot.c_str() );
            }
            _listSet.push_back( def );
        }
        return report.getErrors().size() == errorCountBefore;
    }

    const EquipmentSetDef* EquipmentSetCatalog::findSet( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&EquipmentSetDef::_id>( _listSet, id );
    }

    void EquipmentSetCatalog::computePieces( const EquipmentSetDef& def, const hashed_string& bodyType, vector<EquipmentSetPieceDef>& outListPiece ) const
    {
        outListPiece = def._listPiece;
        if ( bodyType.empty() )
            return;
        for ( const EquipmentSetVariantDef& variant : def._listVariant )
        {
            if ( variant._bodyType != bodyType )
                continue;
            for ( const EquipmentSetPieceDef& replacement : variant._listPiece )
            {
                for ( EquipmentSetPieceDef& piece : outListPiece )
                {
                    if ( piece._slot == replacement._slot )
                        piece._listItem = replacement._listItem;
                }
            }
        }
    }

    EquipmentSetProgress EquipmentSetCatalog::computeProgress( const hashed_string& setID, const vector<EquipmentSlot>& listSlot, const hashed_string& bodyType ) const
    {
        const EquipmentSetDef* pDef = findSet( setID );
        if ( pDef == nullptr )
            return EquipmentSetProgress{};
        return computeProgressWith( *pDef, bodyType, [&]( const hashed_string& slot )
        {
            for ( const EquipmentSlot& equipSlot : listSlot )
            {
                if ( equipSlot._name == slot )
                    return equipSlot._item.isEmpty() || equipSlot._bSuppressed == SW_TRUE ? hashed_string{} : equipSlot._item._itemID;
            }
            return hashed_string{};
        } );
    }

    void EquipmentSetCatalog::collectSetItems( const hashed_string& setID, vector<hashed_string>& outListItemID ) const
    {
        const EquipmentSetDef* pDef = findSet( setID );
        if ( pDef == nullptr )
            return;
        for ( const EquipmentSetPieceDef& piece : pDef->_listPiece )
        {
            outListItemID.insert( outListItemID.end(), piece._listItem.begin(), piece._listItem.end() );
        }
        for ( const EquipmentSetVariantDef& variant : pDef->_listVariant )
        {
            for ( const EquipmentSetPieceDef& piece : variant._listPiece )
            {
                outListItemID.insert( outListItemID.end(), piece._listItem.begin(), piece._listItem.end() );
            }
        }
    }
} // namespace sw
