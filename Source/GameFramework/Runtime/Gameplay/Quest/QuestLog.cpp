#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"

namespace sw
{
    QuestLog::QuestLog()
        : _listProgress{}
        , _eventBuffer{}
        , _pCatalog{ nullptr }
        , _actionWorld{}
    {
    }

    void QuestLog::initialize( const QuestCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _listProgress.clear();
        _eventBuffer.clear();
    }

    QuestProgress* QuestLog::findProgressMutable( const hashed_string& questID )
    {
        return VectorUtil::findBy<&QuestProgress::_questID>( _listProgress, questID );
    }

    const QuestProgress* QuestLog::findProgress( const hashed_string& questID ) const
    {
        return const_cast<QuestLog*>( this )->findProgressMutable( questID );
    }

    QuestStatus QuestLog::getStatus( const hashed_string& questID ) const
    {
        const QuestProgress* pProgress = findProgress( questID );
        return pProgress != nullptr ? pProgress->_status : QuestStatus::NotStarted;
    }

    const QuestStage* QuestLog::findCurrentStage( const hashed_string& questID ) const
    {
        const QuestProgress* pProgress = findProgress( questID );
        const QuestDef*      pQuest    = _pCatalog != nullptr ? _pCatalog->find( questID ) : nullptr;
        return pProgress != nullptr && pQuest != nullptr ? pQuest->findStage( pProgress->_stageID ) : nullptr;
    }

    bool QuestLog::isObjectiveOpen( const hashed_string& questID, const hashed_string& kind, const hashed_string& target ) const
    {
        const QuestProgress* pProgress = findProgress( questID );
        const QuestStage*    pStage    = findCurrentStage( questID );
        if ( pProgress == nullptr || pStage == nullptr )
            return false;
        const bool bCountingObjectives = pProgress->_status == QuestStatus::Active && pProgress->_bAwaitingChoice == SW_FALSE;
        if ( bCountingObjectives == false )
            return false;
        for ( size_t index = 0; index < pStage->_listObjective.size(); ++index )
        {
            const QuestObjective& objective = pStage->_listObjective[index];
            if ( objective.matches( kind, target ) && pProgress->_listCount[index] < objective._count )
                return true;
        }
        return false;
    }

    QuestStartResult QuestLog::evaluateStart( const hashed_string& questID, int32 level ) const
    {
        const QuestDef* pQuest = _pCatalog != nullptr ? _pCatalog->find( questID ) : nullptr;
        if ( pQuest == nullptr )
            return QuestStartResult::UnknownQuest;
        const QuestProgress* pProgress = findProgress( questID );
        if ( pProgress != nullptr && pProgress->_status == QuestStatus::Active )
            return QuestStartResult::AlreadyActive;
        const bool bAlreadyCompleted = pProgress != nullptr && pProgress->_completedCount > 0 && pQuest->_bRepeatable == SW_FALSE;
        if ( bAlreadyCompleted )
            return QuestStartResult::AlreadyDone;
        for ( const hashed_string& requiredID : pQuest->_listRequiredQuest )
        {
            const QuestProgress* pRequired = findProgress( requiredID );
            if ( pRequired == nullptr || pRequired->_completedCount <= 0 )
                return QuestStartResult::RequirementMissing;
        }
        if ( level < pQuest->_requiredLevel )
            return QuestStartResult::LevelTooLow;
        return QuestStartResult::Ok;
    }

