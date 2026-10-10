/**
 * @file ParkLayout.h
 * @brief 공원 배치 데이터(`<ParkLayout>` — 정문 · 입장료 · 시작 자금 · 평지 놀이기구 · 코스터 배치)를 읽는 정의입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/ThemePark.h"

namespace sw
{
    class CoasterLayoutCatalog;
    class XMLNode;
} // namespace sw

namespace sw
{
    /**
     * @struct ParkRidePlacement
     * @brief 지을 수 있는 놀이기구 하나(배치 데이터의 한 줄)입니다. `_layoutId` 가 비면 평지 놀이기구, 아니면 코스터 레이아웃입니다.
     */
    struct ParkRidePlacement
    {
        ParkRide      _ride{};
        hashed_string _layoutId{}; ///< 비면 평지 놀이기구
        string        _shape{ "Cylinder" };
        float3        _position{};
        float3        _size{ 4.0f, 1.0f, 4.0f };
        float4        _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float32       _heading{ 0.0f }; ///< 코스터 스테이션 방향(도)
        float32       _spin{ 0.0f };    ///< 평지 놀이기구가 운행 중 도는 빠르기(rad/s)
        float32       _loadTime{ 15.0f };
        int32         _buildCost{ 1000 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ParkLayout
     * @brief 공원 배치 데이터 한 장입니다. 게임은 읽은 정의로 놀이기구를 세우기만 한다 — 형식과 검증은 여기 한 곳이다.
     * @details 코스터 줄(`<Coaster layout="…">`)은 코스터 레이아웃 카탈로그에서 찾으므로 그 카탈로그를 먼저 읽어 넘긴다. 없는 레이아웃은 경고하고 건너뛴다.
     */
    class SW_GF_API ParkLayout
    {
    public:
        ParkLayout();

        /** @brief 리소스 경로의 `<ParkLayout>` 을 읽습니다. 놀이기구가 하나도 없으면 false 입니다. */
        [[nodiscard]] bool loadFromResource( string_view path, const CoasterLayoutCatalog& layouts );
        /** @brief XML 글에서 읽습니다(시험 · 도구). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, const CoasterLayoutCatalog& layouts, string_view sourceName = {} );

        const float3&                    getGatePosition() const { return _gatePosition; }
        int32                            getEntryFee() const { return _entryFee; }
        int32                            getStartingCash() const { return _startingCash; }
        const vector<ParkRidePlacement>& getPlacements() const { return _listPlacement; }

    private:
        [[nodiscard]] bool loadRoot( const XMLNode& root, const CoasterLayoutCatalog& layouts, string_view sourceName );

        vector<ParkRidePlacement> _listPlacement;
        float3                    _gatePosition;
        int32                     _entryFee;
        int32                     _startingCash;
    };
} // namespace sw
