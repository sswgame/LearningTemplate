#include "pch.h"

#include "GameFramework/Kits/Shooter/Weapon.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    SW_LOG_CALLER( "Weapon" );

    namespace
    {
        struct WeaponInternal
        {
            static constexpr float32 kDegreeToRadian = 3.14159265358979f / 180.0f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // WeaponCatalog
    // ------------------------------------------------------------------------------
    WeaponCatalog::WeaponCatalog()
        : _listWeapon{}
    {
    }

    bool WeaponCatalog::loadFromResource( string_view path )
    {
        XmlDocument doc;
        string      absPath;
        if ( doc.loadPath( path, &absPath ) == false )
        {
            SW_LOG_WARNING( "Failed to read weapon catalog %#", path );
            return false;
        }
        const XmlNode root = doc.getRoot( "WeaponCatalog" );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <WeaponCatalog> root in %#", absPath );
            return false;
        }
        return loadRoot( root, absPath ) > 0;
    }

    bool WeaponCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_WARNING( "Failed to parse weapon catalog text %#", sourceName );
            return false;
        }
        const XmlNode root = doc.getRoot( "WeaponCatalog" );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Missing <WeaponCatalog> root in %#", sourceName );
            return false;
        }
        return loadRoot( root, sourceName ) > 0;
    }

    void WeaponCatalog::addWeapon( const WeaponDef& weapon )
    {
        for ( WeaponDef& existing : _listWeapon )
        {
            if ( existing._id == weapon._id )
            {
                existing = weapon;
                return;
            }
        }
        _listWeapon.push_back( weapon );
    }

    const WeaponDef* WeaponCatalog::findWeapon( const hashed_string& id ) const
    {
        for ( const WeaponDef& weapon : _listWeapon )
        {
            if ( weapon._id == id )
                return &weapon;
        }
        return nullptr;
    }

    uint32 WeaponCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Weapon" ); node; node = node.findNextSibling( "Weapon" ) )
        {
            const utf8* pId = node.findAttribute( "id" );
            if ( StringUtil::isNullOrEmpty( pId ) )
            {
                SW_LOG_WARNING( "%#: <Weapon> without an id - skipped", sourceName );
                continue;
            }
            WeaponDef weapon;
            weapon._id             = hashed_string( pId );
            const utf8* pName      = node.findAttribute( "name" );
            weapon._name           = pName != nullptr ? pName : pId;
            weapon._fireInterval   = MathUtil::max( 0.01f, node.getAttributeFloat( "fireInterval", weapon._fireInterval ) );
            weapon._reloadTime     = MathUtil::max( 0.0f, node.getAttributeFloat( "reloadTime", weapon._reloadTime ) );
            weapon._damage         = node.getAttributeFloat( "damage", weapon._damage );
            weapon._range          = MathUtil::max( 0.1f, node.getAttributeFloat( "range", weapon._range ) );
            weapon._minSpread      = MathUtil::max( 0.0f, node.getAttributeFloat( "minSpread", weapon._minSpread ) );
            weapon._maxSpread      = MathUtil::max( weapon._minSpread, node.getAttributeFloat( "maxSpread", weapon._maxSpread ) );
            weapon._spreadPerShot  = MathUtil::max( 0.0f, node.getAttributeFloat( "spreadPerShot", weapon._spreadPerShot ) );
            weapon._spreadRecovery = MathUtil::max( 0.0f, node.getAttributeFloat( "spreadRecovery", weapon._spreadRecovery ) );
            weapon._recoilPitch    = node.getAttributeFloat( "recoilPitch", weapon._recoilPitch );
            weapon._magazineSize   = MathUtil::max( 1, node.getAttributeInt( "magazineSize", weapon._magazineSize ) );
            weapon._maxReserveAmmo = MathUtil::max( 0, node.getAttributeInt( "maxReserveAmmo", weapon._maxReserveAmmo ) );
            weapon._pelletCount    = MathUtil::max( 1, node.getAttributeInt( "pelletCount", weapon._pelletCount ) );
            weapon._bAutomatic     = node.getAttributeBool( "automatic", true ) ? SW_TRUE : SW_FALSE;
            addWeapon( weapon );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Weapon> entries", sourceName );
        return loadedCount;
    }

    // ------------------------------------------------------------------------------
    // WeaponState
    // ------------------------------------------------------------------------------
    WeaponState::WeaponState()
        : _def{}
        , _random{ 1u }
        , _cooldown{ 0.0f }
        , _reloadRemaining{ 0.0f }
        , _currentSpread{ 0.0f }
        , _magazineAmmo{ 0 }
        , _reserveAmmo{ 0 }
    {
    }

    void WeaponState::equip( const WeaponDef& weapon, int32 reserveAmmo, uint32 spreadSeed )
    {
        _def = weapon;
        _random.setSeed( spreadSeed );
        _cooldown        = 0.0f;
        _reloadRemaining = 0.0f;
        _currentSpread   = weapon._minSpread;
        _magazineAmmo    = weapon._magazineSize;
        _reserveAmmo     = MathUtil::clamp( reserveAmmo, 0, weapon._maxReserveAmmo );
    }

    void WeaponState::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        _cooldown      = MathUtil::max( 0.0f, _cooldown - deltaTime );
        _currentSpread = MathUtil::max( _def._minSpread, _currentSpread - _def._spreadRecovery * deltaTime );

        if ( _reloadRemaining > 0.0f )
        {
            _reloadRemaining -= deltaTime;
            if ( _reloadRemaining <= 0.0f )
            {
                // 재장전 끝 — 예비탄에서 빈 만큼만 옮긴다.
                _reloadRemaining   = 0.0f;
                const int32 needed = _def._magazineSize - _magazineAmmo;
                const int32 moved  = MathUtil::min( needed, _reserveAmmo );
                _magazineAmmo += moved;
                _reserveAmmo -= moved;
            }
        }
    }

    WeaponFireResult WeaponState::pullTrigger( const ShooterRay& aim, bool bTriggerJustPressed, WeaponShot& outShot )
    {
        outShot._listRay.clear();
        outShot._spreadAtFire = _currentSpread;
        if ( _reloadRemaining > 0.0f )
            return WeaponFireResult::Reloading;
        if ( _def._bAutomatic == SW_FALSE && bTriggerJustPressed == false )
            return WeaponFireResult::SemiAutoHeld;
        if ( _cooldown > 0.0f )
            return WeaponFireResult::Cooling;
        if ( _magazineAmmo <= 0 )
        {
            if ( _reserveAmmo <= 0 )
                return WeaponFireResult::OutOfAmmo;
            (void)startReload(); // 빈 탄창으로 당기면 재장전한다(예비탄이 있음을 위에서 봤다)
            return WeaponFireResult::EmptyMagazine;
        }

        --_magazineAmmo;
        _cooldown                   = _def._fireInterval;
        const float32 coneHalfAngle = _currentSpread * WeaponInternal::kDegreeToRadian;
        for ( int32 pelletIndex = 0; pelletIndex < _def._pelletCount; ++pelletIndex )
        {
            ShooterRay ray;
            ray._origin    = aim._origin;
            ray._direction = ShooterMath::applySpread( aim._direction, coneHalfAngle, _random );
            outShot._listRay.push_back( ray );
        }
        _currentSpread = MathUtil::min( _def._maxSpread, _currentSpread + _def._spreadPerShot );
        return WeaponFireResult::Fired;
    }

    bool WeaponState::startReload()
    {
        const bool bCannotReload = _reloadRemaining > 0.0f || _magazineAmmo >= _def._magazineSize || _reserveAmmo <= 0;
        if ( bCannotReload )
            return false;
        _reloadRemaining = MathUtil::max( 1.0e-3f, _def._reloadTime );
        return true;
    }

    void WeaponState::addReserveAmmo( int32 amount )
    {
        if ( amount > 0 )
            _reserveAmmo = MathUtil::min( _def._maxReserveAmmo, _reserveAmmo + amount );
    }
} // namespace sw
