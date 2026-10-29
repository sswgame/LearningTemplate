#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/MonsterHuntContract.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Quest/QuestLog.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Catalog/MonsterHuntCatalog.h"

namespace sw
{
    namespace
    {
        float32 computeDistanceSquared( const float3& lhs, const float3& rhs )
        {
            const float32 dx = lhs._x - rhs._x;
            const float32 dy = lhs._y - rhs._y;
            const float32 dz = lhs._z - rhs._z;
            return dx * dx + dy * dy + dz * dz;
        }
    } // namespace
} // namespace sw

namespace sw
{
    MonsterHuntInvestigation::MonsterHuntInvestigation()
        : _listFound{}
        , _eventBuffer{}
        , _pCatalog{ nullptr }
        , _pContract{ nullptr }
        , _pQuestLog{ nullptr }
        , _stepIndex{ 0 }
    {
    }

    bool MonsterHuntInvestigation::initialize( const MonsterHuntCatalog* pCatalog, const hashed_string& contractID, QuestLog* pQuestLog )
    {
        _pCatalog  = pCatalog;
        _pContract = pCatalog != nullptr ? pCatalog->findContract( contractID ) : nullptr;
        _pQuestLog = pQuestLog;
        _stepIndex = 0;
        _listFound.clear();
        _eventBuffer.clear();
        return _pContract != nullptr;
    }

    void MonsterHuntInvestigation::senseClues( const float3& position, float32 senseRadius, vector<const MonsterHuntClueDef*>& outListClue ) const
    {
        outListClue.clear();
        if ( isSolved() )
            return;
        const int32 openOrder = computeOpenOrder();
        for ( const MonsterHuntClueDef& clue : _pContract->_listStep[static_cast<size_t>( _stepIndex )]._listClue )
        {
            if ( clue._order > openOrder || isFoundInternal( clue._id ) )
                continue;
            if ( computeDistanceSquared( position, clue._position ) <= senseRadius * senseRadius )
                outListClue.push_back( &clue );
        }
    }

    MonsterHuntClueResult MonsterHuntInvestigation::investigate( const hashed_string& clueID, const float3& position )
    {
        if ( isSolved() )
            return MonsterHuntClueResult::Solved;
        const MonsterHuntContractStepDef& step  = _pContract->_listStep[static_cast<size_t>( _stepIndex )];
        const MonsterHuntClueDef*         pClue = nullptr;
        for ( const MonsterHuntClueDef& clue : step._listClue )
        {
            if ( clue._id == clueID )
                pClue = &clue;
        }
        if ( pClue == nullptr )
            return MonsterHuntClueResult::UnknownClue;
        if ( isFoundInternal( clueID ) )
            return MonsterHuntClueResult::AlreadyFound;
        if ( pClue->_order > computeOpenOrder() )
            return MonsterHuntClueResult::OutOfOrder;
        if ( computeDistanceSquared( position, pClue->_position ) > pClue->_radius * pClue->_radius )
            return MonsterHuntClueResult::TooFar;

        _listFound.push_back( clueID );
        MonsterHuntInvestigationEvent found;
        found._kind   = MonsterHuntInvestigationEvent::Kind::ClueFound;
        found._stepID = step._id;
        found._clueID = clueID;
        _eventBuffer.push( found );
        if ( _pQuestLog != nullptr )
            (void)_pQuestLog->notify( hashed_string( kClueNotifyKind ), clueID );
        if ( computeOpenOrder() >= 0 )
            return MonsterHuntClueResult::Found;

        // 단계의 단서를 모두 찾았다. 일지에 알리고 다음 단계로 넘어간다. 단서가 없는 단계는 바로 넘긴다.
        while ( _stepIndex < static_cast<int32>( _pContract->_listStep.size() ) && computeOpenOrder() < 0 )
        {
            const hashed_string           stepID = _pContract->_listStep[static_cast<size_t>( _stepIndex )]._id;
            MonsterHuntInvestigationEvent completed;
            completed._kind   = MonsterHuntInvestigationEvent::Kind::StepCompleted;
            completed._stepID = stepID;
            _eventBuffer.push( completed );
            if ( _pQuestLog != nullptr )
                (void)_pQuestLog->notify( hashed_string( kStepNotifyKind ), stepID );
            ++_stepIndex;
            _listFound.clear();
        }
        if ( isSolved() )
        {
            MonsterHuntInvestigationEvent solved;
            solved._kind = MonsterHuntInvestigationEvent::Kind::Solved;
            _eventBuffer.push( solved );
        }
        return MonsterHuntClueResult::Found;
    }

    bool MonsterHuntInvestigation::isClueFound( const hashed_string& clueID ) const
    {
        if ( _pContract == nullptr )
            return false;
        if ( isFoundInternal( clueID ) )
            return true;
        // 지난 단계의 단서는 모두 찾았다.
        for ( int32 stepIndex = 0; stepIndex < _stepIndex && stepIndex < static_cast<int32>( _pContract->_listStep.size() ); ++stepIndex )
        {
            for ( const MonsterHuntClueDef& clue : _pContract->_listStep[static_cast<size_t>( stepIndex )]._listClue )
            {
                if ( clue._id == clueID )
                    return true;
            }
        }
        return false;
    }

    bool MonsterHuntInvestigation::isSolved() const
    {
        return _pContract == nullptr || _stepIndex >= static_cast<int32>( _pContract->_listStep.size() );
    }

