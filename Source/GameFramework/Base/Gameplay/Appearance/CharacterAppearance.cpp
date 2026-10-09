#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearance.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceXmlUtil.h"
#include "GameFramework/Base/Gameplay/Appearance/CustomizationSchema.h"
#include "GameFramework/Base/Gameplay/Appearance/EquipSetCatalog.h"
#include "GameFramework/Base/Gameplay/Appearance/SlotTable.h"

namespace sw
{
    namespace
    {
        struct CharacterAppearanceInternal
        {
            static constexpr const utf8* kArrPresetAttribute[]    = { "id", "parent", "schema", "bodyType", "bodyShape", "face", "body", "tags" };
            static constexpr const utf8* kArrValueAttribute[]     = { "name", "value", "min", "max", "color", "colors", "option", "options" };
            static constexpr const utf8* kArrItemValueAttribute[] = { "name", "value", "color", "option" };
            static constexpr const utf8* kArrEquipAttribute[]     = { "set", "slot", "item", "items", "visible", "state", "damage" };
            static constexpr const utf8* kArrSocketAttribute[]    = { "name", "parent", "offset", "rotation" };

            // 뽑기 키 — 칸 · 매개변수 이름 해시와 섞어 키마다 다른 흐름을 쓴다.
            static constexpr uint64 kKeyBodyType  = 0x51ed2701a3c1b5e7ull;
            static constexpr uint64 kKeyBodyShape = 0x8b1f0e3d77c2a941ull;
            static constexpr uint64 kKeyFace      = 0x2c64f3a9d05be813ull;
            static constexpr uint64 kKeyBody      = 0xe7a90c4b6612f3d5ull;
            static constexpr uint64 kKeySlot      = 0x9d3b5c7e1f2a4068ull;

            static void readColors( string_view text, vector<float4>& outListColor )
            {
                GameDataXml::forEachToken( text, ";", [&]( string_view token )
                {
                    outListColor.push_back( GameDataXml::parseFloat4( token, float4( 1.0f, 1.0f, 1.0f, 1.0f ) ) );
                } );
            }

            static void readValue( const XmlNode& node, CharacterAppearanceDef& inoutDef, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrValueAttribute, report, sourceName );
                CharacterAppearanceValueDef value;
                value._parameter = AppearanceXmlUtil::readName( node, "name" );
                AppearanceXmlUtil::readNameList( node, "option", value._listOption );
                AppearanceXmlUtil::readNameList( node, "options", value._listOption );
                readColors( node.getAttributeText( "color" ), value._listColor );
                readColors( node.getAttributeText( "colors" ), value._listColor );
                const bool bFixed      = node.findAttribute( "value" ) != nullptr;
                const bool bRange      = node.findAttribute( "min" ) != nullptr && node.findAttribute( "max" ) != nullptr;
                value._bHasNumber      = bFixed || bRange ? SW_TRUE : SW_FALSE;
                value._min             = bFixed ? node.getAttributeFloat( "value", 0.0f ) : node.getAttributeFloat( "min", 0.0f );
                value._max             = bFixed ? value._min : node.getAttributeFloat( "max", 0.0f );
                const int32 shapeCount = ( value._bHasNumber == SW_TRUE ? 1 : 0 ) + ( value._listOption.empty() ? 0 : 1 ) + ( value._listColor.empty() ? 0 : 1 );
                if ( value._parameter.empty() || shapeCount != 1 || value._max < value._min )
                {
                    report.addError( "%#: preset '%#' <Value name='%#'> needs exactly one of value / min+max / option(s) / color(s)", sourceName, inoutDef._id.c_str(), value._parameter.c_str() );
                    return;
                }
                for ( CharacterAppearanceValueDef& existing : inoutDef._listValue )
                {
                    if ( existing._parameter == value._parameter )
                    {
                        report.addError( "%#: preset '%#' sets '%#' twice", sourceName, inoutDef._id.c_str(), value._parameter.c_str() );
                        return;
                    }
                }
                inoutDef._listValue.push_back( value );
            }

