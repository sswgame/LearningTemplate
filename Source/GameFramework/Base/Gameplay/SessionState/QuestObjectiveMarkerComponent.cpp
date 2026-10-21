#include "pch.h"

#include "GameFramework/Base/Gameplay/SessionState/QuestObjectiveMarkerComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Gameplay/Quest/QuestLog.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"

namespace sw
{
    SW_LOG_CALLER( "QuestObjectiveMarkerComponent" );

    QuestObjectiveMarkerComponent::QuestObjectiveMarkerComponent()
        : ObjectiveMarkerComponent{}
        , _quest{}
        , _kind{}
        , _target{}
        , _bQuestLogMissingReported{ SW_FALSE }
    {
    }

    QuestObjectiveMarkerComponent::~QuestObjectiveMarkerComponent() = default;

    void QuestObjectiveMarkerComponent::onBeginPlay()
    {
        if ( isConfigured() == false )
            SW_LOG_ERROR( "Quest objective marker needs a quest, a kind and a target (quest '%#', kind '%#', target '%#') - hidden", _quest.c_str(), _kind.c_str(),
                          _target.c_str() );
        ObjectiveMarkerComponent::onBeginPlay();
    }

    void QuestObjectiveMarkerComponent::configure( const hashed_string& quest, const hashed_string& kind, const hashed_string& target )
    {
        _quest  = quest;
        _kind   = kind;
        _target = target;
    }

    bool QuestObjectiveMarkerComponent::isConfigured() const
    {
        return _quest.empty() == false && _kind.empty() == false && _target.empty() == false;
    }

    bool QuestObjectiveMarkerComponent::isObjectiveOpen( const QuestLog& log ) const
    {
        return isConfigured() && log.isObjectiveOpen( _quest, _kind, _target );
    }

    bool QuestObjectiveMarkerComponent::shouldShowMarker()
    {
        uint32          sessionCount = 0;
        const QuestLog* pQuestLog    = findSessionQuestLog( sessionCount );
        if ( pQuestLog != nullptr )
            return isObjectiveOpen( *pQuestLog );
        if ( _bQuestLogMissingReported == SW_FALSE )
            SW_LOG_ERROR( "Quest objective marker '%#' needs exactly one game state with a quest log in the scene (found %#) - hidden", getLabel().c_str(), sessionCount );
        _bQuestLogMissingReported = SW_TRUE;
        return false;
    }

    const QuestLog* QuestObjectiveMarkerComponent::findSessionQuestLog( uint32& outCount ) const
    {
        outCount                          = 0;
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return nullptr;
        const QuestLog* pQuestLog = nullptr;
        pManager->forEachComponentOfType<SessionStateComponent>( [&outCount, &pQuestLog]( SessionStateComponent* pState )
        {
            ++outCount;
            pQuestLog = &pState->getQuestLog();
        } );
        return outCount == 1 ? pQuestLog : nullptr;
    }
} // namespace sw
