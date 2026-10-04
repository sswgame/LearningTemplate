#include "pch.h"

#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternCatalog.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct WesternCatalogInternal
        {
            static hashed_string readName( const XmlNode& node, const utf8* pAttribute )
            {
                const utf8* pText = node.findAttribute( pAttribute );
                return pText != nullptr && pText[0] != '\0' ? hashed_string( pText ) : hashed_string{};
            }

            static uint8 readFlag( const XmlNode& node, const utf8* pAttribute, bool bFallback )
            {
                return node.getAttributeBool( pAttribute, bFallback ) ? SW_TRUE : SW_FALSE;
            }

            static uint8 parseSizeMask( string_view text )
            {
                uint8 mask = 0;
                GameDataXml::forEachToken( text, ",; ", [&]( string_view token )
                {
                    if ( StringUtil::equals( token, string_view( "Small" ), true ) )
                        mask |= 1u << static_cast<uint32>( WesternAnimalSize::Small );
                    else if ( StringUtil::equals( token, string_view( "Medium" ), true ) )
                        mask |= 1u << static_cast<uint32>( WesternAnimalSize::Medium );
                    else if ( StringUtil::equals( token, string_view( "Large" ), true ) )
                        mask |= 1u << static_cast<uint32>( WesternAnimalSize::Large );
                } );
                return mask;
            }

            static WesternAnimalSize parseSize( string_view text )
            {
                if ( StringUtil::equals( text, string_view( "Small" ), true ) )
                    return WesternAnimalSize::Small;
                if ( StringUtil::equals( text, string_view( "Large" ), true ) )
                    return WesternAnimalSize::Large;
                return WesternAnimalSize::Medium;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "WesternCatalog" );

    WesternCatalog::WesternCatalog()
        : _crimeCatalog{}
        , _regionCatalog{}
        , _horseCatalog{}
        , _foodCatalog{}
        , _clothingCatalog{}
        , _animalCatalog{}
        , _huntWeaponCatalog{}
        , _hitZoneCatalog{}
        , _honorActionCatalog{}
        , _listHonorTier{}
        , _listPursuit{}
        , _listBondLevel{}
        , _listDeadEyeLevel{}
        , _listGradeScale{}
        , _honorReputation{}
        , _bondExperience{}
        , _survival{}
        , _currency{ "Dollar" }
        , _extraHitPenalty{ 1 }
    {
        _listGradeScale = { 0.0f, 0.3f, 0.6f, 1.0f };
    }

    bool WesternCatalog::loadFromResource( string_view path )
    {
        return GameDataXml::loadFile( *this, &WesternCatalog::loadRoot, path, "WesternCatalog" );
    }

    bool WesternCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        return GameDataXml::loadText( *this, &WesternCatalog::loadRoot, xmlText, sourceName, "WesternCatalog" );
    }

    const WesternHonorTierDef* WesternCatalog::findHonorTier( const hashed_string& name ) const
    {
        for ( const WesternHonorTierDef& tier : _listHonorTier )
        {
            if ( tier._name == name )
                return &tier;
        }
        return nullptr;
    }

    const WesternPursuitDef* WesternCatalog::findPursuit( int32 wantedLevel ) const
    {
        const WesternPursuitDef* pFound = nullptr;
        for ( const WesternPursuitDef& pursuit : _listPursuit )
        {
            if ( pursuit._level <= wantedLevel )
                pFound = &pursuit;
        }
        return wantedLevel > 0 ? pFound : nullptr;
    }

    const WesternDeadEyeLevelDef* WesternCatalog::findDeadEyeLevel( int32 level ) const
    {
        const WesternDeadEyeLevelDef* pFound = nullptr;
        for ( const WesternDeadEyeLevelDef& deadEye : _listDeadEyeLevel )
        {
            if ( deadEye._level <= level )
                pFound = &deadEye;
        }
        return pFound;
    }

    float32 WesternCatalog::getGradeScale( int32 stars ) const
    {
        if ( stars <= 0 || _listGradeScale.empty() )
            return 0.0f;
        const size_t index = MathUtil::min( static_cast<size_t>( stars ), _listGradeScale.size() - 1 );
        return _listGradeScale[index];
    }

    uint32 WesternCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        const hashed_string currency = WesternCatalogInternal::readName( root, "currency" );
        if ( currency.empty() == false )
            _currency = currency;
        _extraHitPenalty   = MathUtil::max( 0, root.getAttributeInt( "extraHitPenalty", _extraHitPenalty ) );
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( StringUtil::equals( name, string_view( "Pursuit" ), true ) )
            {
                WesternPursuitDef pursuit;
                pursuit._name          = WesternCatalogInternal::readName( node, "name" );
                pursuit._level         = MathUtil::max( 1, node.getAttributeInt( "level", pursuit._level ) );
                pursuit._lawmen        = MathUtil::max( 0, node.getAttributeInt( "lawmen", pursuit._lawmen ) );
                pursuit._bShootOnSight = WesternCatalogInternal::readFlag( node, "shootOnSight", false );
                pursuit._bBountyHunter = WesternCatalogInternal::readFlag( node, "bountyHunter", false );
                _listPursuit.push_back( pursuit );
                ++loadedCount;
                continue;
            }
            if ( StringUtil::equals( name, string_view( "Honor" ), true ) )
            {
                loadHonor( node );
                ++loadedCount;
                continue;
            }
            if ( StringUtil::equals( name, string_view( "Bond" ), true ) )
            {
                WesternBondLevelDef bond;
                bond._level        = MathUtil::max( 1, node.getAttributeInt( "level", bond._level ) );
                bond._experience   = MathUtil::max( 0.0f, node.getAttributeFloat( "xp", bond._experience ) );
                bond._staminaBonus = node.getAttributeFloat( "stamina", 0.0f );
                bond._healthBonus  = node.getAttributeFloat( "health", 0.0f );
                bond._fearResist   = node.getAttributeFloat( "fearResist", 0.0f );
                GameDataXml::forEachToken( node.getAttributeText( "unlocks" ), ",; ", [&]( string_view token )
                { bond._listUnlock.push_back( hashed_string( token ) ); } );
                _listBondLevel.push_back( bond );
                ++loadedCount;
                continue;
            }
            if ( StringUtil::equals( name, string_view( "BondExperience" ), true ) )
            {
                _bondExperience._ridePerSecond      = MathUtil::max( 0.0f, node.getAttributeFloat( "ride", _bondExperience._ridePerSecond ) );
                _bondExperience._brush              = MathUtil::max( 0.0f, node.getAttributeFloat( "brush", _bondExperience._brush ) );
                _bondExperience._feed               = MathUtil::max( 0.0f, node.getAttributeFloat( "feed", _bondExperience._feed ) );
                _bondExperience._calm               = MathUtil::max( 0.0f, node.getAttributeFloat( "calm", _bondExperience._calm ) );
                _bondExperience._brushCooldownHours = MathUtil::max( 0.0f, node.getAttributeFloat( "brushCooldown", _bondExperience._brushCooldownHours ) );
                continue;
            }
            if ( StringUtil::equals( name, string_view( "Survival" ), true ) )
            {
                WesternSurvivalSettings& survival = _survival;
                survival._comfortMin              = node.getAttributeFloat( "comfortMin", survival._comfortMin );
                survival._comfortMax              = MathUtil::max( survival._comfortMin, node.getAttributeFloat( "comfortMax", survival._comfortMax ) );
                survival._coreDrainPerHour        = MathUtil::max( 0.0f, node.getAttributeFloat( "coreDrain", survival._coreDrainPerHour ) );
                survival._temperatureDrain        = MathUtil::max( 0.0f, node.getAttributeFloat( "temperatureDrain", survival._temperatureDrain ) );
                survival._minRegenScale           = MathUtil::saturate( node.getAttributeFloat( "minRegenScale", survival._minRegenScale ) );
                survival._health                  = MathUtil::max( 1.0f, node.getAttributeFloat( "health", survival._health ) );
                survival._stamina                 = MathUtil::max( 1.0f, node.getAttributeFloat( "stamina", survival._stamina ) );
                survival._deadEye                 = MathUtil::max( 1.0f, node.getAttributeFloat( "deadEye", survival._deadEye ) );
                survival._healthRegen             = MathUtil::max( 0.0f, node.getAttributeFloat( "healthRegen", survival._healthRegen ) );
                survival._staminaRegen            = MathUtil::max( 0.0f, node.getAttributeFloat( "staminaRegen", survival._staminaRegen ) );
                survival._deadEyeRegen            = MathUtil::max( 0.0f, node.getAttributeFloat( "deadEyeRegen", survival._deadEyeRegen ) );
                survival._deadEyeDrain            = MathUtil::max( 0.0f, node.getAttributeFloat( "deadEyeDrain", survival._deadEyeDrain ) );
                survival._deadEyeMinimum          = MathUtil::max( 0.0f, node.getAttributeFloat( "deadEyeMinimum", survival._deadEyeMinimum ) );
                continue;
            }
            if ( StringUtil::equals( name, string_view( "DeadEye" ), true ) )
            {
                WesternDeadEyeLevelDef deadEye;
                deadEye._level     = MathUtil::max( 1, node.getAttributeInt( "level", deadEye._level ) );
                deadEye._timeScale = MathUtil::clamp( node.getAttributeFloat( "timeScale", deadEye._timeScale ), 0.01f, 1.0f );
                deadEye._markCount = MathUtil::max( 0, node.getAttributeInt( "marks", deadEye._markCount ) );
                _listDeadEyeLevel.push_back( deadEye );
                ++loadedCount;
                continue;
            }
            if ( StringUtil::equals( name, string_view( "PeltGrade" ), true ) )
            {
                float32      arrScale[4] = { 0.0f, 0.3f, 0.6f, 1.0f };
                const uint32 count       = GameDataXml::parseFloats( node.getAttributeText( "scales" ), arrScale, 4 );
                if ( count < 4 )
                    SW_LOG_WARNING( "%#: <PeltGrade> needs four scales (0..3 stars) - missing ones keep the default", sourceName );
                _listGradeScale.assign( arrScale, arrScale + 4 );
                continue;
            }

            const bool bKnown = StringUtil::equals( name, string_view( "Crime" ), true ) || StringUtil::equals( name, string_view( "Region" ), true ) ||
                                StringUtil::equals( name, string_view( "Horse" ), true ) || StringUtil::equals( name, string_view( "Food" ), true ) ||
                                StringUtil::equals( name, string_view( "Clothing" ), true ) || StringUtil::equals( name, string_view( "Animal" ), true ) ||
                                StringUtil::equals( name, string_view( "HuntWeapon" ), true ) || StringUtil::equals( name, string_view( "HitZone" ), true );
            if ( bKnown == false )
            {
                SW_LOG_WARNING( "%#: unknown element <%#> - skipped", sourceName, name );
                continue;
            }
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            const hashed_string id( pId );
            ++loadedCount;
            if ( StringUtil::equals( name, string_view( "Crime" ), true ) )
            {
                WesternCrimeDef crime;
                crime._id         = id;
                crime._reportTime = MathUtil::max( 0.0f, node.getAttributeFloat( "reportTime", crime._reportTime ) );
                crime._bounty     = MathUtil::max( 0, node.getAttributeInt( "bounty", crime._bounty ) );
                crime._wanted     = MathUtil::max( 0, node.getAttributeInt( "wanted", crime._wanted ) );
                crime._honor      = node.getAttributeInt( "honor", crime._honor );
                (void)_crimeCatalog.add( crime );
            }
            else if ( StringUtil::equals( name, string_view( "Region" ), true ) )
            {
                WesternRegionDef region;
                region._id                = id;
                region._wantedCooldown    = MathUtil::max( 0.1f, node.getAttributeFloat( "wantedCooldown", region._wantedCooldown ) );
                region._disguiseScale     = MathUtil::max( 1.0f, node.getAttributeFloat( "disguiseScale", region._disguiseScale ) );
                region._maskedBountyScale = MathUtil::max( 0.0f, node.getAttributeFloat( "maskedBountyScale", region._maskedBountyScale ) );
                region._bountyDecayPerDay = MathUtil::max( 0, node.getAttributeInt( "bountyDecayPerDay", region._bountyDecayPerDay ) );
                region._maxWanted         = MathUtil::max( 1, node.getAttributeInt( "maxWanted", region._maxWanted ) );
                (void)_regionCatalog.add( region );
            }
            else if ( StringUtil::equals( name, string_view( "Horse" ), true ) )
            {
                WesternHorseDef horse;
                horse._id                     = id;
                horse._health                 = MathUtil::max( 1.0f, node.getAttributeFloat( "health", horse._health ) );
                horse._stamina                = MathUtil::max( 1.0f, node.getAttributeFloat( "stamina", horse._stamina ) );
                horse._speed                  = MathUtil::max( 0.0f, node.getAttributeFloat( "speed", horse._speed ) );
                horse._courage                = MathUtil::saturate( node.getAttributeFloat( "courage", horse._courage ) );
                horse._gallopDrain            = MathUtil::max( 0.0f, node.getAttributeFloat( "gallopDrain", horse._gallopDrain ) );
                horse._coreDrainPerHour       = MathUtil::max( 0.0f, node.getAttributeFloat( "coreDrain", horse._coreDrainPerHour ) );
                horse._gallopCoreDrainPerHour = MathUtil::max( 0.0f, node.getAttributeFloat( "gallopCoreDrain", horse._gallopCoreDrainPerHour ) );
                (void)_horseCatalog.add( horse );
            }
            else if ( StringUtil::equals( name, string_view( "Food" ), true ) )
            {
                WesternFoodDef food;
                food._id          = id;
                food._healthCore  = node.getAttributeFloat( "healthCore", 0.0f );
                food._staminaCore = node.getAttributeFloat( "staminaCore", 0.0f );
                food._deadEyeCore = node.getAttributeFloat( "deadEyeCore", 0.0f );
                food._health      = node.getAttributeFloat( "health", 0.0f );
                food._stamina     = node.getAttributeFloat( "stamina", 0.0f );
                food._bond        = MathUtil::max( 0.0f, node.getAttributeFloat( "bond", 0.0f ) );
                (void)_foodCatalog.add( food );
            }
            else if ( StringUtil::equals( name, string_view( "Clothing" ), true ) )
            {
                WesternClothingDef clothing;
                clothing._id     = id;
                clothing._warmth = node.getAttributeFloat( "warmth", 0.0f );
                (void)_clothingCatalog.add( clothing );
            }
            else if ( StringUtil::equals( name, string_view( "Animal" ), true ) )
            {
                WesternAnimalDef animal;
                animal._id           = id;
                animal._lootTable    = WesternCatalogInternal::readName( node, "loot" );
                animal._size         = WesternCatalogInternal::parseSize( node.getAttributeText( "size" ) );
                animal._quality      = MathUtil::clamp( node.getAttributeInt( "quality", animal._quality ), 1, 3 );
                animal._peltPrice    = MathUtil::max( 0.0f, node.getAttributeFloat( "pelt", animal._peltPrice ) );
                animal._carcassPrice = MathUtil::max( 0.0f, node.getAttributeFloat( "carcass", animal._carcassPrice ) );
                animal._decayHours   = MathUtil::max( 0.1f, node.getAttributeFloat( "decayHours", animal._decayHours ) );
                (void)_animalCatalog.add( animal );
            }
            else if ( StringUtil::equals( name, string_view( "HuntWeapon" ), true ) )
            {
                WesternHuntWeaponDef weapon;
                weapon._id              = id;
                const string_view sizes = node.getAttributeText( "sizes" );
                weapon._sizeMask        = sizes.empty() ? static_cast<uint8>( 0x7 ) : WesternCatalogInternal::parseSizeMask( sizes );
                weapon._bRuinsPelt      = WesternCatalogInternal::readFlag( node, "ruinsPelt", false );
                (void)_huntWeaponCatalog.add( weapon );
            }
            else
            {
                WesternHitZoneDef zone;
                zone._id      = id;
                zone._penalty = MathUtil::max( 0, node.getAttributeInt( "penalty", zone._penalty ) );
                (void)_hitZoneCatalog.add( zone );
            }
        }

        const auto byPursuitLevel = []( const WesternPursuitDef& lhs, const WesternPursuitDef& rhs )
        { return lhs._level < rhs._level; };
        const auto byBondLevel = []( const WesternBondLevelDef& lhs, const WesternBondLevelDef& rhs )
        { return lhs._level < rhs._level; };
        const auto byDeadEyeLevel = []( const WesternDeadEyeLevelDef& lhs, const WesternDeadEyeLevelDef& rhs )
        { return lhs._level < rhs._level; };
        std::stable_sort( _listPursuit.begin(), _listPursuit.end(), byPursuitLevel );
        std::stable_sort( _listBondLevel.begin(), _listBondLevel.end(), byBondLevel );
        std::stable_sort( _listDeadEyeLevel.begin(), _listDeadEyeLevel.end(), byDeadEyeLevel );
        return loadedCount;
    }

    void WesternCatalog::loadHonor( const XmlNode& node )
    {
        FactionDef faction;
        faction._id         = hashed_string( kHonorFactionId );
        faction._name       = "Honor";
        faction._minValue   = node.getAttributeInt( "min", faction._minValue );
        faction._maxValue   = MathUtil::max( faction._minValue, node.getAttributeInt( "max", faction._maxValue ) );
        faction._startValue = MathUtil::clamp( node.getAttributeInt( "start", 0 ), faction._minValue, faction._maxValue );
        _listHonorTier.clear();
        for ( XmlNode child = node.findChild( "Tier" ); child; child = child.findNextSibling( "Tier" ) )
        {
            ReputationTier tier;
            tier._name     = WesternCatalogInternal::readName( child, "name" );
            tier._minValue = child.getAttributeInt( "min", faction._minValue );
            faction._listTier.push_back( tier );
            WesternHonorTierDef effect;
            effect._name         = tier._name;
            effect._dialogueFlag = WesternCatalogInternal::readName( child, "flag" );
            effect._shopDiscount = MathUtil::clamp( child.getAttributeFloat( "discount", 0.0f ), -1.0f, 1.0f );
            _listHonorTier.push_back( effect );
        }
        std::stable_sort( faction._listTier.begin(), faction._listTier.end(), []( const ReputationTier& lhs, const ReputationTier& rhs )
        { return lhs._minValue < rhs._minValue; } );
        for ( XmlNode child = node.findChild( "Action" ); child; child = child.findNextSibling( "Action" ) )
        {
            const utf8* pId = child.findAttribute( "id" );
            if ( pId == nullptr || pId[0] == '\0' )
                continue;
            WesternHonorActionDef action;
            action._id    = hashed_string( pId );
            action._delta = child.getAttributeInt( "delta", 0 );
            (void)_honorActionCatalog.add( action );
        }
        _honorReputation.addFaction( faction );
    }
} // namespace sw
