/**
 * @file MemoryBudgetMonitor.h
 * @brief 메모리 태그 예산(데이터) · 프레임마다의 예산 검사 · 보고(`-gv_memoryReport`) · FrameProfiler 카운터입니다.
 * @details 상용 엔진의 같은 자리는 언리얼 LLM 의 태그별 예산(`LLM` 트래커 + 플랫폼 .ini 예산)과 `stat llm`, 유니티 Memory Profiler 의 카테고리 표입니다.
 *          태그 · 계수는 Core 의 `MemoryProfiler` 가 들고, 이 타입은 엔진 쪽 일(데이터 읽기 · 프레임 경계 · 로그 · 프로파일러 카운터)만 합니다.
 *          배포본에는 프로파일러가 없어 아무 일도 하지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/array.h"
#include "Core/Container/string.h"
#include "Core/Memory/MemoryTag.h"

namespace sw
{
    class MemoryProfiler;

    /**
     * @class MemoryBudgetMonitor
     * @brief 예산 표를 프로파일러에 걸고, 프레임 끝마다 넘은 태그를 경고하고, 요청이 오면 태그 표를 남깁니다. `EngineLoop` 가 하나를 듭니다.
     */
    class SW_API MemoryBudgetMonitor
    {
    public:
        /** @brief 예산 파일의 기본 위치입니다(프로젝트 폴더 기준). */
        static constexpr const utf8* kBudgetFile = "Config/Engine/MemoryBudget.json";

        MemoryBudgetMonitor();

        /**
         * @brief 예산 JSON 을 읽어 @p profiler 에 겁니다. 모양: `{ "_listBudget": [ { "_tag": "Texture", "_megabytes": 512 }, … ] }`.
         * @details 모르는 키 · 모르는 태그 이름 · 같은 태그 두 번 · 0 이하 크기는 오류입니다(조용히 넘기지 않는다). 실패하면 아무 예산도 바꾸지 않습니다.
         * @param outError 실패 이유(어느 항목이 왜)
         */
        [[nodiscard]] static bool applyBudgetJson( string_view jsonText, MemoryProfiler& profiler, string& outError );
        /**
         * @brief 예산 파일을 읽어 활성 프로파일러에 겁니다. 파일이 없으면 예산 없이 true 입니다. 형식이 틀리면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] bool loadBudgetFile( string_view absolutePath );

        /**
         * @brief `-gv_memoryTracking` 을 프로파일러에 겁니다. 명령줄을 읽은 바로 뒤(`EngineLoop::initialize`)에 불러야 기동의 할당부터 셉니다 — 첫 프레임까지
         *        기다리면 기동 중에 잡은 블록이 태그 줄에 없다. 프레임 끝(`onFrameEnd`)도 값이 바뀌었으면 다시 겁니다.
         */
        void applyTrackingSetting();

        /**
         * @brief 프레임 끝에 한 번 부릅니다 — `-gv_memoryTracking` 적용, 예산 검사(새로 넘은 태그 경고), `-gv_memoryReport` 보고, FrameProfiler 카운터.
         * @details 프로파일러가 없으면(배포본) 바로 돌아옵니다. 추적이 꺼져 있으면 예산 검사도 하지 않습니다(값이 움직이지 않는다).
         */
        void onFrameEnd();

        /**
         * @brief 태그 표를 로그로 남깁니다 — 살아 있는 바이트 · 블록, 최고치, 예산과 그 비율. 살아 있는 바이트가 큰 순서, 0 인 태그는 건너뜁니다.
         * @details `-gv_profileFrames` 보고(`FrameProfileSession`)와 `-gv_memoryReport` 가 같은 표를 씁니다.
         */
        static void logMemoryReport( const MemoryProfiler& profiler, const utf8* pTitle );

    private:
        /** @brief 이번 프레임의 살아 있는 KB 를 FrameProfiler 카운터로 더합니다(전체 한 줄 + 예산이 있는 태그마다 한 줄). */
        void addFrameProfilerCounters( const MemoryProfiler& profiler );

        /** @brief 태그마다 FrameProfiler 슬롯입니다(처음 쓸 때 등록). 예산 없는 태그는 쓰지 않는다 — 슬롯은 64 개뿐이다. */
        array<uint32, kMemoryTagCount> _arrTagCounterSlot;
        uint32                         _liveCounterSlot;
        /** @brief 마지막으로 적용한 `gv_memoryTracking` 값입니다(-1 = 구성 기본값 그대로). */
        int32 _appliedTrackingSetting;
    };
} // namespace sw
