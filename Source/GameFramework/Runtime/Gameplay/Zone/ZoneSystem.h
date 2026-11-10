/**
 * @file ZoneSystem.h
 * @brief 지역 컴포넌트(`ZoneVolumeComponent` · `ZoneContactComponent` · `JumpEscapeReactionComponent` · `ScreenReactionComponent`)의 프레임 일을 한 곳에서 한 번에 하는 프레임 시스템입니다.
 * @details 세 컴포넌트는 예전에 매 틱 자기 일을 통째로 틱 뒤 큐로 미뤘다(물리 질의 · 이펙트 적용 · 오브젝트 지우기는 게임 스레드 일이다) — 컴포넌트 수만큼 미룬 일이 쌓이고,
 *          순서는 워커 순서였다. 이제 컴포넌트는 칸과 등록(`ComponentRegistry`)만 들고, 이 시스템이 프레임마다 틱 뒤 큐에 일 **하나**를 넣어 같은 자리
 *          (`ApplyPrePhysics` 의 틱 뒤 큐 — 틱이 남긴 구조 변경 · 트랜스폼 쓰기를 적용한 뒤, 물리 앞)에서 타입마다 등록 순서로 돈다: 볼륨 → 접촉 → 점프 탈출 → 화면.
 *          언리얼 `UWorldSubsystem` 이 등록받은 목록을 돌고, Mass 처리기가 한 종류를 한 번에 도는 자리다.
 *
 *          - 왜 씬 서비스가 아닌가: 씬 서비스 표(`SW_SCENE_SERVICE`)는 Engine 이미지 안에서만 채운다(매니저가 만들 때 표를 읽는다). GameFramework 의 타입은 프레임 시스템으로
 *            붙고 떼는 길(`ZoneOccupantIndex` 와 같다)을 쓴다 — 첫 지역 컴포넌트의 `onRegister` 가 붙이고(`ensureFor`), 마지막의 `onUnregister` 가 뗀다(`releaseIfUnused`).
 *          - 왜 틱 뒤 큐인가: `FrameSystems` 단계는 틱 앞이다. 예전과 같은 자리(틱 뒤, 물리 앞)에서 돌아야 볼륨이 보는 자리 · 이펙트가 드는 프레임이 그대로다.
 *            `runMain` 은 일을 넣기만 하므로 씬 데이터를 건드리지 않는다고 선언한다. 넣은 일은 그 프레임의 틱이 미룬 일보다 앞에 돈다(넣은 순서).
 *          - 발 아래 표면: 접촉은 같은 오브젝트의 `GroundSurfaceComponent` 를 먼저 읽게 한다(`sampleGroundOnce`) — 표면 서비스(`SurfaceComponentSystem`)는 같은 단계 끝에 돌고
 *            그 프레임에 다시 읽지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class ZoneSystem
     * @brief 매니저 하나에 하나씩 붙는 지역 컴포넌트의 프레임 일입니다. 파일 머리말 참고.
     */
    class SW_GF_API ZoneSystem final : public ISceneMainFrameSystem
    {
    public:
        /** @brief 매니저의 프레임 시스템 키입니다. */
        static constexpr const utf8* kFrameSystemKey = "ZoneSystem";

        ZoneSystem();

        /** @brief 매니저에 붙은 시스템입니다. 없으면 nullptr 입니다. */
        static ZoneSystem* find( const GameObjectManager& manager );
        /** @brief 매니저의 시스템을 반환합니다. 없으면 붙입니다(게임 스레드, 틱 밖). */
        static ZoneSystem& ensureFor( GameObjectManager& manager );
        /** @brief 지역 컴포넌트가 하나도 등록되어 있지 않으면 시스템을 뗍니다. */
        static void releaseIfUnused( GameObjectManager& manager );

        /** @brief 이번 프레임의 지역 일(`runFramePass`) 하나를 틱 뒤 큐에 넣습니다. */
        void runMain( GameObjectManager& manager, float32 deltaTime ) override;
        /** @brief 틱 뒤 큐에 일을 넣기만 한다 — 씬 데이터를 읽거나 쓰지 않는다. */
        SceneFrameAccess getFrameAccess() const override;

        /**
         * @brief 이번 프레임의 지역 일을 등록 순서로 합니다 — 볼륨(`updateZone`) → 접촉(`updateContacts`) → 점프 탈출(`updateJumpEscape`) → 화면(`updateScreen`). 게임 스레드, 틱 밖(테스트도 부른다).
         * @details 볼륨과 접촉은 흐른 시간이 없으면(일시정지) 돌지 않는다 — 예전 틱이 `deltaTime <= 0` 에서 미루지 않던 규칙이다. 점프 탈출 · 화면은 시간과 무관하게 돈다(화면은 흐른 시간만큼 가중치를 옮긴다).
         */
        void runFramePass( GameObjectManager& manager, float32 deltaTime );

    private:
        vector<ComponentHandle> _listHandleScratch; ///< 타입 하나를 도는 동안의 핸들 사본(재사용 버퍼)
    };
} // namespace sw
