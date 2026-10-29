/**
 * @file PackagingModel.h
 * @brief 패키징 창의 모델 — 게임 설정 목록(활성 팩의 게임 고르기), 타깃 · 게임 · 백엔드 · 산출 폴더 · 건너뛰기 설정, 진입점 명령 만들기, 백그라운드 실행 잡과 결과 받기 · 진행,
 *        최근 로그 줄. ImGui 를 모릅니다.
 * @details 패널(`PackagingPanel`)은 콤보 · 체크칸 · 진행 막대 · 로그를 그리기만 한다 — 버그 리포트 · 데이터 테이블 `Model/` 과 같은 선이다
 *          (언리얼 Project Launcher 의 프로필 · 작업 상태와 Slate 창이 나뉜 것과 같다). 진행 줄 읽기는 `PackagingProgressParser` 다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/Process.h"

#include "Editor/Panels/Packaging/Model/PackagingProgressParser.h"

namespace sw::editor
{
    class EditorExternalToolJob;

    /**
     * @class PackagingModel
     * @brief 패키징 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class PackagingModel
    {
    public:
        /** @brief 로그가 보이는 최대 줄 수입니다. */
        static constexpr size_t kVisibleLineCount = 400;

        PackagingModel();
        /** @brief 도는 패키징이 있으면 끝내고 기다립니다. */
        ~PackagingModel();

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

        // ------------------------------------------------------------------------------
        // 설정 — 콤보 · 체크칸이 바로 고친다
        // ------------------------------------------------------------------------------
        /** @brief 진입점이 받는 타깃 이름 수와 이름입니다(`--target`). */
        static uint32      getTargetCount();
        static const utf8* getTargetName( uint32 index );
        /** @brief 쿠킹 테이블의 백엔드 이름 수와 이름입니다(`--rhi`). 0 번은 "프리셋 기본값" 이다. */
        static uint32      getBackendCount();
        static const utf8* getBackendName( uint32 index );
        /** @brief 오류처럼 보이는 로그 줄이면 true 입니다(빨강). */
        static bool isErrorLine( string_view line );

        const vector<string>& getGames() const { return _listGame; }
        uint32&               editGameIndex() { return _gameIndex; }
        uint32                getGameIndex() const { return _gameIndex; }
        uint32&               editTargetIndex() { return _targetIndex; }
        uint32                getTargetIndex() const { return _targetIndex; }
        uint32&               editRHIIndex() { return _rhiIndex; }
        uint32                getRHIIndex() const { return _rhiIndex; }
        /** @brief 비면 진입점의 기본 산출 폴더입니다(`Pack.py --output` 의 기본값이 원본이다). 바뀐 값은 다음 Package 가 쓴다. */
        string& editOutputFolder() { return _outputFolder; }
        bool&   editSkipBuild() { return _bSkipBuild; }
        bool&   editSkipCook() { return _bSkipCook; }

        // ------------------------------------------------------------------------------
        // 진행
        // ------------------------------------------------------------------------------
        const PackagingProgress& getProgress() const { return _progress; }
        PackagingState           getState() const { return _progress._state; }
        const vector<string>&    getLines() const { return _listLine; }

    private:
        /** @brief `Config/Game/` 의 게임 설정 파일 이름을 모으고 활성 팩의 게임을 고릅니다. */
        void refreshGames();
        /** @brief 끝난 실행의 출력을 받아 진행을 정합니다. */
        void collectResult();

    private:
        unique_ptr<EditorExternalToolJob> _pJob;
        vector<string>                    _listGame;
        vector<string>                    _listLine;
        string                            _outputFolder;
        PackagingProgress                 _progress;
        uint32                            _gameIndex;
        uint32                            _targetIndex;
        uint32                            _rhiIndex;
        bool                              _bSkipBuild;
        bool                              _bSkipCook;
        bool                              _bGamesLoaded;
    };
} // namespace sw::editor
