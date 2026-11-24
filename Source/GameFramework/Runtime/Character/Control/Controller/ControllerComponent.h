/**
 * @file ControllerComponent.h
 * @brief 조종자(추상)를 정의합니다.
 * @details 폰 하나를 쥐고 틱마다 그 폰의 의도(`ControlIntent`)를 낸다. 플레이어, AI, 네트워크, 기록 조종자가 이것을 상속한다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ControlIntent;

    class IInputCaptureQuery;
    class InputManager;
    class PawnComponent;

    /** @brief 조종 시스템이 조종자에게 넘기는 이번 틱의 문맥입니다. */
    struct ControlFrameContext
    {
        InputManager*             _pInput{ nullptr };        ///< 플레이어 조종자가 읽는 입력. 서버와 헤드리스에서는 없을 수 있다
        const IInputCaptureQuery* _pInputCapture{ nullptr }; ///< 입력을 먼저 가져간 쪽(런타임 UI). 플레이어 조종자가 소비한 입력, 게임 입력 막기, 커서를 본다. 없을 수 있다
        float32                   _deltaTime{ 0.0f };
        uint32                    _tick{ 0 }; ///< 조종 시스템이 센 틱(기록 · 네트워크 창의 틱 번호)
    };

    /**
     * @brief 조종 프레임 N 번에 한 번 차례가 오고, 그 사이 프레임 시간을 모읍니다(AI 판단 간격 — 언리얼 `TickInterval`, 행동 트리 서비스의 간격).
     * @details 차례는 조종 시스템의 틱 번호(`ControlFrameContext::_tick`)로 정한다 — 시간이 멈춘 프레임은 틱이 오르지 않아 차례도 오지 않는다.
     *          @p phase 로 같은 간격의 조종자들을 서로 다른 프레임에 흩는다. 간격 1 은 매 프레임이다(결정성이 필요한 판은 1 을 쓴다).
     */
    struct ControlFrameInterval
    {
        float32 _elapsed{ 0.0f }; ///< 지난 차례 뒤 모은 프레임 시간

        /** @brief 이번 프레임의 시간을 모으고, 차례면 모은 시간을 @p outDeltaTime 에 내고 비운 뒤 true 입니다. */
        bool advance( const ControlFrameContext& context, uint32 interval, uint32 phase, float32& outDeltaTime )
        {
            _elapsed += context._deltaTime;
            if ( interval > 1 && ( context._tick + phase ) % interval != 0 )
                return false;
            outDeltaTime = _elapsed;
            _elapsed     = 0.0f;
            return true;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class ControllerComponent
     * @brief 폰 하나를 쥐고 틱마다 그 폰의 의도를 냅니다.
     * @details 조종자는 언리얼의 `AController` 처럼 폰과 따로 자기 오브젝트에 산다. 그래서 폰을 바꿔 타도 조종자 상태(조종 회전, 플레이어 번호)는 남는다.
     *          `possess` 와 `unpossess` 는 게임 스레드의 틱 밖에서 불러야 한다. 틱 안이면 `requestPossess` 로 틱 뒤로 미룬다.
     *          남이 쥔 폰을 쥐면 그 조종자는 놓는다. 폰 하나에 조종자 하나이다.
     */
    REFLECT( Abstract, Category = "Control", DisplayName = "Controller", Tooltip = "Possesses a pawn and produces its control intent each tick" )
    class SW_GF_API ControllerComponent : public Component
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( ControllerComponent );

        ControllerComponent();
        ~ControllerComponent() override = default;

        /** @brief 레지스트리에 등록하고, 조종 시스템이 없으면 매니저에 붙입니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 쥔 폰을 놓고 레지스트리에서 빠집니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief `_possessAtStart` 가 있으면 그 오브젝트의 폰을 쥡니다. */
        void onBeginPlay() override;

        /** @brief @p pawn 을 쥡니다. 이미 쥔 폰은 놓습니다. 빙의하는 순간 시점이 튀지 않도록 조종 회전은 폰의 지금 의도 값에서 이어 갑니다. */
        void possess( PawnComponent& pawn );
        /** @brief 틱 안에서도 부를 수 있는 `possess` 입니다. @p pawnObject 의 폰을 틱 뒤에 쥡니다. */
        void requestPossess( const GameObjectHandle& pawnObject );
        /** @brief 쥔 폰을 놓습니다. 놓인 폰의 의도는 0 이 됩니다. */
        void unpossess();
        /** @brief 쥔 폰을 반환합니다. 없거나 지워졌으면 nullptr 입니다. */
        PawnComponent*                         findPawn() const;
        const TComponentHandle<PawnComponent>& getPawn() const { return _pawn; }

        /** @brief 이 조종자의 의도가 오는 네트워크 연결입니다. 0 이면 로컬이며, 쥔 폰의 `getInputPeer` 가 이것을 따라갑니다. 탈것은 운전석 조종자의 연결이 됩니다. */
        uint32 getInputPeer() const { return _inputPeer; }

        float32 getControlYaw() const { return _controlYaw; }
        float32 getControlPitch() const { return _controlPitch; }
        void    setControlRotation( float32 yaw, float32 pitch );
        /**
         * @brief 이 조종자가 폰 하나를 위해 생성됐는지 나타냅니다. 자동 빙의 `PawnAutoPossess::AI` 로 생성된 조종자가 그렇습니다.
         * @details 그런 조종자의 오브젝트는 그 폰이 지워질 때 같이 지운다. 적이 죽어 걷힐 때마다 조종자 오브젝트가 남지 않게 하려는 것이며,
         *          언리얼에서 AI 조종자가 폰과 함께 사라지는 것과 같다.
         */
        bool isSpawnedForPawn() const { return _bSpawnedForPawn == SW_TRUE; }
        void setSpawnedForPawn( bool bSpawned ) { _bSpawnedForPawn = bSpawned ? SW_TRUE : SW_FALSE; }

        /**
         * @brief 이번 틱의 의도를 씁니다. 조종 시스템만 부릅니다.
         * @details 조종 회전 필드는 부른 뒤에 시스템이 `getControlYaw` 와 `getControlPitch` 로 덮는다. 파생 클래스는 `setControlRotation` 으로 돌려야 한다.
         */
        virtual void produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent ) = 0;

    protected:
        /**
         * @brief 서버에서 이 조종자의 의도가 연결 @p connectionID(`INetPeers` 의 연결 id, 0 부터)에서 온다고 정합니다 — 입력 피어는 연결 id + 1 이다(0 = 로컬).
         * @details 원격 조종자를 세우는 길(`RemoteControllerComponent::spawnForConnection` · `transferToConnection`)만 부른다 — 연결 없이 원격 조종자를 세울 수 없게 보호 멤버다. 서버가 "이 폰은 누구 것인가" 를
         *          묻는 정본 길(`PawnComponent::findInputConnection`)의 원천이다. 음수 id 는 오류를 남기고 거절한다.
         */
        void setInputConnection( int32 connectionID );
        /** @brief 폰을 쥔 직후에 불립니다. */
        virtual void onPossessed( PawnComponent& pawn ) { (void)pawn; }
        /** @brief 폰을 놓은 직후에 불립니다. 다른 폰으로 바꾸는 중이면 `isSwitchingPawn` 이 true 입니다. */
        virtual void onUnpossessed( PawnComponent& pawn ) { (void)pawn; }
        /** @brief `possess` 가 앞 폰을 놓는 중이면 true 입니다. 곧 새 폰의 `onPossessed` 가 불립니다. */
        bool isSwitchingPawn() const { return _bSwitchingPawn == SW_TRUE; }

    private:
        /** @brief 입력 피어를 바꾸고 쥔 폰에도 옮깁니다. `setInputConnection` 만 부른다 — 바깥은 `RemoteControllerComponent::spawnForConnection` · `transferToConnection` 으로만 연결을 정한다(연결 id + 1 해석이 한 곳). */
        void setInputPeer( uint32 inputPeer );

        PROPERTY( Category = "Control", DisplayName = "Possess At Start", Tooltip = "Object whose pawn this controller takes when play starts" )
        GameObjectHandle _possessAtStart;

        TComponentHandle<PawnComponent> _pawn; ///< 쥔 폰 컴포넌트
        float32                         _controlYaw;
        float32                         _controlPitch;
        uint32                          _inputPeer; ///< 의도가 오는 연결(0 = 로컬)
        uint8                           _bSwitchingPawn  : 1;
        uint8                           _bSpawnedForPawn : 1; ///< 자동 빙의가 폰 하나를 위해 세웠다(폰과 함께 지운다)
        [[maybe_unused]] uint8          _reserved        : 6;
    };
} // namespace sw
