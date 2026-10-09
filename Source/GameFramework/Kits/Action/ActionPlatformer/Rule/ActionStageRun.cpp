#include "pch.h"

#include "GameFramework/Kits/Action/ActionPlatformer/Rule/ActionStageRun.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Action/ActionPlatformer/Catalog/ActionPlatformerCatalog.h"

namespace sw
{
    ActionStageRun::ActionStageRun()
        : _pCatalog{ nullptr }
        , _pStage{ nullptr }
        , _listSecretCommitted{}
        , _listSecretPending{}
        , _elapsed{ 0.0f }
        , _lives{ 0 }
        , _checkpointIndex{ -1 }
        , _hitCount{ 0 }
        , _deathCount{ 0 }
        , _state{ ActionStageState::NotStarted }
    {
    }

    bool ActionStageRun::start( const ActionPlatformerCatalog* pCatalog, const hashed_string& stageId )
    {
        const ActionStageDef* pStage = pCatalog != nullptr ? pCatalog->findStage( stageId ) : nullptr;
        if ( pStage == nullptr )
            return false;
        _pCatalog = pCatalog;
        _pStage   = pStage;
        restartStage();
        return true;
    }

    void ActionStageRun::restartStage()
    {
        if ( _pStage == nullptr )
            return;
        _listSecretCommitted.clear();
        _listSecretPending.clear();
        _elapsed         = 0.0f;
        _lives           = _pStage->_lives;
        _checkpointIndex = -1;
        _hitCount        = 0;
        _deathCount      = 0;
        _state           = ActionStageState::Playing;
    }

    void ActionStageRun::update( float32 deltaTime )
    {
        if ( _state == ActionStageState::Playing && deltaTime > 0.0f )
            _elapsed += deltaTime;
    }

    bool ActionStageRun::reachCheckpoint( const hashed_string& checkpointId )
    {
        if ( _state != ActionStageState::Playing )
            return false;
        for ( size_t index = 0; index < _pStage->_listCheckpoint.size(); ++index )
        {
            if ( _pStage->_listCheckpoint[index] != checkpointId )
                continue;
            // 앞 것으로 되돌아가지 않는다 — 이미 지난 체크포인트를 다시 밟아도 시작점은 그대로.
            if ( static_cast<int32>( index ) <= _checkpointIndex )
                return false;
            _checkpointIndex = static_cast<int32>( index );
            commitSecrets();
            return true;
        }
        return false;
    }

    bool ActionStageRun::collectSecret( const hashed_string& secretId )
    {
        if ( _state != ActionStageState::Playing || contains( _pStage->_listSecret, secretId ) == false || isSecretFound( secretId ) )
            return false;
        _listSecretPending.push_back( secretId );
        return true;
    }

    void ActionStageRun::registerHit()
    {
        if ( _state == ActionStageState::Playing )
            ++_hitCount;
    }

    hashed_string ActionStageRun::die()
    {
        if ( _state != ActionStageState::Playing )
            return hashed_string{};
        ++_deathCount;
        --_lives;
        _listSecretPending.clear(); // 확정되지 않은 것은 제자리로
        if ( _lives <= 0 )
        {
            _lives = 0;
            _state = ActionStageState::GameOver;
            return hashed_string{};
        }
        return _checkpointIndex >= 0 ? _pStage->_listCheckpoint[static_cast<size_t>( _checkpointIndex )] : hashed_string{};
    }

    bool ActionStageRun::clearStage()
    {
        if ( _state != ActionStageState::Playing )
            return false;
        commitSecrets();
        _state = ActionStageState::Cleared;
        return true;
    }

