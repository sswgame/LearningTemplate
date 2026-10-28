/**
 * @file SceneStreamingSubsystem.h
 * @brief 스트리밍하는 씬의 셀을 스트리밍 원점 둘레로 올리고 내리는 씬 서비스입니다(언리얼 World Partition 의 런타임 스트리밍 · `UWorldPartitionSubsystem` 자리).
 *
 * [데이터]
 * 쿠커가 스트리밍하는 씬(`SceneSettings::_bStreaming`)의 엔티티를 셀로 나눠 쿠킹본에 싣는다(`SceneDocument::_listStreamingCell`). 씬이 그 문서를 지을 때
 * 늘 올라 있는 엔티티만 짓고 셀은 이 서비스에 넘긴다(`Scene::instantiate` → `beginWorld`). XML 로 연 씬(에디터 · Dev)에는 셀이 없어 모든 엔티티가 늘 올라 있다.
 *
 * [갱신 — 씬 프레임의 `DestroyPost` 단계, 게임 스레드]
 * 1. 원점마다 로딩 반경 안의 셀(`StreamingCellGrid::computeCellsInRange`)을 모은다(합집합).
 * 2. 올라 있는 셀 중 모든 원점에서 로딩 반경 + 여유(`SceneSettings::_unloadMargin`)보다 먼 셀을 내린다 — 경계에서 올림 · 내림이 번갈지 않게.
 * 3. 원하는데 내려가 있는 셀을 가까운 순으로 프레임당 `_maxCellLoadsPerFrame` 개까지 짓는다(`Scene::instantiateCell`). 셀의 엔티티는 같은 셀이나
 *    늘 올라 있는 엔티티만 가리키므로(쿠커가 검사했다) 셀 하나를 따로 지어도 참조가 끊기지 않는다.
 * 셀의 오브젝트는 셀마다 id 목록으로 묶어 함께 내린다(`Scene::unloadCell`). 이 단계에서 만든 오브젝트는 다음 프레임의 병합 단계에서 틱에 든다.
 *
 * [원점]
 * 클라이언트는 활성 게임 카메라 자리다(`Scene::update` 가 프레임마다 `setCameraSource` 로 넣는다). 서버처럼 원점이 여럿이면(접속자마다) 게임 코드가
 * `setExternalSources` 로 프레임마다 넣는다 — 한 번 넣으면 카메라 자리는 쓰지 않는다(`clearExternalSources` 로 되돌린다). 원점이 없으면 아무 셀도 원하지 않는다.
 * MMO 서버는 복제기가 관찰자 자리를 넣는다(`MMOReplicator::feedStreamingSources`). 복제기의 관심 영역 격자도 이 격자(`getGrid`)다.
 *
 * [올리는 방식]
 * 셀의 바이트는 씬을 읽을 때(비동기 씬 로드) 함께 메모리에 올라 있다 — 셀마다 파일을 따로 읽지 않는다. 그래서 이 서비스의 일은 짓기 · 내리기이고,
 * 그 비용을 프레임당 셀 수로 나눈다. 셀마다 따로 읽어야 할 만큼 월드가 커지면 쿠킹본에 셀 위치 표를 두고 구간 읽기로 바꾼다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"
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
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        /** @brief 서브시스템 이름입니다(씬 범위의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "SceneStreaming";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /** @brief @p manager 의 스트리밍 서비스를 만듭니다(서비스 레지스트리가 매니저마다 부른다). 셀을 받기 전에는 아무것도 하지 않습니다. */
        explicit SceneStreamingSubsystem( GameObjectManager& manager );

        SceneStreamingSubsystem( const SceneStreamingSubsystem& )            = delete;
        SceneStreamingSubsystem& operator=( const SceneStreamingSubsystem& ) = delete;

        /**
         * @brief 씬 @p scene 의 셀을 받습니다. 받은 셀은 모두 내려가 있고, 다음 갱신부터 원점 둘레를 올립니다.
         * @return 설정이 스트리밍하지 않거나 격자를 만들 수 없거나 같은 셀이 두 번 있으면 오류를 남기고 false 입니다(셀을 받지 않는다).
         */
        [[nodiscard]] bool beginWorld( Scene& scene, const SceneSettings& settings, const vector<SceneDocument::StreamingCell>& listCell );
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
        /** @brief 셀 @p cell 이 올라 있으면 true 입니다. 받지 않은 셀이면 false 입니다. */
        bool isCellLoaded( const StreamingCellCoord& cell ) const;
        /** @brief 받은 격자입니다. `isStreaming` 이 false 이면 쓰지 않습니다. */
        const StreamingCellGrid& getGrid() const { return _grid; }

    private:
        /** @brief 셀 하나 — 엔티티(문서 그대로)와 올라 있으면 지은 오브젝트 id 목록입니다. */
        struct CellSlot
        {
            StreamingCellCoord                     _cell{};
            vector<SceneDocument::SceneObjectNode> _listNode;
            vector<uint64>                         _listObjectID; ///< 지은 오브젝트(엔티티마다 하나, 부품 제외). 내릴 때 자식까지 지운다
            bool                                   _bLoaded{ false };
            bool                                   _bFailed{ false }; ///< 짓다 실패했다 — 다시 시도하지 않는다(프레임마다 같은 오류를 내지 않게)
        };

        /** @brief 셀 좌표의 표 키입니다. */
        static uint64 makeCellKey( const StreamingCellCoord& cell );
        /** @brief 지금 원점 목록입니다(바깥 원점 또는 카메라 원점 하나). */
        span<const float3> getActiveSources() const;
        /** @brief 원점들 중 가장 가까운 것까지 셀 @p cell 의 거리 제곱입니다. 원점이 없으면 가장 큰 값입니다. */
        float32 computeNearestSourceDistanceSquared( const StreamingCellCoord& cell ) const;
        /** @brief 셀을 짓습니다. 실패하면 오류를 남기고 그 셀은 내려간 채로 둡니다. */
        void loadCell( CellSlot& slot );
        /** @brief 셀의 오브젝트를 내립니다. */
        void unloadCell( CellSlot& slot );

        Scene*                        _pScene; ///< 셀을 받은 씬(빌림). nullptr 이면 스트리밍하지 않는다
        SceneSettings                 _settings;
        StreamingCellGrid             _grid;
        vector<CellSlot>              _listCell;
        unordered_map<uint64, uint32> _mapCellIndex;       ///< `makeCellKey` → `_listCell` 인덱스
        vector<float3>                _listExternalSource; ///< `setExternalSources`
        float3                        _cameraSource;       ///< `setCameraSource`
        bool                          _bCameraSourceSet;   ///< 카메라 원점을 한 번이라도 받았다
        bool                          _bExternalSources;   ///< 바깥 원점을 쓴다
        vector<StreamingCellCoord>    _listScratchCell;    ///< 갱신 중 범위 안 셀(프레임마다 다시 쓴다)
        vector<uint32>                _listScratchLoad;    ///< 갱신 중 올릴 셀 인덱스
    };
} // namespace sw
