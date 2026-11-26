#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/Client/Marker/ObjectiveMarkerComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

namespace sw
{
    struct QuestDef;
    class QuestLog;

    /**
     * @class QuestObjectiveMarkerComponent
     * @brief 퀘스트 목표 하나(카탈로그 `<Objective kind target>`)의 월드 자리에 놓는 목표 마커입니다(언리얼 게임들의 퀘스트 목표 액터, 유니티 퀘스트 에셋의 목표 위치 자리).
     * @details 일지가 목표를 세는 열쇠는 `notify( kind, target )` 의 두 이름이므로 이 컴포넌트도 그 둘과 퀘스트 id 를 가진다. 셋 중 하나라도 비면 시작할 때 오류를 남기고 숨는다.
     *          보일지는 `QuestLog::isObjectiveOpen` 이 정하고, 묻는 때는 UI 갱신(`WidgetComponent::shouldShowMarker`, 병렬 틱 밖)이다 — 디렉터가 틱에서 일지를 쓰기 때문이다.
     *          일지는 씬의 플레이어 퀘스트 일지(`QuestLogComponent`)에 있다. 열린 것이 없거나 둘 이상이면(어느 플레이어의 일지인지 모른다) 오류를 한 번 남기고 숨는다.
     *          퀘스트와 무관한 웨이포인트는 기반 `ObjectiveMarkerComponent` 를 쓴다(늘 보인다).
     */
    REFLECT( Category = "Gameplay", DisplayName = "Quest Objective Marker", Tooltip = "Objective marker shown while one quest objective (kind and target as in the quest catalog) is open" )
    class QuestObjectiveMarkerComponent : public ObjectiveMarkerComponent
    {
    public:
        REFLECT_BODY();

        QuestObjectiveMarkerComponent();
        ~QuestObjectiveMarkerComponent() override;

        /** @brief 퀘스트 id · 종류 · 대상이 모두 있는지 보고(비었으면 오류) 마커를 시작합니다. */
        void onBeginPlay() override;

        /** @brief 퀘스트 · 종류 · 대상을 정합니다. 코드로 만든 오브젝트와 테스트가 씁니다. */
        void configure( const hashed_string& quest, const hashed_string& kind, const hashed_string& target );
        /** @brief 셋이 모두 있으면 true 입니다. */
        bool isConfigured() const;
        /** @brief 이 목표가 @p log 에서 지금 열려 있으면 true 입니다(셋이 다 있어야 한다). */
        bool isObjectiveOpen( const QuestLog& log ) const;

    protected:
        /** @brief 씬의 플레이어 진행 하나의 일지에서 이 목표가 열렸을 때만 true 입니다. */
        bool shouldShowMarker() override;

    private:
        /** @brief 이 오브젝트가 속한 씬의 플레이어 진행 일지입니다. 열린 플레이어 진행이 하나가 아니면 nullptr 입니다. */
        const QuestLog* findPlayerQuestLog() const;

    private:
        PROPERTY( Category = "Quest", DisplayName = "Quest", Tooltip = "Quest id in the quest catalog" )
        CatalogKey<QuestDef> _quest;
        PROPERTY( Category = "Quest", DisplayName = "Kind", Tooltip = "Objective kind as notified to the quest log (Kill, Collect, Talk, Reach)" )
        hashed_string _kind;
        PROPERTY( Category = "Quest", DisplayName = "Target", Tooltip = "Objective target as notified to the quest log (wolf, elder, gate)" )
        hashed_string      _target;
        SW_TRANSIENT uint8 _bQuestLogMissingReported; ///< 일지를 찾지 못한 오류를 남겼다(프레임마다 남기지 않는다)
    };

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

#include "QuestObjectiveMarkerComponent.cpp.gen.inl"
