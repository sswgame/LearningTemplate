#include "pch.h"

#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherContract.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Quest/QuestLog.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Rpg/WitcherRpg/WitcherCatalog.h"

namespace sw
{
    namespace
    {
        struct WitcherContractInternal
        {
            static float32 computeDistanceSquared( const float3& lhs, const float3& rhs )
            {
                const float32 dx = lhs._x - rhs._x;
                const float32 dy = lhs._y - rhs._y;
                const float32 dz = lhs._z - rhs._z;
                return dx * dx + dy * dy + dz * dz;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( WitcherClueResult result )
    {
        switch ( result )
        {
            case WitcherClueResult::Found:
                return "Found";
            case WitcherClueResult::UnknownClue:
                return "UnknownClue";
            case WitcherClueResult::AlreadyFound:
                return "AlreadyFound";
            case WitcherClueResult::OutOfOrder:
                return "OutOfOrder";
            case WitcherClueResult::TooFar:
                return "TooFar";
            case WitcherClueResult::Solved:
                return "Solved";
        }
        return "Unknown";
    }

    const utf8* toString( WitcherHaggleResult result )
    {
        switch ( result )
        {
            case WitcherHaggleResult::Accepted:
                return "Accepted";
            case WitcherHaggleResult::Countered:
                return "Countered";
            case WitcherHaggleResult::BrokenOff:
                return "BrokenOff";
            case WitcherHaggleResult::Closed:
                return "Closed";
        }
        return "Unknown";
    }

    WitcherInvestigation::WitcherInvestigation()
        : _listFound{}
        , _eventBuffer{}
        , _pCatalog{ nullptr }
        , _pContract{ nullptr }
        , _pQuestLog{ nullptr }
        , _stepIndex{ 0 }
    {
    }

    bool WitcherInvestigation::initialize( const WitcherCatalog* pCatalog, const hashed_string& contractId, QuestLog* pQuestLog )
    {
        _pCatalog  = pCatalog;
        _pContract = pCatalog != nullptr ? pCatalog->findContract( contractId ) : nullptr;
        _pQuestLog = pQuestLog;
        _stepIndex = 0;
        _listFound.clear();
        _eventBuffer.clear();
        return _pContract != nullptr;
    }

    void WitcherInvestigation::senseClues( const float3& position, float32 senseRadius, vector<const WitcherClueDef*>& outListClue ) const
    {
        outListClue.clear();
        if ( isSolved() )
            return;
        const int32 openOrder = computeOpenOrder();
        for ( const WitcherClueDef& clue : _pContract->_listStep[static_cast<size_t>( _stepIndex )]._listClue )
        {
            if ( clue._order > openOrder || isFoundInternal( clue._id ) )
                continue;
            if ( WitcherContractInternal::computeDistanceSquared( position, clue._position ) <= senseRadius * senseRadius )
                outListClue.push_back( &clue );
        }
    }

    WitcherClueResult WitcherInvestigation::investigate( const hashed_string& clueId, const float3& position )
    {
        if ( isSolved() )
            return WitcherClueResult::Solved;
        const WitcherContractStepDef& step  = _pContract->_listStep[static_cast<size_t>( _stepIndex )];
        const WitcherClueDef*         pClue = nullptr;
        for ( const WitcherClueDef& clue : step._listClue )
        {
            if ( clue._id == clueId )
                pClue = &clue;
        }
        if ( pClue == nullptr )
            return WitcherClueResult::UnknownClue;
        if ( isFoundInternal( clueId ) )
            return WitcherClueResult::AlreadyFound;
        if ( pClue->_order > computeOpenOrder() )
            return WitcherClueResult::OutOfOrder;
        if ( WitcherContractInternal::computeDistanceSquared( position, pClue->_position ) > pClue->_radius * pClue->_radius )
            return WitcherClueResult::TooFar;

        _listFound.push_back( clueId );
        WitcherInvestigationEvent found;
        found._kind   = WitcherInvestigationEvent::Kind::ClueFound;
        found._stepId = step._id;
        found._clueId = clueId;
        _eventBuffer.push( found );
        if ( _pQuestLog != nullptr )
            (void)_pQuestLog->notify( hashed_string( kClueNotifyKind ), clueId );
        if ( computeOpenOrder() >= 0 )
            return WitcherClueResult::Found;

        // 단계의 단서를 모두 찾았다 — 일지에 알리고 다음 단계로. 단서가 없는 단계는 바로 넘긴다.
        while ( _stepIndex < static_cast<int32>( _pContract->_listStep.size() ) && computeOpenOrder() < 0 )
        {
            const hashed_string       stepId = _pContract->_listStep[static_cast<size_t>( _stepIndex )]._id;
            WitcherInvestigationEvent completed;
            completed._kind   = WitcherInvestigationEvent::Kind::StepCompleted;
            completed._stepId = stepId;
            _eventBuffer.push( completed );
            if ( _pQuestLog != nullptr )
                (void)_pQuestLog->notify( hashed_string( kStepNotifyKind ), stepId );
            ++_stepIndex;
            _listFound.clear();
        }
        if ( isSolved() )
        {
            WitcherInvestigationEvent solved;
            solved._kind = WitcherInvestigationEvent::Kind::Solved;
            _eventBuffer.push( solved );
        }
        return WitcherClueResult::Found;
    }

    bool WitcherInvestigation::isClueFound( const hashed_string& clueId ) const
    {
        if ( _pContract == nullptr )
            return false;
        if ( isFoundInternal( clueId ) )
            return true;
        // 지난 단계의 단서는 모두 찾았다.
        for ( int32 stepIndex = 0; stepIndex < _stepIndex && stepIndex < static_cast<int32>( _pContract->_listStep.size() ); ++stepIndex )
        {
            for ( const WitcherClueDef& clue : _pContract->_listStep[static_cast<size_t>( stepIndex )]._listClue )
            {
                if ( clue._id == clueId )
                    return true;
            }
        }
        return false;
    }

    bool WitcherInvestigation::isSolved() const { return _pContract == nullptr || _stepIndex >= static_cast<int32>( _pContract->_listStep.size() ); }

    hashed_string WitcherInvestigation::getStepId() const { return isSolved() ? hashed_string{} : _pContract->_listStep[static_cast<size_t>( _stepIndex )]._id; }

    void WitcherInvestigation::drainEvents( vector<WitcherInvestigationEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    int32 WitcherInvestigation::computeOpenOrder() const
    {
        if ( isSolved() )
            return -1;
        int32 openOrder = -1;
        for ( const WitcherClueDef& clue : _pContract->_listStep[static_cast<size_t>( _stepIndex )]._listClue )
        {
            if ( isFoundInternal( clue._id ) == false && ( openOrder < 0 || clue._order < openOrder ) )
                openOrder = clue._order;
        }
        return openOrder;
    }

    bool WitcherInvestigation::isFoundInternal( const hashed_string& clueId ) const
    {
        for ( const hashed_string& found : _listFound )
        {
            if ( found == clueId )
                return true;
        }
        return false;
    }

    WitcherHaggle::WitcherHaggle()
        : _pCatalog{ nullptr }
        , _pContract{ nullptr }
        , _anger{ 0.0f }
        , _offer{ 0 }
        , _finalReward{ 0 }
        , _bClosed{ SW_FALSE }
    {
    }

    bool WitcherHaggle::initialize( const WitcherCatalog* pCatalog, const hashed_string& contractId )
    {
        _pCatalog    = pCatalog;
        _pContract   = pCatalog != nullptr ? pCatalog->findContract( contractId ) : nullptr;
        _anger       = 0.0f;
        _offer       = _pContract != nullptr ? _pContract->_reward : 0;
        _finalReward = 0;
        _bClosed     = _pContract != nullptr ? SW_FALSE : SW_TRUE;
        return _pContract != nullptr;
    }

    WitcherHaggleResult WitcherHaggle::propose( int32 askReward )
    {
        if ( _bClosed != SW_FALSE )
            return WitcherHaggleResult::Closed;
        const int32 limit = static_cast<int32>( MathUtil::floor( static_cast<float32>( _pContract->_reward ) * _pContract->_limitRatio ) );
        if ( askReward <= _offer || askReward <= limit )
        {
            _finalReward = MathUtil::max( 0, askReward );
            _bClosed     = SW_TRUE;
            return WitcherHaggleResult::Accepted;
        }
        const float32 baseReward = static_cast<float32>( MathUtil::max( 1, _pContract->_reward ) );
        _anger += _pContract->_angerPerRound + static_cast<float32>( askReward - limit ) / baseReward * _pContract->_angerScale;
        if ( _anger >= _pContract->_angerMax )
        {
            _finalReward = _pContract->_reward;
            _bClosed     = SW_TRUE;
            return WitcherHaggleResult::BrokenOff;
        }
        _offer = MathUtil::min( limit, MathUtil::max( _offer, ( _offer + askReward ) / 2 ) );
        return WitcherHaggleResult::Countered;
    }

    int32 WitcherHaggle::acceptOffer()
    {
        if ( _bClosed == SW_FALSE )
        {
            _finalReward = _offer;
            _bClosed     = SW_TRUE;
        }
        return _finalReward;
    }

    void WitcherInvestigation::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, _pContract != nullptr ? _pContract->_id : hashed_string{} );
        outArchive << _stepIndex;
        outArchive << static_cast<uint32>( _listFound.size() );
        for ( const hashed_string& clueId : _listFound )
        {
            StateArchiveUtil::writeName( outArchive, clueId );
        }
    }

