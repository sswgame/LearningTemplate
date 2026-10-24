/**
 * @file InteractionSpatialIndex.h
 * @brief 하는 쪽(`InteractorComponent`)이 여럿일 때 씬 하나가 함께 쓰는 상호작용 대상의 공간 격자입니다.
 * @details 하는 쪽 하나는 등록된 대상 목록을 한 번 도는 편이 싸다(대상 16 · 10k 오브젝트에서 +2~6 us). 대상이 움직일 수 있어 격자를 프레임마다
 *          맞춰야 하고, 그 비용이 목록을 한 번 도는 비용과 같기 때문이다. 하는 쪽이 둘 이상인 공간(3D · 2D)만 격자를 맞추고, 하는 쪽은
 *          격자가 이번 프레임에 맞춰졌을 때만 원 질의로 후보를 받는다. 맞추지 않은 공간은 예전처럼 목록을 돈다.
 *
 *          - 자리: `ISceneFrameSystem` 의 `FrameSystems` 단계(게임 스레드, PrePhysics 틱 앞). 하는 쪽은 PostPhysics 틱(병렬 워커)에서 읽기만 한다.
 *          - 평면: 3D 는 XZ(위 = Y), 2D 는 XY. 평면에 내린 거리는 그 공간의 거리보다 길지 않으므로, 반경 = 최대 도달 거리인 원 질의는 닿는 대상을 빠뜨리지 않는다.
 *          - 셀 = 이번 프레임 대상들의 최대 도달 거리(`InteractionDef::_maxDistance`). 질의 원이 셀 2×2 안팎에 든다.
 *          - 위치는 물리 앞의 값이다. 이번 프레임 물리로 바깥에서 도달 거리 안으로 들어온 대상은 다음 프레임에 후보가 된다(한 프레임 늦음, 판정 자체는 현재 위치로 한다).
 *          - 붙고 떼기: 하는 쪽의 `onBeginPlay` · `onEndPlay` 가 공간별 사용자 수를 센다. 마지막 사용자가 떠나면 매니저에서 뗀다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"
#include "Core/Spatial/SpatialHashGrid2D.h"

#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractionSelector.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class InteractableComponent;

    /**
     * @class InteractionSpatialIndex
     * @brief 공간(3D · 2D)마다 대상 격자 하나를 프레임 앞에 맞춥니다. 파일 머리말 참고.
     */
    class SW_GF_API InteractionSpatialIndex final : public ISceneFrameSystem
    {
    public:
        /** @brief 매니저의 프레임 시스템 키입니다. */
        static constexpr const utf8* kFrameSystemKey = "InteractionSpatialIndex";
        /** @brief 격자를 맞추기 시작하는 하는 쪽 수입니다. 하나는 목록을 한 번 도는 것이 격자를 맞추는 것과 비용이 같다. */
        static constexpr uint32 kMinInteractorCountForGrid = 2;

        InteractionSpatialIndex();

        /** @brief 매니저에 붙은 색인입니다. 없으면 nullptr 입니다. */
        static InteractionSpatialIndex* find( const GameObjectManager& manager );
        /** @brief 하는 쪽 하나를 @p space 의 사용자로 셉니다. 색인이 없으면 붙입니다(게임 스레드, 틱 밖). */
        static void registerInteractor( GameObjectManager& manager, InteractionSpace space );
        /** @brief 하는 쪽 하나를 뺍니다. 사용자가 모두 빠지면 매니저에서 뗍니다. */
        static void unregisterInteractor( GameObjectManager& manager, InteractionSpace space );

        void runBeforeTick( GameObjectManager& manager, float32 deltaTime ) override;
        /**
         * @brief 격자를 맞추는 일입니다. 대상의 월드 자리와 등록부를 읽고(한 프레임 늦어도 된다 — 파일 머리말의 "위치는 물리 앞의 값"), 격자(`InteractionIndex`)를 씁니다.
         */
        SceneFrameAccess getFrameAccess() const override;

        /** @brief 이번 프레임에 @p space 의 격자를 맞췄는지 반환합니다. false 면 부르는 쪽이 목록을 돕니다. */
        bool hasGrid( InteractionSpace space ) const;
        /**
         * @brief @p viewer 의 눈 둘레(반경 = 최대 도달 거리)에 든 대상을 @p outListTarget 에 담습니다. 거리 · 각 판정은 부르는 쪽이 현재 위치로 합니다.
         * @details `hasGrid( viewer._space )` 일 때만 부릅니다. 병렬 틱에서 읽기만 하므로 동시에 불러도 됩니다.
         */
        void queryTargets( const InteractionView& viewer, vector<TComponentHandle<InteractableComponent>>& outListTarget ) const;

        /** @brief @p space 의 사용자 수입니다(테스트 · 진단). */
        uint32 getInteractorCount( InteractionSpace space ) const;

    private:
        /** @brief 공간 하나의 격자입니다. 핸들의 인덱스가 `_listTarget` 의 번호입니다. */
        struct SpaceGrid
        {
            SpatialHashGrid2D                               _grid{};
            vector<TComponentHandle<InteractableComponent>> _listTarget{};
            vector<float3>                                  _listPosition{}; ///< 맞출 때의 위치(`_listTarget` 과 같은 번호). 질의 결과의 판정은 현재 위치로 한다
            float32                                         _maxReach{ 0.0f };
            uint32                                          _interactorCount{ 0 };
            uint8                                           _bBuilt{ SW_FALSE };
        };

        static size_t toSpaceIndex( InteractionSpace space );
        /** @brief 3D 는 (x, z), 2D 는 (x, y) 를 평면 좌표로 냅니다. */
        static void projectToPlane( InteractionSpace space, const float3& position, float32& outPlaneX, float32& outPlaneY );
        void        rebuildSpace( GameObjectManager& manager, InteractionSpace space, SpaceGrid& inoutSpace );

        SpaceGrid _arrSpace[2];
    };
} // namespace sw
