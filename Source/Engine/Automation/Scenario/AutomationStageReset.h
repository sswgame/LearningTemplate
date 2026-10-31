/**
 * @file AutomationStageReset.h
 * @brief 체인의 다음 단계 앞에서 호스트 · 모듈이 자기 상태를 새 프로세스처럼 되돌리는 등록입니다(`SW_AUTOMATION_STAGE_RESET`).
 * @details 세션은 단계마다 씬 · 전역 변수 · 게임 인스턴스를 되돌린다(`AutomationSession::startStage`). 엔진이 모르는 상태(편집기의 패널 · 팝업 · 배치)는
 *          그 상태를 가진 모듈이 여기에 등록해 같은 자리에서 되돌린다 — 되돌리지 않으면 앞 단계가 연 팝업 · 닫은 패널이 다음 단계의 클릭을 막았다
 *          (혼자 돌면 통과하는 시나리오가 체인에서 졌다). 언리얼 PIE 가 플레이마다 새 GameInstance 를 만드는 것과 같은 자리를 모듈 상태까지 넓힌다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 단계 되돌리기 함수입니다. 게임 스레드에서 다음 단계의 실행기를 세우기 전에 부릅니다. */
    using AutomationStageResetFunction = void ( * )();

    /** @struct AutomationStageResetRegistration @brief 등록 항목 하나입니다. 문자열과 함수는 등록한 이미지의 정적 저장소에 있습니다. */
    struct AutomationStageResetRegistration
    {
        const utf8*                  _pName;     ///< 무엇을 되돌리는지(로그 · 중복 검사)
        AutomationStageResetFunction _pFunction; ///< 되돌리는 함수
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationStageResetRegistry
     * @brief 단계 되돌리기 레지스트리입니다. 모듈을 내리면 그 모듈의 항목도 빠집니다. 게임 스레드와 정적 초기화에서만 씁니다.
     */
    class SW_API AutomationStageResetRegistry
    {
    public:
        /** @brief 등록합니다. 같은 이름이 이미 있으면 false 이고 등록하지 않습니다. */
        [[nodiscard]] static bool registerReset( const AutomationStageResetRegistration* pRegistration );
        static void               unregisterReset( const AutomationStageResetRegistration* pRegistration );
        /** @brief 등록된 되돌리기를 등록 순서로 모두 부르고 부른 수를 돌려줍니다. */
        static uint32 resetAll();
    };
} // namespace sw

namespace sw
{
    /** @brief `SW_AUTOMATION_STAGE_RESET` 이 두는 정적 등록자입니다. 등록이 거절됐으면 내릴 때 빼지 않습니다. */
    struct SW_API AutomationStageResetRegistrar
    {
        explicit AutomationStageResetRegistrar( const AutomationStageResetRegistration* pRegistration );
        ~AutomationStageResetRegistrar();
        AutomationStageResetRegistrar( const AutomationStageResetRegistrar& )            = delete;
        AutomationStageResetRegistrar& operator=( const AutomationStageResetRegistrar& ) = delete;

        const AutomationStageResetRegistration* _pRegistration;
    };
} // namespace sw

/** @brief 단계 되돌리기를 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). @p id 는 정적 이름을 만드는 식별자입니다. */
#define SW_AUTOMATION_STAGE_RESET( id, pName, pFunction )                                                 \
    static const ::sw::AutomationStageResetRegistration sw_automationStageReset_##id{ pName, pFunction }; \
    static const ::sw::AutomationStageResetRegistrar    sw_automationStageResetRegistrar_##id             \
    {                                                                                                     \
        &sw_automationStageReset_##id                                                                     \
    }
