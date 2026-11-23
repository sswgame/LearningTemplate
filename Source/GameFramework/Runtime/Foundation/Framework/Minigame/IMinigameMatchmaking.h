/**
 * @file IMinigameMatchmaking.h
 * @brief 매치형 미니게임(`MinigameDefinition::isMatch`)의 매칭 창구입니다 — 게임의 온라인 흐름이 구현해 게임 범위에 겁니다.
 * @details 미니게임 흐름(기반)은 매칭 · 순위표 키트를 모른다. 입구가 매치형을 열면 흐름이 이 창구로 줄을 서고, 구현은 배정을 받아 허브 세션을 떠나 매치 세션으로 옮긴 뒤
 *          `MinigameFlow::enterMatch` 로 들어간다. 줄 · 배정 · 접속 허가는 서버(매칭 키트)가 판정한다 — 클라이언트는 요청만 한다.
 *          언리얼 Lyra 의 매치메이킹 → `ClientTravel`(매치 서버로 옮겨 가고 끝나면 프런트엔드로 돌아온다)과 같은 자리다. 점수는 매치 서버가 순위표에 낸다(`MinigameFlow::findMatchScore`).
 */
#pragma once

namespace sw
{
    class GameObject;
    struct MinigameDefinition;

    /** @class IMinigameMatchmaking @brief 매치형 미니게임의 매칭 창구입니다. 게임 스레드에서 부릅니다. 없는 게임은 매치형 미니게임에 들어가지 못합니다(오류). */
    class IMinigameMatchmaking
    {
    public:
        virtual ~IMinigameMatchmaking() = default;

        /** @brief @p definition 의 매칭 모드에 줄을 섭니다. 로그인 전이거나 이미 줄 · 매치에 있으면 알리고 false 입니다. 배정되면 구현이 `MinigameFlow::enterMatch` 를 부릅니다. */
        [[nodiscard]] virtual bool requestMatch( const MinigameDefinition& definition ) = 0;
        /** @brief @p activator 가 이 프로세스의 플레이어가 조종하는 폰이면 true 입니다 — 접속한 클라이언트 월드에서 남의 복제 폰이 입구에 닿은 것을 거른다. */
        [[nodiscard]] virtual bool isLocalPlayer( const GameObject& activator ) const = 0;
    };
} // namespace sw
