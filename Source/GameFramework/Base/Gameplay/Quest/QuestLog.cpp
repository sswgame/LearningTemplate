#include "pch.h"

#include "GameFramework/Base/Gameplay/Quest/QuestLog.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Quest/QuestCatalog.h"

namespace sw
{
    SW_LOG_CALLER( "QuestLog" );

    const utf8* toString( QuestStartResult result )
    {
        switch ( result )
        {
            case QuestStartResult::Ok:
                return "Ok";
            case QuestStartResult::UnknownQuest:
                return "UnknownQuest";
            case QuestStartResult::AlreadyActive:
                return "AlreadyActive";
            case QuestStartResult::AlreadyDone:
                return "AlreadyDone";
            case QuestStartResult::RequirementMissing:
                return "RequirementMissing";
            case QuestStartResult::LevelTooLow:
                return "LevelTooLow";
        }
        return "Unknown";
    }

    QuestLog::QuestLog()
        : _listProgress{}
        , _eventBuffer{}
        , _pCatalog{ nullptr }
    {
    }

    void QuestLog::initialize( const QuestCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _listProgress.clear();
        _eventBuffer.clear();
    }

    QuestProgress* QuestLog::findProgressMutable( const hashed_string& questId )
    {
        for ( QuestProgress& progress : _listProgress )
        {
            if ( progress._questId == questId )
                return &progress;
        }
        return nullptr;
    }

    const QuestProgress* QuestLog::findProgress( const hashed_string& questId ) const { return const_cast<QuestLog*>( this )->findProgressMutable( questId ); }

    QuestStatus QuestLog::getStatus( const hashed_string& questId ) const
    {
        const QuestProgress* pProgress = findProgress( questId );
        return pProgress != nullptr ? pProgress->_status : QuestStatus::NotStarted;
    }

    const QuestStage* QuestLog::findCurrentStage( const hashed_string& questId ) const
    {
        const QuestProgress* pProgress = findProgress( questId );
        const QuestDef*      pQuest    = _pCatalog != nullptr ? _pCatalog->findQuest( questId ) : nullptr;
        return pProgress != nullptr && pQuest != nullptr ? pQuest->findStage( pProgress->_stageId ) : nullptr;
    }

    QuestStartResult QuestLog::evaluateStart( const hashed_string& questId, int32 level ) const
    {
        const QuestDef* pQuest = _pCatalog != nullptr ? _pCatalog->findQuest( questId ) : nullptr;
        if ( pQuest == nullptr )
            return QuestStartResult::UnknownQuest;
        const QuestProgress* pProgress = findProgress( questId );
        if ( pProgress != nullptr && pProgress->_status == QuestStatus::Active )
            return QuestStartResult::AlreadyActive;
        if ( pProgress != nullptr && pProgress->_completedCount > 0 && pQuest->_bRepeatable == SW_FALSE )
            return QuestStartResult::AlreadyDone;
        for ( const hashed_string& requiredId : pQuest->_listRequiredQuest )
        {
            const QuestProgress* pRequired = findProgress( requiredId );
            if ( pRequired == nullptr || pRequired->_completedCount <= 0 )
                return QuestStartResult::RequirementMissing;
        }
        if ( level < pQuest->_requiredLevel )
            return QuestStartResult::LevelTooLow;
        return QuestStartResult::Ok;
    }

    QuestStartResult QuestLog::start( const hashed_string& questId, int32 level )
    {
        const QuestStartResult result = evaluateStart( questId, level );
        if ( result != QuestStartResult::Ok )
            return result;
        const QuestDef* pQuest    = _pCatalog->findQuest( questId );
        QuestProgress*  pProgress = findProgressMutable( questId );
        if ( pProgress == nullptr )
        {
            _listProgress.push_back( QuestProgress{} );
            pProgress           = &_listProgress.back();
            pProgress->_questId = questId;
        }
        pProgress->_status = QuestStatus::Active;
        pushEvent( QuestEvent::Kind::Started, *pProgress );
        enterStage( *pProgress, *pQuest, pQuest->_listStage.front()._id, 0 );
        return QuestStartResult::Ok;
    }

