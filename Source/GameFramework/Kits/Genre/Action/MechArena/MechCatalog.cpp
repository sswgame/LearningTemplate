#include "pch.h"

#include "GameFramework/Kits/Genre/Action/MechArena/MechCatalog.h"

#include "Core/Foundation/Expected.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct MechCatalogInternal
        {
            /** @brief `<Weapon>` 하나를 읽습니다. Melee 는 `range` 를, Special 은 `projectile` 을 적어야 합니다(종류에 따라 다른 기본값을 두지 않는다). */
            [[nodiscard]] static bool readWeapon( const XMLNode& node, string_view sourceName, MechWeaponSlotDef& outSlot )
            {
                bool bValid = XMLFields::readProperties( node, outSlot, sourceName );
                if ( outSlot._kind == MechWeaponKind::Melee && node.findAttribute( "range" ) == nullptr )
                {
                    SW_LOG_ERROR_ID( "MechCatalog.WeaponIdKindMelee", "%#: <Weapon id=\"%#\"> of kind Melee needs range=\"...\"", sourceName, outSlot._id.c_str() );
                    bValid = false;
                }
                if ( outSlot._kind == MechWeaponKind::Special && outSlot._projectileID.empty() )
                {
                    SW_LOG_ERROR_ID( "MechCatalog.SpecialNeedsProjectile", "%#: <Weapon id=\"%#\"> of kind Special needs projectile=\"...\" (a ProjectileCatalog id)", sourceName, outSlot._id.c_str() );
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
                optional<float32> speed;
                bool              bValid = node.readOptionalAttribute( "speed", speed ) && ( speed.has_value() == false || *speed >= 0.0f );
                if ( bValid == false )
                    SW_LOG_ERROR_ID( "MechCatalog.MechSpeedNeedsNumber", "%#: <Mech speed=\"%#\"> needs a number of 0 or more", sourceName, node.getAttributeText( "speed" ) );
                else if ( speed.has_value() )
                    outMode._speed = *speed;
                return readWeapons( node, sourceName, outMode ) && bValid;
            }
        };
    } // namespace

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

    const MechWeaponSlotDef* MechDef::findSlot( int32 slotIndex ) const
    {
        int32 offset = 0;
        for ( const MechModeDef& mode : _listMode )
        {
            const int32 count = static_cast<int32>( mode._listWeapon.size() );
            if ( 0 <= slotIndex - offset && slotIndex - offset < count )
                return &mode._listWeapon[static_cast<size_t>( slotIndex - offset )];
            offset += count;
        }
        return nullptr;
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
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        // 루트가 틀리면 파일을 읽지 않는다 — 코스트 상한이 "제한 없음" 으로 남은 채 판이 열리지 않게 한다.
        MechCatalogRootDef rootDef;
        if ( XMLFields::readProperties( root, rootDef, sourceName ) == false )
            return 0;
        _deckCostLimit = rootDef._deckCostLimit;

        using Element                          = XMLCatalogElement<MechCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&MechCatalog::loadMech>( "Mech" ),
            Element::of<&MechCatalog::loadClass>( "Class" ).uncounted(),
            Element::of<&MechCatalog::loadSkill>( "Skill" ).uncounted(),
        };
        return XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement )._loadedCount;
    }

    bool MechCatalog::loadClass( const XMLNode& node, string_view sourceName )
    {
        const utf8*    pID        = XMLReadUtil::findRequiredID( node, sourceName );
        MechRangeClass rangeClass = MechRangeClass::Near;
        if ( pID == nullptr )
            return false;
        if ( parseRangeClass( string_view( pID ), rangeClass ) == false )
        {
            SW_LOG_ERROR_ID( "MechCatalog.UnknownMechClassNear", "%#: unknown mech class '%#' (Near, Mid, Far)", sourceName, pID );
            return false;
        }
        StatBlock& modifier = _arrClassModifier[static_cast<size_t>( rangeClass )];
        modifier.clear();
        modifier.addAttributes( node, "id" ); // 속성이 없으면 보정이 없는 것이다
        return true;
    }

    bool MechCatalog::loadMech( const XMLNode& node, string_view sourceName )
    {
        using Internal = MechCatalogInternal;
        MechDef def;
        bool    bValid   = XMLFields::readProperties( node, def, sourceName, { "speed" } );
        XMLNode modeNode = node.findChild( "Mode" );
        if ( modeNode )
        {
            if ( node.findAttribute( "speed" ) != nullptr )
            {
                SW_LOG_ERROR_ID( "MechCatalog.MechIdSpeedBelongs", "%#: <Mech id=\"%#\" speed> belongs to each <Mode> when the mech has modes", sourceName, def._id.c_str() );
                bValid = false;
            }
            if ( node.findChild( "Weapon" ) )
            {
                SW_LOG_ERROR_ID( "MechCatalog.MechWeaponOutsideMode", "%#: <Mech id=\"%#\"> has a <Weapon> outside its <Mode> - it would be dropped", sourceName, def._id.c_str() );
                bValid = false;
            }
            for ( ; modeNode; modeNode = modeNode.findNextSibling( "Mode" ) )
            {
                MechModeDef mode;
                const bool  bModeProperties = XMLFields::readProperties( modeNode, mode, sourceName );
                if ( Internal::readWeapons( modeNode, sourceName, mode ) && bModeProperties )
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
            SW_LOG_ERROR_ID( "MechCatalog.MechIdErrorsNot", "%#: <Mech id=\"%#\"> has errors - not added", sourceName, def._id.c_str() );
            return false;
        }
        return GameDataXML::addUnique( def, sourceName, _catalogMech );
    }

    bool MechCatalog::loadSkill( const XMLNode& node, string_view sourceName )
    {
        MechSkillDef skill;
        if ( XMLFields::readProperties( node, skill, sourceName ) == false )
            return false;
        const XMLNode statNode = node.findChild( "Stats" );
        if ( statNode )
            skill._modifier.addAttributes( statNode ); // 속성이 없으면 보정이 없는 것이다
        return GameDataXML::addUnique( skill, sourceName, _catalogSkill );
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<MechCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( MechCatalog, MechCatalog, SW_KIT_API, MechCatalog::kAssetType.withLoader( AssetTypeLoader::of<MechCatalog>( MemoryTag::Game ) ) );
} // namespace sw
