/**
 * @file SceneSpatialGrid.h
 * @brief 씬 하나가 함께 쓰는 수평 공간 색인입니다 — 층(`SpatialGridLayerDesc`)마다 점 · 상자 항목을 2D 해시 격자에 두고, 월드 변경 번호로 움직인 것만 맞춥니다.
 * @details 예전에는 공간 질의를 쓰는 기능마다 프레임마다 자기 격자를 처음부터 지었다(`InteractionSpatialIndex` · `ZoneOccupantIndex`). 이제 항목을 가진 쪽(대상 · 물리
 *          컴포넌트)이 등록 · 해제할 때만 항목을 올리고 내리며, 읽는 쪽은 질의만 한다(언리얼 Chaos 의 외부 가속 구조 — 물리가 맞춘 구조를 게임 스레드 질의가
 *          읽기만 하는 것, 유니티 `Physics` 의 질의 트리와 같은 자리).
 *
 *          - 증분: 움직인 소스만 받는다(`SceneMoveConsumer` — 유니티 `TransformChangeDispatch`, 언리얼 이동이 물리 구조에 올리는 더티, Godot 의 이동 알림과 같은 자리).
 *            서 있는 항목은 프레임마다 묻지 않는다. 움직인 항목은 두 버퍼의 줄에 서고, 버퍼마다 자기 차례에 마지막에 본 월드 변경 번호와 달라진 것만 격자에 고쳐 넣는다.
 *          - 이중 버퍼: 격자는 층마다 둘이다. 읽기는 앞 버퍼만 보고, `SpatialIndex` 단계 끝(`update`, 게임 스레드)이 뒤 버퍼의 바뀐 항목을 모아 워커에 넘긴다.
 *            일은 `TickPostPhysics` 틱과 겹쳐 돌고 `ApplyPostPhysics` 본문 앞(`completeFrameJob`)에서 앞뒤를 바꾼다 — 그 사이 틱의 질의는 지난 프레임의 앞 버퍼를 본다.
 *            버퍼마다 자기가 본 번호를 들므로 두 버퍼에 같은 바뀜을 따로 기록하지 않는다.
 *          - 바로 보임: 항목을 더하거나 빼면(게임 스레드, 틱 밖) 앞 버퍼에 그 자리에서 넣고 뺀다 — 같은 프레임의 질의가 새 항목을 보고 지운 컴포넌트를 받지 않는다.
 *          - 자리: 맞춘 자리는 `SpatialIndex` 단계의 월드 자리다(이번 물리 · 접촉 처리 뒤). 그 뒤에 옮긴 항목은 다음 프레임에 격자에서 옮겨 간다 — 읽는 쪽은 격자로 고르기만
 *            하고 판정은 지금 자리로 한다. 원점 이동은 모든 항목을 한꺼번에 멀리 옮기므로 앞 버퍼를 그 자리에서 고친다(`onWorldOriginShifted`).
 *          - 층: 층 서술자는 항목을 올리는 쪽이 정적 상수로 하나 두고, 읽는 쪽이 같은 상수를 쓴다. 층은 사용자 수가 `_minUserCount` 이상이고 셀이 0 보다 크면 격자를 맞춘다.
 *            셀은 `requireCellSize` 로 받은 값과 항목 반 크기의 두 배 중 큰 값이고 커지기만 한다(커지면 그 층을 다시 짓는다).
 *          - 순서: 질의 결과는 항목을 올린 순서다(격자 칸 번호를 다시 쓰므로 번호 순서는 뜻이 없다).
 */
