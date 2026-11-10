/**
 * @file VehicleSeatSystem.h
 * @brief 탈것 좌석(`VehicleSeatComponent`)의 탑승 반응(탑승자 기울임 · 카메라 킥 · 소리 파라미터)을 한 곳에서 한 번에 거는 프레임 시스템입니다.
 * @details 좌석은 예전에 앉은 쪽이 있으면 매 틱 반응을 통째로 틱 뒤 큐로 미뤘고(소리 · 카메라 디렉터는 게임 스레드 일이다), 탑승자 기울임은 틱 안에서 **남의 오브젝트**
 *          (탑승자의 소켓 바인딩 · 애니메이터)에 바로 썼다 — 탑승자 오브젝트가 다른 워커에서 틱하는 동안 쓰는 경합이었다. 이제 틱은 자기 칸(가속도 · 기울임 · 걸 값)만 쓰고
 *          깃발을 세우며, 이 시스템이 다음 프레임 `ApplyPrePhysics` 의 틱 뒤 큐 맨 앞에서 등록 순서로 건다(`SurfaceExposureSystem` 과 같은 모양).
 *          언리얼 `UWorldSubsystem` 이 등록받은 목록을 돌고, Mass 처리기가 한 종류를 한 번에 도는 자리다.
 *
 *          - 자리: 좌석은 `PostPhysics` 에서 잰다. 예전 반응은 같은 프레임 `ApplyPostPhysics` 비우기에서 돌아 카메라 시스템(그 비우기의 맨 앞)보다 늦었다 — 킥은 늘 다음 프레임
 *            카메라 계산에 들었다. 다음 프레임 `ApplyPrePhysics` 맨 앞도 그 프레임 카메라 계산(`ApplyPostPhysics`)보다 앞이라 킥 시점은 같고, 붙인 순서에 기대지 않는다.
 *            기울임은 한 프레임 늦게 든다(다음 프레임 탑승자 틱이 읽는다) — 경합을 없애는 값이다.
 *          - 왜 씬 서비스가 아닌가: 씬 서비스 표(`SW_SCENE_SERVICE`)는 Engine 이미지 안에서만 채운다. 첫 좌석의 `onRegister` 가 붙이고 마지막의 `onUnregister` 가 뗀다.
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
     * @class VehicleSeatSystem
     * @brief 매니저 하나에 하나씩 붙는 좌석 탑승 반응의 프레임 일입니다. 파일 머리말 참고.
     */
    class SW_GF_API VehicleSeatSystem final : public ISceneMainFrameSystem
    {
    public:
        /** @brief 매니저의 프레임 시스템 키입니다. */
        static constexpr const utf8* kFrameSystemKey = "VehicleSeatSystem";

        VehicleSeatSystem();

        /** @brief 매니저에 붙은 시스템입니다. 없으면 nullptr 입니다. */
        static VehicleSeatSystem* find( const GameObjectManager& manager );
        /** @brief 매니저의 시스템을 반환합니다. 없으면 붙입니다(게임 스레드, 틱 밖). */
        static VehicleSeatSystem& ensureFor( GameObjectManager& manager );
        /** @brief 좌석이 하나도 등록되어 있지 않으면 시스템을 뗍니다. */
        static void releaseIfUnused( GameObjectManager& manager );

        /** @brief 이번 프레임의 반응 일(`runFramePass`) 하나를 틱 뒤 큐에 넣습니다. */
        void runMain( GameObjectManager& manager, float32 deltaTime ) override;
        /** @brief 틱 뒤 큐에 일을 넣기만 한다 — 씬 데이터를 읽거나 쓰지 않는다. */
        SceneFrameAccess getFrameAccess() const override;

        /** @brief 켜진 좌석마다 지난 틱이 잰 반응을 겁니다(`VehicleSeatComponent::applyPendingRide`, 등록 순서). 게임 스레드, 틱 밖(테스트도 부른다). */
        void runFramePass( GameObjectManager& manager );

    private:
        vector<ComponentHandle> _listHandleScratch; ///< 도는 동안의 핸들 사본(재사용 버퍼)
    };
} // namespace sw
