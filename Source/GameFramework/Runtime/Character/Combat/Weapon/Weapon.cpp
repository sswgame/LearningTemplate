#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Weapon/Weapon.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"

#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct WeaponInternal
        {
        };
    } // namespace
} // namespace sw

namespace sw
{
    WeaponDef::WeaponDef()
        : _id{}
        , _ammoID{}
        , _itemID{}
        , _name{}
        , _fireInterval{ 0.1f }
        , _reloadTime{ 1.8f }
        , _damage{ 10.0f }
        , _range{ 80.0f }
        , _minSpread{ 0.5f }
        , _maxSpread{ 4.0f }
        , _spreadPerShot{ 0.6f }
        , _spreadRecovery{ 6.0f }
        , _recoilPitch{ 0.6f }
        , _falloffStart{ 0.0f }
        , _falloffEnd{ 0.0f }
        , _falloffMinScale{ 1.0f }
        , _projectileSpeed{ 0.0f }
        , _projectileGravity{ 1.0f }
        , _headshotMultiplier{ 2.0f }
        , _magazineSize{ 30 }
        , _maxReserveAmmo{ 180 }
        , _pelletCount{ 1 }
        , _bAutomatic{ SW_TRUE }
    {
    }

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
            // 적지 않은 칸은 WeaponDef 의 초기값이다. 깨진 값 · 범위 밖 값(최소 <= 최대 퍼짐 · 감쇠 시작 <= 끝 — 필드 경계)은 그 무기를 넣지 않는 오류다(자르지 않는다).
            WeaponDef weapon;
            bool      bValid = XMLFields::readFields( node, weapon, sourceName );
            if ( weapon._id.empty() )
                continue;
            const utf8* const pID = weapon._id.c_str();
            if ( weapon._name.empty() )
                weapon._name = pID;
            if ( bValid == false )
            {
                SW_LOG_ERROR_ID( "Weapon.WeaponInvalidFieldsSkipped", "%#: weapon '%#' has invalid fields - skipped", sourceName, pID );
                continue;
            }
            add( weapon );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING_ID( "Weapon.NoWeaponEntries", "%#: no <Weapon> entries", sourceName );
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

    WeaponFireResult WeaponState::pullTrigger( const Ray& aim, bool bTriggerJustPressed, WeaponShot& outShot )
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
            Ray ray;
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

namespace sw
{
    /** @brief `WeaponCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_RESOURCE_DATA_KIND( Weapons, WeaponCatalog::kDataKind, "game/", nullptr, "/data/weapons.xml", &AssetDataKind::loadCatalog<WeaponCatalog> );
} // namespace sw
