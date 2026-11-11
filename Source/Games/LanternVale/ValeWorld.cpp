#include "pch.h"

#include "Games/LanternVale/ValeWorld.h"

#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // ValeProfile
    // ------------------------------------------------------------------------------
    void ValeProfile::write( BitWriter& writer ) const
    {
        writer.writeVarUint( kFormatVersion );
        writer.writeVarInt( _level );
        writer.writeVarInt( _experience );
        writer.writeVarInt( _coins );
        writer.writeVarInt( _herbCount );
        writer.writeVarInt( _defeatCount );
        writer.writeFloat( _position._x );
        writer.writeFloat( _position._y );
        writer.writeFloat( _position._z );
    }

    bool ValeProfile::read( BitReader& reader )
    {
        if ( reader.readVarUint() != kFormatVersion )
            return false;
        ValeProfile loaded;
        loaded._level        = reader.readVarInt32();
        loaded._experience   = reader.readVarInt32();
        loaded._coins        = reader.readVarInt32();
        loaded._herbCount    = reader.readVarInt32();
        loaded._defeatCount  = reader.readVarInt32();
        const bool bPosition = reader.readFiniteFloat( loaded._position._x ) && reader.readFiniteFloat( loaded._position._y ) && reader.readFiniteFloat( loaded._position._z );
        const bool bInRange  = loaded._level >= 1 && loaded._experience >= 0 && loaded._coins >= 0 && loaded._herbCount >= 0 && loaded._defeatCount >= 0;
        if ( reader.hasOverflowed() || bPosition == false || bInRange == false )
            return false;
        *this = loaded;
        return true;
    }

    // ------------------------------------------------------------------------------
    // ValeWorld
    // ------------------------------------------------------------------------------
    ValeWorld::ValeWorld()
        : _rules{}
        , _mapAvatar{}
        , _mapParty{}
        , _listPlacement{}
        , _nextAvatarID{ 1 }
        , _nextPartyID{ 1 }
    {
    }

    bool ValeWorld::initialize( const ValeRules& rules )
    {
        _rules = rules;
        _listPlacement.clear();
        bool bValid = true;
        if ( rules._listLevelExperience.empty() || static_cast<int32>( rules._listLevelExperience.size() ) > kMaxLevelStep )
        {
            SW_LOG_ERROR_ID( "LanternVale.LevelTableInvalid", "LanternVale rules: the level table needs 1..%# entries - got %#", kMaxLevelStep, static_cast<uint32>( rules._listLevelExperience.size() ) );
            bValid = false;
        }
        for ( size_t index = 0; index < rules._listLevelExperience.size(); ++index )
        {
            const int32 previous = index == 0 ? 0 : rules._listLevelExperience[index - 1];
            if ( rules._listLevelExperience[index] > previous )
                continue;
            SW_LOG_ERROR_ID( "LanternVale.LevelTableNotAscending", "LanternVale rules: level table entry %# (%#) must be above the one before (%#)", static_cast<uint32>( index ),
                             rules._listLevelExperience[index], previous );
            bValid = false;
        }
        if ( rules._listPlacement.empty() || rules._listPlacement.size() > kMaxPlacement )
        {
            SW_LOG_ERROR_ID( "LanternVale.PlacementListInvalid", "LanternVale rules: the vale needs 1..%# creatures and herbs - got %#", kMaxPlacement, static_cast<uint32>( rules._listPlacement.size() ) );
            bValid = false;
        }
        if ( isInsideVale( rules._avatarSpawn._x, rules._avatarSpawn._z ) == false )
        {
            SW_LOG_ERROR_ID( "LanternVale.AvatarSpawnOutside", "LanternVale rules: the avatar spawn point lies outside the vale (half extent %#)", rules._worldHalfExtent );
            bValid = false;
        }
        for ( size_t index = 0; bValid && index < rules._listPlacement.size(); ++index )
        {
            const ValePlacement& spawn = rules._listPlacement[index];
            if ( isInsideVale( spawn._position._x, spawn._position._z ) == false )
            {
                SW_LOG_ERROR_ID( "LanternVale.PlacementOutside", "LanternVale rules: spawn %# lies outside the vale (half extent %#)", static_cast<uint32>( index ), rules._worldHalfExtent );
                bValid = false;
                break;
            }
            ValePlacementState state;
            state._def         = spawn;
            state._placementID = kPlacementIDBase + static_cast<uint32>( index );
            state._health      = spawn._kind == ValePlacementKind::Creature ? rules._creatureHealth : 1;
            _listPlacement.push_back( state );
        }
        if ( bValid == false )
            _listPlacement.clear();
        return bValid;
    }

    uint32 ValeWorld::addAvatar( uint64 accountID, const ValeProfile& profile )
    {
        ValeAvatar avatar;
        avatar._profile    = profile;
        avatar._accountID  = accountID;
        avatar._avatarID   = _nextAvatarID++;
        avatar._bChanged   = SW_TRUE;
        avatar._moveBudget = _rules._maxMoveStep; // 들어온 자리에서 한 걸음은 바로 걷는다
        // 저장된 자리가 지금 골짜기 밖이면(규칙이 바뀌었다) 나는 자리에서 다시 시작한다 — 진행은 그대로다.
        if ( isInsideVale( avatar._profile._position._x, avatar._profile._position._z ) == false )
            avatar._profile._position = _rules._avatarSpawn;
        const uint32 avatarID = avatar._avatarID;
        _mapAvatar[avatarID]  = avatar;
        return avatarID;
    }

    void ValeWorld::removeAvatar( uint32 avatarID )
    {
        ValeAvatar* pAvatar = findAvatarMutable( avatarID );
        if ( pAvatar == nullptr )
            return;
        detachFromParty( *pAvatar );
        // 이 아바타가 보낸 초대는 무효다.
        for ( auto& entry : _mapAvatar )
        {
            if ( entry.second._inviteFromID == avatarID )
                entry.second._inviteFromID = 0;
        }
        _mapAvatar.erase( avatarID );
    }

    ValeActionResult ValeWorld::attackCreature( uint32 avatarID, uint32 placementID )
    {
        ValeAvatar*  pAvatar = findAvatarMutable( avatarID );
        const uint32 index   = placementID - kPlacementIDBase;
        if ( pAvatar == nullptr || isPlacementOfKind( placementID, ValePlacementKind::Creature ) == false )
            return ValeActionResult::UnknownTarget;
        ValePlacementState& creature = _listPlacement[index];
        if ( creature._bPresent == SW_FALSE )
            return ValeActionResult::TargetGone;
        if ( pAvatar->_attackCooldown > 0.0f )
            return ValeActionResult::CoolingDown;
        if ( float3::getHorizontalDistance( pAvatar->_profile._position, creature._def._position ) > _rules._attackRange )
            return ValeActionResult::OutOfRange;
        pAvatar->_attackCooldown = _rules._attackCooldownSeconds;
        if ( DepletingValue::take( creature._health, _rules._attackPower )._bDepleted == SW_FALSE )
            return ValeActionResult::Ok;
        creature._bPresent     = SW_FALSE;
        creature._respawnTimer = _rules._creatureRespawnSeconds;
        grantDefeatReward( *pAvatar, creature._def._position );
        return ValeActionResult::Ok;
    }

    ValeActionResult ValeWorld::gatherHerb( uint32 avatarID, uint32 placementID )
    {
        ValeAvatar* pAvatar = findAvatarMutable( avatarID );
        if ( pAvatar == nullptr || isPlacementOfKind( placementID, ValePlacementKind::Herb ) == false )
            return ValeActionResult::UnknownTarget;
        ValePlacementState& herb = _listPlacement[placementID - kPlacementIDBase];
        if ( herb._bPresent == SW_FALSE )
            return ValeActionResult::TargetGone;
        if ( float3::getHorizontalDistance( pAvatar->_profile._position, herb._def._position ) > _rules._gatherRange )
            return ValeActionResult::OutOfRange;
        herb._bPresent     = SW_FALSE;
        herb._respawnTimer = _rules._herbRespawnSeconds;
        ++pAvatar->_profile._herbCount;
        addExperience( *pAvatar, _rules._herbExperience );
        return ValeActionResult::Ok;
    }

    ValeActionResult ValeWorld::moveAvatar( uint32 avatarID, float32 x, float32 z )
    {
        ValeAvatar* pAvatar = findAvatarMutable( avatarID );
        if ( pAvatar == nullptr )
            return ValeActionResult::UnknownTarget;
        if ( isInsideVale( x, z ) == false )
            return ValeActionResult::StepTooLong;
        const float3 target{ x, pAvatar->_profile._position._y, z };
        // 걸음 길이만 보면 틱 없이 요청을 몰아 보내 아무 속도로나 갈 수 있었다(감사 K-1 — 공격에는 서버 쿨다운이 있는데 이동에는 없었다). 걸은 거리를
        // 시간이 채우는 예산으로 막는다(언리얼 UCharacterMovementComponent 의 시간 불일치 감지 자리).
        const float32 distance = float3::getHorizontalDistance( pAvatar->_profile._position, target );
        if ( distance > _rules._maxMoveStep || distance > pAvatar->_moveBudget )
            return ValeActionResult::StepTooLong;
        pAvatar->_moveBudget -= distance;
        pAvatar->_profile._position = target;
        return ValeActionResult::Ok;
    }

    ValeActionResult ValeWorld::inviteToParty( uint32 avatarID, uint32 targetAvatarID )
    {
        ValeAvatar* pLeader = findAvatarMutable( avatarID );
        ValeAvatar* pTarget = findAvatarMutable( targetAvatarID );
        if ( pLeader == nullptr || pTarget == nullptr || pLeader == pTarget )
            return ValeActionResult::UnknownTarget;
        if ( pTarget->_partyID != 0 )
            return ValeActionResult::AlreadyInParty;
        if ( getPartySize( avatarID ) >= _rules._partyMaxSize )
            return ValeActionResult::PartyFull;
        pTarget->_inviteFromID = avatarID;
        pTarget->_bChanged     = SW_TRUE;
        return ValeActionResult::Ok;
    }

    ValeActionResult ValeWorld::acceptPartyInvite( uint32 avatarID, uint32 leaderAvatarID )
    {
        ValeAvatar* pAvatar = findAvatarMutable( avatarID );
        ValeAvatar* pLeader = findAvatarMutable( leaderAvatarID );
        if ( pAvatar == nullptr || pLeader == nullptr )
            return ValeActionResult::UnknownTarget;
        if ( pAvatar->_inviteFromID != leaderAvatarID )
            return ValeActionResult::NotInvited;
        if ( pAvatar->_partyID != 0 )
            return ValeActionResult::AlreadyInParty;
        if ( getPartySize( leaderAvatarID ) >= _rules._partyMaxSize )
            return ValeActionResult::PartyFull;
        if ( pLeader->_partyID == 0 )
        {
            ValeParty party;
            party._partyID = _nextPartyID++;
            party._listMember.push_back( leaderAvatarID );
            pLeader->_partyID         = party._partyID;
            _mapParty[party._partyID] = party;
        }
        ValeParty* pParty = findParty( pLeader->_partyID );
        pParty->_listMember.push_back( avatarID );
        pAvatar->_partyID      = pLeader->_partyID;
        pAvatar->_inviteFromID = 0;
        for ( const uint32 memberID : pParty->_listMember )
        {
            findAvatarMutable( memberID )->_bChanged = SW_TRUE;
        }
        return ValeActionResult::Ok;
    }

    ValeActionResult ValeWorld::leaveParty( uint32 avatarID )
    {
        ValeAvatar* pAvatar = findAvatarMutable( avatarID );
        if ( pAvatar == nullptr )
            return ValeActionResult::UnknownTarget;
        if ( pAvatar->_partyID == 0 )
            return ValeActionResult::NotInParty;
        detachFromParty( *pAvatar );
        return ValeActionResult::Ok;
    }

    void ValeWorld::tick( float32 deltaSeconds )
    {
        for ( auto& entry : _mapAvatar )
        {
            entry.second._attackCooldown = MathUtil::max( 0.0f, entry.second._attackCooldown - deltaSeconds );
            entry.second._moveBudget     = MathUtil::min( _rules._maxMoveStep, entry.second._moveBudget + _rules._moveSpeed * deltaSeconds );
        }
        for ( ValePlacementState& spawn : _listPlacement )
        {
            if ( spawn._bPresent != SW_FALSE )
                continue;
            spawn._respawnTimer -= deltaSeconds;
            if ( spawn._respawnTimer > 0.0f )
                continue;
            spawn._bPresent = SW_TRUE;
            spawn._health   = spawn._def._kind == ValePlacementKind::Creature ? _rules._creatureHealth : 1;
        }
    }

    bool ValeWorld::isPlacementOfKind( uint32 placementID, ValePlacementKind kind ) const
    {
        const bool bInRange = placementID >= kPlacementIDBase && placementID - kPlacementIDBase < static_cast<uint32>( _listPlacement.size() );
        return bInRange && _listPlacement[placementID - kPlacementIDBase]._def._kind == kind;
    }

    bool ValeWorld::isInsideVale( float32 x, float32 z ) const
    {
        const bool bFinite = MathUtil::isFinite( x ) && MathUtil::isFinite( z );
        return bFinite && MathUtil::abs( x ) <= _rules._worldHalfExtent && MathUtil::abs( z ) <= _rules._worldHalfExtent;
    }

    const ValeAvatar* ValeWorld::findAvatar( uint32 avatarID ) const
    {
        const auto iter = _mapAvatar.find( avatarID );
        return iter != _mapAvatar.end() ? &iter->second : nullptr;
    }

    const ValeAvatar* ValeWorld::findAvatarByAccount( uint64 accountID ) const
    {
        for ( const auto& entry : _mapAvatar )
        {
            if ( entry.second._accountID == accountID )
                return &entry.second;
        }
        return nullptr;
    }

    const ValePlacementState* ValeWorld::findPlacement( uint32 placementID ) const
    {
        const bool bInRange = placementID >= kPlacementIDBase && placementID - kPlacementIDBase < static_cast<uint32>( _listPlacement.size() );
        return bInRange ? &_listPlacement[placementID - kPlacementIDBase] : nullptr;
    }

    int32 ValeWorld::getPartySize( uint32 avatarID ) const
    {
        const ValeAvatar* pAvatar = findAvatar( avatarID );
        if ( pAvatar == nullptr )
            return 0;
        const auto iter = _mapParty.find( pAvatar->_partyID );
        return iter != _mapParty.end() ? static_cast<int32>( iter->second._listMember.size() ) : 1;
    }

    bool ValeWorld::consumeDirtyFlag( uint32 avatarID )
    {
        ValeAvatar* pAvatar = findAvatarMutable( avatarID );
        if ( pAvatar == nullptr || pAvatar->_bChanged == SW_FALSE )
            return false;
        pAvatar->_bChanged = SW_FALSE;
        return true;
    }

    ValeAvatar* ValeWorld::findAvatarMutable( uint32 avatarID )
    {
        const auto iter = _mapAvatar.find( avatarID );
        return iter != _mapAvatar.end() ? &iter->second : nullptr;
    }

    ValeParty* ValeWorld::findParty( uint32 partyID )
    {
        const auto iter = _mapParty.find( partyID );
        return iter != _mapParty.end() ? &iter->second : nullptr;
    }

    void ValeWorld::addExperience( ValeAvatar& avatar, int32 experience )
    {
        ValeProfile& profile = avatar._profile;
        profile._experience += experience;
        const vector<int32>& listStep = _rules._listLevelExperience;
        while ( profile._level - 1 < static_cast<int32>( listStep.size() ) && profile._experience >= listStep[static_cast<size_t>( profile._level - 1 )] )
        {
            ++profile._level;
        }
        avatar._bChanged = SW_TRUE;
    }

    void ValeWorld::grantDefeatReward( ValeAvatar& striker, const float3& at )
    {
        striker._profile._coins += _rules._creatureCoins;
        ++striker._profile._defeatCount;
        striker._bChanged = SW_TRUE;
        ValeParty* pParty = striker._partyID != 0 ? findParty( striker._partyID ) : nullptr;
        if ( pParty == nullptr )
        {
            addExperience( striker, _rules._creatureExperience );
            return;
        }
        // 나눔 거리 안의 파티원(친 사람은 늘 든다)이 똑같이 나눈다. 나머지는 올림이라 한 사람 몫이 줄지 않는다.
        vector<ValeAvatar*> listShare;
        for ( const uint32 memberID : pParty->_listMember )
        {
            ValeAvatar* pMember = findAvatarMutable( memberID );
            if ( pMember == nullptr )
                continue;
            const bool bNear = pMember == &striker || float3::getHorizontalDistance( pMember->_profile._position, at ) <= _rules._partyShareRange;
            if ( bNear )
                listShare.push_back( pMember );
        }
        const int32 shareCount = static_cast<int32>( listShare.size() );
        const int32 share      = ( _rules._creatureExperience + shareCount - 1 ) / shareCount;
        for ( ValeAvatar* pMember : listShare )
        {
            addExperience( *pMember, share );
        }
    }

    void ValeWorld::detachFromParty( ValeAvatar& avatar )
    {
        ValeParty* pParty = avatar._partyID != 0 ? findParty( avatar._partyID ) : nullptr;
        avatar._partyID   = 0;
        avatar._bChanged  = SW_TRUE;
        if ( pParty == nullptr )
            return;
        vector<uint32>& listMember = pParty->_listMember;
        for ( size_t index = 0; index < listMember.size(); ++index )
        {
            if ( listMember[index] != avatar._avatarID )
                continue;
            listMember.erase( listMember.begin() + static_cast<ptrdiff_t>( index ) );
            break;
        }
        for ( const uint32 memberID : listMember )
        {
            findAvatarMutable( memberID )->_bChanged = SW_TRUE;
        }
        if ( listMember.size() > 1 )
            return;
        // 한 사람만 남으면 파티가 아니다.
        for ( const uint32 memberID : listMember )
        {
            findAvatarMutable( memberID )->_partyID = 0;
        }
        const uint32 partyID = pParty->_partyID; // 지우는 원소 안의 값을 열쇠로 넘기지 않는다
        _mapParty.erase( partyID );
    }
} // namespace sw
