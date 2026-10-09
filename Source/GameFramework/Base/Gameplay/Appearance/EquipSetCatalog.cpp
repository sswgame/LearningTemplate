#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/EquipSetCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/Xml/XmlDocument.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceXmlUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Equipment.h"

namespace sw
{
    namespace
    {
        struct EquipSetCatalogInternal
        {
            static constexpr const utf8* kArrSetAttribute[]      = { "id" };
            static constexpr const utf8* kArrPieceAttribute[]    = { "slot", "items" };
            static constexpr const utf8* kArrVariantAttribute[]  = { "bodyType" };
            static constexpr const utf8* kArrCompleteAttribute[] = { "visual", "slots" };

            [[nodiscard]] static bool readPiece( const XmlNode& node, EquipSetPieceDef& outPiece, AppearanceLoadReport& report, string_view sourceName, const hashed_string& setId )
            {
                (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrPieceAttribute, report, sourceName );
                outPiece._slot = AppearanceXmlUtil::readName( node, "slot" );
                AppearanceXmlUtil::readNameList( node, "items", outPiece._listItem );
                if ( outPiece._slot.empty() || outPiece._listItem.empty() )
                {
                    report.addError( "%#: set '%#' <Piece> needs a slot and items", sourceName, setId.c_str() );
                    return false;
                }
                return true;
            }

