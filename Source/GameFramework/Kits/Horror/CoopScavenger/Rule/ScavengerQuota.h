/**
 * @file ScavengerQuota.h
 * @brief 할당량 주기 — 마감까지 남은 날 · 이번 할당량 · 채운 양, 마감 판정(채우면 다음 할당량 · 보너스, 못 채우면 게임 오버)입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Horror/CoopScavenger/Catalog/ScavengerCatalog.h"

namespace sw
{
    class Archive;
    class GameRandom;

    /** @brief 마감 판정 결과입니다. */
    enum class ScavengerDeadlineResult : uint8
    {
        NotDue = 0, ///< 아직 마감 날이 아니다 — 하루가 줄었다
        QuotaMet,   ///< 다음 주기로 — 할당량이 오르고 날이 다시 찬다
        GameOver    ///< 못 채웠다
    };

    /**
     * @class ScavengerQuota
     * @brief 리썰 컴퍼니의 할당량입니다. 다음 할당량 = 지금 + 기본 × (1 + 주기² / 가파름) × (1 + 무작위 × (r − 0.5)) 를 반올림한 것입니다
     *        (주기 = 채운 할당량 수 — 갈수록 가파르게 오른다). 남은 날이 0 인 날이 마감 날이고, 그날이 끝날 때(`endDay`) 판정합니다.
     */
    class SW_GF_API ScavengerQuota
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "SCQT" );
        static constexpr uint32 kStateVersion = 1;

        ScavengerQuota();

        void initialize( const ScavengerQuotaSettings& settings );
        /** @brief 회사에 판 값을 더합니다. */
        void addFulfilled( int32 value );
        /** @brief 하루를 끝냅니다 — 마감 날이면 판정하고, 아니면 남은 날을 하나 줄입니다. @p outOvertimeBonus 는 넘긴 만큼의 보너스입니다. */
        ScavengerDeadlineResult endDay( GameRandom& random, int32& outOvertimeBonus );
        /** @brief 주기 @p cycleIndex(0 부터) 를 채운 뒤의 늘어남입니다(무작위 r 을 넘긴다 — 시험 · 화면 예고). */
        int32 computeIncrease( int32 cycleIndex, float32 randomValue ) const;

        /** @brief 할당량 · 채운 양 · 남은 날 · 주기 · 게임 오버를 씁니다. 설정은 `initialize` 의 것이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        int32 getQuota() const { return _quota; }
        int32 getFulfilled() const { return _fulfilled; }
        int32 getDaysLeft() const { return _daysLeft; }
        int32 getCycle() const { return _cycle; }
        bool  isMet() const { return _fulfilled >= _quota; }
        bool  isGameOver() const { return _bGameOver == SW_TRUE; }

    private:
        ScavengerQuotaSettings _settings;
        int32                  _quota;
        int32                  _fulfilled;
        int32                  _daysLeft;
        int32                  _cycle;
        uint8                  _bGameOver;
    };
} // namespace sw