    hashed_string MonsterHuntInvestigation::getStepID() const
    {
        return isSolved() ? hashed_string{} : _pContract->_listStep[static_cast<size_t>( _stepIndex )]._id;
    }

    void MonsterHuntInvestigation::drainEvents( vector<MonsterHuntInvestigationEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    int32 MonsterHuntInvestigation::computeOpenOrder() const
    {
        if ( isSolved() )
            return -1;
        int32 openOrder = -1;
        for ( const MonsterHuntClueDef& clue : _pContract->_listStep[static_cast<size_t>( _stepIndex )]._listClue )
        {
            if ( isFoundInternal( clue._id ) == false && ( openOrder < 0 || clue._order < openOrder ) )
                openOrder = clue._order;
        }
        return openOrder;
    }

    bool MonsterHuntInvestigation::isFoundInternal( const hashed_string& clueID ) const
    {
        return VectorUtil::contains( _listFound, clueID );
    }

    MonsterHuntHaggle::MonsterHuntHaggle()
        : _pCatalog{ nullptr }
        , _pContract{ nullptr }
        , _anger{ 0.0f }
        , _offer{ 0 }
        , _finalReward{ 0 }
        , _bClosed{ SW_FALSE }
    {
    }

    bool MonsterHuntHaggle::initialize( const MonsterHuntCatalog* pCatalog, const hashed_string& contractID )
    {
        _pCatalog    = pCatalog;
        _pContract   = pCatalog != nullptr ? pCatalog->findContract( contractID ) : nullptr;
        _anger       = 0.0f;
        _offer       = _pContract != nullptr ? _pContract->_reward : 0;
        _finalReward = 0;
        _bClosed     = _pContract != nullptr ? SW_FALSE : SW_TRUE;
        return _pContract != nullptr;
    }

    MonsterHuntHaggleResult MonsterHuntHaggle::propose( int32 askReward )
    {
        if ( _bClosed != SW_FALSE )
            return MonsterHuntHaggleResult::Closed;
        const int32 limit = static_cast<int32>( MathUtil::floor( static_cast<float32>( _pContract->_reward ) * _pContract->_limitRatio ) );
        if ( askReward <= _offer || askReward <= limit )
        {
            _finalReward = MathUtil::max( 0, askReward );
            _bClosed     = SW_TRUE;
            return MonsterHuntHaggleResult::Accepted;
        }
        const float32 baseReward = static_cast<float32>( MathUtil::max( 1, _pContract->_reward ) );
        _anger += _pContract->_angerPerRound + static_cast<float32>( askReward - limit ) / baseReward * _pContract->_angerScale;
        if ( _anger >= _pContract->_angerMax )
        {
            _finalReward = _pContract->_reward;
            _bClosed     = SW_TRUE;
            return MonsterHuntHaggleResult::BrokenOff;
        }
        _offer = MathUtil::min( limit, MathUtil::max( _offer, ( _offer + askReward ) / 2 ) );
        return MonsterHuntHaggleResult::Countered;
    }

    int32 MonsterHuntHaggle::acceptOffer()
    {
        if ( _bClosed == SW_FALSE )
        {
            _finalReward = _offer;
            _bClosed     = SW_TRUE;
        }
        return _finalReward;
    }

    void MonsterHuntInvestigation::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, _pContract != nullptr ? _pContract->_id : hashed_string{} );
        outArchive << _stepIndex;
        outArchive << static_cast<uint32>( _listFound.size() );
        for ( const hashed_string& clueID : _listFound )
        {
            StateArchiveUtil::writeName( outArchive, clueID );
        }
    }

    bool MonsterHuntInvestigation::readState( Archive& archive )
    {
        hashed_string contractID;
        int32         stepIndex = 0;
        uint32        count     = 0;
        if ( _pCatalog == nullptr || StateArchiveUtil::readName( archive, contractID ) == false )
            return false;
        const MonsterHuntContractDef* pContract = _pCatalog->findContract( contractID );
        archive >> stepIndex;
        const bool bInvalidContractData = pContract == nullptr || archive.isError() || StateArchiveUtil::readCount( archive, 4, count ) == false;
        if ( bInvalidContractData )
            return false;
        const bool bStepInside = 0 <= stepIndex && stepIndex <= static_cast<int32>( pContract->_listStep.size() );
        if ( bStepInside == false )
            return false;
        vector<hashed_string> listFound( count );
        for ( hashed_string& clueID : listFound )
        {
            if ( StateArchiveUtil::readName( archive, clueID ) == false )
                return false;
        }
        _pContract = pContract;
        _stepIndex = stepIndex;
        _listFound = std::move( listFound );
        _eventBuffer.clear();
        return true;
    }

    void MonsterHuntHaggle::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, _pContract != nullptr ? _pContract->_id : hashed_string{} );
        outArchive << _anger;
        outArchive << _offer;
        outArchive << _finalReward;
        outArchive << _bClosed;
    }

    bool MonsterHuntHaggle::readState( Archive& archive )
    {
        hashed_string contractID;
        float32       anger       = 0.0f;
        int32         offer       = 0;
        int32         finalReward = 0;
        uint8         bClosed     = SW_FALSE;
        if ( _pCatalog == nullptr || StateArchiveUtil::readName( archive, contractID ) == false )
            return false;
        const MonsterHuntContractDef* pContract = _pCatalog->findContract( contractID );
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
