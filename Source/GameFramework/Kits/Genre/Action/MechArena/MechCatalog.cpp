#include "pch.h"

#include "GameFramework/Kits/Genre/Action/MechArena/MechCatalog.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

#include <optional>

namespace sw
{
    namespace
    {
        struct MechCatalogInternal
        {
            /** @brief `<Weapon>` 하나를 읽습니다. Melee · Special 은 `range` 를 적어야 합니다(종류에 따라 다른 기본값을 두지 않는다). */
            [[nodiscard]] static bool readWeapon( const XMLNode& node, string_view sourceName, MechWeaponSlotDef& outSlot )
            {
                bool bValid = GameDataXML::readFields( node, outSlot, sourceName, { "moves" } );
                GameDataXML::appendNameList( node.getAttributeText( "moves" ), ",; ", outSlot._listMoveID );
                if ( outSlot._kind != MechWeaponKind::Shot && node.findAttribute( "range" ) == nullptr )
                {
                    SW_LOG_ERROR( "%#: <Weapon id=\"%#\"> of kind Melee or Special needs range=\"...\"", sourceName, outSlot._id.c_str() );
                    bValid = false;
                }
                return bValid;
            }

            /** @brief @p parent 아래의 `<Weapon>` 을 모두 읽습니다. 하나라도 오류면 false 이고, 오류인 무기는 넣지 않습니다. */
            [[nodiscard]] static bool readWeapons( const XMLNode& parent, string_view sourceName, MechModeDef& outMode )
            {
                bool bValid = true;
                for ( XMLNode node = parent.findChild( "Weapon" ); node; node = node.findNextSibling( "Weapon" ) )
                {
                    MechWeaponSlotDef slot;
                    if ( readWeapon( node, sourceName, slot ) )
                        outMode._listWeapon.push_back( slot );
                    else
                        bValid = false;
                }
                return bValid;
            }

            /** @brief `<Mode>` 없이 무기를 바로 단 기체의 형태 "default" 를 읽습니다. 속도는 `<Mech speed>` 이고 적지 않으면 형태의 초기값입니다. */
            [[nodiscard]] static bool readDefaultMode( const XMLNode& node, string_view sourceName, MechModeDef& outMode )
            {
                outMode._id = hashed_string( "default" );
                std::optional<float32> speed;
                bool                   bValid = node.readOptionalAttribute( "speed", speed ) && ( speed.has_value() == false || *speed >= 0.0f );
                if ( bValid == false )
                    SW_LOG_ERROR( "%#: <Mech speed=\"%#\"> needs a number of 0 or more", sourceName, node.getAttributeText( "speed" ) );
                else if ( speed.has_value() )
                    outMode._speed = *speed;
                return readWeapons( node, sourceName, outMode ) && bValid;
            }
        };
    } // namespace

    SW_LOG_CALLER( "MechCatalog" );

    int32 MechDef::computeSlotOffset( int32 mode ) const
    {
        int32 offset = 0;
        for ( int32 index = 0; index < mode && index < static_cast<int32>( _listMode.size() ); ++index )
        {
            offset += static_cast<int32>( _listMode[static_cast<size_t>( index )]._listWeapon.size() );
        }
        return offset;
    }

    int32 MechDef::computeSlotCount() const
    {
        return computeSlotOffset( static_cast<int32>( _listMode.size() ) );
    }

    MechCatalog::MechCatalog()
        : _catalogMech{}
        , _catalogSkill{}
        , _arrClassModifier{}
        , _deckCostLimit{ 0 }
    {
    }

    const StatBlock& MechCatalog::getClassModifier( MechRangeClass rangeClass ) const
    {
        return _arrClassModifier[static_cast<size_t>( rangeClass )];
    }

