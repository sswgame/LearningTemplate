/**
 * @file InterpolationBuffer.h
 * @brief 보간 표본 줄입니다 — 틱이 붙은 표본을 틱 순으로 들고, 렌더 틱을 사이에 둔 두 표본과 비율을 찾습니다.
 * @details 구간 규칙은 하나입니다(`InterpolationBracketKind`): 앞 = 렌더 틱 이하의 가장 새 것, 뒤 = 그보다 큰 가장 오래된 것, 비율 = `NetInterpolationUtil::computeAlpha`.
 *          뒤가 없으면 앞에 멈춘다(내다보지 않는다 — 외삽하지 않는다). 앞이 없으면(첫 표본보다 앞) 첫 표본을 두 쪽에 주고 그것을 알린다 — 부르는 쪽이
 *          그것을 그릴지(복제 클라이언트), 그리지 않을지(파괴 덩어리 — 같은 사건으로 혼자 날아가는 중)를 정한다.
 *          성긴 표본(덩어리 자세 — 주기마다 하나 + 멈춤 확정)에 쓴다. 표본이 틱마다 오고 델타 기준도 되는 스냅숏은 `TickRingBuffer` 에 두고 구간만
 *          같은 규칙(`computeAlpha`)으로 고른다 — 성긴 틱을 틱 % 크기 고리에 두면 서로 덮는다.
 *          유니티 Netcode for GameObjects `BufferedLinearInterpolator<T>` 의 자리입니다. 스레드 안전하지 않다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    /** @brief 렌더 틱이 표본들의 어디에 있나입니다. */
    enum class InterpolationBracketKind : uint8
    {
        Empty,       ///< 표본이 없다 — 앞 · 뒤 nullptr
        BeforeFirst, ///< 첫 표본보다 앞 — 앞 = 뒤 = 첫 표본, 비율 0
        Between,     ///< 두 표본 사이(앞 이하 · 뒤 초과) — 비율 0..1
        AfterLast,   ///< 마지막 표본 이후(같은 틱 포함) — 앞 = 뒤 = 마지막 표본, 비율 0
    };
} // namespace sw

namespace sw
{
    /** @brief 보간 구간의 공통 계산입니다(표본을 어디에 두든 같은 식). */
    struct NetInterpolationUtil
    {
        /** @brief 앞 · 뒤 표본 틱 사이에서 렌더 틱의 비율(0..1로 자른다)입니다. 뒤가 앞보다 크지 않으면 0 입니다. */
        static float32 computeAlpha( uint32 fromTick, uint32 toTick, float32 renderTick )
        {
            if ( toTick <= fromTick )
                return 0.0f;
            return MathUtil::saturate( ( renderTick - static_cast<float32>( fromTick ) ) / static_cast<float32>( toTick - fromTick ) );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class InterpolationBuffer
     * @brief 틱 오름차순 표본 줄(틱마다 하나)입니다. `initialize` 의 상한을 넘으면 가장 오래된 것을 버립니다.
     */
    template <typename T>
    class InterpolationBuffer
    {
    public:
        /** @brief 표본 하나입니다. */
        struct Sample
        {
            T      _value{};
            uint32 _tick{ 0 };
        };

        InterpolationBuffer()
            : _listSample{}
            , _capacity{ 0 }
        {
        }

        /** @brief 비우고 상한(1 보다 작으면 1)을 둡니다. */
        void initialize( int32 capacity )
        {
            _listSample.clear();
            _capacity = capacity > 0 ? capacity : 1;
        }
        void clear() { _listSample.clear(); }

        /**
         * @brief 틱 순으로 끼웁니다(늦게 온 옛 표본도 제자리에). 같은 틱이 있으면 @p bReplaceSameTick 일 때만 바꾼다(멈춤 확정이 움직이는 자세를 이긴다).
         *        상한을 넘으면 가장 오래된 것을 버린다.
         * @warning `initialize` 전에는 부르지 않는다(상한 0 — 넣는 즉시 버린다).
         */
        void insert( uint32 tick, const T& value, bool bReplaceSameTick )
        {
            SW_ASSERT( _capacity > 0 );
            auto iter = _listSample.begin();
            while ( iter != _listSample.end() && iter->_tick < tick )
                ++iter;
            if ( iter != _listSample.end() && iter->_tick == tick )
            {
                if ( bReplaceSameTick )
                    iter->_value = value;
            }
            else
            {
                Sample sample;
                sample._value = value;
                sample._tick  = tick;
                _listSample.insert( iter, sample );
            }
            while ( static_cast<int32>( _listSample.size() ) > _capacity )
                _listSample.pop_front();
        }

        /**
         * @brief 렌더 틱을 사이에 둔 두 표본과 비율입니다 — 규칙은 파일 머리말. @p pOutFrom · @p pOutTo 는 다음 `insert` · `removeConsumed` 전까지 유효하다.
         * @return 렌더 틱이 표본들의 어디에 있나입니다.
         */
        InterpolationBracketKind findBracket( float32 renderTick, const Sample*& pOutFrom, const Sample*& pOutTo, float32& outAlpha ) const
        {
            pOutFrom = nullptr;
            pOutTo   = nullptr;
            outAlpha = 0.0f;
            if ( _listSample.empty() )
                return InterpolationBracketKind::Empty;
            const Sample& first = _listSample.front();
            pOutFrom            = &first;
            if ( static_cast<float32>( first._tick ) > renderTick )
            {
                pOutTo = &first;
                return InterpolationBracketKind::BeforeFirst;
            }
            for ( const Sample& sample : _listSample )
            {
                if ( static_cast<float32>( sample._tick ) > renderTick )
                {
                    pOutTo   = &sample;
                    outAlpha = NetInterpolationUtil::computeAlpha( pOutFrom->_tick, sample._tick, renderTick );
                    return InterpolationBracketKind::Between;
                }
                pOutFrom = &sample;
            }
            pOutTo = pOutFrom;
            return InterpolationBracketKind::AfterLast;
        }

        /** @brief 다 쓴 표본을 버립니다 — 렌더 틱 이하는 마지막 하나(보간의 앞)만 남기고, 둘보다 적게는 줄이지 않는다. */
        void removeConsumed( float32 renderTick )
        {
            while ( _listSample.size() > 2 && static_cast<float32>( _listSample[1]._tick ) <= renderTick )
                _listSample.pop_front();
        }

        bool  isEmpty() const { return _listSample.empty(); }
        int32 getCount() const { return static_cast<int32>( _listSample.size() ); }

    private:
        deque<Sample> _listSample; ///< 틱 오름차순, 틱마다 하나
        int32         _capacity;
    };
} // namespace sw
