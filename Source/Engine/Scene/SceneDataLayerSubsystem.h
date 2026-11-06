/**
 * @file SceneDataLayerSubsystem.h
 * @brief 씬의 데이터 레이어를 켜고 끄는 씬 서비스입니다(언리얼 Data Layers 의 런타임 상태 · `UDataLayerManager` 자리). 계절 · 이벤트 · 야간 변형을 씬 복사 없이 만듭니다.
 *
 * [모델]
 * 켜진 레이어는 이름 집합이다. 엔티티는 붙은 레이어가 **모두** 켜졌을 때 오른다. 언리얼은 기본이 "하나라도"(Or)이고 And 를 고를 수 있다(기억에 의존). 여기는 And 만 둔다 —
 * 인스턴스 멤버가 앵커의 레이어를 더해 가지므로("겨울 앵커 안의 야간 등") 합집합이 곧 조건이 된다. "겨울이나 가을" 은 그 뜻의 레이어를 따로 선언한다.
 * 이름 집합이라 문맥 태그(`ResourceContext`, 리소스 구조 단계 5)가 그대로 이 값을 켤 수 있다.
 *
 * [올리는 방식 — 스트리밍 셀과 같다]
 * 레이어 집합이 같은 엔티티를 한 묶음(`Group`)으로 들고 있다가 묶음째 짓고 내린다(`Scene::instantiateLayerNodes` · `Scene::unloadCell`). 켜고 끄기는 다음 `DestroyPost` 에 처리한다 —
 * 틱 도중 부를 수 있고, 만든 오브젝트는 다음 프레임 병합에서 틱에 든다. 스트리밍하는 씬의 셀은 쿠커가 좌표 + 레이어 집합으로 나누고(`SceneDocument::StreamingCell`) `SceneStreamingSubsystem` 이 이 집합을 물어 올린다.
 *
 * [참조]
 * 묶음은 레이어 수가 적은 것부터 짓는다. 쿠커가 "대상의 레이어 ⊆ 가리키는 쪽의 레이어" 를 검사하므로(`SceneCooker`) 가리키는 쪽이 오를 때 대상은 늘 올라 있다.
 *
 * [저장]
 * 짓지 않은 묶음의 엔티티는 문서 그대로 저장에 다시 쓴다(`Scene::serializeToDocument`). 그래서 레이어를 끈 채 열고 저장해도 그 엔티티를 잃지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneSettings.h"

namespace sw
{
    class GameObjectManager;
    class Scene;

    /**
     * @class SceneDataLayerSubsystem
     * @brief 파일 머리말 참고.
     */
    class SW_API SceneDataLayerSubsystem final : public ISceneService
    {
    public:
        /** @brief 서브시스템 이름입니다(씬 범위의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "SceneDataLayers";
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        /** @brief 레이어 집합이 같은 엔티티 묶음입니다. */
        struct Group
        {
            vector<string>                         _listLayer;
            vector<SceneDocument::SceneObjectNode> _listNode;
            vector<uint64>                         _listObjectID;        ///< 지은 오브젝트(엔티티마다 하나, 부품 제외)
            bool                                   _bBuilt{ SW_FALSE };  ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool                                   _bFailed{ SW_FALSE }; ///< 짓다 실패했다 — 다시 시도하지 않는다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        /** @brief 플레이 종료 복원이 들고 있는 값입니다. 오브젝트는 원래 id 로 돌아오므로 묶음의 id 목록이 그대로 맞습니다. */
        struct State
        {
            vector<Group>  _listGroup;
            vector<string> _listActiveLayer;
        };

        /** @brief @p manager 의 레이어 서비스를 만듭니다(서비스 레지스트리가 매니저마다 부른다). */
        explicit SceneDataLayerSubsystem( GameObjectManager& manager );

        SceneDataLayerSubsystem( const SceneDataLayerSubsystem& )            = delete;
        SceneDataLayerSubsystem& operator=( const SceneDataLayerSubsystem& ) = delete;

        /**
         * @brief 씬 @p scene 의 레이어와 레이어가 붙은 엔티티를 받고, 켜져 있는 묶음을 지금 짓습니다(`Scene::instantiate`).
         * @param bActivateAll 선언한 레이어를 모두 켭니다(쿠커 — 모든 엔티티를 지어 봐야 한다).
         */
        void beginScene( Scene& scene, const SceneSettings& settings, const vector<SceneDocument::SceneObjectNode>& listLayeredNode, bool bActivateAll );

        /** @brief 켜고 끄기를 처리합니다(바뀐 것이 있을 때만). */
        void update( float32 deltaTime ) override;
        /** @brief 씬을 비울 때 묶음을 버립니다(오브젝트는 매니저가 이미 비웠다). */
        void onSceneCleared() override;

        /**
         * @brief 레이어 @p name 을 켜거나 끕니다. 오브젝트는 다음 `DestroyPost` 에 오르내립니다.
         * @return 씬이 선언하지 않은 이름이면 오류를 남기고 false 입니다(잘못 쓴 이름을 조용히 넘기지 않는다).
         */
        [[nodiscard]] bool setLayerActive( string_view name, bool bActive );
        /** @brief 레이어 @p name 이 켜져 있으면 true 입니다. */
        [[nodiscard]] bool isLayerActive( string_view name ) const;
        /** @brief @p listLayer 가 모두 켜져 있으면 true 입니다. 빈 목록은 true 입니다. */
        [[nodiscard]] bool areLayersActive( const vector<string>& listLayer ) const;
        /** @brief 켜진 레이어 이름을 정렬해 채웁니다(비우고 채웁니다). */
        void collectActiveLayers( vector<string>& outListLayer ) const;
        /** @brief 묶음 목록입니다(레이어 수 · 이름 순서). */
        const vector<Group>& getGroups() const { return _listGroup; }

        /** @brief 지금 상태를 값으로 찍습니다. */
        [[nodiscard]] State captureState() const;
        /** @brief @p state 로 되돌립니다. 오브젝트는 짓지 않습니다 — 부르는 쪽이 같은 id 로 이미 복원했습니다. */
        void restoreState( Scene& scene, const SceneSettings& settings, State state );

    private:
        /** @brief 바라는 상태와 다른 묶음을 내리고 짓습니다. 큰 집합을 먼저 내리고 작은 집합을 먼저 짓습니다. */
        void applyGroups();

    private:
        Scene*                _pScene; ///< 받은 씬(빌림)
        SceneSettings         _settings;
        vector<Group>         _listGroup;
        unordered_set<string> _uniqueActiveLayer;
        uint8                 _bDirty : 1; ///< 켜고 끄기가 있었다 — 다음 갱신이 처리한다
    };
} // namespace sw
