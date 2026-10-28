/**
 * @file PairedAnimationNetStarterComponent.h
 * @brief 같은 오브젝트의 `PairedAnimationComponent` 세션을 끝날 때마다 다시 시작하는 서버 쪽 시작자입니다 — 두 프로세스 복제 확인 씬(`pairedanimnet.scene.xml`)의 구동기입니다.
 * @details 복제 계약(`PairedAnimationNetSessionComponent`)은 "서버가 시작을 결정한다" 이다. 이 컴포넌트가 그 결정을 씬 데이터로 내린다: 앵커 오브젝트에 `PairedAnimationComponent`
 *          (`_bStartOnPlay` 는 끈다 — 켜면 클라이언트도 제 세션을 따로 시작해 복제된 세션과 같은 참가자를 두고 부딪힌다)와 이 컴포넌트를 함께 둔다.
 *          역할은 실행 인자가 정한다(`GameNetSession` 머리말): 클라이언트면 아무것도 하지 않고(서버가 보낸 세션을 받기만 한다), 서버나 세션이 없으면(혼자 보는 씬) 시작한다.
 *          세션이 `Playing` 이 아닌 틱이 `_restartDelay` 초 이어지면 틱 뒤에 `startSession` 을 부른다 — 세션이 쉬지 않고 돌아 늦게 붙은 클라이언트가 늘 재생 중인
 *          세션을 받는다(늦은 참가), 새 세션이 시작될 때마다 시작 · 끝 메시지가 오간다. 참가자의 클립을 아직 읽는 중이면(`ParticipantNotReady`) 다음 틱에 다시 시도한다.
 *          그 밖의 실패는 데이터 결함이라 오류로 남기고 멈춘다(다른 값으로 대신 시도하지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class PairedAnimationNetStarterComponent
     * @brief 파일 머리말 참고.
     */
    REFLECT( Category = "Network", DisplayName = "Paired Animation Net Starter", Tooltip = "Server side: restarts this object's paired animation session each time it ends (verification scene driver)" )
    class SW_KIT_API PairedAnimationNetStarterComponent : public Component
    {
    public:
        REFLECT_BODY();

        PairedAnimationNetStarterComponent();
        ~PairedAnimationNetStarterComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

    private:
        /** @brief 같은 오브젝트의 `PairedAnimationComponent` 로 세션을 시작합니다(틱 뒤에 불립니다). */
        void startSession();

        PROPERTY( Category = "Network", DisplayName = "Restart Delay", Min = 0.0, Units = s, Tooltip = "Seconds the session may stay ended before the next one starts" )
        float32 _restartDelay;

        float32 _endedSeconds; ///< 세션이 재생 중이 아니었던 시간
        uint8   _bStarts;      ///< 이 프로세스가 시작한다(클라이언트가 아니고 데이터 결함으로 멈추지 않았다)
    };
} // namespace sw
