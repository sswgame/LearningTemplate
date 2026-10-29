/**
 * @file Engine/Module/GameRestarter.h
 * @brief 호스트가 엔진에 내주는 게임 재시작 창구의 인터페이스입니다 — 시나리오 체인이 단계마다 게임의 기동 흐름을 처음부터 다시 돌게 합니다.
 * @details 호스트(App)는 호스트 범위에 이 인터페이스를 구현한 객체를 걸어 둡니다(`GameModuleManager`). 엔진은 게임 인스턴스를 어떻게 만들고 내리는지 모릅니다.
 *          언리얼 PIE 가 플레이마다 새 GameInstance 를 만드는 것과 같다 — 앞 단계의 게임 상태가 다음 단계로 새지 않는다.
 */
#pragma once

namespace sw
{
    /**
     * @class IGameRestarter
     * @brief 게임 인스턴스를 상태 복원 없이 내리고, 활성 씬을 닫은 뒤 새로 만듭니다(프로세스 기동과 같은 빈 자리).
     */
    class IGameRestarter
    {
    public:
        virtual ~IGameRestarter() = default;

        /**
         * @brief 게임을 처음부터 다시 시작합니다. 새 인스턴스의 초기화가 기동 때처럼 첫 씬을 요청하고 · 벤치를 세운다. 게임 스레드에서만 부릅니다.
         * @return 게임 인스턴스가 없었거나 새로 만들지 못했으면 false 입니다(그러면 게임이 없다).
         */
        [[nodiscard]] virtual bool restartGame() = 0;
    };
} // namespace sw
