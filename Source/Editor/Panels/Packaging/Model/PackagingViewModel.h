/**
 * @file PackagingViewModel.h
 * @brief 패키징 창의 모델 — 게임 설정 목록(활성 팩의 게임 고르기), 타깃 · 게임 · 백엔드 · 산출 폴더 · 건너뛰기 설정, 진입점 명령 만들기, 백그라운드 실행 잡과 결과 받기 · 진행,
 *        최근 로그 줄. ImGui 를 모릅니다.
 * @details 패널(`PackagingPanel`)은 콤보 · 체크칸 · 진행 막대 · 로그를 그리기만 한다 — 버그 리포트 · 데이터 테이블 `Model/` 과 같은 선이다
 *          (언리얼 Project Launcher 의 프로필 · 작업 상태와 Slate 창이 나뉜 것과 같다). 진행 줄 읽기는 `PackagingProgress` 다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/Process.h"

// 패키징 진입점(`Scripts/package/pack.py`)의 출력 줄을 읽어 진행과 결과로 바꿉니다(ImGui 없음).
namespace sw::editor
{
    /** @brief 패키징 한 번의 상태입니다. 감시 값 `Editor.PackagingState` 의 값이기도 하다. */
    enum class PackagingState : uint8
    {
        Idle = 0,
        Running,
        Succeeded,
        Failed
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 출력에서 읽은 진행입니다. */
    struct PackagingProgress
    {
        string         _stepName;     ///< 마지막으로 시작한 단계(`build` · `cook` · `stage` · `verify`)
        string         _outputFolder; ///< 끝 줄의 패키지 폴더
        string         _failure;      ///< 실패 줄의 이유
        uint64         _byteCount{ 0 };
        uint32         _stepIndex{ 0 }; ///< 시작한 단계(1 부터, 아직 없으면 0)
        uint32         _stepCount{ 0 };
        PackagingState _state{ PackagingState::Idle };

        /** @brief 진행 막대 값(0..1)입니다. 끝났으면 1 입니다. */
        float32 computeFraction() const;

        // `[package] step k/n <단계>` · `[package] done <폴더> <바이트>` · `[package] FAILED <단계> <이유>` 줄을 읽습니다. 다른 줄은 지나칩니다.
        /** @brief 줄 하나를 @p inoutProgress 에 반영합니다. 진입점의 줄이면 true 입니다. */
        [[nodiscard]] static bool parseLine( string_view line, PackagingProgress& inoutProgress );
        /**
         * @brief 끝난 실행의 출력 전부를 읽습니다.
         * @details 끝 줄 없이 끝났으면(파이썬을 못 띄움 · 예외) 종료 코드로 실패를 정하고 이유에 마지막 줄을 둔다. @p exitCode 가 없으면(띄우지 못함 · 기다리지 못함) 0 이 아닌 것과 같다.
         */
        static PackagingProgress parseOutput( const vector<string>& listLine, const optional<int32>& exitCode );
    };
} // namespace sw::editor

namespace sw::editor
{
    class EditorExternalToolJob;

    /**
     * @class PackagingViewModel
     * @brief 패키징 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class PackagingViewModel
    {
    public:
        /** @brief 로그가 보이는 최대 줄 수입니다. */
        static constexpr size_t kVisibleLineCount = 400;

        PackagingViewModel();
        /** @brief 도는 패키징이 있으면 끝내고 기다립니다. */
        ~PackagingViewModel();

        /** @brief 도는 패키징을 끝내고 기다립니다(모듈이 언로드되기 전). */
        void cancelAndWait();
        /** @brief 게임 목록을 아직 모으지 않았으면 모으고, 끝난 실행의 출력을 받아 진행을 정합니다. 그리기마다 부릅니다. */
        void update();
        /** @brief 패키징이 도는 중이면 true 입니다(설정 · 시작 단추를 막는다). */
        [[nodiscard]] bool isBusy() const;
        /** @brief 지금 설정으로 진입점 명령을 만듭니다(작업 폴더는 저장소 루트). */
        ProcessCommand makeCommand() const;
        /** @brief 패키징을 띄웁니다. 이미 돌면 false 입니다. */
        bool startPackaging();
        /** @brief 끝난 패키징의 출력 폴더를 탐색기로 엽니다. 못 띄우면 경고가 남고 false 입니다. 디스크를 바꾸지 않아 되돌리기에 남지 않는다. */
        bool openOutputFolder() const;

