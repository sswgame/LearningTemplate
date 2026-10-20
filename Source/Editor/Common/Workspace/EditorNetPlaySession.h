/**
 * @file EditorNetPlaySession.h
 * @brief 에디터의 멀티 프로세스 재생을 담당합니다. 서버와 클라이언트 N 개를 자식 프로세스로 실행해 연결하고, 로그를 한곳에 모으고, Stop 할 때 트레이스를 합칩니다. ImGui 에 의존하지 않습니다.
 * @details 언리얼 PIE 의 Net Mode(Play As Listen Server, Play As Client 와 "Run Under One Process" 끔), 유니티의 Multiplayer Play Mode 에 해당한다.
 *          이미 있는 실행 파일만 쓴다. 서버는 전용이면 `Server`, 리슨 호스트이면 첫 `App` 이고, 클라이언트는 `App` 이다. 각 프로세스에 넘기는 인자는 다음과 같다.
 *
 *          | 프로세스 | 인자 |
 *          |---|---|
 *          | 서버 | `-gv_netListenPort=<포트>` (+ 트레이스면 `-gv_traceSaveOnExit=<폴더>/server.trace.json`) |
 *          | 전용 서버만 | `-gv_debugChannelPort=<TCP 포트> -gv_debugChannelToken=<세션마다 새 토큰>` — Inspector 탭이 이 채널로 붙는다(`getDebugChannelPort`) |
 *          | 클라이언트 | `-gv_netConnectAddress=127.0.0.1:<포트> -gv_standardInputCommands=1` (+ `-gv_traceSaveOnExit=<폴더>/clientN.trace.json`) |
 *          | `App` 마다(창 배치를 켜면) | `-W=<너비> -H=<높이> -WinX=<x> -WinY=<y>` — 리슨 호스트 · 클라이언트 순서로 나란히(`makeWindowArguments`) |
 *
 *          - 로그: 프로세스마다 읽기 스레드가 줄을 모은다. `update` 가 게임 스레드에서 `[Server]`, `[Client1]` 같은 태그를 붙여 에디터 로그에 다시 쓰므로
 *            Output Log 하나에 모인다. 패널에서 출처별로 거를 수 있도록 최근 줄도 보관한다.
 *          - Stop: 모든 프로세스의 표준 입력에 `quit` 를 보내 정상 종료시키고(트레이스는 이때 쓰인다), 시한 안에 끝나지 않으면 강제 종료한다.
 *            트레이스를 켠 세션이면 남은 파일을 `ProfilerTraceExport::mergeTraceJSON` 으로 `<폴더>/merged.trace.json` 에 합친다.
 *            같은 PC 이므로 단조 시계를 그대로 맞출 수 있어 정확하다. 어느 길(툴바 · Session 탭 · `netplay.stop`)로 멈춰도 합친 트레이스는
 *            `takeUnopenedMergedTrace` 가 한 번 내주고, Profiler 창의 프레임 단계(`SW_EDITOR_FRAME_STEP`)가 그것을 Timeline 탭에 연다.
 *          - 사용자 프리셋(`EditorNetPlayPresetStore`, `Saved/Editor/NetPlayPresets.json`)은 초기화 때 읽고, 마지막으로 고른 프리셋을 다음 세션의 구성으로 둔다.
 *          - 하나가 먼저 죽으면 `update` 가 종료 코드와 마지막 줄을 함께 경고한다.
 *          - 서버 일시정지와 한 틱 진행은 서버 표준 입력에 `pause`, `step N`, `resume` 을 보내서 처리한다(전용 서버의 Dev 명령).
 *          - 회선 조절은 그 프로세스의 표준 입력에 `net.emulation …` 을 보낸다(`sendProcessCommand`). 답 줄 가운데 `NetEmulationReport` 줄은 프로세스마다 마지막 것을 보관한다.
 *          - `NetStatsReport` 줄(`net.stats` 의 답 — Profiler 탭이 0.5 초마다 묻는다)도 프로세스마다 마지막 것을 보관한다. 이 줄은 에디터 로그 · 마지막 줄로 옮기지 않는다(Output Log 를 메운다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/NetStatsReport.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorNetPlayPresetStore.h"
#include "Editor/Common/Workspace/EditorNetPlaySettings.h"

#include <chrono>
#include <thread>

namespace sw::editor
{
    /** @brief 모은 로그 한 줄입니다. */
    struct EditorNetPlayLogLine
    {
        string _text;
        int32  _processIndex{ 0 }; ///< `getProcessLabel` 의 번호
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 패널이 그리는 프로세스 하나의 상태 복사본입니다. */
    struct EditorNetPlayProcessStatus
    {
        string             _label;
        string             _lastLine;
        string             _tracePath;
        NetEmulationReport _emulationReport{};         ///< 마지막으로 받은 `net.emulation stats` 의 답
        uint32             _emulationReportCount{ 0 }; ///< 받은 보고 수 — 새 보고가 왔는지 이 값으로 본다
        NetStatsReport     _statsReport{};             ///< 마지막으로 받은 `net.stats` 의 답(Profiler 탭)
        uint32             _statsReportCount{ 0 };     ///< 받은 `net.stats` 보고 수
        int32              _processID{ 0 };
        int32              _exitCode{ 0 };
        bool               _bRunning{ false };
        bool               _bConnected{ false }; ///< 클라이언트가 서버에 연결되었다는 줄(`Net session connected to`)을 남겼다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorNetPlaySession
     * @brief 서버와 클라이언트 프로세스를 묶은 세션 하나입니다. `start`, 프레임마다 `update`, `stop` 순서로 씁니다. 게임(UI) 스레드에서만 호출합니다.
     * @details 확장 모듈의 Multiplayer 창 탭(`SW_EDITOR_MULTIPLAYER_TAB`)도 이 세션을 받으므로 EditorModule 밖으로 내보낸다.
     */
    class SW_EDITOR_API EditorNetPlaySession : public EditorSubsystem
    {
    public:
        /** @brief 패널 필터용으로 보관하는 최근 줄 수입니다. 넘으면 오래된 줄부터 버리며, 에디터 로그에는 모두 남습니다. */
        static constexpr size_t kMaxLogLine = 4000;
        /** @brief 서버(전용 서버 · 리슨 호스트)의 프로세스 번호입니다. 클라이언트는 1 부터입니다. */
        static constexpr int32 kServerProcessIndex = 0;
        /** @brief 합친 트레이스의 파일 이름입니다. */
        static constexpr const utf8* kMergedTraceFileName = "merged.trace.json";
        /** @brief `updateUntil` 이 조건을 다시 보는 간격입니다. */
        static constexpr int64 kUpdatePollMilliseconds = 50;

        EditorNetPlaySession();
        ~EditorNetPlaySession() override;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "EditorNetPlaySession";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        /** @brief 사용자 프리셋 파일을 읽고, 마지막으로 고른 프리셋이 있으면 다음 세션의 구성(`getSettings`)으로 둡니다. 깨진 파일은 치우고 기동은 잇습니다. */
        SubsystemInitResult initializeSubsystem() override;

        EditorNetPlaySession( const EditorNetPlaySession& )            = delete;
        EditorNetPlaySession& operator=( const EditorNetPlaySession& ) = delete;

        /** @brief 프로세스들을 실행합니다. 이미 실행 중이거나 하나라도 실행하지 못하면 이미 실행한 것을 종료하고 false 를 반환합니다. 이유는 로그에 남습니다. */
        [[nodiscard]] bool start( const EditorNetPlaySettings& settings );
        /**
         * @brief 모든 프로세스를 정상 종료시키고(시한을 넘으면 강제 종료) 읽기 스레드를 정리합니다. 트레이스를 켰으면 합칩니다.
         * @return 합친 트레이스 경로입니다. 트레이스가 꺼져 있거나 둘 미만이면 빈 문자열입니다.
         */
        string stop();
        /** @brief 모은 줄을 에디터 로그로 옮기고, 먼저 끝난 프로세스를 알립니다. 프레임마다 호출합니다. */
        void update();
        /**
         * @brief @p isDone 이 true 를 돌려주거나 시한이 지날 때까지 `update` 를 돌리며 부른 스레드를 붙잡습니다.
         * @details 자동화 명령(`netplay.wait` · `netplay.stats` · `netplay.profile`)과 정상 종료가 씁니다. 시나리오는 프레임 단위라
         *          게임 스레드를 막고 기다리는 것이 의도입니다. 조건은 `update` 뒤에 보므로 마지막으로 모은 줄까지 반영합니다.
         */
        template <typename Predicate>
        void updateUntil( int64 timeoutMilliseconds, const Predicate& isDone )
        {
            const Deadline deadline = Deadline::afterMilliseconds( timeoutMilliseconds );
            while ( true )
            {
                update();
                if ( isDone() || deadline.isExpired() )
                    return;
                std::this_thread::sleep_for( std::chrono::milliseconds( kUpdatePollMilliseconds ) );
            }
        }

        /** @brief 서버(전용 서버 또는 리슨 호스트)의 표준 입력에 한 줄을 보냅니다. `pause`, `step 3`, `resume`, 개발 명령을 보낼 수 있습니다. 보냈으면 true 입니다. */
        [[nodiscard]] bool sendServerCommand( string_view command );
        /**
         * @brief 프로세스 하나(`getProcessStatus` 의 번호)의 표준 입력에 한 줄을 보냅니다. 끝났거나 번호가 없으면 false 입니다.
         * @details 클라이언트 · 리슨 호스트(`App -gv_standardInputCommands=1`)와 전용 서버(Dev)는 그 줄을 개발 명령으로 돌리고 답을 로그 줄로 남긴다.
         */
        [[nodiscard]] bool sendProcessCommand( int32 processIndex, string_view command );

        bool isRunning() const { return _listProcess.empty() == false; }
        /** @brief 서버에 연결되었다고 로그에 남긴 클라이언트 수입니다. `Net session connected to` 줄을 센 값이며, 프로브와 테스트가 접속 여부를 이 값으로 확인합니다. */
        int32 getConnectedClientCount() const;
        /** @brief 패널과 툴바가 편집하는 다음 세션의 구성입니다. `start` 는 받은 구성을 따로 복사해서 씁니다. */
        EditorNetPlaySettings& getSettings() { return _pendingSettings; }
        uint16                 getPort() const { return _port; }
        /** @brief 전용 서버 세션이면 서버의 디버그 채널 포트(루프백 TCP)입니다. 리슨 호스트 세션 · 멈춘 세션은 0 입니다(Inspector 탭이 붙는다). */
        uint16 getDebugChannelPort() const { return _debugChannelPort; }
        /** @brief 위 채널의 토큰입니다. 세션마다 새로 만듭니다. 채널이 없으면 비어 있습니다. */
        const string&              getDebugChannelToken() const { return _debugChannelToken; }
        int32                      getProcessCount() const { return static_cast<int32>( _listProcess.size() ); }
        EditorNetPlayProcessStatus getProcessStatus( int32 processIndex ) const;
        /** @brief 패널 필터용 최근 줄입니다. `update` 가 채웁니다. */
        const vector<EditorNetPlayLogLine>& getLogLines() const { return _listLogLine; }
        /** @brief 지난 `stop` 이 합친 트레이스 경로입니다. 없으면 빈 문자열입니다. */
        const string& getMergedTracePath() const { return _mergedTracePath; }
        /**
         * @brief 지난 `stop` 이 합쳤고 아직 아무도 열지 않은 트레이스 경로를 한 번 내줍니다. 다음 호출부터는 빈 문자열입니다.
         * @details 멈추는 길이 셋(툴바 · Session 탭 · `netplay.stop`)이라 여는 일을 각 길에 두지 않는다 — Profiler 창의 프레임 단계가 프레임마다 이것을 묻고 연다.
         */
        [[nodiscard]] string takeUnopenedMergedTrace();
        /** @brief 사용자 프리셋입니다. 패널이 저장 · 고르기 · 지우기를 하고, 고른 구성을 `getSettings` 에 복사합니다. */
        EditorNetPlayPresetStore& getPresetStore() { return _presetStore; }

        /** @brief 이 플랫폼의 기본 실행 파일 경로입니다. 에디터 실행 파일 옆이며, Windows 에서는 @p pBaseName 에 `.exe` 를 붙입니다. */
        static string makeDefaultExecutablePath( const utf8* pBaseName );
        /**
         * @brief `App` 창 @p windowIndex 번째(0 부터 — 리슨 호스트가 있으면 호스트가 0)의 창 크기 · 위치 인자(`-W=… -H=… -WinX=… -WinY=…`)입니다.
         * @details 창은 왼쪽 위부터 `_windowColumnCount` 개씩 한 줄에 놓고 다음 줄로 내려간다. 칸 사이는 창 틀(제목 줄 · 테두리)만큼 띄운다 — 위치는 틀의 왼쪽 위이고
         *          크기는 클라이언트 영역이기 때문이다. 창 배치를 끈 구성이면 빈 문자열이다.
         */
        static string makeWindowArguments( const EditorNetPlaySettings& settings, int32 windowIndex );
        /** @brief 비어 있는 UDP 포트 하나를 고릅니다. 포트를 잠깐 열었다 닫으므로 고른 뒤에 다른 프로세스가 가져갈 수 있습니다. 고르지 못하면 0 입니다. */
        static uint16 findFreeUDPPort();
        /** @brief 루프백의 비어 있는 TCP 포트 하나를 고릅니다(디버그 채널). 고른 뒤에 다른 프로세스가 가져갈 수 있는 것은 UDP 와 같습니다. 고르지 못하면 0 입니다. */
        static uint16 findFreeStreamPort();

    private:
        struct ChildProcess;

        /** @brief 읽기 스레드 본문입니다. 파이프가 닫힐 때까지 줄을 쌓고 종료 코드를 수집합니다. */
        static void readChildOutput( ChildProcess* pChild );
        /** @brief 프로세스 하나를 띄우고 읽기 스레드를 붙입니다. */
        [[nodiscard]] bool launchChild( const string& label, const string& executablePath, const string& arguments, const string& tracePath );
        /** @brief 트레이스 폴더 안의 `<이름>.trace.json` 경로를 만듭니다. 트레이스가 꺼져 있으면 빈 문자열입니다. */
        string makeTracePath( const string& fileStem ) const;
        /** @brief 트레이스를 남긴 프로세스의 파일을 모아 합칩니다. 둘 미만이면 빈 문자열입니다. */
        string mergeTraces();

        vector<unique_ptr<ChildProcess>> _listProcess;
        vector<EditorNetPlayLogLine>     _listLogLine;
        vector<EditorNetPlayLogLine>     _listDrainScratch;
        string                           _traceDirectory;
        string                           _mergedTracePath;
        string                           _debugChannelToken; ///< 전용 서버의 디버그 채널 토큰(세션마다 새로). 채널이 없으면 비어 있다
        EditorNetPlaySettings            _settings;          ///< 실행 중인 세션의 구성
        EditorNetPlaySettings            _pendingSettings;   ///< 다음 세션의 구성(`getSettings`)
        EditorNetPlayPresetStore         _presetStore;
        uint16                           _port;
        uint16                           _debugChannelPort;     ///< 전용 서버의 디버그 채널 TCP 포트. 채널이 없으면 0
        bool                             _bMergedTraceUnopened; ///< `_mergedTracePath` 를 아직 `takeUnopenedMergedTrace` 가 내주지 않았다
    };
} // namespace sw::editor
