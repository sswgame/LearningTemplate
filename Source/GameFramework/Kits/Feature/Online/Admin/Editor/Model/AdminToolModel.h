/**
 * @file AdminToolModel.h
 * @brief GM 도구 패널의 그리기와 섞여 있던 규칙입니다. 입력 칸 해석, 표 칸의 글, 기간 계산을 화면 없이 합니다(ImGui 없음).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Kits/Feature/Online/Admin/Runtime/AdminProtocol.h"

namespace sw::editor
{
    /**
     * @struct AdminToolModel
     * @brief 파일 머리말을 참고하세요.
     */
    struct AdminToolModel
    {
        /** @brief 편집 칸의 16 진 글을 계정 id 로 바꿉니다. 비었거나 16 진이 아니거나 0 이면 false 입니다. */
        [[nodiscard]] static bool parseAccountID( string_view text, AccountID& outAccountID );
        static string             formatAccountID( AccountID accountID );
        static string             formatBalance( const LedgerBalance& balance );
        /** @brief 유닉스 밀리초의 UTC 시각(`HH:MM:SS`)입니다. */
        static string formatClock( int64 unixMs );
        /** @brief 제재 하나의 상태 글입니다(none, permanent, expired, `N s left`). @p nowMs 는 부르는 쪽의 시계입니다. */
        static string formatSanction( int64 untilMs, int64 nowMs );

        /** @brief 지급 · 회수 칸의 양입니다. 칸은 크기만 받습니다(방향은 누른 단추가 정한다). 0 이하면 false 입니다. */
        [[nodiscard]] static bool isAdjustAmountValid( int64 amount ) { return amount > 0; }
        /** @brief 제재를 거는 기간입니다. 0 은 해제 단추의 뜻이라 거는 단추로 보내지 않습니다. */
        [[nodiscard]] static bool isSanctionDurationValid( int64 seconds ) { return seconds > 0; }
        /**
         * @brief 기간(초)을 끝 시각으로 바꿉니다. 0 은 끝 없음(0)입니다.
         * @return 음수이거나 끝 시각이 int64 를 넘으면 false 입니다. 기간 0 과 같은 뜻으로 보내면 "해제" · "끝날 때까지" 가 되어 GM 이 적은 것과 반대다.
         */
        [[nodiscard]] static bool tryMakeEndMs( int64 nowMs, int64 durationSeconds, int64& outEndMs );

        /** @brief 상태 줄과 로그의 명령 이름입니다. */
        static const utf8* getMethodName( uint16 method );
    };
} // namespace sw::editor
