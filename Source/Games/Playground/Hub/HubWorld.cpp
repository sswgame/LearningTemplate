#include "pch.h"

#include "Games/Playground/Hub/HubWorld.h"

#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // HubProfile
    // ------------------------------------------------------------------------------
    bool HubProfile::write( BitWriter& writer ) const
    {
        kFormat.writeVersion( writer );
        return ReplicatedLayout::writeFull( *this, writer );
    }

    bool HubProfile::read( BitReader& reader )
    {
        if ( kFormat.readVersion( reader ) == false )
            return false;
        return ReplicatedLayout::readFull( reader, *this, nullptr ) == FixedReadResult::Ok; // `Ok` 일 때만 값을 바꾼다
    }

    AbsolutePosition HubRules::clampInside( const AbsolutePosition& position ) const
    {
        const float3& value = position._value;
        return AbsolutePosition{
            float3{ MathUtil::clamp( value._x, -_worldHalfExtent, _worldHalfExtent ), value._y, MathUtil::clamp( value._z, -_worldHalfExtent, _worldHalfExtent ) }
        };
    }

    // ------------------------------------------------------------------------------
    // HubWorld
    // ------------------------------------------------------------------------------
    HubWorld::HubWorld()
        : _rules{}
        , _mapAvatar{}
        , _mapParty{}
        , _listPlacement{}
        , _nextAvatarID{ 1 }
        , _nextPartyID{ 1 }
    {
    }

    bool HubWorld::initialize( const HubRules& rules )
    {
        _rules = rules;
        _listPlacement.clear();
        bool bValid = true;
        if ( rules._listLevelExperience.empty() || static_cast<int32>( rules._listLevelExperience.size() ) > kMaxLevelStep )
        {
            SW_LOG_ERROR_ID( "Hub.LevelTableInvalid", "Hub rules: the level table needs 1..%# entries - got %#", kMaxLevelStep, static_cast<uint32>( rules._listLevelExperience.size() ) );
            bValid = false;
        }
        for ( size_t index = 0; index < rules._listLevelExperience.size(); ++index )
        {
            const int32 previous = index == 0 ? 0 : rules._listLevelExperience[index - 1];
            if ( rules._listLevelExperience[index] > previous )
                continue;
            SW_LOG_ERROR_ID( "Hub.LevelTableNotAscending", "Hub rules: level table entry %# (%#) must be above the one before (%#)", static_cast<uint32>( index ),
                             rules._listLevelExperience[index], previous );
            bValid = false;
        }
        if ( rules._listPlacement.empty() || rules._listPlacement.size() > kMaxPlacement )
        {
            SW_LOG_ERROR_ID( "Hub.PlacementListInvalid", "Hub rules: the hub needs 1..%# creatures and herbs - got %#", kMaxPlacement, static_cast<uint32>( rules._listPlacement.size() ) );
            bValid = false;
        }
        if ( rules._listAvatarStart.empty() )
        {
            SW_LOG_ERROR_ID( "Hub.AvatarStartMissing", "Hub rules: the hub needs at least one avatar start point" );
            bValid = false;
        }
        for ( const RespawnSpawnPoint& start : rules._listAvatarStart )
        {
            if ( isInsideHub( start._position._x, start._position._z ) )
                continue;
            SW_LOG_ERROR_ID( "Hub.AvatarStartOutside", "Hub rules: the avatar start point '%#' lies outside the hub (half extent %#)", start._id.c_str(), rules._worldHalfExtent );
            bValid = false;
        }
        for ( size_t index = 0; bValid && index < rules._listPlacement.size(); ++index )
        {
            const HubPlacement& spawn = rules._listPlacement[index];
            if ( isInsideHub( spawn._position._x, spawn._position._z ) == false )
            {
                SW_LOG_ERROR_ID( "Hub.PlacementOutside", "Hub rules: spawn %# lies outside the hub (half extent %#)", static_cast<uint32>( index ), rules._worldHalfExtent );
                bValid = false;
                break;
            }
            HubPlacementState state;
            state._def         = spawn;
            state._placementID = kPlacementIDBase + static_cast<uint32>( index );
            state._health      = spawn._kind == HubPlacementKind::Creature ? rules._creatureHealth : 1;
            _listPlacement.push_back( state );
        }
        if ( bValid == false )
            _listPlacement.clear();
        return bValid;
    }

    uint32 HubWorld::addAvatar( uint64 accountID, const HubProfile& profile )
    {
        HubAvatar avatar;
        avatar._profile    = profile;
        avatar._accountID  = accountID;
        avatar._avatarID   = _nextAvatarID++;
        avatar._bChanged   = SW_TRUE;
        avatar._moveBudget = _rules._maxMoveStep; // 들어온 자리에서 한 걸음은 바로 걷는다
        // 저장된 자리가 지금 허브 밖이면(규칙이 바뀌었다) 나는 자리에서 다시 시작한다 — 진행은 그대로다.
        if ( isInsideHub( avatar._profile._position._x, avatar._profile._position._z ) == false )
            avatar._profile._position = chooseAvatarStart();
        const uint32 avatarID = avatar._avatarID;
        _mapAvatar[avatarID]  = avatar;
        return avatarID;
    }

    float3 HubWorld::chooseAvatarStart() const
    {
        float3  best{};
        float32 bestDistance = -1.0f;
        for ( const RespawnSpawnPoint& start : _rules._listAvatarStart )
        {
            float32 nearest = MathUtil::kMaxFloat;
            for ( const auto& entry : _mapAvatar )
            {
                nearest = MathUtil::min( nearest, float3::getHorizontalDistance( entry.second._profile._position, start._position ) );
            }
            if ( nearest >= _rules._startClearance )
                return start._position;
            if ( nearest > bestDistance )
            {
                bestDistance = nearest;
                best         = start._position;
            }
        }
        return best; // 시작 자리가 하나도 없는 규칙은 `initialize` 가 거절했다
    }

    void HubWorld::removeAvatar( uint32 avatarID )
    {
        HubAvatar* pAvatar = findAvatarMutable( avatarID );
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

    HubActionResult HubWorld::attackCreature( uint32 avatarID, uint32 placementID )
    {
        HubAvatar*   pAvatar = findAvatarMutable( avatarID );
        const uint32 index   = placementID - kPlacementIDBase;
        if ( pAvatar == nullptr || isPlacementOfKind( placementID, HubPlacementKind::Creature ) == false )
            return HubActionResult::UnknownTarget;
        HubPlacementState& creature = _listPlacement[index];
        if ( creature._bPresent == SW_FALSE )
            return HubActionResult::TargetGone;
        if ( pAvatar->_attackCooldown.isActive() )
            return HubActionResult::CoolingDown;
        if ( float3::getHorizontalDistance( pAvatar->_profile._position, creature._def._position ) > _rules._attackRange )
            return HubActionResult::OutOfRange;
        pAvatar->_attackCooldown.start( _rules._attackCooldownSeconds );
        if ( DepletingValue::take( creature._health, _rules._attackPower )._bDepleted == SW_FALSE )
            return HubActionResult::Ok;
        creature._bPresent = SW_FALSE;
        creature._respawn.start( _rules._creatureRespawnSeconds );
        grantDefeatReward( *pAvatar, creature._def._position );
        return HubActionResult::Ok;
    }

    HubActionResult HubWorld::gatherHerb( uint32 avatarID, uint32 placementID )
    {
        HubAvatar* pAvatar = findAvatarMutable( avatarID );
        if ( pAvatar == nullptr || isPlacementOfKind( placementID, HubPlacementKind::Herb ) == false )
            return HubActionResult::UnknownTarget;
        HubPlacementState& herb = _listPlacement[placementID - kPlacementIDBase];
        if ( herb._bPresent == SW_FALSE )
            return HubActionResult::TargetGone;
        if ( float3::getHorizontalDistance( pAvatar->_profile._position, herb._def._position ) > _rules._gatherRange )
            return HubActionResult::OutOfRange;
        herb._bPresent = SW_FALSE;
        herb._respawn.start( _rules._herbRespawnSeconds );
        ++pAvatar->_profile._herbCount;
        addExperience( *pAvatar, _rules._herbExperience );
        return HubActionResult::Ok;
    }

    HubActionResult HubWorld::moveAvatar( uint32 avatarID, const AbsolutePosition& position, float32 stepSeconds, bool bRiding )
    {
        const float3& target  = position._value; // 규칙의 계산은 모두 절대 자리다
        HubAvatar*    pAvatar = findAvatarMutable( avatarID );
        if ( pAvatar == nullptr )
            return HubActionResult::UnknownTarget;
        // 예산은 이 걸음의 시간이 채운다 — 걸음을 낸 시간(입력 몫)과 같은 시계다.
        const float32 speedLimit = bRiding ? _rules._rideSpeed : _rules._moveSpeed;
        pAvatar->_moveBudget     = MathUtil::min( _rules._maxMoveStep, pAvatar->_moveBudget + speedLimit * MathUtil::max( stepSeconds, 0.0f ) );
        if ( isInsideHub( target._x, target._z ) == false || MathUtil::isFinite( target._y ) == false )
            return HubActionResult::StepTooLong;
        // 걸음 길이만 보면 몰아서 아무 속도로나 갈 수 있었다(감사 K-1 — 공격에는 서버 쿨다운이 있는데 이동에는 없었다). 간 거리를
        // 걸음의 시간이 채우는 예산으로 막는다(언리얼 UCharacterMovementComponent 의 시간 불일치 감지 자리).
        const float32 distance = float3::getHorizontalDistance( pAvatar->_profile._position, target );
        if ( distance > _rules._maxMoveStep || distance > pAvatar->_moveBudget )
            return HubActionResult::StepTooLong;
        pAvatar->_moveBudget -= distance;
        pAvatar->_profile._position = target;
        return HubActionResult::Ok;
    }

    HubActionResult HubWorld::inviteToParty( uint32 avatarID, uint32 targetAvatarID )
    {
        HubAvatar* pLeader = findAvatarMutable( avatarID );
        HubAvatar* pTarget = findAvatarMutable( targetAvatarID );
        if ( pLeader == nullptr || pTarget == nullptr || pLeader == pTarget )
            return HubActionResult::UnknownTarget;
        if ( pTarget->_partyID != 0 )
            return HubActionResult::AlreadyInParty;
        if ( getPartySize( avatarID ) >= _rules._partyMaxSize )
            return HubActionResult::PartyFull;
        pTarget->_inviteFromID = avatarID;
        pTarget->_bChanged     = SW_TRUE;
        return HubActionResult::Ok;
    }

    HubActionResult HubWorld::acceptPartyInvite( uint32 avatarID, uint32 leaderAvatarID )
    {
        HubAvatar* pAvatar = findAvatarMutable( avatarID );
        HubAvatar* pLeader = findAvatarMutable( leaderAvatarID );
        if ( pAvatar == nullptr || pLeader == nullptr )
            return HubActionResult::UnknownTarget;
        if ( pAvatar->_inviteFromID != leaderAvatarID )
            return HubActionResult::NotInvited;
        if ( pAvatar->_partyID != 0 )
            return HubActionResult::AlreadyInParty;
        if ( getPartySize( leaderAvatarID ) >= _rules._partyMaxSize )
            return HubActionResult::PartyFull;
        if ( pLeader->_partyID == 0 )
        {
            HubParty party;
            party._partyID = _nextPartyID++;
            party._listMember.push_back( leaderAvatarID );
            pLeader->_partyID         = party._partyID;
            _mapParty[party._partyID] = party;
        }
        HubParty* pParty = findParty( pLeader->_partyID );
        pParty->_listMember.push_back( avatarID );
        pAvatar->_partyID      = pLeader->_partyID;
        pAvatar->_inviteFromID = 0;
        for ( const uint32 memberID : pParty->_listMember )
        {
            findAvatarMutable( memberID )->_bChanged = SW_TRUE;
        }
        return HubActionResult::Ok;
    }

    HubActionResult HubWorld::leaveParty( uint32 avatarID )
    {
        HubAvatar* pAvatar = findAvatarMutable( avatarID );
        if ( pAvatar == nullptr )
            return HubActionResult::UnknownTarget;
        if ( pAvatar->_partyID == 0 )
            return HubActionResult::NotInParty;
        detachFromParty( *pAvatar );
        return HubActionResult::Ok;
    }

    void HubWorld::update( float32 deltaSeconds )
    {
        for ( auto& entry : _mapAvatar )
        {
            (void)entry.second._attackCooldown.update( deltaSeconds );
        }
        for ( HubPlacementState& spawn : _listPlacement )
        {
            if ( spawn._bPresent != SW_FALSE )
                continue;
            (void)spawn._respawn.update( deltaSeconds );
            if ( spawn._respawn.isActive() )
                continue;
            spawn._bPresent = SW_TRUE;
            spawn._health   = spawn._def._kind == HubPlacementKind::Creature ? _rules._creatureHealth : 1;
        }
    }

    bool HubWorld::isPlacementOfKind( uint32 placementID, HubPlacementKind kind ) const
    {
        const bool bInRange = placementID >= kPlacementIDBase && placementID - kPlacementIDBase < static_cast<uint32>( _listPlacement.size() );
        return bInRange && _listPlacement[placementID - kPlacementIDBase]._def._kind == kind;
    }

    bool HubWorld::isInsideHub( float32 x, float32 z ) const
    {
        const bool bFinite = MathUtil::isFinite( x ) && MathUtil::isFinite( z );
        return bFinite && MathUtil::abs( x ) <= _rules._worldHalfExtent && MathUtil::abs( z ) <= _rules._worldHalfExtent;
    }

    const HubAvatar* HubWorld::findAvatar( uint32 avatarID ) const
    {
        const auto iter = _mapAvatar.find( avatarID );
        return iter != _mapAvatar.end() ? &iter->second : nullptr;
    }

    const HubAvatar* HubWorld::findAvatarByAccount( uint64 accountID ) const
    {
        for ( const auto& entry : _mapAvatar )
        {
            if ( entry.second._accountID == accountID )
                return &entry.second;
        }
        return nullptr;
    }

    const HubPlacementState* HubWorld::findPlacement( uint32 placementID ) const
    {
        const bool bInRange = placementID >= kPlacementIDBase && placementID - kPlacementIDBase < static_cast<uint32>( _listPlacement.size() );
        return bInRange ? &_listPlacement[placementID - kPlacementIDBase] : nullptr;
    }

    int32 HubWorld::getPartySize( uint32 avatarID ) const
    {
        const HubAvatar* pAvatar = findAvatar( avatarID );
        if ( pAvatar == nullptr )
            return 0;
        const auto iter = _mapParty.find( pAvatar->_partyID );
        return iter != _mapParty.end() ? static_cast<int32>( iter->second._listMember.size() ) : 1;
    }

    bool HubWorld::consumeDirtyFlag( uint32 avatarID )
    {
        HubAvatar* pAvatar = findAvatarMutable( avatarID );
        if ( pAvatar == nullptr || pAvatar->_bChanged == SW_FALSE )
            return false;
        pAvatar->_bChanged = SW_FALSE;
        return true;
    }

    HubAvatar* HubWorld::findAvatarMutable( uint32 avatarID )
    {
        const auto iter = _mapAvatar.find( avatarID );
        return iter != _mapAvatar.end() ? &iter->second : nullptr;
    }

    HubParty* HubWorld::findParty( uint32 partyID )
    {
        const auto iter = _mapParty.find( partyID );
        return iter != _mapParty.end() ? &iter->second : nullptr;
    }

    void HubWorld::addExperience( HubAvatar& avatar, int32 experience )
    {
        HubProfile& profile = avatar._profile;
        profile._experience += experience;
        const vector<int32>& listStep = _rules._listLevelExperience;
        while ( profile._level - 1 < static_cast<int32>( listStep.size() ) && profile._experience >= listStep[static_cast<size_t>( profile._level - 1 )] )
        {
            ++profile._level;
        }
        avatar._bChanged = SW_TRUE;
    }

    void HubWorld::grantDefeatReward( HubAvatar& striker, const float3& at )
    {
        striker._profile._coins += _rules._creatureCoins;
        ++striker._profile._defeatCount;
        striker._bChanged = SW_TRUE;
        HubParty* pParty  = striker._partyID != 0 ? findParty( striker._partyID ) : nullptr;
        if ( pParty == nullptr )
        {
            addExperience( striker, _rules._creatureExperience );
            return;
        }
        // 나눔 거리 안의 파티원(친 사람은 늘 든다)이 똑같이 나눈다. 나머지는 올림이라 한 사람 몫이 줄지 않는다.
        vector<HubAvatar*> listShare;
        for ( const uint32 memberID : pParty->_listMember )
        {
            HubAvatar* pMember = findAvatarMutable( memberID );
            if ( pMember == nullptr )
                continue;
            const bool bNear = pMember == &striker || float3::getHorizontalDistance( pMember->_profile._position, at ) <= _rules._partyShareRange;
            if ( bNear )
                listShare.push_back( pMember );
        }
        const int32 shareCount = static_cast<int32>( listShare.size() );
        const int32 share      = ( _rules._creatureExperience + shareCount - 1 ) / shareCount;
        for ( HubAvatar* pMember : listShare )
        {
            addExperience( *pMember, share );
        }
    }

    void HubWorld::detachFromParty( HubAvatar& avatar )
    {
        HubParty* pParty = avatar._partyID != 0 ? findParty( avatar._partyID ) : nullptr;
        avatar._partyID  = 0;
        avatar._bChanged = SW_TRUE;
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
