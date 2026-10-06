/**
 * @file ControlSystem.h
 * @brief 조종 시스템 — 씬 하나의 조종자 · 폰을 틱 전에 게임 스레드에서 돌려 모든 폰의 이번 틱 의도를 채웁니다(씬 프레임 단계 `FrameSystems`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"

#include "Engine/Object/GameObject/ISceneFrameSystem.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class InputManager;

    /**
     * @class ControlSystem
     * @brief 매니저 하나에 하나 — `FrameSystems` 단계에서 (1) 자동 빙의 (2) 조종자 없는 폰의 의도 0 (3) 조종자마다 의도 → 폰 을 합니다. 게임 스레드.
     * @details 조종자 · 폰 목록은 씬을 훑지 않고 등록부(`ComponentRegistry`)에서 읽습니다(등록 순서 — 결정적). 의도는 `quantize` 를 거쳐 폰에 넣습니다 —
     *          로컬 플레이어도 네트워크 · 재생과 같은 값으로 움직인다.
     *          첫 폰 · 조종자가 등록될 때 `ensureFor` 가 매니저에 붙이고(매니저가 소유), 마지막 것이 빠질 때 `releaseIfUnused` 가 뗍니다 —
     *          GameFramework 모듈이 내려가기 전에 그 컴포넌트가 먼저 지워지므로 매니저에 이 타입의 객체가 남지 않는다.
     *          에디터가 씬을 멈추면 `GameObjectManager::tick` 이 돌지 않아 이 단계도 돌지 않는다.
     */
    class SW_GF_API ControlSystem final : public ISceneFrameSystem
    {
    public:
        ControlSystem();
        ~ControlSystem() override = default;

        /** @brief @p manager 의 조종 시스템입니다(없으면 붙입니다). */
        static ControlSystem& ensureFor( GameObjectManager& manager );
        /** @brief @p manager 의 조종 시스템입니다. 없으면 nullptr 입니다(씬을 비우는 중에도 nullptr — 시스템이 먼저 떨어진다). */
        static ControlSystem* find( const GameObjectManager& manager );
        /** @brief 폰 · 조종자가 하나도 등록되어 있지 않으면 시스템을 뗍니다. */
        static void releaseIfUnused( GameObjectManager& manager );

        void runBeforeTick( GameObjectManager& manager, float32 deltaTime ) override;

        /** @brief 플레이어 조종자가 읽을 입력을 바꿉니다(시험 · 서버). nullptr 이면 게임 서비스의 입력 관리자입니다. */
        void setInputManager( InputManager* pInput ) { _pInputOverride = pInput; }
        /** @brief 플레이어 조종자가 읽을 입력입니다. 없을 수 있습니다(서버 · 헤드리스). */
        InputManager* findInputManager() const;
        /** @brief 지금까지 돈 틱 수입니다(기록 · 네트워크 창의 틱 번호). */
        uint32 getTick() const { return _tick; }

    private:
        /** @brief 자동 빙의가 걸린 채 쥔 이가 없는 폰을 플레이어 0 · AI 조종자에게 쥐어 줍니다. */
        void autoPossess( GameObjectManager& manager );

        vector<ComponentHandle> _listAutoPossessPawn; ///< 이번 프레임에 자동 빙의할 폰(등록부를 도는 동안 오브젝트를 만들지 않게 모은다)
        InputManager*           _pInputOverride;
        uint32                  _tick;
    };
} // namespace sw
