/**
 * @file BugItReport.h
 * @brief 버그 리포트를 한 번에 만듭니다. 개발 명령 `bugit [메모]` 가 스크린샷, 로그, 씬, 카메라 위치, 재현 명령을 폴더 하나에 모으고, `bugitgo [폴더]` 가 그 위치로 돌아갑니다.
 * @details 언리얼 `BugIt` / `BugItGo` 에 해당합니다. 폴더는 `Saved/BugIt/<yyyyMMdd-HHmmss>/`(같은 초면 `-2` …)이고 zip 은 만들지 않습니다
 *          (폴더가 첨부하기 쉽고 의존이 늘지 않습니다). 게임 창(`~` 콘솔)과 에디터(Output Log 입력 줄, Report Bug 커맨드)가 같은 명령을 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Capture/ScreenshotPathUtil.h"

namespace sw
{
    struct BugItInputs;

    class FrameProfiler;
    class SceneManager;

    /**
     * @struct BugItReport
     * @brief bugit 한 번에 남길 값과 그 파일을 읽고 쓰는 함수입니다. 파일 쓰기와 읽기는 ImGui 나 렌더러 없이 테스트할 수 있습니다(`BugItReportTest`).
     * @details `info.txt` 는 `키 = 값` 한 줄씩입니다. 읽을 때 모르는 키는 건너뜁니다(필드가 늘어도 이전 폴더를 읽을 수 있습니다).
     */
    struct SW_API BugItReport
    {
        /** @brief 저장 폴더(`path::kSavedFolder`) 아래 BugIt 폴더 이름입니다. */
        static constexpr const utf8* kFolderName = "BugIt";
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
        /** @brief 타임라인 기록이 켜져 있었으면 최근 프레임의 Chrome Trace Event 파일입니다(Dev 만). */
        static constexpr const utf8* kTraceFileName = "trace.trace.json";

        string _note;
        string _scenePath;          ///< 활성 씬의 원본 경로(사본이 아니라 원래 씬)
        string _rhiBackend;         ///< 디바이스 이름(`IRHIDevice::getBackendName`)
        string _buildConfiguration; ///< `sw::build::kConfigName`
        string _game;               ///< 팩 설정의 창 제목(`PackEngineSettings::_windowTitle`)
        float3 _cameraPosition{};
        float3 _cameraRotationDegrees{}; ///< pitch, yaw, roll(도). 시선은 `(sin yaw cos pitch, -sin pitch, cos yaw cos pitch)` 이며 뷰포트 카메라와 같은 규칙입니다

        /** @brief `<directory>/info.txt` 를 씁니다. */
        [[nodiscard]] bool writeInfo( string_view directory ) const;
        /** @brief `<directory>/info.txt` 를 읽습니다. 모르는 키는 건너뜁니다. 파일이 없으면 false 입니다. */
        [[nodiscard]] static bool readInfo( string_view directory, BugItReport& outReport );
        /**
         * @brief `<root>/<yyyyMMdd-HHmmss>` 폴더를 만들고 경로를 돌려줍니다. 같은 이름이 이미 있으면 `-2`, `-3` … 을 붙입니다.
         * @return 만든 폴더 경로. 만들지 못하면 빈 문자열입니다.
         */
        static string makeUniqueDirectory( string_view root, const ScreenshotLocalTime& localTime );
        /** @brief @p root 아래 BugIt 폴더 가운데 가장 최근(이름 순으로 마지막)의 경로입니다. 없으면 빈 문자열입니다. */
        static string findLatestDirectory( string_view root );
        /** @brief 시선 벡터를 pitch 와 yaw(도, roll 0)로 바꿉니다. */
        static float3 computeRotationDegrees( const float3& forward );
        /** @brief pitch 와 yaw(도)를 시선 벡터로 바꿉니다. */
        static float3 computeForward( const float3& rotationDegrees );
        /** @brief 디바이스 이름(`Direct3D 12` …)에 맞는 App 명령줄 백엔드 플래그(`dx12` …)입니다. 모르면 빈 문자열입니다. */
        static const utf8* findBackendFlag( string_view backendName );

        /**
         * @brief 지금 엔진 상태에서 bugit 입력을 모읍니다. 전역 엔진 서비스를 읽는 곳은 이 함수 하나입니다.
         * @details 카메라는 보이는 시점입니다. 뷰포트 카메라(`CameraRole::Viewport`)가 있으면 그것, 없으면 활성 게임 카메라를 씁니다.
         *          로거를 여기서 비웁니다(비동기라 비우지 않으면 bugit 직전 줄이 사본에 없습니다).
         *          입력 녹화는 담지 않습니다(전역 녹화기가 없습니다. Input Map 패널의 녹화는 그 패널의 것입니다).
         */
        [[nodiscard]] static BugItInputs makeInputs( string_view note );
        /**
         * @brief @p inputs 를 `<루트>/<시각>/` 에 모읍니다. info.txt, 다음 프레임 screenshot.png, 로그 사본, 활성 씬 사본, trace, repro.txt 를 만듭니다.
         * @details 엔진 서비스를 직접 찾지 않습니다. 비어 있는 입력(서비스 포인터 null · 빈 로그 폴더)은 그 파일을 건너뜁니다.
         * @param outDirectory 만든 폴더(작업 폴더 기준 상대 경로)입니다.
         * @return 폴더와 info.txt 를 썼으면 true 입니다. 나머지(스크린샷, 로그, 씬)는 할 수 있는 만큼 남기고 실패는 경고로 알립니다.
         */
        [[nodiscard]] static bool capture( const BugItInputs& inputs, string& outDirectory );
        /**
         * @brief @p directory(비면 `Saved/BugIt` 아래 가장 최근 폴더)의 info.txt 를 읽어 그 씬을 @p sceneManager 로 열고(활성 씬과 다를 때) 보이는 카메라를 그 위치와 각도로 옮깁니다.
         * @details 뷰포트 카메라를 옮기면 그 뷰포트를 그리는 도구가 옮긴 위치를 이어받습니다.
         */
        [[nodiscard]] static bool goTo( SceneManager& sceneManager, string_view directory, string& outReply );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct BugItInputs
     * @brief `BugItReport::capture` 가 쓰는 입력 전부입니다. 엔진에서는 `BugItReport::makeInputs` 가 채우고, 테스트는 가짜 값을 넣습니다.
     */
    struct BugItInputs
    {
        BugItReport          _report;                      ///< info.txt 에 쓸 값(메모, 씬, 백엔드, 빌드, 게임, 카메라)
        string               _root;                        ///< BugIt 폴더들의 루트(엔진에서는 `Saved/BugIt`)
        ScreenshotLocalTime  _localTime{};                 ///< 폴더 이름의 시각
        string               _logFolderPath;               ///< 가장 최근 .txt 를 log.txt 로 복사할 로그 폴더. 비면 건너뛴다
        SceneManager*        _pSceneManager{ nullptr };    ///< 활성 씬 사본을 저장할 씬 관리자. 활성 씬이 없으면 null
        const FrameProfiler* _pFrameProfiler{ nullptr };   ///< 최근 프레임 trace 를 담을 프로파일러. null 이면 건너뛴다
        bool                 _bEditor{ false };            ///< 재현 명령에 `-EnableEditor` 를 붙인다
        bool                 _bRequestScreenshot{ false }; ///< 다음 프레임 스크린샷을 렌더 스레드에 요청한다
    };
} // namespace sw