    ActionStageResult ActionStageRun::computeResult() const
    {
        ActionStageResult result;
        if ( _pStage == nullptr || _pCatalog == nullptr )
            return result;
        const ActionGradingRules& grading = _pCatalog->getGrading();
        result._clearTime                 = _elapsed;
        result._hitCount                  = _hitCount;
        result._deathCount                = _deathCount;
        result._secretFound               = static_cast<int32>( _listSecretCommitted.size() );
        result._secretTotal               = static_cast<int32>( _pStage->_listSecret.size() );
        // 시간: 기준 시간 안이면 만점, 넘으면 기준 ÷ 걸린 시간. 피격: 허용 수에 닿으면 0(죽음도 한 번의 피격으로 센다). 수집: 찾은 비율.
        result._timeScore    = _elapsed <= _pStage->_parTime ? 1.0f : _pStage->_parTime / _elapsed;
        const float32 hits   = static_cast<float32>( _hitCount + _deathCount );
        result._hitScore     = MathUtil::saturate( 1.0f - hits / static_cast<float32>( _pStage->_hitTolerance ) );
        result._collectScore = result._secretTotal > 0 ? static_cast<float32>( result._secretFound ) / static_cast<float32>( result._secretTotal ) : 1.0f;
        const float32 weight = grading._timeWeight + grading._hitWeight + grading._collectWeight;
        result._score        = weight > 0.0f ? 100.0f *
                                            ( result._timeScore * grading._timeWeight + result._hitScore * grading._hitWeight +
                                              result._collectScore * grading._collectWeight ) /
                                            weight
                                             : 0.0f;
        for ( const ActionGradeDef& grade : grading._listGrade )
        {
            if ( result._score + 1.0e-4f >= grade._minScore )
            {
                result._grade = grade._grade;
                break;
            }
        }
        if ( result._grade.empty() && grading._listGrade.empty() == false )
            result._grade = grading._listGrade.back()._grade;
        return result;
    }

    bool ActionStageRun::isSecretFound( const hashed_string& secretId ) const { return contains( _listSecretCommitted, secretId ) || contains( _listSecretPending, secretId ); }

    void ActionStageRun::commitSecrets()
    {
        for ( const hashed_string& secretId : _listSecretPending )
        {
            _listSecretCommitted.push_back( secretId );
        }
        _listSecretPending.clear();
    }

    bool ActionStageRun::contains( const vector<hashed_string>& listId, const hashed_string& id )
    {
        for ( const hashed_string& entry : listId )
        {
            if ( entry == id )
                return true;
        }
        return false;
    }

    void ActionStageRun::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeName( outArchive, _pStage != nullptr ? _pStage->_id : hashed_string{} );
        outArchive << static_cast<uint8>( _state );
        outArchive << _elapsed;
        outArchive << _lives;
        outArchive << _checkpointIndex;
        outArchive << _hitCount;
        outArchive << _deathCount;
        outArchive << static_cast<uint32>( _listSecretCommitted.size() );
        for ( const hashed_string& secretId : _listSecretCommitted )
        {
            StateArchiveUtil::writeName( outArchive, secretId );
        }
        outArchive << static_cast<uint32>( _listSecretPending.size() );
        for ( const hashed_string& secretId : _listSecretPending )
        {
            StateArchiveUtil::writeName( outArchive, secretId );
        }
    }

    bool ActionStageRun::readState( Archive& archive )
    {
        hashed_string stageId;
        uint8         state = 0;
        if ( StateArchiveUtil::readName( archive, stageId ) == false )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다.
        ActionStageRun restored = *this;
        restored._pStage        = stageId.empty() || _pCatalog == nullptr ? nullptr : _pCatalog->findStage( stageId );
        if ( stageId.empty() == false && restored._pStage == nullptr )
            return false;
        archive >> state;
        archive >> restored._elapsed;
        archive >> restored._lives;
        archive >> restored._checkpointIndex;
        archive >> restored._hitCount;
        archive >> restored._deathCount;
        const int32 checkpointCount = restored._pStage != nullptr ? static_cast<int32>( restored._pStage->_listCheckpoint.size() ) : 0;
        const bool  bHeadValid      = archive.isOk() && state <= static_cast<uint8>( ActionStageState::GameOver ) && 0.0f <= restored._elapsed && 0 <= restored._lives &&
                              -1 <= restored._checkpointIndex && restored._checkpointIndex < checkpointCount && 0 <= restored._hitCount && 0 <= restored._deathCount;
        if ( bHeadValid == false )
            return false;
        restored._state = static_cast<ActionStageState>( state );

        // 확정 · 지닌 비밀 순서 — 비밀은 이 스테이지의 것이어야 한다
        for ( int32 listIndex = 0; listIndex < 2; ++listIndex )
        {
            vector<hashed_string>& listSecret  = listIndex == 0 ? restored._listSecretCommitted : restored._listSecretPending;
            uint32                 secretCount = 0;
            if ( StateArchiveUtil::readCount( archive, 4, secretCount ) == false )
                return false;
            listSecret.assign( secretCount, hashed_string{} );
            for ( hashed_string& secretId : listSecret )
            {
                const bool bSecretValid = StateArchiveUtil::readName( archive, secretId ) && restored._pStage != nullptr && contains( restored._pStage->_listSecret, secretId );
                if ( bSecretValid == false )
                    return false;
            }
        }
        *this = std::move( restored );
        return true;
    }
} // namespace sw
