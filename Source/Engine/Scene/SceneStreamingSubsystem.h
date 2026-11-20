/**
 * @file SceneStreamingSubsystem.h
 * @brief 스트리밍하는 씬의 셀을 스트리밍 원점 둘레로 올리고 내리는 씬 서비스입니다(언리얼 World Partition 의 런타임 스트리밍 · `UWorldPartitionSubsystem` 자리).
 *
 * [데이터]
 * 쿠커가 스트리밍하는 씬(`SceneSettings::_bStreaming`)의 엔티티를 셀로 나눠 쿠킹본에 싣는다(`SceneDocument::_listStreamingCell`). 씬이 그 문서를 지을 때
 * 늘 올라 있는 엔티티만 짓고 셀은 이 서비스에 넘긴다(`Scene::instantiate` → `beginWorld`). XML 로 연 씬(에디터 · Dev)에는 셀이 없어 모든 엔티티가 늘 올라 있다.
 *
 * [갱신 — 씬 프레임의 `DestroyPost` 단계, 게임 스레드] 언리얼 World Partition 의 셀 스트리밍과 같은 단계다 — 색인만 상주, 내용은 비동기 로드, 오브젝트 짓기는 시간 예산.
 * 1. 내리기: 올라 있거나 올리는 중인 셀 중 모든 원점에서 로딩 반경 + 여유(`SceneSettings::_unloadMargin`)보다 멀거나 레이어가 꺼진 셀을 되돌린다 — 경계에서 올림 · 내림이 번갈지 않게.
 *    읽는 중이면 읽기를 취소하고, 지어 있으면 오브젝트의 지금 상태를 셀 엔티티로 떠 둔 뒤(`Scene::captureCellNodes`) 내린다 — 다시 올리면 그 상태로 짓는다(상태 보존).
 * 2. 읽기: 원점마다 로딩 반경 안의 셀(`StreamingCellGrid::computeCellsInRange`, 합집합) 중 내용이 없는 셀을 가까운 순으로 프레임당 `_maxCellLoadsPerFrame` 개까지
 *    쿠킹본의 구간으로 비동기로 읽는다(`AssetIO::readBinaryAssetRangeAsync` — IO 스레드가 읽고 태스크 워커가 엔티티 목록으로 푼다, `SceneDocument::readStreamingCellContent`).
 * 3. 짓기: 내용이 온 셀을 가까운 순으로 게임 스레드에서 **엔티티 단위로** 짓는다(`Scene::beginCellBuild` · `continueCellBuild`). 시간 예산(`_addToWorldBudgetMicroseconds`, 언리얼 AddToWorld 시간 예산)에
 *    닿으면 셀 한가운데서도 멈추고 다음 프레임에 잇는다 — 짓는 중인 셀을 먼저 잇고, 프레임마다 엔티티 하나는 짓는다. 다 짓기 전의 오브젝트는 드러나지 않고(병합 · 틱 · 시작 · 등록 밖)
 *    다 지은 뒤 배치를 잇고 한 번에 드러난다(언리얼 레벨 가시성 단계). 셀의 엔티티는 같은 셀이나 늘 올라 있는 엔티티만 가리키므로(쿠커가 검사했다) 셀 하나를 따로 지어도 참조가 끊기지 않는다.
 * 셀의 오브젝트는 셀마다 id 목록으로 묶어 함께 내린다(`Scene::unloadCell`). 이 단계에서 만든 오브젝트는 다음 프레임의 병합 단계에서 틱에 든다.
 *
 * [셀 경계 — 소속은 내릴 때 자리로(설계 `docs/DesignBacklog.md` "스트리밍 셀 경계 정합 · 저장")]
 * 쿠커가 정한 소속은 처음 값일 뿐이다. 셀을 내리는 프레임에 루트 오브젝트마다 지금 자리(primary 씬 컴포넌트, 쿠커와 같은 식)의 셀을 다시 본다(`regroupObjects`).
 * - 셀이 든 오브젝트가 다른 셀 자리에 있으면 같은 레이어 집합의 그 셀 조각으로 옮긴다 — 그 조각이 올라 있으면(또는 반경 안이라 지금 만든 빈 조각이면) id 만 옮겨 살려 두고,
 *   내려가 있으면 그 오브젝트를 떠서 조각의 떠 둔 상태에 합친다(쿠킹 내용만 든 조각은 들어온 엔티티 `_listArrivedNode` 에 두었다가 다음 짓기에서 같은 배치로 짓는다).
 *   주인 셀이 남고 옮길 셀이 반경 안인데 아직 오르지 않았으면 다음에 본다(사라졌다 나타나지 않게). 옮길 셀이 실패한 조각이면 주인 셀과 함께 떠 둔다.
 * - 어느 셀에도 들지 않은 런타임 스폰(늘 올라 있음 표시 `GameObject::_bAlwaysLoaded` 가 없고 자리가 있는 루트 — 레이어 묶음 · 파생 오브젝트 제외)은 반경 밖이면 그 자리 셀에 든다(유니티 Additive 씬의 "새 오브젝트는 활성 씬에").
 *   네트워크로 복제하는 스폰(`NetObjectTable` 생성 번호 — MMO 엔티티를 세운 오브젝트 등)은 넣지 않는다(존재는 서버가, 보임은 복제기 관심 영역이 정한다). 원점이 하나도 없으면 스폰을 넣지 않는다.
 * 셀을 넘는 참조는 약한 참조다 — 갈라진 뒤 다시 지으면 빈 핸들로 이어진다(배치 규칙).
 *
 * [바닥 없는 물리] 자리 셀에 문서의 조각(켜진 레이어)이 있는데 올라 있지 않으면 바닥이 없다(`isFloorMissing` = `isCellStreamingCompleted` 의 반대 — 바닥이 될 정적 바디가 그 셀에 있을 수 있다).
 * 물리(`ScenePhysics`, 층이 위)가 프레임마다 이것을 물어 동적 강체 · 캐릭터 컨트롤러를 시뮬레이션에서 빼고, 셀이 오르면 풀어 지금 트랜스폼에서 바디를 다시 만든다.
 * 문서의 조각이 없는 셀(늘 올라 있는 지형만)은 바닥이 있다고 본다.
 *
 * [세이브] 떠 둔 상태는 목적 `Snapshot` 이다(런타임 칸까지 — `Scene::captureCellNodes`). 세이브 봉투(`GameStateSnapshot`)는 셀이 든 오브젝트를 씬 섹션에서 빼고(`isCellObject`)
 * 조각마다 문서 내용에서 달라진 상태를 따로 싣는다(`writeSaveState` — 올라 있으면 지금 떠서). 복원은 매니저를 비우면 조각을 문서 내용으로 되돌리고(`onSceneCleared` — 월드는 놓지 않는다)
 * 세이브의 상태를 얹는다(`applySaveState`) — 다음 갱신이 원점 둘레를 그 상태로 짓는다. 짓기는 엔티티를 모두 숨긴 채 만들고 배치를 이은 뒤 한 번에 드러내 등록하므로
 * 물리 · 내비 · AI 는 서로가 모두 선 뒤에 찾는다.
 * 결정적 실행(`DeterministicRunPolicy::_bFinishAssetJobsEachFrame`)은 읽기를 그 프레임에 기다리고 예산 없이 짓는다 — 도착이 워커 순서에 달리지 않게.
 *
 * [데이터 레이어]
 * 한 좌표에 레이어 집합마다 셀 조각이 하나씩 있다(`SceneDocument::StreamingCell::_listDataLayer`, 언리얼 World Partition 의 런타임 셀과 같다). 조각은 범위 안이고 레이어가 모두 켜졌을 때 오른다.
 * 쿠커가 집합이 작은 조각을 앞에 두고, 짓기는 같은 좌표의 앞 조각(켜진 것)이 지어진 뒤에만 뒤 조각을 짓는다 — 읽기가 어느 순서로 끝나도 레이어 조각이 레이어 없는 조각을 파일 id 로 가리켜 끊기지 않는다.
 *
 * [원점]
 * 클라이언트는 활성 게임 카메라 자리다(`Scene::update` 가 프레임마다 `setCameraSource` 로 넣는다). 서버처럼 원점이 여럿이면(접속자마다) 게임 코드가
 * `setExternalSources` 로 프레임마다 넣는다 — 한 번 넣으면 카메라 자리는 쓰지 않는다(`clearExternalSources` 로 되돌린다). 원점이 없으면 아무 셀도 원하지 않는다.
 * MMO 서버는 복제기가 관찰자 자리를 넣는다(`MMOReplicator::feedStreamingSources`). 복제기의 관심 영역 격자도 이 격자(`getGrid`)다.
 * 원점마다 반경 · 우선순위가 있다(`StreamingSource`, 언리얼 World Partition 스트리밍 소스). 올리는 셀은 원점 반경의 합집합이고, 읽기 차례는 셀을 덮는 원점 중
 * 가장 급한 우선순위 → 가까운 거리, 내리기는 모든 원점에서 (그 원점 반경 + 여유) 밖이다. 카메라 원점은 월드 설정의 로딩 반경 · `Normal` 이다(바깥 원점은 반경을 늘 함께 준다).
 *
 * [HLOD — 설계 `docs/DesignBacklog.md` "씬 스트리밍 (라) HLOD · 우선순위 공유 · 로드 큐 디버그"]
 * 쿠커가 셀 조각마다 정적 메시를 인스턴스 메시로 모은 대리 엔티티(늘 올라 있음, 조각의 레이어)를 굽고 조각에 그 파일 id 를 적는다(`SceneDocument::StreamingCell::_hlodFileID`).
 * 조각이 올라 있지 않으면(내려감 · 읽는 중 · 짓는 중 · 실패) 대리를 켜 두고, 다 지어 드러나면 끈다(`updateHLODVisibility` — 갱신 끝, 짓기 뒤). 짓는 동안은 대리가 남아 구멍이 나지 않는다.
 *
 * [우선순위 공유] 셀 읽기 차례 · IO 우선순위와 텍스처 밉 차례 · IO 우선순위가 같은 값이다(`computePriorityAt` — 그 자리 셀을 덮는 원점 중 가장 급한 것, `toAsyncIOPriority`).
 * 메시는 셀을 지을 때 컴포넌트가 읽기를 거므로 셀 짓기 차례(같은 키)를 따른다 — 메시 LOD 스트리밍은 없다.
 *
 * [로드 큐 디버그] 지난 갱신의 올릴 셀 줄(우선순위 → 거리 차례)을 `collectLoadQueue` 가 돌려준다 — 개발 명령 `world.streamqueue` · 감시 값 `Scene.PendingCellCount` 가 읽는다.
 *
 * [셀 내용]
 * 쿠킹본에서 읽은 셀은 색인(좌표 · 레이어 · 내용 구간)만 들고(`SceneDocument::StreamingCell::hasFileContent`), 지은 뒤에는 읽은 엔티티를 버린다 — 다시 올리면 다시 읽는다.
 * 내린 적이 있는 셀은 떠 둔 상태를 들고 그것으로 짓는다(읽지 않는다). 쿠커가 메모리에서 나눈 문서(쿠커 · 테스트)는 셀 내용이 이미 메모리에 있어 읽기 없이 짓기로 간다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneSettings.h"
#include "Engine/Scene/StreamingCellGrid.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
    class Scene;

    /** @brief 셀 조각 하나의 단계입니다(언리얼 World Partition 셀의 Unloaded → Loading → Loaded → Activated 자리). 이름은 `getEnumName` 으로 읽는다(로드 큐 디버그). */
    ENUM()
    enum class StreamingCellState : uint8
    {
        Unloaded, ///< 오브젝트가 없다. 내용은 `_bContentHeld` 이면 `_listNode` 에 있다
        Loading,  ///< 쿠킹본의 구간을 읽는 중이다(`_pRead`)
        Ready,    ///< 내용(`_listNode`)이 있고 짓기를 기다린다
        Building, ///< 엔티티 단위로 짓는 중이다(`_pBuild`) — 지은 오브젝트는 아직 드러나지 않았다
        Loaded,   ///< 오브젝트를 지었다(`_listObjectID`)
        Failed,   ///< 읽기 · 짓기가 실패했다 — 다시 시도하지 않는다(프레임마다 같은 오류를 내지 않게)
    };

    /**
     * @class SceneStreamingSubsystem
     * @brief 파일 머리말 참고.
     */
    class SW_API SceneStreamingSubsystem final : public ISceneService
    {
    public:
        /** @brief 서브시스템 이름입니다(씬 범위의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "SceneStreaming";
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        /** @brief @p manager 의 스트리밍 서비스를 만듭니다(서비스 레지스트리가 매니저마다 부른다). 셀을 받기 전에는 아무것도 하지 않습니다. */
        explicit SceneStreamingSubsystem( GameObjectManager& manager );

        SceneStreamingSubsystem( const SceneStreamingSubsystem& )            = delete;
        SceneStreamingSubsystem& operator=( const SceneStreamingSubsystem& ) = delete;

        /**
         * @brief 씬 @p scene 의 셀을 받습니다. 받은 셀은 모두 내려가 있고, 다음 갱신부터 원점 둘레를 올립니다.
         * @param contentPath 셀 내용이 든 쿠킹본의 리소스 id 입니다(`SceneDocument::_sourcePath`). 내용이 메모리에 있는 셀만 받으면 쓰지 않습니다.
         * @param maxFileCellFileID 파일에 남은(색인만 읽은) 셀 엔티티의 가장 큰 파일 id 입니다(`SceneDocument::_maxCellFileID`).
         * @return 설정이 스트리밍하지 않거나 격자를 만들 수 없거나 같은 셀이 두 번 있거나 파일 내용을 읽을 쿠킹본이 없으면 오류를 남기고 false 입니다(셀을 받지 않는다).
         */
        [[nodiscard]] bool beginWorld( Scene& scene, const SceneSettings& settings, const vector<SceneDocument::StreamingCell>& listCell, string_view contentPath,
                                       uint64 maxFileCellFileID );
        /** @brief 셀을 모두 버립니다. 올라 있는 셀의 오브젝트도 내립니다. */
        void endWorld();

        /** @brief 셀을 올리고 내립니다(파일 머리말의 갱신). */
        void update( float32 deltaTime ) override;
        /**
         * @brief 매니저가 오브젝트를 비웠습니다(세이브 복원). 조각을 문서 내용으로 되돌리고 월드는 놓지 않습니다 — 다음 갱신이 원점 둘레를 다시 짓고, 복원은 그 전에 세이브의 상태를 얹는다(`applySaveState`).
         * @details 씬을 내릴 때는 씬이 먼저 `endWorld` 를 부른다 — 매니저 소멸의 비우기에서는 받은 월드가 없다.
         */
        void onSceneCleared() override;

        /** @brief 이번 프레임의 카메라 원점입니다(씬의 로컬 자리 — 월드 원점을 더해 절대로 든다). 바깥 원점(`setExternalSources`)을 쓰는 동안에는 무시합니다. */
        void setCameraSource( const float3& position );
        /**
         * @brief 원점 목록(반경 · 우선순위가 원점마다 다르다)을 바깥(게임 · 서버 — 접속자마다)이 정합니다. 부른 뒤로는 카메라 원점을 쓰지 않습니다. 빈 목록이면 아무 셀도 원하지 않습니다.
         * @details 올리는 셀은 원점 반경의 합집합이다. 반경이 커도 한 프레임의 일은 받은 셀 수를 넘지 않는다(`update` 가 범위 셀과 받은 셀 중 적은 쪽을 훑는다).
         * @return 위치(X · Z)나 반경이 유한하지 않거나 반경이 음수인 원점이 있으면 오류를 남기고 false 이며 지난 원점을 그대로 둡니다(값을 맞춰 넣지 않는다).
         */
        [[nodiscard]] bool setExternalSources( span<const StreamingSource> listSource );
        /** @brief 바깥 원점을 버리고 카메라 원점으로 돌아갑니다. */
        void clearExternalSources();
        /** @brief 바깥 원점을 쓰는 중이면 true 입니다. */
        bool usesExternalSources() const { return _bExternalSources; }

        /** @brief 세이브에 싣는 조각 하나의 상태입니다(`writeSaveState` · `readSaveState`). 문서 내용 그대로인 조각은 싣지 않습니다. */
        struct SavedCell
        {
            StreamingCellCoord                     _cell{};
            vector<string>                         _listDataLayer;
            vector<SceneDocument::SceneObjectNode> _listNode;              ///< 떠 둔 상태(`_bContentHeld` 일 때만 뜻이 있다)
            vector<SceneDocument::SceneObjectNode> _listArrivedNode;       ///< 다른 셀에서 들어와 아직 짓지 않은 엔티티
            bool                                   _bContentHeld{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };
        /** @brief 세이브의 스트리밍 몫입니다. */
        struct SaveState
        {
            vector<SavedCell> _listCell;
        };
        /**
         * @brief 조각마다 문서 내용에서 달라진 상태를 @p outBytes 에 씁니다(비우고 채운다). 올라 있는 조각은 지금 떠서 쓴다(목적 `Snapshot`). 스트리밍하지 않으면 빈 목록입니다.
         * @details 엔티티의 읽을 목적(`SceneObjectNode::_statePurpose`)도 엔티티마다 싣는다 — 쿠킹 내용은 저작, 떠 둔 것은 스냅숏이다.
         */
        void writeSaveState( vector<uint8>& outBytes ) const;
        /** @brief `writeSaveState` 가 쓴 바이트를 풉니다(검증 — 씬을 건드리지 않는다). 잘렸거나 목적 값이 틀리면 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool readSaveState( const uint8* pData, size_t size, SaveState& outState );
        /** @brief 세이브의 상태를 조각에 얹습니다(없는 조각은 만든다). 매니저를 비운 뒤(`onSceneCleared` — 조각이 문서 내용으로 돌아왔다) 부릅니다. 올라 있는 조각은 먼저 상태 없이 내립니다. */
        void applySaveState( SaveState state );
        /**
         * @brief 셀이 들고 있을 수 있는 엔티티(읽지 않은 쿠킹 내용 · 떠 둔 상태 · 들어온 엔티티, 인스턴스 멤버 제외)의 가장 큰 파일 id 입니다. 스트리밍하지 않으면 0 입니다.
         * @details 씬이 새 오브젝트에 파일 id 를 줄 때(`Scene::computeNextFileID`) 이 위에서 고른다 — 내려간 셀의 엔티티와 겹치면 그 셀이 오를 때 참조가 엉뚱한 오브젝트에 붙는다.
         */
        uint64 getMaxCellFileID() const;
        /** @brief 오브젝트(또는 그 조상)가 셀에 들어 있으면 true 입니다 — 세이브의 씬 섹션이 빼는 오브젝트입니다(셀 몫은 `writeSaveState` 가 싣는다). */
        bool isCellObject( uint64 objectID ) const;
        /**
         * @brief 월드 자리 @p position 의 셀에 문서의 조각(켜진 레이어)이 있는데 올라 있지 않으면 true 입니다 — 바닥이 없다(읽는 중 · 짓는 중 포함, 실패한 조각 제외).
         * @details 물리(`ScenePhysics`)가 동적 바디를 쥘지 묻는다. 스트리밍하지 않으면 false 입니다.
         */
        bool isFloorMissing( const float3& position ) const;

        /** @brief 셀을 받았으면 true 입니다. */
        bool isStreaming() const { return _pScene != nullptr; }
        /** @brief 받은 셀 수입니다. */
        uint32 getCellCount() const { return static_cast<uint32>( _listCell.size() ); }
        /** @brief 올라 있는 셀 수입니다. */
        uint32 getLoadedCellCount() const;
        /** @brief 내용을 읽는 중인 셀 수입니다(취소한 읽기는 세지 않는다). */
        uint32 getLoadingCellCount() const;
        /** @brief 셀 @p cell 이 올라 있으면 true 입니다. 받지 않은 셀이면 false 입니다. */
        bool isCellLoaded( const StreamingCellCoord& cell ) const;
        /**
         * @brief 셀 @p cell 의 스트리밍이 끝났으면 true 입니다 — 그 좌표의 문서 조각(`_bFromDocument`) 가운데 레이어가 켜진 것이 모두 올랐거나 실패했습니다(언리얼 `IsStreamingCompleted` 자리).
         * @details 셀 상태 질의는 이것 하나다 — 물리의 바닥 쥐기(`isFloorMissing`)와 클라이언트 복제 보류(`MMOSceneClientWorld`)가 같은 판정을 본다.
         *          엔티티가 없는 셀(조각 없음) · 옮겨 온 오브젝트를 받으려 만든 조각만 있는 셀 · 스트리밍하지 않는 씬은 기다릴 것이 없어 true 다.
         */
        bool isCellStreamingCompleted( const StreamingCellCoord& cell ) const;
        /** @brief 받은 격자입니다. `isStreaming` 이 false 이면 쓰지 않습니다. */
        const StreamingCellGrid& getGrid() const { return _grid; }
        /** @brief 지금 원점 목록입니다(바깥 원점 또는 카메라 원점 하나, 절대 좌표). 같은 반경으로 따라 올리는 것(내비메시 타일 — `SceneNavigation`)도 이것을 본다. */
        span<const StreamingSource> getActiveSources() const;
        /** @brief 이 거리 안에 닿는 것을 올립니다(월드 설정의 로딩 반경). `isStreaming` 이 false 이면 쓰지 않습니다. */
        float32 getLoadingRadius() const { return _settings._cellGrid._loadingRadius; }
        /** @brief 셀이 오르거나(다 지어 드러남) 내려갈 때마다 오르는 번호입니다. 셀 구역의 입력을 따라 다시 재는 쪽(내비메시 타일)이 본다. */
        uint64 getChangeSerial() const { return _changeSerial; }
        /** @brief 번호 @p serial 뒤에 오르내린 셀의 좌표를 @p outListCell 에 채웁니다(비우고 채운다, 조각마다 한 번 — 같은 좌표가 겹칠 수 있다). */
        void collectCellsChangedSince( uint64 serial, vector<StreamingCellCoord>& outListCell ) const;
        /** @brief 원점마다 (그 원점 반경 + 이 여유)보다 멀면 내립니다(`StreamingSource::_loadingRadius` 가 원점마다 다를 때). */
        float32 getUnloadMargin() const { return _settings._unloadMargin; }

        /**
         * @brief 씬의 로컬 자리 @p position 의 스트리밍 우선순위입니다 — 그 자리 셀을 반경으로 덮는 원점 가운데 가장 급한 것(셀 읽기 차례와 같은 값)이고, 덮는 원점이 없으면 `Lowest` 입니다.
         * @details 텍스처 밉 스트리밍(`TextureStreamingSystem`)이 메시 자리로 묻는다. 스트리밍하지 않으면 모두 `Normal` 입니다. 읽기만 하므로 원점을 바꾸지 않는 단계의 워커에서 불러도 된다.
         */
        StreamingSourcePriority computePriorityAt( const float3& position ) const;

        /** @brief HLOD 대리가 있는 조각 수입니다. */
        uint32 getHLODCount() const;
        /** @brief 지금 켜진(조각이 올라 있지 않아 대신 그리는) HLOD 대리 수입니다. 레이어가 꺼져 대리가 없는 조각은 세지 않습니다. */
        uint32 getShownHLODCount() const;

        /** @brief 로드 큐의 한 줄입니다(`collectLoadQueue`). */
        struct LoadQueueEntry
        {
            StreamingCellCoord      _cell{};
            vector<string>          _listDataLayer;
            StreamingSourcePriority _priority{ StreamingSourcePriority::Lowest };
            float32                 _distance{ 0.0f };                      ///< 가장 가까운 원점까지 셀 거리(m)
            StreamingCellState      _state{ StreamingCellState::Unloaded }; ///< Unloaded · Loading · Ready · Building 가운데 하나
        };
        /**
         * @brief 지난 갱신에서 원점 반경 안이라 올리려는 조각 가운데 아직 오르지 않은 것을 줄 차례(우선순위 → 거리)로 @p outListEntry 에 채웁니다(비우고 채운다).
         * @details 다 지었거나 실패한 조각 · 그 뒤 레이어가 꺼진 조각은 뺀다. 개발 명령 · 감시 값용이다 — 갱신이 쓰는 줄을 그대로 읽으므로 따로 재지 않는다.
         */
        void collectLoadQueue( vector<LoadQueueEntry>& outListEntry ) const;
        /** @brief `collectLoadQueue` 의 줄 수입니다(채우지 않고 센다). */
        uint32 getPendingCellCount() const;

    private:
        using CellState = StreamingCellState;

        /** @brief 워커와 나누는 읽기 결과입니다(정의는 .cpp). 워커가 엔티티를 채운 뒤 상태를 올리고, 게임 스레드는 상태를 본 뒤에만 엔티티를 가져간다. */
        struct CellContentRead;

        /** @brief 셀 하나 — 색인(좌표 · 레이어 · 쿠킹본 구간), 짓기를 기다리거나 떠 둔 엔티티, 올라 있으면 지은 오브젝트 id 목록입니다. */
        struct CellSlot
        {
            StreamingCellCoord                     _cell{};
            vector<string>                         _listDataLayer;      ///< 이 조각의 데이터 레이어. 모두 켜졌을 때 오른다
            uint64                                 _contentOffset{ 0 }; ///< 쿠킹본 안 내용의 시작 바이트(`SceneDocument::StreamingCell`)
            uint64                                 _contentSize{ 0 };   ///< 내용의 바이트 수. 0 이면 내용이 처음부터 메모리에 있다
            vector<SceneDocument::SceneObjectNode> _listNode;           ///< 지을 엔티티 — 읽어 온 내용, 떠 둔 상태, 또는 메모리 문서의 내용
            shared_ptr<CellContentRead>            _pRead;              ///< 읽는 중인 결과(`Loading` 일 때만)
            shared_ptr<Scene::CellBuild>           _pBuild;             ///< 짓는 중인 진행(`Building` 일 때만). `_listNode` 를 가리킨다 — 짓는 동안 `_listNode` 를 바꾸지 않는다
            AsyncReadHandle                        _readHandle;         ///< 읽기 요청(취소 · 결정적 실행의 기다림). 결과와 따로 든다 — 요청의 완료 콜백이 결과를 쥐어 결과가 요청을 쥐면 고리가 된다
            vector<SceneDocument::SceneObjectNode> _listArrivedNode;    ///< 다른 셀에서 들어온 엔티티(떠 둔 상태) — 다음 짓기에서 내용 뒤에 붙여 같은 배치로 짓는다
            vector<uint64>                         _listObjectID;       ///< 든 오브젝트(지은 엔티티 · 옮겨 온 오브젝트, 엔티티마다 하나, 부품 제외). 내릴 때 자식까지 지운다
            uint64                                 _hlodFileID{ 0 };    ///< 이 조각의 HLOD 대리 엔티티(늘 올라 있음)의 파일 id — 0 이면 대리가 없다(`SceneDocument::StreamingCell::_hlodFileID`)
            uint64                                 _hlodObjectID{ 0 };  ///< 풀어 둔 대리 오브젝트 id(`resolveHLODObject` 만 쓴다) — 0 이거나 그 오브젝트가 사라졌으면 파일 id 로 다시 푼다
            CellState                              _state{ CellState::Unloaded };
            uint64                                 _changeSerial{ 0 };         ///< 마지막으로 오르거나 내려간 때의 `SceneStreamingSubsystem::_changeSerial`
            bool                                   _bContentHeld{ SW_FALSE };  ///< `_listNode` 를 짓고 나서도 든다(메모리 문서의 셀 · 떠 둔 상태) — 아니면 지은 뒤 버리고 다시 올릴 때 다시 읽는다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool                                   _bFromDocument{ SW_FALSE }; ///< 문서(쿠킹본)의 조각이다 — 바닥이 될 정적 바디가 있을 수 있다. 옮겨 온 오브젝트를 받으려 만든 조각은 아니다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        /** @brief 번호의 조각입니다(`_listCell` 은 조각을 따로 들어, 조각을 더해도 짓는 중인 조각의 주소가 흔들리지 않는다 — `Scene::CellBuild` 가 `_listNode` 를 가리킨다). */
        CellSlot&       getSlot( uint32 slotIndex ) { return *_listCell[slotIndex]; }
        const CellSlot& getSlot( uint32 slotIndex ) const { return *_listCell[slotIndex]; }
        /** @brief 문서의 셀(`_listSourceCell`)로 조각을 처음 상태로 다시 만듭니다. 오브젝트 · 읽기는 부르는 쪽이 이미 놓았다. */
        void resetCells();
        /** @brief 좌표 @p cell · 레이어 집합 @p listDataLayer 의 조각 번호입니다. 없으면 빈 조각(떠 둔 상태가 빈 목록)을 그 좌표의 끝 조각으로 만듭니다. */
        uint32 findOrAddSlot( const StreamingCellCoord& cell, const vector<string>& listDataLayer );
        /** @brief 씬의 로컬 자리 @p local 을 절대 자리로 옮깁니다(+ 월드 원점, `Scene::getWorldOrigin`). 격자 · 원점 · 셀 판정은 절대 좌표다(쿠킹본 · 서버와 같다). */
        float3 toAbsolute( const float3& local ) const;
        /** @brief 오브젝트의 셀입니다 — 쿠커와 같이 primary 씬 컴포넌트의 월드 자리(절대)입니다. 씬 컴포넌트가 없으면 false 입니다. */
        bool computeObjectCell( const GameObject& object, StreamingCellCoord& outCell ) const;
        /**
         * @brief 셀을 내리는 프레임에 루트 오브젝트의 소속을 지금 자리로 다시 정합니다(파일 머리말의 셀 경계). @p listUnloading 은 이번에 내릴 조각입니다.
         * @details 셀이 든 오브젝트가 다른 셀 자리에 있으면 옮기고, 어느 셀에도 들지 않은 런타임 스폰이 반경 밖이면 그 자리 셀에 넣는다.
         */
        void regroupObjects( const vector<uint32>& listUnloading );
        /**
         * @brief 루트 @p rootObjectID 와 그 자손 엔티티(@p fromIndex 조각에 든 것)를 좌표 @p cell 의 조각(@p listDataLayer)으로 옮깁니다. @p fromIndex 가 `kNoSlot` 이면 셀에 들지 않은 런타임 스폰입니다.
         * @param bFromUnloading 주인 조각이 이번에 내린다 — 옮길 조각이 반경 안인데 아직 오르지 않았어도 떠서 넘긴다.
         */
        void moveObjectGroup( uint64 rootObjectID, uint32 fromIndex, const StreamingCellCoord& cell, const vector<string>& listDataLayer, bool bFromUnloading );
        /** @brief 올릴 셀의 정렬 키 — 덮는 원점의 가장 급한 우선순위, 가장 가까운 원점 거리 제곱, 셀 인덱스(결정적 끝 비교)입니다. */
        struct LoadOrderKey
        {
            StreamingSourcePriority _priority{ StreamingSourcePriority::Lowest };
            float32                 _distanceSquared{ 0.0f };
            uint32                  _slotIndex{ 0 };
        };

        /** @brief 셀 좌표의 표 키입니다. */
        static uint64 makeCellKey( const StreamingCellCoord& cell );
        /** @brief 원점들 중 가장 가까운 것까지 셀 @p cell 의 거리 제곱입니다. 원점이 없으면 가장 큰 값입니다. */
        float32 computeNearestSourceDistanceSquared( const StreamingCellCoord& cell ) const;
        /** @brief 셀 @p cell 이 원점 @p source 의 반경 안이면 true 입니다. */
        bool isCellInSource( const StreamingCellCoord& cell, const StreamingSource& source ) const;
        /** @brief 셀 @p cell 을 반경으로 덮는 원점 가운데 가장 급한 우선순위입니다. 덮는 원점이 없으면 `Lowest` 입니다. */
        StreamingSourcePriority computeCellPriority( const StreamingCellCoord& cell ) const;
        /** @brief 어느 원점이든 (그 원점 반경 + @p margin) 안에 셀 @p cell 이 닿으면 true 입니다. */
        bool isCellWithinSources( const StreamingCellCoord& cell, float32 margin ) const;
        /** @brief 셀 내용의 비동기 읽기를 겁니다(`Unloaded` → `Loading`). 걸지 못하면 오류를 남기고 `Failed` 입니다. */
        void requestCellContent( CellSlot& slot );
        /** @brief 읽기가 끝났으면 거둡니다(`Loading` → `Ready` · `Failed`). 끝나지 않았으면 그대로입니다. */
        void collectCellContent( CellSlot& slot );
        /**
         * @brief 셀을 이어 짓습니다(`Ready` → `Building` → `Loaded`). @p stopwatch 가 예산에 닿으면 멈추고 false 입니다(다음 프레임에 잇는다). @p bForceProgress 면 엔티티 하나는 짓습니다.
         * @details 실패하면 오류를 남기고 지은 것을 내린 뒤 `Failed` 입니다(그때도 true — 이 셀은 끝났다).
         */
        [[nodiscard]] bool buildCell( uint32 slotIndex, const Stopwatch& stopwatch, int64 budgetNanoseconds, bool bForceProgress );
        /** @brief 셀을 내립니다 — 읽기는 취소하고, 든 오브젝트는 @p bKeepState 면 상태를 떠 둔 뒤 지웁니다(`Unloaded`). */
        void unloadCell( CellSlot& slot, bool bKeepState );
        /** @brief 올릴 셀 줄(`_listScratchLoad`)을 시간 예산 안에서 엔티티 단위로 짓습니다(파일 머리말의 갱신 3). 예산에 닿으면 남은 셀은 다음 프레임에 잇습니다. */
        void buildQueuedCells( bool bFinishEachFrame );
        /** @brief 조각마다 HLOD 대리를 조각이 올라 있지 않을 때만 켭니다(바뀐 것만 `GameObject::setActive`). */
        void updateHLODVisibility();
        /**
         * @brief 조각의 HLOD 대리 오브젝트입니다 — 풀어 둔 id(`CellSlot::_hlodObjectID`)를 쓰고, 없거나 사라졌으면(레이어를 껐다 켬 · 세이브 복원) 파일 id 로 한 번 다시 풀어 듭니다.
         * @details 대리가 없거나(파일 id 0) 레이어가 꺼져 대리가 없으면 nullptr 이고 풀지 않는다 — 파일 id 찾기는 대리가 다시 설 때만 돈다.
         */
        GameObject* resolveHLODObject( CellSlot& slot );
        /** @brief 풀어 둔 대리 오브젝트입니다(풀지 않는다 — 감시 값 · 개발 명령). 아직 풀지 않았거나 사라졌으면 nullptr 입니다. */
        const GameObject* findResolvedHLODObject( const CellSlot& slot ) const;
        /** @brief 조각이 로드 큐에 남아 있으면(오르지 않았고 실패하지 않았고 레이어가 켜졌다) true 입니다. */
        bool isSlotPending( const CellSlot& slot ) const;
        /** @brief 조각의 데이터 레이어가 모두 켜져 있으면 true 입니다. */
        bool areSlotLayersActive( const CellSlot& slot ) const;
        /** @brief 같은 좌표에서 @p slotIndex 앞의 조각 가운데 켜진 것이 모두 지어졌으면(또는 실패했으면) true 입니다 — 뒤 조각은 앞 조각을 가리킬 수 있다. */
        bool areEarlierSlotsBuilt( uint32 slotIndex ) const;

    private:
        /** @brief 셀에 들지 않았다는 조각 번호입니다(`moveObjectGroup`). */
        static constexpr uint32 kNoSlot = constant::invalid_index::kUint32;

        Scene*                                _pScene; ///< 셀을 받은 씬(빌림). nullptr 이면 스트리밍하지 않는다
        SceneSettings                         _settings;
        string                                _contentPath; ///< 셀 내용이 든 쿠킹본의 리소스 id
        StreamingCellGrid                     _grid;
        vector<SceneDocument::StreamingCell>  _listSourceCell;       ///< 받은 문서의 셀(쿠킹본은 색인만) — 복원이 조각을 처음 상태로 되돌린다(`resetCells`)
        vector<unique_ptr<CellSlot>>          _listCell;             ///< 문서의 조각(문서 순서) 뒤에 옮겨 온 오브젝트를 받으려 만든 조각
        unordered_map<uint64, vector<uint32>> _mapCellIndex;         ///< `makeCellKey` → 그 좌표의 조각들(`_listCell` 인덱스, 문서 순서)
        unordered_map<uint64, uint32>         _mapObjectSlot;        ///< 다 지었거나 옮겨 온 오브젝트 id → 든 조각(`_listObjectID` 의 거울)
        uint64                                _maxFileCellFileID;    ///< 파일에 남은 셀 엔티티의 가장 큰 파일 id(`beginWorld`)
        vector<StreamingSource>               _listExternalSource;   ///< `setExternalSources`
        StreamingSource                       _cameraSource;         ///< `setCameraSource`. 반경은 월드 설정의 로딩 반경
        uint8                                 _bCameraSourceSet : 1; ///< 카메라 원점을 한 번이라도 받았다
        uint8                                 _bExternalSources : 1; ///< 바깥 원점을 쓴다
        uint64                                _changeSerial;         ///< 셀이 오르내린 번호(`getChangeSerial`). 월드를 바꿔도 되돌리지 않는다 — 보는 쪽의 마지막 번호가 늘 지금 이하다
        vector<StreamingCellCoord>            _listScratchCell;      ///< 갱신 중 범위 안 셀(프레임마다 다시 쓴다)
        vector<uint32>                        _listScratchLoad;      ///< 갱신 중 범위 안의 올릴 셀 인덱스(가까운 순)
        vector<uint32>                        _listScratchUnload;    ///< 갱신 중 내릴 셀 인덱스
        vector<LoadOrderKey>                  _listScratchLoadKey;   ///< 갱신 중 올릴 셀의 정렬 키(셀마다 한 번 잰다)
    };
} // namespace sw
