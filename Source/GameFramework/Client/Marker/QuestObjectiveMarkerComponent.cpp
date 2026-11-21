#include "pch.h"

#include "GameFramework/Client/Marker/QuestObjectiveMarkerComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

namespace sw
{
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
            SW_LOG_ERROR_ID( "QuestObjectiveMarkerComponent.QuestObjectiveMarkerNeeds", "Quest objective marker needs a quest, a kind and a target (quest '%#', kind '%#', target '%#') - hidden", _quest.c_str(), _kind.c_str(),
                             _target.c_str() );
        ObjectiveMarkerComponent::onBeginPlay();
    }

    void QuestObjectiveMarkerComponent::configure( const hashed_string& quest, const hashed_string& kind, const hashed_string& target )
    {
        _quest  = CatalogKey<QuestDef>( quest );
        _kind   = kind;
        _target = target;
    }

    bool QuestObjectiveMarkerComponent::isConfigured() const
    {
        return _quest.empty() == false && _kind.empty() == false && _target.empty() == false;
    }

    bool QuestObjectiveMarkerComponent::isObjectiveOpen( const QuestLog& log ) const
    {
        return isConfigured() && log.isObjectiveOpen( _quest.key(), _kind, _target );
    }

    bool QuestObjectiveMarkerComponent::shouldShowMarker()
    {
        const QuestLog* pQuestLog = findPlayerQuestLog();
        if ( pQuestLog != nullptr )
            return isObjectiveOpen( *pQuestLog );
        if ( _bQuestLogMissingReported == SW_FALSE )
            SW_LOG_ERROR_ID( "QuestObjectiveMarkerComponent.QuestObjectiveMarkerNeeds2", "Quest objective marker '%#' needs exactly one open player quest log (QuestLogComponent) in the scene - hidden", getLabel().c_str() );
        _bQuestLogMissingReported = SW_TRUE;
        return false;
    }

    const QuestLog* QuestObjectiveMarkerComponent::findPlayerQuestLog() const
    {
        const GameObject*        pOwner    = getOwner();
        const QuestLogComponent* pQuestLog = PlayerComponentUtil::findOpen<QuestLogComponent>( pOwner != nullptr ? pOwner->getManager() : nullptr ); // 둘 이상이면 오류로 nullptr
        return pQuestLog != nullptr ? &pQuestLog->getQuestLog() : nullptr;
    }
} // namespace sw