    bool MechCatalog::parseRangeClass( string_view text, MechRangeClass& outRangeClass )
    {
        if ( StringUtil::equals( text, string_view( "Near" ) ) )
            outRangeClass = MechRangeClass::Near;
        else if ( StringUtil::equals( text, string_view( "Mid" ) ) )
            outRangeClass = MechRangeClass::Mid;
        else if ( StringUtil::equals( text, string_view( "Far" ) ) )
            outRangeClass = MechRangeClass::Far;
        else
            return false;
        return true;
    }

    uint32 MechCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        MechCatalogRootDef rootDef;
        if ( GameDataXML::readFields( root, rootDef, sourceName ) )
            _deckCostLimit = rootDef._deckCostLimit;

        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( StringUtil::equals( name, string_view( "Mech" ), true ) )
            {
                if ( loadMech( node, sourceName ) )
                    ++loadedCount;
            }
            else if ( StringUtil::equals( name, string_view( "Class" ), true ) )
            {
                if ( loadClass( node, sourceName ) == false )
                    SW_LOG_ERROR( "%#: <Class> was not read", sourceName );
            }
            else if ( StringUtil::equals( name, string_view( "Skill" ), true ) )
            {
                if ( loadSkill( node, sourceName ) == false )
                    SW_LOG_ERROR( "%#: <Skill> was not read", sourceName );
            }
            else
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        }
        return loadedCount;
    }

    bool MechCatalog::loadClass( const XMLNode& node, string_view sourceName )
    {
        const utf8*    pID        = GameDataXML::findRequiredID( node, sourceName );
        MechRangeClass rangeClass = MechRangeClass::Near;
        if ( pID == nullptr )
            return false;
        if ( parseRangeClass( string_view( pID ), rangeClass ) == false )
        {
            SW_LOG_ERROR( "%#: unknown mech class '%#' (Near, Mid, Far)", sourceName, pID );
            return false;
        }
        StatBlock& modifier = _arrClassModifier[static_cast<size_t>( rangeClass )];
        modifier.clear();
        modifier.loadFromAttributes( node, "id" ); // 속성이 없으면 보정이 없는 것이다
        return true;
    }

    bool MechCatalog::loadMech( const XMLNode& node, string_view sourceName )
    {
        using Internal = MechCatalogInternal;
        MechDef def;
        bool    bValid   = GameDataXML::readFields( node, def, sourceName, { "speed" } );
        XMLNode modeNode = node.findChild( "Mode" );
        if ( modeNode )
        {
            if ( node.findAttribute( "speed" ) != nullptr )
            {
                SW_LOG_ERROR( "%#: <Mech id=\"%#\" speed> belongs to each <Mode> when the mech has modes", sourceName, def._id.c_str() );
                bValid = false;
            }
            for ( ; modeNode; modeNode = modeNode.findNextSibling( "Mode" ) )
            {
                MechModeDef mode;
                const bool  bModeFields = GameDataXML::readFields( modeNode, mode, sourceName );
                if ( Internal::readWeapons( modeNode, sourceName, mode ) && bModeFields )
                    def._listMode.push_back( mode );
                else
                    bValid = false;
            }
        }
        else
        {
            MechModeDef mode;
            bValid = Internal::readDefaultMode( node, sourceName, mode ) && bValid;
            def._listMode.push_back( mode );
        }
        if ( bValid == false )
        {
            SW_LOG_ERROR( "%#: <Mech id=\"%#\"> has errors - not added", sourceName, def._id.c_str() );
            return false;
        }
        (void)_catalogMech.add( def ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        return true;
    }

    bool MechCatalog::loadSkill( const XMLNode& node, string_view sourceName )
    {
        MechSkillDef skill;
        if ( GameDataXML::readFields( node, skill, sourceName ) == false )
            return false;
        const XMLNode statNode = node.findChild( "Stats" );
        if ( statNode )
            skill._modifier.loadFromAttributes( statNode ); // 속성이 없으면 보정이 없는 것이다
        (void)_catalogSkill.add( skill );
        return true;
    }
} // namespace sw
