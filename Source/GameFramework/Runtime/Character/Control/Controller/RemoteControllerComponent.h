/**
 * @file RemoteControllerComponent.h
 * @brief 원격 조종자입니다. 네트워크로 받은 의도를 냅니다. 의도는 네트워크 키트가 틱마다 넣어 주는 값(`receiveIntent`)이고 조종자가 사본을 소유합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Replication/TickRingBuffer.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Control/Controller/ControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"

namespace sw
{
    /**
     * @class RemoteControllerComponent
     * @brief 서버(또는 롤백 피어)의 폰을 상대 연결의 의도로 몹니다. 조종 시스템 틱마다 받아 둔 상대 틱을 모두 읽어 하나의 의도로 냅니다.
     * @details - 결정: 받은 의도는 **조종자가 사본으로 소유**합니다(틱 링, `kReceiveCapacity`). 예전에는 네트워크 키트의 받는 버퍼를 빌려 읽었는데,
     *            서버의 버퍼는 연결 목록(`vector`) 원소라 연결이 늘면 주소가 바뀌어 빌린 포인터가 매달렸다. 이제 키트는 틱마다 꺼낸 의도
     *            (`ReplicationServer::popIntent`, 입력 창 바이트를 `ControlIntent::read`)를 `receiveIntent` 로 넣기만 한다.
     *          - 상대 틱(네트 고정 틱)과 조종 시스템 틱(프레임)은 걸음이 다르다. 프레임에 새 틱이 없으면 마지막 의도를 되풀이하고(발동 칸은 지운 채 — 한 번 누름이 두 번 되지 않게),
     *            여러 틱이 쌓였으면 마지막 틱의 값에 그 사이의 발동 비트를 모아 낸다(발동이 사라지지 않게). 읽을 범위 안에 빠진 틱만 "못 받은 틱" 으로 센다.
     *          - 네트워크 형식은 의도 하나(`ControlIntent::write`, 양자화)라서 로컬 조종자가 낸 값(조종 시스템이 `quantize` 한 값)과 같은 값으로 움직인다.
     *          언리얼 서버의 `ServerMove` 가 받은 `FSavedMove` 로 폰을 움직이는 것, 유니티 Netcode 에서 서버가 `ICommandData` 를 읽는 것에 해당합니다.
     */
    REFLECT( Category = "Control", DisplayName = "Remote Controller", Tooltip = "Drives the pawn with control intents received over the network" )
    class SW_GF_API RemoteControllerComponent : public ControllerComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 받아 둘 수 있는 틱 수입니다. 읽을 틱보다 이만큼 앞서 온 의도는 앞 칸을 덮습니다. */
        static constexpr int32 kReceiveCapacity = 64;

        RemoteControllerComponent();
        ~RemoteControllerComponent() override = default;

        void produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent ) final;

        /** @brief 상대 틱 @p tick 의 의도를 넣습니다(사본). 이미 읽은 틱(`_nextInputTick` 앞)은 버립니다. 같은 틱을 다시 넣으면 나중 값이 남습니다. */
        void receiveIntent( uint32 tick, const ControlIntent& intent );
        /** @brief 받아 둔 의도를 모두 잊습니다(연결이 끊기거나 새 판). 마지막 의도 되풀이도 멈춥니다. */
        void clearReceived();
        /** @brief 다음에 읽을 상대 틱입니다(연결이 정한 상대 틱 ↔ 이 씬 틱 대응). */
        void   setNextInputTick( uint32 tick ) { _nextInputTick = tick; }
        uint32 getNextInputTick() const { return _nextInputTick; }
        /** @brief 뒤의 틱이 왔는데 빠져 있던 상대 틱 수입니다. */
        uint32 getMissingTickCount() const { return _missingTickCount; }

    private:
        TickRingBuffer<ControlIntent> _received;   ///< 받은 의도(사본). 읽은 틱은 지운다
        ControlIntent                 _lastIntent; ///< 마지막으로 받은 의도(발동 비트는 지운 채)
        uint32                        _nextInputTick;
        uint32                        _missingTickCount;
        bool                          _bHasLastIntent;
    };
} // namespace sw
