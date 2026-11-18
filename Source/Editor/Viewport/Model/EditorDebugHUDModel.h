/**
 * @file EditorDebugHUDModel.h
 * @brief 씬 뷰 HUD 상자의 Model 쪽입니다 — 디버그 HUD 섹션 쓰개 목록, 4 Hz 다시 부르기, 메인 선택을 디버그 대상으로 따르기. ImGui 를 모릅니다.
 * @details 그리기는 `EditorDebugHUDView`(View)가 이 값(섹션마다 적힌 줄 · 그래프)만 읽어 한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    struct DebugHUDSectionRegistration;

    class DebugHUDSectionWriter;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorDebugHUDModel
     * @brief 씬 뷰 HUD 의 섹션 줄입니다(언리얼 에디터 뷰포트의 `stat` 표시에 해당 — 뷰포트마다 켜고, 내용은 게임과 같은 통계 체계).
     * @details 게임 HUD(`DebugHUD`)는 런타임 UI 라 게임 뷰에만 그려진다. 씬 뷰는 같은 섹션 본문을 4 Hz 로 불러 적어 둔다.
     *          섹션 켜짐은 게임 HUD 와 같은 설정(`debug.hudSections`)이다. `kUsesFrameProfiler` 섹션은 프로파일러가 꺼져 있으면 건너뛰고 그 수를 센다.
     *          대상을 보는 섹션(npc · movement · ability · sequence · animrewind …)의 대상은 메인 씬 선택을 따른다(`followSelection`) — 언리얼 Gameplay Debugger 가
     *          뷰포트에서 고른 액터의 카테고리를 그리는 자리다. 선택이 바뀔 때만 대상을 바꾸므로 그 사이 `debug.target` 으로 정한 대상은 다음 선택까지 남는다.
     */
    class SW_EDITOR_API EditorDebugHUDModel
    {
    public:
        /** @brief 섹션 하나의 적힌 줄입니다. 등록 포인터로 짝을 찾는다(모듈이 언로드되면 레지스트리 버전 번호가 바뀌어 다시 만든다). */
        struct SectionSlot
        {
            const DebugHUDSectionRegistration* _pRegistration;
            unique_ptr<DebugHUDSectionWriter>  _pWriter;
        };

        EditorDebugHUDModel();
        ~EditorDebugHUDModel();

        /** @brief 한 프레임을 진행합니다 — 프레임 시간을 적고, 레지스트리가 바뀌었으면 목록을 다시 짓고, 4 Hz 마다 섹션 본문을 다시 부른다(메인 스레드). */
        void update( float32 deltaSeconds );

        const vector<SectionSlot>& getSlots() const { return _listSlot; }
        /** @brief 프로파일러가 꺼져 건너뛴 섹션 수입니다. */
        uint32 getSkippedProfilerSectionCount() const { return _skippedProfilerSectionCount; }

    private:
        void rebuildSlots();
        void refresh();
        /** @brief 메인 씬의 첫 선택이 바뀌었으면 디버그 대상(`DebugHUDRegistry::setTarget`)을 그 오브젝트로 바꿉니다. 선택이 비면 대상을 그대로 둔다. */
        void followSelection();

    private:
        vector<SectionSlot> _listSlot;
        uint64              _registryRevision;
        float32             _refreshTimer;
        uint32              _skippedProfilerSectionCount;
        uint64              _followedSelectionRevision; ///< 지난 `followSelection` 이 본 선택 차례(`EditorSelection::getRevision`) — 오브젝트 id 를 견주면 4 Hz 사이에 비웠다 같은 것을 다시 고른 것을 놓친다
    };
} // namespace sw::editor
