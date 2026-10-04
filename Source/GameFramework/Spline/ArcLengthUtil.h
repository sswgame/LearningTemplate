/**
 * @file ArcLengthUtil.h
 * @brief 누적 거리 표(샘플 점마다 시작부터의 거리)로 거리 → 구간 · 비율을 찾는 공통 계산입니다.
 * @details 스플라인(`SplinePath`) · 코스터 트랙(`CoasterTrack`)처럼 곡선을 점 목록으로 풀어 들고 "거리 s 의 자리" 를 묻는 곳이 함께 씁니다.
 *          닫힌 곡선은 거리를 한 바퀴로 감고(음수도), 열린 곡선은 양 끝에서 자릅니다. 마지막 점 뒤의 구간은 닫힌 곡선에서만 첫 점으로 이어집니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    /** @brief 거리 하나가 떨어지는 구간입니다 — `_index` 점과 `_nextIndex` 점 사이 `_alpha`(0..1) 자리입니다. */
    struct ArcLengthSpan
    {
        size_t  _index{ 0 };
        size_t  _nextIndex{ 0 };
        float32 _alpha{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ArcLengthUtil
     * @brief 거리 감기 · 구간 찾기입니다. 점 타입은 거리 칸을 가리키는 멤버 포인터로 받습니다(점마다 다른 칸을 들어도 같은 계산).
     */
    struct ArcLengthUtil
    {
        /** @brief @p distance 를 곡선 위 거리로 감습니다(닫힌 곡선) 또는 자릅니다(열린 곡선). 길이가 0 이하면 0 입니다. */
        static float32 wrapDistance( float32 distance, float32 length, bool bClosed )
        {
            if ( length <= 0.0f )
                return 0.0f;
            if ( bClosed == false )
                return MathUtil::clamp( distance, 0.0f, length );
            float32 wrapped = MathUtil::fmod( distance, length );
            if ( wrapped < 0.0f )
                wrapped += length;
            return wrapped;
        }

        /**
         * @brief 이미 감긴 거리 @p wrapped 가 떨어지는 구간입니다. 점은 둘 이상이고 거리는 오름차순이어야 합니다.
         * @details 거리가 @p wrapped 이하인 마지막 점을 이분 탐색으로 찾고, 그 점과 다음 점 사이를 봅니다. 마지막 점 뒤는 닫힌 곡선이면 첫 점으로
         *          (끝 거리는 @p length), 열린 곡선이면 그 점 자신입니다.
         */
        template <typename TPoint>
        static ArcLengthSpan findSpan( const vector<TPoint>& listPoint, float32 TPoint::* pDistance, float32 wrapped, float32 length, bool bClosed )
        {
            ArcLengthSpan span;
            const size_t  pointCount = listPoint.size();
            if ( pointCount < 2 )
                return span;
            size_t lowIndex  = 0;
            size_t highIndex = pointCount - 1;
            while ( lowIndex < highIndex )
            {
                const size_t middleIndex = ( lowIndex + highIndex + 1 ) / 2;
                if ( listPoint[middleIndex].*pDistance <= wrapped )
                    lowIndex = middleIndex;
                else
                    highIndex = middleIndex - 1;
            }
            const bool    bWrapSegment = lowIndex + 1 == pointCount;
            const float32 fromDistance = listPoint[lowIndex].*pDistance;
            const float32 endDistance  = bWrapSegment ? length : listPoint[lowIndex + 1].*pDistance;
            const float32 spanLength   = endDistance - fromDistance;
            span._index                = lowIndex;
            span._nextIndex            = bWrapSegment ? ( bClosed ? 0 : lowIndex ) : lowIndex + 1;
            span._alpha                = spanLength > 1.0e-6f ? MathUtil::clamp( ( wrapped - fromDistance ) / spanLength, 0.0f, 1.0f ) : 0.0f;
            return span;
        }
    };
} // namespace sw
