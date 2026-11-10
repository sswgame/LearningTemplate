/**
 * @file ZoneOccupantIndex.h
 * @brief 지역 볼륨(`ZoneVolumeComponent`)이 "안에 들 수 있는 물리 개체" 를 씬 공간 색인(`SceneSpatialGrid`)에서 고르는 읽기 창구입니다.
 * @details 예전에는 이 색인이 프레임마다 씬의 캐릭터 컨트롤러 · 강체를 모두 돌아 자기 격자를 지었다. 이제 3D 캐릭터 · 강체가 등록 동안 엔진 색인의 층
 *          (`PhysicsComponent::getCharacterSpatialLayer` · `getBodySpatialLayer`)에 오르고, 여기는 그 층을 읽기만 한다(언리얼 Mass 가 엔진이 맞춘 청크를 여러 질의가 나눠 읽는 자리).
 *
 *          - 순서: 후보는 캐릭터 전부, 그다음 움직이는 강체이고, 층 안은 오른 순서다 — 볼륨이 대상을 넣는 순서(진입 이벤트 순서)가 실행마다 같다.
 *          - 판정: 격자는 고르기만 하고, 안인지는 볼륨이 지금 자리로 잰다(`sampleAt`). 격자의 자리는 지난 `SpatialIndex` 단계의 것이라 그 뒤 옮긴(순간 이동 · 이번 틱) 개체는 다음 프레임에 든다.
 *          - 거르기: 움직이는 강체인지 · 캐릭터와 한 오브젝트인지는 질의할 때 지금 상태로 본다(바디 종류가 바뀌어도 항목은 그대로다).
 *          - 셀: 볼륨의 수평 반 크기(퍼짐 제외)의 두 배 이상 — 볼륨 사각형이 셀 2×2 안팎에 든다. 셀은 커지기만 한다(`SceneSpatialGrid::requireCellSize`).
 *          - 사용자: 볼륨의 `onRegister` · `onUnregister` 가 두 층의 사용자를 센다. 볼륨이 없으면 층을 맞추지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class CharacterControllerComponent;
    class Component;
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
     * @brief 지역 후보를 씬 공간 색인에서 고르는 함수 모음입니다. 상태가 없습니다(파일 머리말 참고).
     */
    class SW_GF_API ZoneOccupantIndex final
    {
    public:
        ZoneOccupantIndex() = delete;

        /** @brief 볼륨 하나를 두 층의 사용자로 셉니다(게임 스레드, 틱 밖). */
        static void addLayerReader( GameObjectManager& manager );
        /** @brief 볼륨 하나를 뺍니다. */
        static void removeLayerReader( GameObjectManager& manager );

        /**
         * @brief 수평(XZ) 사각형 [@p minX, @p maxX] × [@p minZ, @p maxZ] 에 걸린 후보를 후보 순서로 @p outListCandidate 에 담습니다(먼저 비운다).
         * @param cellSize 이 볼륨이 바라는 셀 크기(수평 반 크기의 두 배). 층의 셀이 이보다 작으면 키운다
         * @param inoutListKey 색인 질의 결과를 받는 버퍼(부르는 쪽이 들고 재사용한다)
         * @details 게임 스레드, 틱 밖에서만 부른다(셀을 키우면 색인을 고친다).
         */
        static void queryCandidates( GameObjectManager& manager, float32 cellSize, float32 minX, float32 minZ, float32 maxX, float32 maxZ, vector<Component*>& inoutListKey,
                                     vector<ZoneOccupantCandidate>& outListCandidate );
    };
} // namespace sw
