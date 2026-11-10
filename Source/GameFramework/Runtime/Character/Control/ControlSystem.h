/**
 * @file ControlSystem.h
 * @brief 씬 하나의 조종자와 폰을 틱 전에 게임 스레드에서 돌려 모든 폰의 이번 틱 의도를 채우는 조종 시스템을 정의합니다.
 * @details 씬 프레임 단계 `FrameSystems` 에서 돈다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntentHistory.h"

namespace sw
{
    class ControllerComponent;
    class GameObjectManager;
    class IInputCaptureQuery;
    class InputManager;
    class PawnComponent;
    class PlayerControllerComponent;

    /**
     * @class ControlSystem
     * @brief 매니저 하나에 하나씩 붙는 조종 시스템입니다. 게임 스레드에서 돕니다.
     * @details `FrameSystems` 단계에서 세 가지를 순서대로 한다.
     *          (1) 자동 빙의를 처리한다. (2) 조종자 없는 폰의 의도를 0 으로 둔다. (3) 조종자마다 의도를 만들어 폰에 넣는다.
     *          조종자와 폰 목록은 씬을 훑지 않고 레지스트리(`ComponentRegistry`)에서 읽는다. 등록 순서대로 돌므로 결정적이다.
     *          의도는 `quantize` 를 거쳐 폰에 넣으므로, 로컬 플레이어도 네트워크나 재생과 같은 값으로 움직인다.
     *          첫 폰이나 조종자가 등록될 때 `ensureFor` 가 매니저에 붙이고(매니저가 소유), 마지막 것이 빠질 때 `releaseIfUnused` 가 뗀다.
     *          GameFramework 모듈이 내려가기 전에 그 컴포넌트가 먼저 지워지므로, 매니저에 이 타입의 객체가 남지 않는다.
     *          에디터가 씬을 멈추면 `GameObjectManager::update` 이 돌지 않으므로 이 단계도 돌지 않는다.
     */
    class SW_GF_API ControlSystem final : public ISceneMainFrameSystem
    {
    public:
        ControlSystem();
        ~ControlSystem() override = default;

        /** @brief @p manager 의 조종 시스템을 반환합니다. 없으면 붙입니다. */
        static ControlSystem& ensureFor( GameObjectManager& manager );
        /** @brief @p manager 의 조종 시스템을 반환합니다. 없으면 nullptr 이며, 씬을 비우는 중에도 시스템이 먼저 떨어지므로 nullptr 입니다. */
        static ControlSystem* find( const GameObjectManager& manager );
        /**
         * @brief 로컬 플레이어 @p playerIndex 의 조종자를 반환합니다. 없으면 오브젝트 하나를 생성해 만듭니다. 언리얼의 GameMode 가 플레이어마다 PlayerController 를 생성하는 것과 같습니다.
         * @details 오브젝트를 만들기 때문에 게임 스레드의 틱 밖에서 불러야 한다. 자동 빙의나 자동 플레이를 끌 때 플레이어에게 폰을 돌려주는 곳이 쓴다.
         */
        static PlayerControllerComponent* findOrCreatePlayerController( GameObjectManager& manager, uint32 playerIndex );
        /** @brief @p pawn 을 지금 @p controllerObject 위의 조종자가 쥐고 있으면 true 입니다. 디렉터가 자동 플레이 AI 가 폰을 가졌는지 묻는다. */
        static bool isPawnControlledBy( const GameObjectManager& manager, const PawnComponent& pawn, GameObjectHandle controllerObject );
        /**
         * @brief 자동 플레이를 끌 때 폰을 플레이어 0 의 조종자에게 돌려줍니다. 폰이 빈 채이거나 @p autoPlayObject 의 조종자가 쥐었을 때만 옮기며 그때 true 입니다.
         * @details 다른 조종자(컷신 등)가 쥔 폰은 그대로 둔다. 게임 스레드의 틱 밖에서 부른다(조종자를 만들 수 있다). 언리얼 GameMode 의 `SwapPlayerControllers` 자리 —
         *          디렉터 넷이 같은 되돌리기를 저마다 적던 것을 한 곳으로 모았다.
         */
        static bool returnPawnToPlayer( GameObjectManager& manager, PawnComponent& pawn, GameObjectHandle autoPlayObject );
        /** @brief 폰과 조종자가 하나도 등록되어 있지 않으면 시스템을 뗍니다. */
        static void releaseIfUnused( GameObjectManager& manager );

        void runMain( GameObjectManager& manager, float32 deltaTime ) override;
        /**
         * @brief 조종자의 판단이 하는 일입니다. 폰의 의도 · 시선 · 빙의를 쓰고(`ControlIntent`), 자동 빙의가 조종자 오브젝트를 만들며(`Structure`),
         *        AI 가 내비게이션 에이전트에 목적지를 건네고 멈추게 합니다(`Navigation`). 폰 자리(AI 의 이동 방향)와 에이전트의 이동 상태는 지난 프레임의 값을 읽습니다.
         *        AI 의 `think` 는 게임이 채우는 훅이라 읽는 것이 더 있을 수 있으나, 쓰기는 이 선언 밖에서 하지 않는 약속입니다.
         */
        SceneFrameAccess getFrameAccess() const override;

        /** @brief 플레이어 조종자가 읽을 입력을 바꿉니다. 테스트와 서버가 씁니다. nullptr 이면 게임 서비스의 입력 관리자를 씁니다. */
        void setInputManager( InputManager* pInput ) { _pInputOverride = pInput; }
        /** @brief 플레이어 조종자가 읽을 입력을 반환합니다. 서버나 헤드리스에서는 없을 수 있습니다. */
        InputManager* findInputManager() const;
        /** @brief 플레이어 조종자가 볼 입력 포획 질의를 바꿉니다. 테스트가 씁니다. nullptr 이면 게임 서비스의 질의를 씁니다. */
        void setInputCapture( const IInputCaptureQuery* pCapture ) { _pInputCaptureOverride = pCapture; }
        /** @brief 플레이어 조종자가 볼 입력 포획 질의를 반환합니다. 없을 수 있습니다(테스트 하네스). */
        const IInputCaptureQuery* findInputCapture() const;
        /** @brief 지금까지 돈 틱 수입니다. 기록과 네트워크 창의 틱 번호로 씁니다. */
        uint32 getTick() const { return _tick; }
        /** @brief 틱 번호를 정합니다. 되감기(`ControlSceneRewindWorld`)가 키 스냅샷으로 되돌린 뒤 키를 찍은 틱으로 맞출 때만 씁니다. */
        void setTick( uint32 tick ) { _tick = tick; }

        /**
         * @brief 이 씬의 서버 틱을 알립니다 — 넷 시뮬레이션 틱을 가진 컴포넌트가 자기 틱에서 부릅니다. 차이(`서버 틱 − 조종 틱`)만 듭니다.
         * @details 틱 함수 바닥(`TickedPathMoverComponent`)이 이 값으로 자리를 낸다. 씬 값 하나라 쓰는 쪽은 바닥을 모르고 바닥은 쓰는 쪽을 모른다 — 예전에는
         *          쓰는 쪽이 씬의 바닥을 모두 돌며 칸마다 써 넣었다(틱 안 씬 순회, 바닥 틱과 겹치는 쓰기). 병렬 틱에서 쓰므로 원자값이고, 같은 프레임에 바닥이
         *          이 값을 읽게 하는 순서는 `setServerTickSource` 의 선행 조건이 맡는다. 서버는 `넷 틱`, 클라이언트는 `예측하는 입력 틱 + 서버 차이` 를 넣는다.
         */
        void publishServerTick( int64 serverTick ) { _serverTickOffset.store( serverTick - static_cast<int64>( _tick ), std::memory_order_release ); }
        /** @brief 지금 조종 틱에 맞춘 서버 틱입니다. 아무도 알리지 않았으면 조종 틱과 같습니다(차이 0). 0 아래로 내려가지 않습니다. */
        uint32 getServerTick() const
        {
            const int64 serverTick = static_cast<int64>( _tick ) + _serverTickOffset.load( std::memory_order_acquire );
            return serverTick > 0 ? static_cast<uint32>( serverTick ) : 0u;
        }
        /**
         * @brief 서버 틱을 알리는 틱(`publishServerTick` 을 부르는 컴포넌트의 주 틱)입니다. 서버 틱을 읽는 서브틱이 이것을 선행 조건으로 걸어 같은 프레임의 값을 읽는다.
         * @details 시작 · 끝(게임 스레드, 틱 밖)에서만 바꾼다 — 읽는 틱과 겹치지 않는다. 언리얼 `AddTickPrerequisiteComponent` 의 대상을 씬이 하나 알려 주는 자리다.
         */
        void                 setServerTickSource( const SubTickHandle& source ) { _serverTickSource = source; }
        const SubTickHandle& getServerTickSource() const { return _serverTickSource; }

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
         *          요청 즉시 옮기면 등록 순서에 따라 같은 틱에 두 조종자가 같은 폰을 몰거나 아무도 몰지 않게 된다.
         */
        void queuePossess( const TComponentHandle<ControllerComponent>& controller, const TComponentHandle<PawnComponent>& pawn );

    private:
        /** @struct QueuedPossess @brief 다음 틱 첫머리로 미룬 빙의 하나입니다. */
        struct QueuedPossess
        {
            TComponentHandle<ControllerComponent> _controller{};
            TComponentHandle<PawnComponent>       _pawn{}; ///< 무효면 놓는다
        };
        /** @brief 자동 빙의가 설정됐지만 빙의한 조종자가 없는 폰을 플레이어 0 이나 AI 조종자에게 빙의시킵니다. */
        void autoPossess( GameObjectManager& manager );
        /** @brief `queuePossess` 로 미룬 빙의를 넣은 순서대로 처리합니다. */
        void applyQueuedPossess( GameObjectManager& manager );
        /** @brief 틱마다 모든 폰의 의도를 기록에 넣습니다. */
        void recordIntents( GameObjectManager& manager );

    private:
        ControlIntentHistory                    _history;
        vector<TComponentHandle<PawnComponent>> _listAutoPossessPawn; ///< 이번 프레임에 자동 빙의할 폰(레지스트리를 도는 동안 오브젝트를 만들지 않게 모은다)
        vector<QueuedPossess>                   _listQueuedPossess;
        InputManager*                           _pInputOverride;
        const IInputCaptureQuery*               _pInputCaptureOverride;
        SubTickHandle                           _serverTickSource; ///< 서버 틱을 알리는 틱(없으면 빈 핸들)
        atomic<int64>                           _serverTickOffset; ///< 서버 틱 − 조종 틱(`publishServerTick`)
        uint32                                  _tick;
        uint8                                   _bRecording : 1;
        [[maybe_unused]] uint8                  _reserved   : 7;
    };
} // namespace sw
