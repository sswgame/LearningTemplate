#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearance.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceXMLUtil.h"
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

            // 뽑기 키: 슬롯이나 매개변수 이름 해시와 섞어서 키마다 다른 난수열을 쓴다.
            static constexpr uint64 kKeyBodyType  = 0x51ed2701a3c1b5e7ull;
            static constexpr uint64 kKeyBodyShape = 0x8b1f0e3d77c2a941ull;
            static constexpr uint64 kKeyFace      = 0x2c64f3a9d05be813ull;
            static constexpr uint64 kKeyBody      = 0xe7a90c4b6612f3d5ull;
            static constexpr uint64 kKeySlot      = 0x9d3b5c7e1f2a4068ull;

            /** @brief ";" 로 나뉜 "r g b a" 색 목록을 읽습니다. 정확히 네 실수가 아닌 색은 @p report 에 오류를 더하고 목록에 넣지 않습니다. */
            static void readColors( string_view text, vector<float4>& outListColor, AppearanceLoadReport& report, string_view sourceName )
            {
                StringUtil::forEachToken( text, ";", [&]( string_view token )
                {
                    float4 color{};
                    if ( GameDataXML::parseFloat4( token, color ) )
                        outListColor.push_back( color );
                    else
                        report.addError( "%#: color '%#' must be four numbers \"r g b a\"", sourceName, token );
                } );
            }

            static void readValue( const XMLNode& node, CharacterAppearanceDef& inoutDef, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrValueAttribute, report, sourceName );
                CharacterAppearanceValueDef value;
                value._parameter = AppearanceXMLUtil::readName( node, "name" );
                AppearanceXMLUtil::readNameList( node, "option", value._listOption );
                AppearanceXMLUtil::readNameList( node, "options", value._listOption );
                readColors( node.getAttributeText( "color" ), value._listColor, report, sourceName );
                readColors( node.getAttributeText( "colors" ), value._listColor, report, sourceName );
                const bool bFixed              = node.findAttribute( "value" ) != nullptr;
                const bool bRange              = node.findAttribute( "min" ) != nullptr && node.findAttribute( "max" ) != nullptr;
                value._bHasNumber              = bFixed || bRange ? SW_TRUE : SW_FALSE;
                value._min                     = bFixed ? node.getAttributeFloat( "value", 0.0f ) : node.getAttributeFloat( "min", 0.0f );
                value._max                     = bFixed ? value._min : node.getAttributeFloat( "max", 0.0f );
                const int32 shapeCount         = ( value._bHasNumber == SW_TRUE ? 1 : 0 ) + ( value._listOption.empty() ? 0 : 1 ) + ( value._listColor.empty() ? 0 : 1 );
                const bool  bInvalidValueShape = value._parameter.empty() || shapeCount != 1 || value._max < value._min;
                if ( bInvalidValueShape )
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

            static void readItemValue( const XMLNode& node, CharacterAppearanceSlotDef& inoutSlot, AppearanceLoadReport& report, string_view sourceName, const hashed_string& presetID )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrItemValueAttribute, report, sourceName );
                const hashed_string parameter = AppearanceXMLUtil::readName( node, "name" );
                if ( node.findAttribute( "option" ) != nullptr )
                    inoutSlot._customization.setOption( parameter, AppearanceXMLUtil::readName( node, "option" ) );
                else if ( node.findAttribute( "color" ) != nullptr )
                {
                    float4 color{};
                    AppearanceXMLUtil::readRequiredFloat4( node, "color", color, report, sourceName );
                    inoutSlot._customization.setColor( parameter, color );
                }
                else if ( node.findAttribute( "value" ) != nullptr )
                    inoutSlot._customization.setNumber( parameter, node.getAttributeFloat( "value", 0.0f ) );
                else
                    report.addError( "%#: preset '%#' slot '%#' item value '%#' needs value / color / option", sourceName, presetID.c_str(), inoutSlot._slot.c_str(), parameter.c_str() );
            }

            static void readEquip( const XMLNode& node, CharacterAppearanceDef& inoutDef, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrEquipAttribute, report, sourceName );
                const hashed_string setID = AppearanceXMLUtil::readName( node, "set" );
                if ( setID.empty() == false )
                {
                    if ( node.findAttribute( "slot" ) != nullptr )
                        report.addError( "%#: preset '%#' <Equip> names both a set and a slot", sourceName, inoutDef._id.c_str() );
                    inoutDef._listSet.push_back( setID );
                    return;
                }
                CharacterAppearanceSlotDef slot;
                slot._slot = AppearanceXMLUtil::readName( node, "slot" );
                AppearanceXMLUtil::readNameList( node, "item", slot._listItem );
                AppearanceXMLUtil::readNameList( node, "items", slot._listItem );
                slot._bClear        = node.findAttribute( "item" ) != nullptr && slot._listItem.empty() ? SW_TRUE : SW_FALSE;
                slot._visibleVisual = AppearanceXMLUtil::readName( node, "visible" );
                slot._state         = AppearanceXMLUtil::readName( node, "state" );
                slot._damage        = node.getAttributeFloat( "damage", 0.0f );
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Value", true ) )
                        readItemValue( child, slot, report, sourceName, inoutDef._id );
                    else
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
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

            static void readPreset( const XMLNode& node, CharacterAppearanceDef& outDef, AppearanceLoadReport& report, string_view sourceName )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( node, kArrPresetAttribute, report, sourceName );
                outDef._parent = AppearanceXMLUtil::readName( node, "parent" );
                outDef._schema = AppearanceXMLUtil::readName( node, "schema" );
                AppearanceXMLUtil::readNameList( node, "bodyType", outDef._listBodyType );
                AppearanceXMLUtil::readNameList( node, "bodyShape", outDef._listBodyShape );
                AppearanceXMLUtil::readNameList( node, "face", outDef._listFace );
                AppearanceXMLUtil::readNameList( node, "body", outDef._listBody );
                AppearanceXMLUtil::readTags( node, "tags", outDef._tags );
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
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
                        (void)AppearanceXMLUtil::reportUnknownAttributes( child, kArrSocketAttribute, report, sourceName );
                        AppearanceSocketOverride socketOverride;
                        socketOverride._name = AppearanceXMLUtil::readName( child, "name" );
                        AppearanceXMLUtil::readPlacement( child, "parent", socketOverride._placement, report, sourceName );
                        if ( socketOverride._name.empty() || socketOverride._placement.isEmpty() )
                            report.addError( "%#: preset '%#' <SocketOverride> needs a name and a parent", sourceName, outDef._id.c_str() );
                        else
                            outDef._listSocketOverride.push_back( socketOverride );
                    }
                    else
                    {
                        AppearanceXMLUtil::reportUnknownChild( node, child, report, sourceName );
                    }
                }
            }

            // --- 쓰기 — 위 읽기와 같은 형식(이름 목록은 쉼표, 색은 공백으로 나눈 rgba, 색 목록은 ;) -----------------------------------------

            static void appendNumber( string& inoutText, float32 value )
            {
                utf8         arrBuffer[constant::kMaxBuffer64];
                const uint32 length = StringUtil::formatNumber( arrBuffer, static_cast<uint32>( sizeof( arrBuffer ) ), value );
                inoutText.append( arrBuffer, length );
            }

            static string formatColor( const float4& color )
            {
                string text;
                appendNumber( text, color._x );
                text += ' ';
                appendNumber( text, color._y );
                text += ' ';
                appendNumber( text, color._z );
                text += ' ';
                appendNumber( text, color._w );
                return text;
            }

            static string joinNames( const vector<hashed_string>& listName )
            {
                string text;
                for ( const hashed_string& name : listName )
                {
                    if ( text.empty() == false )
                        text += ',';
                    text += name.c_str();
                }
                return text;
            }

            /** @brief 이름 목록 속성 — 비었으면 쓰지 않는다(읽기가 빈 목록을 "적지 않음" 으로 본다). */
            static void appendNameList( const XMLNode& node, const utf8* pAttribute, const vector<hashed_string>& listName )
            {
                if ( listName.empty() == false )
                    node.appendAttribute( pAttribute, string_view( joinNames( listName ) ) );
            }

            static void appendName( const XMLNode& node, const utf8* pAttribute, const hashed_string& name )
            {
                if ( name.empty() == false )
                    node.appendAttribute( pAttribute, name.c_str() );
            }

            static void writeValue( const XMLNode& parent, const CharacterAppearanceValueDef& value )
            {
                const XMLNode node = parent.appendChild( "Value" );
                node.appendAttribute( "name", value._parameter.c_str() );
                if ( value._bHasNumber == SW_TRUE && value._min == value._max )
                    node.appendAttribute( "value", value._min );
                else if ( value._bHasNumber == SW_TRUE )
                {
                    node.appendAttribute( "min", value._min );
                    node.appendAttribute( "max", value._max );
                }
                else if ( value._listOption.size() == 1 )
                    node.appendAttribute( "option", value._listOption[0].c_str() );
                else if ( value._listOption.empty() == false )
                    node.appendAttribute( "options", string_view( joinNames( value._listOption ) ) );
                else
                {
                    string colors;
                    for ( const float4& color : value._listColor )
                    {
                        if ( colors.empty() == false )
                            colors += "; ";
                        colors += formatColor( color );
                    }
                    node.appendAttribute( value._listColor.size() == 1 ? "color" : "colors", string_view( colors ) );
                }
            }

            static void writeSlot( const XMLNode& parent, const CharacterAppearanceSlotDef& slot )
            {
                const XMLNode node = parent.appendChild( "Equip" );
                node.appendAttribute( "slot", slot._slot.c_str() );
                if ( slot._bClear == SW_TRUE )
                    node.appendAttribute( "item", "" );
                else if ( slot._listItem.size() == 1 )
                    node.appendAttribute( "item", slot._listItem[0].c_str() );
                else
                    appendNameList( node, "items", slot._listItem );
                appendName( node, "visible", slot._visibleVisual );
                appendName( node, "state", slot._state );
                if ( slot._damage != 0.0f )
                    node.appendAttribute( "damage", slot._damage );
            }

            static void writeSocketOverride( const XMLNode& parent, const AppearanceSocketOverride& socketOverride )
            {
                const XMLNode node = parent.appendChild( "SocketOverride" );
                node.appendAttribute( "name", socketOverride._name.c_str() );
                appendNameList( node, "parent", socketOverride._placement._listSocket );
                const float3& offset   = socketOverride._placement._offset;
                const float3& rotation = socketOverride._placement._rotation;
                string        text;
                if ( offset != float3::Zero )
                {
                    appendNumber( text, offset._x );
                    text += ' ';
                    appendNumber( text, offset._y );
                    text += ' ';
                    appendNumber( text, offset._z );
                    node.appendAttribute( "offset", string_view( text ) );
                }
                if ( rotation != float3::Zero )
                {
                    text.clear();
                    appendNumber( text, rotation._x );
                    text += ' ';
                    appendNumber( text, rotation._y );
                    text += ' ';
                    appendNumber( text, rotation._z );
                    node.appendAttribute( "rotation", string_view( text ) );
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

    bool CharacterAppearanceCatalog::loadFromNode( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const size_t errorCountBefore = report.getErrors().size();
        (void)AppearanceXMLUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "CharacterAppearance", true ) == false )
            {
                AppearanceXMLUtil::reportUnknownChild( root, node, report, sourceName );
                continue;
            }
            CharacterAppearanceDef def;
            def._id = AppearanceXMLUtil::readName( node, "id" );
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

    bool CharacterAppearanceCatalog::loadPresetFromNode( const XMLNode& presetNode, AppearanceLoadReport& report, string_view sourceName )
    {
        const size_t errorCountBefore = report.getErrors().size();
        if ( StringUtil::equals( presetNode.getName(), "CharacterAppearance", true ) == false )
        {
            report.addError( "%#: a preset asset needs a <CharacterAppearance> root, got <%#>", sourceName, presetNode.getName() );
            return false;
        }
        CharacterAppearanceDef def;
        def._id = AppearanceXMLUtil::readName( presetNode, "id" );
        if ( def._id.empty() )
        {
            report.addError( "%#: <CharacterAppearance> without an id", sourceName );
            return false;
        }
        CharacterAppearanceInternal::readPreset( presetNode, def, report, sourceName );
        if ( report.getErrors().size() != errorCountBefore )
            return false;
        for ( CharacterAppearanceDef& existing : _listPreset )
        {
            if ( existing._id != def._id )
                continue;
            existing = std::move( def );
            return true;
        }
        _listPreset.push_back( std::move( def ) );
        return true;
    }

    bool CharacterAppearanceCatalog::writePresetToNode( const CharacterAppearanceDef& def, const XMLNode& outNode, string& outReason )
    {
        using Internal = CharacterAppearanceInternal;
        if ( def._id.empty() )
        {
            outReason = "a preset needs an id";
            return false;
        }
        for ( const CharacterAppearanceSlotDef& slot : def._listSlot )
        {
            if ( slot._customization.isEmpty() == false )
            {
                outReason = string( "slot '" ) + slot._slot.c_str() + "' has item instance values, which a preset asset cannot carry yet";
                return false;
            }
        }
        outNode.appendAttribute( "id", def._id.c_str() );
        Internal::appendName( outNode, "parent", def._parent );
        Internal::appendName( outNode, "schema", def._schema );
        Internal::appendNameList( outNode, "bodyType", def._listBodyType );
        Internal::appendNameList( outNode, "bodyShape", def._listBodyShape );
        Internal::appendNameList( outNode, "face", def._listFace );
        Internal::appendNameList( outNode, "body", def._listBody );
        string tags;
        for ( const TagID& tag : def._tags.getTags() )
        {
            if ( tags.empty() == false )
                tags += ',';
            tags += tag.getString();
        }
        if ( tags.empty() == false )
            outNode.appendAttribute( "tags", string_view( tags ) );
        for ( const CharacterAppearanceValueDef& value : def._listValue )
        {
            Internal::writeValue( outNode, value );
        }
        for ( const hashed_string& setID : def._listSet )
        {
            outNode.appendChild( "Equip" ).appendAttribute( "set", setID.c_str() );
        }
        for ( const CharacterAppearanceSlotDef& slot : def._listSlot )
        {
            Internal::writeSlot( outNode, slot );
        }
        for ( const AppearanceSocketOverride& socketOverride : def._listSocketOverride )
        {
            Internal::writeSocketOverride( outNode, socketOverride );
        }
        return true;
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
                return false; // 알 수 없는 부모
        }
        // 뿌리에서 잎 방향으로 처리한다.
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
        outSpec._presetID           = id;
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
        // 장비 구성은 레이어마다 세트, 슬롯 순서로 적용한다. 아래 레이어의 세트가 위 레이어의 슬롯을 덮어쓴다.
        vector<const CharacterAppearanceDef*> listChain;
        if ( collectChain( id, listChain ) == false )
            return false;
        vector<EquipSetPieceDef> listPiece;
        for ( const CharacterAppearanceDef* pLevel : listChain )
        {
            for ( const hashed_string& setID : pLevel->_listSet )
            {
                const EquipSetDef* pSet = sets.findSet( setID );
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
                    pRequest->_itemID = piece._listItem.front();
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
                    const hashed_string itemID = Internal::pickName( slot._listItem, seed, slot._slot.getHash() ^ Internal::kKeySlot );
                    if ( itemID != pRequest->_itemID )
                    {
                        *pRequest       = AppearanceSlotRequest{};
                        pRequest->_slot = slot._slot;
                    }
                    pRequest->_itemID = itemID;
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
