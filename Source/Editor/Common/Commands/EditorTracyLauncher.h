/**
 * @file EditorTracyLauncher.h
 * @brief 에디터의 "Tracy 열기" — Tracy 출력을 켜고 같은 판(0.13.1) 외부 뷰어를 띄워 localhost 에 붙입니다(ImGui 없음).
 * @details 언리얼 에디터 → Unreal Insights 와 같은 방식입니다. Tracy 뷰어를 에디터 도킹 창으로 넣지 않는 이유는 `Source/Engine/Utility/Profiling/README.md`
 *          ("에디터 안의 뷰어") 에 있습니다 — 뷰어는 자기 ImGui(판 고정) · capstone · freetype · zstd · nfd · curl … 를 끌고 와 한 프로세스에
 *          ImGui 두 벌을 만들거나 에디터의 ImGui 판을 묶습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    /**
     * @enum EditorTracyLaunchResult
     * @brief "Tracy 열기" 의 결과입니다. 패널이 그대로 사용자에게 보여 줍니다.
     */
    enum class EditorTracyLaunchResult : uint8
    {
        Launched,          ///< 출력을 켜고 뷰어를 띄웠다
        TracyNotCompiled,  ///< 이 빌드에 Tracy 가 없다(Shipping · SW_ENABLE_TRACY=OFF)
        ViewerNotFound,    ///< 뷰어 실행 파일을 찾지 못했다(`gv_tracyViewerPath` · Tools/Tracy)
        ViewerLaunchFailed ///< 뷰어 프로세스를 띄우지 못했다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorTracyLauncher
     * @brief 뷰어 경로 찾기 · 명령 만들기 · 띄우기입니다. 찾기와 명령은 순수 함수라 `EditorTest` 가 시험합니다.
     */
    struct EditorTracyLauncher
    {
        /** @brief 저장소 안 기본 자리(저장소에 넣지 않는다 — `.gitignore` 의 Tools/Tracy/)입니다. */
        static constexpr auto kDefaultViewerFolder = "Tools/Tracy";

        /** @brief 이 플랫폼의 뷰어 실행 파일 이름입니다(Windows 릴리스 zip 의 `tracy-profiler.exe`). */
        static const utf8* getViewerFileName();

        /**
         * @brief 뷰어 실행 파일을 찾습니다. 순서: @p configuredPath(파일이거나 그 파일이 든 폴더) → `<projectRoot>/Tools/Tracy/<이름>`.
         * @return 있는 파일을 찾았으면 true 입니다.
         */
        [[nodiscard]] static bool findViewerPath( string_view configuredPath, string_view projectRoot, string& outPath );

        /** @brief 뷰어를 localhost 의 @p port 에 붙여 띄우는 명령 한 줄입니다(경로는 따옴표로 감싼다). */
        static string makeViewerCommand( string_view viewerPath, uint16 port );

        /** @brief Tracy 출력을 켜고(이미 켜져 있으면 그대로) 뷰어를 띄웁니다. `gv_tracyViewerPath` 와 프로젝트 루트로 찾습니다. */
        static EditorTracyLaunchResult openViewer();

        /** @brief 결과의 사람이 읽는 한 줄입니다(영문 — 로그 · 패널 공용). */
        static const utf8* describeResult( EditorTracyLaunchResult result );
    };
} // namespace sw::editor
