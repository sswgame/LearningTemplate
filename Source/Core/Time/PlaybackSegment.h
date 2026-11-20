/**
 * @file PlaybackSegment.h
 * @brief 재생 커서가 갱신 한 번에 지난 구간과 문턱 판정(`PlaybackSegment`)입니다 — 시퀀서(정수 프레임) · 애니메이션 알림(초)이 같은 규칙을 씁니다.
 */
#pragma once

namespace sw
{
    /**
     * @struct PlaybackSegment
     * @brief 갱신 하나가 지난 구간입니다. `_from` 은 이미 본 위치, `_to` 는 이번에 닿은 위치이고 방향은 둘의 크기가 정합니다.
     * @details 발화 규칙: 정방향(`_from < _to`)은 `_from < t <= _to`, 역방향은 `_to <= t < _from` 인 문턱 @p t 를 지난다.
     *          반복이 되감거나 왕복이 꺾이는 끝은 꺾인 뒤 구간의 `_from` 이라 다시 지나지 않는다 — 같은 문턱을 한 번 지나면 한 번(언리얼 Sequencer 의 평가 범위와 같은 열림 · 닫힘).
     *          처음 위치 자체를 넣는 첫 걸음은 부르는 쪽이 `_from` 을 처음 위치 하나 바깥에 두거나(정수 프레임) 따로 견준다(초 — `AnimationTimeStep::_bIncludesStart`).
     * @tparam T 위치의 형식입니다(정수 프레임 · 초).
     */
    template <typename T>
    struct PlaybackSegment
    {
        T _from{};
        T _to{};

        /** @brief 정방향 구간이면 true 입니다. 빈 구간(`_from == _to`)은 어느 쪽도 아니다. */
        [[nodiscard]] constexpr bool isForward() const { return _from < _to; }
        /** @brief 이 구간을 지나며 문턱 @p threshold 를 넘었으면 true 입니다(머리말의 발화 규칙). 빈 구간은 아무것도 지나지 않는다. */
        [[nodiscard]] constexpr bool hasCrossed( T threshold ) const
        {
            return _from < _to ? ( _from < threshold && threshold <= _to ) : ( _to <= threshold && threshold < _from );
        }
    };
} // namespace sw
