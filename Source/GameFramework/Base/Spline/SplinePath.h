/**
 * @file SplinePath.h
 * @brief 스플라인 곡선 — 조절점(Catmull-Rom · 3차 베지어 · 꺾은선), 호 길이 매개변수, 가장 가까운 점, 고른 간격 샘플입니다.
 * @details 움직이는 발판 · 카메라 레일 · 길 · 레일 그라인드처럼 "곡선 위 거리 s 의 자리" 를 묻는 곳이 함께 씁니다. 거리는 조밀한 샘플 점의
 *          누적 현 길이 표(`ArcLengthUtil`)로 곡선 매개변수(구간 + t)로 바뀌고, 자리 · 접선은 그 매개변수에서 곡선 식으로 다시 구합니다
 *          (샘플 사이 직선 보간이 아니라 곡선 위). 같은 입력이면 같은 결과입니다(결정적 — 고정 스텝 무버가 기댄다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 조절점을 곡선으로 잇는 방식입니다. */
    ENUM()
    enum class SplineType : uint8
    {
        CatmullRom = 0, ///< 모든 조절점을 지난다(이웃 점으로 접선을 정한다)
        Bezier,         ///< 3차 베지어 — 점 열은 [끝점, 손잡이, 손잡이, 끝점, 손잡이, 손잡이, 끝점 …]. 열린 곡선은 3n+1 개, 닫힌 곡선은 3n 개
        Linear          ///< 꺾은선
    };
} // namespace sw

namespace sw
{
    /** @brief 곡선 위 한 자리입니다. */
    struct SplineSample
    {
        float3  _position{};
        float3  _tangent{ 0.0f, 0.0f, 1.0f }; ///< 단위 접선(진행 방향)
        float32 _distance{ 0.0f };            ///< 시작부터의 호 길이(감긴 값)
    };
} // namespace sw

namespace sw
{
    /**
     * @class SplinePath
     * @brief 조절점 목록으로 지은 곡선과 그 호 길이 표입니다. 좌표계는 넣은 점의 것 그대로입니다(로컬이든 월드든).
     */
    class SW_GF_API SplinePath
    {
    public:
        /** @brief 호 길이 표의 점 하나입니다 — 곡선 매개변수(`_parameter` = 구간 번호 + t)와 시작부터의 누적 거리입니다. */
        struct TablePoint
        {
            float3  _position{};
            float32 _parameter{ 0.0f };
            float32 _distance{ 0.0f };
        };

        SplinePath();

        /**
         * @brief 곡선을 짓습니다. 점이 모자라면(Catmull-Rom · 꺾은선은 둘, 베지어는 열린 곡선 4 · 닫힌 곡선 3, 베지어 개수 규칙 어긋남) false 이고 비어 있습니다.
         * @param samplesPerSegment 구간마다 호 길이 표에 넣는 샘플 수(1 이상). 많을수록 길이가 정확합니다.
         */
        [[nodiscard]] bool initialize( const vector<float3>& listControlPoint, SplineType type, bool bClosed, int32 samplesPerSegment = 16 );
        /** @brief 비웁니다. */
        void clear();

        /** @brief 거리 @p distance 의 자리 · 접선입니다(닫힌 곡선은 감기고, 열린 곡선은 잘립니다). */
        SplineSample sampleAtDistance( float32 distance ) const;
        /** @brief 정규화 거리(0..1 = 처음..끝)의 자리입니다. */
        SplineSample sampleAtFraction( float32 fraction ) const { return sampleAtDistance( fraction * _length ); }
        /**
         * @brief @p point 에 가장 가까운 곡선 위 자리입니다. 표의 구간마다 투영해 가장 가까운 것을 고른 뒤 그 매개변수에서 곡선 위 점으로 다시 구합니다.
         * @return 그 자리(`_distance` 가 곡선 위 거리). 곡선이 비었으면 기본값입니다.
         */
        SplineSample findClosest( const float3& point ) const;
        /** @brief 처음부터 끝까지 @p spacing(m) 간격의 자리를 @p outListSample 에 채웁니다(끝점 포함). 길이 · 간격이 0 이하면 비웁니다. */
        void sampleUniform( float32 spacing, vector<SplineSample>& outListSample ) const;
        /** @brief 곡선 매개변수(구간 + t)의 자리입니다. */
        float3 evaluate( float32 parameter ) const;
        /** @brief 곡선 매개변수의 접선(정규화 전 미분)입니다. */
        float3 evaluateDerivative( float32 parameter ) const;
        /** @brief 거리를 곡선 위로 감거나 자릅니다. */
        float32 wrapDistance( float32 distance ) const;

        bool                      isValid() const { return _listTablePoint.size() >= 2; }
        bool                      isClosed() const { return _bClosed == SW_TRUE; }
        float32                   getLength() const { return _length; }
        SplineType                getType() const { return _type; }
        int32                     getSegmentCount() const { return _segmentCount; }
        const vector<float3>&     getControlPoints() const { return _listControlPoint; }
        const vector<TablePoint>& getTablePoints() const { return _listTablePoint; }

    private:
        /** @brief 매개변수를 구간 번호와 그 안의 t 로 나눕니다. */
        void    splitParameter( float32 parameter, int32& outSegment, float32& outT ) const;
        float3  getControlPointWrapped( int32 index ) const;
        float32 findParameterAtDistance( float32 wrappedDistance ) const;

        vector<float3>     _listControlPoint;
        vector<TablePoint> _listTablePoint;
        float32            _length;
        int32              _segmentCount;
        SplineType         _type;
        uint8              _bClosed;
    };
} // namespace sw
