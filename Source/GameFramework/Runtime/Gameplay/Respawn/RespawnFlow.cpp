#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Respawn/RespawnFlow.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Character/Combat/Health/HealthStateMachine.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/Respawn/CheckpointState.h"

namespace sw
{
    namespace
    {
        struct RespawnFlowInternal
        {
            /** @brief 스폰 지점이 @p team 이 쓸 수 있는 것인지(모든 팀 지점이거나 같은 팀) 봅니다. */
            static bool isUsableBy( const RespawnSpawnPoint& point, int32 team ) { return point._team < 0 || point._team == team; }

            static RespawnCandidate makeCandidate( const hashed_string& pointID, const float3& position, float32 yaw )
            {
                RespawnCandidate candidate;
                candidate._pointID  = pointID;
                candidate._position = position;
                candidate._yaw      = yaw;
                return candidate;
            }
        };
    } // namespace

    RespawnFlow::RespawnFlow()
        : _rules{}
        , _listUses{}
        , _deathPosition{}
        , _pendingPosition{}
        , _pendingPointID{}
        , _deathYaw{ 0.0f }
        , _pendingYaw{ 0.0f }
        , _delay{}
        , _choice{}
        , _cursor{ 0 }
        , _choiceEntry{ constant::invalid_index::kInt32 }
        , _pendingEntry{ constant::invalid_index::kInt32 }
        , _phase{ RespawnPhase::Alive }
    {
    }

    bool RespawnFlow::initialize( const RespawnRules& rules )
    {
        const string errors = rules.describeErrors();
        if ( errors.empty() == false )
        {
            SW_LOG_ERROR_ID( "RespawnFlow.RefusedRules", "RespawnFlow: refused rules - %#", errors.c_str() );
            return false;
        }
        _rules = rules;
        reset();
        return true;
    }

    void RespawnFlow::reset()
    {
        _listUses.assign( _rules._listPolicy.size(), 0 );
        _deathPosition = float3{};
        _deathYaw      = 0.0f;
        _delay.clear();
        _choice.clear();
        _cursor      = 0;
        _choiceEntry = constant::invalid_index::kInt32;
        clearPending();
        _phase = RespawnPhase::Alive;
    }

    bool RespawnFlow::notifyDied( const float3& position, float32 yaw )
    {
        if ( _phase != RespawnPhase::Alive || _rules._listPolicy.empty() )
            return false;
        _deathPosition = position;
        _deathYaw      = yaw;
        _delay.start( _rules._delay );
        _choice.clear();
        _cursor      = 0;
        _choiceEntry = constant::invalid_index::kInt32;
        clearPending();
        _phase = RespawnPhase::Waiting;
        return true;
    }

    optional<RespawnSpawn> RespawnFlow::update( float32 deltaTime, const RespawnContext& context )
    {
        if ( _phase == RespawnPhase::Waiting )
        {
            // 대기가 끝난 걸음에 바로 자리를 고른다(따로 한 걸음을 더 쉬지 않는다).
            (void)_delay.update( deltaTime );
            if ( _delay.isActive() )
                return nullopt;
            _phase = RespawnPhase::Search;
        }
        if ( _phase == RespawnPhase::Selection )
        {
            const float32 timeout = _rules._listPolicy[static_cast<size_t>( _choiceEntry )]._choiceTimeout;
            if ( timeout <= 0.0f )
                return nullopt;
            (void)_choice.update( deltaTime );
            if ( _choice.isActive() )
                return nullopt;
            // 시한 안에 고르지 않았다 — 고르기 다음 정책부터 저절로 시도한다.
            _cursor = _choiceEntry + 1;
            _phase  = RespawnPhase::Search;
        }
        if ( _phase != RespawnPhase::Search )
            return nullopt;
        if ( _pendingEntry < 0 )
            searchAutomatic( context );
        if ( _phase != RespawnPhase::Search || _pendingEntry < 0 )
            return nullopt;
        if ( context._pWorld != nullptr && context._pWorld->isLocationReady( _pendingPosition ) == false )
            return nullopt;
        return spawn( context );
    }

