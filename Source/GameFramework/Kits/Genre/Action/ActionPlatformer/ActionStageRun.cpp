#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/ActionStageRun.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/ActionPlatformerCatalog.h"
#include "GameFramework/Runtime/Character/Movement/PlatformerMotor2D.h"

namespace sw
{
    ActionStageRun::ActionStageRun()
        : _pCatalog{ nullptr }
        , _pStage{ nullptr }
        , _listSecretCommitted{}
        , _listSecretPending{}
        , _elapsed{ 0.0f }
        , _lives{ 0 }
        , _checkpoints{}
        , _hitCount{ 0 }
        , _deathCount{ 0 }
        , _state{ ActionStageState::NotStarted }
    {
        _checkpoints.setForwardOnly( true ); // 지난 체크포인트로 되돌아가지 않는다(건너뛴 것은 인정한다)
    }

    bool ActionStageRun::start( const ActionPlatformerCatalog& catalog, const hashed_string& stageID )
    {
        const ActionStageDef* pStage = catalog.findStage( stageID );
        if ( pStage == nullptr )
            return false;
        _pCatalog = &catalog;
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
        _elapsed = 0.0f;
        _lives   = _pStage->_lives;
        _checkpoints.reset();
        _hitCount   = 0;
        _deathCount = 0;
        _state      = ActionStageState::Playing;
    }

    void ActionStageRun::update( float32 deltaTime )
    {
        if ( _state == ActionStageState::Playing && deltaTime > 0.0f )
            _elapsed += deltaTime;
    }

    bool ActionStageRun::reachCheckpoint( const hashed_string& checkpointID, const float3& position )
    {
        if ( _state != ActionStageState::Playing )
            return false;
        for ( size_t index = 0; index < _pStage->_listCheckpoint.size(); ++index )
        {
            if ( _pStage->_listCheckpoint[index] != checkpointID )
                continue;
            // 앞쪽 체크포인트로 되돌아가지 않는다. 이미 지난 체크포인트를 다시 밟아도 시작점은 그대로다(공통 상태의 앞으로만 규칙).
            CheckpointDef def;
            def._id    = checkpointID;
            def._order = static_cast<int32>( index );
            if ( _checkpoints.activate( def, position, 0.0f ) != CheckpointActivation::Activated )
                return false;
            commitSecrets();
            return true;
        }
        return false;
    }

    int32 ActionStageRun::getCheckpointIndex() const
    {
        const CheckpointRecord* pCurrent = _checkpoints.getCurrent();
        return pCurrent != nullptr ? pCurrent->_order : constant::invalid_index::kInt32;
    }

    bool ActionStageRun::collectSecret( const hashed_string& secretID )
    {
        const bool bSecretNotCollectable = _state != ActionStageState::Playing || VectorUtil::contains( _pStage->_listSecret, secretID ) == false || isSecretFound( secretID );
        if ( bSecretNotCollectable )
            return false;
        _listSecretPending.push_back( secretID );
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
        _listSecretPending.clear(); // 확정되지 않은 것은 원래 자리에 다시 놓는다
        if ( _lives <= 0 )
        {
            _lives = 0;
            _state = ActionStageState::GameOver;
            return hashed_string{};
        }
        const CheckpointRecord* pCurrent = _checkpoints.getCurrent();
        return pCurrent != nullptr ? pCurrent->_id : hashed_string{};
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
        // 시간은 기준 시간 안이면 만점이고, 넘으면 기준 시간을 걸린 시간으로 나눈 값이다. 피격은 허용 수에 닿으면 0이다(죽음도 한 번의 피격으로 센다). 수집은 찾은 비율이다.
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
            if ( result._score + MathUtil::kLooseEpsilon >= grade._minScore )
            {
                result._grade = grade._grade;
                break;
            }
        }
        return result;
    }

    bool ActionStageRun::isSecretFound( const hashed_string& secretID ) const
    {
        return VectorUtil::contains( _listSecretCommitted, secretID ) || VectorUtil::contains( _listSecretPending, secretID );
    }

    void ActionStageRun::commitSecrets()
    {
        for ( const hashed_string& secretID : _listSecretPending )
        {
            _listSecretCommitted.push_back( secretID );
        }
        _listSecretPending.clear();
    }

    void ActionStageRun::serializeState( StateIO& io )
    {
        io.reflected( *this ); // 상태 · 경과 · 목숨 · 체크포인트 기록 · 맞은 수 · 죽은 수 — 바닥은 칸의 `PROPERTY( Min )`
        io.reference( _pStage, [this]( const hashed_string& id )
        { return _pCatalog != nullptr ? _pCatalog->findStage( id ) : nullptr; } );
        // 시작한 판은 스테이지가 있어야 한다. 없으면 다음 `reachCheckpoint` · `collectSecret` · `die` 가 빈 정의를 읽는다.
        // 체크포인트 기록은 앞으로만 규칙을 지키고, 기록마다 순번이 스테이지 목록 안이며 그 자리의 이름이어야 한다(안쪽 반사 구조체는 범위만 검사된다).
        bool bCheckpointsValid = _checkpoints.isConsistent();
        for ( const CheckpointRecord& record : _checkpoints.getActivated() )
        {
            const bool bInStage = _pStage != nullptr && record._order >= 0 && record._order < static_cast<int32>( _pStage->_listCheckpoint.size() );
            bCheckpointsValid   = bCheckpointsValid && bInStage && _pStage->_listCheckpoint[static_cast<size_t>( record._order )] == record._id;
        }
        if ( io.check( bCheckpointsValid && ( _state == ActionStageState::NotStarted || _pStage != nullptr ) ) == false )
            return;
        // 확정한 비밀, 지닌 비밀 순서로 읽는다. 비밀은 이 스테이지의 것이고 두 목록을 통틀어 한 번만 있어야 한다(겹치면 수집 점수가 1 을 넘는다).
        if ( io.isReading() )
        {
            _listSecretPending.clear();
        }
        const auto isNewSecret = [this]( const hashed_string& secretID )
        { return _pStage != nullptr && VectorUtil::contains( _pStage->_listSecret, secretID ) && isSecretFound( secretID ) == false; };
        io.nameList( _listSecretCommitted, isNewSecret );
        io.nameList( _listSecretPending, isNewSecret );
    }

} // namespace sw

