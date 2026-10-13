/**
 * @file PositionWelder.h
 * @brief 허용 오차 안의 위치를 처음 본 위치 하나로 합치는(weld) 격자 용접기입니다. 메시 정점 합치기의 정본입니다.
 * @details 위치를 차례로 넣으면 대표 점 번호를 돌려줍니다. 오차 안에 이미 넣은 점이 있으면 그 번호, 없으면 새 번호입니다.
 *          격자 셀(한 변이 허용 오차)과 이웃 26 셀을 함께 봐서 셀 경계에 걸친 두 점도 합칩니다.
 *
 *          **결정성.** 같은 위치를 같은 순서로 넣으면 번호와 대표 점이 언제나 같습니다. 해시 표는 찾기에만 쓰고 순회하지 않으며,
 *          이웃 셀은 정해진 순서(z → y → x, 각각 -1 → +1)로, 셀 안의 점은 넣은 순서로 봅니다. 쿠킹 해시와 파괴 해시가 이 성질에 기댑니다.
 *          대표 점은 오차 안에서 **처음 넣은 점**이라 합친 결과가 넣는 순서에 따라 달라질 수 있습니다(같은 입력이면 같다).
 *
 *          쓰는 곳: 물리 메시 셰이프(`PhysicsMeshShapeCache::makeShapeData`), 메시 파괴(`MeshFracture`), 캐릭터 기하 자르기(`GeometryCutUtil`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

namespace sw
{
    /** @class PositionWelder @brief 허용 오차 안의 위치를 하나로 합치는 격자 용접기입니다. 파일 머리말 참고. */
    class SW_API PositionWelder
    {
    public:
        /** @brief 기본 허용 오차(미터)입니다. 따로 구한 같은 점(세 평면이 만나는 점 등)이 어긋나는 1e-7 m 보다 넉넉히 크고, 실제 모서리보다는 훨씬 짧습니다. */
        static constexpr float32 kDefaultTolerance = 1.0e-5f;

        explicit PositionWelder( float32 tolerance = kDefaultTolerance );

        /** @brief @p position 을 넣고 대표 점 번호를 돌려줍니다. 오차 안에 이미 있는 점이 있으면 그 번호(위치는 그 점의 것)입니다. */
        uint32 weld( const float3& position );

        /** @brief @p listPosition 을 차례로 넣고, 위치마다 대표 점 번호를 @p outListRemap 에 씁니다(크기는 입력과 같다). */
        void weldAll( span<const float3> listPosition, vector<uint32>& outListRemap );

        /** @brief 번호 @p index 의 대표 점입니다. */
        const float3& getPoint( uint32 index ) const { return _listPoint[index]; }
        /** @brief 지금까지 생긴 대표 점입니다(번호 순서). */
        const vector<float3>& getPoints() const { return _listPoint; }
        uint32                getPointCount() const { return static_cast<uint32>( _listPoint.size() ); }
        float32               getTolerance() const { return _tolerance; }

        /** @brief 넣은 점을 모두 비웁니다. 허용 오차는 그대로입니다. */
        void clear();

    private:
        float32                               _tolerance;
        vector<float3>                        _listPoint;
        unordered_map<uint64, vector<uint32>> _mapCellToPoint; ///< 셀 키 → 그 셀에 든 대표 점 번호(넣은 순서). 찾기에만 쓰고 순회하지 않는다.
    };
} // namespace sw
