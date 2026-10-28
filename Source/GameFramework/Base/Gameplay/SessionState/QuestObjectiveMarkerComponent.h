/**
 * @file QuestObjectiveMarkerComponent.h
 * @brief 퀘스트 목표의 월드 오브젝트입니다. 어느 퀘스트의 어느 목표(종류와 대상)가 이 오브젝트인지 데이터로 적고, 그 목표가 열린 동안만 목표 마커를 보입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/UI/Marker/ObjectiveMarkerComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class QuestLog;

    /**
     * @class QuestObjectiveMarkerComponent
     * @brief 퀘스트 목표 하나(카탈로그 `<Objective kind target>`)의 월드 자리에 놓는 목표 마커입니다(언리얼 게임들의 퀘스트 목표 액터, 유니티 퀘스트 에셋의 목표 위치 자리).
     * @details 일지가 목표를 세는 열쇠는 `notify( kind, target )` 의 두 이름이므로 이 컴포넌트도 그 둘과 퀘스트 id 를 가진다. 셋 중 하나라도 비면 시작할 때 오류를 남기고 숨는다.
     *          보일지는 `QuestLog::isObjectiveOpen` 이 정하고, 묻는 때는 UI 갱신(`WidgetComponent::shouldShowMarker`, 병렬 틱 밖)이다 — 디렉터가 틱에서 일지를 쓰기 때문이다.
     *          일지는 씬의 판 상태(`SessionStateComponent`, 디렉터 오브젝트)에 있다. 판 상태가 없거나 둘 이상이면(어느 플레이어의 일지인지 모른다) 오류를 한 번 남기고 숨는다.
     *          퀘스트와 무관한 웨이포인트는 기반 `ObjectiveMarkerComponent` 를 쓴다(늘 보인다).
     */
    REFLECT( Category = "Gameplay", DisplayName = "Quest Objective Marker", Tooltip = "Objective marker shown while one quest objective (kind and target as in the quest catalog) is open" )
    class SW_GF_API QuestObjectiveMarkerComponent : public ObjectiveMarkerComponent
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
        /** @brief 씬의 판 상태 하나의 일지에서 이 목표가 열렸을 때만 true 입니다. */
        bool shouldShowMarker() override;

    private:
        /** @brief 이 오브젝트가 속한 씬의 판 상태 일지입니다. 판 상태가 하나가 아니면 nullptr 이고 @p outCount 에 찾은 수를 둡니다. */
        const QuestLog* findSessionQuestLog( uint32& outCount ) const;

    private:
        PROPERTY( Category = "Quest", DisplayName = "Quest", Tooltip = "Quest id in the quest catalog" )
        hashed_string _quest;
        PROPERTY( Category = "Quest", DisplayName = "Kind", Tooltip = "Objective kind as notified to the quest log (Kill, Collect, Talk, Reach)" )
        hashed_string _kind;
        PROPERTY( Category = "Quest", DisplayName = "Target", Tooltip = "Objective target as notified to the quest log (wolf, elder, gate)" )
        hashed_string _target;
        uint8         _bQuestLogMissingReported; ///< 일지를 찾지 못한 오류를 남겼다(프레임마다 남기지 않는다)
    };
} // namespace sw