namespace sw
{
    namespace
    {
        struct ActionPlatformerRespawnWorldInternal
        {
            /** @brief 발이 칸 윗면에 닿았다고 보는 거리(칸 크기 비율) — 땅에 선 몸은 발이 윗면에 붙어 있다. */
            static constexpr float32 kFeetTolerance = 0.05f;
        };
    } // namespace

    ActionPlatformerRespawnWorld::ActionPlatformerRespawnWorld( const PlatformTileMap& map, const float2& halfExtents )
        : _pMap{ &map }
        , _halfExtents{ halfExtents }
    {
    }

    bool ActionPlatformerRespawnWorld::isStandingCell( int32 x, int32 y ) const
    {
        if ( x < 0 || x >= _pMap->getWidth() || y < 1 || y >= _pMap->getHeight() )
            return false;
        const PlatformTile below = _pMap->getTile( x, y - 1 );
        if ( below != PlatformTile::Solid && below != PlatformTile::OneWay )
            return false;
        // 몸 높이만큼의 칸이 모두 비어야 선다(위험 · 사다리 · 경사 칸에는 세우지 않는다).
        const int32 bodyRows = MathUtil::max( 1, static_cast<int32>( MathUtil::ceil( 2.0f * _halfExtents._y / _pMap->getTileSize() ) ) );
        for ( int32 row = 0; row < bodyRows; ++row )
        {
            if ( _pMap->getTile( x, y + row ) != PlatformTile::Empty )
                return false;
        }
        return true;
    }

    optional<float3> ActionPlatformerRespawnWorld::findSafeLocation( const float3& position, float32 radius ) const
    {
        const float32 tileSize = _pMap->getTileSize();
        const float32 feetY    = position._y - _halfExtents._y;
        const int32   column   = _pMap->computeTileX( position._x );
        const int32   row      = _pMap->computeTileY( feetY + ActionPlatformerRespawnWorldInternal::kFeetTolerance * tileSize );
        const bool    bOnFloor = MathUtil::abs( feetY - _pMap->getTileBottom( row ) ) <= ActionPlatformerRespawnWorldInternal::kFeetTolerance * tileSize;
        if ( bOnFloor && isStandingCell( column, row ) )
            return position;

        optional<float3> best{};
        float32          bestDistance = 0.0f;
        const int32      minColumn    = MathUtil::max( 0, _pMap->computeTileX( position._x - radius ) );
        const int32      maxColumn    = MathUtil::min( _pMap->getWidth() - 1, _pMap->computeTileX( position._x + radius ) );
        const int32      minRow       = MathUtil::max( 1, _pMap->computeTileY( feetY - radius ) );
        const int32      maxRow       = MathUtil::min( _pMap->getHeight() - 1, _pMap->computeTileY( feetY + radius ) );
        for ( int32 y = minRow; y <= maxRow; ++y )
        {
            for ( int32 x = minColumn; x <= maxColumn; ++x )
            {
                if ( isStandingCell( x, y ) == false )
                    continue;
                const float3  candidate{ _pMap->getTileLeft( x ) + 0.5f * tileSize, _pMap->getTileBottom( y ) + _halfExtents._y, position._z };
                const float32 distance = float3::getDistance( candidate, position );
                // 같은 거리면 먼저 본 칸(아래 줄 · 왼쪽)이다 — 결과가 훑는 순서에만 매인다(결정적이다).
                if ( distance > radius || ( best.has_value() && distance >= bestDistance ) )
                    continue;
                best         = candidate;
                bestDistance = distance;
            }
        }
        return best;
    }

    bool ActionPlatformerRespawnWorld::isLocationReady( const float3& position ) const
    {
        (void)position; // 한 판 지형은 스트리밍하지 않는다
        return true;
    }
} // namespace sw