#pragma once
#include "Core/Concurrency/DevCounter.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Spatial/SpatialHashGrid2D.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Object/Component/SceneMoveDispatch.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class Component;
    class GameObjectManager;
    class SceneComponent;

    /** @brief 층이 월드 자리를 내리는 평면입니다. 3D 는 XZ(위 = Y), 2D 는 XY 입니다. */
    enum class SpatialGridPlane : uint8
    {
        XZ,
        XY,
    };

    /** @brief 층 하나의 서술자입니다. 항목을 올리는 쪽이 정적 상수로 두고, 주소가 층의 열쇠입니다. */
    struct SpatialGridLayerDesc
    {
        const utf8*      _pName;        ///< 진단용 이름
        SpatialGridPlane _plane;        ///< 자리를 내리는 평면
        uint32           _minUserCount; ///< 격자를 맞추기 시작하는 사용자 수(이보다 적으면 읽는 쪽이 자기 목록을 돈다)
    };

    /**
     * @class SceneSpatialGrid
     * @brief 씬마다 하나 있는 공간 색인 서비스입니다. 파일 머리말 참고.
     */
    class SW_API SceneSpatialGrid final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "SceneSpatialGrid";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit SceneSpatialGrid( GameObjectManager& manager );
        ~SceneSpatialGrid() override;

        SceneSpatialGrid( const SceneSpatialGrid& )            = delete;
        SceneSpatialGrid& operator=( const SceneSpatialGrid& ) = delete;

        // 쓰기 — 게임 스레드, 틱 밖. 넘긴 일이 돌고 있으면 먼저 마친다.
        /**
         * @brief @p layer 에 항목을 올립니다. 자리는 @p source 의 월드 자리를 평면에 내린 점 둘레 반 크기 @p extent 의 상자입니다.
         * @param key 질의가 돌려줄 컴포넌트입니다. 항목을 빼기 전까지 살아 있어야 합니다(등록 · 해제 짝).
         */
        [[nodiscard]] SlotHandle add( const SpatialGridLayerDesc& layer, Component& key, const SceneComponent& source, float32 extent );
        /** @brief 항목을 빼고 @p inoutEntry 를 비웁니다. 빈 핸들이면 할 일이 없습니다. */
        void remove( SlotHandle& inoutEntry );
        /** @brief 항목의 반 크기를 바꿉니다(정의가 바뀐 대상). */
        void setExtent( SlotHandle entry, float32 extent );
        /** @brief @p layer 의 사용자를 하나 더합니다. 최소 사용자 수에 닿으면 격자를 짓습니다. */
        void addLayerReader( const SpatialGridLayerDesc& layer );
        /** @brief @p layer 의 사용자를 하나 뺍니다. 최소 사용자 수 아래로 내려가면 격자를 놓습니다. */
        void removeLayerReader( const SpatialGridLayerDesc& layer );
        /** @brief @p layer 의 셀을 적어도 @p cellSize 로 키웁니다. 이미 크거나 같으면 할 일이 없습니다. */
        void requireCellSize( const SpatialGridLayerDesc& layer, float32 cellSize );

        // 읽기 — 앞 버퍼, 어느 스레드에서나(병렬 틱이 함께 읽는다).
        /** @brief @p layer 의 앞 버퍼 격자가 지어져 있으면 true 입니다. false 면 읽는 쪽이 자기 목록을 돈다. */
        bool isActive( const SpatialGridLayerDesc& layer ) const;
        /**
         * @brief @p layer 의 평면 사각형 [@p minX, @p maxX] × [@p minY, @p maxY] 에 걸린 항목의 열쇠를 올린 순서로 @p outListKey 에 담습니다(먼저 비운다).
         * @details `isActive( layer )` 일 때만 부릅니다. 질의마다 자기 버퍼를 쓰므로 동시에 불러도 됩니다. 꺼진 열쇠(`Component::isActive` — 주인의 계층 활성 포함)는 담지 않습니다.
         */
        void query( const SpatialGridLayerDesc& layer, float32 minX, float32 minY, float32 maxX, float32 maxY, vector<Component*>& outListKey ) const;
        /** @brief @p layer 에 올랐던 항목의 반 크기 가운데 가장 큰 값(위쪽 한계)입니다 — 항목 크기에 배율을 곱해 찾는 질의가 상자를 넓힌다. 층이 없으면 0 입니다. */
        float32 getMaxExtent( const SpatialGridLayerDesc& layer ) const;
        /** @brief 3D 월드 자리를 @p layer 의 평면 좌표로 내립니다. */
        static void projectToPlane( const SpatialGridLayerDesc& layer, const float3& position, float32& outPlaneX, float32& outPlaneY );

        /** @brief 뒤 버퍼의 바뀐 항목을 모아 워커에 넘깁니다(`SpatialIndex` 단계 끝). */
        void update( float32 deltaTime ) override;
        /** @brief 넘긴 일을 기다리고 앞뒤 버퍼를 바꿉니다(`ApplyPostPhysics` 본문 앞). */
        void completeFrameJob() override;
        /** @brief 넘긴 일을 기다립니다. 항목은 컴포넌트의 해제가 뺀다. */
        void onSceneCleared() override;
        /**
         * @brief 원점 이동 뒤 앞 버퍼를 지금 자리로 고쳐 넣습니다(`Scene::shiftWorldOrigin` 이 루트를 옮긴 뒤). 뒤 버퍼는 월드 변경 번호가 바뀌어 다음 일이 고친다.
         * @details 다음 `SpatialIndex` 단계까지 두면 그 사이 질의(지역 볼륨 — `ApplyPrePhysics`)가 옛 자리 격자로 골라 안의 대상이 모두 나갔다 다음 프레임에 다시 든다(가짜 이탈 · 진입).
         */
        void onWorldOriginShifted( const float3& delta ) override;

        /** @brief 지금까지 뒤 버퍼에 고쳐 넣은 항목 수입니다(증분을 보는 테스트 · 진단). */
        const DevCounter& getAppliedChangeCount() const { return _appliedChangeCount; }

    private:
        /** @brief 항목 하나입니다. 핸들의 인덱스가 `_listEntry` 의 번호입니다. */
        struct Entry
        {
            Component*            _pKey{ nullptr };    ///< nullptr 이면 빈 칸이거나 뒤 버퍼에서 빼기를 기다린다
            const SceneComponent* _pSource{ nullptr }; ///< 항목보다 먼저 사라질 수 있다(같은 오브젝트의 해체 차례) — `isSourceAlive` 로 본 뒤에만 읽는다
            uint64                _sequence{ 0 };      ///< 올린 순서(질의 결과의 순서)
            float32               _extent{ 0.0f };
            uint32                _layerIndex{ 0 };
            uint32                _sourceSlot{ 0 };                                  ///< 소스의 트랜스폼 칸(살아 있는지 본다)
            uint32                _positionInLayer{ 0 };                             ///< 층의 `_listEntryIndex` 안 자리
            uint32                _generation{ 1 };                                  ///< 핸들 세대(0 은 무효)
            uint32                _nextOfSource{ constant::invalid_index::kUint32 }; ///< 같은 소스의 다음 항목(`_mapSourceEntry` 의 줄)
            uint32                _arrSeenVersion[2]{};                              ///< 버퍼마다 마지막에 넣은 월드 변경 번호. 0 이면 넣지 않았다(번호는 1 부터 오른다)
        };
        /** @brief 층 하나입니다. */
        struct Layer
        {
            const SpatialGridLayerDesc* _pDesc{ nullptr };
            SpatialHashGrid2D           _arrGrid[2];        ///< 앞 · 뒤 버퍼(기본 생성자가 explicit 이라 `{}` 로 복사 목록 초기화하지 않는다)
            float32                     _arrBuiltCell[2]{}; ///< 버퍼마다 지은 셀 크기. 0 이면 짓지 않았다
            float32                     _cellSize{ 0.0f };  ///< 받은 셀 크기와 항목 반 크기 두 배 중 큰 값
            uint32                      _userCount{ 0 };
            vector<uint32>              _listEntryIndex{};
            uint8                       _bRebuildBack{ SW_FALSE }; ///< 넘긴 일이 뒤 버퍼를 새 셀로 다시 짓는다

            bool needsGrid() const { return _userCount >= _pDesc->_minUserCount && _cellSize > 0.0f; }
            /**
             * @brief 지금 셀 크기로 빈 그리드를 만듭니다. 1 이하(작은 항목)는 그리드 기본 셀로 짓는다 — 그리드가 예전에 말없이 하던 바꾸기를 여기로 드러냈다.
             * @details 작은 셀은 질의마다 훑는 셀이 늘어 바꾸기 전에 장면 질의 비용을 재야 한다. `_arrBuiltCell` 은 받은 값 그대로라 다시 짓는 판정은 같다.
             */
            SpatialHashGrid2D makeGrid() const { return SpatialHashGrid2D{ _cellSize > 1.0f ? _cellSize : SpatialHashGrid2D::kDefaultCellSize }; }
        };
        /** @brief 뒤 버퍼에 고쳐 넣을 것 하나입니다. */
        struct Change
        {
            SlotHandle _handle{};
            uint32     _layerIndex{ 0 };
            float32    _minX{ 0.0f };
            float32    _minY{ 0.0f };
            float32    _maxX{ 0.0f };
            float32    _maxY{ 0.0f };
            uint8      _bRemove{ SW_FALSE };
        };

        uint32       getOrCreateLayer( const SpatialGridLayerDesc& layer );
        const Layer* findLayer( const SpatialGridLayerDesc& layer ) const;
        Entry*       resolve( SlotHandle entry );
        Change       makeInsertChange( uint32 entryIndex, uint32& outVersion ) const;
        /** @brief 층의 셀 · 사용자가 바뀐 뒤 앞 버퍼를 맞춥니다 — 맞출 때면 지금 셀로 다시 짓고, 아니면 두 버퍼를 놓습니다. */
        void refreshLayer( uint32 layerIndex );
        /** @brief 넘긴 일입니다(워커). 뒤 버퍼에만 쓴다. */
        void rebuildBackBuffer();
        void waitBackBufferRebuild();
        /** @brief 항목 @p entry 의 소스가 아직 살아 있으면 true 입니다. */
        static bool isSourceAlive( const Entry& entry ) { return SceneMoveConsumer::isAlive( entry._sourceSlot, entry._pSource ); }
        /** @brief 움직인 소스의 항목을 두 버퍼의 줄에 세웁니다(`update` 의 첫 일). */
        void collectMovedEntries();
        /** @brief 항목 @p entryIndex 를 소스의 줄에 잇습니다 — 소스의 움직임을 듣는다. */
        void linkSource( uint32 entryIndex );
        /** @brief 항목 @p entryIndex 를 소스의 줄에서 뗍니다 — 마지막 항목이면 그만 듣는다. */
        void unlinkSource( uint32 entryIndex );

    private:
        GameObjectManager&                           _manager;
        SceneMoveConsumer                            _moves;
        unordered_map<const SceneComponent*, uint32> _mapSourceEntry;     ///< 소스 → 첫 항목 번호(나머지는 `Entry::_nextOfSource`). 열쇠는 견주기만 한다
        vector<SceneComponent*>                      _listMovedSource;    ///< 이번에 꺼낸 움직인 소스(재사용)
        vector<uint32>                               _arrPendingEntry[2]; ///< 버퍼마다 고쳐 넣을지 볼 항목 번호(겹쳐 설 수 있다 — 본 번호가 거른다)
        vector<Entry>                                _listEntry;
        vector<uint32>                               _listFreeEntry;
        vector<uint32>                               _listRemovedEntry; ///< 앞 버퍼에서 뺐고 뒤 버퍼에서 빼기를 기다리는 칸(넘긴 일이 끝나면 비운다)
        vector<Layer>                                _listLayer;
        vector<Change>                               _listChange;    ///< 이번에 넘긴 일의 몫
        Layer*                                       _pJobLayer;     ///< 넘긴 일이 보는 층 배열(일을 띄울 때 잡는다 — 일이 도는 동안 층 벡터는 자라지 않는다)
        uint32                                       _jobLayerCount; ///< 위 배열의 수
        TaskStageHandle                              _jobStage;
        uint64                                       _nextSequence;
        DevCounter                                   _appliedChangeCount;
        uint8                                        _frontIndex;
        uint8                                        _bJobLaunched  : 1;
        uint8                                        _bJobOnWorkers : 1;
    };
} // namespace sw
