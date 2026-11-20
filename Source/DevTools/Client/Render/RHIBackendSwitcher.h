/**
 * @file DevTools/Client/Render/RHIBackendSwitcher.h
 * @brief 실행 중 백엔드 교체 요청(`gv_rhiBackend` · 시나리오 단계 `SwapBackend`)과 장치 제거 시험(`rhi.removeDevice`)의 개발 창구입니다.
 * @details 실행 중 교체 · 디바이스 제거 시험은 개발 기능이라 개발 도구에 둔다(배포는 짓지 않는다 — 언리얼도 RHI 변경은 재시작). 교체기는 요청만 RHI 에 예약한다 —
 *          적용(모듈 내리기 → 장치 → 모듈 세우기)은 배포도 쓰는 모듈 관리자 하나가 프레임 경계에서 한다(`GameModuleManager::applyPendingDeviceChange`). 그래서 App 의
 *          모듈 관리자를 몰라도 되고, 기동 단계 서브시스템으로 스스로 선다(렌더러가 있는 클라이언트만 — `RHIBackendSwitcher.cpp` 의 등록).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/DynamicModules.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    class IRHIDevice;
    class RenderThreadSubsystem;
    class RHI;
    class RHISubsystem;
    enum class RHIBackend : uint32;

    struct GlobalVariableInfo;

    /**
     * @class RHIBackendSwitcher
     * @brief `gv_rhiBackend` 변경을 받아 백엔드 교체를 RHI 에 예약하고, 시나리오 · 개발 명령의 교체 · 장치 제거 창구를 듭니다.
     * @details 요청은 메뉴 · 콘솔(`gv_rhiBackend`)과 시나리오 단계 `SwapBackend` 가 같은 길로 합니다. 단계와 감시 값은 `engine::findSubsystem<RHIBackendSwitcher>()` 로 찾습니다.
     *          RHI 단계에 의존을 선언하지 않는다 — 기대면 백엔드 교체(`shutdownDependents`)가 이것도 내렸다 세운다. RHI · 렌더 스레드는 쓸 때 찾는다(`EngineRenderDriver` 와 같은 까닭).
     */
    class SW_DEVTOOLS_API RHIBackendSwitcher final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName      = "RHIBackendSwitcher";
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::EngineMisc;

        /** @param rhiSubsystem · renderThreadSubsystem 같은 기동 범위의 단계입니다(이것보다 먼저 만들어지고 나중에 사라진다). */
        RHIBackendSwitcher( RHISubsystem& rhiSubsystem, RenderThreadSubsystem& renderThreadSubsystem );
        ~RHIBackendSwitcher() override = default;

        RHIBackendSwitcher( const RHIBackendSwitcher& )            = delete;
        RHIBackendSwitcher& operator=( const RHIBackendSwitcher& ) = delete;

        /** @brief `gv_rhiBackend` 변경 훅을 겁니다. 변수가 없으면 기동을 멈춘다(교체 요청이 조용히 사라지지 않게). */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 변경 훅 · 렌더 스레드 훅을 뗍니다. */
        void              shutdownSubsystem() override;
        const utf8*       getSubsystemName() const override { return kSubsystemName; }
        SubsystemHostMask getSubsystemHost() const override;

        // --- 자동화 창구(시나리오 단계 `SwapBackend` 와 감시 값 `RHI.*`) -------------------------------------------------
        /** @brief 지금 확정된(디바이스가 실제로 쓰는) 백엔드입니다. RHI 가 없으면 false 입니다. */
        [[nodiscard]] bool findCommittedBackend( RHIBackend& outBackend ) const;
        /**
         * @brief 지금 확정된 백엔드를 "시나리오가 시작할 때의 백엔드" 로 적어 둡니다(`backend="start"` 가 돌아올 곳). RHI 가 없으면 false 이고 적지 않습니다.
         * @details 교체기는 백엔드 교체와 모듈 리로드를 지나 살아남는 기동 단계라 시나리오 단계가 맡길 자리로 알맞다. 시나리오 준비 단계가 시작마다 한 번 부른다.
         */
        [[nodiscard]] bool recordStartBackend();
        /** @brief `recordStartBackend` 로 적어 둔 백엔드입니다. 아직 적지 않았으면 false 입니다. */
        [[nodiscard]] bool findStartBackend( RHIBackend& outBackend ) const;
        /** @brief 요청했지만 아직 적용하지 않은 교체가 있으면 true 입니다. */
        bool isSwapPending() const;
