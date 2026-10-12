/**
 * @file EditorNetPlaySession.h
 * @brief 에디터의 멀티 프로세스 재생을 담당합니다. 서버와 클라이언트 N 개를 자식 프로세스로 실행해 연결하고, 로그를 한곳에 모으고, Stop 할 때 트레이스를 합칩니다. ImGui 에 의존하지 않습니다.
 * @details 언리얼 PIE 의 Net Mode(Play As Listen Server, Play As Client 와 "Run Under One Process" 끔), 유니티의 Multiplayer Play Mode 에 해당한다.
 *          이미 있는 실행 파일만 쓴다. 서버는 전용이면 `Server`, 리슨 호스트이면 첫 `App` 이고, 클라이언트는 `App` 이다. 각 프로세스에 넘기는 인자는 다음과 같다.
 *
 *          | 프로세스 | 인자 |
 *          |---|---|
 *          | 서버 | `-gv_netListenPort=<포트>` (+ 트레이스면 `-gv_traceSaveOnExit=<폴더>/server.trace.json`) |
 *          | 클라이언트 | `-gv_netConnectAddress=127.0.0.1:<포트> -gv_standardInputCommands=1` (+ `-gv_traceSaveOnExit=<폴더>/clientN.trace.json`) |
 *
 *          - 로그: 프로세스마다 읽기 스레드가 줄을 모은다. `update` 가 게임 스레드에서 `[Server]`, `[Client1]` 같은 태그를 붙여 에디터 로그에 다시 쓰므로
 *            Output Log 하나에 모인다. 패널에서 출처별로 거를 수 있도록 최근 줄도 보관한다.
 *          - Stop: 모든 프로세스의 표준 입력에 `quit` 를 보내 정상 종료시키고(트레이스는 이때 쓰인다), 시한 안에 끝나지 않으면 강제 종료한다.
 *            트레이스를 켠 세션이면 남은 파일을 `ProfilerTraceExport::mergeTraceJSON` 으로 `<폴더>/merged.trace.json` 에 합친다.
 *            같은 PC 이므로 단조 시계를 그대로 맞출 수 있어 정확하다.
 *          - 하나가 먼저 죽으면 `update` 가 종료 코드와 마지막 줄을 함께 경고한다.
 *          - 서버 일시정지와 한 틱 진행은 서버 표준 입력에 `pause`, `step N`, `resume` 을 보내서 처리한다(전용 서버의 Dev 명령).
 *          - 회선 조절은 그 프로세스의 표준 입력에 `net.emulation …` 을 보낸다(`sendProcessCommand`). 답 줄 가운데 `NetEmulationReport` 줄은 프로세스마다 마지막 것을 보관한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Transport/NetEmulation.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /** @brief 재생의 네트워크 모드입니다(언리얼 PIE 의 Net Mode). */
    enum class EditorNetMode : uint8
    {
        Standalone = 0,  ///< 에디터 안에서 재생한다. 이 세션을 쓰지 않으며 `start` 가 false 를 반환한다
        DedicatedServer, ///< `Server` 하나와 `App` 클라이언트 N 개
        ListenHost       ///< 첫 `App` 이 리슨 호스트이고 나머지가 `App` 클라이언트이다. 플레이어 수에 호스트가 포함된다
    };

    const utf8* toString( EditorNetMode mode );
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 세션 하나의 구성입니다. 패널의 프리셋이 이 구성을 저장합니다. */
    struct EditorNetPlaySettings
    {
        string        _serverExecutablePath{}; ///< 비어 있으면 에디터 실행 파일 옆의 `Server`
        string        _clientExecutablePath{}; ///< 비어 있으면 에디터 실행 파일 옆의 `App`
        string        _clientArguments{};      ///< 클라이언트마다 붙이는 인자(백엔드 스위치, 창 크기)
        string        _traceDirectory{};       ///< 비어 있으면 `Saved/Traces/netplay_<유닉스 밀리초>`
        float32       _stopTimeoutSeconds{ 15.0f };
        int32         _clientCount{ 2 }; ///< 플레이어 수. 전용 서버이면 클라이언트 수이고, 리슨 호스트이면 호스트를 포함한 수이다
        uint16        _port{ 0 };        ///< 0 이면 비어 있는 UDP 포트를 고른다
        EditorNetMode _mode{ EditorNetMode::Standalone };
        bool          _bRecordTraces{ true };
    };
} // namespace sw::editor

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
    class SW_EDITOR_API EditorNetPlaySession
    {
    public:
        /** @brief 패널 필터용으로 보관하는 최근 줄 수입니다. 넘으면 오래된 줄부터 버리며, 에디터 로그에는 모두 남습니다. */
        static constexpr size_t kMaxLogLine = 4000;
        /** @brief 서버(전용 서버 · 리슨 호스트)의 프로세스 번호입니다. 클라이언트는 1 부터입니다. */
        static constexpr int32 kServerProcessIndex = 0;
        /** @brief 합친 트레이스의 파일 이름입니다. */
        static constexpr const utf8* kMergedTraceFileName = "merged.trace.json";

        EditorNetPlaySession();
        ~EditorNetPlaySession();

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
        EditorNetPlaySettings&     getSettings() { return _pendingSettings; }
        uint16                     getPort() const { return _port; }
        int32                      getProcessCount() const { return static_cast<int32>( _listProcess.size() ); }
        EditorNetPlayProcessStatus getProcessStatus( int32 processIndex ) const;
        /** @brief 패널 필터용 최근 줄입니다. `update` 가 채웁니다. */
        const vector<EditorNetPlayLogLine>& getLogLines() const { return _listLogLine; }
        /** @brief 지난 `stop` 이 합친 트레이스 경로입니다. 없으면 빈 문자열입니다. */
        const string& getMergedTracePath() const { return _mergedTracePath; }

        /** @brief 이 플랫폼의 기본 실행 파일 경로입니다. 에디터 실행 파일 옆이며, Windows 에서는 @p pBaseName 에 `.exe` 를 붙입니다. */
        static string makeDefaultExecutablePath( const utf8* pBaseName );
        /** @brief 비어 있는 UDP 포트 하나를 고릅니다. 포트를 잠깐 열었다 닫으므로 고른 뒤에 다른 프로세스가 가져갈 수 있습니다. 고르지 못하면 0 입니다. */
        static uint16 findFreeUDPPort();

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
        EditorNetPlaySettings            _settings;        ///< 실행 중인 세션의 구성
        EditorNetPlaySettings            _pendingSettings; ///< 다음 세션의 구성(`getSettings`)
        uint16                           _port;
    };
} // namespace sw::editor
