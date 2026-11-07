/**
 * @file FrameProfileReport.h
 * @brief 프레임 프로파일 한 회분(`-gv_profileFrames` · `-gv_profileSeconds`)의 기계 판독 보고입니다(`-gv_profileReportPath=<경로>` 가 JSON 으로 쓴다).
 * @details 로그 표(`[Profile] …`)는 사람이 읽는 것이라 회귀 스크립트가 글을 긁어야 했다. 같은 값을 칸 이름이 붙은 JSON 한 벌로 남기고,
 *          기준선과 견줄 때 같은 조건인지 가릴 메타데이터(빌드 · 플랫폼 · 타깃 · RHI · 씬)를 같이 적는다. 언리얼 `-csvprofile` 의 CSV 와 같은 자리다.
 *          판정(예산 · 기준선 비교)은 이 파일을 읽는 쪽의 일이다 — 기기 편차가 큰 시간보다 할당 수 · 히치 수 같은 안정된 카운터부터 본다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 프로파일 구간 하나의 측정 창 요약입니다(`FrameProfiler::report` 표의 한 줄과 같은 값). */
    REFLECT()
    struct SW_API FrameProfileScopeSummary
    {
        REFLECT_BODY();

        PROPERTY()
        string _name;
        PROPERTY()
        int64 _avgMicro{ 0 };
        PROPERTY()
        int64 _p50Micro{ 0 };
        PROPERTY()
        int64 _p99Micro{ 0 };
        PROPERTY()
        int64 _minMicro{ 0 };
        PROPERTY()
        int64 _maxMicro{ 0 };
        PROPERTY()
        int64 _callsPerFrameX10{ 0 }; ///< 프레임당 호출 수(카운터 구간이면 프레임당 합) × 10
    };

    /** @brief 측정 한 회분의 보고 본문입니다. 칸을 더하면 `kSchemaVersion` 을 올린다(읽는 스크립트가 판을 본다). */
    REFLECT()
    struct SW_API FrameProfileReport
    {
        REFLECT_BODY();

        /** @brief 보고 판입니다. */
        static constexpr uint32 kSchemaVersion = 1;

        PROPERTY()
        uint32 _schemaVersion{ kSchemaVersion };
        PROPERTY()
        string _build; ///< `build::kConfigName`
        PROPERTY()
        string _platform; ///< `build::kPlatformName`
        PROPERTY()
        string _target; ///< `build::kTargetName`
        PROPERTY()
        string _rhiBackend; ///< 디바이스 이름. 렌더러 없는 실행은 빈 값
        PROPERTY()
        string _scene; ///< 보고 순간의 활성 씬(원본 경로 또는 이름)
        PROPERTY()
        int64 _warmupFrames{ 0 };
        PROPERTY()
        int64 _frames{ 0 };
        PROPERTY()
        int64 _elapsedMicro{ 0 }; ///< 측정 창의 벽시계
        PROPERTY()
        int64 _microPerFrame{ 0 };
        PROPERTY()
        vector<int64> _listWorstFrameMicro; ///< 가장 긴 프레임 간격(큰 순)
        PROPERTY()
        int64 _over16ms{ 0 };
        PROPERTY()
        int64 _over33ms{ 0 };
        PROPERTY()
        int64 _over100ms{ 0 };
        PROPERTY()
        int64 _pacingMedianMicro{ 0 };
        PROPERTY()
        int64 _pacingOverOneAndHalf{ 0 };
        PROPERTY()
        int64 _pacingOverTwoAndHalf{ 0 };
        PROPERTY()
        int64 _allocationCount{ 0 }; ///< 측정 창의 sw 할당 수(메모리 프로파일러가 없으면 0 — `_allocationTracked` 로 가른다)
        PROPERTY()
        int64 _allocationsPerFrameX10{ 0 };
        PROPERTY()
        int64 _allocationTracked{ 0 }; ///< 할당을 셌으면 1(0 인 할당 수가 "없음" 인지 "안 셈" 인지 가른다)
        PROPERTY()
        vector<FrameProfileScopeSummary> _listScope;

        /** @brief JSON 으로 씁니다(부모 폴더를 만든다). 쓰지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool saveToFile( string_view absPath ) const;
    };
} // namespace sw
