/**
 * @file ModuleManager/DeviceChangeSequence.h
 * @brief 장치 교체 · 다시 만들기의 순서 하나입니다 — 모듈 인스턴스를 내리고 → 렌더러가 장치를 다시 만들고 → 모듈 인스턴스를 다시 세운다.
 * @details 순서를 아는 곳은 여기 하나다. 개발 구성의 백엔드 교체(`gv_rhiBackend`)와 배포도 쓰는 장치 분실 재생성(`RHIDeviceLostPolicy::Recreate`)이 같은 길로 온다
 *          (언리얼 · 유니티도 장치 상실 · 어댑터 변경을 한 재생성 경로로 모은다). 모듈 몫은 주인(`GameModuleManager::applyPendingDeviceChange`)이 델리게이트로 넘긴다 —
 *          주인의 소멸이 엔진 전역 등록을 건드리므로 순서만 따로 두어 가짜 렌더러로 시험한다(`ModuleManager/Tests/TestDeviceChangeSequence.cpp`).
 *          필수 값이 없으면 멈춘다: 쓸 장치가 없거나 모듈을 다시 세우지 못하면 게임 인스턴스 없이 돌지 않고 종료를 요청한다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"

#include "ModuleManager/ModuleManagerAPI.h"

namespace sw
{
    class IEngineRenderDriver;
    class QuitRequest;

    /**
     * @struct DeviceChangeSequence
     * @brief 파일 머리말 참고. 장치를 다시 만드는 앞뒤의 모듈 몫을 주인(`GameModuleManager`)이 채우고 `apply` 를 부른다. 프레임 경계의 게임 스레드에서만 부른다.
     */
    struct SW_MODULEMANAGER_API DeviceChangeSequence
    {
        Delegate<void()> _suspendModules;  ///< 게임 · 호스트 모듈 인스턴스를 내린다(API 표는 둔다 — 같은 라이브러리로 다시 세운다)
        Delegate<bool()> _recreateModules; ///< 새 장치로 게임 · 호스트 모듈 인스턴스를 다시 세운다. 모두 섰으면 true

        /**
         * @brief 예약된 장치 교체 · 다시 만들기가 있으면 적용합니다.
         * @details 쓸 장치가 없으면(`BackendChangeResult::Failed`) 모듈을 다시 세우지 않고, 모듈을 다시 세우지 못하면 — 둘 다 @p quitRequest 에
         *          `ProcessExitCode::DeviceLost` 를 건다(처음 요청의 코드가 이긴다).
         * @return 예약이 없거나 요청대로 섰으면 true 입니다. 앞 백엔드로 되돌렸거나(`Restored` — 모듈은 다시 섰다) 끝내기를 요청했으면 false 입니다.
         */
        [[nodiscard]] bool apply( IEngineRenderDriver& renderDriver, QuitRequest& quitRequest ) const;
    };
} // namespace sw
