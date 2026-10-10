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

#include "GameFramework/Base/Actor/Control/Intent/ControlIntentHistory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class InputManager;
    class PlayerControllerComponent;
    class UISystem;

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
        /**
         * @brief 로컬 플레이어 @p playerIndex 의 조종자입니다. 없으면 오브젝트 하나를 세워 만듭니다(언리얼 GameMode 가 플레이어마다 PlayerController 를 세우는 자리).
         * @details 게임 스레드, 틱 밖에서 부릅니다(오브젝트를 만든다). 자동 빙의 · 자동 플레이를 끌 때 플레이어에게 폰을 돌려주는 곳이 씁니다.
         */
        static PlayerControllerComponent* findOrCreatePlayerController( GameObjectManager& manager, uint32 playerIndex );
        /** @brief 폰 · 조종자가 하나도 등록되어 있지 않으면 시스템을 뗍니다. */
        static void releaseIfUnused( GameObjectManager& manager );

        void runBeforeTick( GameObjectManager& manager, float32 deltaTime ) override;

        /** @brief 플레이어 조종자가 읽을 입력을 바꿉니다(시험 · 서버). nullptr 이면 게임 서비스의 입력 관리자입니다. */
        void setInputManager( InputManager* pInput ) { _pInputOverride = pInput; }
        /** @brief 플레이어 조종자가 읽을 입력입니다. 없을 수 있습니다(서버 · 헤드리스). */
        InputManager* findInputManager() const;
        /** @brief 플레이어 조종자가 볼 UI 를 바꿉니다(시험). nullptr 이면 게임 서비스의 UI 시스템입니다. */
        void setUISystem( const UISystem* pUISystem ) { _pUISystemOverride = pUISystem; }
        /** @brief 플레이어 조종자가 볼 UI 입니다 — 시작하지 않았으면(서버 · 시험 하네스) nullptr 입니다. */
        const UISystem* findUISystem() const;
        /** @brief 지금까지 돈 틱 수입니다(기록 · 네트워크 창의 틱 번호). */
        uint32 getTick() const { return _tick; }

        /**
         * @brief 의도 기록을 켜거나 끕니다. 켤 때 기록을 비우고 폰마다 @p capacityTicks 틱 고리를 잡습니다 — 그 뒤 틱마다 모든 폰의 의도(폰에 넣은 값)를 적는다.
         * @details 끄면 기록은 남는다(`getHistory` 로 읽어 `.swintent` 로 쓴다).
         */
        void                        setRecording( bool bRecording, int32 capacityTicks = ControlIntentHistory::kDefaultCapacityTicks );
        bool                        isRecording() const { return _bRecording == SW_TRUE; }
        const ControlIntentHistory& getHistory() const { return _history; }
        /**
         * @brief 다음 틱 첫머리에 @p controller 가 @p pawn 을 쥐게 합니다(@p pawn 이 무효면 놓게). 조종자가 의도를 내는 중(`produceIntent`)에 빙의를 옮길 때 —
         *        그 자리에서 옮기면 등록 순서에 따라 같은 틱에 두 조종자가 같은 폰을 몰거나 아무도 몰지 않는다.
         */
        void queuePossess( const ComponentHandle& controller, const ComponentHandle& pawn );

    private:
        /** @brief 자동 빙의가 걸린 채 쥔 이가 없는 폰을 플레이어 0 · AI 조종자에게 쥐어 줍니다. */
        void autoPossess( GameObjectManager& manager );
        /** @brief `queuePossess` 로 미룬 빙의를 넣은 순서대로 합니다. */
        void applyQueuedPossess( GameObjectManager& manager );
        /** @brief 틱마다 모든 폰의 의도를 기록에 넣습니다. */
        void recordIntents( GameObjectManager& manager );

        /** @struct QueuedPossess @brief 다음 틱 첫머리로 미룬 빙의 하나입니다. */
        struct QueuedPossess
        {
            ComponentHandle _controller{};
            ComponentHandle _pawn{}; ///< 무효면 놓는다
        };

        ControlIntentHistory    _history;
        vector<ComponentHandle> _listAutoPossessPawn; ///< 이번 프레임에 자동 빙의할 폰(등록부를 도는 동안 오브젝트를 만들지 않게 모은다)
        vector<QueuedPossess>   _listQueuedPossess;
        InputManager*           _pInputOverride;
        const UISystem*         _pUISystemOverride;
        uint32                  _tick;
        uint8                   _bRecording : 1;
        [[maybe_unused]] uint8  _reserved   : 7;
    };
} // namespace sw