    void RespawnFlow::searchAutomatic( const RespawnContext& context )
    {
        for ( int32 index = _cursor; index < static_cast<int32>( _rules._listPolicy.size() ); ++index )
        {
            const RespawnPolicyEntry& entry = _rules._listPolicy[static_cast<size_t>( index )];
            if ( entry._bRequestOnly )
                continue;
            if ( entry._kind == RespawnPolicyKind::PlayerChoice )
            {
                _choiceEntry = index;
                _choice.start( entry._choiceTimeout );
                _phase = RespawnPhase::Selection;
                return;
            }
            RespawnCandidate candidate;
            if ( resolveEntry( index, context, candidate ) == RespawnRequestResult::Accepted )
            {
                setPending( index, candidate );
                return;
            }
        }
    }

    RespawnRequestResult RespawnFlow::resolveEntry( int32 entryIndex, const RespawnContext& context, RespawnCandidate& outCandidate ) const
    {
        const RespawnPolicyEntry& entry = _rules._listPolicy[static_cast<size_t>( entryIndex )];
        if ( entry._maxUses > 0 && _listUses[static_cast<size_t>( entryIndex )] >= entry._maxUses )
            return RespawnRequestResult::UsesExhausted;
        if ( entry._costAmount > 0 && ( context._pWallet == nullptr || context._pWallet->canAfford( entry._costCurrency, entry._costAmount ) == false ) )
            return RespawnRequestResult::CannotAfford;
        switch ( entry._kind )
        {
            case RespawnPolicyKind::LastCheckpoint:
            {
                const CheckpointRecord* pCurrent = context._pCheckpoints != nullptr ? context._pCheckpoints->getCurrent() : nullptr;
                if ( pCurrent == nullptr )
                    return RespawnRequestResult::NoLocation;
                outCandidate = RespawnFlowInternal::makeCandidate( pCurrent->_id, pCurrent->_position, pCurrent->_yaw );
                return RespawnRequestResult::Accepted;
            }
            case RespawnPolicyKind::InPlace:
            {
                // 안전한지는 세계만 안다 — 세계 질의가 없으면 죽은 자리(낭떠러지 · 용암일 수 있다)에 세우지 않는다.
                if ( context._pWorld == nullptr )
                    return RespawnRequestResult::NoLocation;
                const optional<float3> safe = context._pWorld->findSafeLocation( _deathPosition, entry._safeRadius );
                if ( safe.has_value() == false )
                    return RespawnRequestResult::NoLocation;
                outCandidate = RespawnFlowInternal::makeCandidate( hashed_string{}, *safe, _deathYaw );
                return RespawnRequestResult::Accepted;
            }
            case RespawnPolicyKind::NearestSpawnPoint:
            case RespawnPolicyKind::TeamBase:
            {
                const bool               bTeamBase = entry._kind == RespawnPolicyKind::TeamBase;
                const RespawnSpawnPoint* pBest     = nullptr;
                float32                  bestDistance{ 0.0f };
                for ( size_t index = 0; context._pSpawnPoints != nullptr && index < context._pSpawnPoints->size(); ++index )
                {
                    const RespawnSpawnPoint& point = ( *context._pSpawnPoints )[index];
                    // 팀 거점은 내 팀의 첫 지점이고, 가장 가까운 지점은 내 팀 · 모든 팀 지점 가운데 죽은 자리에서 가장 가까운 것이다(같으면 앞의 것).
                    if ( bTeamBase ? ( context._team < 0 || point._team != context._team ) : RespawnFlowInternal::isUsableBy( point, context._team ) == false )
                        continue;
                    if ( bTeamBase )
                    {
                        pBest = &point;
                        break;
                    }
                    const float32 distance = float3::getDistanceSquared( point._position, _deathPosition );
                    if ( pBest == nullptr || distance < bestDistance )
                    {
                        pBest        = &point;
                        bestDistance = distance;
                    }
                }
                if ( pBest == nullptr )
                    return RespawnRequestResult::NoLocation;
                outCandidate = RespawnFlowInternal::makeCandidate( pBest->_id, pBest->_position, pBest->_yaw );
                return RespawnRequestResult::Accepted;
            }
            case RespawnPolicyKind::PlayerChoice:
            case RespawnPolicyKind::Count:
                break;
        }
        return RespawnRequestResult::PolicyNotAllowed;
    }

