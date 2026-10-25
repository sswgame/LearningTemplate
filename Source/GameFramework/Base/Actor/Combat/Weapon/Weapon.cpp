#include "pch.h"

#include "GameFramework/Base/Actor/Combat/Weapon/Weapon.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    SW_LOG_CALLER( "Weapon" );

    namespace
    {
        struct WeaponInternal
        {
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // WeaponCatalog
    // ------------------------------------------------------------------------------
    WeaponCatalog::WeaponCatalog()
    {
    }

    uint32 WeaponCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Weapon" ); node; node = node.findNextSibling( "Weapon" ) )
        {
            const utf8* pID = GameDataXML::findRequiredID( node, sourceName );
            if ( pID == nullptr )
                continue;
            WeaponDef weapon;
            weapon._id        = hashed_string( pID );
            const utf8* pName = node.findAttribute( "name" );
            weapon._name      = pName != nullptr ? pName : pID;
            // 적지 않은 칸은 WeaponDef 의 초기값이다. 깨진 값 · 범위 밖 값은 그 무기를 넣지 않는 오류다(자르지 않는다).
            constexpr int32   kAnyInt   = MathUtil::kMaxInt32;
            constexpr float32 kAnyFloat = MathUtil::kMaxFloat;
            bool              bValid    = GameDataXML::readOptionalNumber( node, "fireInterval", sourceName, WeaponDef::kMinFireInterval, kAnyFloat, weapon._fireInterval );
            bValid                      = GameDataXML::readOptionalNumber( node, "reloadTime", sourceName, 0.0f, kAnyFloat, weapon._reloadTime ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "damage", sourceName, -kAnyFloat, kAnyFloat, weapon._damage ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "range", sourceName, WeaponDef::kMinRange, kAnyFloat, weapon._range ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "minSpread", sourceName, 0.0f, kAnyFloat, weapon._minSpread ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "maxSpread", sourceName, 0.0f, kAnyFloat, weapon._maxSpread ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "spreadPerShot", sourceName, 0.0f, kAnyFloat, weapon._spreadPerShot ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "spreadRecovery", sourceName, 0.0f, kAnyFloat, weapon._spreadRecovery ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "recoilPitch", sourceName, -kAnyFloat, kAnyFloat, weapon._recoilPitch ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "magazineSize", sourceName, 1, kAnyInt, weapon._magazineSize ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "maxReserveAmmo", sourceName, 0, kAnyInt, weapon._maxReserveAmmo ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "pelletCount", sourceName, 1, kAnyInt, weapon._pelletCount ) && bValid;
            bValid                      = GameDataXML::readOptionalFlag( node, "automatic", weapon._bAutomatic ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "falloffStart", sourceName, 0.0f, kAnyFloat, weapon._falloffStart ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "falloffEnd", sourceName, 0.0f, kAnyFloat, weapon._falloffEnd ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "falloffMinScale", sourceName, 0.0f, 1.0f, weapon._falloffMinScale ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "projectileSpeed", sourceName, 0.0f, kAnyFloat, weapon._projectileSpeed ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "projectileGravity", sourceName, -kAnyFloat, kAnyFloat, weapon._projectileGravity ) && bValid;
            bValid                      = GameDataXML::readOptionalNumber( node, "headshotMultiplier", sourceName, 0.0f, kAnyFloat, weapon._headshotMultiplier ) && bValid;
            if ( weapon._maxSpread < weapon._minSpread || weapon._falloffEnd < weapon._falloffStart )
            {
                SW_LOG_ERROR( "%#: weapon '%#' needs minSpread <= maxSpread and falloffStart <= falloffEnd", sourceName, pID );
                bValid = false;
            }
            if ( bValid == false )
            {
                SW_LOG_ERROR( "%#: weapon '%#' has invalid fields - skipped", sourceName, pID );
                continue;
            }
            const utf8* pAmmoID = node.findAttribute( "ammo" );
            weapon._ammoID      = pAmmoID != nullptr ? hashed_string( pAmmoID ) : hashed_string{};
            const utf8* pItemID = node.findAttribute( "item" );
            weapon._itemID      = pItemID != nullptr ? hashed_string( pItemID ) : hashed_string{};
            add( weapon );
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
        _cooldown.update( deltaTime );
        _currentSpread = MathUtil::max( _def._minSpread, _currentSpread - _def._spreadRecovery * deltaTime );

        if ( _reload.update( deltaTime ) )
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
        const float32 coneHalfAngle = _currentSpread * MathUtil::kDegreeToRadian;
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

    void WeaponState::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, _def._id );
        StateArchiveUtil::writeRandom( outArchive, _random );
        StateArchiveUtil::writeCountdown( outArchive, _cooldown );
        StateArchiveUtil::writeCountdown( outArchive, _reload );
        outArchive << _currentSpread;
        outArchive << _magazineAmmo;
        outArchive << _reserveAmmo;
    }

    bool WeaponState::readState( Archive& archive )
    {
        hashed_string defID;
        if ( StateArchiveUtil::readName( archive, defID ) == false || defID != _def._id )
            return false;
        WeaponState restored = *this;
        const bool  bRead    = StateArchiveUtil::readRandom( archive, restored._random ) && StateArchiveUtil::readCountdown( archive, restored._cooldown ) &&
                           StateArchiveUtil::readCountdown( archive, restored._reload );
        archive >> restored._currentSpread;
        archive >> restored._magazineAmmo;
        archive >> restored._reserveAmmo;
        const bool bValid = bRead && archive.isOk() && 0 <= restored._magazineAmmo && 0 <= restored._reserveAmmo;
        if ( bValid == false )
            return false;
        *this = std::move( restored );
        return true;
    }
} // namespace sw