            static void readItemValue( const XmlNode& node, CharacterAppearanceSlotDef& inoutSlot, AppearanceLoadReport& report, string_view sourceName, const hashed_string& presetId )
            {
                (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrItemValueAttribute, report, sourceName );
                const hashed_string parameter = AppearanceXmlUtil::readName( node, "name" );
                if ( node.findAttribute( "option" ) != nullptr )
                    inoutSlot._customization.setOption( parameter, AppearanceXmlUtil::readName( node, "option" ) );
                else if ( node.findAttribute( "color" ) != nullptr )
                    inoutSlot._customization.setColor( parameter, AppearanceXmlUtil::readFloat4( node, "color", float4( 1.0f, 1.0f, 1.0f, 1.0f ) ) );
                else if ( node.findAttribute( "value" ) != nullptr )
                    inoutSlot._customization.setNumber( parameter, node.getAttributeFloat( "value", 0.0f ) );
                else
                    report.addError( "%#: preset '%#' slot '%#' item value '%#' needs value / color / option", sourceName, presetId.c_str(), inoutSlot._slot.c_str(), parameter.c_str() );
            }

            static void readEquip( const XmlNode& node, CharacterAppearanceDef& inoutDef, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrEquipAttribute, report, sourceName );
                const hashed_string setId = AppearanceXmlUtil::readName( node, "set" );
                if ( setId.empty() == false )
                {
                    if ( node.findAttribute( "slot" ) != nullptr )
                        report.addError( "%#: preset '%#' <Equip> names both a set and a slot", sourceName, inoutDef._id.c_str() );
                    inoutDef._listSet.push_back( setId );
                    return;
                }
                CharacterAppearanceSlotDef slot;
                slot._slot = AppearanceXmlUtil::readName( node, "slot" );
                AppearanceXmlUtil::readNameList( node, "item", slot._listItem );
                AppearanceXmlUtil::readNameList( node, "items", slot._listItem );
                slot._bClear        = node.findAttribute( "item" ) != nullptr && slot._listItem.empty() ? SW_TRUE : SW_FALSE;
                slot._visibleVisual = AppearanceXmlUtil::readName( node, "visible" );
                slot._state         = AppearanceXmlUtil::readName( node, "state" );
                slot._damage        = node.getAttributeFloat( "damage", 0.0f );
                for ( XmlNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Value", true ) )
                        readItemValue( child, slot, report, sourceName, inoutDef._id );
                    else
                        AppearanceXmlUtil::reportUnknownChild( node, child, report, sourceName );
                }
                if ( slot._slot.empty() )
                {
                    report.addError( "%#: preset '%#' <Equip> needs a set or a slot", sourceName, inoutDef._id.c_str() );
                    return;
                }
                for ( const CharacterAppearanceSlotDef& existing : inoutDef._listSlot )
                {
                    if ( existing._slot == slot._slot )
                    {
                        report.addError( "%#: preset '%#' equips slot '%#' twice", sourceName, inoutDef._id.c_str(), slot._slot.c_str() );
                        return;
                    }
                }
                inoutDef._listSlot.push_back( slot );
            }

