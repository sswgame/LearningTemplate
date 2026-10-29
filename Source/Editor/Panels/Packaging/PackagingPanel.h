/**
 * @file PackagingPanel.h
 * @brief 패키징 창입니다. 타깃 · 게임 · 백엔드 · 산출 폴더를 고르고 `Scripts/package/Pack.py` 를 새 프로세스로 띄웁니다(언리얼 Package Project).
 * @details 설정 · 게임 목록 · 실행 잡 · 진행 · 로그 줄은 `PackagingModel`(Model/, ImGui 없음)이 들고, 이 창은 그리기만 한다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Process/Process.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Packaging/Model/PackagingModel.h"

namespace sw::editor
{
    /** @brief 배포 패키지를 만드는 도구 창입니다. 진행은 진입점의 `[package]` 줄로 읽습니다. */
    class PackagingPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. */
        static constexpr const utf8* kPanelID = "packaging";

        /** @brief 패키징 창을 만듭니다(닫힌 채 시작한다). */
        PackagingPanel();

        /** @brief 창 제목입니다. */
        const utf8* getPanelTitle() const override { return "Packaging"; }
        /** @brief 설정 · 진행 · 로그를 그립니다. */
        void drawContent() override;
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */
        bool isToolPanel() const override { return true; }
        /** @brief 처음 열 때의 크기입니다(96 DPI 기준). */
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 96.0f, 27.0f ); }
        /** @brief 도는 패키징을 끝냅니다(모듈이 언로드되기 전). */
        void shutdown( IRHIDevice* pDevice ) override;

        /** @brief 지금 패키징 상태입니다(감시 값 `Editor.PackagingState`). */
        PackagingState getState() const { return _model.getState(); }
        /** @brief 지금 설정으로 진입점 명령을 만듭니다(작업 폴더는 저장소 루트). */
        ProcessCommand makeCommand() const { return _model.makeCommand(); }
        /** @brief 패키징을 띄웁니다. 이미 돌면 false 입니다. */
        bool startPackaging() { return _model.startPackaging(); }

    private:
        /** @brief 타깃 · 게임 · RHI 콤보, 출력 폴더, 건너뛰기 체크칸입니다. 패키징이 도는 동안은 막습니다. */
        void drawOptionRows( bool bBusy );
        /** @brief 패키징 시작과 출력 폴더 열기 단추입니다. */
        void drawActionRow( bool bBusy );
        /** @brief 진행 막대와 실패 · 성공 안내입니다. */
        void drawProgressBar();
        /** @brief 최근 로그 줄을 그립니다(오류처럼 보이는 줄은 빨강). */
        void drawLogList();

    private:
        PackagingModel _model; ///< 소멸자가 도는 패키징을 끝내고 기다린다
    };
} // namespace sw::editor
