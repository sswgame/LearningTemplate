#include "pch.h"

#include "Engine/Character/Hit/MeleeSweepHistory.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"

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

    MeleeSweepHistory::MeleeSweepHistory()
        : _listRecord{}
        , _frame{ 0 }
    {
    }

    MeleeSweepHistory* MeleeSweepHistory::find( const GameObjectManager& manager )
    {
        // 이 키로는 이 타입만 붙인다(`findOrCreate`).
        return static_cast<MeleeSweepHistory*>( manager.findFrameSystem( hashed_string( kFrameSystemKey ) ) );
    }

    MeleeSweepHistory& MeleeSweepHistory::findOrCreate( GameObjectManager& manager )
    {
        MeleeSweepHistory* pLedger = find( manager );
        if ( pLedger != nullptr )
            return *pLedger;
        unique_ptr<MeleeSweepHistory> ledger = make_unique<MeleeSweepHistory>();
        pLedger                              = ledger.get();
        const bool bAdded                    = manager.addFrameSystem( hashed_string( kFrameSystemKey ), std::move( ledger ) );
        SW_ASSERT( bAdded ); // 같은 키가 없음을 바로 위에서 보았다
        (void)bAdded;
        return *pLedger;
    }

    void MeleeSweepHistory::release( GameObjectManager& manager )
    {
        if ( find( manager ) != nullptr )
            manager.removeFrameSystem( hashed_string( kFrameSystemKey ) );
    }

    void MeleeSweepHistory::runLane( SceneJobContext& context )
    {
        (void)context;
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

    SceneFrameAccess MeleeSweepHistory::getFrameAccess() const
    {
        SceneFrameAccess access{};
        access._writes = { SceneDataKind::HitRecord };
        return access;
    }

    void MeleeSweepHistory::record( uint64 attackerID, const MeleeHitParams& params, vector_reference<const float3> listPrevious, vector_reference<const float3> listCurrent )
    {
        SweepRecord& entry = _listRecord.emplace_back();
        entry._attackerID  = attackerID;
        entry._frame       = _frame;
        entry._params      = params;
        entry._listCurrent.assign( listCurrent.begin(), listCurrent.end() );
        // 지난 칼날은 길이가 같을 때만 쓴다(`MeleeHit::sweep` 와 같은 규칙).
        if ( listPrevious.size() == listCurrent.size() )
            entry._listPrevious.assign( listPrevious.begin(), listPrevious.end() );
    }

    float32 MeleeSweepHistory::computeDistanceSquaredToSegment( const float3& point, const float3& from, const float3& to )
    {
        const float3  segment       = to - from;
        const float32 segmentLength = segment.getLengthSquared();
        float32       along         = 0.0f;
        if ( segmentLength > MathUtil::kEpsilon * MathUtil::kEpsilon )
            along = MathUtil::clamp( ( point - from ).dot( segment ) / segmentLength, 0.0f, 1.0f );
        return ( point - ( from + segment * along ) ).getLengthSquared();
    }

    bool MeleeSweepHistory::containsSphere( const SweepRecord& record, const float3& center, float32 radius )
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

    MeleeClaimResult MeleeSweepHistory::verify( uint64 attackerID, const MeleeHitClaim& claim, const float3& targetCenter, float32 targetRadius, MeleeHitParams* pOutParams ) const
    {
        bool bAnySweep = false;
        for ( const SweepRecord& record : _listRecord )
        {
            const uint32 distance = record._frame > claim._frame ? record._frame - claim._frame : claim._frame - record._frame;
            if ( record._attackerID != attackerID || distance > kClaimSlackFrames )
                continue;
            bAnySweep = true;
            if ( containsSphere( record, targetCenter, targetRadius ) == false )
                continue;
            if ( pOutParams != nullptr )
                *pOutParams = record._params;
            return MeleeClaimResult::Confirmed;
        }
        return bAnySweep ? MeleeClaimResult::OutOfBlade : MeleeClaimResult::NoSweep;
    }

    MeleeClaimResult MeleeSweepHistory::claimHit( GameObject& attacker, GameObject& target, const MeleeHitClaim& claim, const float3& targetCenter, float32 targetRadius,
                                                  vector<GameObjectHandle>& inoutListHitTarget ) const
    {
        MeleeHitParams         params;
        const MeleeClaimResult result = verify( attacker.getObjectID(), claim, targetCenter, targetRadius, &params );
        if ( result != MeleeClaimResult::Confirmed )
            return result;
        const GameObjectHandle handle = target.getHandle();
        if ( VectorUtil::contains( inoutListHitTarget, handle ) )
            return result; // 창에서 이미 맞았다 — 서버 스윕이 먼저 닿았다
        inoutListHitTarget.push_back( handle );
        HitInfo hit;
        hit._pInstigator = &attacker;
        hit._kind        = params._kind;
        hit._point       = targetCenter;
        hit._damage      = params._damage;
        hit._impulse     = params._impulse;
        hit._bIs2D       = params._bIs2D;
        CharacterRayHit::deliverHit( target, hit );
        return result;
    }
} // namespace sw
