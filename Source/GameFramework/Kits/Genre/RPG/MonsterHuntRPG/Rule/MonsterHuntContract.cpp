#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/MonsterHuntContract.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Catalog/MonsterHuntCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

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
        if ( _pContract == nullptr )
            return false;
        advanceFinishedSteps(); // 첫 단계에 단서가 없으면 찾을 것이 없어 조사가 영영 끝나지 않는다
        return true;
    }

    void MonsterHuntInvestigation::senseClues( const float3& position, float32 senseRadius, vector<const MonsterHuntClueDef*>& outListClue ) const
    {
        outListClue.clear();
        if ( isSolved() || senseRadius < 0.0f ) // 음수 반지름은 제곱하면 양수가 되어 그 거리까지 보인다
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
        advanceFinishedSteps();
        return MonsterHuntClueResult::Found;
    }

    void MonsterHuntInvestigation::advanceFinishedSteps()
    {
        if ( computeOpenOrder() >= 0 )
            return;
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

    void MonsterHuntInvestigation::pollEvents( vector<MonsterHuntInvestigationEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    int32 MonsterHuntInvestigation::computeOpenOrder() const
    {
        if ( isSolved() )
            return constant::invalid_index::kInt32;
        int32 openOrder = constant::invalid_index::kInt32;
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
        , _bClosed{ SW_TRUE } // 계약 없이는 흥정이 끝난 상태다 — 열려 있으면 `propose` 가 빈 계약을 읽는다
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
        // 요구는 한도를 넘어 제안보다 크다. `( 제안 + 요구 ) / 2` 는 큰 요구에서 넘친다.
        _offer = MathUtil::min( limit, _offer + ( askReward - _offer ) / 2 );
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

    void MonsterHuntInvestigation::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        hashed_string contractID = _pContract != nullptr ? _pContract->_id : hashed_string{};
        io.value( contractID );
        io.value( _stepIndex );
        const MonsterHuntContractStepDef* pStep = nullptr;
        if ( io.isReading() )
        {
            _pContract = _pCatalog->findContract( contractID );
            if ( io.check( _pContract != nullptr && 0 <= _stepIndex && _stepIndex <= static_cast<int32>( _pContract->_listStep.size() ) ) == false )
                return;
            pStep = _stepIndex < static_cast<int32>( _pContract->_listStep.size() ) ? &_pContract->_listStep[static_cast<size_t>( _stepIndex )] : nullptr;
        }
        io.nameList( _listFound, [&]( const hashed_string& clueID )
        { return pStep != nullptr && VectorUtil::containsBy<&MonsterHuntClueDef::_id>( pStep->_listClue, clueID ); } );
        if ( io.isReading() )
        {
            // 찾은 단서는 지금 단계의 것이고 하나는 남아 있어야 한다. 다 찾은 단계에 멈춘 상태는 넘어갈 길이 없어 조사가 영영 끝나지 않는다.
            io.check( pStep != nullptr ? _listFound.size() < pStep->_listClue.size() : _listFound.empty() );
            _eventBuffer.clear();
        }
    }

    void MonsterHuntHaggle::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        hashed_string contractID = _pContract != nullptr ? _pContract->_id : hashed_string{};
        io.value( contractID );
        io.value( _anger );
        io.value( _offer );
        io.value( _finalReward );
        io.flag( _bClosed );
        if ( io.isReading() )
        {
            _pContract = _pCatalog->findContract( contractID );
            io.check( _pContract != nullptr && 0.0f <= _anger && 0 <= _offer && 0 <= _finalReward );
        }
    }

} // namespace sw