    void QuestLog::writeDebugLines( DebugHUDSectionWriter& writer, int32 playerLevel, uint32 maxLines ) const
    {
        uint32     lineCount = 0;
        uint32     skipped   = 0;
        const auto add       = [&]( const string& label, const string& value )
        {
            if ( lineCount >= maxLines )
            {
                ++skipped;
                return;
            }
            writer.addLine( label, value );
            ++lineCount;
        };
        for ( const QuestProgress& progress : _listProgress )
        {
            const QuestDef*   pQuest = _pCatalog != nullptr ? _pCatalog->find( progress._questID ) : nullptr;
            const QuestStage* pStage = pQuest != nullptr ? pQuest->findStage( progress._stageID ) : nullptr;
            if ( progress._status != QuestStatus::Active )
            {
                add( string( progress._questID.c_str() ), string( getEnumName( progress._status ) ) + " x" + to_string( progress._completedCount ) );
                continue;
            }
            string state = string( "Active stage " ) + progress._stageID.c_str() + " t=" + to_string( static_cast<int32>( progress._stageTime ) ) + "s";
            if ( pStage != nullptr && pStage->_timeLimit > 0.0f )
                state += "/" + to_string( static_cast<int32>( pStage->_timeLimit ) ) + "s";
            if ( progress._bAwaitingChoice == SW_TRUE )
                state += " (awaiting choice)";
            add( string( progress._questID.c_str() ), state );
            if ( pStage == nullptr )
                continue;
            for ( size_t index = 0; index < pStage->_listObjective.size() && index < progress._listCount.size(); ++index )
            {
                const QuestObjective& objective = pStage->_listObjective[index];
                const int32           count     = progress._listCount[index];
                string                value     = to_string( count ) + "/" + to_string( objective._count );
                if ( count >= objective._count )
                    value += " done";
                else if ( objective._bOptional )
                    value += " optional";
                add( string( "  " ) + objective._kind.c_str() + " " + objective._target.c_str(), value );
            }
        }
        if ( _pCatalog == nullptr )
            return;
        // 받지 않은 퀘스트는 "왜 못 받나" 를 적는다 — 빠진 선행 퀘스트의 이름까지(판정은 evaluateStart 와 같은 순서).
        for ( const QuestDef& quest : _pCatalog->getAll() )
        {
            if ( findProgress( quest._id ) != nullptr )
                continue;
            const QuestStartResult result = evaluateStart( quest._id, playerLevel );
            string                 reason = getEnumName( result );
            if ( result == QuestStartResult::RequirementMissing )
            {
                for ( const hashed_string& requiredID : quest._listRequiredQuest )
                {
                    const QuestProgress* pRequired = findProgress( requiredID );
                    if ( pRequired == nullptr || pRequired->_completedCount <= 0 )
                    {
                        reason += string( " (" ) + requiredID.c_str() + ")";
                        break;
                    }
                }
            }
            else if ( result == QuestStartResult::LevelTooLow )
                reason += " (needs " + to_string( quest._requiredLevel ) + ")";
            string available = "available";
            if ( quest._requiredLevel > 0 )
                available += " (level " + to_string( quest._requiredLevel ) + "+)";
            add( string( quest._id.c_str() ), result == QuestStartResult::Ok ? available : "locked: " + reason );
        }
        if ( skipped > 0 )
            writer.addLine( "...", to_string( skipped ) + " more" );
    }

    QuestStartResult QuestLog::start( const hashed_string& questID, int32 level )
    {
        const QuestStartResult result = evaluateStart( questID, level );
        if ( result != QuestStartResult::Ok )
            return result;
        const QuestDef* pQuest    = _pCatalog->find( questID );
        QuestProgress*  pProgress = findProgressMutable( questID );
        if ( pProgress == nullptr )
        {
            _listProgress.push_back( QuestProgress{} );
            pProgress           = &_listProgress.back();
            pProgress->_questID = questID;
        }
        pProgress->_status = QuestStatus::Active;
        pushEvent( QuestEvent::Kind::Started, *pProgress );
        enterStage( *pProgress, *pQuest, pQuest->_listStage.front()._id, 0 );
        return QuestStartResult::Ok;
    }

    void QuestLog::enterStage( QuestProgress& progress, const QuestDef& quest, const hashed_string& stageID, int32 depth )
    {
        const QuestStage* pStage = quest.findStage( stageID );
        if ( pStage == nullptr || depth >= kMaxChainedStages )
        {
            progress._status = QuestStatus::Failed; // 데이터가 끊겼으므로 멈추지 않게 실패로 처리한다
            pushEvent( QuestEvent::Kind::Failed, progress );
            return;
        }
        progress._stageID         = stageID;
        progress._stageTime       = 0.0f;
        progress._bAwaitingChoice = SW_FALSE;
        progress._listCount.assign( pStage->_listObjective.size(), 0 );
        const bool bHasReward = pStage->_reward._items.isEmpty() == false || pStage->_reward._values.isEmpty() == false || pStage->_reward._listAction.empty() == false;
        pushEvent( QuestEvent::Kind::StageEntered, progress, nullopt, 0, bHasReward ? &pStage->_reward : nullptr );
        for ( const GameActionCall& call : pStage->_reward._listAction )
        {
            ActionContext context;
            context._world = _actionWorld;
            if ( call.invoke( context ) == ActionResult::Failed )
                SW_LOG_WARNING_ID( "QuestLog.RewardActionFailed", "Quest '%#' stage '%#': reward action '%#' failed", quest._id.c_str(), stageID.c_str(), call.getName() );
        }
        if ( pStage->_bFail )
        {
            progress._status = QuestStatus::Failed;
            pushEvent( QuestEvent::Kind::Failed, progress );
            return;
        }
        if ( pStage->_bComplete )
        {
            progress._status = QuestStatus::Completed;
            ++progress._completedCount;
            pushEvent( QuestEvent::Kind::Completed, progress );
            return;
        }
        bool bHasRequired = false;
        for ( const QuestObjective& objective : pStage->_listObjective )
        {
            bHasRequired = bHasRequired || objective._bOptional == SW_FALSE;
        }
        if ( bHasRequired )
            return;
        if ( pStage->_listBranch.empty() == false )
        {
            progress._bAwaitingChoice = SW_TRUE;
            pushEvent( QuestEvent::Kind::AwaitingChoice, progress );
            return;
        }
        if ( pStage->_nextStage.empty() == false )
            enterStage( progress, quest, pStage->_nextStage, depth + 1 );
    }

