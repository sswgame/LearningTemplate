/**
 * @file GameCurve.h
 * @brief 시간 → 값 꺾은선 곡선입니다 — 스폰 예산 배율(`SpawnTable`) · 페이싱 단계의 배율(`AiDirectorProfile`)이 같은 것을 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 곡선의 점 하나입니다. */
    struct GameCurvePoint
    {
        float32 _time{ 0.0f };
        float32 _value{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GameCurve
     * @brief 점 사이는 선형, 첫 점 앞 · 끝 점 뒤는 끝 값, 점이 없으면 대체값입니다. 점은 시각 순으로 둡니다(같은 시각은 넣은 순서).
     * @code
     *     <Curve time="0" scale="0.5"/><Curve time="600" scale="2"/>   // curve.readPoints( node, "Curve", "scale", 0.0f )
     * @endcode
     */
    class SW_GF_API GameCurve
    {
    public:
        void clear() { _listPoint.clear(); }
        /** @brief 점 하나를 시각 순 자리에 넣습니다. */
        void addPoint( float32 time, float32 value );
        /** @brief @p node 의 @p pElement 자식마다 `time` 과 @p pValueAttribute 를 읽어 넣습니다(값은 @p minValue 아래로 내려가지 않는다). 읽은 점 수입니다. */
        uint32 readPoints( const XMLNode& node, const utf8* pElement, const utf8* pValueAttribute, float32 minValue );
        /** @brief @p time 의 값입니다. 점이 없으면 @p fallback 입니다. */
        float32 evaluate( float32 time, float32 fallback = 1.0f ) const;

        bool                          isEmpty() const { return _listPoint.empty(); }
        const vector<GameCurvePoint>& getPoints() const { return _listPoint; }

    private:
        vector<GameCurvePoint> _listPoint{}; ///< 시각 순
    };
} // namespace sw
