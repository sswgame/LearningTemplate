/**
 * @file ControlSystem.h
 * @brief 씬 하나의 조종자와 폰을 틱 전에 게임 스레드에서 돌려 모든 폰의 이번 틱 의도를 채우는 조종 시스템을 정의합니다.
 * @details 씬 프레임 단계 `FrameSystems` 에서 돈다.
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
     * @brief 매니저 하나에 하나씩 붙는 조종 시스템입니다. 게임 스레드에서 돕니다.
     * @details `FrameSystems` 단계에서 세 가지를 순서대로 한다.
     *          (1) 자동 빙의를 처리한다. (2) 조종자 없는 폰의 의도를 0 으로 둔다. (3) 조종자마다 의도를 만들어 폰에 넣는다.
     *          조종자와 폰 목록은 씬을 훑지 않고 레지스트리(`ComponentRegistry`)에서 읽는다. 등록 순서대로 돌므로 결정적이다.
     *          의도는 `quantize` 를 거쳐 폰에 넣으므로, 로컬 플레이어도 네트워크나 재생과 같은 값으로 움직인다.
     *          첫 폰이나 조종자가 등록될 때 `ensureFor` 가 매니저에 붙이고(매니저가 소유), 마지막 것이 빠질 때 `releaseIfUnused` 가 뗀다.
     *          GameFramework 모듈이 내려가기 전에 그 컴포넌트가 먼저 지워지므로, 매니저에 이 타입의 객체가 남지 않는다.
     *          에디터가 씬을 멈추면 `GameObjectManager::tick` 이 돌지 않으므로 이 단계도 돌지 않는다.
     */
    class SW_GF_API ControlSystem final : public ISceneFrameSystem
    {
    public:
        ControlSystem();
        ~ControlSystem() override = default;

        /** @brief @p manager 의 조종 시스템을 반환합니다. 없으면 붙입니다. */
        static ControlSystem& ensureFor( GameObjectManager& manager );
        /** @brief @p manager 의 조종 시스템을 반환합니다. 없으면 nullptr 이며, 씬을 비우는 중에도 시스템이 먼저 떨어지므로 nullptr 입니다. */
        static ControlSystem* find( const GameObjectManager& manager );
        /**
         * @brief 로컬 플레이어 @p playerIndex 의 조종자를 반환합니다. 없으면 오브젝트 하나를 세워 만듭니다. 언리얼의 GameMode 가 플레이어마다 PlayerController 를 세우는 것과 같습니다.
         * @details 오브젝트를 만들기 때문에 게임 스레드의 틱 밖에서 불러야 한다. 자동 빙의나 자동 플레이를 끌 때 플레이어에게 폰을 돌려주는 곳이 쓴다.
         */
        static PlayerControllerComponent* findOrCreatePlayerController( GameObjectManager& manager, uint32 playerIndex );
        /** @brief 폰과 조종자가 하나도 등록되어 있지 않으면 시스템을 뗍니다. */
        static void releaseIfUnused( GameObjectManager& manager );

        void runBeforeTick( GameObjectManager& manager, float32 deltaTime ) override;

        /** @brief 플레이어 조종자가 읽을 입력을 바꿉니다. 테스트와 서버가 씁니다. nullptr 이면 게임 서비스의 입력 관리자를 씁니다. */
        void setInputManager( InputManager* pInput ) { _pInputOverride = pInput; }
        /** @brief 플레이어 조종자가 읽을 입력을 반환합니다. 서버나 헤드리스에서는 없을 수 있습니다. */
        InputManager* findInputManager() const;
        /** @brief 플레이어 조종자가 볼 UI 를 바꿉니다. 테스트가 씁니다. nullptr 이면 게임 서비스의 UI 시스템을 씁니다. */
        void setUISystem( const UISystem* pUISystem ) { _pUISystemOverride = pUISystem; }
        /** @brief 플레이어 조종자가 볼 UI 를 반환합니다. 시작하지 않았으면(서버, 테스트 하네스) nullptr 입니다. */
        const UISystem* findUISystem() const;
        /** @brief 지금까지 돈 틱 수입니다. 기록과 네트워크 창의 틱 번호로 씁니다. */
        uint32 getTick() const { return _tick; }

        /**
         * @brief 의도 기록을 켜거나 끕니다. 켤 때 기록을 비우고 폰마다 @p capacityTicks 틱 크기의 고리를 잡습니다.
         * @details 켠 뒤에는 틱마다 모든 폰의 의도(폰에 넣은 값)를 적는다.
         *          끄면 기록은 남으며, `getHistory` 로 읽어 `.swintent` 로 쓸 수 있다.
         */
        void                        setRecording( bool bRecording, int32 capacityTicks = ControlIntentHistory::kDefaultCapacityTicks );
        bool                        isRecording() const { return _bRecording == SW_TRUE; }
        const ControlIntentHistory& getHistory() const { return _history; }
        /**
         * @brief 다음 틱 첫머리에 @p controller 가 @p pawn 을 쥐게 합니다. @p pawn 이 무효면 놓게 합니다.
         * @details 조종자가 의도를 내는 중(`produceIntent`)에 빙의를 옮길 때 쓴다.
         *          그 자리에서 옮기면 등록 순서에 따라 같은 틱에 두 조종자가 같은 폰을 몰거나 아무도 몰지 않게 된다.
         */
        void queuePossess( const ComponentHandle& controller, const ComponentHandle& pawn );

    private:
        /** @brief 자동 빙의가 걸린 채 쥔 이가 없는 폰을 플레이어 0 이나 AI 조종자에게 쥐어 줍니다. */
        void autoPossess( GameObjectManager& manager );
        /** @brief `queuePossess` 로 미룬 빙의를 넣은 순서대로 처리합니다. */
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
        vector<ComponentHandle> _listAutoPossessPawn; ///< 이번 프레임에 자동 빙의할 폰(레지스트리를 도는 동안 오브젝트를 만들지 않게 모은다)
        vector<QueuedPossess>   _listQueuedPossess;
        InputManager*           _pInputOverride;
        const UISystem*         _pUISystemOverride;
        uint32                  _tick;
        uint8                   _bRecording : 1;
        [[maybe_unused]] uint8  _reserved   : 7;
    };
} // namespace sw
