/**
 * @file EditorDebugHUDView.h
 * @brief 씬 뷰 오른쪽 위에 디버그 HUD 섹션을 그립니다 — 게임 HUD 와 같은 섹션 레지스트리 · 같은 섹션 켜짐을 쓰고, 그리기만 ImGui 입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
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
     * @brief 씬 뷰의 HUD 상자입니다(언리얼 에디터 뷰포트의 `stat` 표시에 해당 — 뷰포트마다 켜고, 내용은 게임과 같은 통계 체계).
     * @details 게임 HUD(`DebugHUD`)는 런타임 UI 라 게임 뷰에만 그려진다. 씬 뷰는 에디터 카메라 화면이라 같은 섹션 본문을 4 Hz 로 불러 ImGui 로 그린다.
     *          섹션 켜짐은 게임 HUD 와 같은 설정(`debug.hudSections`)이고, 이 상자를 켜고 끄는 것만 뷰마다다(툴바 `HUD`).
     *          `kUsesFrameProfiler` 섹션은 프로파일러가 꺼져 있으면 건너뛰고 한 줄로 알린다(씬 뷰가 프로파일러를 켜지 않는다).
     *          대상을 보는 섹션(npc · movement · ability · sequence · animrewind …)의 대상은 메인 씬 선택을 따른다(`followSelection`) — 언리얼 Gameplay Debugger 가
     *          뷰포트에서 고른 액터의 카테고리를 그리는 자리다. 선택이 바뀔 때만 대상을 바꾸므로 그 사이 `debug.target` 으로 정한 대상은 다음 선택까지 남는다.
     */
    class EditorDebugHUDView
    {
    public:
        EditorDebugHUDView();
        ~EditorDebugHUDView();

        /** @brief 캔버스 오른쪽 위(@p topRightInset 만큼 안 — 붙은 오버레이 바 아래)에 상자를 그립니다. 상자 아래 끝(캔버스 위 기준 픽셀)을 돌려줍니다. */
        float32 draw( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float2& topRightInset, float32 deltaSeconds );
        /** @brief 지난 그리기에 담은 섹션 수입니다(감시 값 `Editor.SceneViewHUDSections`). */
        uint32 getDrawnSectionCount() const { return _drawnSectionCount; }

    private:
        /** @brief 섹션 하나의 적힌 줄입니다. 등록 포인터로 짝을 찾는다(모듈이 언로드되면 레지스트리 버전 번호가 바뀌어 다시 만든다). */
        struct SectionSlot
        {
            const DebugHUDSectionRegistration* _pRegistration;
            unique_ptr<DebugHUDSectionWriter>  _pWriter;
        };

        void rebuildSlots();
        void refresh();
        /** @brief 메인 씬의 첫 선택이 바뀌었으면 디버그 대상(`DebugHUDRegistry::setTarget`)을 그 오브젝트로 바꿉니다. 선택이 비면 대상을 그대로 둔다. */
        void followSelection();

    private:
        vector<SectionSlot> _listSlot;
        uint64              _registryRevision;
        float32             _refreshTimer;
        uint32              _drawnSectionCount;
        uint32              _skippedProfilerSectionCount;
        uint64              _followedObjectID; ///< 지난 `followSelection` 이 본 첫 선택(0 = 없음)
    };
} // namespace sw::editor