    void QuestLog::reconcileWithDefinition( QuestProgress& inoutProgress, const QuestDef& quest )
    {
        const QuestStage* pStage = quest.findStage( inoutProgress._stageID );
        if ( pStage == nullptr )
        {
            inoutProgress._listCount.clear();
            inoutProgress._bAwaitingChoice = SW_FALSE;
            if ( inoutProgress._status != QuestStatus::Active )
                return;
            SW_LOG_WARNING_ID( "QuestLog.QuestLogSavedStageNot", "QuestLog: quest '%#' was saved in stage '%#' which no longer exists - failed", quest._id.c_str(), inoutProgress._stageID.c_str() );
            inoutProgress._status = QuestStatus::Failed;
            return;
        }
        inoutProgress._listCount.resize( pStage->_listObjective.size(), 0 );
        for ( size_t index = 0; index < inoutProgress._listCount.size(); ++index )
        {
            inoutProgress._listCount[index] = MathUtil::clamp( inoutProgress._listCount[index], 0, pStage->_listObjective[index]._count );
        }
        if ( pStage->_listBranch.empty() )
            inoutProgress._bAwaitingChoice = SW_FALSE;
    }

    void QuestLog::tryAdvance( QuestProgress& progress )
    {
        const QuestDef*   pQuest = _pCatalog->find( progress._questID );
        const QuestStage* pStage = pQuest != nullptr ? pQuest->findStage( progress._stageID ) : nullptr;
        if ( pStage == nullptr || progress._bAwaitingChoice )
            return;
        for ( size_t index = 0; index < pStage->_listObjective.size(); ++index )
        {
            const QuestObjective& objective = pStage->_listObjective[index];
            if ( objective._bOptional == SW_FALSE && progress._listCount[index] < objective._count )
                return;
        }
        if ( pStage->_listBranch.empty() == false )
        {
            progress._bAwaitingChoice = SW_TRUE;
            pushEvent( QuestEvent::Kind::AwaitingChoice, progress );
            return;
        }
        if ( pStage->_nextStage.empty() == false )
            enterStage( progress, *pQuest, pStage->_nextStage, 0 );
    }

    int32 QuestLog::applyNotify( const hashed_string& kind, const hashed_string& target, int32 value, bool bAbsolute )
    {
        int32 advancedCount = 0;
        // 단계가 넘어가면서 진행 목록이 바뀌어도 안전하도록 인덱스로 순회한다. 퀘스트는 여기서 새로 생기지 않는다.
        for ( size_t questIndex = 0; questIndex < _listProgress.size(); ++questIndex )
        {
            QuestProgress& progress = _listProgress[questIndex];
            if ( progress._status != QuestStatus::Active || progress._bAwaitingChoice )
                continue;
            const QuestStage* pStage = findCurrentStage( progress._questID );
            if ( pStage == nullptr )
                continue;
            bool bChanged = false;
            for ( size_t index = 0; index < pStage->_listObjective.size(); ++index )
            {
                const QuestObjective& objective = pStage->_listObjective[index];
                if ( objective.matches( kind, target ) == false )
                    continue;
                const int32 before = progress._listCount[index];
                const int32 after  = MathUtil::clamp( bAbsolute ? value : before + value, 0, objective._count );
                if ( after == before )
                    continue;
                progress._listCount[index] = after;
                bChanged                   = true;
                ++advancedCount;
                pushEvent( QuestEvent::Kind::ObjectiveProgress, progress, static_cast<int32>( index ), after );
                if ( after >= objective._count )
                    pushEvent( QuestEvent::Kind::ObjectiveDone, progress, static_cast<int32>( index ), after );
            }
            if ( bChanged )
                tryAdvance( progress );
        }
        return advancedCount;
    }

    int32 QuestLog::notify( const hashed_string& kind, const hashed_string& target, int32 amount )
    {
        return amount > 0 ? applyNotify( kind, target, amount, false ) : 0;
    }

    int32 QuestLog::notifyCount( const hashed_string& kind, const hashed_string& target, int32 count )
    {
        return applyNotify( kind, target, count, true );
    }

