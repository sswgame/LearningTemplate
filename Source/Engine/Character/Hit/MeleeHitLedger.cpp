#include "pch.h"

#include "Engine/Character/Hit/MeleeHitLedger.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    void MeleeHitClaim::write( BitWriter& writer ) const
    {
        writer.writeVarUint( _frame );
        writer.writeVarUint( _targetID );
    }

    bool MeleeHitClaim::read( BitReader& reader, MeleeHitClaim& outClaim )
    {
        const uint64 frame    = reader.readVarUint();
        const uint64 targetID = reader.readVarUint();
        if ( reader.hasOverflowed() || frame > MathUtil::kMaxUInt32 )
            return false;
        outClaim._frame    = static_cast<uint32>( frame );
        outClaim._targetID = targetID;
        return true;
    }

    MeleeHitLedger::MeleeHitLedger()
        : _listRecord{}
        , _frame{ 0 }
    {
    }

    MeleeHitLedger* MeleeHitLedger::find( const GameObjectManager& manager )
    {
        // 이 키로는 이 타입만 붙인다(`ensureFor`).
        return static_cast<MeleeHitLedger*>( manager.findFrameSystem( hashed_string( kFrameSystemKey ) ) );
    }

    MeleeHitLedger& MeleeHitLedger::ensureFor( GameObjectManager& manager )
    {
        MeleeHitLedger* pLedger = find( manager );
        if ( pLedger != nullptr )
            return *pLedger;
        unique_ptr<MeleeHitLedger> ledger = make_unique<MeleeHitLedger>();
        pLedger                           = ledger.get();
        const bool bAdded                 = manager.addFrameSystem( hashed_string( kFrameSystemKey ), std::move( ledger ) );
        SW_ASSERT( bAdded ); // 같은 키가 없음을 바로 위에서 보았다
        (void)bAdded;
        return *pLedger;
    }

    void MeleeHitLedger::release( GameObjectManager& manager )
    {
        if ( find( manager ) != nullptr )
            manager.removeFrameSystem( hashed_string( kFrameSystemKey ) );
    }

    void MeleeHitLedger::runBeforeTick( GameObjectManager& manager, float32 deltaTime )
    {
        (void)manager;
        (void)deltaTime;
        ++_frame;
        // 보관 프레임 밖의 기록을 앞에서부터 버린다(기록은 프레임 순으로 쌓인다).
        size_t dropCount = 0;
        while ( dropCount < _listRecord.size() && _frame - _listRecord[dropCount]._frame > kKeepFrameCount )
        {
            ++dropCount;
        }
        if ( dropCount > 0 )
            _listRecord.erase( _listRecord.begin(), _listRecord.begin() + static_cast<std::ptrdiff_t>( dropCount ) );
    }

    void MeleeHitLedger::record( uint64 attackerID, const MeleeHitParams& params, vector_reference<const float3> listPrevious, vector_reference<const float3> listCurrent )
    {
        SweepRecord& entry = _listRecord.emplace_back();
        entry._attackerID  = attackerID;
        entry._frame       = _frame;
        entry._params      = params;
        entry._listCurrent.assign( listCurrent.begin(), listCurrent.end() );
        // 지난 칼날은 길이가 같을 때만 쓴다(`MeleeHitWindowUtil::sweep` 와 같은 규칙).
        if ( listPrevious.size() == listCurrent.size() )
            entry._listPrevious.assign( listPrevious.begin(), listPrevious.end() );
    }

    float32 MeleeHitLedger::computeDistanceSquaredToSegment( const float3& point, const float3& from, const float3& to )
    {
        const float3  segment       = to - from;
        const float32 segmentLength = segment.getLengthSquared();
        float32       along         = 0.0f;
        if ( segmentLength > MathUtil::kEpsilon * MathUtil::kEpsilon )
            along = MathUtil::clamp( ( point - from ).dot( segment ) / segmentLength, 0.0f, 1.0f );
        return ( point - ( from + segment * along ) ).getLengthSquared();
    }

    bool MeleeHitLedger::coversSphere( const SweepRecord& record, const float3& center, float32 radius )
    {
        const float32 reach        = record._params._radius + radius + kClaimToleranceMeters;
        const float32 reachSquared = reach * reach;
        if ( record._listPrevious.size() == record._listCurrent.size() )
        {
            for ( size_t pointIndex = 0; pointIndex < record._listCurrent.size(); ++pointIndex )
            {
                if ( computeDistanceSquaredToSegment( center, record._listPrevious[pointIndex], record._listCurrent[pointIndex] ) <= reachSquared )
                    return true;
            }
        }
        if ( record._listCurrent.size() >= 2 )
            return computeDistanceSquaredToSegment( center, record._listCurrent.front(), record._listCurrent.back() ) <= reachSquared;
        return false;
    }

    MeleeClaimVerdict MeleeHitLedger::verify( uint64 attackerID, const MeleeHitClaim& claim, const float3& targetCenter, float32 targetRadius, MeleeHitParams* pOutParams ) const
    {
        bool bAnySweep = false;
        for ( const SweepRecord& record : _listRecord )
        {
            const uint32 distance = record._frame > claim._frame ? record._frame - claim._frame : claim._frame - record._frame;
            if ( record._attackerID != attackerID || distance > kClaimSlackFrames )
                continue;
            bAnySweep = true;
            if ( coversSphere( record, targetCenter, targetRadius ) == false )
                continue;
            if ( pOutParams != nullptr )
                *pOutParams = record._params;
            return MeleeClaimVerdict::Confirmed;
        }
        return bAnySweep ? MeleeClaimVerdict::OutOfBlade : MeleeClaimVerdict::NoSweep;
    }

    MeleeClaimVerdict MeleeHitLedger::claimHit( GameObject& attacker, GameObject& target, const MeleeHitClaim& claim, const float3& targetCenter, float32 targetRadius,
                                                vector<GameObjectHandle>& inoutListHitTarget ) const
    {
        MeleeHitParams          params;
        const MeleeClaimVerdict verdict = verify( attacker.getObjectID(), claim, targetCenter, targetRadius, &params );
        if ( verdict != MeleeClaimVerdict::Confirmed )
            return verdict;
        const GameObjectHandle handle = target.getHandle();
        if ( VectorUtil::contains( inoutListHitTarget, handle ) )
            return verdict; // 창에서 이미 맞았다 — 서버 스윕이 먼저 닿았다
        inoutListHitTarget.push_back( handle );
        HitInfo hit;
        hit._pInstigator = &attacker;
        hit._kind        = params._kind;
        hit._point       = targetCenter;
        hit._damage      = params._damage;
        hit._impulse     = params._impulse;
        hit._bIs2D       = params._bIs2D;
        CharacterHitUtil::deliverHit( target, hit );
        return verdict;
    }
} // namespace sw
