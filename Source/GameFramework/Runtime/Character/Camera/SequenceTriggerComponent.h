/**
 * @file SequenceTriggerComponent.h
 * @brief 활성자가 들어오면 시퀀스(컷신)를 트는 볼륨입니다. 마을 도착 · 보스 방 입구 같은 자리에 둡니다.
 */
#pragma once
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/ActivatorVolumeComponent.h"

namespace sw
{
    /**
     * @brief 누구에게 컷씬을 틀고 진행 플래그를 어디에 적는지입니다(멀티플레이). 혼자 하는 게임에서는 둘이 같다(플레이어 하나).
     * @details 언리얼은 트리거 블루프린트가 서버에서 `Multicast`(전원) 또는 그 플레이어의 `Client` RPC 로 시퀀스를 틀고, 진행은 `GameState` 또는 `PlayerState` 에 둔다.
     *          유니티 Netcode 는 `ClientRpc` 의 `ClientRpcParams` 로 대상을 고른다. 여기는 그 고르기를 데이터 칸 하나로 둔다.
     */
    ENUM()
    enum class SequenceTriggerAudience : uint8
    {
        Everyone = 0, ///< 모든 플레이어에게 튼다 — 진행 플래그는 판 상태(세션 하나)
        Activator     ///< 들어온 플레이어에게만 튼다 — 진행 플래그는 그 플레이어의 상태(`PlayerStateComponent`)
    };

    /**
     * @class SequenceTriggerComponent
     * @brief 활성자가 들어오면(`ActivatorVolumeComponent` — 겹침 또는 거리) `_sequence` 오브젝트의 `SequencePlayerComponent` 를 처음부터 틉니다.
     * @details 컷 카메라 · 블렌드는 그 오브젝트의 `CinematicCameraComponent` 가, 외형 바꾸기는 `SequenceAppearanceComponent` 가 받는다 — 이 볼륨은 트는 때만 정한다.
     *          언리얼에서 트리거 박스의 겹침 이벤트로 Level Sequence Actor 를 Play 하는 블루프린트, 유니티에서 `OnTriggerEnter` 로 `PlayableDirector.Play` 를 부르는
     *          스크립트에 해당한다(둘 다 게임마다 손으로 적는다). 오브젝트에 시퀀스 플레이어가 없으면 오류를 남기고 발동한 것으로 치지 않는다.
     *          들어옴은 트리거 계약의 오브젝트 쌍 진입(`onOverlapBegin`)이라 유지(Stay) · 몸 여럿에서 다시 틀지 않는다. 한 번만 · 다시 재생은 바탕의 `_bOnce`(블루프린트 `DoOnce` 자리)이고,
     *          발동 수(`_fireCount`)는 상태 칸이라 세이브 · 재접속 뒤에도 한 번만 하는 컷씬을 다시 틀지 않는다. 재생 중 다시 들어오면 처음부터 다시 틀지 않는다(발동 아님),
     *          플레이어가 데이터 결함으로 틀지 못하면 발동 아님이다.
     *          진입만 받으므로 재생 중 트리거 밖으로 밀려나거나 트리거의 스트리밍 셀이 내려가도(파괴 길의 이탈) 재생은 이어진다 — 시퀀스 오브젝트는 늘 올라 있는 자리에 둔다.
     *          멀티플레이: 판정은 서버만 한다(언리얼 `HasAuthority`) — 클라이언트 월드의 트리거는 틀지 않고, 서버가 튼 재생을 넷 키트(`GF_NetSequence`)가 `_audience` 대로
     *          보낸다(그 플레이어만 · 전원, 늦게 들어온 클라이언트에는 지금 프레임부터). 키트는 등록 칸(`SequenceTriggerComponent`)에서 발동 수(`getActivationCount`)가 오른 트리거를 찾는다.
     *          본 기록을 세이브에 남기고 입구끼리 나누려면 바탕의 진행 플래그(`_progressFlag`), 부활 · 불러오기 자리에서 다시 틀지 않으려면 `_spawnInside` 를 고른다(언리얼 `bPlayOnce` 류는 액터 상태라 세이브에 없다).
     */
    REFLECT( Category = "Cinematics", DisplayName = "Sequence Trigger", Tooltip = "Plays a sequence player's sequence when the activator enters (trigger overlap or distance)" )
    class SW_GF_API SequenceTriggerComponent : public ActivatorVolumeComponent
    {
    public:
        REFLECT_BODY();

        SequenceTriggerComponent();
        ~SequenceTriggerComponent() override = default;

        /** @brief 틀 시퀀스 플레이어의 오브젝트를 정합니다. 코드로 만든 볼륨 · 테스트가 씁니다. 씬에 놓은 볼륨은 PROPERTY 가 채웁니다. */
        void setSequence( GameObjectHandle sequence ) { _sequence = sequence; }
        /** @brief 누구에게 틀고 진행을 어디에 적는지 정합니다. 코드로 만든 볼륨 · 테스트가 씁니다. */
        void setAudience( SequenceTriggerAudience audience ) { _audience = audience; }

        /** @brief 레지스트리의 자기 칸에 오릅니다 — 넷 키트가 씬을 훑지 않고 트리거를 찾는다. */
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 가리킨 디렉터가 스스로 트는(`_bAutoPlay`) 것이면 오류를 남깁니다 — 시작 때 이미 틀어 트리거는 "재생 중" 으로 보고 영영 발동하지 않는다. */
        void onBeginPlay() override;

        SequenceTriggerAudience getAudience() const { return _audience; }
        const GameObjectHandle& getSequence() const { return _sequence; }
        /** @brief 이 트리거가 시퀀스를 튼 횟수(이번 실행, 저장하지 않는다)입니다. 넷 키트가 오른 것을 보고 시작을 보낸다. */
        uint32 getActivationCount() const { return _activationCount; }
        /** @brief 마지막으로 틀게 한 활성자입니다. */
        const GameObjectHandle& getLastActivator() const { return _lastActivator; }

    protected:
        [[nodiscard]] bool onVolumeActivated( const GameObject& activator ) override;
        /** @brief `Activator` 면 활성자의 플레이어 상태, `Everyone` 이면 판 상태(바탕)입니다. */
        [[nodiscard]] GameFlags* findProgressFlags( const GameObject* pActivator ) const override;

    private:
        PROPERTY( Category = "Sequence", DisplayName = "Sequence", Tooltip = "Object with the SequencePlayerComponent to play" )
        GameObjectHandle _sequence;
        PROPERTY( Category = "Sequence", DisplayName = "Audience", Tooltip = "Everyone: play for all players, progress flag in the session state; Activator: play only for the player who entered, progress flag in that player's state" )
        SequenceTriggerAudience _audience;
        GameObjectHandle        _lastActivator;   ///< 마지막으로 틀게 한 활성자(넷 키트가 대상을 고른다)
        uint32                  _activationCount; ///< 이번 실행에서 튼 횟수(넷 키트가 오른 것을 본다)
    };
} // namespace sw