    RespawnRequestResult RespawnFlow::request( RespawnPolicyKind kind, uint32 pointHash, const RespawnContext& context )
    {
        if ( _phase != RespawnPhase::Selection )
            return RespawnRequestResult::NotInSelection;
        if ( kind != RespawnPolicyKind::PlayerChoice )
        {
            const optional<int32> found = _rules.findPolicyIndex( kind );
            if ( found.has_value() == false )
                return RespawnRequestResult::PolicyNotAllowed;
            const int32                entryIndex = *found;
            RespawnCandidate           candidate;
            const RespawnRequestResult result = resolveEntry( entryIndex, context, candidate );
            if ( result == RespawnRequestResult::Accepted )
                setPending( entryIndex, candidate );
            return result;
        }
        // 지점 고르기 — 횟수 · 비용은 고르기 항목의 것이다. 고를 수 있는 것은 켠 체크포인트와 내 팀 · 모든 팀 스폰 지점뿐이다.
        const RespawnPolicyEntry& entry = _rules._listPolicy[static_cast<size_t>( _choiceEntry )];
        if ( entry._maxUses > 0 && _listUses[static_cast<size_t>( _choiceEntry )] >= entry._maxUses )
            return RespawnRequestResult::UsesExhausted;
        if ( entry._costAmount > 0 && ( context._pWallet == nullptr || context._pWallet->canAfford( entry._costCurrency, entry._costAmount ) == false ) )
            return RespawnRequestResult::CannotAfford;
        for ( size_t index = 0; context._pCheckpoints != nullptr && index < context._pCheckpoints->getActivated().size(); ++index )
        {
            const CheckpointRecord& record = context._pCheckpoints->getActivated()[index];
            if ( record._id.getHash() != pointHash )
                continue;
            setPending( _choiceEntry, RespawnFlowInternal::makeCandidate( record._id, record._position, record._yaw ) );
            return RespawnRequestResult::Accepted;
        }
        for ( size_t index = 0; context._pSpawnPoints != nullptr && index < context._pSpawnPoints->size(); ++index )
        {
            const RespawnSpawnPoint& point = ( *context._pSpawnPoints )[index];
            if ( point._id.getHash() != pointHash || RespawnFlowInternal::isUsableBy( point, context._team ) == false )
                continue;
            setPending( _choiceEntry, RespawnFlowInternal::makeCandidate( point._id, point._position, point._yaw ) );
            return RespawnRequestResult::Accepted;
        }
        return RespawnRequestResult::UnknownPoint;
    }

    void RespawnFlow::collectChoices( const RespawnContext& context, vector<hashed_string>& outListPointID ) const
    {
        for ( size_t index = 0; context._pCheckpoints != nullptr && index < context._pCheckpoints->getActivated().size(); ++index )
        {
            outListPointID.push_back( context._pCheckpoints->getActivated()[index]._id );
        }
        for ( size_t index = 0; context._pSpawnPoints != nullptr && index < context._pSpawnPoints->size(); ++index )
        {
            if ( RespawnFlowInternal::isUsableBy( ( *context._pSpawnPoints )[index], context._team ) )
                outListPointID.push_back( ( *context._pSpawnPoints )[index]._id );
        }
    }

    void RespawnFlow::setPending( int32 entryIndex, const RespawnCandidate& candidate )
    {
        _pendingEntry    = entryIndex;
        _pendingPointID  = candidate._pointID;
        _pendingPosition = candidate._position;
        _pendingYaw      = candidate._yaw;
        _phase           = RespawnPhase::Search;
    }

    void RespawnFlow::clearPending()
    {
        _pendingEntry    = constant::invalid_index::kInt32;
        _pendingPointID  = hashed_string{};
        _pendingPosition = float3{};
        _pendingYaw      = 0.0f;
    }