            static void readPreset( const XmlNode& node, CharacterAppearanceDef& outDef, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXmlUtil::reportUnknownAttributes( node, kArrPresetAttribute, report, sourceName );
                outDef._parent = AppearanceXmlUtil::readName( node, "parent" );
                outDef._schema = AppearanceXmlUtil::readName( node, "schema" );
                AppearanceXmlUtil::readNameList( node, "bodyType", outDef._listBodyType );
                AppearanceXmlUtil::readNameList( node, "bodyShape", outDef._listBodyShape );
                AppearanceXmlUtil::readNameList( node, "face", outDef._listFace );
                AppearanceXmlUtil::readNameList( node, "body", outDef._listBody );
                AppearanceXmlUtil::readTags( node, "tags", outDef._tags );
                for ( XmlNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    const utf8* pName = child.getName();
                    if ( StringUtil::equals( pName, "Value", true ) )
                    {
                        readValue( child, outDef, report, sourceName );
                    }
                    else if ( StringUtil::equals( pName, "Equip", true ) )
                    {
                        readEquip( child, outDef, report, sourceName );
                    }
                    else if ( StringUtil::equals( pName, "SocketOverride", true ) )
                    {
                        (void)AppearanceXmlUtil::reportUnknownAttributes( child, kArrSocketAttribute, report, sourceName );
                        AppearanceSocketOverride socketOverride;
                        socketOverride._name = AppearanceXmlUtil::readName( child, "name" );
                        AppearanceXmlUtil::readPlacement( child, "parent", socketOverride._placement );
                        if ( socketOverride._name.empty() || socketOverride._placement.isEmpty() )
                            report.addError( "%#: preset '%#' <SocketOverride> needs a name and a parent", sourceName, outDef._id.c_str() );
                        else
                            outDef._listSocketOverride.push_back( socketOverride );
                    }
                    else
                    {
                        AppearanceXmlUtil::reportUnknownChild( node, child, report, sourceName );
                    }
                }
            }

            template <typename TList>
            static void replaceIfGiven( TList& inoutList, const TList& listGiven )
            {
                if ( listGiven.empty() == false )
                    inoutList = listGiven;
            }

