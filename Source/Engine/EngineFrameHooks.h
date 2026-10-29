/**
 * @file EngineFrameHooks.h
 * @brief 엔진 루프의 프레임 경계에 붙는 훅(자동화 시나리오 · 텔레메트리 · 개발 도구 같은 외부 구동기)의 델리게이트 묶음과 그 문맥입니다.
 * @details 루프는 훅 목록만 알고 무엇이 붙었는지는 모릅니다(언리얼 `FCoreDelegates::OnBeginFrame` · 유니티 PlayerLoop 훅과 같은 자리).
 *          붙는 쪽은 인터페이스를 상속하지 않고 쓰는 시점의 칸만 채운다. Core 만 include 하므로 어느 Engine 티어에서 써도 됩니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class InputManager;

    /** @brief 프레임 앞 · 씬 틱 뒤 · 프레임 뒤와 창 닫힘에서 관찰자에게 넘기는 문맥입니다. */
    struct EngineFrameContext
    {
        /** @brief 이번 프레임의 입력 관리자입니다. 입력 단계가 서지 않은 호스트에서는 nullptr 입니다. */
        InputManager* _pInput = nullptr;
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
    /** @brief 종료 코드 칸이 "아직 끝나지 않았다(종료를 요청하지 않는다)" 를 알리는 값입니다. */
    inline constexpr int32 kEngineFrameRunning = -1;

    /** @brief 프레임 시점 하나에 붙는 훅입니다. */
    using EngineFrameDelegate = Delegate<void( const EngineFrameContext& )>;
    /** @brief 종료 코드를 정할 수 있는 시점의 훅입니다. 정할 것이 없으면 `kEngineFrameRunning` 을 돌려줍니다. */
    using EngineFrameExitDelegate = Delegate<int32( const EngineFrameContext& )>;

    /**
     * @struct EngineFrameHooks
     * @brief `Engine::registerFrameHooks` 로 붙이는 훅 묶음입니다. 쓰지 않는 칸은 비워 둔다. 게임 스레드에서만 불립니다.
     * @details 루프는 붙인 순서대로 칸을 부르고, 종료 코드 칸은 처음으로 `kEngineFrameRunning` 이 아닌 값을 낸 것의 코드로 끝냅니다.
     *          훅이 가리키는 객체의 수명은 붙인 쪽이 쥐며, 없애기 전에 `registerFrameHooks` 가 돌려준 구독을 놓습니다.
     */
    struct EngineFrameHooks
    {
        /** @brief 프레임 시작 — 입력 `beginFrame` 전입니다(가상 입력은 여기서 붙인다). */
        EngineFrameDelegate _onFrameBegin;
        /**
         * @brief 씬 틱 뒤 — 씬 틱 · 물리 · 내비 디버그 그리기 뒤, 런타임 UI 갱신 · 렌더 패킷 앞입니다(헤드리스 루프에서는 불리지 않는다).
         * @details 이 호출 자리를 옮기면 훅이 보는 시점(그 프레임의 게임 상태가 정해진 뒤)이 바뀝니다.
         */
        EngineFrameDelegate _onAfterSceneTick;
        /** @brief 프레임 끝 — 씬 틱 · 렌더 제출 뒤, 입력 프레임을 닫기 전입니다(그 프레임의 눌림 엣지가 아직 보인다). 프로세스를 끝낼 종료 코드를 돌려줍니다. */
        EngineFrameExitDelegate _onFrameEnd;
        /** @brief 창이 닫혀 루프가 끝났습니다(App 이 루프 뒤에 한 번 부른다). 코드를 내면 다른 종료 요청(프로파일 세션)보다 이깁니다. */
        EngineFrameExitDelegate _onWindowClosed;
    };
} // namespace sw