    bool QuestLog::choose( const hashed_string& questID, const hashed_string& choice )
    {
        QuestProgress* pProgress        = findProgressMutable( questID );
        const bool     bNoPendingChoice = pProgress == nullptr || pProgress->_status != QuestStatus::Active || pProgress->_bAwaitingChoice == SW_FALSE;
        if ( bNoPendingChoice )
            return false;
        const QuestDef*   pQuest = _pCatalog->find( questID );
        const QuestStage* pStage = pQuest->findStage( pProgress->_stageID );
        for ( const QuestBranch& branch : pStage->_listBranch )
        {
            if ( branch._choice == choice )
            {
                enterStage( *pProgress, *pQuest, branch._nextStage, 0 );
                return true;
            }
        }
        return false;
    }

    bool QuestLog::fail( const hashed_string& questID )
    {
        QuestProgress* pProgress = findProgressMutable( questID );
        if ( pProgress == nullptr || pProgress->_status != QuestStatus::Active )
            return false;
        pProgress->_status = QuestStatus::Failed;
        pushEvent( QuestEvent::Kind::Failed, *pProgress );
        return true;
    }

    bool QuestLog::abandon( const hashed_string& questID )
    {
        QuestProgress* pProgress = findProgressMutable( questID );
        if ( pProgress == nullptr || pProgress->_status != QuestStatus::Active )
            return false;
        pProgress->_status = QuestStatus::NotStarted; // 다시 받을 수 있다
        pushEvent( QuestEvent::Kind::Abandoned, *pProgress );
        return true;
    }

    void QuestLog::update( float32 deltaTime )
    {
        for ( QuestProgress& progress : _listProgress )
        {
            if ( progress._status != QuestStatus::Active )
                continue;
            const QuestStage* pStage = findCurrentStage( progress._questID );
            if ( pStage == nullptr || pStage->_timeLimit <= 0.0f )
                continue;
            progress._stageTime += deltaTime;
            if ( progress._stageTime >= pStage->_timeLimit )
            {
                progress._status = QuestStatus::Failed;
                pushEvent( QuestEvent::Kind::Failed, progress );
            }
        }
    }

    void QuestLog::collectActive( vector<hashed_string>& outListQuest ) const
    {
        outListQuest.clear();
        for ( const QuestProgress& progress : _listProgress )
        {
            if ( progress._status == QuestStatus::Active )
                outListQuest.push_back( progress._questID );
        }
    }

    void QuestLog::pushEvent( QuestEvent::Kind kind, const QuestProgress& progress, optional<int32> objective, int32 value, const QuestReward* pReward )
    {
        QuestEvent event;
        event._kind      = kind;
        event._questID   = progress._questID;
        event._stageID   = progress._stageID;
        event._objective = objective;
        event._value     = value;
        event._pReward   = pReward;
        _eventBuffer.push( event );
    }

    void QuestLog::pollEvents( vector<QuestEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void QuestLog::serializeState( StateIO& io )
    {
        uint32 progressCount = static_cast<uint32>( _listProgress.size() );
        // 퀘스트마다 이름 두 개(8) + 목표 수(4) + 단계 시간(4) + 완료 횟수(4) + 상태(1) + 선택 대기(1) 이상
        io.count( 22, progressCount );
        if ( io.isOk() == false )
            return;
        vector<QuestProgress> listRestored;
        for ( uint32 progressIndex = 0; progressIndex < progressCount; ++progressIndex )
        {
            QuestProgress  restored;
            QuestProgress& progress = io.isWriting() ? _listProgress[progressIndex] : restored;
            io.value( progress._questID );
            io.value( progress._stageID );
            io.list( progress._listCount );
            io.value( progress._stageTime );
            io.value( progress._completedCount );
            io.enumValue( progress._status, QuestStatus::Failed );
            io.flag( progress._bAwaitingChoice );
            if ( io.check( progress._stageTime >= 0.0f && progress._completedCount >= 0 ) == false )
                return;
            if ( io.isWriting() )
                continue;
            const QuestDef* pQuest = _pCatalog != nullptr ? _pCatalog->find( progress._questID ) : nullptr;
            if ( _pCatalog != nullptr && pQuest == nullptr )
            {
                SW_LOG_WARNING_ID( "QuestLog.QuestLogSavedQuestNot", "QuestLog: saved quest '%#' is not in the catalog - dropped", progress._questID.c_str() );
                continue;
            }
            if ( pQuest != nullptr )
                reconcileWithDefinition( progress, *pQuest );
            listRestored.push_back( std::move( progress ) );
        }
        if ( io.isReading() )
        {
            _listProgress = std::move( listRestored );
            _eventBuffer.clear();
        }
    }

} // namespace sw
