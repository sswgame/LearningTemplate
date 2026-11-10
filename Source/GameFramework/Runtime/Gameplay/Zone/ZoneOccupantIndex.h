/**
 * @file ZoneOccupantIndex.h
 * @brief 지역 볼륨(`ZoneVolumeComponent`)들이 함께 쓰는 "안에 들 수 있는 물리 개체" 의 프레임 격자입니다.
 * @details 예전에는 볼륨마다 틱 뒤에 씬의 캐릭터 컨트롤러 · 강체를 모두 돌았다 — 볼륨 Z 개 × 개체 N 개(지역 수백이면 N² 모양). 이제 이번 프레임 처음 묻는 볼륨이
 *          개체를 한 번 모아 수평(XZ) 격자에 넣고, 볼륨은 자기 사각형에 든 후보만 받는다(언리얼 Mass 프로세서가 한 번 모은 청크를 여러 질의가 나눠 읽는 것과 같은 자리).
 *
 *          - 자리: 짓기는 틱 뒤 게임 스레드(볼륨의 `updateZone` — 예전과 같은 자리라 볼륨이 보는 위치는 그대로다). `FrameSystems` 단계(`runLane`)는 낡았다고만 적는다.
 *          - 순서: 후보는 예전 순회 순서(캐릭터 컨트롤러 전부, 그다음 움직이는 강체)이고, 질의 결과도 그 순서다 — 볼륨이 대상을 넣는 순서(진입 이벤트 순서)가 같다.
 *          - 판정: 격자는 고르기만 하고, 안인지는 볼륨이 지금 자리로 잰다(`sampleAt`). 같은 프레임의 앞 볼륨 동작이 옮긴(순간 이동) 개체는 지은 자리로 걸러져 다음 프레임에 든다.
 *          - 셀: 볼륨들의 가장 큰 수평 반 크기의 두 배 — 볼륨 사각형이 셀 2×2 안팎에 든다(`InteractionSpatialIndex` 의 최대 도달 거리 셀과 같은 규칙).
 *          - 붙고 떼기: 볼륨의 `onRegister` 가 붙이고(`ensureFor`), 마지막 볼륨의 `onUnregister` 가 뗀다(`releaseIfUnused`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Spatial/SpatialHashGrid2D.h"

#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class CharacterControllerComponent;
    class GameObject;
    class GameObjectManager;
    class RigidBodyComponent;

    /** @brief 지역 볼륨 안에 들 수 있는 물리 개체 하나입니다 — 캐릭터는 발, 움직이는 강체는 중심. 한 오브젝트에 둘이 있으면 캐릭터 쪽 하나다. */
    struct ZoneOccupantCandidate
    {
        GameObject*                         _pObject{ nullptr };
        const CharacterControllerComponent* _pController{ nullptr }; ///< 캐릭터면 그 컨트롤러(발 자리 · 키), 아니면 nullptr
        const RigidBodyComponent*           _pBody{ nullptr };       ///< 캐릭터가 아니면 움직이는 강체(중심)
    };

    /**
     * @class ZoneOccupantIndex
     * @brief 매니저 하나에 하나씩 붙는 지역 후보 격자입니다. 파일 머리말 참고.
     */
    class SW_GF_API ZoneOccupantIndex final : public ISceneLaneFrameSystem
    {
    public:
        /** @brief 매니저의 프레임 시스템 키입니다. */
        static constexpr const utf8* kFrameSystemKey = "ZoneOccupantIndex";

        ZoneOccupantIndex();

        /** @brief 매니저에 붙은 격자입니다. 없으면 nullptr 입니다. */
        static ZoneOccupantIndex* find( const GameObjectManager& manager );
        /** @brief 매니저의 격자를 반환합니다. 없으면 붙입니다(게임 스레드, 틱 밖). */
        static ZoneOccupantIndex& ensureFor( GameObjectManager& manager );
        /** @brief 지역 볼륨이 하나도 등록되어 있지 않으면 격자를 뗍니다. */
        static void releaseIfUnused( GameObjectManager& manager );

        /** @brief 이번 프레임 격자를 낡았다고 적습니다. 다음 질의가 다시 짓는다. */
        void runLane( SceneJobContext& context ) override;
        /** @brief 자기 깃발 하나만 바꾼다 — 씬 데이터를 읽거나 쓰지 않는다(짓기는 틱 뒤 질의에서 한다). */
        SceneFrameAccess getFrameAccess() const override;

        /**
         * @brief 이번 프레임 후보 중 수평(XZ) 사각형 [@p minX, @p maxX] × [@p minZ, @p maxZ] 에 든 것을 후보 순서로 @p outListCandidate 에 담습니다(먼저 비운다).
         * @details 이번 프레임 처음 부르면 짓는다. 게임 스레드, 틱 밖에서만 부른다(짓기가 씬의 물리 컴포넌트를 한 번 돈다).
         */
        void queryCandidates( GameObjectManager& manager, float32 minX, float32 minZ, float32 maxX, float32 maxZ, vector<ZoneOccupantCandidate>& outListCandidate );
        /** @brief 지금까지 지은 횟수입니다 — 볼륨이 여럿이어도 프레임에 한 번인지 보는 테스트용입니다. */
        uint32 getBuildCount() const { return _buildCount; }

    private:
        /** @brief 후보를 모으고 격자에 넣습니다. */
        void rebuild( GameObjectManager& manager );

    private:
        vector<ZoneOccupantCandidate> _listCandidate;     ///< 이번 프레임 후보(예전 순회 순서)
        SpatialHashGrid2D             _grid;              ///< 후보 번호(`SlotHandle` 의 인덱스) → 지은 때의 수평 자리
        vector<SlotHandle>            _listHandleScratch; ///< 질의 결과 재사용 버퍼
        uint32                        _buildCount;
        uint8                         _bBuilt : 1; ///< 이번 프레임에 지었다
    };
} // namespace sw
