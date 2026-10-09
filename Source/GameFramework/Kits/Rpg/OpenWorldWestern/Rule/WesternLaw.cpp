#include "pch.h"

#include "GameFramework/Kits/Rpg/OpenWorldWestern/Rule/WesternLaw.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Kits/Rpg/OpenWorldWestern/Catalog/WesternCatalog.h"

namespace sw
{
    WesternPerceptionSight::WesternPerceptionSight( const AiPerceptionSettings& settings, const NavGrid* pGrid )
        : _perception{}
        , _pGrid{ pGrid }
    {
        _perception.setSettings( settings );
    }

    bool WesternPerceptionSight::canWitnessSee( const WesternWitness& witness, const float3& crimePosition ) const
    {
        return _perception.canSee( witness._position, witness._forward, crimePosition, _pGrid, false );
    }

    const utf8* toString( WesternPayResult result )
    {
        switch ( result )
        {
            case WesternPayResult::Ok:
                return "Ok";
            case WesternPayResult::UnknownRegion:
                return "UnknownRegion";
            case WesternPayResult::NoBounty:
                return "NoBounty";
            case WesternPayResult::ActivelyWanted:
                return "ActivelyWanted";
            case WesternPayResult::NotEnoughMoney:
                return "NotEnoughMoney";
        }
        return "Unknown";
    }

    WesternLawState::WesternLawState()
        : _listRecord{}
        , _listPending{}
        , _eventBuffer{}
        , _pCatalog{ nullptr }
        , _nextIncidentId{ 1 }
        , _bDisguised{ SW_FALSE }
    {
    }

    void WesternLawState::initialize( const WesternCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _listRecord.clear();
        _listPending.clear();
        _eventBuffer.clear();
        _nextIncidentId = 1;
        _bDisguised     = SW_FALSE;
    }

    uint32 WesternLawState::commitCrime( const hashed_string& crimeId, const hashed_string& regionId, const float3& position,
                                         const vector<WesternWitness>& listCandidate, const IWesternWitnessSight& sight, bool bMasked )
    {
        if ( _pCatalog == nullptr )
            return 0;
        const WesternCrimeDef* pCrime = _pCatalog->findCrime( crimeId );
        if ( pCrime == nullptr || _pCatalog->findRegion( regionId ) == nullptr )
            return 0;
        const uint32 incidentId = _nextIncidentId++;
        bool         bWitnessed = false;
        for ( const WesternWitness& witness : listCandidate )
        {
            if ( sight.canWitnessSee( witness, position ) == false )
                continue;
            bWitnessed = true;
            PendingReport report;
            report._crimeId    = crimeId;
            report._regionId   = regionId;
            report._witnessId  = witness._id;
            report._incidentId = incidentId;
            report._remaining.start( witness._bLawman != SW_FALSE ? 0.0f : pCrime->_reportTime );
            report._bMasked = bMasked ? SW_TRUE : SW_FALSE;
            if ( witness._bLawman != SW_FALSE )
            {
                // 보안관은 그 자리에서 — 같은 사건의 다른 신고 대기는 필요 없다.
                applyReport( report );
                for ( size_t index = _listPending.size(); index > 0; --index )
                {
                    if ( _listPending[index - 1]._incidentId == incidentId )
                        _listPending.erase( _listPending.begin() + static_cast<ptrdiff_t>( index - 1 ) );
                }
                return incidentId;
            }
            _listPending.push_back( report );
            pushEvent( WesternLawEvent::Kind::Witnessed, regionId, 0, incidentId, crimeId, witness._id );
        }
        if ( bWitnessed == false )
            pushEvent( WesternLawEvent::Kind::Unwitnessed, regionId, 0, incidentId, crimeId );
        return incidentId;
    }

    bool WesternLawState::silenceWitness( uint64 witnessId )
    {
        bool bRemoved = false;
        for ( size_t index = _listPending.size(); index > 0; --index )
        {
            const PendingReport report = _listPending[index - 1];
            if ( report._witnessId != witnessId )
                continue;
            _listPending.erase( _listPending.begin() + static_cast<ptrdiff_t>( index - 1 ) );
            bRemoved           = true;
            bool bOtherWitness = false;
            for ( const PendingReport& other : _listPending )
            {
                bOtherWitness = bOtherWitness || other._incidentId == report._incidentId;
            }
            if ( bOtherWitness == false )
                pushEvent( WesternLawEvent::Kind::ReportPrevented, report._regionId, 0, report._incidentId, report._crimeId, witnessId );
        }
        return bRemoved;
    }

    void WesternLawState::setSeenByLaw( const hashed_string& regionId, bool bSeen )
    {
        RegionRecord& record = acquireRecord( regionId );
        record._bSeenByLaw   = bSeen ? SW_TRUE : SW_FALSE;
        if ( bSeen )
            record._unseenTime = 0.0f;
    }

