/**
 * @file SurfaceExposureSystem.h
 * @brief 표면 노출(`SurfaceExposureComponent` — 비 · 물이 몸 표면을 바꾼다)의 프레임 일을 한 곳에서 한 번에 하는 프레임 시스템입니다.
 * @details 노출 컴포넌트는 예전에 매 틱 자기 일을 통째로 틱 뒤 큐로 미뤘다(물 찾기 · 지붕 광선 · 외형 표면 쓰기는 게임 스레드 일이다) — 컴포넌트 수만큼 미룬 일이 쌓이고
 *          순서는 워커 순서였다. 이제 컴포넌트는 틱에서 게임 초만 쌓고, 이 시스템이 프레임마다 틱 뒤 큐에 일 **하나**를 넣어 같은 자리
 *          (`ApplyPrePhysics` 의 틱 뒤 큐 — 노출의 틱 그룹 `DuringPhysics` 가 미루던 자리)에서 등록 순서로 돈다. 지역 시스템(`ZoneSystem`)과 같은 모양이다.
 *          언리얼 `UWorldSubsystem` 이 등록받은 목록을 돌고, Mass 처리기가 한 종류를 한 번에 도는 자리다.
 *
 *          - 왜 씬 서비스가 아닌가: 씬 서비스 표(`SW_SCENE_SERVICE`)는 Engine 이미지 안에서만 채운다. GameFramework 의 타입은 프레임 시스템으로 붙고 뗀다 —
 *            첫 노출 컴포넌트의 `onRegister` 가 붙이고(`findOrCreate`), 마지막의 `onUnregister` 가 뗀다(`releaseIfUnused`).
 *          - 같은 프레임의 순서: 넣은 일은 그 프레임의 틱이 미룬 일보다 앞에 돈다(넣은 순서). 노출이 쓰는 외형 표면 상태는 외형의 다음 틱이 감쇠 · 머티리얼로 올린다 — 예전과 같다.
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
     * @class SurfaceExposureSystem
     * @brief 매니저 하나에 하나씩 붙는 표면 노출의 프레임 일입니다. 파일 머리말 참고.
     */
    class SW_GF_API SurfaceExposureSystem final : public ISceneMainFrameSystem
    {
    public:
        /** @brief 매니저의 프레임 시스템 키입니다. */
        static constexpr const utf8* kFrameSystemKey = "SurfaceExposureSystem";

        SurfaceExposureSystem();

        /** @brief 매니저에 붙은 시스템입니다. 없으면 nullptr 입니다. */
        static SurfaceExposureSystem* find( const GameObjectManager& manager );
        /** @brief 매니저의 시스템을 반환합니다. 없으면 붙입니다(게임 스레드, 틱 밖). */
        static SurfaceExposureSystem& findOrCreate( GameObjectManager& manager );
        /** @brief 노출 컴포넌트가 하나도 등록되어 있지 않으면 시스템을 뗍니다. */
        static void releaseIfUnused( GameObjectManager& manager );

        /** @brief 이번 프레임의 노출 일(`runFramePass`) 하나를 틱 뒤 큐에 넣습니다. */
        void runMain( GameObjectManager& manager, float32 deltaTime ) override;
        /** @brief 틱 뒤 큐에 일을 넣기만 한다 — 씬 데이터를 읽거나 쓰지 않는다. */
        SceneFrameAccess getFrameAccess() const override;

        /** @brief 켜진 노출 컴포넌트마다 쌓인 시간을 적용합니다(`SurfaceExposureComponent::applyExposure`, 등록 순서). 게임 스레드, 틱 밖(테스트도 부른다). */
        void runFramePass( GameObjectManager& manager );

    private:
        vector<ComponentHandle> _listHandleScratch; ///< 도는 동안의 핸들 사본(재사용 버퍼)
    };
} // namespace sw