    bool WitcherInvestigation::readState( Archive& archive )
    {
        hashed_string contractId;
        int32         stepIndex = 0;
        uint32        count     = 0;
        if ( _pCatalog == nullptr || StateArchiveUtil::readName( archive, contractId ) == false )
            return false;
        const WitcherContractDef* pContract = _pCatalog->findContract( contractId );
        archive >> stepIndex;
        if ( pContract == nullptr || archive.isError() || StateArchiveUtil::readCount( archive, 4, count ) == false )
            return false;
        const bool bStepInside = 0 <= stepIndex && stepIndex <= static_cast<int32>( pContract->_listStep.size() );
        if ( bStepInside == false )
            return false;
        vector<hashed_string> listFound( count );
        for ( hashed_string& clueId : listFound )
        {
            if ( StateArchiveUtil::readName( archive, clueId ) == false )
                return false;
        }
        _pContract = pContract;
        _stepIndex = stepIndex;
        _listFound = std::move( listFound );
        _eventBuffer.clear();
        return true;
    }

    void WitcherHaggle::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, _pContract != nullptr ? _pContract->_id : hashed_string{} );
        outArchive << _anger;
        outArchive << _offer;
        outArchive << _finalReward;
        outArchive << _bClosed;
    }

    bool WitcherHaggle::readState( Archive& archive )
    {
        hashed_string contractId;
        float32       anger       = 0.0f;
        int32         offer       = 0;
        int32         finalReward = 0;
        uint8         bClosed     = SW_FALSE;
        if ( _pCatalog == nullptr || StateArchiveUtil::readName( archive, contractId ) == false )
            return false;
        const WitcherContractDef* pContract = _pCatalog->findContract( contractId );
        archive >> anger;
        archive >> offer;
        archive >> finalReward;
        archive >> bClosed;
        const bool bValid = pContract != nullptr && archive.isOk() && bClosed <= SW_TRUE && 0.0f <= anger;
        if ( bValid == false )
            return false;
        _pContract   = pContract;
        _anger       = anger;
        _offer       = offer;
        _finalReward = finalReward;
        _bClosed     = bClosed;
        return true;
    }
} // namespace sw