            static hashed_string pickName( const vector<hashed_string>& listCandidate, uint32 seed, uint64 key )
            {
                if ( listCandidate.empty() )
                    return hashed_string{};
                return listCandidate[CharacterAppearanceCatalog::pickIndex( seed, key, static_cast<uint32>( listCandidate.size() ) )];
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AppearanceSlotRequest* CharacterAppearanceSpec::findSlot( const hashed_string& slot )
    {
        for ( AppearanceSlotRequest& request : _listSlot )
        {
            if ( request._slot == slot )
                return &request;
        }
        return nullptr;
    }

    const AppearanceSlotRequest* CharacterAppearanceSpec::findSlot( const hashed_string& slot ) const
    {
        for ( const AppearanceSlotRequest& request : _listSlot )
        {
            if ( request._slot == slot )
                return &request;
        }
        return nullptr;
    }

    bool CharacterAppearanceCatalog::loadFromNode( const XmlNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXmlUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XmlNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "CharacterAppearance", true ) == false )
            {
                AppearanceXmlUtil::reportUnknownChild( root, node, report, sourceName );
                continue;
            }
            CharacterAppearanceDef def;
            def._id = AppearanceXmlUtil::readName( node, "id" );
            if ( def._id.empty() || findPreset( def._id ) != nullptr )
            {
                report.addError( "%#: <CharacterAppearance> without an id or with a duplicate id '%#'", sourceName, def._id.c_str() );
                continue;
            }
            CharacterAppearanceInternal::readPreset( node, def, report, sourceName );
            _listPreset.push_back( def );
        }
        return report.getErrors().size() == errorCountBefore;
    }

    const CharacterAppearanceDef* CharacterAppearanceCatalog::findPreset( const hashed_string& id ) const
    {
        for ( const CharacterAppearanceDef& def : _listPreset )
        {
            if ( def._id == id )
                return &def;
        }
        return nullptr;
    }

    bool CharacterAppearanceCatalog::collectChain( const hashed_string& id, vector<const CharacterAppearanceDef*>& outListChain ) const
    {
        outListChain.clear();
        for ( const CharacterAppearanceDef* pDef = findPreset( id ); pDef != nullptr; pDef = pDef->_parent.empty() ? nullptr : findPreset( pDef->_parent ) )
        {
            for ( const CharacterAppearanceDef* pVisited : outListChain )
            {
                if ( pVisited == pDef )
                    return false; // 순환
            }
            outListChain.push_back( pDef );
            if ( pDef->_parent.empty() == false && findPreset( pDef->_parent ) == nullptr )
                return false; // 모르는 부모
        }
        // 뿌리부터 잎으로.
        std::reverse( outListChain.begin(), outListChain.end() );
        return outListChain.empty() == false;
    }

    bool CharacterAppearanceCatalog::mergeChain( const hashed_string& id, CharacterAppearanceDef& outMerged ) const
    {
        vector<const CharacterAppearanceDef*> listChain;
        if ( collectChain( id, listChain ) == false )
            return false;
        outMerged     = CharacterAppearanceDef{};
        outMerged._id = id;
        for ( const CharacterAppearanceDef* pLevel : listChain )
        {
            const CharacterAppearanceDef& level = *pLevel;
            if ( level._schema.empty() == false )
                outMerged._schema = level._schema;
            CharacterAppearanceInternal::replaceIfGiven( outMerged._listBodyType, level._listBodyType );
            CharacterAppearanceInternal::replaceIfGiven( outMerged._listBodyShape, level._listBodyShape );
            CharacterAppearanceInternal::replaceIfGiven( outMerged._listFace, level._listFace );
            CharacterAppearanceInternal::replaceIfGiven( outMerged._listBody, level._listBody );
            for ( const TagID tag : level._tags.getTags() )
            {
                outMerged._tags.addTag( tag );
            }
            outMerged._listSet.insert( outMerged._listSet.end(), level._listSet.begin(), level._listSet.end() );
            for ( const CharacterAppearanceValueDef& value : level._listValue )
            {
                bool bReplaced = false;
                for ( CharacterAppearanceValueDef& existing : outMerged._listValue )
                {
                    if ( existing._parameter == value._parameter )
                    {
                        existing  = value;
                        bReplaced = true;
                    }
                }
                if ( bReplaced == false )
                    outMerged._listValue.push_back( value );
            }
            for ( const CharacterAppearanceSlotDef& slot : level._listSlot )
            {
                bool bReplaced = false;
                for ( CharacterAppearanceSlotDef& existing : outMerged._listSlot )
                {
                    if ( existing._slot == slot._slot )
                    {
                        existing  = slot;
                        bReplaced = true;
                    }
                }
                if ( bReplaced == false )
                    outMerged._listSlot.push_back( slot );
            }
            for ( const AppearanceSocketOverride& socketOverride : level._listSocketOverride )
            {
                bool bReplaced = false;
                for ( AppearanceSocketOverride& existing : outMerged._listSocketOverride )
                {
                    if ( existing._name == socketOverride._name )
                    {
                        existing  = socketOverride;
                        bReplaced = true;
                    }
                }
                if ( bReplaced == false )
                    outMerged._listSocketOverride.push_back( socketOverride );
            }
        }
        return true;
    }

    uint32 CharacterAppearanceCatalog::pickIndex( uint32 seed, uint64 keyHash, uint32 count )
    {
        if ( count <= 1 )
            return 0;
        const uint32 key = static_cast<uint32>( keyHash ) ^ GameHash::mix32( static_cast<uint32>( keyHash >> 32 ) );
        return GameHash::mix32( seed ^ GameHash::mix32( key ) ) % count;
    }

    float32 CharacterAppearanceCatalog::pickUnit( uint32 seed, uint64 keyHash )
    {
        const uint32 key = static_cast<uint32>( keyHash ) ^ GameHash::mix32( static_cast<uint32>( keyHash >> 32 ) );
        return GameHash::toUnitFloat( GameHash::mix32( seed ^ GameHash::mix32( key ^ 0x68e31da4u ) ) );
    }

    bool CharacterAppearanceCatalog::expand( const hashed_string& id, uint32 seed, const SlotTable& slotTable, const EquipSetCatalog& sets, const CustomizationSchemaCatalog& schemas,
                                             CharacterAppearanceSpec& outSpec ) const
    {
        CharacterAppearanceDef merged;
        if ( mergeChain( id, merged ) == false )
            return false;
        using Internal              = CharacterAppearanceInternal;
        outSpec                     = CharacterAppearanceSpec{};
        outSpec._presetId           = id;
        outSpec._seed               = seed;
        outSpec._schema             = merged._schema;
        outSpec._tags               = merged._tags;
        outSpec._bodyType           = Internal::pickName( merged._listBodyType, seed, Internal::kKeyBodyType );
        outSpec._bodyShape          = Internal::pickName( merged._listBodyShape, seed, Internal::kKeyBodyShape );
        outSpec._face               = Internal::pickName( merged._listFace, seed, Internal::kKeyFace );
        outSpec._bodyVisual         = Internal::pickName( merged._listBody, seed, Internal::kKeyBody );
        outSpec._listSocketOverride = merged._listSocketOverride;

        const CustomizationSchemaDef* pSchema = schemas.findSchema( merged._schema );
        for ( const CharacterAppearanceValueDef& value : merged._listValue )
        {
            const CustomizationParamDef* pParam = pSchema != nullptr ? pSchema->findParameter( value._parameter ) : nullptr;
            if ( pParam == nullptr )
                continue;
            const uint64 key = value._parameter.getHash();
            switch ( pParam->_kind )
            {
                case CustomizationKind::Slider:
                {
                    outSpec._customization.setNumber( value._parameter, value._min + ( value._max - value._min ) * pickUnit( seed, key ) );
                    break;
                }
                case CustomizationKind::Color:
                {
                    if ( value._listColor.empty() == false )
                        outSpec._customization.setColor( value._parameter, value._listColor[pickIndex( seed, key, static_cast<uint32>( value._listColor.size() ) )] );
                    break;
                }
                case CustomizationKind::Choice:
                case CustomizationKind::Attachment:
                {
                    if ( value._listOption.empty() == false )
                        outSpec._customization.setOption( value._parameter, Internal::pickName( value._listOption, seed, key ) );
                    break;
                }
            }
        }

        for ( const AppearanceSlotDef& slotDef : slotTable.getSlots() )
        {
            AppearanceSlotRequest request;
            request._slot = slotDef._name;
            outSpec._listSlot.push_back( request );
        }
        // 장비 구성은 층마다 세트 → 칸 순서로 건다 — 아래 층의 세트가 위 층의 칸을 덮는다.
        vector<const CharacterAppearanceDef*> listChain;
        if ( collectChain( id, listChain ) == false )
            return false;
        vector<EquipSetPieceDef> listPiece;
        for ( const CharacterAppearanceDef* pLevel : listChain )
        {
            for ( const hashed_string& setId : pLevel->_listSet )
            {
                const EquipSetDef* pSet = sets.findSet( setId );
                if ( pSet == nullptr )
                    continue;
                sets.computePieces( *pSet, outSpec._bodyType, listPiece );
                for ( const EquipSetPieceDef& piece : listPiece )
                {
                    AppearanceSlotRequest* pRequest = outSpec.findSlot( piece._slot );
                    if ( pRequest == nullptr )
                        continue;
                    *pRequest         = AppearanceSlotRequest{};
                    pRequest->_slot   = piece._slot;
                    pRequest->_itemId = piece._listItem.front();
                }
            }
            for ( const CharacterAppearanceSlotDef& slot : pLevel->_listSlot )
            {
                AppearanceSlotRequest* pRequest = outSpec.findSlot( slot._slot );
                if ( pRequest == nullptr )
                    continue;
                if ( slot._bClear == SW_TRUE )
                {
                    *pRequest       = AppearanceSlotRequest{};
                    pRequest->_slot = slot._slot;
                    continue;
                }
                if ( slot._listItem.empty() == false )
                {
                    const hashed_string itemId = Internal::pickName( slot._listItem, seed, slot._slot.getHash() ^ Internal::kKeySlot );
                    if ( itemId != pRequest->_itemId )
                    {
                        *pRequest       = AppearanceSlotRequest{};
                        pRequest->_slot = slot._slot;
                    }
                    pRequest->_itemId = itemId;
                }
                if ( slot._visibleVisual.empty() == false )
                    pRequest->_visibleVisual = slot._visibleVisual;
                if ( slot._state.empty() == false )
                    pRequest->_state = slot._state;
                if ( slot._damage > 0.0f )
                    pRequest->_damage = slot._damage;
                pRequest->_customization.overlay( slot._customization );
            }
        }
        return true;
    }
} // namespace sw
