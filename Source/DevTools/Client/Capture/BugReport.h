/**
 * @file BugReport.h
 * @brief 버그 리포트를 한 번에 만듭니다. 개발 명령 `bugreport [메모]` 가 스크린샷, 로그, 씬, 카메라 위치, 재현 명령을 폴더 하나에 모으고, `bugreport.goto [폴더]` 가 그 위치로 돌아갑니다.
 * @details 언리얼의 개발 명령 `BugIt` / `BugItGo` 에 해당합니다(이름은 내용으로 — 언리얼 이름은 뜻이 보이지 않는다). 폴더는 `Saved/BugReports/<yyyyMMdd-HHmmss>/`(같은 초면 `-2` …)이고 zip 은 만들지 않습니다
 *          (폴더가 첨부하기 쉽고 의존이 늘지 않습니다). 게임 창(`~` 콘솔)과 에디터(Output Log 입력 줄, Report Bug 커맨드)가 같은 명령을 씁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "DevTools/DevToolsAPI.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Capture/ScreenshotLocalTime.h"

namespace sw
{
    struct BugReportInputs;

    class FrameProfiler;
    class InputReplay;
    class SceneManager;

    /**
     * @struct BugReport
     * @brief bugreport 한 번에 남길 값과 그 파일을 읽고 쓰는 함수입니다. 파일 쓰기와 읽기는 ImGui 나 렌더러 없이 테스트할 수 있습니다(`BugReportTest`).
     * @details `info.txt` 는 `키 = 값` 한 줄씩입니다. 읽을 때 모르는 키는 건너뜁니다(필드가 늘어도 이전 폴더를 읽을 수 있습니다).
     */
    struct SW_DEVTOOLS_API BugReport
    {
        /** @brief 저장 폴더(`constant::path::kSavedFolder`) 아래 BugReport 폴더 이름입니다. */
        static constexpr const utf8* kFolderName = "BugReports";
        /** @brief 정보 파일 이름입니다. */
        static constexpr const utf8* kInfoFileName = "info.txt";
        /** @brief 스크린샷 파일 이름입니다(다음 프레임에 쓰입니다). */
        static constexpr const utf8* kScreenshotFileName = "screenshot.png";
        /** @brief 로그 사본 파일 이름입니다. */
        static constexpr const utf8* kLogFileName = "log.txt";
        /** @brief 씬 사본 파일 이름입니다(그 순간의 활성 씬이며 런타임에 생긴 오브젝트를 포함합니다). */
        static constexpr const utf8* kSceneFileName = "scene.scene.xml";
        /** @brief 재현 명령 파일 이름입니다. */
        static constexpr const utf8* kReproFileName = "repro.txt";
        /** @brief trace 파일 이름입니다(타임라인 기록이 켜져 있었으면 최근 프레임의 Chrome Trace Event, Dev 만). */
        static constexpr const utf8* kTraceFileName = "trace.trace.json";
        /** @brief 세션 입력 기록 파일 이름입니다(`bugreport.input.start` 로 기록한 프레임이 있을 때만). `bugreport.replay` 가 다시 붙인다. */
        static constexpr const utf8* kInputFileName = "input.swreplay";
        /** @brief 세션 입력 기록의 프레임 상한입니다(60 fps 로 10 분 — 고정 용량, 넘으면 기록이 스스로 멈춘다). */
        static constexpr uint32 kMaxInputFrameCount = 36000;

        string  _note;
        string  _scenePath;          ///< 활성 씬의 원본 경로(사본이 아니라 원래 씬)
        string  _rhiBackend;         ///< 디바이스 이름(`IRHIDevice::getBackendName`)
        string  _buildConfiguration; ///< `sw::build::kConfigName`
        string  _game;               ///< 팩 설정의 창 제목(`PackEngineSettings::_windowTitle`)
        string  _platform;           ///< `sw::build::kPlatformName`
        string  _target;             ///< `sw::build::kTargetName`(App · Server …)
        string  _sceneHash;          ///< 씬 원본 파일 바이트의 FNV-1a 64(16 진). 원본을 읽지 못하면 빈 값 — 재생 전에 "같은 씬인가" 를 대조한다
        string  _configHash;         ///< 호스트 설정 폴더(`config::kDirConfig`) 파일 전부(상대 경로 + 바이트)의 FNV-1a 64. 비면 읽지 못한 것
        float3  _cameraPosition{};
        float3  _cameraRotationDegrees{}; ///< pitch, yaw, roll(도). 시선은 `(sin yaw cos pitch, -sin pitch, cos yaw cos pitch)` 이며 뷰포트 카메라와 같은 규칙입니다
        uint32  _inputFrameCount{ 0 };    ///< 담은 세션 입력 기록의 프레임 수(0 이면 기록 없음)
        float32 _inputSeconds{ 0.0f };    ///< 담은 세션 입력 기록의 길이(초)

        /** @brief `<directory>/info.txt` 를 씁니다. */
        [[nodiscard]] bool writeInfo( string_view directory ) const;
        /** @brief `<directory>/info.txt` 를 읽습니다. 모르는 키는 건너뜁니다. 파일이 없으면 false 입니다. */
        [[nodiscard]] static bool readInfo( string_view directory, BugReport& outReport );
        /**
         * @brief `<root>/<yyyyMMdd-HHmmss>` 폴더를 만들고 경로를 돌려줍니다. 같은 이름이 이미 있으면 `-2`, `-3` … 을 붙입니다.
         * @return 만든 폴더 경로. 만들지 못하면 빈 문자열입니다.
         */
        static string makeUniqueDirectory( string_view root, const ScreenshotLocalTime& localTime );
        /** @brief @p root 아래 BugReport 폴더 가운데 가장 최근(이름 순으로 마지막)의 경로입니다. 없으면 빈 문자열입니다. */
        static string findLatestDirectory( string_view root );
        /** @brief 시선 벡터를 pitch 와 yaw(도, roll 0)로 바꿉니다. */
        static float3 computeRotationDegrees( const float3& forward );
        /** @brief pitch 와 yaw(도)를 시선 벡터로 바꿉니다. */
        static float3 computeForward( const float3& rotationDegrees );
        /** @brief 바이트의 FNV-1a 64 를 16 자리 16 진 글로 씁니다(재현 환경 해시의 한 모양). */
        static string formatHash( uint64 hash );
        /**
         * @brief @p directory 아래 파일 전부(하위 폴더 포함)를 상대 경로 순으로 정렬해 "상대 경로 + 바이트" 를 FNV-1a 64 로 접습니다.
         * @details 순서를 정렬로 고정해 파일 시스템의 열거 순서가 해시를 바꾸지 않는다. 한 파일이라도 못 읽으면 false(부분 해시를 같은 설정으로 믿지 않는다).
         */
        [[nodiscard]] static bool computeFolderHash( string_view directory, uint64& outHash );

        /**
         * @brief 지금 엔진 상태에서 bugreport 입력을 모읍니다. 전역 엔진 서비스를 읽는 곳은 이 함수 하나입니다.
         * @details 카메라는 보이는 시점입니다. 뷰포트 카메라(`CameraRole::Viewport`)가 있으면 그것, 없으면 활성 게임 카메라를 씁니다.
         *          로거를 여기서 비웁니다(비동기라 비우지 않으면 bugreport 직전 줄이 사본에 없습니다).
         *          입력은 `InputManager` 의 세션 입력 기록(`bugreport.input.start` · `stop`)이 프레임을 가졌을 때만 담습니다 — 늘 도는 녹화가 아닙니다.
         *          재현 환경(플랫폼 · 타깃 · 씬 원본 해시 · 설정 폴더 해시)을 같이 적어, 재생 전에 같은 빌드 · 콘텐츠인지 대조할 수 있게 합니다.
         */
        [[nodiscard]] static BugReportInputs makeInputs( string_view note );
        /**
         * @brief @p inputs 를 `<루트>/<시각>/` 에 모읍니다. info.txt, 다음 프레임 screenshot.png, 로그 사본, 활성 씬 사본, trace, repro.txt 를 만듭니다.
         * @details 엔진 서비스를 직접 찾지 않습니다. 비어 있는 입력(서비스 포인터 null · 빈 로그 폴더)은 그 파일을 건너뜁니다.
         * @param outDirectory 만든 폴더(작업 폴더 기준 상대 경로)입니다.
         * @return 폴더와 info.txt 를 썼으면 true 입니다. 나머지(스크린샷, 로그, 씬)는 할 수 있는 만큼 남기고 실패는 경고로 알립니다.
         */
        [[nodiscard]] static bool capture( const BugReportInputs& inputs, string& outDirectory );
        /**
         * @brief @p directory(비면 `Saved/BugReports` 아래 가장 최근 폴더)의 info.txt 를 읽어 그 씬을 @p sceneManager 로 열고(활성 씬과 다를 때) 보이는 카메라를 그 위치와 각도로 옮깁니다.
         * @details 뷰포트 카메라를 옮기면 그 뷰포트를 그리는 도구가 옮긴 위치를 이어받습니다.
         */
        [[nodiscard]] static bool goTo( SceneManager& sceneManager, string_view directory, string& outReply );
        /**
         * @brief @p directory 의 재현 환경을 지금 값(@p current)과 대조해 다른 칸을 `칸: 기록 -> 지금` 줄로 돌려줍니다. 같으면 빈 목록입니다.
         * @details 빈 칸(기록하지 못한 값)은 대조하지 않는다. 입력 재생(`bugreport.replay`)은 다른 칸이 있으면 경고를 남기고 재생한다 — 결정론 재생을 약속하지 않는다.
         */
        static vector<string> findEnvironmentMismatches( const BugReport& recorded, const BugReport& current );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct BugReportInputs
     * @brief `BugReport::capture` 가 쓰는 입력 전부입니다. 엔진에서는 `BugReport::makeInputs` 가 채우고, 테스트는 가짜 값을 넣습니다.
     */
    struct BugReportInputs
    {
        BugReport            _report;                      ///< info.txt 에 쓸 값(메모, 씬, 백엔드, 빌드, 게임, 카메라)
        string               _root;                        ///< BugReport 폴더들의 루트(엔진에서는 `Saved/BugReports`)
        ScreenshotLocalTime  _localTime{};                 ///< 폴더 이름의 시각
        string               _logFilePath;                 ///< log.txt 로 복사할 이 프로세스의 로그 파일(`ILogSink::getLogFilePath`). 비면 건너뛴다
        string               _backendFlag;                 ///< 재현 명령의 백엔드 플래그(쿠킹 표의 명령줄 이름 `dx12` · `wgpu`, `RHIBackendInfoTable::findCommandLineName`). 장치가 없으면 빔
        SceneManager*        _pSceneManager{ nullptr };    ///< 활성 씬 사본을 저장할 씬 관리자. 활성 씬이 없으면 null
        const FrameProfiler* _pFrameProfiler{ nullptr };   ///< 최근 프레임 trace 를 담을 프로파일러. null 이면 건너뛴다
        const InputReplay*   _pInputRecording{ nullptr };  ///< 세션 입력 기록(`InputManager::getSessionRecording`). null 이거나 프레임이 없으면 input.swreplay 를 건너뛴다
        bool                 _bEditor{ false };            ///< 재현 명령에 `-EnableEditor` 를 붙인다 NOLINT(sw-bool-flag): 참조(`bool&`)로 넘긴다
        bool                 _bRequestScreenshot{ false }; ///< 다음 프레임 스크린샷을 렌더 스레드에 요청한다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw
