/**
 * @file AutomationStageFingerprint.h
 * @brief 자동화 단계가 시작하는 순간의 프로세스 상태 지문입니다 — 앞 단계가 무엇을 남겼는지 이름으로 냅니다.
 * @details 예전에는 남은 상태가 다음 단계의 골든이 우연히 갈릴 때만 보였다(자막 · 알림 · 멈춘 시간). 새는 순간, 새는 것의 이름으로 진다(`AutomationFailureKind::StageStateLeak`).
 *          게임 인스턴스와 씬이 내려가고 새 인스턴스가 서기 전에 찍는다 — 새 게임이 정당하게 만든 것과 섞이지 않는다. 언리얼 Gauntlet 이 테스트마다 새 프로세스를 띄우는 자리다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 지문의 한 칸입니다. */
    struct AutomationStageFingerprintEntry
    {
        string           _name;  ///< 무엇인지(`GameTimeScale pause requests`)
        string           _state; ///< 남은 상태의 이름(수 칸이면 빈 글) — 앞 지문에 없던 것이면 샌 것이다
        optional<uint64> _count; ///< 수 칸의 값 — 앞 지문보다 늘었으면 샌 것이다(앞 지문에 없으면 처음 센 값이라 기준이 된다)
    };

    /**
     * @struct AutomationStageFingerprint
     * @brief 지문 하나입니다(칸은 찍은 순서).
     */
    struct AutomationStageFingerprint
    {
        vector<AutomationStageFingerprintEntry> _listEntry;

        /**
         * @brief 지금 프로세스 상태를 찍습니다. 게임 스레드에서, 게임 인스턴스가 없을 때 부릅니다.
         * @param bCountObjects 구독 · 오브젝트 수를 함께 센다 — 에디터가 뜬 실행은 에디터의 것이 섞이고, 첫 단계 앞은 처음 돌 때 서는 엔진 쪽 구독이 아직 없어 세지 않는다.
         */
        [[nodiscard]] static AutomationStageFingerprint capture( bool bCountObjects );
        /** @brief 앞 지문 @p baseline 뒤로 @p current 에 샌 것을 한 줄에 하나씩(`UISubtitles: subtitles`, `EventDispatcher subscribers: 12 -> 13`) 채웁니다. 줄어든 것은 새지 않은 것이다. */
        [[nodiscard]] static vector<string> describeLeaks( const AutomationStageFingerprint& baseline, const AutomationStageFingerprint& current );
        /** @brief 이름 @p name 의 칸입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const AutomationStageFingerprintEntry* findEntry( string_view name ) const;
    };
} // namespace sw
