#include "pch.h"

#include "GameFramework/Base/Combat/Weapon.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"

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
        : _catalog{}
    {
    }

    void WeaponCatalog::addWeapon( const WeaponDef& weapon )
    {
        (void)_catalog.add( weapon ); // 빈 id 는 카탈로그가 거른다
    }

    uint32 WeaponCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Weapon" ); node; node = node.findNextSibling( "Weapon" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            WeaponDef weapon;
            weapon._id                 = hashed_string( pId );
            const utf8* pName          = node.findAttribute( "name" );
            weapon._name               = pName != nullptr ? pName : pId;
            weapon._fireInterval       = MathUtil::max( 0.01f, node.getAttributeFloat( "fireInterval", weapon._fireInterval ) );
            weapon._reloadTime         = MathUtil::max( 0.0f, node.getAttributeFloat( "reloadTime", weapon._reloadTime ) );
            weapon._damage             = node.getAttributeFloat( "damage", weapon._damage );
            weapon._range              = MathUtil::max( 0.1f, node.getAttributeFloat( "range", weapon._range ) );
            weapon._minSpread          = MathUtil::max( 0.0f, node.getAttributeFloat( "minSpread", weapon._minSpread ) );
            weapon._maxSpread          = MathUtil::max( weapon._minSpread, node.getAttributeFloat( "maxSpread", weapon._maxSpread ) );
            weapon._spreadPerShot      = MathUtil::max( 0.0f, node.getAttributeFloat( "spreadPerShot", weapon._spreadPerShot ) );
            weapon._spreadRecovery     = MathUtil::max( 0.0f, node.getAttributeFloat( "spreadRecovery", weapon._spreadRecovery ) );
            weapon._recoilPitch        = node.getAttributeFloat( "recoilPitch", weapon._recoilPitch );
            weapon._magazineSize       = MathUtil::max( 1, node.getAttributeInt( "magazineSize", weapon._magazineSize ) );
            weapon._maxReserveAmmo     = MathUtil::max( 0, node.getAttributeInt( "maxReserveAmmo", weapon._maxReserveAmmo ) );
            weapon._pelletCount        = MathUtil::max( 1, node.getAttributeInt( "pelletCount", weapon._pelletCount ) );
            weapon._bAutomatic         = node.getAttributeBool( "automatic", true ) ? SW_TRUE : SW_FALSE;
            weapon._falloffStart       = MathUtil::max( 0.0f, node.getAttributeFloat( "falloffStart", weapon._falloffStart ) );
            weapon._falloffEnd         = MathUtil::max( weapon._falloffStart, node.getAttributeFloat( "falloffEnd", weapon._falloffEnd ) );
            weapon._falloffMinScale    = MathUtil::clamp( node.getAttributeFloat( "falloffMinScale", weapon._falloffMinScale ), 0.0f, 1.0f );
            weapon._projectileSpeed    = MathUtil::max( 0.0f, node.getAttributeFloat( "projectileSpeed", weapon._projectileSpeed ) );
            weapon._projectileGravity  = node.getAttributeFloat( "projectileGravity", weapon._projectileGravity );
            weapon._headshotMultiplier = MathUtil::max( 0.0f, node.getAttributeFloat( "headshotMultiplier", weapon._headshotMultiplier ) );
            const utf8* pAmmoId        = node.findAttribute( "ammo" );
            weapon._ammoId             = pAmmoId != nullptr ? hashed_string( pAmmoId ) : hashed_string{};
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
        , _cooldown{}
        , _reload{}
        , _currentSpread{ 0.0f }
        , _magazineAmmo{ 0 }
        , _reserveAmmo{ 0 }
    {
    }

    void WeaponState::equip( const WeaponDef& weapon, int32 reserveAmmo, uint32 spreadSeed )
    {
        _def = weapon;
        _random.setSeed( spreadSeed );
        _cooldown.clear();
        _reload.clear();
        _currentSpread = weapon._minSpread;
        _magazineAmmo  = weapon._magazineSize;
        _reserveAmmo   = MathUtil::clamp( reserveAmmo, 0, weapon._maxReserveAmmo );
    }

    void WeaponState::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        // 이번 프레임 안에서 준비된 총만 늦은 몫을 든다. 이미 준비돼 있던 총(쉬는 중)은 0 에 머문다 — 쉰 시간을 다음 발로 잇지 않는다.
        _cooldown.tick( deltaTime );
        _currentSpread = MathUtil::max( _def._minSpread, _currentSpread - _def._spreadRecovery * deltaTime );

        if ( _reload.tick( deltaTime ) )
        {
            // 재장전 끝 — 예비탄에서 빈 만큼만 옮긴다.
            const int32 needed = _def._magazineSize - _magazineAmmo;
            const int32 moved  = MathUtil::min( needed, _reserveAmmo );
            _magazineAmmo += moved;
            _reserveAmmo -= moved;
        }
    }

    WeaponFireResult WeaponState::pullTrigger( const GameRay& aim, bool bTriggerJustPressed, WeaponShot& outShot )
    {
        outShot._listRay.clear();
        outShot._spreadAtFire = _currentSpread;
        if ( _reload.isActive() )
            return WeaponFireResult::Reloading;
        if ( _def._bAutomatic == SW_FALSE && bTriggerJustPressed == false )
            return WeaponFireResult::SemiAutoHeld;
        if ( _cooldown.isActive() )
            return WeaponFireResult::Cooling;
        if ( _magazineAmmo <= 0 )
        {
            if ( _reserveAmmo <= 0 )
                return WeaponFireResult::OutOfAmmo;
            (void)startReload(); // 빈 탄창으로 당기면 재장전한다(예비탄이 있음을 위에서 봤다)
            return WeaponFireResult::EmptyMagazine;
        }

        --_magazineAmmo;
        // 늦은 몫을 다음 간격에서 뺀다(연사가 fps 에 매이지 않는다). 몫은 한 간격까지만 — 한 번 당기면 한 발이다.
        _cooldown.restart( _def._fireInterval );
        const float32 coneHalfAngle = _currentSpread * WeaponInternal::kDegreeToRadian;
        for ( int32 pelletIndex = 0; pelletIndex < _def._pelletCount; ++pelletIndex )
        {
            GameRay ray;
            ray._origin    = aim._origin;
            ray._direction = WeaponMath::applySpread( aim._direction, coneHalfAngle, _random );
            outShot._listRay.push_back( ray );
        }
        _currentSpread = MathUtil::min( _def._maxSpread, _currentSpread + _def._spreadPerShot );
        return WeaponFireResult::Fired;
    }

    bool WeaponState::startReload()
    {
        const bool bCannotReload = _reload.isActive() || _magazineAmmo >= _def._magazineSize || _reserveAmmo <= 0;
        if ( bCannotReload )
            return false;
        _reload.start( MathUtil::max( 1.0e-3f, _def._reloadTime ) );
        return true;
    }

    void WeaponState::addReserveAmmo( int32 amount )
    {
        if ( amount > 0 )
            _reserveAmmo = MathUtil::min( _def._maxReserveAmmo, _reserveAmmo + amount );
    }
} // namespace sw
