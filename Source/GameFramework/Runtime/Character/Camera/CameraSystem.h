/**
 * @file CameraSystem.h
 * @brief 카메라 디렉터(`CameraDirectorComponent`)와 카메라 매니저(`CameraManagerComponent`)가 틱 안에서 미룬 포즈 계산을 한 곳에서 정한 순서로 푸는 프레임 시스템입니다.
 * @details 두 컴포넌트는 예전에 매 틱 계산을 통째로 틱 뒤 큐로 미뤘다(암 충돌은 물리를 쓸어 보는 게임 스레드 일이다). "디렉터가 타깃 카메라를 움직인 다음 매니저가 읽는다" 는
 *          순서가 큐에 넣은 순서(디렉터 `PostPhysics` → 매니저 `PostUpdate`)에 기대고 있었고, 다른 늦은 틱이 디렉터에 거는 흔들림(좌석 승차감)은 워커 순서에 따라
 *          이번 프레임이나 다음 프레임에 들었다. 이제 틱은 시간을 모으고 깃발(`isResolvePending`)만 세우고, 이 시스템이 한 자리에서 **디렉터 → 매니저** 를 등록 순서로 푼다.
 *          언리얼 `PlayerCameraManager::UpdateCamera` 가 틱 그룹 끝에서 뷰 타깃을 한 번 푸는 자리, 시네머신 Brain 이 LateUpdate 에서 가상 카메라를 한 번에 푸는 자리다.
 *
 *          - 자리: 늦은 틱(`PostPhysics` · `PostUpdate`)이 끝나고 `ApplyPostPhysics` 가 그 틱의 구조 변경 · 트랜스폼 쓰기를 적용한 뒤, 그 틱이 미룬 일보다 **앞**(틱 뒤 큐의 맨 앞).
 *            예전 계산이 보던 것(이번 프레임 물리 · 늦은 틱이 옮긴 타깃 · 시퀀서 값)을 그대로 본다. 늦은 틱이 미룬 일이 디렉터에 거는 것(흔들림 · 킥)은 다음 프레임 계산에 든다 — 늘 같다.
 *          - 거기 닿는 길: 프레임 시스템은 `FrameSystems` 단계(틱 앞)에서 돈다. `runMain` 이 틱 뒤 큐에 일을 넣고(`ApplyPrePhysics` 에서 돈다), 그 일이 다시 넣은 일은
 *            다음 비우기 — `ApplyPostPhysics` 의 맨 앞 — 로 간다(`DeferredDelegateQueue` 는 비우는 중에 넣은 일을 다음 비우기로 넘긴다는 약속). 그 사이 비우기는 없다.
 *          - 틱 밖에서 부른 `updateCamera`(시작 · 프리셋 바꾸기 · 상태 복원)는 예전처럼 바로 푼다 — 깃발은 틱 안에서만 선다.
 *          - 왜 씬 서비스가 아닌가: 씬 서비스 표(`SW_SCENE_SERVICE`)는 Engine 이미지 안에서만 채운다. 첫 카메라 컴포넌트의 `onRegister` 가 붙이고 마지막의 `onUnregister` 가 뗀다.
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
     * @class CameraSystem
     * @brief 매니저 하나에 하나씩 붙는 카메라 포즈 계산의 자리입니다. 파일 머리말 참고.
     */
    class SW_GF_API CameraSystem final : public ISceneMainFrameSystem
    {
    public:
        /** @brief 매니저의 프레임 시스템 키입니다. */
        static constexpr const utf8* kFrameSystemKey = "CameraSystem";

        CameraSystem();

        /** @brief 매니저에 붙은 시스템입니다. 없으면 nullptr 입니다. */
        static CameraSystem* find( const GameObjectManager& manager );
        /** @brief 매니저의 시스템을 반환합니다. 없으면 붙입니다(게임 스레드, 틱 밖). */
        static CameraSystem& findOrCreate( GameObjectManager& manager );
        /** @brief 디렉터 · 매니저가 하나도 등록되어 있지 않으면 시스템을 뗍니다. */
        static void releaseIfUnused( GameObjectManager& manager );

        /** @brief 이번 프레임의 카메라 일(`runFramePass`)을 늦은 틱 뒤 큐의 맨 앞에 닿게 넣습니다(파일 머리말의 "거기 닿는 길"). */
        void runMain( GameObjectManager& manager, float32 deltaTime ) override;
        /** @brief 틱 뒤 큐에 일을 넣기만 한다 — 씬 데이터를 읽거나 쓰지 않는다. */
        SceneFrameAccess getFrameAccess() const override;

        /** @brief 틱 안에서 미룬 계산을 디렉터 → 매니저 순서로(타입 안은 등록 순서) 풉니다. 게임 스레드, 틱 밖(테스트도 부른다). */
        void runFramePass( GameObjectManager& manager );

    private:
        vector<ComponentHandle> _listHandleScratch; ///< 타입 하나를 도는 동안의 핸들 사본(재사용 버퍼)
    };
} // namespace sw
