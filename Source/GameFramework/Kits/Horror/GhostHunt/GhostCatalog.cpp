#include "pch.h"

#include "GameFramework/Kits/Horror/GhostHunt/GhostCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "GhostCatalog" );

    namespace
    {
        struct GhostCatalogInternal
        {
            static constexpr uint32 kMaxStageCount = 16;

            static hashed_string readName( const XmlNode& node, const utf8* pName )
            {
                const utf8* pValue = node.findAttribute( pName );
                return pValue != nullptr && pValue[0] != '\0' ? hashed_string( pValue ) : hashed_string{};
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    GhostCatalog::GhostCatalog()
        : _flashlight{}
        , _vacuum{}
        , _ghostCatalog{}
        , _roomCatalog{}
        , _doorCatalog{}
        , _furnitureCatalog{}
        , _booCatalog{}
    {
        _vacuum._listStagePower.push_back( 10.0f );
    }

    uint32 GhostCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        const XmlNode flashlight = root.findChild( "Flashlight" );
        if ( flashlight )
        {
            _flashlight._range            = MathUtil::max( 0.1f, flashlight.getAttributeFloat( "range", _flashlight._range ) );
            _flashlight._halfAngle        = MathUtil::clamp( flashlight.getAttributeFloat( "angle", _flashlight._halfAngle ), 1.0f, 89.0f );
            _flashlight._strobeRange      = MathUtil::max( 0.1f, flashlight.getAttributeFloat( "strobeRange", _flashlight._strobeRange ) );
            _flashlight._strobeHalfAngle  = MathUtil::clamp( flashlight.getAttributeFloat( "strobeAngle", _flashlight._strobeHalfAngle ), 1.0f, 89.0f );
            _flashlight._strobeChargeTime = MathUtil::max( 0.0f, flashlight.getAttributeFloat( "strobeCharge", _flashlight._strobeChargeTime ) );
        }
        const XmlNode vacuum = root.findChild( "Vacuum" );
        if ( vacuum )
        {
            float32      arrStage[GhostCatalogInternal::kMaxStageCount] = {};
            const uint32 stageCount                                     = GameDataXml::parseFloats( vacuum.getAttributeText( "stages" ), arrStage, GhostCatalogInternal::kMaxStageCount );
            if ( stageCount > 0 )
            {
                _vacuum._listStagePower.clear();
                for ( uint32 stage = 0; stage < stageCount; ++stage )
                    _vacuum._listStagePower.push_back( MathUtil::max( 0.0f, arrStage[stage] ) );
            }
            _vacuum._range          = MathUtil::max( 0.1f, vacuum.getAttributeFloat( "range", _vacuum._range ) );
            _vacuum._alignThreshold = MathUtil::clamp( vacuum.getAttributeFloat( "alignThreshold", _vacuum._alignThreshold ), -1.0f, 1.0f );
            _vacuum._alignBonus     = MathUtil::max( 0.0f, vacuum.getAttributeFloat( "alignBonus", _vacuum._alignBonus ) );
            _vacuum._surgeFillTime  = MathUtil::max( 0.01f, vacuum.getAttributeFloat( "surgeFill", _vacuum._surgeFillTime ) );
            _vacuum._surgeDamage    = MathUtil::max( 0.0f, vacuum.getAttributeFloat( "surgeDamage", _vacuum._surgeDamage ) );
            _vacuum._dragSpeed      = MathUtil::max( 0.0f, vacuum.getAttributeFloat( "dragSpeed", _vacuum._dragSpeed ) );
            _vacuum._dragReduction  = MathUtil::clamp( vacuum.getAttributeFloat( "dragReduction", _vacuum._dragReduction ), 0.0f, 1.0f );
        }

        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Ghost" ); node; node = node.findNextSibling( "Ghost" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            GhostDef ghost;
            ghost._id           = hashed_string( pId );
            ghost._hp           = MathUtil::max( 1.0f, node.getAttributeFloat( "hp", ghost._hp ) );
            ghost._hideTime     = MathUtil::max( 0.0f, node.getAttributeFloat( "hideTime", ghost._hideTime ) );
            ghost._appearTime   = MathUtil::max( 0.0f, node.getAttributeFloat( "appearTime", ghost._appearTime ) );
            ghost._attackTime   = MathUtil::max( 0.05f, node.getAttributeFloat( "attackTime", ghost._attackTime ) );
            ghost._attackDamage = MathUtil::max( 0.0f, node.getAttributeFloat( "attackDamage", ghost._attackDamage ) );
            ghost._stunTime     = MathUtil::max( 0.1f, node.getAttributeFloat( "stunTime", ghost._stunTime ) );
            ghost._pull         = MathUtil::max( 0.0f, node.getAttributeFloat( "pull", ghost._pull ) );
            ghost._fleeInterval = MathUtil::max( 0.1f, node.getAttributeFloat( "fleeInterval", ghost._fleeInterval ) );
            ghost._coins        = MathUtil::max( 0, node.getAttributeInt( "coins", ghost._coins ) );
            ghost._bStrobeOnly  = node.getAttributeBool( "strobeOnly", false ) ? SW_TRUE : SW_FALSE;
            (void)_ghostCatalog.add( ghost );
            ++loadedCount;
        }
        for ( XmlNode node = root.findChild( "Room" ); node; node = node.findNextSibling( "Room" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            GhostRoomDef room;
            room._id        = hashed_string( pId );
            room._keyReward = GhostCatalogInternal::readName( node, "key" );
            room._lightFlag = GhostCatalogInternal::readName( node, "lightFlag" );
            if ( room._lightFlag.empty() )
            {
                string flag( "lit." );
                flag += pId;
                room._lightFlag = hashed_string( flag.c_str() );
            }
            GameDataXml::forEachToken( node.getAttributeText( "ghosts" ), ",; ", [&]( string_view token )
            {
                const hashed_string ghostId( string( token.data(), token.size() ).c_str() );
                if ( _ghostCatalog.find( ghostId ) == nullptr )
                    SW_LOG_WARNING( "%#: room '%#' has an unknown ghost '%#' - skipped", sourceName, pId, token );
                else
                    room._listGhost.push_back( ghostId );
            } );
            (void)_roomCatalog.add( room );
        }
        for ( XmlNode node = root.findChild( "Door" ); node; node = node.findNextSibling( "Door" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            GhostDoorDef door;
            door._id   = hashed_string( pId );
            door._key  = GhostCatalogInternal::readName( node, "key" );
            door._flag = GhostCatalogInternal::readName( node, "flag" );
            if ( door._flag.empty() )
                door._flag = door._id;
            (void)_doorCatalog.add( door );
        }
        for ( XmlNode node = root.findChild( "Furniture" ); node; node = node.findNextSibling( "Furniture" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            GhostFurnitureDef furniture;
            furniture._id                = hashed_string( pId );
            furniture._room              = GhostCatalogInternal::readName( node, "room" );
            furniture._lootTable         = GhostCatalogInternal::readName( node, "loot" );
            const string_view searchText = node.getAttributeText( "search" );
            if ( searchText.empty() == false )
            {
                furniture._bVacuum = SW_FALSE;
                furniture._bShake  = SW_FALSE;
                GameDataXml::forEachToken( searchText, ",; ", [&]( string_view token )
                {
                    if ( StringUtil::equals( token, string_view( "Vacuum" ), true ) )
                        furniture._bVacuum = SW_TRUE;
                    else if ( StringUtil::equals( token, string_view( "Shake" ), true ) )
                        furniture._bShake = SW_TRUE;
                    else
                        SW_LOG_WARNING( "%#: furniture '%#' has an unknown search mode '%#'", sourceName, pId, token );
                } );
            }
            (void)_furnitureCatalog.add( furniture );
        }
        for ( XmlNode node = root.findChild( "Boo" ); node; node = node.findNextSibling( "Boo" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            GhostBooDef boo;
            boo._id         = hashed_string( pId );
            boo._room       = GhostCatalogInternal::readName( node, "room" );
            boo._furniture  = GhostCatalogInternal::readName( node, "furniture" );
            boo._hp         = MathUtil::max( 1.0f, node.getAttributeFloat( "hp", boo._hp ) );
            boo._escapeTime = MathUtil::max( 0.1f, node.getAttributeFloat( "escapeTime", boo._escapeTime ) );
            (void)_booCatalog.add( boo );
        }
        return loadedCount;
    }
} // namespace sw