            static bool hasPieceSlot( const vector<EquipSetPieceDef>& listPiece, const hashed_string& slot )
            {
                for ( const EquipSetPieceDef& piece : listPiece )
                {
                    if ( piece._slot == slot )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool EquipSetCatalog::loadFromNode( const XmlNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXmlUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XmlNode setNode = root.findChild(); setNode; setNode = setNode.findNextSibling() )
        {
            if ( StringUtil::equals( setNode.getName(), "Set", true ) == false )
            {
                AppearanceXmlUtil::reportUnknownChild( root, setNode, report, sourceName );
                continue;
            }
            (void)AppearanceXmlUtil::reportUnknownAttributes( setNode, EquipSetCatalogInternal::kArrSetAttribute, report, sourceName );
            EquipSetDef def;
            def._id = AppearanceXmlUtil::readName( setNode, "id" );
            if ( def._id.empty() || findSet( def._id ) != nullptr )
            {
                report.addError( "%#: <Set> without an id or with a duplicate id '%#'", sourceName, def._id.c_str() );
                continue;
            }
            for ( XmlNode child = setNode.findChild(); child; child = child.findNextSibling() )
            {
                const utf8* pName = child.getName();
                if ( StringUtil::equals( pName, "Piece", true ) )
                {
                    EquipSetPieceDef piece;
                    if ( EquipSetCatalogInternal::readPiece( child, piece, report, sourceName, def._id ) == false )
                        continue;
                    if ( EquipSetCatalogInternal::hasPieceSlot( def._listPiece, piece._slot ) )
                        report.addError( "%#: set '%#' has two pieces for slot '%#'", sourceName, def._id.c_str(), piece._slot.c_str() );
                    else
                        def._listPiece.push_back( piece );
                }
                else if ( StringUtil::equals( pName, "Variant", true ) )
                {
                    (void)AppearanceXmlUtil::reportUnknownAttributes( child, EquipSetCatalogInternal::kArrVariantAttribute, report, sourceName );
                    EquipSetVariantDef variant;
                    variant._bodyType = AppearanceXmlUtil::readName( child, "bodyType" );
                    if ( variant._bodyType.empty() )
                        report.addError( "%#: set '%#' <Variant> without a bodyType", sourceName, def._id.c_str() );
                    for ( XmlNode pieceNode = child.findChild(); pieceNode; pieceNode = pieceNode.findNextSibling() )
                    {
                        EquipSetPieceDef piece;
                        if ( StringUtil::equals( pieceNode.getName(), "Piece", true ) == false )
                            AppearanceXmlUtil::reportUnknownChild( child, pieceNode, report, sourceName );
                        else if ( EquipSetCatalogInternal::readPiece( pieceNode, piece, report, sourceName, def._id ) )
                            variant._listPiece.push_back( piece );
                    }
                    def._listVariant.push_back( variant );
                }
                else if ( StringUtil::equals( pName, "Complete", true ) )
                {
                    (void)AppearanceXmlUtil::reportUnknownAttributes( child, EquipSetCatalogInternal::kArrCompleteAttribute, report, sourceName );
                    def._completeVisual = AppearanceXmlUtil::readName( child, "visual" );
                    AppearanceXmlUtil::readNameList( child, "slots", def._listCompleteSlot );
                    if ( def._completeVisual.empty() || def._listCompleteSlot.empty() )
                        report.addError( "%#: set '%#' <Complete> needs a visual and slots", sourceName, def._id.c_str() );
                }
                else
                {
                    AppearanceXmlUtil::reportUnknownChild( setNode, child, report, sourceName );
                }
            }
            if ( def._listPiece.empty() )
                report.addError( "%#: set '%#' has no <Piece>", sourceName, def._id.c_str() );
            for ( const EquipSetVariantDef& variant : def._listVariant )
            {
                for ( const EquipSetPieceDef& piece : variant._listPiece )
                {
                    if ( EquipSetCatalogInternal::hasPieceSlot( def._listPiece, piece._slot ) == false )
                        report.addError( "%#: set '%#' variant '%#' replaces slot '%#' the set has no piece for", sourceName, def._id.c_str(), variant._bodyType.c_str(), piece._slot.c_str() );
                }
            }
            for ( const hashed_string& slot : def._listCompleteSlot )
            {
                if ( EquipSetCatalogInternal::hasPieceSlot( def._listPiece, slot ) == false )
                    report.addError( "%#: set '%#' <Complete> names slot '%#' the set has no piece for", sourceName, def._id.c_str(), slot.c_str() );
            }
            _listSet.push_back( def );
        }
        return report.getErrors().size() == errorCountBefore;
    }

    const EquipSetDef* EquipSetCatalog::findSet( const hashed_string& id ) const
    {
        for ( const EquipSetDef& def : _listSet )
        {
            if ( def._id == id )
                return &def;
        }
        return nullptr;
    }

    void EquipSetCatalog::computePieces( const EquipSetDef& def, const hashed_string& bodyType, vector<EquipSetPieceDef>& outListPiece ) const
    {
        outListPiece = def._listPiece;
        if ( bodyType.empty() )
            return;
        for ( const EquipSetVariantDef& variant : def._listVariant )
        {
            if ( variant._bodyType != bodyType )
                continue;
            for ( const EquipSetPieceDef& replacement : variant._listPiece )
            {
                for ( EquipSetPieceDef& piece : outListPiece )
                {
                    if ( piece._slot == replacement._slot )
                        piece._listItem = replacement._listItem;
                }
            }
        }
    }

    EquipSetProgress EquipSetCatalog::computeProgress( const hashed_string& setId, const vector<EquipSlot>& listSlot, const hashed_string& bodyType ) const
    {
        const EquipSetDef* pDef = findSet( setId );
        if ( pDef == nullptr )
            return EquipSetProgress{};
        return computeProgressWith( *pDef, bodyType, [&]( const hashed_string& slot )
        {
            for ( const EquipSlot& equipSlot : listSlot )
            {
                if ( equipSlot._name == slot )
                    return equipSlot._item.isEmpty() || equipSlot._bSuppressed == SW_TRUE ? hashed_string{} : equipSlot._item._itemId;
            }
            return hashed_string{};
        } );
    }

    void EquipSetCatalog::collectSetItems( const hashed_string& setId, vector<hashed_string>& outListItemId ) const
    {
        const EquipSetDef* pDef = findSet( setId );
        if ( pDef == nullptr )
            return;
        for ( const EquipSetPieceDef& piece : pDef->_listPiece )
        {
            outListItemId.insert( outListItemId.end(), piece._listItem.begin(), piece._listItem.end() );
        }
        for ( const EquipSetVariantDef& variant : pDef->_listVariant )
        {
            for ( const EquipSetPieceDef& piece : variant._listPiece )
            {
                outListItemId.insert( outListItemId.end(), piece._listItem.begin(), piece._listItem.end() );
            }
        }
    }
} // namespace sw
