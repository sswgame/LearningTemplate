/**
 * @file OnlineServerKitName.h
 * @brief 온라인 서버 키트 이름입니다. 설정 파일(`_listKit[]._name`) · 로그 · 의존 칸과, 키트가 내놓는 인터페이스의 제공 키트 상수(`kProviderKit`)가 같은 상수를 씁니다.
 * @details 서버 전용 폴더(`Server/`) 밖에 둔다 — 공유 헤더의 인터페이스(`ILedgerPolicy`)도 제공 키트 상수를 들어야 하기 때문이다(이름 상수뿐이라 클라이언트에 새는 것이 없다).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @brief 키트 이름입니다. 의존 칸은 이 상수를 인터페이스 형식의 `kProviderKit` 으로만 읽어, 철자가 틀리거나 형식과 이름이 어긋나면 컴파일 오류입니다.
     * @details 키트끼리는 include 하지 않으므로 이름만 기반에 둡니다(언리얼 서브시스템의 클래스 의존과 같은 자리).
     */
    struct OnlineServerKitName
    {
        static constexpr const utf8* kBase        = "onlineBase"; ///< 런타임의 공통 기반(저장소 · 캐시 · 호스트). 모든 키트가 이것 뒤에 선다
        static constexpr const utf8* kAccount     = "account";    ///< 신원, 접속 상태, 세션 끊기, 이름 찾기
        static constexpr const utf8* kGameConnect = "gameConnect";
        static constexpr const utf8* kEconomy     = "economy"; ///< 원장 정책(화폐 상한), 잔액 변경 알림
        static constexpr const utf8* kSocial      = "social";  ///< 차단 목록
        static constexpr const utf8* kDirectory   = "directory";
        static constexpr const utf8* kChat        = "chat";
        static constexpr const utf8* kLeaderboard = "leaderboard";
        static constexpr const utf8* kMatchmaking = "matchmaking";
        static constexpr const utf8* kLiveOps     = "liveops";
        static constexpr const utf8* kMailbox     = "mailbox";
        static constexpr const utf8* kTrade       = "trade";
        static constexpr const utf8* kAdmin       = "admin";
    };
} // namespace sw