    void WesternLawState::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _pCatalog == nullptr )
            return;
        // 신고 — 먼저 다다른 목격자가 신고하면 같은 사건의 나머지는 지운다. 넣은 순서로 보아 결과가 늘 같다.
        for ( PendingReport& report : _listPending )
        {
            report._remaining.tick( deltaTime );
        }
        for ( size_t index = 0; index < _listPending.size(); )
        {
            if ( _listPending[index]._remaining.isActive() )
            {
                ++index;
                continue;
            }
            const PendingReport report = _listPending[index];
            applyReport( report );
            for ( size_t other = _listPending.size(); other > 0; --other )
            {
                if ( _listPending[other - 1]._incidentId == report._incidentId )
                    _listPending.erase( _listPending.begin() + static_cast<ptrdiff_t>( other - 1 ) );
            }
            index = 0;
        }

        for ( RegionRecord& record : _listRecord )
        {
            if ( record._wantedLevel <= 0 || record._bSeenByLaw != SW_FALSE )
                continue;
            const WesternRegionDef* pRegion = _pCatalog->findRegion( record._regionId );
            if ( pRegion == nullptr )
                continue;
            const float32 scale = _bDisguised != SW_FALSE ? pRegion->_disguiseScale : 1.0f;
            record._unseenTime += deltaTime * scale;
            while ( record._wantedLevel > 0 && record._unseenTime >= pRegion->_wantedCooldown )
            {
                record._unseenTime -= pRegion->_wantedCooldown;
                setWantedLevel( record, record._wantedLevel - 1 );
                pushEvent( WesternLawEvent::Kind::WantedLowered, record._regionId, record._wantedLevel );
            }
            if ( record._wantedLevel == 0 )
                record._unseenTime = 0.0f;
        }
    }

    void WesternLawState::advanceDay()
    {
        if ( _pCatalog == nullptr )
            return;
        for ( RegionRecord& record : _listRecord )
        {
            const WesternRegionDef* pRegion = _pCatalog->findRegion( record._regionId );
            if ( pRegion != nullptr )
                record._bounty = MathUtil::max( 0, record._bounty - pRegion->_bountyDecayPerDay );
        }
    }

    WesternPayResult WesternLawState::payBounty( const hashed_string& regionId, Wallet& inoutWallet )
    {
        if ( _pCatalog == nullptr || _pCatalog->findRegion( regionId ) == nullptr )
            return WesternPayResult::UnknownRegion;
        RegionRecord* pRecord = findRecordMutable( regionId );
        if ( pRecord == nullptr || pRecord->_bounty <= 0 )
            return WesternPayResult::NoBounty;
        if ( pRecord->_wantedLevel > 0 )
            return WesternPayResult::ActivelyWanted;
        const int32 bounty = pRecord->_bounty;
        if ( inoutWallet.trySpend( _pCatalog->getCurrency(), bounty ) == false )
            return WesternPayResult::NotEnoughMoney;
        pRecord->_bounty = 0;
        pushEvent( WesternLawEvent::Kind::BountyPaid, regionId, bounty );
        return WesternPayResult::Ok;
    }

    int32 WesternLawState::getBounty( const hashed_string& regionId ) const
    {
        const RegionRecord* pRecord = findRecord( regionId );
        return pRecord != nullptr ? pRecord->_bounty : 0;
    }

    int32 WesternLawState::getWantedLevel( const hashed_string& regionId ) const
    {
        const RegionRecord* pRecord = findRecord( regionId );
        return pRecord != nullptr ? pRecord->_wantedLevel : 0;
    }

    const WesternPursuitDef* WesternLawState::findPursuit( const hashed_string& regionId ) const
    {
        return _pCatalog != nullptr ? _pCatalog->findPursuit( getWantedLevel( regionId ) ) : nullptr;
    }

    void WesternLawState::drainEvents( vector<WesternLawEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void WesternLawState::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listRecord.size() );
        for ( const RegionRecord& record : _listRecord )
        {
            StateArchiveUtil::writeName( outArchive, record._regionId );
            outArchive << record._unseenTime;
            outArchive << record._bounty;
            outArchive << record._wantedLevel;
            outArchive << record._bSeenByLaw;
        }
        outArchive << static_cast<uint32>( _listPending.size() );
        for ( const PendingReport& report : _listPending )
        {
            StateArchiveUtil::writeName( outArchive, report._crimeId );
            StateArchiveUtil::writeName( outArchive, report._regionId );
            outArchive << report._witnessId;
            outArchive << report._incidentId;
            StateArchiveUtil::writeCountdown( outArchive, report._remaining );
            outArchive << report._bMasked;
        }
        outArchive << _nextIncidentId;
        outArchive << _bDisguised;
    }

    bool WesternLawState::readState( Archive& archive )
    {
        uint32 recordCount = 0;
        // 기록마다 지역(4) + 안 보인 시간 · 현상금 · 수배(12) + 보는지(1)
        if ( _pCatalog == nullptr || StateArchiveUtil::readCount( archive, 17, recordCount ) == false )
            return false;
        vector<RegionRecord> listRecord( recordCount );
        for ( RegionRecord& record : listRecord )
        {
            if ( StateArchiveUtil::readName( archive, record._regionId ) == false || _pCatalog->findRegion( record._regionId ) == nullptr )
                return false;
            archive >> record._unseenTime;
            archive >> record._bounty;
            archive >> record._wantedLevel;
            archive >> record._bSeenByLaw;
            const bool bValid = archive.isOk() && 0 <= record._bounty && 0 <= record._wantedLevel && record._bSeenByLaw <= SW_TRUE;
            if ( bValid == false )
                return false;
        }

        uint32 pendingCount = 0;
        // 대기마다 범죄 · 지역(8) + 목격자(8) + 사건(4) + 남은 시간(4) + 가렸는지(1)
        if ( StateArchiveUtil::readCount( archive, 25, pendingCount ) == false )
            return false;
        vector<PendingReport> listPending( pendingCount );
        for ( PendingReport& report : listPending )
        {
            const bool bNamesRead = StateArchiveUtil::readName( archive, report._crimeId ) && StateArchiveUtil::readName( archive, report._regionId );
            if ( bNamesRead == false || _pCatalog->findCrime( report._crimeId ) == nullptr || _pCatalog->findRegion( report._regionId ) == nullptr )
                return false;
            archive >> report._witnessId;
            archive >> report._incidentId;
            if ( StateArchiveUtil::readCountdown( archive, report._remaining ) == false )
                return false;
            archive >> report._bMasked;
            if ( archive.isError() || report._bMasked > SW_TRUE )
                return false;
        }

        uint32 nextIncidentId = 0;
        uint8  bDisguised     = SW_FALSE;
        archive >> nextIncidentId;
        archive >> bDisguised;
        if ( archive.isError() || nextIncidentId == 0 || bDisguised > SW_TRUE )
            return false;
        _listRecord     = std::move( listRecord );
        _listPending    = std::move( listPending );
        _nextIncidentId = nextIncidentId;
        _bDisguised     = bDisguised;
        _eventBuffer.clear();
        return true;
    }

    WesternLawState::RegionRecord* WesternLawState::findRecordMutable( const hashed_string& regionId )
    {
        for ( RegionRecord& record : _listRecord )
        {
            if ( record._regionId == regionId )
                return &record;
        }
        return nullptr;
    }

    const WesternLawState::RegionRecord* WesternLawState::findRecord( const hashed_string& regionId ) const
    {
        for ( const RegionRecord& record : _listRecord )
        {
            if ( record._regionId == regionId )
                return &record;
        }
        return nullptr;
    }

    WesternLawState::RegionRecord& WesternLawState::acquireRecord( const hashed_string& regionId )
    {
        RegionRecord* pRecord = findRecordMutable( regionId );
        if ( pRecord != nullptr )
            return *pRecord;
        RegionRecord record;
        record._regionId = regionId;
        _listRecord.push_back( record );
        return _listRecord.back();
    }

    void WesternLawState::applyReport( const PendingReport& report )
    {
        const WesternCrimeDef*  pCrime  = _pCatalog->findCrime( report._crimeId );
        const WesternRegionDef* pRegion = _pCatalog->findRegion( report._regionId );
        if ( pCrime == nullptr || pRegion == nullptr )
            return;
        RegionRecord& record = acquireRecord( report._regionId );
        const float32 scale  = report._bMasked != SW_FALSE ? pRegion->_maskedBountyScale : 1.0f;
        record._bounty += static_cast<int32>( MathUtil::round( static_cast<float32>( pCrime->_bounty ) * scale ) );
        record._unseenTime = 0.0f;
        _bDisguised        = SW_FALSE; // 새로 신고된 모습 — 갈아입은 옷도 이제 알려졌다
        pushEvent( WesternLawEvent::Kind::Reported, report._regionId, record._bounty, report._incidentId, report._crimeId, report._witnessId );
        if ( pCrime->_wanted > 0 )
        {
            setWantedLevel( record, MathUtil::min( pRegion->_maxWanted, record._wantedLevel + pCrime->_wanted ) );
            pushEvent( WesternLawEvent::Kind::WantedRaised, report._regionId, record._wantedLevel, report._incidentId, report._crimeId );
        }
    }

    void WesternLawState::setWantedLevel( RegionRecord& record, int32 wantedLevel )
    {
        const WesternPursuitDef* pBefore = _pCatalog->findPursuit( record._wantedLevel );
        record._wantedLevel              = MathUtil::max( 0, wantedLevel );
        if ( _pCatalog->findPursuit( record._wantedLevel ) != pBefore )
            pushEvent( WesternLawEvent::Kind::PursuitChanged, record._regionId, record._wantedLevel );
    }

    void WesternLawState::pushEvent( WesternLawEvent::Kind kind, const hashed_string& regionId, int32 value, uint32 incidentId, const hashed_string& crimeId,
                                     uint64 witnessId )
    {
        WesternLawEvent event;
        event._kind       = kind;
        event._regionId   = regionId;
        event._value      = value;
        event._incidentId = incidentId;
        event._crimeId    = crimeId;
        event._witnessId  = witnessId;
        _eventBuffer.push( event );
    }
} // namespace sw