    optional<RespawnSpawn> RespawnFlow::spawn( const RespawnContext& context )
    {
        const RespawnPolicyEntry& entry = _rules._listPolicy[static_cast<size_t>( _pendingEntry )];
        // 고른 뒤 자리가 준비되기를 기다리는 사이에 잔액이 줄었을 수 있다 — 외상으로 세우지 않고 다시 고른다.
        if ( entry._costAmount > 0 && ( context._pWallet == nullptr || context._pWallet->canAfford( entry._costCurrency, entry._costAmount ) == false ) )
        {
            clearPending();
            return nullopt;
        }
        RespawnSpawn result;
        result._pointID                = _pendingPointID;
        result._position               = _pendingPosition;
        result._yaw                    = _pendingYaw;
        result._policy                 = entry._kind;
        result._experiencePenaltyRatio = _rules._penaltyExperienceRatio;
        result._durabilityPenaltyRatio = _rules._penaltyDurabilityRatio;
        ++_listUses[static_cast<size_t>( _pendingEntry )];
        if ( context._pWallet != nullptr )
        {
            if ( entry._costAmount > 0 )
            {
                context._pWallet->charge( entry._costCurrency, entry._costAmount );
                result._costCharged = entry._costAmount;
            }
            if ( _rules._penaltyCurrency.empty() == false )
            {
                const int64 balance = context._pWallet->getBalance( _rules._penaltyCurrency );
                const int64 byRatio = static_cast<int64>( MathUtil::floor( static_cast<float64>( balance ) * static_cast<float64>( _rules._penaltyCurrencyRatio ) ) );
                const int64 penalty = MathUtil::min( balance, byRatio + static_cast<int64>( _rules._penaltyCurrencyAmount ) );
                if ( penalty > 0 )
                {
                    context._pWallet->charge( _rules._penaltyCurrency, penalty );
                    result._currencyPenalty = penalty;
                }
            }
        }
        if ( context._pHealth != nullptr )
        {
            context._pHealth->respawn();
            if ( _rules._invulnerableTime > 0.0f )
                context._pHealth->setInvulnerable( _rules._invulnerableTime );
        }
        _delay.clear();
        _choice.clear();
        _cursor      = 0;
        _choiceEntry = constant::invalid_index::kInt32;
        clearPending();
        _phase = RespawnPhase::Alive;
        return result;
    }

    int32 RespawnFlow::getPolicyUses( RespawnPolicyKind kind ) const
    {
        const optional<int32> index = _rules.findPolicyIndex( kind );
        return index.has_value() ? _listUses[static_cast<size_t>( *index )] : 0;
    }

    bool RespawnFlow::isStateConsistent() const
    {
        const int32 policyCount = static_cast<int32>( _rules._listPolicy.size() );
        if ( static_cast<int32>( _listUses.size() ) != policyCount || _cursor > policyCount || _pendingEntry >= policyCount || _choiceEntry >= policyCount )
            return false;
        for ( const int32 uses : _listUses )
        {
            if ( uses < 0 )
                return false;
        }
        const bool bChoiceValid = _choiceEntry < 0 || _rules._listPolicy[static_cast<size_t>( _choiceEntry )]._kind == RespawnPolicyKind::PlayerChoice;
        switch ( _phase )
        {
            case RespawnPhase::Alive:
            case RespawnPhase::Waiting:
                return _pendingEntry < 0 && bChoiceValid;
            case RespawnPhase::Selection:
                return _pendingEntry < 0 && _choiceEntry >= 0 && bChoiceValid;
            case RespawnPhase::Search:
                return bChoiceValid;
        }
        return false;
    }

    void RespawnFlow::serializeState( StateIO& io )
    {
        io.reflected( *this ); // 단계 · 자리 · 횟수 · 커서 — 칸 바닥은 `PROPERTY( Min )`
        io.value( _delay );    // 리플렉션 밖 값 — 남은 시간은 손 코덱이다
        io.value( _choice );
        // 규칙은 `initialize` 의 것이다 — 다른 규칙(정책 수)으로 쓴 바이트나 단계와 어긋난 자리는 다음 `update` 가 목록 밖을 읽게 한다.
        if ( io.isReading() && io.isOk() )
            io.check( isStateConsistent() );
    }
} // namespace sw
