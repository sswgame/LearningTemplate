/**
 * @file PackagingPanel.h
 * @brief 패키징 창입니다. 타깃 · 게임 · 백엔드 · 산출 폴더를 고르고 `Scripts/dev/MakePackage.py` 를 새 프로세스로 띄웁니다(언리얼 Package Project).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/PackagingProgressParser.h"

namespace sw::editor
{
    class EditorExternalToolJob;

    /** @brief 배포 패키지를 만드는 도구 창입니다. 진행은 진입점의 `[package]` 줄로 읽습니다. */
    class PackagingPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. */
        static constexpr const utf8* kPanelID = "packaging";

        /** @brief 패키징 창을 만듭니다(닫힌 채 시작한다). */
        PackagingPanel();
        /** @brief 도는 패키징이 있으면 끝내고 기다립니다. */
        ~PackagingPanel() override;

        /** @brief 창 제목입니다. */
        const utf8* getPanelTitle() const override { return "Packaging"; }
        /** @brief 설정 · 진행 · 로그를 그립니다. */
        void drawContent() override;
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */
        bool isToolPanel() const override { return true; }
        /** @brief 처음 열 때의 크기입니다(96 DPI 기준). */
        float2 getInitialPanelSize() const override { return float2{ 720.0f, 520.0f }; }
        /** @brief 도는 패키징을 끝냅니다(모듈이 내려가기 전). */
        void shutdown( IRHIDevice* pDevice ) override;

        /** @brief 지금 패키징 상태입니다(탐침 `Editor.PackagingState`). */
        PackagingState getState() const { return _progress._state; }
        /** @brief 지금 설정으로 진입점 명령 한 줄을 만듭니다(작업 폴더는 저장소 루트). */
        string makeCommand() const;
        /** @brief 패키징을 띄웁니다. 이미 돌면 false 입니다. */
        bool startPackaging();

    private:
        /** @brief 끝난 실행의 출력을 받아 진행을 정합니다. */
        void collectResult();
        /** @brief `Config/Game/` 의 게임 설정 파일 이름을 모으고 활성 팩의 게임을 고릅니다. */
        void refreshGames();

    private:
        unique_ptr<EditorExternalToolJob> _pJob;
        vector<string>                    _listGame;
        vector<string>                    _listLine;
        string                            _outputFolder;
        PackagingProgress                 _progress;
        uint32                            _gameIndex;
        uint32                            _targetIndex;
        uint32                            _rhiIndex;
        uint8                             _bSkipBuild    : 1;
        uint8                             _bSkipCook     : 1;
        uint8                             _bGamesLoaded  : 1;
        [[maybe_unused]] uint8            _reservedFlags : 5;
    };
} // namespace sw::editor
