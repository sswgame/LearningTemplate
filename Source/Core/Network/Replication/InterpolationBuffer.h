/**
 * @file InterpolationBuffer.h
 * @brief 보간 표본 큐를 구현합니다. 틱이 붙은 표본을 틱 순서로 보관하고, 렌더 틱을 사이에 둔 두 표본과 비율을 찾습니다.
 * @details 구간 규칙은 하나다(`InterpolationBracketKind`). 앞은 렌더 틱 이하의 가장 새 표본, 뒤는 그보다 큰 가장 오래된 표본이고, 비율은 `NetSampleRatio` 가 든다.
 *          뒤가 없으면 앞에 멈춘다(내다보지 않으므로 외삽하지 않는다).
 *          앞이 없으면(첫 표본보다 앞) 첫 표본을 앞과 뒤 양쪽에 주고 그 사실을 알린다. 그릴지 말지는 부르는 쪽이 정한다.
 *          복제 클라이언트는 그리고, 파괴 덩어리(같은 사건으로 혼자 날아가는 중)는 그리지 않는다.
 *          성긴 표본(덩어리 자세, 주기마다 하나와 멈춤 확정)에 쓴다. 표본이 틱마다 오고 델타 기준도 되는 스냅샷은 `TickRingBuffer` 에 두고,
 *          구간만 같은 규칙(`NetSampleRatio`)으로 고른다. 성긴 틱을 틱 % 크기 링에 두면 서로 덮어쓰기 때문이다.
 *          유니티 Netcode for GameObjects 의 `BufferedLinearInterpolator<T>` 에 해당한다. 스레드 안전하지 않다.
 */
