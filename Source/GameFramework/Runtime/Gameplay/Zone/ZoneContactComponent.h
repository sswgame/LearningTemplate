/**
 * @file ZoneContactComponent.h
 * @brief 표면 · 물리 재질의 지역 효과를 받는 개체 쪽 입구입니다 — 디딤(공통 표면 질의 — 빙판 · 늪), 부딪힘(접촉 · 충격 세기 — 가시덩굴 · 전기 울타리), 잠김(물 깊이 — 바다 · 용암 호수).
 * @details 프로필은 물리 재질(= 표면 종류, `SurfaceKindDef::_zoneProfile`)에 붙는다 — 지형 레이어 · 아무 메시든 콜라이더 재질이 프로필을 부르고, 그 메시에는 전용 컴포넌트가 없다
 *          (언리얼 Physical Material · 유니티 PhysicMaterial 자리, 기억). 이 컴포넌트는 받는 쪽(캐릭터 · NPC · 탈것 · 상자)에 두고, 종류마다 `ZoneTracker` 하나로 볼륨과 같은 처리 길을 지난다.
 *          출처는 `surface:<종류>` 이다. 위치 값: `zoneDepth01` 는 1(표면에 가장자리가 없다), `immersion` 은 물 깊이 ÷ 키, `impactSpeed` 는 부딪힌 속도, `contactNormal` 은 접촉 법선의 위쪽 성분.
 *          캐릭터는 발 아래 질의(같은 오브젝트의 `GroundSurfaceComponent` 가 있으면 그 표본)와 허리 높이 수평 네 방향 광선으로, 강체는 발 아래 질의와 접촉 사건(`onCollisionBegin` · 올린 유지 `ScenePhysics::registerContactStayListener`)으로 안다.
 *          이펙트의 이동 제약(`MovementConstraint` — 감속 · 속박)을 같은 오브젝트의 캐릭터 컨트롤러 이동 계수로 넘긴다 — 폰 이동기가 없는 캐릭터(시연의 걷는 해골 · 스크립트 NPC)도 같은 계약이다.
 *          갱신은 지역 프레임 시스템(`ZoneSystem`)이 틱 뒤 게임 스레드에서 한 번에 한다 — 컴포넌트는 칸 · 접촉 기록과 등록만 든다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneModule.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneTracker.h"

namespace sw
{
    class GameObjectManager;
    struct SurfaceKindDef;
    struct SurfaceKindTable;

    /**
     * @class ZoneContactComponent
     * @brief 표면 · 재질 지역 효과의 받는 쪽 입구입니다(파일 머리말 참고).
     */
    REFLECT( Category = "Gameplay", DisplayName = "Zone Contact", Tooltip = "Feeds the surfaces this object stands on, touches or is submerged in to their zone profiles (ice, thorns, sea, lava)" )
    class SW_GF_API ZoneContactComponent : public Component
    {
    public:
        REFLECT_BODY();

        /** @brief 표면 종류 하나의 지역입니다(그 종류의 프로필을 이 개체에 대해 컴파일한 것과 처리 기록). */
        struct SurfaceZone
        {
            hashed_string       _kind{};
            ZoneCompiledProfile _compiled{};
            ZoneTracker         _tracker{};
            uint8               _bValid{ SW_FALSE }; ///< 프로필을 읽고 컴파일했다(틀렸으면 다시 시도하지 않는다 — 오류는 한 번)
            uint8               _arrReserved[3]{};
        };

        ZoneContactComponent();
        ~ZoneContactComponent() override = default;

        /** @brief 씬의 컴포넌트 레지스트리에 오르고 지역 프레임 시스템(`ZoneSystem`)을 붙이고, 접촉 유지를 듣게 올립니다(유지는 opt-in — 감사 S-1). */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레지스트리에서 내리고, 마지막 지역 컴포넌트면 시스템을 뗍니다. */
        void onUnregister( GameObjectManager& manager ) override;
        void onEndPlay() override;
        void onCollisionBegin( const CollisionInfo& collision ) override;

        /** @brief 이번 프레임을 처리합니다(`ZoneSystem` 이 틱 뒤 게임 스레드에서 부른다, 테스트도 부른다). */
        void updateContacts( GameObjectManager& manager, float32 deltaTime );
        /** @brief 지금까지 만든 표면 지역입니다(감시 값 · 진단). */
        const vector<unique_ptr<SurfaceZone>>& getSurfaceZones() const { return _listSurfaceZone; }

    private:
        /** @brief 이번 프레임에 닿은 표면 하나입니다. */
        struct ContactRecord
        {
            hashed_string    _kind{};
            GameObjectHandle _object{}; ///< 표면의 오브젝트(알면)
            float32          _immersion{ 0.0f };
            float32          _impactSpeed{ 0.0f };
            float32          _normalUp{ 0.0f };
        };

        /** @brief 종류 @p kind 의 접촉을 @p inoutList 에 합칩니다(같은 종류면 큰 값). 프로필이 없는 종류는 넣지 않는다. */
        static void addContact( vector<ContactRecord>& inoutList, const SurfaceKindTable& table, const ContactRecord& contact );
        /** @brief 종류의 지역을 찾거나 처음이면 프로필을 읽어 컴파일합니다. */
        SurfaceZone& findOrAddSurfaceZone( const SurfaceKindTable& table, const hashed_string& kind );
        /** @brief 올린 접촉 유지(`ContactStayListener`)를 받습니다 — 시작과 같이 접촉으로 적는다. */
        static void receiveCollisionStay( Component& self, const CollisionInfo& collision );
        /** @brief 강체 접촉 하나를 다음 갱신에 넘길 접촉으로 적습니다. */
        void recordCollision( const CollisionInfo& collision );
        /** @brief 이 개체의 키(m)입니다 — 캐릭터는 캡슐, 아니면 `_bodyHeight`. */
        float32 computeBodyHeight() const;

    private:
        PROPERTY( Category = "Contact", DisplayName = "Probe Length", Min = 0.05, Units = m, Tooltip = "How far below the feet the ground is looked for (when there is no GroundSurfaceComponent)" )
        float32 _probeLength;
        PROPERTY( Category = "Contact", DisplayName = "Body Height", Min = 0.05, Units = m, Tooltip = "Height that turns water depth into immersion for a body without a character capsule" )
        float32 _bodyHeight;
        PROPERTY( Category = "Contact", DisplayName = "Touch Distance", Min = 0.0, Units = m, Tooltip = "How far past a character's capsule a wall counts as touched (thorns, electric fences)" )
        float32 _touchDistance;

        vector<unique_ptr<SurfaceZone>> _listSurfaceZone; ///< 종류마다(처음 닿을 때 만든다). 기록이 처리기 기억을 가리키므로 자리가 움직이지 않게 힙에 둔다
        vector<ContactRecord>           _listCollision;   ///< 지난 갱신 뒤 강체 접촉 사건이 적은 것
    };
} // namespace sw
