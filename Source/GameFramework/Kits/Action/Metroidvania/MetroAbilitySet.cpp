#include "pch.h"

#include "GameFramework/Kits/Action/Metroidvania/MetroAbilitySet.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/World/Query/GameFlags.h"
#include "GameFramework/Kits/Action/Metroidvania/MetroidvaniaCatalog.h"

namespace sw
{
    namespace
    {
        struct MetroAbilitySetInternal
        {
            struct FloatField
            {
                const utf8* _pName;
                float32 PlatformerSettings::* _pMember;
            };

            struct IntField
            {
                const utf8* _pName;
                int32 PlatformerSettings::* _pMember;
            };

            static constexpr FloatField kArrFloatField[] = {
                {          "runSpeed",           &PlatformerSettings::_runSpeed},
                {"groundAcceleration", &PlatformerSettings::_groundAcceleration},
                {   "airAcceleration",    &PlatformerSettings::_airAcceleration},
                {        "jumpHeight",         &PlatformerSettings::_jumpHeight},
                {      "maxFallSpeed",       &PlatformerSettings::_maxFallSpeed},
                {    "wallSlideSpeed",     &PlatformerSettings::_wallSlideSpeed},
                {    "wallJumpSpeedX",     &PlatformerSettings::_wallJumpSpeedX},
                {    "wallJumpSpeedY",     &PlatformerSettings::_wallJumpSpeedY},
                {         "dashSpeed",          &PlatformerSettings::_dashSpeed},
                {      "dashDuration",       &PlatformerSettings::_dashDuration},
                {      "dashCooldown",       &PlatformerSettings::_dashCooldown},
                {        "climbSpeed",         &PlatformerSettings::_climbSpeed},
            };

            static constexpr IntField kArrIntField[] = {
                {"extraJumpCount", &PlatformerSettings::_extraJumpCount},
                {  "airDashCount",   &PlatformerSettings::_airDashCount},
            };

            static constexpr const utf8* kWallJumpName = "wallJump";
            static constexpr const utf8* kDashName     = "dash";

