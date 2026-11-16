/**
 * @file AutomationGoldenUtil.h
 * @brief 골든 이미지의 판정(`GoldenResult`)과 기록 결정, 판정 정책 데이터(`Config/Dev/GoldenImage.json`), 골든 이름 · 메타(`*.golden.json`)입니다.
 * @details 판정은 한 식(`evaluate`)이고 기록 모드도 같은 식을 본다 — 예전에는 허용치 안이면 기록 모드를 보기 전에 돌아가, 0.99 % 로 남은 골든이 다음 실행에 1.02 % 로 졌다.
 *          이제 허용치의 일정 비율(`_nearLimitRatio`)을 넘은 골든은 `PassNearLimit` 이고 기록 모드가 다시 쓴다.
 *          상용 엔진 비교: 언리얼 Screenshot Comparison 은 골든 옆에 메타(.json — 플랫폼 · RHI · 허용치)를 두고 비교 결과를 보고에 모은다. 유니티 Graphics Test Framework 는
 *          `ImageComparisonSettings`(화소 문턱 · 평균 오차 상한 · 틀린 화소 비율)를 테스트마다 덮는다. 여기는 둘을 합쳐 기본은 데이터, 덮기는 `<ExpectImage>` 속성이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "DevTools/Client/Automation/Runner/AutomationImage.h"
#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /** @brief 골든 비교 한 번의 판정입니다. 세 값을 한 식(`AutomationGoldenUtil::evaluate`)이 낸다. */
    enum class GoldenResult : uint8
    {
        Pass,          ///< 모든 축이 허용치의 `_nearLimitRatio` 안이다
        PassNearLimit, ///< 통과지만 한 축이라도 허용치에 가깝다 — 다음 실행의 흔들림에 질 수 있다(기록 모드가 다시 쓴다)
        Fail,          ///< 한 축이라도 허용치를 넘었다
    };

    /** @brief 판정이 낸 뒤 판정기가 할 일입니다(`AutomationGoldenUtil::decide`). */
    enum class GoldenAction : uint8
    {
        Keep,           ///< 통과 — 골든을 그대로 둔다
        KeepNearLimit,  ///< 통과지만 근접 — 보고의 `nearLimitGoldens[]` 와 경고 줄로 알린다
        Write,          ///< 기록 모드 — 없거나 · 크기가 다르거나 · 근접 · 실패한 골든을 지금 그림으로 쓴다
        FailMissing,    ///< 골든이 없다
        FailUnreadable, ///< 골든과 견주지 못했다(크기가 다르다)
        Fail,           ///< 허용치를 넘었다
    };

    /** @struct AutomationGoldenLimits @brief 판정의 세 축의 상한입니다. 기본은 정책 데이터, 시나리오 `<ExpectImage>` 가 덮는다. */
    struct AutomationGoldenLimits
    {
        float64 _maxDifferingFraction{ 0.0 }; ///< 다른 화소 비율의 상한(0..1) — `tolerance`
        float64 _maxMeanDistance{ 0.0 };      ///< 평균 색 거리의 상한(0..1) — `maxMeanDistance`, 넓고 얕은 차이
        uint32  _maxDeepRegionPixels{ 0 };    ///< 깊은 화소 덩어리의 상한(축소 그림의 화소 수) — `maxRegionPixels`, 좁고 깊은 차이
    };

    /** @struct AutomationGoldenPolicy @brief `Config/Dev/GoldenImage.json` 의 내용입니다. 칸이 하나라도 없거나 범위 밖이면 읽기가 오류다(기본값이 없다). */
    struct AutomationGoldenPolicy
    {
        AutomationImageThreshold _measure{};
        AutomationGoldenLimits   _limits{};
        float64                  _nearLimitRatio{ 0.0 }; ///< 한 축이 상한의 이 비율을 넘으면 근접(0..1 사이, 끝 제외)
    };

    /** @struct AutomationGoldenMeta @brief 골든 옆 `*.golden.json` 한 편의 내용입니다 — 언제(커밋) · 어디서(백엔드) · 왜 · 그때 무엇을 쟀는지. */
    struct AutomationGoldenMeta
    {
        string                    _backendTag{}; ///< `dx12` …(쿠킹 표의 명령줄 이름)
        string                    _platform{};   ///< `windows` · `linux`
        string                    _commit{};     ///< 기록한 작업 트리의 커밋(`git describe --always --dirty`, 고친 채면 `-dirty`)
        int64                     _recordedUnixMs{ 0 };
        string                    _reason{};           ///< `missing` · `size-changed` · `near-limit` · `failed`
        bool                      _bMeasured{ false }; ///< 앞 골든과 견줬다(`_difference` 가 뜻이 있다) NOLINT(sw-bool-flag): 집합체다
        AutomationImageDifference _difference{};       ///< 쓰기 전 골든과 지금 그림의 차이
        AutomationGoldenLimits    _limits{};           ///< 그때의 상한(덮은 값 포함)
        uint32                    _width{ 0 };
        uint32                    _height{ 0 };
    };

    /**
     * @struct AutomationGoldenSample
     * @brief 골든 비교 한 번의 측정값과 그때의 상한입니다. 기록 모드가 아닌 실행이 보고 `goldenSamples[]` 에 남기고,
     *        `Scripts/dev/scenario_bisect.py --golden-margins` 가 여러 보고를 모아 골든마다 실행 사이 흔들림(최대값)과 여유(상한 / 최대값)를 낸다.
     */
    struct AutomationGoldenSample
    {
        string                    _goldenID{};
        AutomationImageDifference _difference{};
        AutomationGoldenLimits    _limits{};
    };

    /** @struct AutomationGoldenUtil @brief 골든 판정 · 기록 결정 · 정책 읽기 · 이름 짓기입니다. */
    struct SW_DEVTOOLS_API AutomationGoldenUtil
    {
        /** @brief 정책 파일(프로젝트 루트 기준)입니다. */
        static constexpr const utf8* kPolicyFile = "Config/Dev/GoldenImage.json";

        /** @brief 정책 JSON 글을 읽습니다. 칸이 없거나 형식 · 범위가 틀리면 그 칸이 대상인 오류입니다. */
        [[nodiscard]] static expected<AutomationGoldenPolicy> parsePolicy( string_view jsonText, string_view sourceName );
        /** @brief 프로젝트의 정책 파일을 읽습니다(`parsePolicy`). */
        [[nodiscard]] static expected<AutomationGoldenPolicy> loadPolicy();

        /** @brief 판정입니다 — 한 축이라도 상한을 넘으면 `Fail`, 한 축이라도 상한 × @p nearLimitRatio 를 넘으면 `PassNearLimit`, 아니면 `Pass`. */
        [[nodiscard]] static GoldenResult evaluate( const AutomationImageDifference& difference, const AutomationGoldenLimits& limits, float64 nearLimitRatio );
        /** @brief 판정 뒤 할 일입니다. 기록 모드(@p bUpdateGolden)는 `Pass` 가 아닌 모든 골든을 쓴다 — 근접도 쓴다. */
        [[nodiscard]] static GoldenAction decide( bool bHasGolden, bool bCompared, GoldenResult result, bool bUpdateGolden );
        /** @brief 메타의 `_reason` 글입니다(`decide` 가 `Write` 인 까닭). */
        [[nodiscard]] static const utf8* describeWriteReason( bool bHasGolden, bool bCompared, GoldenResult result );

        /** @brief 백엔드 열거값(`RHI.Backend` 감시 값)의 골든 꼬리 — 쿠킹 표의 명령줄 이름(`dx12` · `wgpu` …)입니다. 표 밖이면 없음. */
        [[nodiscard]] static optional<string_view> findBackendTag( uint32 backendIndex );
        /** @brief 이 플랫폼의 골든 꼬리(Windows 는 빈 글, Linux 는 `.linux`)입니다. */
        [[nodiscard]] static string_view getPlatformTag();
        /** @brief `<폴더>/<시나리오>.<이름>.<백엔드><플랫폼>.ppm` 입니다. */
        [[nodiscard]] static string makeGoldenID( string_view folderID, string_view scenarioName, string_view goldenName, string_view backendTag, string_view platformTag );
        /** @brief 골든 id 의 메타 id(`.ppm` → `.golden.json`)입니다. */
        [[nodiscard]] static string makeMetaID( string_view goldenID );
        /** @brief 메타 JSON 글입니다. */
        [[nodiscard]] static string makeMetaText( const AutomationGoldenMeta& meta );
        /** @brief 지금 작업 트리의 커밋(`git describe --always --dirty --abbrev=40`)입니다. git 이 없거나 저장소가 아니면 오류입니다. */
        [[nodiscard]] static expected<string> findRecordingCommit();
        /**
         * @brief `git describe` 의 결과(`Process::execute` 의 결과 · 첫 줄)를 커밋으로 읽습니다. git 을 띄우지 못함(오류) · 저장소 아님(0 아닌 종료) · 빈 출력은
         *        저마다 까닭을 말하는 오류입니다(폴백 없음).
         */
        [[nodiscard]] static expected<string> readRecordingCommit( const expected<int32>& exitCode, string_view firstLine );
    };
} // namespace sw
