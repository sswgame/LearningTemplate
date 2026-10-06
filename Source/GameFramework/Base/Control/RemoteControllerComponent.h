/**
 * @file RemoteControllerComponent.h
 * @brief 원격 조종자 — 네트워크로 받은 의도(입력 창 `NetInputReceiveBuffer` 의 틱별 바이트 = `ControlIntent::write`)를 냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Control/ControlIntent.h"
#include "GameFramework/Base/Control/ControllerComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class NetInputReceiveBuffer;

    /**
     * @class RemoteControllerComponent
     * @brief 서버(또는 롤백 피어)의 폰을 상대 연결의 의도로 몹니다. 틱마다 `_nextInputTick` 의 바이트를 읽어 내고 한 틱 나아갑니다.
     * @details - 받는 버퍼는 빌려 씁니다 — 네트워크 키트(연결)가 들고, 창(`setWindow` · 확인)도 그쪽이 옮긴다. 키트가 버퍼를 버리기 전에 `setReceiveBuffer( nullptr )`.
     *          - 그 틱을 못 받았으면 마지막 의도를 되풀이합니다(예측) — 발동 비트는 지운다(한 번 누름이 두 번 되지 않게). 못 받은 틱 수를 셉니다.
     *          - 선 형식은 의도 하나(`ControlIntent::write` — 양자화)라 로컬 조종자가 낸 값(조종 시스템이 `quantize` 한 값)과 같은 값으로 움직인다.
     *          언리얼 서버의 `ServerMove` 가 받은 `FSavedMove` 로 폰을 모는 자리 · 유니티 Netcode `ICommandData` 를 서버가 읽는 자리입니다.
     */
    REFLECT( Category = "Control", DisplayName = "Remote Controller", Tooltip = "Drives the pawn with control intents received over the network (input window bytes)" )
    class SW_GF_API RemoteControllerComponent : public ControllerComponent
    {
    public:
        REFLECT_BODY();

        RemoteControllerComponent();
        ~RemoteControllerComponent() override = default;

        void produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent ) final;

        /** @brief 읽을 버퍼를 빌립니다(nullptr 이면 떼기 — 그 뒤로는 마지막 의도를 되풀이). */
        void setReceiveBuffer( const NetInputReceiveBuffer* pReceiveBuffer ) { _pReceiveBuffer = pReceiveBuffer; }
        /** @brief 다음에 읽을 상대 틱입니다(연결이 정한 상대 틱 ↔ 이 씬 틱 대응). */
        void   setNextInputTick( uint32 tick ) { _nextInputTick = tick; }
        uint32 getNextInputTick() const { return _nextInputTick; }
        /** @brief 받지 못해(또는 깨져) 마지막 의도를 되풀이한 틱 수입니다. */
        uint32 getMissingTickCount() const { return _missingTickCount; }

    private:
        ControlIntent                _lastIntent; ///< 마지막으로 받은 의도(발동 비트는 지운 채)
        const NetInputReceiveBuffer* _pReceiveBuffer;
        uint32                       _nextInputTick;
        uint32                       _missingTickCount;
        bool                         _bHasLastIntent;
    };
} // namespace sw