        // ------------------------------------------------------------------------------
        // 설정 — 콤보 · 체크칸이 바로 고친다
        // ------------------------------------------------------------------------------
        /** @brief 진입점이 받는 플랫폼 이름 수와 이름입니다(`--platform`). 0 번은 이 호스트(`Desktop`)다. */
        static uint32      getPlatformCount();
        static const utf8* getPlatformName( uint32 index );
        /** @brief 고른 플랫폼이 타깃 · 백엔드를 고르게 하면 true 입니다. 웹 · Android 는 클라이언트뿐이고 백엔드가 프리셋에 묶여 있다 — 명령에 넣지 않는다. */
        bool isDesktopPlatform() const { return _platformIndex == 0; }
        /** @brief 진입점이 받는 타깃 이름 수와 이름입니다(`--target`). */
        static uint32      getTargetCount();
        static const utf8* getTargetName( uint32 index );
        /** @brief 쿠킹 테이블의 백엔드 이름 수와 이름입니다(`--rhi`). 0 번은 "프리셋 기본값" 이다. */
        static uint32      getBackendCount();
        static const utf8* getBackendName( uint32 index );
        /** @brief 오류처럼 보이는 로그 줄이면 true 입니다(빨강). */
        static bool isErrorLine( string_view line );
        /** @brief 결과 폴더 @p folder 를 파일 탐색기로 엽니다(`EditorAssetFileCommands::showInFileExplorer` — 못 띄우면 경고가 남는다). View 가 커맨드 헤더를 보지 않게 여기를 지난다. */
        static void showInFileExplorer( const string& folder );

        const vector<string>& getGames() const { return _listGame; }
        uint32&               editGameIndex() { return _gameIndex; }
        uint32                getGameIndex() const { return _gameIndex; }
        uint32&               editPlatformIndex() { return _platformIndex; }
        uint32                getPlatformIndex() const { return _platformIndex; }
        uint32&               editTargetIndex() { return _targetIndex; }
        uint32                getTargetIndex() const { return _targetIndex; }
        uint32&               editRHIIndex() { return _rhiIndex; }
        uint32                getRHIIndex() const { return _rhiIndex; }
        /** @brief 비면 진입점의 기본 산출 폴더입니다(`pack.py --output` 의 기본값이 원본이다). 바뀐 값은 다음 Package 가 쓴다. */
        string& editOutputFolder() { return _outputFolder; }
        bool&   editSkipBuild() { return _bSkipBuild; }
        bool&   editSkipCook() { return _bSkipCook; }

        // ------------------------------------------------------------------------------
        // 진행
        // ------------------------------------------------------------------------------
        const PackagingProgress& getProgress() const { return _progress; }
        PackagingState           getState() const { return _progress._state; }
        const vector<string>&    getLines() const { return _listLine; }
        /** @brief 이번 실행에서 패키징이 끝나기 전에(잡이 결과를 내기 전에) 받은 줄 수입니다 — 출력이 도는 동안 오는지 상태 시점과 무관하게 본다(감사 E-15, 감시 값 `Editor.PackagingLiveLineCount`). */
        uint32 getLiveLineCount() const { return _liveLineCount; }

    private:
        /** @brief `Config/Game/` 의 게임 설정 파일 이름을 모으고 활성 팩의 게임을 고릅니다. */
        void refreshGames();
        /** @brief 끝난 실행의 출력을 받아 진행을 정합니다. */
        void collectResult();

    private:
        unique_ptr<EditorExternalToolJob> _pJob;
        vector<string>                    _listGame;
        vector<string>                    _listLine;
        uint32                            _liveLineCount{ 0 }; ///< 끝나기 전에 받은 줄 수(`getLiveLineCount`) — 시작할 때 비운다
        string                            _outputFolder;
        PackagingProgress                 _progress;
        uint32                            _gameIndex;
        uint32                            _platformIndex;
        uint32                            _targetIndex;
        uint32                            _rhiIndex;
        bool                              _bSkipBuild; ///< NOLINT(sw-bool-flag): 참조(`bool&`)로 넘긴다
        bool                              _bSkipCook;  ///< NOLINT(sw-bool-flag): 참조(`bool&`)로 넘긴다
        uint8                             _bGamesLoaded : 1;
    };
} // namespace sw::editor