#pragma once
#include "Core/Container/deque.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    /** @brief 렌더 틱이 표본들의 어디에 있는지 나타냅니다. */
    enum class InterpolationBracketKind : uint8
    {
        Empty,       ///< 표본이 없다. 앞과 뒤가 모두 nullptr 이다.
        BeforeFirst, ///< 첫 표본보다 앞이다. 앞과 뒤가 모두 첫 표본이다.
        Between,     ///< 두 표본 사이다(앞 이하, 뒤 초과). 비율은 0~1이다.
        AfterLast,   ///< 마지막 표본 이후다(같은 틱 포함). 앞과 뒤가 모두 마지막 표본이다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetSampleRatio
     * @brief 앞뒤 두 표본 사이의 한 시각입니다. 칸마다의 규칙(잇는 칸 · 이산 칸 · 있고 없음 · 순간 이동)을 이 형식이 정합니다.
     * @details 비율을 그대로 내주면 쓰는 곳마다 규칙이 갈린다 — 자리는 잇고 생사 · 무기는 뒤 표본을 바로 써서 아직 살아 있는 자리에 시체가 서거나, 서버의 되감기와 클라이언트의 화면이
     *          다른 규칙을 썼다. 그래서 비율은 꺼내지 못하고 아래 연산만 있다: 잇는 칸은 `interpolate`, 이산 칸은 `choose`(뒤 표본의 시각이 되어야 바뀐다), 있고 없음은 `isPresent`(같은 규칙).
     *          두 표본이 이어지지 않으면(순간 이동 · 다시 태어남, `breakAt`) 잇는 칸도 이산 칸처럼 바뀌고 속도는 0 이다. 유니티 Netcode `SmoothingAction`(Interpolate · Clamp) ·
     *          `[GhostField]` 최대 보간 거리, Valve 랙 보정의 `LC_ALIVE` · 순간 이동 중단과 같은 자리.
     */
    class NetSampleRatio
    {
    public:
        /** @brief 앞뒤 표본 틱 사이에서 @p tick 의 자리입니다(양 끝으로 자른다). 뒤가 앞보다 크지 않으면 앞 표본 그대로입니다. */
        static NetSampleRatio make( uint32 fromTick, uint32 toTick, float64 tick )
        {
            if ( toTick <= fromTick )
                return NetSampleRatio{ 0.0f, 0.0f };
            // 틱 차이를 먼저 뺀다(float64) — 큰 틱끼리의 float32 뺄셈은 비율을 잃는다(감사 N-4).
            const float64 span = static_cast<float64>( toTick - fromTick );
            return NetSampleRatio{ MathUtil::saturate( static_cast<float32>( ( tick - static_cast<float64>( fromTick ) ) / span ) ), static_cast<float32>( span ) };
        }
        /** @brief 표본 하나 그대로입니다(뒤 표본이 없다). */
        static NetSampleRatio makeHeld() { return NetSampleRatio{ 0.0f, 0.0f }; }

        /** @brief 두 표본의 순간 이동 번호가 다르면 잇지 않습니다 — 뒤 표본의 시각이 되어야 옮겨진다. */
        void breakAt( uint32 fromTeleportCount, uint32 toTeleportCount ) { _bContinuous = _bContinuous && fromTeleportCount == toTeleportCount; }

        float32    interpolate( float32 from, float32 to ) const { return from + ( to - from ) * getBlend(); }
        float3     interpolate( const float3& from, const float3& to ) const { return from + ( to - from ) * getBlend(); }
        quaternion interpolate( const quaternion& from, const quaternion& to ) const { return quaternion::slerp( from, to, getBlend() ); }
        /** @brief 각도(라디안)를 가까운 쪽으로 잇습니다. */
        float32 interpolateAngle( float32 from, float32 to ) const { return MathUtil::wrapAngle( from + MathUtil::wrapAngle( to - from ) * getBlend() ); }
        /** @brief 이산 칸입니다 — 뒤 표본의 시각이 되어야 바뀝니다. */
        template <typename T>
        const T& choose( const T& from, const T& to ) const
        {
            return _ratio >= 1.0f ? to : from;
        }
        /** @brief 그 시각에 있는가입니다 — 생겨남 · 사라짐도 이산 칸이다. */
        bool isPresent( bool bInFrom, bool bInTo ) const { return choose( bInFrom, bInTo ); }
        /** @brief 두 자리 사이의 속도(초당)입니다. 멈춘 표본이거나 이어지지 않으면 0 입니다 — 화면 자리의 차이로 재면 첫 프레임 · 순간 이동에 튄다. */
        float3 computeVelocity( const float3& from, const float3& to, float32 tickInterval ) const
        {
            return _bContinuous && _spanTicks > 0.0f && tickInterval > 0.0f ? ( to - from ) * ( 1.0f / ( _spanTicks * tickInterval ) ) : float3{ 0.0f, 0.0f, 0.0f };
        }

    private:
        NetSampleRatio( float32 ratio, float32 spanTicks )
            : _ratio{ ratio }
            , _spanTicks{ spanTicks }
            , _bContinuous{ true }
        {
        }

        float32 getBlend() const { return _bContinuous ? _ratio : choose( 0.0f, 1.0f ); }

    private:
        float32 _ratio;
        float32 _spanTicks;   ///< 두 표본 사이의 틱 수. 멈춘 표본이면 0
        bool    _bContinuous; ///< NOLINT(sw-bool-flag): 값 형식이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class InterpolationBuffer
     * @brief 틱 오름차순 표본 큐입니다(틱마다 하나). `initialize` 의 상한을 넘으면 가장 오래된 표본을 버립니다.
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

        /** @brief 비우고 상한을 정합니다(1보다 작으면 1). */
        void initialize( int32 capacity )
        {
            _listSample.clear();
            _capacity = capacity > 0 ? capacity : 1;
        }
        void clear() { _listSample.clear(); }

        /**
         * @brief 틱 순서에 맞는 위치에 끼워 넣습니다(늦게 온 옛 표본도 제자리에 들어갑니다). 같은 틱이 있으면 @p bReplaceSameTick 일 때만 바꿉니다(멈춤 확정이 움직이는 자세를 이깁니다).
         *        상한을 넘으면 가장 오래된 표본을 버립니다.
         * @warning `initialize` 전에는 부르지 않는다(상한이 0이라 넣자마자 버려진다).
         * @warning `initialize` 전에는 부르지 않는다(상한 0 — 넣는 즉시 버린다).
         */
        void insert( uint32 tick, const T& value, bool bReplaceSameTick )
        {
            SW_ASSERT( _capacity > 0 );
            auto iter = _listSample.begin();
            while ( iter != _listSample.end() && iter->_tick < tick )
            {
                ++iter;
            }
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
            {
                _listSample.pop_front();
            }
        }

        /**
         * @brief 렌더 틱을 사이에 둔 두 표본과 비율을 찾습니다. 규칙은 파일 머리말을 참고하세요. @p pOutFrom 과 @p pOutTo 는 다음 `insert` 나 `removeConsumed` 전까지 유효합니다.
         * @return 렌더 틱이 표본들의 어디에 있는지 반환합니다.
         */
        InterpolationBracketKind findBracket( float64 renderTick, const Sample*& pOutFrom, const Sample*& pOutTo, NetSampleRatio& outRatio ) const
        {
            pOutFrom = nullptr;
            pOutTo   = nullptr;
            outRatio = NetSampleRatio::makeHeld();
            if ( _listSample.empty() )
                return InterpolationBracketKind::Empty;
            const Sample& first = _listSample.front();
            pOutFrom            = &first;
            if ( static_cast<float64>( first._tick ) > renderTick )
            {
                pOutTo = &first;
                return InterpolationBracketKind::BeforeFirst;
            }
            for ( const Sample& sample : _listSample )
            {
                if ( static_cast<float64>( sample._tick ) > renderTick )
                {
                    pOutTo   = &sample;
                    outRatio = NetSampleRatio::make( pOutFrom->_tick, sample._tick, renderTick );
                    return InterpolationBracketKind::Between;
                }
                pOutFrom = &sample;
            }
            pOutTo = pOutFrom;
            return InterpolationBracketKind::AfterLast;
        }

        /** @brief 다 쓴 표본을 버립니다. 렌더 틱 이하는 마지막 하나(보간의 앞)만 남기며, 두 개보다 적게는 줄이지 않습니다. */
        void removeConsumed( float64 renderTick )
        {
            while ( _listSample.size() > 2 && static_cast<float64>( _listSample[1]._tick ) <= renderTick )
            {
                _listSample.pop_front();
            }
        }

        bool  isEmpty() const { return _listSample.empty(); }
        int32 getCount() const { return static_cast<int32>( _listSample.size() ); }

    private:
        deque<Sample> _listSample; ///< 틱 오름차순이며 틱마다 하나다.
        int32         _capacity;
    };
} // namespace sw
