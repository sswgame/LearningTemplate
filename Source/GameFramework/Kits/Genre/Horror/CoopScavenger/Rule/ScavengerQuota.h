/**
 * @file ScavengerQuota.h
 * @brief 할당량 주기를 구현합니다. 마감까지 남은 날, 이번 할당량, 채운 양을 관리하고 마감을 판정합니다(채우면 다음 할당량과 보너스, 못 채우면 게임 오버).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Catalog/ScavengerCatalog.h"

namespace sw
{
    class Archive;
    class GameRandom;

    /** @brief 마감 판정의 결과입니다. */
    enum class ScavengerDeadlineResult : uint8
    {
        NotDue = 0, ///< 아직 마감 날이 아니며 하루가 줄었다
        QuotaMet,   ///< 다음 주기로 넘어가며, 할당량이 오르고 날이 다시 찬다
        GameOver    ///< 못 채웠다
    };

    /**
     * @class ScavengerQuota
     * @brief 리썰 컴퍼니의 할당량입니다. 다음 할당량은 지금 값 + 기본 x (1 + 주기^2 / 가파름) x (1 + 무작위 x (r - 0.5))를 반올림한 값입니다
     *        (주기는 채운 할당량 수이며 갈수록 가파르게 오릅니다). 남은 날이 0 인 날이 마감 날이고, 그날이 끝날 때(`endDay`) 판정합니다.
     */
    class SW_KIT_API ScavengerQuota
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "SCQT" );
        static constexpr uint32 kStateVersion = 1;

        ScavengerQuota();

        void initialize( const ScavengerQuotaSettings& settings );
        /** @brief 회사에 판 값을 더합니다. */
        void addFulfilled( int32 value );
        /** @brief 하루를 끝냅니다. 마감 날이면 판정하고, 아니면 남은 날을 하나 줄입니다. @p outOvertimeBonus 는 넘긴 만큼의 보너스입니다. */
        ScavengerDeadlineResult endDay( GameRandom& random, int32& outOvertimeBonus );
        /** @brief 주기 @p cycleIndex(0부터)를 채운 뒤의 늘어나는 양을 계산합니다. 무작위 값 r 은 호출하는 쪽이 넘깁니다(테스트와 화면 예고용). */
        int32 computeIncrease( int32 cycleIndex, float32 randomValue ) const;

        /** @brief 할당량, 채운 양, 남은 날, 주기, 게임 오버를 씁니다. 설정은 `initialize` 에서 받으므로 저장하지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
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
