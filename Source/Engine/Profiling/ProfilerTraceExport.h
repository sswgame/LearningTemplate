/**
 * @file ProfilerTraceExport.h
 * @brief `ProfilerTimeline` 의 사건을 Chrome Trace Event 형식(JSON)으로 바꾸고 저장합니다. `chrome://tracing` 과 Perfetto UI 가 읽습니다.
 * @note Shipping 에서는 통째로 빠집니다(`SW_PROFILER_TRACE_ENABLED` 0 — 헤더는 비고 소스도 컴파일되지 않는다).
 * @details 변환은 저장하는 순간에만 합니다 — 링 버퍼를 복사해 문자열로 바꾸므로 프레임당 비용이 없습니다.
 *          구간은 완전 이벤트(`X`, 시작 · 길이 마이크로초), 스레드 · 프로세스 이름은 메타 이벤트(`M`), 프레임 경계는 인스턴트 이벤트(`i`)입니다.
 *          파일 머리말(`otherData`)에 저장 시점의 벽시계와 단조 시계 쌍 · 호스트 이름을 남겨, 프로세스마다 저장한 파일을 `mergeTraceJSON` 이
 *          한 타임라인으로 합칩니다(`profile.trace merge`). 사람이 누르지 않아도 남는 길은 셋입니다 — `bugit`(타임라인 기록이 켜져 있으면),
 *          측정 창 전체(`-gv_profileTracePath`), 느린 틱(`-gv_traceSlowTickMs`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/ProfilerTimeline.h"
#include "Core/Network/Connection/NetFlowLog.h"

#if SW_PROFILER_TRACE_ENABLED

namespace sw
{
    class FrameProfiler;

    /** @brief 트레이스에 전역 인스턴트 이벤트로 찍는 표식입니다(느린 틱 · 잘림 같은 "여기를 보라"). */
    struct ProfilerTraceMarker
    {
        string _name;
        uint64 _nanos{ 0 }; ///< 단조 시계 나노초
    };
} // namespace sw

namespace sw
{
    /** @brief 트레이스 파일 머리말에 들어가는 프로세스 정보입니다. */
    struct ProfilerTraceMeta
    {
        string                      _processName;
        string                      _hostName; ///< 같은 기계에서 나온 파일끼리는 단조 시계를 그대로 맞춘다(합치기)
        int32                       _processID{ 0 };
        int64                       _wallUnixMilliseconds{ 0 }; ///< 저장 시점의 UTC 유닉스 시각 — 아래 단조 시계와 한 쌍입니다
        uint64                      _monotonicNanoseconds{ 0 }; ///< 저장 시점의 단조 시계(`MonotonicClock::nowNanoseconds`)
        bool                        _bTruncated{ false };       ///< 긴 캡처가 예산을 넘어 뒤를 담지 못했다 — 머리말과 전역 표식으로 알린다
        vector<ProfilerTraceMarker> _listMarker;                ///< 전역 인스턴트로 찍을 표식(쓰기 전용 — 읽을 때는 비어 있다)
        /**
         * @brief 네트워크 메시지 흐름(쓰기 전용 — 읽을 때는 비어 있다). "Net messages" 줄에 짧은 구간과 흐름 이벤트(`s` · `f`)로 씁니다.
         * @details 파일로 저장하는 길(`writeTraceFile`)이 저장하는 사건의 시간 구간으로 `NetFlowLog::collect` 를 불러 채운다.
         *          합친 트레이스(`mergeTraceJSON`)에서 보낸 프로세스와 받은 프로세스의 같은 id 가 화살표로 이어진다.
         */
        vector<NetFlowEvent> _listFlow;
    };
} // namespace sw

namespace sw
{
    /** @brief 합칠 트레이스 하나입니다 — 파일 내용과(비면 머리말의 프로세스 이름을 쓰는) 줄 묶음 이름입니다. */
    struct ProfilerTraceMergeInput
    {
        string _json;
        string _label;
    };
} // namespace sw

namespace sw
{
    /** @brief 불러온 트레이스 파일입니다 — 스레드마다 사건(시작 시각 순 · 깊이는 겹침으로 다시 계산), 구간 이름 표, 프레임 경계입니다. */
    struct ProfilerLoadedTrace
    {
        ProfilerTraceMeta              _meta;
        vector<ProfilerTimelineThread> _listThread;
        vector<string>                 _listSlotName; ///< 사건의 `_slot` 이 이 표의 번호다(이름마다 하나)
        vector<uint64>                 _listFrameBegin;
        uint64                         _beginNanos{ 0 }; ///< 가장 이른 사건 시작
        uint64                         _endNanos{ 0 };   ///< 가장 늦은 사건 끝
        string                         _sourcePath;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ProfilerTraceExport
     * @brief 타임라인 사건 → Trace Event JSON 변환기와 저장 창구입니다(전부 static).
     */
    class SW_API ProfilerTraceExport
    {
    public:
        /** @brief 기본 파일 확장자입니다. */
        static constexpr const utf8* kFileExtension = ".trace.json";

