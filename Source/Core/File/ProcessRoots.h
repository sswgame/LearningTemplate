/**
 * @file ProcessRoots.h
 * @brief 프로세스의 루트 폴더 모음입니다(R5). 실행 파일 위치로 **한 번** 정하고 실행 중 작업 폴더는 보지 않는다.
 * @details 예전에는 저장 폴더(`Saved`) 기준이 셋이었다 - 작업 폴더 아래(`getSavedFolderPath` · 상대 `"Saved/…"`), 실행 파일 폴더 아래(셰이더 캐시),
 *          프로젝트 아래(DDC · 쿠킹 저장소 · 트레이스) - 이제 실행 산출물(`_saved`)과 구성이 함께 쓰는 파생물(`_derivedData`) 둘을 이름으로 가른다. 작업 폴더를 바꾸고 띄우면 산출물이 다른 곳에 쌓였다. 여기 한 곳이 답한다.
 *          못 정하면(실행 파일 경로를 모름 · 위로 올라가도 `Content` 가 없음) 처음 묻는 자리에서 멈춘다(폴백 없음). 언리얼 `FPaths::ProjectSavedDir` 자리다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Path/AbsolutePath.h"

namespace sw
{
    /**
     * @brief Android 기동(AndroidMain)이 앱 전용 폴더(`ANativeActivity::internalDataPath`)를 넘기는 환경 변수입니다. APK 안에는 실행 파일 폴더가 없어 그 폴더가 바이너리 폴더를 대신한다 —
     *        `Saved` 가 그 아래 생기고, 팩 폴더(`<그 폴더>/Packs`)는 APK 의 `assets/Packs` 에 걸린다(`BundledContentFileSource`).
     */
    inline constexpr const utf8* kAndroidAppFolderVariable = "SW_ANDROID_APP_FOLDER";

    /**
     * @brief 부모(백엔드별 부모 `AutomationPerBackend` · 두 번 기동 짝)가 자식의 실행 뿌리(`ProcessRoots::_run`)를 넘기는 환경 변수입니다.
     * @details 명령줄 인자(`-run-root=`)가 아니라 환경이다 — 이 표는 명령줄 해석 전에 처음 불리고(로그 파일 자리), Core 에는 언리얼 `FCommandLine` 같은 전역 명령줄이 없다.
     *          사용자 데이터 뿌리(`SW_USER_DATA_ROOT`) · Android 앱 폴더와 같은 길이다. 자식이 띄운 손자도 물려받는다(같은 실행이다).
     */
    inline constexpr const utf8* kRunRootVariable = "SW_RUN_ROOT";

    /**
     * @struct ProcessRoots
     * @brief 프로세스의 루트 폴더들입니다. `get()` 이 처음 부를 때 실행 파일 위치로 정한다.
     */
    struct SW_API ProcessRoots
    {
        AbsolutePath _binary;      ///< 엔진 바이너리 폴더(`Bin` - 실행 파일의 폴더)
        AbsolutePath _project;     ///< 프로젝트 루트 - 바이너리 폴더에서 위로 올라가 `Content` 가 있는 첫 폴더. 없으면(패키지 - `Packs` 만 있다) 바이너리 폴더
        AbsolutePath _content;     ///< 리소스 도메인 루트(`<프로젝트>/Content`) - 패키지에는 없는 폴더다(읽는 쪽이 있는지 본다)
        AbsolutePath _packs;       ///< 구운 팩 폴더(`<바이너리>/Packs`)
        AbsolutePath _saved;       ///< 실행 산출물 · 개발 캐시(`<바이너리>/Saved` - 로그 · 스크린샷 · 셰이더 캐시 · 자동화)
        AbsolutePath _derivedData; ///< 빌드 구성(Debug · Release)이 함께 쓰는 파생물(`<프로젝트>/Saved` - DDC · 쿠킹 기록 · 개발 인증서)
        /**
         * @brief 실행 뿌리 — 이 프로세스가 쓰고 버리는 상태(사용자 데이터 · 에디터 상태 · 시나리오 사본 `scratch` 도메인 · 죽이기 전 덤프)가 모두 이 아래다.
         * @details 부모가 `kRunRootVariable` 로 주면 그 폴더(`Saved/Automation/Runs/<실행 id>/<백엔드>`), 혼자 뜬 실행은 `<_saved>/Automation/Runs/<pid>-<시작 ns>` 를 스스로 정한다.
         *          폴더는 쓰는 쪽이 처음 쓸 때 만든다. 지우는 것은 주인(`SavedCleanup` 의 `Automation` 규칙)뿐이다 — 자식은 제 뿌리 밖을 지우지 않는다.
         *          예전에는 상태마다 pid 꼬리(`run_p<pid>` · `editor_p<pid>` · `scratch/<이름>_p<pid>`)로 이름을 짓고 자식마다 죽은 형제의 폴더를 거뒀다. Gauntlet 이 역할마다 주는 산출물 폴더와 같은 자리다.
         */
        AbsolutePath _run;
        /**
         * @brief 실행 뿌리를 부모가 줬다(`kRunRootVariable`) — 이 프로세스는 더 큰 실행(백엔드별 체인 · 두 번 기동 짝)의 일부다.
         * @details 사람에게 남기는 산출물(버그 리포트)도 이때는 실행 뿌리 아래에 둔다 — 동시에 도는 형제가 한 폴더의 "가장 최근" 을 다투지 않고, 사람의 `Saved` 를 테스트가 채우지 않는다.
         */
        uint8 _bRunFromParent : 1;

        /** @brief 루트들입니다. 처음 부를 때 정하고 그 뒤로 바뀌지 않는다. 정하지 못하면 멈춥니다. */
        [[nodiscard]] static const ProcessRoots& get();
    };
} // namespace sw
