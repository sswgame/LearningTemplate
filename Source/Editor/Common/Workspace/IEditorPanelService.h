/**
 * @file IEditorPanelService.h
 * @brief 아래 층(커맨드 · 작업 공간 · 뷰포트)이 패널과 도킹을 부르는 진입점입니다. 구현은 GUI 층(`EditorPanelService`)에 있습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorPanelID.h"

namespace sw::editor
{
    /**
     * @brief 에셋 파일 하나를 연 문서입니다 — 리소스 id 와 미저장 여부(`IEditorPanelService::collectOpenDocuments`, 활성 씬은 부른 쪽이 더한다).
     * @details 옮기기 · 삭제 계획이 본다(감사 P0-4): 미저장 문서가 옮기는 에셋을 적었으면 디스크에서 고친 참조를 다음 저장이 옛 id 로 덮고, 연 문서를 지우면 다음 저장이 파일을 되살린다.
     */
    struct EditorOpenDocument
    {
        string _assetID;
        bool   _bDirty{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /**
     * @class IEditorPanelService
     * @brief 패널 열기 · 최대화와 도구 문서 저장을 패널 매니저 · 도킹을 모르고 부르는 인터페이스입니다(`EditorContext::getPanelService`).
     * @details 커맨드는 ImGui 없는 동작이라 패널 매니저(GUI)를 include 하지 않습니다. 언리얼에서 에셋 편집기를 여는 쪽이
     *          `UAssetEditorSubsystem` 진입점만 보고 탭 매니저(Slate)를 모르는 것과 같습니다.
     */
    class IEditorPanelService : public EditorSubsystem
    {
    public:
        ~IEditorPanelService() override = default;

        /** @brief 패널을 열고 그 창(탭)을 다음 프레임에 앞으로 둡니다. 그 id 의 패널이 없으면 false 입니다. id 는 형식(`EditorPanelID`)이라 글자를 넘길 수 없습니다. */
        virtual bool focusPanel( EditorPanelID panelID ) = 0;
        /** @brief 지금 배치를 임시 레이아웃으로 저장하고 @p keepPanelID 밖의 패널을 닫은 뒤 그 패널을 앞으로 둡니다. 저장하지 못했으면 false 입니다. */
        [[nodiscard]] virtual bool maximizePanel( EditorPanelID keepPanelID ) = 0;
        /** @brief 최대화 전 배치(임시 레이아웃)를 다음 프레임에 되읽습니다. 그 파일이 없으면 false 입니다. */
        [[nodiscard]] virtual bool restoreMaximizedLayout() = 0;

        /** @brief 포커스된 도구 문서가 dirty 이면 저장하고 true 입니다. */
        [[nodiscard]] virtual bool saveFocusedDirtyDocument() = 0;
        /** @brief 모든 dirty 도구 문서를 저장합니다. 하나라도 실패하면 false 입니다. */
        [[nodiscard]] virtual bool saveAllDirtyDocuments() = 0;
        /** @brief 모든 dirty 도구 문서를 저장하지 않고 버립니다. */
        virtual void discardAllDirtyDocuments() = 0;
        /** @brief 열린 패널 · 닫힌 패널을 모두 포함한 dirty 문서 개수입니다. */
        virtual uint32 countDirtyDocuments() const = 0;
        /** @brief 에셋 파일을 연 문서 패널마다 한 줄을 @p outList 끝에 더합니다(연 경로가 없는 상태 문서 · 아직 연 문서가 없는 패널은 빠진다). */
        virtual void collectOpenDocuments( vector<EditorOpenDocument>& outList ) const = 0;
    };
} // namespace sw::editor
