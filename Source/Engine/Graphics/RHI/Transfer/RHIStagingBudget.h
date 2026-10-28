/**
 * @file RHIStagingBudget.h
 * @brief 스테이징 메모리를 드라이버가 따로 잡아 주는 백엔드(DX11)의 업로드 스테이징 바이트 예산입니다. 구간 장부(`RHIStagingAllocator`)의 "총량만" 판입니다.
 * @details 장부는 링 하나 안의 오프셋을 나눠 주지만 DX11 은 업로드마다 스테이징 텍스처를 새로 만들어 나눌 메모리 덩어리가 없다. 오프셋을 흉내 내면 드라이버가 받아 줄
 *          업로드를 조각 남음 때문에 거절하므로, 복사 중인 바이트의 합만 센다. 장부와 같은 것은 두 규칙이다: 예산보다 큰 한 장은 아무것도 복사 중이지 않을 때 예산 전부를
 *          잡고 혼자 들어가고, 못 받은 그런 요청은 예약을 남겨 작은 업로드가 끊이지 않아 큰 한 장이 굶는 것을 막는다.
 *          시계는 호출자가 넘긴다 — 시간 규칙을 GPU 없이 결정적으로 시험하려는 것이다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class RHIStagingBudget
     * @brief 복사 중인 스테이징 바이트의 합을 예산 이하로 지킵니다. 잠금이 없고 어느 스레드에서나 부릅니다.
     * @details `tryReserve` 가 잡은 바이트는 같은 값으로 `release` 에 한 번만 돌려줍니다(`D3D11RHIDevice::UploadStaging` 이 소멸자에서 돌려준다).
     */
    class SW_API RHIStagingBudget
    {
    public:
        /// @brief 예산을 통째로 잡아야 하는 요청이 못 받은 뒤 새 작은 요청을 막는 시간의 기본값(마이크로초)입니다. 장부의 `kSoloReservationMicroseconds` 와 같은 추정이다.
        static constexpr int64 kWholeReservationMicroseconds = 250 * 1000;

        RHIStagingBudget();

        RHIStagingBudget( const RHIStagingBudget& )            = delete;
        RHIStagingBudget& operator=( const RHIStagingBudget& ) = delete;

        /**
         * @brief 예산과 예약 시간을 정하고 복사 중인 합을 0 으로 되돌립니다. 다른 스레드가 쓰기 전에 한 번 부릅니다.
         * @param budgetBytes 복사 중인 스테이징 바이트 합의 상한입니다. 0 보다 커야 합니다.
         * @param wholeReservationMicroseconds 예산 전부를 잡아야 하는 요청이 못 받았을 때 새 작은 요청을 막는 시간입니다. 0 이면 예약하지 않습니다.
         */
        void initialize( uint64 budgetBytes, int64 wholeReservationMicroseconds = kWholeReservationMicroseconds );

        /**
         * @brief @p requestBytes 를 예산에서 잡습니다. 예산보다 큰 요청은 예산 전부를 잡습니다.
         * @param nowMicroseconds 지금 시각입니다(`MonotonicClock::nowMicroseconds`). 예약이 끝났는지 가립니다.
         * @param outReservedBytes 잡았으면 잡은 바이트이고 `release` 에 그대로 돌려줍니다(요청이 0 이면 0). 못 잡았으면 0 입니다.
         * @return 잡았으면 true 입니다. false 는 지금 자리가 없다는 뜻이고 복사 중인 업로드가 끝나면 생깁니다(부르는 쪽은 `Busy`).
         * @details 예산 전부를 잡는 요청(`requestBytes` ≥ 예산)은 복사 중인 것이 하나도 없을 때만 받고, 못 받으면 예약을 남깁니다. 예약이 살아 있는 동안
         *          더 작은 요청은 자리가 있어도 못 받아 복사 중인 것이 비게 합니다. 큰 요청이 다시 올 때마다 예약이 연장되고 다시 오지 않으면(호출자가 포기) 끝납니다.
         */
        [[nodiscard]] bool tryReserve( uint64 requestBytes, int64 nowMicroseconds, uint64& outReservedBytes );

        /** @brief `tryReserve` 가 준 바이트를 돌려줍니다. 잡은 것보다 많이 돌려주지 않습니다. */
        void release( uint64 reservedBytes );

        /** @brief 복사 중인 스테이징 바이트의 합입니다. */
        [[nodiscard]] uint64 getInFlightBytes() const { return _inFlightBytes.load( std::memory_order_acquire ); }
        /** @brief 예산(바이트)입니다. */
        [[nodiscard]] uint64 getBudgetBytes() const { return _budgetBytes; }

    private:
        atomic<uint64> _inFlightBytes;                ///< 복사 중인 스테이징 바이트의 합. `_budgetBytes` 를 넘지 않는다
        atomic<int64>  _reservedUntilMicroseconds;    ///< 이 시각 전까지 예산을 통째로 잡지 않는 요청은 못 받는다. 0 이면 예약이 없다
        uint64         _budgetBytes;                  ///< `initialize` 가 정한다. 그 뒤로 읽기만 한다
        int64          _wholeReservationMicroseconds; ///< `initialize` 가 정한다. 그 뒤로 읽기만 한다
    };
} // namespace sw
