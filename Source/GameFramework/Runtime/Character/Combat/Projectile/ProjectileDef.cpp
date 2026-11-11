#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ResolvedShot.h"
#include "GameFramework/Runtime/Foundation/Condition/GameConditions.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        struct ProjectileDefInternal
        {
            static constexpr const utf8* kBareTag = "tag"; ///< 탄 · 맞음 조건의 그냥 이름은 태그다(`accepts="Ammo.Rifle"`)

            /** @brief @p value 에 수정자 하나를 얹습니다. */
            static float32 apply( float32 value, const ShotModifierDef& modifier )
            {
                return modifier._op == ShotModifierOp::Add ? value + modifier._value : value * modifier._value;
            }

            /** @brief 정수 칸에 수정자를 얹고 반올림합니다. 0 아래로 내려가지 않습니다. */
            static int32 applyCount( int32 value, const ShotModifierDef& modifier )
            {
                return MathUtil::max( 0, static_cast<int32>( MathUtil::round( apply( static_cast<float32>( value ), modifier ) ) ) );
            }

            static void applyModifier( ResolvedShot& inoutShot, const ShotModifierDef& modifier )
            {
                switch ( modifier._field )
                {
                    case ShotField::Damage:
                    {
                        inoutShot._damage = MathUtil::max( 0.0f, apply( inoutShot._damage, modifier ) );
                        break;
                    }
                    case ShotField::Speed:
                    {
                        inoutShot._speed = MathUtil::max( 0.0f, apply( inoutShot._speed, modifier ) );
                        break;
                    }
                    case ShotField::Gravity:
                    {
                        inoutShot._gravityScale = apply( inoutShot._gravityScale, modifier );
                        break;
                    }
                    case ShotField::Drag:
                    {
                        inoutShot._drag = MathUtil::max( 0.0f, apply( inoutShot._drag, modifier ) );
                        break;
                    }
                    case ShotField::Range:
                    {
                        inoutShot._range = MathUtil::max( 0.0f, apply( inoutShot._range, modifier ) );
                        break;
                    }
                    case ShotField::Lifetime:
                    {
                        inoutShot._lifetime = MathUtil::max( 0.0f, apply( inoutShot._lifetime, modifier ) );
                        break;
                    }
                    case ShotField::Radius:
                    {
                        inoutShot._radius = MathUtil::max( 0.0f, apply( inoutShot._radius, modifier ) );
                        break;
                    }
                    case ShotField::Homing:
                    {
                        inoutShot._homingRate = MathUtil::max( 0.0f, apply( inoutShot._homingRate, modifier ) );
                        break;
                    }
                    case ShotField::Pierce:
                    {
                        inoutShot._pierceCount = applyCount( inoutShot._pierceCount, modifier );
                        break;
                    }
                    case ShotField::Bounce:
                    {
                        inoutShot._bounceCount = applyCount( inoutShot._bounceCount, modifier );
                        break;
                    }
                    case ShotField::BlastRadius:
                    {
                        inoutShot._blastRadius = MathUtil::max( 0.0f, apply( inoutShot._blastRadius, modifier ) );
                        break;
                    }
                    case ShotField::HeadshotMultiplier:
                    {
                        inoutShot._headshotMultiplier = MathUtil::max( 0.0f, apply( inoutShot._headshotMultiplier, modifier ) );
                        break;
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ProjectileDef::ProjectileDef()
        : _id{}
        , _listTag{}
        , _damage{ 10.0f }
        , _speed{ 0.0f }
        , _gravityScale{ 1.0f }
        , _drag{ 0.0f }
        , _range{ 80.0f }
        , _lifetime{ 0.0f }
        , _radius{ 0.0f }
        , _homingRate{ 0.0f }
        , _pierceCount{ 0 }
        , _bounceCount{ 0 }
        , _restitution{ 1.0f }
        , _blastRadius{ 0.0f }
        , _falloffStart{ 0.0f }
        , _falloffEnd{ 0.0f }
        , _falloffMinScale{ 1.0f }
        , _headshotMultiplier{ 2.0f }
        , _tracerColor{ 1.0f, 1.0f, 1.0f, 1.0f }
        , _tracerWidth{ 0.0f }
        , _tags{}
        , _listHitAction{}
    {
    }

    void ProjectileDef::runHitActions( const vector<ProjectileHitAction>& listHitAction, ActionContext& context )
    {
        for ( const ProjectileHitAction& action : listHitAction )
        {
            if ( action._when.evaluate( context._world ) )
                (void)action._call.invoke( context ); // 즉시 동작이다 — 실패(칸 없음)는 그 문맥에 없는 세계일 뿐이다
        }
    }

    // ------------------------------------------------------------------------------
    // ResolvedShot
    // ------------------------------------------------------------------------------
    ResolvedShot ResolvedShot::resolve( const ProjectileDef& projectile, const vector<ShotModifierDef>& listWeaponModifier, const vector<ShotModifierDef>& listAttachment )
    {
        ResolvedShot shot;
        shot._projectileID       = projectile._id;
        shot._tags               = projectile._tags;
        shot._listHitAction      = projectile._listHitAction;
        shot._tracerColor        = projectile._tracerColor;
        shot._tracerWidth        = projectile._tracerWidth;
        shot._damage             = projectile._damage;
        shot._speed              = projectile._speed;
        shot._gravityScale       = projectile._gravityScale;
        shot._drag               = projectile._drag;
        shot._range              = projectile._range;
        shot._lifetime           = projectile._lifetime;
        shot._radius             = projectile._radius;
        shot._homingRate         = projectile._homingRate;
        shot._restitution        = projectile._restitution;
        shot._blastRadius        = projectile._blastRadius;
        shot._falloffStart       = projectile._falloffStart;
        shot._falloffEnd         = projectile._falloffEnd;
        shot._falloffMinScale    = projectile._falloffMinScale;
        shot._headshotMultiplier = projectile._headshotMultiplier;
        shot._pierceCount        = projectile._pierceCount;
        shot._bounceCount        = projectile._bounceCount;
        // 순서는 한 벌이다: 탄 → 무기 → 부착물. 같은 칸에 곱과 합이 섞이면 적힌 순서가 곧 식이다.
        for ( const ShotModifierDef& modifier : listWeaponModifier )
        {
            ProjectileDefInternal::applyModifier( shot, modifier );
        }
        for ( const ShotModifierDef& modifier : listAttachment )
        {
            ProjectileDefInternal::applyModifier( shot, modifier );
        }
        return shot;
    }

    float32 ResolvedShot::computeFalloffScale( float32 distance ) const
    {
        if ( _falloffEnd <= 0.0f || distance <= _falloffStart )
            return 1.0f;
        if ( distance >= _falloffEnd || _falloffEnd <= _falloffStart )
            return _falloffMinScale;
        const float32 ratio = ( distance - _falloffStart ) / ( _falloffEnd - _falloffStart );
        return 1.0f + ( _falloffMinScale - 1.0f ) * ratio;
    }

    float32 ResolvedShot::computeDamage( float32 distance, bool bHeadshot ) const
    {
        const float32 damage = _damage * computeFalloffScale( distance );
        return bHeadshot ? damage * _headshotMultiplier : damage;
    }

    // ------------------------------------------------------------------------------
    // ProjectileCatalog
    // ------------------------------------------------------------------------------
    ProjectileCatalog::ProjectileCatalog()
        : _listImpact{}
    {
    }

    const ProjectileImpactDef* ProjectileCatalog::findImpact( const hashed_string& surface, const TagContainer& projectileTags ) const
    {
        GameplayContext context;
        context.set<SubjectTagsSlot>( &projectileTags );
        for ( const ProjectileImpactDef& impact : _listImpact )
        {
            if ( impact._surface == surface && impact._whenExpr.evaluate( context ) )
                return &impact;
        }
        return nullptr;
    }

    uint32 ProjectileCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        using Element                          = XMLCatalogElement<ProjectileCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&ProjectileCatalog::loadProjectile>( "Projectile" ),
            Element::of<&ProjectileCatalog::loadImpact>( "Impact" ).uncounted(),
        };
        return XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement )._loadedCount;
    }

    bool ProjectileCatalog::loadProjectile( const XMLNode& node, string_view sourceName )
    {
        // 적지 않은 칸은 ProjectileDef 의 초기값이다. 깨진 값 · 범위 밖 값 · 틀린 식은 그 탄을 넣지 않는 오류다(자르지 않는다).
        ProjectileDef projectile;
        bool          bValid = XMLFields::readProperties( node, projectile, sourceName );
        for ( const TagID& tag : projectile._listTag )
        {
            projectile._tags.addTag( tag );
        }
        for ( XMLNode actionNode = node.findChild( "OnHit" ); actionNode; actionNode = actionNode.findNextSibling( "OnHit" ) )
        {
            ProjectileHitActionDef text;
            ProjectileHitAction    action;
            const bool             bProperties = XMLFields::readProperties( actionNode, text, sourceName );
            const bool             bCompiled   = bProperties && ConditionExpr::compile( text._when, sourceName, action._when, ProjectileDefInternal::kBareTag ) &&
                                   GameActionCall::compileText( text._call, sourceName, action._call );
            bValid = bValid && bCompiled;
            if ( bCompiled )
                projectile._listHitAction.push_back( action );
        }
        if ( projectile._id.empty() || bValid == false )
        {
            SW_LOG_ERROR_ID( "Projectile.ProjectileInvalidSkipped", "%#: projectile '%#' has invalid fields - skipped", sourceName, projectile._id.c_str() );
            return false;
        }
        return add( projectile ) >= 0;
    }

    bool ProjectileCatalog::loadImpact( const XMLNode& node, string_view sourceName )
    {
        ProjectileImpactDef impact;
        if ( XMLFields::readProperties( node, impact, sourceName ) == false )
            return false;
        if ( ConditionExpr::compile( impact._when, sourceName, impact._whenExpr, ProjectileDefInternal::kBareTag ) == false )
            return false;
        for ( const ProjectileImpactDef& existing : _listImpact )
        {
            if ( existing._id == impact._id )
            {
                SW_LOG_ERROR_ID( "Projectile.ImpactDuplicateID", "%#: <Impact id=\"%#\"> appears twice", sourceName, impact._id.c_str() );
                return false;
            }
        }
        _listImpact.push_back( impact );
        return true;
    }
} // namespace sw

namespace sw
{
    /** @brief `ProjectileCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( Projectiles, ProjectileCatalog, SW_GF_API, AssetTypeDef::makeDataFile( ProjectileCatalog::kDataKind, AssetPathRule{ "game/", {}, "/data/projectiles.xml" }, AssetTypeLoader::of<ProjectileCatalog>( MemoryTag::Game ) ) );
} // namespace sw
