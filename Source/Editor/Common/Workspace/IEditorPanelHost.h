/**
 * @file IEditorPanelHost.h
 * @brief 아래 층(커맨드 · 작업 공간 · 뷰포트)이 패널과 도킹을 부르는 창구입니다. 구현은 GUI 층(`EditorPanelHost`)에 있습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    /**
     * @class IEditorPanelHost
     * @brief 패널 열기 · 최대화와 도구 문서 저장을 패널 매니저 · 도킹을 모르고 부르는 인터페이스입니다(`EditorContext::getPanelHost`).
     * @details 커맨드는 ImGui 없는 동작이라 패널 매니저(GUI)를 include 하지 않습니다. 언리얼에서 에셋 편집기를 여는 쪽이
     *          `UAssetEditorSubsystem` 창구만 보고 탭 매니저(Slate)를 모르는 것과 같습니다.
     */
    class IEditorPanelHost
    {
    public:
        virtual ~IEditorPanelHost() = default;

        /** @brief 패널을 열고 그 창(탭)을 다음 프레임에 앞으로 둡니다. 그 id 의 패널이 없으면 false 입니다. */
        virtual bool focusPanel( string_view panelID ) = 0;
        /** @brief 지금 배치를 임시 레이아웃으로 저장하고 @p keepPanelID 밖의 패널을 닫은 뒤 그 패널을 앞으로 둡니다. 저장하지 못했으면 false 입니다. */
        [[nodiscard]] virtual bool maximizePanel( string_view keepPanelID ) = 0;
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
    };
} // namespace sw::editor
