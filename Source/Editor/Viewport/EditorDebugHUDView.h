/**
 * @file EditorDebugHUDView.h
 * @brief 씬 뷰 오른쪽 위에 디버그 HUD 섹션을 그립니다 — 게임 HUD 와 같은 섹션 레지스트리 · 같은 섹션 켜짐을 쓰고, 그리기만 ImGui 입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

struct ImDrawList;

namespace sw
{
    struct DebugHUDSectionRegistration;

    class DebugHUDSectionWriter;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorDebugHUDView
     * @brief 씬 뷰의 HUD 상자입니다(언리얼 에디터 뷰포트의 `stat` 표시 자리 — 뷰포트마다 켜고, 내용은 게임과 같은 통계 체계).
     * @details 게임 HUD(`DebugHUD`)는 런타임 UI 라 게임 뷰에만 그려진다. 씬 뷰는 에디터 카메라 화면이라 같은 섹션 본문을 4 Hz 로 불러 ImGui 로 그린다.
     *          섹션 켜짐은 게임 HUD 와 같은 설정(`debug.hudSections`)이고, 이 상자를 켜고 끄는 것만 뷰마다다(툴바 `HUD`).
     *          `kUsesFrameProfiler` 섹션은 프로파일러가 꺼져 있으면 건너뛰고 한 줄로 알린다(씬 뷰가 프로파일러를 켜지 않는다).
     */
    class EditorDebugHUDView
    {
    public:
        EditorDebugHUDView();
        ~EditorDebugHUDView();

        /** @brief 캔버스 오른쪽 위(@p topRightInset 만큼 안 — 붙은 오버레이 바 아래)에 상자를 그립니다. 상자 아래 끝(캔버스 위 기준 픽셀)을 돌려줍니다. */
        float32 draw( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float2& topRightInset, float32 deltaSeconds );
        /** @brief 지난 그리기에 담은 섹션 수입니다(탐침 `Editor.SceneViewHUDSections`). */
        uint32 getDrawnSectionCount() const { return _drawnSectionCount; }

    private:
        /** @brief 섹션 하나의 적힌 줄입니다. 등록 포인터로 짝을 찾는다(모듈이 내려가면 레지스트리 판 번호가 바뀌어 다시 짓는다). */
        struct SectionSlot
        {
            const DebugHUDSectionRegistration* _pRegistration;
            unique_ptr<DebugHUDSectionWriter>  _pWriter;
        };

        void rebuildSlots();
        void refresh();

    private:
        vector<SectionSlot> _listSlot;
        uint64              _registryRevision;
        float32             _refreshTimer;
        uint32              _drawnSectionCount;
        uint32              _skippedProfilerSectionCount;
    };
} // namespace sw::editor
