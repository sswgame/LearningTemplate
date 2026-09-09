/**
 * @file BackendSwapController.h
 * @brief RHI 백엔드 교체(핫스왑) 조율 — 전역 변수 훅부터 모듈 재생성까지.
 *
 * @details 교체는 "렌더 워커 배수 → 모듈 인스턴스 파괴 → 디바이스 재생성 → 모듈 재생성" 순서를
 *          어기면 즉시 죽는다. 그 순서를 아는 자리를 하나로 둔다. 상용 엔진이 디바이스 상실·
 *          어댑터 변경·전체화면 전환을 전부 같은 재생성 경로로 모으는 것과 같은 이유다 —
 *          사유는 늘어도 순서를 아는 코드는 하나여야 한다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    struct GlobalVariableInfo;

    class EngineLoop;
    class ModuleHost;

    /**
     * @class BackendSwapController
     * @brief gv_rhiBackend 변경을 받아 프레임 경계에서 백엔드를 교체합니다.
     */
    class BackendSwapController
    {
    public:
        BackendSwapController();
        ~BackendSwapController();

        BackendSwapController( const BackendSwapController& )            = delete;
        BackendSwapController& operator=( const BackendSwapController& ) = delete;

        /**
         * @brief 협력자를 연결하고 gv_rhiBackend 변경 훅을 설치합니다.
         * @param pEngineLoop RHI 와 LiveReloadManager 를 소유한 루프.
         * @param pModuleHost 교체 전후로 내리고 다시 세울 모듈 호스트.
         * @param bEnableEditor 에디터 모드면 에디터 미지원 백엔드 요청을 되돌립니다.
         */
        void initialize( EngineLoop* pEngineLoop, ModuleHost* pModuleHost, bool bEnableEditor );
        /**
         * @brief 변경 훅을 떼어 냅니다.
         * @details GlobalVariableManager 는 EngineLoop 이 소유한다 — 그 종료보다 먼저 불러야
         *          죽은 this 를 가리키는 훅이 남지 않는다.
         */
        void shutdown();

        /** @brief 예약된 교체가 있으면 적용합니다. 프레임 경계에서만 부릅니다. */
        void applyIfPending();

    private:
        /** @brief gv_rhiBackend 변경 이벤트 훅. */
        void onBackendVariableChanged( const GlobalVariableInfo* pInfo );
        /** @brief 모듈을 내리고 디바이스를 다시 만든 뒤 모듈을 세웁니다. 실패하면 false. */
        bool applyPendingChange();

    private:
        EngineLoop* _pEngineLoop; // non-owning
        ModuleHost* _pModuleHost; // non-owning

        uint8 _bEnableEditor : 1;
        /** @brief gv_rhiBackend 되돌림 대입이 변경 콜백을 재귀 호출하는 것을 막습니다. */
        uint8                  _bHandlingChange : 1;
        [[maybe_unused]] uint8 _reserved        : 6;
    };
} // namespace sw
