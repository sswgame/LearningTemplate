#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Weapon/Weapon.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct WeaponInternal
        {
            static constexpr const utf8* kBareTag = "tag"; ///< `accepts` 의 그냥 이름은 탄 태그다
        };
    } // namespace
} // namespace sw

namespace sw
{
    WeaponDef::WeaponDef()
        : _id{}
        , _ammoID{}
        , _itemID{}
        , _projectileID{}
        , _accepts{}
        , _name{}
        , _fireInterval{ 0.1f }
        , _reloadTime{ 1.8f }
        , _minSpread{ 0.5f }
        , _maxSpread{ 4.0f }
        , _spreadPerShot{ 0.6f }
        , _spreadRecovery{ 6.0f }
        , _recoilPitch{ 0.6f }
        , _magazineSize{ 30 }
        , _maxReserveAmmo{ 180 }
        , _pelletCount{ 1 }
        , _bAutomatic{ SW_TRUE }
        , _bTwoHanded{ SW_FALSE }
        , _acceptsExpr{}
        , _listModifier{}
    {
    }

    bool WeaponDef::compileAccepts( string_view sourceName )
    {
        return ConditionExpr::compile( _accepts, sourceName, _acceptsExpr, WeaponInternal::kBareTag );
    }

    bool WeaponDef::canAccept( const ProjectileDef& projectile ) const
    {
        GameplayContext context;
        context.set<SubjectTagsSlot>( &projectile._tags );
        return _acceptsExpr.evaluate( context );
    }

    // ------------------------------------------------------------------------------
    // WeaponCatalog
    // ------------------------------------------------------------------------------
    WeaponCatalog::WeaponCatalog()
    {
    }

    uint32 WeaponCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        using Element                          = XMLCatalogElement<WeaponCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&WeaponCatalog::loadWeapon>( "Weapon" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool WeaponCatalog::loadWeapon( const XMLNode& node, string_view sourceName )
    {

        // 적지 않은 칸은 WeaponDef 의 초기값이다. 깨진 값 · 범위 밖 값(최소 <= 최대 퍼짐 · 감쇠 시작 <= 끝 — 필드 경계)은 그 무기를 넣지 않는 오류다(자르지 않는다).
        WeaponDef weapon;
        bool      bValid = XMLFields::readProperties( node, weapon, sourceName );
        if ( weapon._id.empty() )
            return false;
        const utf8* const pID = weapon._id.c_str();
        if ( weapon._name.empty() )
            weapon._name = pID;
        bValid = weapon.compileAccepts( sourceName ) && bValid;
        for ( XMLNode modifierNode = node.findChild( "Modifier" ); modifierNode; modifierNode = modifierNode.findNextSibling( "Modifier" ) )
        {
            ShotModifierDef modifier;
            bValid = XMLFields::readProperties( modifierNode, modifier, sourceName ) && bValid;
            weapon._listModifier.push_back( modifier );
        }
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "Weapon.WeaponInvalidFieldsSkipped", "%#: weapon '%#' has invalid fields - skipped", sourceName, pID );
            return false;
        }
        add( weapon );
        return true;
    }

    bool WeaponCatalog::checkProjectiles( const ProjectileCatalog& projectiles ) const
    {
        bool bComplete = true;
        for ( const WeaponDef& weapon : getAll() )
        {
            const ProjectileDef* pProjectile = projectiles.find( weapon._projectileID );
            if ( pProjectile == nullptr )
            {
                SW_LOG_ERROR_ID( "Weapon.ProjectileUnknown", "weapon '%#' loads projectile '%#', which is not in the projectile catalog", weapon._id.c_str(), weapon._projectileID.c_str() );
                bComplete = false;
                continue;
            }
            if ( weapon.canAccept( *pProjectile ) == false )
            {
                SW_LOG_ERROR_ID( "Weapon.ProjectileNotAccepted", "weapon '%#' loads projectile '%#', which its accepts='%#' refuses", weapon._id.c_str(), weapon._projectileID.c_str(),
                                 weapon._accepts.c_str() );
                bComplete = false;
            }
        }
        return bComplete;
    }

    // ------------------------------------------------------------------------------
    // WeaponState
    // ------------------------------------------------------------------------------
    WeaponState::WeaponState()
        : _def{}
        , _shot{}
        , _random{ 1u }
        , _cooldown{}
        , _reload{}
        , _currentSpread{ 0.0f }
        , _magazineAmmo{ 0 }
        , _reserveAmmo{ 0 }
    {
    }

    bool WeaponState::equip( const WeaponDef& weapon, const ProjectileDef& projectile, int32 reserveAmmo, uint32 spreadSeed )
    {
        if ( weapon.canAccept( projectile ) == false )
        {
            SW_LOG_ERROR_ID( "Weapon.EquipRefusedProjectile", "weapon '%#' does not accept projectile '%#' (accepts='%#')", weapon._id.c_str(), projectile._id.c_str(), weapon._accepts.c_str() );
            return false;
        }
        _def  = weapon;
        _shot = ResolvedShot::resolve( projectile, weapon._listModifier, {} );
        _random.setSeed( spreadSeed );
        _cooldown.clear();
        _reload.clear();
        _currentSpread = weapon._minSpread;
        _magazineAmmo  = weapon._magazineSize;
        _reserveAmmo   = MathUtil::clamp( reserveAmmo, 0, weapon._maxReserveAmmo );
        return true;
    }

    bool WeaponState::loadProjectile( const ProjectileDef& projectile, const vector<ShotModifierDef>& listAttachment )
    {
        if ( _def.canAccept( projectile ) == false )
        {
            SW_LOG_ERROR_ID( "Weapon.LoadRefusedProjectile", "weapon '%#' does not accept projectile '%#' (accepts='%#')", _def._id.c_str(), projectile._id.c_str(), _def._accepts.c_str() );
            return false;
        }
        _shot = ResolvedShot::resolve( projectile, _def._listModifier, listAttachment );
        return true;
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

    void WeaponState::serializeState( StateIO& io )
    {
        io.expect( _def._id );
        // 장전한 탄은 쓰는 쪽이 `loadProjectile` 로 먼저 맞춘다 — 다른 탄의 상태는 받지 않는다(탄 값이 들어맞지 않는다).
        io.expect( _shot._projectileID );
        io.value( _random );
        io.value( _cooldown );
        io.value( _reload );
        io.value( _currentSpread );
        io.value( _magazineAmmo );
        io.value( _reserveAmmo );
        io.check( 0 <= _magazineAmmo && 0 <= _reserveAmmo );
    }

} // namespace sw

namespace sw
{
    /** @brief `WeaponCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( Weapons, WeaponCatalog, SW_GF_API, AssetTypeDef::makeDataFile( WeaponCatalog::kDataKind, AssetPathRule{ "game/", {}, "/data/weapons.xml" }, AssetTypeLoader::of<WeaponCatalog>( MemoryTag::Game ) ) );
} // namespace sw
