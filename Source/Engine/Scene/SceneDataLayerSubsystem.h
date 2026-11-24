/**
 * @file SceneDataLayerSubsystem.h
 * @brief 씬의 데이터 레이어를 켜고 끄는 씬 서비스입니다(언리얼 Data Layers 의 런타임 상태 · `UDataLayerManager` 자리). 계절 · 이벤트 · 야간 변형을 씬 복사 없이 만듭니다.
 *
 * [모델]
 * 켜진 레이어는 이름 집합이다. 엔티티는 붙은 레이어가 **모두** 켜졌을 때 오른다. 언리얼은 기본이 "하나라도"(Or)이고 And 를 고를 수 있다(기억에 의존). 여기는 And 만 둔다 —
 * 인스턴스 멤버가 앵커의 레이어를 더해 가지므로("겨울 앵커 안의 야간 등") 합집합이 곧 조건이 된다. "겨울이나 가을" 은 그 뜻의 레이어를 따로 선언한다.
 * 이름 집합이라 리소스 조건의 문맥 태그(`ResourceTags` 의 `layer` 축)로 그대로 넣는다 — 켜고 끌 때마다 넣고, 리소스는 `<_when _if="layer.Winter">` 로 값을 바꾼다.
 * 태그는 씬마다의 몫이다(`ResourceTags::addAxisValues` 의 구독을 이 서비스가 든다) — 씬이 여럿이면(로드 중인 다음 씬 · 에디터 세계와 플레이 세계) 합집합이고, 내린 씬은 제 몫만 거둔다.
 *
 * [올리는 방식 — 스트리밍 셀과 같다]
 * 레이어 집합이 같은 엔티티를 한 묶음(`Group`)으로 들고 있다가 묶음째 짓고 내린다(`Scene::instantiateLayerNodes` · `Scene::unloadCell`). 켜고 끄기는 다음 `DestroyPost` 에 처리한다 —
 * 틱 도중 부를 수 있고, 만든 오브젝트는 다음 프레임 병합에서 틱에 든다. 스트리밍하는 씬의 셀은 쿠커가 좌표 + 레이어 집합으로 나누고(`SceneDocument::StreamingCell`) `SceneStreamingSubsystem` 이 이 집합을 물어 올린다.
 *
 * [참조]
 * 묶음은 레이어 수가 적은 것부터 짓는다. 쿠커가 "대상의 레이어 ⊆ 가리키는 쪽의 레이어" 를 검사하므로(`SceneCooker`) 가리키는 쪽이 오를 때 대상은 늘 올라 있다.
 *
 * [상태 보존 — 스트리밍 셀과 같다(설계 "스트리밍 셀 경계 정합 · 저장")]
 * 레이어를 꺼 묶음을 내릴 때 오브젝트의 지금 상태를 떠 두고(`Scene::captureCellNodes` — 목적 `Snapshot`, 런타임 칸까지) 다시 켜면 그 상태로 짓는다(`_listHeldNode`).
 * 세이브 복원이 매니저를 비우면(`onSceneCleared`) 묶음 · 켜진 레이어를 지키고 지은 표시만 지운다 — 다음 갱신이 다시 짓는다. 세이브 봉투는 묶음 오브젝트를 씬 섹션에서 빼고(`isGroupObject`)
 * 켜진 레이어와 묶음마다의 상태를 씬 몫에 싣는다(`writeSaveState` · `readSaveState` · `applySaveState`).
 *
 * [저장]
 * 짓지 않은 묶음의 엔티티는 문서 그대로 저장에 다시 쓴다(`Scene::serializeToDocument`, `_listNode` — 떠 둔 상태는 스냅숏이라 저작 파일에 쓰지 않는다). 그래서 레이어를 끈 채 열고 저장해도 그 엔티티를 잃지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Scene/SceneDataLayerTypes.h"
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

        /** @brief 레이어 집합이 같은 엔티티 묶음입니다(값 형식은 `SceneDataLayerTypes.h` — 씬이 값으로 든다). */
        using Group = SceneDataLayerGroup;
        /** @brief 플레이 종료 복원이 들고 있는 값입니다. */
        using State = SceneDataLayerState;

        /** @brief @p manager 의 레이어 서비스를 만듭니다(서비스 레지스트리가 매니저마다 부른다). */
        explicit SceneDataLayerSubsystem( GameObjectManager& manager );

        SceneDataLayerSubsystem( const SceneDataLayerSubsystem& )            = delete;
        SceneDataLayerSubsystem& operator=( const SceneDataLayerSubsystem& ) = delete;

        /**
         * @brief 씬 @p scene 의 레이어와 레이어가 붙은 엔티티를 받고, 켜져 있는 묶음을 지금 짓습니다(`Scene::instantiate`).
         * @param bActivateAll 선언한 레이어를 모두 켭니다(쿠커 — 모든 엔티티를 지어 봐야 한다).
         */
        void beginScene( Scene& scene, const SceneSettings& settings, const vector<SceneDocument::SceneObjectNode>& listLayeredNode, bool bActivateAll );

        /** @brief 씬을 내립니다(`Scene::shutdown`) — 묶음을 버리고 켜진 레이어 태그를 비웁니다. 받은 씬이 없으면 아무것도 하지 않습니다(다른 씬의 태그를 지우지 않게). */
        void endScene();

        /** @brief 켜고 끄기를 처리합니다(바뀐 것이 있을 때만). */
        void update( float32 deltaTime ) override;
        /** @brief 매니저가 오브젝트를 비웠습니다(세이브 복원). 묶음 · 켜진 레이어는 지키고 지은 표시만 지웁니다 — 다음 갱신이 켜진 묶음을 다시 짓는다(복원은 그 전에 `applySaveState`). */
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
        /** @brief 켜진 레이어 집합이 바뀔 때마다 오르는 번호입니다. 레이어에 기댄 판정을 들고 있는 쪽이 다시 볼 때를 안다. */
        uint64 getActiveSerial() const { return _activeSerial; }
        /** @brief 켜진 레이어 이름을 정렬해 채웁니다(비우고 채웁니다). */
        void collectActiveLayers( vector<string>& outListLayer ) const;
        /** @brief 묶음 목록입니다(레이어 수 · 이름 순서). */
        const vector<Group>& getGroups() const { return _listGroup; }

        /** @brief 세이브에 싣는 묶음 하나의 상태입니다. */
        struct SavedGroup
        {
            vector<string>                         _listLayer;
            vector<SceneDocument::SceneObjectNode> _listHeldNode;
            bool                                   _bStateHeld{ false };
        };
        /** @brief 세이브의 데이터 레이어 몫입니다. */
        struct SaveState
        {
            vector<string>     _listActiveLayer;
            vector<SavedGroup> _listGroup;
        };
        /** @brief 켜진 레이어와 묶음마다의 상태(지어 있으면 지금 떠서, 내려가 있으면 떠 둔 것)를 @p outBytes 에 씁니다(비우고 채운다). 씬을 받지 않았으면 빈 목록입니다. */
        void writeSaveState( vector<uint8>& outBytes ) const;
        /** @brief `writeSaveState` 의 짝입니다(검증 — 씬을 건드리지 않는다). 잘렸으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool readSaveState( const uint8* pData, size_t size, SaveState& outState );
        /** @brief 세이브의 켜진 레이어 · 묶음 상태를 얹습니다(매니저를 비운 뒤). 다음 갱신이 켜진 묶음을 그 상태로 짓습니다. 문서에 없는 묶음 · 선언하지 않은 레이어는 오류를 남기고 건너뜁니다. */
        void applySaveState( SaveState state );
        /** @brief 오브젝트(또는 그 조상)가 지은 묶음에 들어 있으면 true 입니다 — 세이브의 씬 섹션이 빼는 오브젝트입니다(묶음 몫은 `writeSaveState` 가 싣는다). */
        bool isGroupObject( uint64 objectID ) const;
        /** @brief 오브젝트 자신이 이 서비스의 목록에 들어 있으면 true 입니다(조상은 보지 않는다) — 조상이 다른 묶음에 든 오브젝트를 두 번 뜨지 않게 가린다(`Scene::collectCellGroup`). */
        bool isListedObject( uint64 objectID ) const;
        /** @brief 오브젝트 @p objectID 를 묶음의 목록에서 뺍니다 — 묶음을 내려도 남습니다(`Scene::releaseFromParts`). 든 적이 없으면 할 일이 없습니다. */
        void releaseObject( uint64 objectID );

        /** @brief 지금 상태를 값으로 찍습니다. */
        [[nodiscard]] State captureState() const;
        /** @brief @p state 로 되돌립니다. 오브젝트는 짓지 않습니다 — 부르는 쪽이 같은 id 로 이미 복원했습니다. */
        void restoreState( Scene& scene, const SceneSettings& settings, State state );

    private:
        /** @brief 바라는 상태와 다른 묶음을 내리고 짓습니다. 큰 집합을 먼저 내리고 작은 집합을 먼저 짓습니다. */
        void applyGroups();
        /** @brief 묶음 오브젝트 표(`_mapObjectGroup`)를 묶음의 id 목록으로 다시 만듭니다. */
        void rebuildObjectGroupMap();
        /** @brief 켜진 레이어 집합을 바꾼 뒤 부릅니다 — 번호(`getActiveSerial`)를 올리고 리소스 태그 `layer` 축으로 넣습니다 — 리소스의 `<_when _if="layer.Winter">` 가 읽고, 그 태그에 기댄 리소스는 다음 프레임에 다시 읽힌다. */
        void publishActiveLayers();

    private:
        Scene*                        _pScene; ///< 받은 씬(빌림)
        SceneSettings                 _settings;
        vector<Group>                 _listGroup;
        unordered_set<string>         _uniqueActiveLayer;
        unordered_map<uint64, uint32> _mapObjectGroup; ///< 지은 묶음 오브젝트 id → 묶음 번호(`isGroupObject`)
        Subscription                  _layerTags;      ///< 켜진 레이어를 리소스 태그 `layer` 축에 더한 몫(`publishActiveLayers`)
        uint64                        _activeSerial;   ///< `getActiveSerial`
        uint8                         _bDirty : 1;     ///< 켜고 끄기가 있었다 — 다음 갱신이 처리한다
    };
} // namespace sw
