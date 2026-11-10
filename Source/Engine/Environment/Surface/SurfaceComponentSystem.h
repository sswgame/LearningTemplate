/**
 * @file SurfaceComponentSystem.h
 * @brief 씬 하나의 표면 컴포넌트 프레임 일입니다 — 발 아래 표면 읽기(`GroundSurfaceComponent`), 표면 변형 도장(`SurfaceStampComponent`).
 * @details 두 컴포넌트는 예전에 매 틱 자기 일을 통째로 틱 뒤 큐로 미뤘다(물리 질의 · 씬 서비스 쓰기는 게임 스레드 일이다) — 컴포넌트 수만큼 미룬 일이 쌓이고 순서는 워커 순서였다.
 *          이제 컴포넌트는 칸과 등록(`ComponentRegistry`)만 들고, 이 서비스가 같은 자리(`ApplyPrePhysics` — PrePhysics 틱이 남긴 것을 적용한 뒤, 물리 앞)에서 타입마다 등록 순서로 한 번에 돈다.
 *          발 아래 읽기가 먼저다 — 이동 계수가 이번 물리에 든다. 언리얼 `UWorldSubsystem` 이 등록받은 목록을 돌고, Mass 처리기가 한 종류를 한 번에 도는 자리다.
 *          씬 서비스(`ISceneService`)이고 `SW_SCENE_SERVICE` 줄로 매니저마다 하나 생긴다(`manager.getSubsystem<sw::SurfaceComponentSystem>()`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class SurfaceComponentSystem
     * @brief 씬마다 하나 있는 표면 컴포넌트의 프레임 일입니다(파일 머리말 참고).
     */
    class SW_API SurfaceComponentSystem final : public ISceneService
    {
    public:
        /** @brief 이 서비스의 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "SurfaceComponentSystem";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit SurfaceComponentSystem( GameObjectManager& manager );

        SurfaceComponentSystem( const SurfaceComponentSystem& )            = delete;
        SurfaceComponentSystem& operator=( const SurfaceComponentSystem& ) = delete;

        /** @brief 켜진 표면 컴포넌트를 타입마다 등록 순서로 돕니다 — 발 아래 읽기 → 도장 → 지면 머티리얼. */
        void update( float32 deltaTime ) override;
        /** @brief 들고 있는 것이 없다 — 컴포넌트가 자기 상태를 든다. */
        void onSceneCleared() override {}

    private:
        GameObjectManager*      _pManager;
        vector<ComponentHandle> _listHandleScratch; ///< 타입 하나를 도는 동안의 핸들 사본(재사용 버퍼)
    };
} // namespace sw
