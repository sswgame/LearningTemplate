#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaAbilitySet.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/World/Query/GameFlags.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"

namespace sw
{
    namespace
    {
        struct MetroidvaniaAbilitySetInternal
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    MetroidvaniaAbilitySet::MetroidvaniaAbilitySet()
        : _pCatalog{ nullptr }
        , _baseSettings{}
        , _listAbility{}
    {
    }

    void MetroidvaniaAbilitySet::initialize( const MetroidvaniaCatalog* pCatalog, const PlatformerSettings& baseSettings )
    {
        _pCatalog                     = pCatalog;
        _baseSettings                 = baseSettings;
        _baseSettings._bWallJump      = SW_FALSE;
        _baseSettings._extraJumpCount = 0;
        _baseSettings._airDashCount   = 0;
        _listAbility.clear();
    }

    bool MetroidvaniaAbilitySet::grantAbility( const hashed_string& abilityID, GameFlags& flags )
    {
        if ( _pCatalog == nullptr || hasAbility( abilityID ) )
            return false;
        const MetroidvaniaAbilityDef* pAbility = _pCatalog->findAbility( abilityID );
        if ( pAbility == nullptr )
            return false;
        _listAbility.push_back( pAbility->_id );
        flags.setFlag( pAbility->_flag, 1 );
        return true;
    }

    int32 MetroidvaniaAbilitySet::restoreFromFlags( const GameFlags& flags )
    {
        _listAbility.clear();
        if ( _pCatalog == nullptr )
            return 0;
        for ( const MetroidvaniaAbilityDef& ability : _pCatalog->getAbilities() )
        {
            if ( flags.hasFlag( ability._flag ) )
                _listAbility.push_back( ability._id );
        }
        return static_cast<int32>( _listAbility.size() );
    }

    bool MetroidvaniaAbilitySet::hasAbility( const hashed_string& abilityID ) const
    {
        return VectorUtil::contains( _listAbility, abilityID );
    }

    void MetroidvaniaAbilitySet::applyToSettings( PlatformerSettings& outSettings ) const
    {
        outSettings = _baseSettings;
        if ( _pCatalog == nullptr )
            return;
        const hashed_string wallJumpName( MetroidvaniaAbilitySetInternal::kWallJumpName );
        for ( const hashed_string& abilityID : _listAbility )
        {
            const MetroidvaniaAbilityDef* pAbility = _pCatalog->findAbility( abilityID );
            if ( pAbility == nullptr )
                continue;
            for ( const StatValue& value : pAbility->_motor.getValues() )
            {
                if ( value._name == wallJumpName )
                {
                    outSettings._bWallJump = value._value != 0.0f ? SW_TRUE : outSettings._bWallJump;
                    continue;
                }
                for ( const MetroidvaniaAbilitySetInternal::FloatField& field : MetroidvaniaAbilitySetInternal::kArrFloatField )
                {
                    if ( value._name == hashed_string( field._pName ) )
                        outSettings.*field._pMember += value._value;
                }
                for ( const MetroidvaniaAbilitySetInternal::IntField& field : MetroidvaniaAbilitySetInternal::kArrIntField )
                {
                    if ( value._name == hashed_string( field._pName ) )
                        outSettings.*field._pMember = MathUtil::max( 0, outSettings.*field._pMember + static_cast<int32>( MathUtil::round( value._value ) ) );
                }
            }
        }
    }

    void MetroidvaniaAbilitySet::applyToMotor( PlatformerMotor2D& motor ) const
    {
        PlatformerSettings settings;
        applyToSettings( settings );
        motor.setSettings( settings );
    }

    PlatformerInput MetroidvaniaAbilitySet::filterInput( const PlatformerInput& input ) const
    {
        PlatformerInput filtered = input;
        if ( canDash() == false )
            filtered._bDashPressed = SW_FALSE;
        return filtered;
    }

    bool MetroidvaniaAbilitySet::canDash() const
    {
        if ( _pCatalog == nullptr )
            return false;
        const hashed_string dashName( MetroidvaniaAbilitySetInternal::kDashName );
        for ( const hashed_string& abilityID : _listAbility )
        {
            const MetroidvaniaAbilityDef* pAbility = _pCatalog->findAbility( abilityID );
            if ( pAbility != nullptr && pAbility->_motor.getValue( dashName ) != 0.0f )
                return true;
        }
        return false;
    }

    bool MetroidvaniaAbilitySet::isMotorSettingName( const hashed_string& name )
    {
        if ( name == hashed_string( MetroidvaniaAbilitySetInternal::kWallJumpName ) || name == hashed_string( MetroidvaniaAbilitySetInternal::kDashName ) )
            return true;
        for ( const MetroidvaniaAbilitySetInternal::FloatField& field : MetroidvaniaAbilitySetInternal::kArrFloatField )
        {
            if ( name == hashed_string( field._pName ) )
                return true;
        }
        for ( const MetroidvaniaAbilitySetInternal::IntField& field : MetroidvaniaAbilitySetInternal::kArrIntField )
        {
            if ( name == hashed_string( field._pName ) )
                return true;
        }
        return false;
    }

    void MetroidvaniaAbilitySet::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listAbility.size() );
        for ( const hashed_string& abilityID : _listAbility )
        {
            StateArchiveUtil::writeName( outArchive, abilityID );
        }
    }

    bool MetroidvaniaAbilitySet::readState( Archive& archive )
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
            hashed_string abilityID;
            if ( StateArchiveUtil::readName( archive, abilityID ) == false )
                return false;
            const bool bDuplicate = VectorUtil::contains( listAbility, abilityID );
            if ( _pCatalog->findAbility( abilityID ) == nullptr || bDuplicate )
                return false;
            listAbility.push_back( abilityID );
        }
        _listAbility = std::move( listAbility );
        return true;
    }
} // namespace sw