#if SW_WITH_DYNAMIC_MODULES
        /**
         * @brief 백엔드 교체를 요청합니다 — 메뉴 · 콘솔과 같은 길(`gv_rhiBackend` 의 `setValueAsInt` → 변경 콜백)이며, 적용은 이 프레임 끝의 모듈 관리자입니다.
         * @details 동적 모듈 구성에만 있다 — 정적 모듈 구성(`SW_STATIC_MODULES`)은 백엔드 하나를 Engine 에 링크해 교체할 곳이 없다. 그 구성에는 시나리오 단계 `SwapBackend` 도
         *          등록되지 않아, 그 단계를 쓴 시나리오는 불러올 때 "unknown step" 오류로 멈춘다(조용히 지지 않는다).
         * @return 요청했으면(이미 그 백엔드이면 요청 없이도) true 입니다. 변수를 찾지 못하면 false 입니다.
         */
        [[nodiscard]] bool requestSwap( RHIBackend backend );
#endif
        /** @brief 지금 디바이스를 제거된 상태로 만듭니다(개발 명령 `rhi.removeDevice`, `IRHIDevice::debugRemoveDevice`). 할 수 없는 백엔드면 false 입니다. */
        [[nodiscard]] bool removeDeviceForTest();
        /** @brief `removeDeviceAfterDriverWaitForTest` 가 받는 가장 긴 대기(밀리초)입니다. 행 감시를 시험할 만큼(시나리오 시한 + 유예) 길되 체인의 프로세스 시한은 넘지 않는다. */
        static constexpr uint32 kMaxDeviceLossDriverWaitMilliseconds = 60000;
        /**
         * @brief 렌더 스레드가 다음 프레임의 펜스 대기 자리에서 @p waitMilliseconds 동안 잠든 뒤 디바이스를 제거하게 합니다(개발 명령 `rhi.removeDevice <ms>`).
         * @details 실제 TDR 은 GPU 가 멈춘 동안 펜스 대기가 드라이버 안에서 막힌 뒤에야 첫 손실 결과를 돌려준다. `debugRemoveDevice` 만으로는 그 대기가 없어
         *          "대기 뒤의 복귀 · 행" 을 재현하지 못했다. 요청은 이 객체가 들고, 렌더 스레드의 프레임 시작 훅(처음 요청 때 건다)이 소비한다.
         * @param waitMilliseconds 1..kMaxDeviceLossDriverWaitMilliseconds. 아직 소비하지 않은 요청이 있으면 새 값으로 바꾼다.
         * @return 렌더 스레드가 없으면 false 이고 요청을 걸지 않습니다.
         */
        [[nodiscard]] bool removeDeviceAfterDriverWaitForTest( uint32 waitMilliseconds );

    private:
        /** @brief 선 RHI 입니다(서지 않았으면 nullptr). */
        RHI* findRHI() const;
        /** @brief gv_rhiBackend 변경 이벤트 훅입니다. 동적 모듈 구성은 교체를 예약하고, 정적 모듈 구성은 오류를 남기고 값을 지금 백엔드로 되돌린다(콘솔 · 메뉴가 조용히 무시되지 않게). */
        void onBackendVariableChanged( GlobalVariableInfo* pInfo );
        /** @brief 렌더 스레드 프레임 시작 훅입니다(`RenderThread::setBeginFrameHook`). 걸린 대기가 있으면 잠든 뒤 디바이스를 제거한다. */
        void onBeginFrameOnRenderThread( IRHIDevice& device );

    private:
        RHISubsystem&          _rhiSubsystem;          // 소유하지 않는다
        RenderThreadSubsystem& _renderThreadSubsystem; // 소유하지 않는다
        /** @brief `gv_rhiBackend` 변경 훅의 구독입니다(`initializeSubsystem` 이 건다 — 변수가 먼저 사라져도 무효가 될 뿐이다). */
        Subscription _backendVariableSubscription;
        /// @brief `removeDeviceAfterDriverWaitForTest` 가 건 대기(0 = 요청 없음)입니다. 개발 명령이 쓰고 렌더 스레드가 비운다.
        atomic<uint32> _pendingDeviceLossWaitMilliseconds;
        /** @brief 시나리오가 시작할 때의 백엔드입니다(`recordStartBackend`). `_bStartBackendRecorded` 가 참일 때만 뜻이 있다. */
        RHIBackend _startBackend;

        /** @brief `recordStartBackend` 가 성공한 적이 있으면 참입니다. */
        uint8 _bStartBackendRecorded : 1;
        /** @brief 렌더 스레드에 프레임 시작 훅을 걸었으면 참입니다(`shutdownSubsystem` 이 뗀다). */
        uint8                  _bBeginFrameHookBound : 1;
        [[maybe_unused]] uint8 _reserved             : 6;
    };
} // namespace sw
