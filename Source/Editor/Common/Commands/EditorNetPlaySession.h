/**
 * @file EditorNetPlaySession.h
 * @brief 에디터의 멀티 프로세스 재생 — 서버와 클라이언트 N 개를 자식 프로세스로 띄워 붙이고, 로그를 한곳에 모으고, Stop 때 트레이스를 합칩니다(ImGui 없음).
 * @details 언리얼 PIE 의 Net Mode(Play As Listen Server · Play As Client + "Run Under One Process" 끔)와 유니티 Multiplayer Play Mode 의 자리입니다.
 *          있는 실행 파일만 씁니다 — 서버는 `Server`(전용) 또는 첫 `App`(리슨 호스트), 클라이언트는 `App` 입니다. 각 프로세스에 넘기는 인자:
 *
 *          | 프로세스 | 인자 |
 *          |---|---|
 *          | 서버 | `-gv_netListenPort=<포트>` (+ 트레이스면 `-gv_traceSaveOnExit=<폴더>/server.trace.json`) |
 *          | 클라이언트 | `-gv_netConnectAddress=127.0.0.1:<포트> -gv_standardInputCommands=1` (+ `-gv_traceSaveOnExit=<폴더>/clientN.trace.json`) |
 *
 *          - 로그: 프로세스마다 읽기 스레드가 줄을 모으고, `update` 가 게임 스레드에서 `[Server]` · `[Client1]` 꼬리표를 붙여 에디터 로그에 다시 씁니다
 *            (Output Log 하나에 모인다). 패널의 출처 거르기용으로 최근 줄도 들고 있습니다.
 *          - Stop: 모든 프로세스의 표준 입력에 `quit` 를 보내 정상 종료시키고(트레이스가 그때 써진다), 시한 안에 끝나지 않으면 강제 종료합니다.
 *            트레이스를 켠 세션이면 남은 파일을 `ProfilerTraceExport::mergeTraceJSON` 으로 `<폴더>/merged.trace.json` 에 합칩니다 — 같은 PC 라
 *            단조 시계를 그대로 맞춘다(정확).
 *          - 하나가 먼저 죽으면 `update` 가 종료 코드 · 마지막 줄과 함께 경고합니다.
 *          - 서버 일시정지 · 한 틱 진행은 서버 표준 입력에 `pause` · `step N` · `resume` 을 보냅니다(전용 서버의 Dev 명령).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw::editor
{
    /** @brief 재생의 네트워크 모양입니다(언리얼 PIE 의 Net Mode). */
    enum class EditorNetMode : uint8
    {
        Standalone = 0,  ///< 에디터 안 재생 — 이 세션을 쓰지 않는다(`start` 가 false)
        DedicatedServer, ///< `Server` 하나 + `App` 클라이언트 N
        ListenHost       ///< 첫 `App` 이 리슨 호스트 + 나머지 `App` 클라이언트(플레이어 수에 호스트가 든다)
    };

    const utf8* toString( EditorNetMode mode );
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 세션 하나의 구성입니다 — 패널의 프리셋이 이것을 저장합니다. */
    struct EditorNetPlaySettings
    {
        string        _serverExecutablePath{}; ///< 비면 에디터 실행 파일 옆의 `Server`
        string        _clientExecutablePath{}; ///< 비면 에디터 실행 파일 옆의 `App`
        string        _clientArguments{};      ///< 클라이언트마다 붙이는 인자(백엔드 스위치 · 창 크기)
        string        _traceDirectory{};       ///< 비면 `Saved/Traces/netplay_<유닉스 밀리초>`
        float32       _stopTimeoutSeconds{ 15.0f };
        int32         _clientCount{ 2 }; ///< 플레이어 수 — 전용 서버면 클라이언트 수, 리슨 호스트면 호스트를 넣은 수
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
    /** @brief 패널이 그리는 프로세스 하나의 상태 사본입니다. */
    struct EditorNetPlayProcessStatus
    {
        string _label;
        string _lastLine;
        string _tracePath;
        int32  _processID{ 0 };
        int32  _exitCode{ 0 };
        bool   _bRunning{ false };
        bool   _bConnected{ false }; ///< 클라이언트 — 서버에 붙었다는 줄(`Net session connected to`)을 남겼다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorNetPlaySession
     * @brief 서버 · 클라이언트 프로세스 묶음 하나입니다. `start` → (프레임마다 `update`) → `stop` 입니다. 게임(UI) 스레드에서만 부릅니다.
     */
    class EditorNetPlaySession
    {
    public:
        /** @brief 패널 거르기용으로 들고 있는 최근 줄 수입니다(넘으면 오래된 것부터 버린다 — 에디터 로그에는 다 남는다). */
        static constexpr size_t kMaxLogLine = 4000;
        /** @brief 합친 트레이스의 파일 이름입니다. */
        static constexpr const utf8* kMergedTraceFileName = "merged.trace.json";

        EditorNetPlaySession();
        ~EditorNetPlaySession();

        EditorNetPlaySession( const EditorNetPlaySession& )            = delete;
        EditorNetPlaySession& operator=( const EditorNetPlaySession& ) = delete;

        /** @brief 프로세스들을 띄웁니다. 이미 돌고 있거나 하나라도 못 띄우면(띄운 것은 끄고) false 입니다 — 이유는 로그에. */
        [[nodiscard]] bool start( const EditorNetPlaySettings& settings );
        /**
         * @brief 모두 정상 종료시키고(시한을 넘으면 강제 종료) 읽기 스레드를 거둡니다. 트레이스를 켰으면 합칩니다.
         * @return 합친 트레이스 경로 — 트레이스가 꺼졌거나 둘 미만이면 빈 글.
         */
        string stop();
        /** @brief 모은 줄을 에디터 로그로 옮기고, 먼저 끝난 프로세스를 알립니다. 프레임마다 부릅니다. */
        void update();

        /** @brief 서버(전용 서버 또는 리슨 호스트)의 표준 입력에 한 줄을 보냅니다(`pause` · `step 3` · `resume` · 개발 명령). 보냈으면 true 입니다. */
        [[nodiscard]] bool sendServerCommand( string_view command );

        bool isRunning() const { return _listProcess.empty() == false; }
        /** @brief 서버에 붙었다고 로그에 남긴 클라이언트 수입니다(`Net session connected to` 줄 — 탐침 · 테스트가 접속을 이것으로 본다). */
        int32 getConnectedClientCount() const;
        /** @brief 패널 · 툴바가 고치는 다음 세션의 구성입니다(`start` 는 받은 구성을 따로 복사한다). */
        EditorNetPlaySettings&     getSettings() { return _pendingSettings; }
        uint16                     getPort() const { return _port; }
        int32                      getProcessCount() const { return static_cast<int32>( _listProcess.size() ); }
        EditorNetPlayProcessStatus getProcessStatus( int32 processIndex ) const;
        /** @brief 패널 거르기용 최근 줄입니다(`update` 가 채운다). */
        const vector<EditorNetPlayLogLine>& getLogLines() const { return _listLogLine; }
        /** @brief 지난 `stop` 이 합친 트레이스(없으면 빈 글)입니다. */
        const string& getMergedTracePath() const { return _mergedTracePath; }

        /** @brief 이 플랫폼의 기본 실행 파일 경로입니다(에디터 실행 파일 옆, @p pBaseName 에 `.exe` 를 붙인다 — Windows). */
        static string makeDefaultExecutablePath( const utf8* pBaseName );
        /** @brief 비어 있는 UDP 포트 하나를 고릅니다(잠깐 열었다 닫는다 — 고른 뒤 다른 프로세스가 가져갈 수 있다). 못 고르면 0 입니다. */
        static uint16 findFreeUDPPort();

    private:
        struct ChildProcess;

        /** @brief 읽기 스레드 본문 — 파이프가 닫힐 때까지 줄을 쌓고 종료 코드를 거둡니다. */
        static void readChildOutput( ChildProcess* pChild );
        /** @brief 프로세스 하나를 띄우고 읽기 스레드를 붙입니다. */
        [[nodiscard]] bool launchChild( const string& label, const string& executablePath, const string& arguments, const string& tracePath );
        /** @brief 트레이스 폴더 안의 `<이름>.trace.json` 경로입니다. 트레이스가 꺼졌으면 빈 글입니다. */
        string makeTracePath( const string& fileStem ) const;
        /** @brief 트레이스를 남긴 프로세스의 파일을 모아 합칩니다. 둘 미만이면 빈 글입니다. */
        string mergeTraces();

        vector<unique_ptr<ChildProcess>> _listProcess;
        vector<EditorNetPlayLogLine>     _listLogLine;
        vector<EditorNetPlayLogLine>     _listDrainScratch;
        string                           _traceDirectory;
        string                           _mergedTracePath;
        EditorNetPlaySettings            _settings;        ///< 돌고 있는 세션의 구성
        EditorNetPlaySettings            _pendingSettings; ///< 다음 세션의 구성(`getSettings`)
        uint16                           _port;
    };
} // namespace sw::editor
