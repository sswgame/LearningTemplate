/**
 * @file EngineFrameObserver.h
 * @brief 엔진 루프의 프레임 경계에 붙는 구동기(자동화 시나리오 · 시퀀서 · AI 같은 외부 구동기)의 인터페이스와 그 문맥입니다.
 * @details 루프는 관찰자 목록만 알고 무엇이 붙었는지는 모릅니다(언리얼 자동화 모듈 · 유니티 PlayerLoop 훅과 같은 자리).
 *          Core 만 include 하므로 어느 Engine 티어에서 구현해도 됩니다(`CheckEngineLayers` prelude).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class InputManager;

    /** @brief 프레임 앞 · 뒤와 창 닫힘에서 관찰자에게 넘기는 문맥입니다. */
    struct EngineFrameContext
    {
        /** @brief 이번 프레임의 입력 관리자입니다. 입력 단계가 서지 않은 호스트에서는 nullptr 입니다. */
        InputManager* _pInput = nullptr;
        /** @brief 렌더 스레드가 지금까지 쓴 시나리오 스크린샷 수(프로세스 전체 누적)입니다. 렌더 스레드가 없으면 0 입니다. */
        uint32 _completedScreenshotCount = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 렌더 패킷을 채우는 동안 관찰자에게 넘기는 문맥입니다. 관찰자가 실을 것만 칸으로 둡니다(관찰자는 렌더러 헤더를 몰라도 된다). */
    struct EngineRenderPacketContext
    {
        /** @brief 이 패킷을 그린 뒤 화면 이미지를 쓸 경로입니다. 비어 있으면 찍지 않습니다. 앞 관찰자가 이미 채웠으면 덮어쓰지 않습니다. */
        string* _pScreenshotPath = nullptr;
    };
} // namespace sw

namespace sw
{
    /**
     * @class IEngineFrameObserver
     * @brief `EngineLoop::addFrameObserver` 로 붙는 프레임 관찰자입니다. 게임 스레드에서만 불립니다.
     * @details 종료 코드를 돌려주는 두 함수는 끝나지 않았으면 `kRunning` 을 돌려줍니다. 루프는 처음으로 `kRunning` 이 아닌 값을 낸 관찰자의 코드로 끝냅니다.
     *          관찰자의 수명은 붙인 쪽이 쥐며, 없애기 전에 `EngineLoop::removeFrameObserver` 로 뗍니다.
     */
    class IEngineFrameObserver
    {
    public:
        /** @brief 아직 끝나지 않았습니다(종료를 요청하지 않는다). */
        static constexpr int32 kRunning = -1;

        virtual ~IEngineFrameObserver() = default;

        /** @brief 프레임 시작 — 입력 `beginFrame` 전입니다(가상 입력은 여기서 붙인다). */
        virtual void onFrameBegin( const EngineFrameContext& context ) = 0;
        /**
         * @brief 프레임 끝 — 씬 틱 · 렌더 제출 뒤, 입력 프레임을 닫기 전입니다(그 프레임의 눌림 엣지가 아직 보인다).
         * @return 프로세스를 끝낼 종료 코드, 아니면 `kRunning` 입니다.
         */
        [[nodiscard]] virtual int32 onFrameEnd( const EngineFrameContext& context ) = 0;
        /**
         * @brief 창이 닫혀 루프가 끝났습니다(App 이 루프 뒤에 한 번 부른다).
         * @return 이 관찰자가 정하는 종료 코드, 정할 것이 없으면 `kRunning` 입니다. 코드를 내면 다른 종료 요청(프로파일 세션)보다 이깁니다.
         */
        [[nodiscard]] virtual int32 onWindowClosed( const EngineFrameContext& context ) = 0;
        /** @brief 이번 프레임의 렌더 패킷을 채우는 중입니다(게임 스레드, 제출 전). */
        virtual void onBuildRenderPacket( EngineRenderPacketContext& inoutContext ) = 0;
    };
} // namespace sw