        /**
         * @brief 모은 스레드 사건을 Trace Event JSON 문자열로 바꿉니다(순수 함수).
         * @param listThread 스레드마다 사건(`ProfilerTimeline::collectRecentFrames` 결과)
         * @param listFrameBegin 프레임 시작 시각(나노초) 목록
         * @param listSlotName 슬롯 번호로 찾는 구간 이름. 범위 밖이거나 빈 이름이면 `slot <번호>` 로 냅니다
         * @param meta 프로세스 이름 · 시계 쌍
         * @return JSON 문자열. 시각은 마이크로초(소수 셋째 자리까지)이고 단조 시계 값을 그대로 씁니다.
         */
        static string makeTraceJSON( const vector<ProfilerTimelineThread>& listThread, const vector<uint64>& listFrameBegin,
                                     const vector<string>& listSlotName, const ProfilerTraceMeta& meta );

        /** @brief 긴 캡처의 스레드당 사건 수 기본 예산입니다(사건 24 B — 스레드마다 약 24 MB). */
        static constexpr uint32 kDefaultEventBudgetPerThread = 1000000;

        /**
         * @brief @p seconds 초 동안 처음부터 모아 끝나면 저장하는 긴 캡처를 시작합니다(링 버퍼의 최근 몇 프레임과 따로).
         * @param seconds 0 이하면 시간 제한이 없고 `finishRecording` 이 끝냅니다(측정 창에 맞춰 끝내는 `-gv_profileTracePath`).
         * @param budget 스레드당 사건 수 상한(0 이면 기본값). 넘으면 거기서 멈추고 잘림 표지를 남깁니다 — 오래된 것을 버리지 않습니다.
         * @details 시간이 다 된 것은 `tickRecording` 이 알아챕니다(`Engine::endFrame` 이 프레임마다 부른다). 이미 진행 중이면 거절합니다.
         * @return 시작했으면 true
         */
        [[nodiscard]] static bool startRecording( FrameProfiler& profiler, float64 seconds, string_view path, string_view processName, uint32 budget,
                                                  string& outReply );
        /** @brief 긴 캡처가 진행 중이면 true 입니다. */
        static bool isRecording();
        /** @brief 긴 캡처의 시간이 다 됐으면 멈추고 저장합니다. 진행 중이 아니면 atomic 하나 읽고 돌아옵니다. 게임 스레드가 부릅니다. */
        static void updateRecording( FrameProfiler& profiler );
        /**
         * @brief 진행 중인 긴 캡처를 지금 멈추고 저장합니다. 게임 스레드가 부릅니다.
         * @return 파일을 썼으면 true(진행 중이 아니었거나 사건이 없거나 쓰기에 실패하면 false — 이유는 로그에)
         */
        [[nodiscard]] static bool finishRecording( FrameProfiler& profiler );

        /** @brief 느린 틱을 넘긴 뒤 몇 프레임을 더 모아 저장하는가입니다 — 원인이 틱 뒤에 이어지는 것(밀린 일)도 보이게. */
        static constexpr uint32 kSlowFrameFramesAfter = 3;
        /** @brief 느린 틱 저장 사이의 최소 간격(초)입니다. 저장 자체가 틱을 늦추므로 연달아 잡지 않습니다. */
        static constexpr float64 kSlowFrameCooldownSeconds = 30.0;
        /** @brief 프로세스 하나가 남기는 느린 틱 파일 수 상한입니다(디스크를 채우지 않게). */
        static constexpr uint32 kSlowFrameMaxFileCount = 20;

        /**
         * @brief 느린 틱 자동 캡처입니다 — `-gv_traceSlowTickMs=N` 이 0 보다 크면 타임라인 링 버퍼를 켜 두고, 틱 본문이 N ms 를 넘으면
         *        `kSlowFrameFramesAfter` 프레임 뒤 링 버퍼 전체(그 틱 앞뒤)를 `Saved/Traces/slowtick_<유닉스 밀리초>.trace.json` 에 저장합니다.
         * @param frameBeginNanos 틱 본문 시작(단조 시계) — 파일에 "Slow tick" 표식으로 찍힌다
         * @param frameEndNanos 틱 본문 끝
         * @details 게임 스레드가 틱(프레임)을 마친 뒤 한 번 부릅니다. 꺼져 있으면 정수 하나 비교하고 돌아옵니다.
         *          Shipping 에는 없습니다 — 운영에서 쓰려면 Dev 구성 서버(`Ninja-Release`)를 씁니다.
         */
        static void updateSlowFrameCapture( FrameProfiler& profiler, uint64 frameBeginNanos, uint64 frameEndNanos, string_view processName );

