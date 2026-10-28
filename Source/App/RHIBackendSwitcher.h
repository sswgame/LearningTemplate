/**
 * @file RHIBackendSwitcher.h
 * @brief gv_rhiBackend 변경을 받아 프레임 경계에서 그래픽 API 를 교체합니다(전역 변수 훅부터 모듈 재생성까지).
 */
#pragma once
#include "Core/Console/DevCommandsEnabled.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    enum class RHIBackend : uint32;

    struct GlobalVariableInfo;

    class EditorModuleManager;
    class Engine;

    /**
     * @class RHIBackendSwitcher
     * @brief gv_rhiBackend 변경을 받아 프레임 경계에서 백엔드를 교체합니다(전역 변수 훅부터 모듈 재생성까지).
     * @details 교체는 "렌더 워커 비우기 → 모듈 인스턴스 파괴 → 디바이스 재생성 → 모듈 재생성" 순서를 어기면 바로 죽습니다.
     *          그 순서를 아는 곳을 하나로 둡니다. 상용 엔진이 디바이스 상실 · 어댑터 변경 · 전체 화면 전환을 모두 같은 재생성
     *          경로로 모으는 것과 같은 이유입니다. 사유는 늘어도 순서를 아는 코드는 하나여야 합니다. 쓰는 곳은 App 뿐입니다.
     *          요청은 메뉴 · 콘솔(`gv_rhiBackend`)과 시나리오 단계 `SwapBackend` 가 같은 길로 합니다. App 이 이 객체를 호스트 범위에 걸어 두고(`SubsystemScope::bind`)
     *          단계와 감시 값은 `engine::findSubsystem<RHIBackendSwitcher>()` 로 찾습니다 — 정적 포인터 창구를 두지 않습니다.
     */
    class RHIBackendSwitcher
    {
    public:
        RHIBackendSwitcher();
        ~RHIBackendSwitcher() = default;

        RHIBackendSwitcher( const RHIBackendSwitcher& )            = delete;
        RHIBackendSwitcher& operator=( const RHIBackendSwitcher& ) = delete;

        /**
         * @brief 협력 객체를 연결하고 gv_rhiBackend 변경 훅을 설치합니다.
         * @param pEngine RHI 를 소유한 루프
         * @param pGameModuleManager 교체 전후로 내리고 다시 세울 모듈 호스트
         * @param bEnableEditor 에디터 모드면, 에디터를 지원하지 않는 백엔드 요청을 되돌립니다.
         */
        void initialize( Engine* pEngine, EditorModuleManager* pGameModuleManager, bool bEnableEditor );
        /**
         * @brief 변경 훅을 떼어 냅니다.
         * @details GlobalVariableManager 는 Engine 가 소유합니다. 그 종료보다 먼저 불러야 이미 사라진 this 를 가리키는 훅이
         *          남지 않습니다.
         */
        void shutdown();

        /** @brief 예약된 교체가 있으면 적용합니다. 프레임 경계에서만 부릅니다. */
        void applyIfPending();

        // --- 자동화 창구(시나리오 단계 `SwapBackend` 와 감시 값 `RHI.*` — `RHIBackendSwitcherAutomation.cpp`) -------------------------------------------------
        /** @brief 지금 확정된(디바이스가 실제로 쓰는) 백엔드입니다. RHI 가 없으면 false 입니다. */
        [[nodiscard]] bool findCommittedBackend( RHIBackend& outBackend ) const;
        /**
         * @brief 지금 확정된 백엔드를 "시나리오가 시작할 때의 백엔드" 로 적어 둡니다(`backend="start"` 가 돌아올 곳). RHI 가 없으면 false 이고 적지 않습니다.
         * @details 교체기는 백엔드 교체와 모듈 리로드를 지나 살아남는 App 의 객체라 시나리오 단계가 맡길 자리로 알맞다. 시나리오 준비 단계가 시작마다 한 번 부른다.
         */
        [[nodiscard]] bool recordStartBackend();
        /** @brief `recordStartBackend` 로 적어 둔 백엔드입니다. 아직 적지 않았으면 false 입니다. */
        [[nodiscard]] bool findStartBackend( RHIBackend& outBackend ) const;
        /** @brief 요청했지만 아직 적용하지 않은 교체가 있으면 true 입니다. */
        bool isSwapPending() const;
        /** @brief 이 프로세스에서 교체에 실패한 횟수입니다(실패하면 이전 백엔드로 복구한다). */
        uint32 getFailedSwapCount() const { return _failedSwapCount; }
        /**
         * @brief 백엔드 교체를 요청합니다 — 메뉴 · 콘솔과 같은 길(`gv_rhiBackend` 의 `setValueAsInt` → 변경 콜백)이며, 적용은 이 프레임 끝의 `applyIfPending` 입니다.
         * @return 요청했으면(이미 그 백엔드이면 요청 없이도) true 입니다. 변수를 찾지 못하면 false 입니다.
         */
        [[nodiscard]] bool requestSwap( RHIBackend backend );
        /** @brief 지금 디바이스를 제거된 상태로 만듭니다(개발 명령 `rhi.removeDevice`, `IRHIDevice::debugRemoveDevice`). 할 수 없는 백엔드면 false 입니다. */
        [[nodiscard]] bool removeDeviceForTest();
#if SW_DEV_COMMANDS_ENABLED
        /**
         * @brief 렌더 스레드가 다음 프레임의 펜스 대기 자리에서 @p waitMilliseconds 동안 잠든 뒤 디바이스를 제거하게 합니다(개발 명령 `rhi.removeDevice <ms>`,
         *        `RenderThread::requestDeviceLossAfterDriverWait`). 렌더 스레드가 없으면(엔진이 붙지 않음) false 입니다.
         */
        [[nodiscard]] bool removeDeviceAfterDriverWaitForTest( uint32 waitMilliseconds );
#endif

    private:
        /** @brief gv_rhiBackend 변경 이벤트 훅입니다. */
        void onBackendVariableChanged( const GlobalVariableInfo* pInfo );
        /** @brief 모듈을 내리고 디바이스를 다시 만든 뒤 모듈을 세웁니다. 실패하면 false. */
        [[nodiscard]] bool applyPendingChange();

    private:
        Engine*              _pEngine;            // 소유하지 않는다
        EditorModuleManager* _pGameModuleManager; // 소유하지 않는다

        uint32 _failedSwapCount;
        /** @brief 시나리오가 시작할 때의 백엔드입니다(`recordStartBackend`). `_bStartBackendRecorded` 가 참일 때만 뜻이 있다. */
        RHIBackend _startBackend;

        uint8 _bEnableEditor : 1;
        /** @brief gv_rhiBackend 를 되돌리는 대입이 변경 콜백을 재귀 호출하지 않게 막습니다. */
        uint8 _bHandlingChange : 1;
        /** @brief `recordStartBackend` 가 성공한 적이 있으면 참입니다. */
        uint8                  _bStartBackendRecorded : 1;
        [[maybe_unused]] uint8 _reserved              : 5;
    };
} // namespace sw