            /** @brief 능력의 플래그 — 비면 `ability.<id>`(공유 플래그에서 다른 키트의 같은 id 와 갈린다). */
            static hashed_string resolveFlag( const MetroAbilityDef& ability ) { return ability._flag.empty() ? hashed_string( string( "ability." ) + ability._id.c_str() ) : ability._flag; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MetroAbilitySet::MetroAbilitySet()
        : _pCatalog{ nullptr }
        , _baseSettings{}
        , _listAbility{}
    {
    }

    void MetroAbilitySet::initialize( const MetroidvaniaCatalog* pCatalog, const PlatformerSettings& baseSettings )
    {
        _pCatalog                     = pCatalog;
        _baseSettings                 = baseSettings;
        _baseSettings._bWallJump      = SW_FALSE;
        _baseSettings._extraJumpCount = 0;
        _baseSettings._airDashCount   = 0;
        _listAbility.clear();
    }

    bool MetroAbilitySet::grantAbility( const hashed_string& abilityId, GameFlags& flags )
    {
        if ( _pCatalog == nullptr || hasAbility( abilityId ) )
            return false;
        const MetroAbilityDef* pAbility = _pCatalog->findAbility( abilityId );
        if ( pAbility == nullptr )
            return false;
        _listAbility.push_back( pAbility->_id );
        flags.setFlag( MetroAbilitySetInternal::resolveFlag( *pAbility ), 1 );
        return true;
    }

    int32 MetroAbilitySet::restoreFromFlags( const GameFlags& flags )
    {
        _listAbility.clear();
        if ( _pCatalog == nullptr )
            return 0;
        for ( const MetroAbilityDef& ability : _pCatalog->getAbilities() )
        {
            if ( flags.hasFlag( MetroAbilitySetInternal::resolveFlag( ability ) ) )
                _listAbility.push_back( ability._id );
        }
        return static_cast<int32>( _listAbility.size() );
    }

    bool MetroAbilitySet::hasAbility( const hashed_string& abilityId ) const
    {
        for ( const hashed_string& owned : _listAbility )
        {
            if ( owned == abilityId )
                return true;
        }
        return false;
    }

    void MetroAbilitySet::applyToSettings( PlatformerSettings& outSettings ) const
    {
        outSettings = _baseSettings;
        if ( _pCatalog == nullptr )
            return;
        const hashed_string wallJumpName( MetroAbilitySetInternal::kWallJumpName );
        for ( const hashed_string& abilityId : _listAbility )
        {
            const MetroAbilityDef* pAbility = _pCatalog->findAbility( abilityId );
            if ( pAbility == nullptr )
                continue;
            for ( const StatValue& value : pAbility->_motor.getValues() )
            {
                if ( value._name == wallJumpName )
                {
                    outSettings._bWallJump = value._value != 0.0f ? SW_TRUE : outSettings._bWallJump;
                    continue;
                }
                for ( const MetroAbilitySetInternal::FloatField& field : MetroAbilitySetInternal::kArrFloatField )
                {
                    if ( value._name == hashed_string( field._pName ) )
                        outSettings.*field._pMember += value._value;
                }
                for ( const MetroAbilitySetInternal::IntField& field : MetroAbilitySetInternal::kArrIntField )
                {
                    if ( value._name == hashed_string( field._pName ) )
                        outSettings.*field._pMember = MathUtil::max( 0, outSettings.*field._pMember + static_cast<int32>( MathUtil::round( value._value ) ) );
                }
            }
        }
    }

    void MetroAbilitySet::applyToMotor( PlatformerMotor2D& motor ) const
    {
        PlatformerSettings settings;
        applyToSettings( settings );
        motor.setSettings( settings );
    }

    PlatformerInput MetroAbilitySet::filterInput( const PlatformerInput& input ) const
    {
        PlatformerInput filtered = input;
        if ( canDash() == false )
            filtered._bDashPressed = SW_FALSE;
        return filtered;
    }

    bool MetroAbilitySet::canDash() const
    {
        if ( _pCatalog == nullptr )
            return false;
        const hashed_string dashName( MetroAbilitySetInternal::kDashName );
        for ( const hashed_string& abilityId : _listAbility )
        {
            const MetroAbilityDef* pAbility = _pCatalog->findAbility( abilityId );
            if ( pAbility != nullptr && pAbility->_motor.getValue( dashName ) != 0.0f )
                return true;
        }
        return false;
    }

    bool MetroAbilitySet::isMotorSettingName( const hashed_string& name )
    {
        if ( name == hashed_string( MetroAbilitySetInternal::kWallJumpName ) || name == hashed_string( MetroAbilitySetInternal::kDashName ) )
            return true;
        for ( const MetroAbilitySetInternal::FloatField& field : MetroAbilitySetInternal::kArrFloatField )
        {
            if ( name == hashed_string( field._pName ) )
                return true;
        }
        for ( const MetroAbilitySetInternal::IntField& field : MetroAbilitySetInternal::kArrIntField )
        {
            if ( name == hashed_string( field._pName ) )
                return true;
        }
        return false;
    }

    void MetroAbilitySet::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listAbility.size() );
        for ( const hashed_string& abilityId : _listAbility )
        {
            StateArchiveUtil::writeName( outArchive, abilityId );
        }
    }

    bool MetroAbilitySet::readState( Archive& archive )
    {
        if ( _pCatalog == nullptr )
            return false;
        uint32 abilityCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, abilityCount ) == false )
            return false;
        vector<hashed_string> listAbility;
        listAbility.reserve( abilityCount );
        for ( uint32 entry = 0; entry < abilityCount; ++entry )
        {
            hashed_string abilityId;
            if ( StateArchiveUtil::readName( archive, abilityId ) == false )
                return false;
            const bool bDuplicate = std::find( listAbility.begin(), listAbility.end(), abilityId ) != listAbility.end();
            if ( _pCatalog->findAbility( abilityId ) == nullptr || bDuplicate )
                return false;
            listAbility.push_back( abilityId );
        }
        _listAbility = std::move( listAbility );
        return true;
    }
} // namespace sw