        /**
         * @brief 프로세스마다 저장한 트레이스 여럿을 한 타임라인으로 합칩니다(순수 함수).
         * @details 시간 맞추기: 머리말의 호스트 이름이 첫 파일과 같으면 단조 시계를 나눠 쓰므로 그대로 두고(정확), 다르면 벽시계와 단조 시계 쌍으로
         *          옮깁니다(두 기계의 시계 차이만큼 틀린다 — 줄 묶음 이름에 적는다). 머리말이 없는 파일(다른 도구)은 옮기지 않습니다.
         *          프로세스마다 줄 묶음(`pid`)이 하나이고 겹치면 새 번호를 줍니다. 사건은 `ts` · `pid` 만 바꾸고 나머지(흐름 이벤트 `s` · `t` · `f` 의
         *          `id` 포함)는 그대로 옮겨, 양쪽에 같은 메시지 id 가 있으면 뷰어가 프로세스 사이 화살표를 그립니다.
         * @param outJSON 합친 Trace Event JSON(머리말 `otherData.sources` 에 파일마다 맞춘 방법 · 옮긴 양)
         * @param outReport 실패 이유 또는 요약
         * @return 입력이 모두 읽혔고 사건이 하나라도 있으면 true
         */
        [[nodiscard]] static bool mergeTraceJSON( const vector<ProfilerTraceMergeInput>& listInput, string& outJSON, string& outReport );
        /** @brief 파일들을 읽어 `mergeTraceJSON` 으로 합치고 @p outputPath 에 씁니다. 이름은 각 파일의 머리말 프로세스 이름입니다. */
        [[nodiscard]] static bool mergeTraceFiles( const vector<string>& listInputPath, string_view outputPath, string& outReply );

        /**
         * @brief Trace Event JSON(이 파일이 낸 것, 또는 완전 이벤트 `X` 와 스레드 이름 메타를 가진 다른 도구의 것)을 읽습니다.
         * @details 시각은 마이크로초 → 나노초. 같은 (pid, tid) 는 한 스레드이고, 스레드 줄 이름은 `thread_name` 메타입니다(없으면 `tid <번호>`).
         *          깊이는 시작 시각 순으로 겹침을 세어 다시 계산합니다(파일에 깊이가 없다). 구간 이름은 같은 글이면 같은 번호입니다.
         * @return 사건이 하나라도 있으면 true. 아니면 @p outError 에 이유.
         */
        [[nodiscard]] static bool parseTraceJSON( string_view json, ProfilerLoadedTrace& outTrace, string& outError );
        /** @brief 파일을 읽어 `parseTraceJSON` 으로 해석합니다. */
        [[nodiscard]] static bool loadTraceFile( string_view path, ProfilerLoadedTrace& outTrace, string& outError );
        /** @brief 기본 저장 폴더(`<프로젝트>/Saved/Traces`)에서 가장 최근 `.trace.json` 의 경로입니다. 없으면 빈 글. */
        static string findLatestTraceFile();

        /**
         * @brief 최근 @p frameCount 프레임을 모아 @p path 에 저장합니다.
         * @param path 비면 `<프로젝트>/Saved/Traces/trace_<유닉스 밀리초>.trace.json`
         * @param outSavedPath 실제로 쓴 경로
         * @param outReply 실패 이유 또는 사건 · 스레드 수 요약
         * @return 쓰기에 성공하면 true. 기록된 프레임이 둘 미만이면 false(타임라인 기록이 꺼져 있었다).
         */
        [[nodiscard]] static bool saveRecentFrames( const FrameProfiler& profiler, uint32 frameCount, string_view path, string_view processName,
                                                    string& outSavedPath, string& outReply );

        /**
         * @brief 종료 때 트레이스 저장을 준비합니다 — `-gv_traceSaveOnExit=<경로>` 가 있으면 타임라인 링 버퍼 기록을 켭니다(없으면 아무것도 하지 않는다).
         * @details 엔진 기동 끝에 부릅니다(`Engine::initialize`). 여러 프로세스를 띄운 부모(에디터의 멀티 프로세스 재생)가 프로세스마다 경로를 주고,
         *          모두 끝난 뒤 그 파일들을 `mergeTraceJSON` 으로 합칩니다.
         */
        static void startExitCapture( FrameProfiler& profiler );
        /**
         * @brief `-gv_traceSaveOnExit` 가 있으면 링 버퍼의 최근 프레임을 그 경로에 저장합니다. 결과는 로그에 남깁니다(`Exit trace saved <경로>`).
         * @details 엔진 종료 첫머리에 부릅니다(서비스가 아직 있다). @p processName 은 머리말의 프로세스 이름입니다.
         */
        static void finishExitCapture( const FrameProfiler& profiler, string_view processName );

        /** @brief 개발 명령 `profile.trace` 의 첫 낱말이 가리키는 하위 명령입니다. */
        enum class CommandWord : uint8
        {
            Unknown,
            Save,
            Record,
            Merge,
            Switch, ///< 링 버퍼 기록을 켜고 끈다 — 켬 · 끔은 불리언 철자 표(`StringUtil::tryParseBool`)로 읽는다
        };
        /**
         * @brief `profile.trace` 의 첫 낱말을 가려냅니다(순수 함수). `save` · `record` · `merge` 는 정확히 그 글자이고(대소문자 구분), 그 밖은
         *        불리언으로 읽히면 `Switch`(값은 @p outRecordingOn), 아니면 `Unknown` 입니다. 하위 명령 철자가 불리언 철자와 겹치지 않는다.
         */
        [[nodiscard]] static CommandWord parseCommandWord( string_view word, bool& outRecordingOn );
    };
} // namespace sw

#endif // SW_PROFILER_TRACE_ENABLED