    void QuestLog::enterStage( QuestProgress& progress, const QuestDef& quest, const hashed_string& stageId, int32 depth )
    {
        const QuestStage* pStage = quest.findStage( stageId );
        if ( pStage == nullptr || depth >= kMaxChainedStages )
        {
            progress._status = QuestStatus::Failed; // 데이터가 끊겼다 — 멈추지 않게 실패로
            pushEvent( QuestEvent::Kind::Failed, progress );
            return;
        }
        progress._stageId         = stageId;
        progress._stageTime       = 0.0f;
        progress._bAwaitingChoice = SW_FALSE;
        progress._listCount.assign( pStage->_listObjective.size(), 0 );
        const bool bHasReward = pStage->_reward._items.isEmpty() == false || pStage->_reward._values.isEmpty() == false;
        pushEvent( QuestEvent::Kind::StageEntered, progress, -1, 0, bHasReward ? &pStage->_reward : nullptr );
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

    void QuestLog::tryAdvance( QuestProgress& progress )
    {
        const QuestDef*   pQuest = _pCatalog->findQuest( progress._questId );
        const QuestStage* pStage = pQuest != nullptr ? pQuest->findStage( progress._stageId ) : nullptr;
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
        // 단계가 넘어가며 진행 목록이 바뀌지 않도록 자리로 돈다(퀘스트는 여기서 생기지 않는다).
        for ( size_t questIndex = 0; questIndex < _listProgress.size(); ++questIndex )
        {
            QuestProgress& progress = _listProgress[questIndex];
            if ( progress._status != QuestStatus::Active || progress._bAwaitingChoice )
                continue;
            const QuestStage* pStage = findCurrentStage( progress._questId );
            if ( pStage == nullptr )
                continue;
            bool bChanged = false;
            for ( size_t index = 0; index < pStage->_listObjective.size(); ++index )
            {
                const QuestObjective& objective = pStage->_listObjective[index];
                if ( objective._kind != kind || ( objective._target.empty() == false && objective._target != target ) )
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

    int32 QuestLog::notifyCount( const hashed_string& kind, const hashed_string& target, int32 count ) { return applyNotify( kind, target, count, true ); }

    bool QuestLog::choose( const hashed_string& questId, const hashed_string& choice )
    {
        QuestProgress* pProgress = findProgressMutable( questId );
        if ( pProgress == nullptr || pProgress->_status != QuestStatus::Active || pProgress->_bAwaitingChoice == SW_FALSE )
            return false;
        const QuestDef*   pQuest = _pCatalog->findQuest( questId );
        const QuestStage* pStage = pQuest->findStage( pProgress->_stageId );
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

    bool QuestLog::fail( const hashed_string& questId )
    {
        QuestProgress* pProgress = findProgressMutable( questId );
        if ( pProgress == nullptr || pProgress->_status != QuestStatus::Active )
            return false;
        pProgress->_status = QuestStatus::Failed;
        pushEvent( QuestEvent::Kind::Failed, *pProgress );
        return true;
    }

    bool QuestLog::abandon( const hashed_string& questId )
    {
        QuestProgress* pProgress = findProgressMutable( questId );
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
            const QuestStage* pStage = findCurrentStage( progress._questId );
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
                outListQuest.push_back( progress._questId );
        }
    }

    void QuestLog::pushEvent( QuestEvent::Kind kind, const QuestProgress& progress, int32 objective, int32 value, const QuestReward* pReward )
    {
        QuestEvent event;
        event._kind      = kind;
        event._questId   = progress._questId;
        event._stageId   = progress._stageId;
        event._objective = objective;
        event._value     = value;
        event._pReward   = pReward;
        _eventBuffer.push( event );
    }

    void QuestLog::drainEvents( vector<QuestEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void QuestLog::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listProgress.size() );
        for ( const QuestProgress& progress : _listProgress )
        {
            StateArchiveUtil::writeName( outArchive, progress._questId );
            StateArchiveUtil::writeName( outArchive, progress._stageId );
            outArchive << static_cast<uint32>( progress._listCount.size() );
            for ( const int32 count : progress._listCount )
            {
                outArchive << count;
            }
            outArchive << progress._stageTime;
            outArchive << progress._completedCount;
            outArchive << static_cast<uint8>( progress._status );
            outArchive << progress._bAwaitingChoice;
        }
    }

    bool QuestLog::readState( Archive& archive )
    {
        uint32 progressCount = 0;
        // 퀘스트마다 이름 둘(8) + 목표 수(4) + 단계 시간(4) + 완료 횟수(4) + 상태(1) + 선택 대기(1) 이상
        if ( StateArchiveUtil::readCount( archive, 22, progressCount ) == false )
            return false;
        vector<QuestProgress> listProgress;
        listProgress.reserve( progressCount );
        for ( uint32 progressIndex = 0; progressIndex < progressCount; ++progressIndex )
        {
            QuestProgress progress;
            uint32        objectiveCount = 0;
            const bool    bHeadRead      = StateArchiveUtil::readName( archive, progress._questId ) && StateArchiveUtil::readName( archive, progress._stageId ) &&
                                   StateArchiveUtil::readCount( archive, 4, objectiveCount );
            if ( bHeadRead == false )
                return false;
            progress._listCount.resize( objectiveCount, 0 );
            for ( int32& count : progress._listCount )
            {
                archive >> count;
            }
            uint8 status = 0;
            archive >> progress._stageTime;
            archive >> progress._completedCount;
            archive >> status;
            archive >> progress._bAwaitingChoice;
            const bool bValid = archive.isOk() && status <= static_cast<uint8>( QuestStatus::Failed ) && progress._bAwaitingChoice <= SW_TRUE;
            if ( bValid == false )
                return false;
            progress._status  = static_cast<QuestStatus>( status );
            const bool bKnown = _pCatalog == nullptr || _pCatalog->findQuest( progress._questId ) != nullptr;
            if ( bKnown == false )
            {
                SW_LOG_WARNING( "QuestLog: saved quest '%#' is not in the catalog - dropped", progress._questId.c_str() );
                continue;
            }
            listProgress.push_back( std::move( progress ) );
        }
        _listProgress = std::move( listProgress );
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
