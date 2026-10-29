/**
 * @file StopToken.h
 * @brief 협조 중단 신호 하나입니다 — 올리는 쪽 `StopSource` 와 읽는 쪽 `StopToken`(`std::stop_source` · `std::stop_token` 과 같은 역할).
 * @details 스레드(`sw::thread`), 태스크 주인(편집기 백그라운드 잡 · 씬 로드), 긴 파일 훑기(`FileUtil::collectFiles`)가 같은 형식을 쓴다.
 *          강제 취소가 아니다 — 신호를 읽는 본문이 스스로 일찍 끝난다. 한 번 올리면 내리지 않는다. 칸은 공유 소유라 원천이 먼저 사라져도 토큰이 읽을 수 있다.
 *          (Concurrency 층이라 `Core/Memory` 위의 `sw::make_shared` 대신 `std::shared_ptr` 를 쓴다 — `thread.h` 와 같다.)
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/StdHeaders.h"

#include <memory>

namespace sw
{
    /** @class StopToken @brief 중단 요청을 읽는 쪽입니다. 값으로 넘깁니다. 기본값은 어느 원천에도 묶이지 않아 중단을 받는 일이 없습니다. */
    class StopToken
    {
    public:
        StopToken() noexcept = default;

        /** @brief 중단을 요청받았으면 true 입니다. */
        bool isStopRequested() const noexcept { return _pFlag != nullptr && _pFlag->load( std::memory_order_acquire ); }

    private:
        friend class StopSource;

        explicit StopToken( std::shared_ptr<const atomic<bool>> pFlag ) noexcept
            : _pFlag{ std::move( pFlag ) }
        {
        }

        std::shared_ptr<const atomic<bool>> _pFlag{};
    };

    /** @class StopSource @brief 중단을 요청하는 쪽입니다. 만들 때 칸을 하나 가진다. */
    class StopSource
    {
    public:
        StopSource()
            : _pFlag{ std::make_shared<atomic<bool>>( false ) }
        {
        }

        /** @brief 중단을 요청합니다(내리지 않는다). */
        void requestStop() noexcept
        {
            if ( _pFlag != nullptr )
                _pFlag->store( true, std::memory_order_release );
        }
        /** @brief 중단을 요청했으면 true 입니다. */
        bool isStopRequested() const noexcept { return _pFlag != nullptr && _pFlag->load( std::memory_order_acquire ); }
        /** @brief 이 원천을 읽는 토큰입니다. */
        StopToken getToken() const noexcept { return StopToken{ _pFlag }; }

    private:
        std::shared_ptr<atomic<bool>> _pFlag;
    };
} // namespace sw
