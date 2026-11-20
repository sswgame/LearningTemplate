/**
 * @file SceneSubLevelSubsystem.h
 * @brief 런타임에 다른 씬을 활성 씬에 더하고 빼는 씬 서비스입니다(언리얼 `ULevelStreamingDynamic::LoadLevelInstance` · 유니티 `SceneManager.LoadSceneAsync( Additive )` 자리).
 *
 * [모델 — 런타임 씬 인스턴스(설계 `docs/DesignBacklog.md` "런타임 하위 레벨 · Packed Level · 원점 이동")]
 * 하위 레벨은 씬 파일 안의 씬 인스턴스(`<entity scene=…>`)를 실행 중에 놓은 것이다. 월드는 하나(오브젝트 매니저 하나)이고, 더한 씬의 엔티티는 앵커 오브젝트 아래에 펼친다.
 * - 앵커: 하위 레벨마다 씬 컴포넌트 하나를 가진 오브젝트를 놓은 자리(위치 · 회전)에 세운다. 늘 올라 있음(`GameObject::_bAlwaysLoaded`)이라 스트리밍 셀이 삼키지 않는다.
 * - 파일 id: 앵커는 이름에서 정한 넓은 값(`computeAnchorFileID`)이고 멤버는 내부 파일 id ^ 앵커 범위(`SceneDocument::computeInstanceIDScope`)다 — 파일 안 씬 인스턴스와 같은 규칙이라
 *   부착 · 핸들이 자기 하위 레벨 안에서 이어지고, 같은 씬을 두 번 더해도 겹치지 않는다. 이름이 같으면 프로세스가 달라도 id 가 같다(서버 · 클라이언트가 같은 하위 레벨을 각자 더해도
 *   MMO 정적 엔티티 `_staticID` · 넷 씬 번호가 맞는다). 앵커 id 는 씬의 다음 파일 id 셈에서 뺀다(`Scene::computeNextFileID` 가 묻는다).
 * - 이름: 멤버는 `<하위 레벨 이름>/<엔티티 이름>` 이다(씬 인스턴스와 같다).
 *
 * [갱신 — `DestroyPost`, 게임 스레드] 더하기 · 빼기는 요청만 받고 다음 갱신이 처리한다(틱 도중 불러도 된다).
 * 1. 빼기: 지은 오브젝트(앵커 · 멤버)를 내린다(`Scene::unloadCell`). 짓는 중이면 짓기를 버린다.
 * 2. 짓기: 앵커를 세우고 멤버를 엔티티 단위로 짓는다(`Scene::beginCellBuild` · `continueCellBuild` — 스트리밍 셀과 같은 길, 시간 예산은 월드 설정 `_addToWorldBudgetMicroseconds`,
 *    0 이면 한 프레임에 다 짓는다). 다 지은 뒤 한 번에 드러난다.
 *
 * [세이브 — 데이터 레이어 묶음과 같은 꼴] 세이브 봉투는 하위 레벨 오브젝트를 씬 섹션에서 빼고(`isSubLevelObject`) 하위 레벨마다 이름 · 씬 · 자리 · 지금 상태(목적 `Snapshot`)를 싣는다.
 * 복원은 매니저를 비우면(`onSceneCleared`) 하위 레벨을 모두 버리고, 세이브에 실린 것을 그 상태로 다시 세운다(`applySaveState` — 문서를 다시 읽지 않는다).
 *
 * [스트리밍 · 복제] 하위 레벨은 셀에 들지 않는다(앵커가 늘 올라 있고 멤버는 앵커의 자식이다). 더하기 · 빼기는 프로세스마다의 일이다 — 서버의 복제(`NetObjectTable` 번호 · MMO 관심 영역)는
 * 게임이 하위 레벨 오브젝트에 번호를 매길 때만 따르고, 클라이언트는 같은 이름으로 같은 하위 레벨을 더한다(언리얼도 스트리밍 레벨 상태를 게임이 클라이언트에 맞춘다).
 *
 * [받지 않는 하위 레벨] 스트리밍하는 씬(셀이 있다) · 데이터 레이어를 쓰는 씬은 하위 레벨로 받지 않는다(오류). 하위 레벨은 한 묶음으로 오르내리는 단위라 그 안을 다시 나누지 않는다.
 * shortcut: 문서 읽기는 더하는 프레임의 게임 스레드에서 한다(짓기만 프레임에 나눈다) — 큰 하위 레벨이 생기면 씬 로드 단계(`SceneManager` 의 `_loadStage`)로 옮긴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneSubLevelTypes.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class SceneSubLevelSubsystem
     * @brief 파일 머리말 참고.
     */
    class SW_API SceneSubLevelSubsystem final : public ISceneService
    {
    public:
        /** @brief 서브시스템 이름입니다(씬 범위의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "SceneSubLevels";
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        /** @brief @p manager 의 하위 레벨 서비스를 만듭니다(서비스 레지스트리가 매니저마다 부른다). */
        explicit SceneSubLevelSubsystem( GameObjectManager& manager );

        SceneSubLevelSubsystem( const SceneSubLevelSubsystem& )            = delete;
        SceneSubLevelSubsystem& operator=( const SceneSubLevelSubsystem& ) = delete;

        using Placement = SubLevelPlacement;
        using State     = SceneSubLevelCapture;

        /**
         * @brief 씬 @p scenePath 를 이름 @p name 의 하위 레벨로 @p scene 에 더합니다. 다음 갱신부터 짓습니다.
         * @return 이름이 비었거나 이미 있거나, 씬을 읽지 못했거나, 스트리밍하는 씬 · 데이터 레이어를 쓰는 씬이거나, @p scene 이 이 서비스의 매니저가 아니면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool addSubLevel( Scene& scene, string_view name, string_view scenePath, const Placement& placement );
        /** @brief 이름 @p name 의 하위 레벨을 뺍니다(다음 갱신에 내린다). 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool removeSubLevel( string_view name );
        /** @brief 씬을 내립니다(`Scene::shutdown`) — 하위 레벨을 모두 버립니다. 오브젝트는 씬이 지운다. */
        void endScene();

        /** @brief 빼기 · 짓기를 처리합니다(파일 머리말의 갱신). */
        void update( float32 deltaTime ) override;
        /** @brief 매니저가 오브젝트를 비웠습니다(세이브 복원) — 하위 레벨을 모두 버립니다. 복원은 이어서 세이브에 실린 하위 레벨을 다시 세운다(`applySaveState`). */
        void onSceneCleared() override;

        /** @brief 하위 레벨 수입니다(빼는 중 포함). */
        uint32 getSubLevelCount() const { return static_cast<uint32>( _listSubLevel.size() ); }
        /** @brief 다 지어 드러난 하위 레벨 수입니다. */
        uint32 getLoadedSubLevelCount() const;
        /** @brief 이름 @p name 의 하위 레벨 단계입니다. 없으면 false 입니다. */
        [[nodiscard]] bool findSubLevelState( string_view name, SubLevelState& outState ) const;
        /** @brief 하위 레벨 이름을 더한 순서로 채웁니다(비우고 채운다). */
        void collectSubLevelNames( vector<string>& outListName ) const;
        /** @brief 오브젝트(또는 그 조상)가 하위 레벨의 앵커 · 멤버면 true 입니다 — 세이브의 씬 섹션 · 씬 문서 쓰기가 빼는 오브젝트입니다. */
        bool isSubLevelObject( uint64 objectID ) const;
        /** @brief 파일 id 가 하위 레벨 앵커의 것이면 true 입니다 — 씬이 다음 파일 id 를 셀 때 그 넓은 id 를 뺀다(앵커를 떠 둔 상태로 짓는 동안에도). */
        bool isAnchorFileID( uint64 fileID ) const;

        /** @brief 이름 @p name 의 하위 레벨 앵커의 파일 id 입니다 — 넓은 값이라 씬 파일의 작은 id 와 겹치지 않고, 프로세스가 달라도 같다. */
        [[nodiscard]] static uint64 computeAnchorFileID( string_view name );
        /**
         * @brief 씬 문서 @p doc 의 엔티티를 앵커 @p anchorFileID 아래의 멤버로 옮깁니다(파일 id · 글 상태 범위 · 쿠킹 상태 범위 · 앵커 · 이름). 문서를 다시 쓰지 않고 @p outListNode 를 채운다.
         * @details 파일 안 씬 인스턴스를 펼치는 규칙(`SceneDocument::expandSceneInstances`)과 같다 — 앵커 없는 멤버는 이 앵커에, 안쪽 인스턴스의 멤버는 안쪽 앵커에 붙는다.
         */
        static void makeMemberNodes( const SceneDocument& doc, string_view name, uint64 anchorFileID, vector<SceneDocument::SceneObjectNode>& outListNode );

        /** @brief 세이브에 싣는 하위 레벨 하나입니다. */
        struct SavedSubLevel
        {
            string                                 _name;
            string                                 _scenePath;
            Placement                              _placement{};
            vector<SceneDocument::SceneObjectNode> _listNode; ///< 지을 엔티티 — 다 지었으면 떠 둔 상태(앵커 포함, `Snapshot`), 아니면 멤버 내용(앵커는 세울 때 만든다)
        };
        /** @brief 세이브의 하위 레벨 몫입니다. */
        struct SaveState
        {
            vector<SavedSubLevel> _listSubLevel;
        };
        /** @brief 하위 레벨마다 이름 · 씬 · 자리 · 지금 상태를 @p outBytes 에 씁니다(비우고 채운다). 실패 · 빼는 중인 하위 레벨은 싣지 않습니다. */
        void writeSaveState( vector<uint8>& outBytes ) const;
        /** @brief `writeSaveState` 의 짝입니다(검증 — 씬을 건드리지 않는다). 잘렸으면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool readSaveState( const uint8* pData, size_t size, SaveState& outState );
        /** @brief 세이브의 하위 레벨을 @p scene 에 다시 세웁니다(매니저를 비운 뒤). 다음 갱신이 실린 상태로 짓습니다. 이름이 겹치면 오류를 남기고 뒤의 것을 건너뜁니다. */
        void applySaveState( Scene& scene, SaveState state );

        /** @brief 다 지은 하위 레벨을 값으로 찍습니다. */
        [[nodiscard]] State captureState() const;
        /** @brief @p state 로 되돌립니다. 오브젝트는 짓지 않습니다 — 부르는 쪽이 같은 id 로 이미 복원했습니다. */
        void restoreState( Scene& scene, State state );

    private:
        /** @brief 하위 레벨 하나입니다. `Scene::CellBuild` 가 `_listNode` 를 가리키므로 따로 든다(`unique_ptr`). */
        struct SubLevel
        {
            string                                 _name;
            string                                 _scenePath;
            Placement                              _placement{};
            uint64                                 _anchorFileID{ 0 };
            uint64                                 _anchorObjectID{ 0 };
            vector<SceneDocument::SceneObjectNode> _listNode;     ///< 지을 엔티티(짓는 동안 바꾸지 않는다)
            shared_ptr<Scene::CellBuild>           _pBuild;       ///< 짓는 중인 진행(`Building` 일 때만)
            vector<uint64>                         _listObjectID; ///< 앵커와 지은 멤버(엔티티마다 하나, 부품 제외). 내릴 때 자식까지 지운다
            SubLevelState                          _state{ SubLevelState::Queued };
        };

        /** @brief 이름의 하위 레벨입니다. 없으면 nullptr 입니다. */
        SubLevel* findSubLevel( string_view name );
        /** @brief 이름 · 씬 · 자리 · 지을 엔티티로 하위 레벨을 목록 끝에 더합니다(`Queued`). */
        void pushSubLevel( string_view name, string_view scenePath, const Placement& placement, vector<SceneDocument::SceneObjectNode> listNode );
        /** @brief 앵커를 세우고 짓기를 시작합니다(`Queued` → `Building`). 앵커를 세우지 못하면 오류를 남기고 `Failed` 입니다. */
        void beginBuild( SubLevel& subLevel );
        /** @brief 이 서비스가 세운 앵커를 id 목록 앞에 넣습니다(이미 있거나 앵커가 없으면 그대로). 짓기가 끝난 뒤 · 내리기 전에 부른다. */
        void includeAnchorObject( SubLevel& subLevel );
        /** @brief 하위 레벨의 오브젝트를 내리고 표에서 뺍니다. */
        void unloadSubLevel( SubLevel& subLevel );
        /** @brief 오브젝트 표(`_mapObjectSubLevel`)를 하위 레벨의 id 목록으로 다시 만듭니다. */
        void rebuildObjectMap();

    private:
        Scene*                        _pScene;   ///< 하위 레벨을 받은 씬(빌림). 첫 더하기 · 복원이 정하고 `endScene` 이 놓는다
        GameObjectManager*            _pManager; ///< 이 서비스의 매니저(빌림)
        vector<unique_ptr<SubLevel>>  _listSubLevel;
        unordered_map<uint64, uint64> _mapObjectSubLevel; ///< 앵커 · 멤버 오브젝트 id → 앵커 오브젝트 id(`isSubLevelObject`)
    };
} // namespace sw
