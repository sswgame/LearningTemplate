/**
 * @file ProfilerPerformanceModel.h
 * @brief 프로파일러 패널 Performance 탭의 상태입니다 — 벽시계 프레임 시간 기록과 그 통계, 리소스 카탈로그 개수 잡(ImGui 없음).
 */
#pragma once
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"

namespace sw::editor
{
    /** @brief 프레임 시간 기록의 통계입니다(ms). 기록이 없으면 최소는 `MathUtil::kMaxFloat` 입니다. */
    struct ProfilerFrameTimeStats
    {
        float32 _currentMs{ 0.0f };
        float32 _currentFPS{ 0.0f };
        float32 _averageMs{ 0.0f };
        float32 _averageFPS{ 0.0f };
        float32 _minMs{ 0.0f };
        float32 _maxMs{ 0.0f };
    };

    /**
     * @class ProfilerPerformanceModel
     * @brief 최근 120 프레임의 벽시계 프레임 시간과 리소스 카탈로그 개수를 가집니다.
     * @details ImGui 경과는 엔진 경과(자동화에서는 고정 프레임 시간)라 프레임 비용을 재려면 벽시계를 따로 읽는다.
     */
    class ProfilerPerformanceModel
    {
    public:
        /** @brief 기록하는 프레임 수입니다. */
        static constexpr uint32 kHistoryCount = 120;

        ProfilerPerformanceModel();

        /** @brief 지난 호출 뒤의 벽시계 경과를 한 칸 기록하고 통계를 돌려줍니다. Performance 탭을 그릴 때마다 부릅니다. */
        ProfilerFrameTimeStats sampleFrame();
        /** @brief 프레임 시간 링 버퍼(ms)와 다음에 쓸 칸입니다. 그래프가 읽습니다. */
        const float32* getHistory() const { return _arrFrameTimeHistory; }
        uint32         getHistoryOffset() const { return _historyOffset; }

        /** @brief 카탈로그 잡의 결과를 받고, 다시 셀 차례면 잡을 띄웁니다. 카탈로그 구역을 그릴 때 부릅니다. */
        void pollCatalog();
        /** @brief 다음 `pollCatalog` 에서 리소스를 다시 셉니다. */
        void requestCatalogScan() { _bCatalogDirty = SW_TRUE; }
        /** @brief 마지막으로 센 종류별 개수입니다. */
        const EditorResourceCatalogCounts& getCatalogCounts() const { return _catalogCounts; }

    private:
        float32                     _arrFrameTimeHistory[kHistoryCount];
        uint32                      _historyOffset;
        Stopwatch                   _drawStopwatch; ///< 성능 탭을 마지막으로 그린 뒤의 벽시계 경과
        EditorResourceCatalogJob    _catalogJob;
        EditorResourceCatalogCounts _catalogCounts;
        uint8                       _bCatalogDirty : 1;
    };
} // namespace sw::editor
