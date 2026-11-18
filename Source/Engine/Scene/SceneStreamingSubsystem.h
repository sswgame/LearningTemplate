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
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneSettings.h"
#include "Engine/Scene/StreamingCellGrid.h"

namespace sw
{
    class GameObjectManager;
    class Scene;

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
         * @return 설정이 스트리밍하지 않거나 격자를 만들 수 없거나 같은 셀이 두 번 있거나 파일 내용을 읽을 쿠킹본이 없으면 오류를 남기고 false 입니다(셀을 받지 않는다).
         */
        [[nodiscard]] bool beginWorld( Scene& scene, const SceneSettings& settings, const vector<SceneDocument::StreamingCell>& listCell, string_view contentPath );
        /** @brief 셀을 모두 버립니다. 올라 있는 셀의 오브젝트도 내립니다. */
        void endWorld();

        /** @brief 셀을 올리고 내립니다(파일 머리말의 갱신). */
        void update( float32 deltaTime ) override;
        /** @brief 씬을 비울 때 셀을 버립니다(오브젝트는 매니저가 이미 비웠다). */
        void onSceneCleared() override;

        /** @brief 이번 프레임의 카메라 원점입니다. 바깥 원점(`setExternalSources`)을 쓰는 동안에는 무시합니다. */
        void setCameraSource( const float3& position );
        /** @brief 원점 목록을 바깥(게임 · 서버)이 정합니다. 부른 뒤로는 카메라 원점을 쓰지 않습니다. 빈 목록이면 아무 셀도 원하지 않습니다. */
        void setExternalSources( span<const float3> listSource );
        /** @brief 바깥 원점을 버리고 카메라 원점으로 돌아갑니다. */
        void clearExternalSources();
        /** @brief 바깥 원점을 쓰는 중이면 true 입니다. */
        bool usesExternalSources() const { return _bExternalSources; }

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
        /** @brief 받은 격자입니다. `isStreaming` 이 false 이면 쓰지 않습니다. */
        const StreamingCellGrid& getGrid() const { return _grid; }
        /** @brief 지금 원점 목록입니다(바깥 원점 또는 카메라 원점 하나). 같은 반경으로 따라 올리는 것(내비메시 타일 — `SceneNavigation`)도 이것을 본다. */
        span<const float3> getActiveSources() const;
        /** @brief 이 거리 안에 닿는 것을 올립니다(월드 설정의 로딩 반경). `isStreaming` 이 false 이면 쓰지 않습니다. */
        float32 getLoadingRadius() const { return _settings._cellGrid._loadingRadius; }
        /** @brief 모든 원점에서 이 거리보다 멀면 내립니다(로딩 반경 + 여유 — 경계에서 번갈지 않게). */
        float32 getUnloadRadius() const { return _settings._cellGrid._loadingRadius + _settings._unloadMargin; }

    private:
        /** @brief 셀 하나의 단계입니다(언리얼 World Partition 셀의 Unloaded → Loading → Loaded → Activated 자리). */
        enum class CellState : uint8
        {
            Unloaded, ///< 오브젝트가 없다. 내용은 `_bContentHeld` 이면 `_listNode` 에 있다
            Loading,  ///< 쿠킹본의 구간을 읽는 중이다(`_pRead`)
            Ready,    ///< 내용(`_listNode`)이 있고 짓기를 기다린다
            Building, ///< 엔티티 단위로 짓는 중이다(`_pBuild`) — 지은 오브젝트는 아직 드러나지 않았다
            Loaded,   ///< 오브젝트를 지었다(`_listObjectID`)
            Failed,   ///< 읽기 · 짓기가 실패했다 — 다시 시도하지 않는다(프레임마다 같은 오류를 내지 않게)
        };

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
            vector<uint64>                         _listObjectID;       ///< 지은 오브젝트(엔티티마다 하나, 부품 제외). 내릴 때 자식까지 지운다
            CellState                              _state{ CellState::Unloaded };
            bool                                   _bContentHeld{ SW_FALSE }; ///< `_listNode` 를 짓고 나서도 든다(메모리 문서의 셀 · 떠 둔 상태) — 아니면 지은 뒤 버리고 다시 올릴 때 다시 읽는다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        /** @brief 셀 좌표의 표 키입니다. */
        static uint64 makeCellKey( const StreamingCellCoord& cell );
        /** @brief 원점들 중 가장 가까운 것까지 셀 @p cell 의 거리 제곱입니다. 원점이 없으면 가장 큰 값입니다. */
        float32 computeNearestSourceDistanceSquared( const StreamingCellCoord& cell ) const;
        /** @brief 셀 내용의 비동기 읽기를 겁니다(`Unloaded` → `Loading`). 걸지 못하면 오류를 남기고 `Failed` 입니다. */
        void requestCellContent( CellSlot& slot );
        /** @brief 읽기가 끝났으면 거둡니다(`Loading` → `Ready` · `Failed`). 끝나지 않았으면 그대로입니다. */
        void collectCellContent( CellSlot& slot );
        /**
         * @brief 셀을 이어 짓습니다(`Ready` → `Building` → `Loaded`). @p stopwatch 가 예산에 닿으면 멈추고 false 입니다(다음 프레임에 잇는다). @p bForceProgress 면 엔티티 하나는 짓습니다.
         * @details 실패하면 오류를 남기고 지은 것을 내린 뒤 `Failed` 입니다(그때도 true — 이 셀은 끝났다).
         */
        [[nodiscard]] bool buildCell( CellSlot& slot, const Stopwatch& stopwatch, int64 budgetNanoseconds, bool bForceProgress );
        /** @brief 셀을 내립니다 — 읽기는 취소하고, 지은 오브젝트는 @p bKeepState 면 상태를 떠 둔 뒤 지웁니다(`Unloaded`). */
        void unloadCell( CellSlot& slot, bool bKeepState );
        /** @brief 조각의 데이터 레이어가 모두 켜져 있으면 true 입니다. */
        bool areSlotLayersActive( const CellSlot& slot ) const;
        /** @brief 같은 좌표에서 @p slotIndex 앞의 조각 가운데 켜진 것이 모두 지어졌으면(또는 실패했으면) true 입니다 — 뒤 조각은 앞 조각을 가리킬 수 있다. */
        bool areEarlierSlotsBuilt( uint32 slotIndex ) const;

    private:
        Scene*                                _pScene; ///< 셀을 받은 씬(빌림). nullptr 이면 스트리밍하지 않는다
        SceneSettings                         _settings;
        string                                _contentPath; ///< 셀 내용이 든 쿠킹본의 리소스 id
        StreamingCellGrid                     _grid;
        vector<CellSlot>                      _listCell;
        unordered_map<uint64, vector<uint32>> _mapCellIndex;         ///< `makeCellKey` → 그 좌표의 조각들(`_listCell` 인덱스, 문서 순서)
        vector<float3>                        _listExternalSource;   ///< `setExternalSources`
        float3                                _cameraSource;         ///< `setCameraSource`
        uint8                                 _bCameraSourceSet : 1; ///< 카메라 원점을 한 번이라도 받았다
        uint8                                 _bExternalSources : 1; ///< 바깥 원점을 쓴다
        vector<StreamingCellCoord>            _listScratchCell;      ///< 갱신 중 범위 안 셀(프레임마다 다시 쓴다)
        vector<uint32>                        _listScratchLoad;      ///< 갱신 중 범위 안의 올릴 셀 인덱스(가까운 순)
    };
} // namespace sw
