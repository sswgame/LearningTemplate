/**
 * @file EditorStateDocumentPanel.h
 * @brief 문서가 에셋 파일 하나가 아닌 문서 패널의 기반입니다 — 실시간 상태(전역 변수 · 메시 모디파이어 세션)나 설정 파일 묶음(프로젝트 설정)을 저장합니다.
 * @details 문서 패널의 기반은 둘이고 `IEditorDocumentPanel` 을 직접 상속할 수 없다(생성자가 두 기반에만 열려 있다). 에셋 파일 하나를 고치는 패널은
 *          `EditorDocumentPanel`(워크스페이스 포커스 → `acceptsDocumentPath` → `loadDocument`, 저장 왕복 시나리오가 사본을 연다)이고, 이 기반은 연 문서 경로가 없는 패널이다 —
 *          언리얼 Project Settings · Console Variables 창이 에셋 편집기(`FAssetEditorToolkit`)가 아닌 것과 같다. 예전에는 에셋 파일을 고치는 패널도 직접 상속해
 *          경로 칸 · 목록마다 자체 열기 길과 문서 경로를 따로 들었고, 시나리오가 사본을 가리킬 수 없었다.
 *          에셋 파일 하나를 고치는 새 패널이 이 기반을 고르면 안 된다 — 열기 길과 저장 왕복을 잃는다.
 */
#pragma once
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/Panel/IEditorDocumentPanel.h"

namespace sw::editor
{
    /**
     * @class EditorStateDocumentPanel
     * @brief 연 문서 경로가 없는 문서 패널입니다. 파일 머리말 참고. 필요할 때 여는 도구라 닫힌 채 시작합니다.
     */
    class SW_EDITOR_API EditorStateDocumentPanel : public IEditorDocumentPanel
    {
    protected:
        EditorStateDocumentPanel()
            : IEditorDocumentPanel{ false }
        {
        }
    };
} // namespace sw::editor
