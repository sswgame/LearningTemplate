// 뷰포트 입력 오버레이 설정 서브시스템의 등록입니다(ImGui 없음). 그리기는 `ViewportInputOverlay.cpp`(MVVM View)다 — 등록 줄이 작업 공간 헤더를 보므로 View 밖에 둔다.
#include "pch.h"

#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Viewport/ViewportInputOverlay.h"

namespace sw::editor
{
    SW_EDITOR_SUBSYSTEM( ViewportInputOverlaySettings, 20 );
} // namespace sw::editor
