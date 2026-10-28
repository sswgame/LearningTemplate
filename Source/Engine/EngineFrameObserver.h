/**
 * @file EngineFrameObserver.h
 * @brief 엔진 루프의 프레임 경계에 붙는 구동기(자동화 시나리오 · 시퀀서 · AI 같은 외부 구동기)의 인터페이스와 그 문맥입니다.
 * @details 루프는 관찰자 목록만 알고 무엇이 붙었는지는 모릅니다(언리얼 자동화 모듈 · 유니티 PlayerLoop 훅과 같은 자리).
 *          Core 만 include 하므로 어느 Engine 티어에서 구현해도 됩니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class InputManager;

    /** @brief 프레임 앞 · 씬 틱 뒤 · 프레임 뒤와 창 닫힘에서 관찰자에게 넘기는 문맥입니다. */
    struct EngineFrameContext
    {
        /** @brief 이번 프레임의 입력 관리자입니다. 입력 단계가 서지 않은 호스트에서는 nullptr 입니다. */
        InputManager* _pInput = nullptr;
        /** @brief 렌더 스레드가 지금까지 쓴 시나리오 스크린샷 수(프로세스 전체 누적)입니다. 렌더 스레드가 없으면 0 입니다. */
        uint32 _completedScreenshotCount = 0;
        /** @brief 이번 프레임 시간(초, 시간 배율이 든 값 — 루프가 `beginFrame` · `tick` 에 받은 것)입니다. 창 닫힘에서는 마지막 프레임의 값입니다. */
        float32 _deltaSeconds = 0.0f;
        /**
         * @brief 그 훅을 부르는 시점의 활성 씬 식별자(원본 경로, 없으면 씬 이름)입니다. 활성 씬이 없으면 비어 있습니다.
         * @details 씬의 문자열을 가리키므로 훅 안에서만 유효합니다. 들고 있으려면 복사합니다. 씬 전환은 프레임 안에서 일어나므로 훅마다 다를 수 있습니다.
         */
        string_view _activeSceneID{};
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
     * @brief `Engine::registerFrameObserver` 로 붙는 프레임 관찰자입니다. 게임 스레드에서만 불립니다.
     * @details 종료 코드를 돌려주는 두 함수는 끝나지 않았으면 `kRunning` 을 돌려줍니다. 루프는 처음으로 `kRunning` 이 아닌 값을 낸 관찰자의 코드로 끝냅니다.
     *          관찰자의 수명은 붙인 쪽이 쥐며, 없애기 전에 `Engine::registerFrameObserver` 가 돌려준 구독을 놓습니다.
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
         * @brief 씬 틱 뒤 — `Engine::update` 이 씬 틱 · 물리 · 내비 디버그 그리기 뒤, 런타임 UI 갱신 · 렌더 패킷 앞, 헤드리스 return 뒤에서 부릅니다(헤드리스 루프에서는 불리지 않는다).
         * @details 주의: 이 호출 자리를 옮기면 관찰자가 보는 시점(그 프레임의 게임 상태가 정해진 뒤)이 바뀝니다. 그 시점에 할 일(장면별 프레임 시간 기록 같은 것)이
         *          있는 관찰자만 덮어씁니다. 기본은 아무것도 하지 않습니다.
         */
        virtual void onAfterSceneTick( [[maybe_unused]] const EngineFrameContext& context ) {}
        /**
         * @brief 프레임 끝 — 씬 틱 · 렌더 제출 뒤, 입력 프레임을 닫기 전입니다(그 프레임의 눌림 엣지가 아직 보인다).
         * @return 프로세스를 끝낼 종료 코드, 아니면 `kRunning` 입니다.
         */
        [[nodiscard]] virtual int32 onFrameEnd( const EngineFrameContext& context ) = 0;
        /**
         * @brief 창 제목에 적을 "지금 무엇을 돌리는가" 입니다. 적을 것이 없으면 false 입니다(기본).
         * @param outScenarioName 돌고 있는 시나리오 이름
         * @param outStepKind 마지막으로 시작한 단계 이름(아직 없으면 빈 글)
         * @param outStepNumber 그 단계가 파일에서 몇 번째인지(0 부터)
         * @param outFrame 시나리오 프레임
         */
        [[nodiscard]] virtual bool tryGetRunStatus( [[maybe_unused]] string& outScenarioName, [[maybe_unused]] string& outStepKind, [[maybe_unused]] uint32& outStepNumber,
                                                    [[maybe_unused]] uint32& outFrame ) const
        {
            return false;
        }
        /**
         * @brief 창이 닫혀 루프가 끝났습니다(App 이 루프 뒤에 한 번 부른다).
         * @return 이 관찰자가 정하는 종료 코드, 정할 것이 없으면 `kRunning` 입니다. 코드를 내면 다른 종료 요청(프로파일 세션)보다 이깁니다.
         */
        [[nodiscard]] virtual int32 onWindowClosed( const EngineFrameContext& context ) = 0;
        /** @brief 이번 프레임의 렌더 패킷을 채우는 중입니다(게임 스레드, 제출 전). */
        virtual void onBuildRenderPacket( EngineRenderPacketContext& inoutContext ) = 0;
    };
} // namespace sw
