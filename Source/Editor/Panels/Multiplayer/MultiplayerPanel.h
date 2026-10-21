/**
 * @file MultiplayerPanel.h
 * @brief Multiplayer 창 — 멀티 프로세스 재생을 보는 창 하나입니다. 위쪽의 대상 고르기와, 등록된 탭(`SW_EDITOR_MULTIPLAYER_TAB`)을 그립니다.
 * @details 언리얼 Network Insights · 유니티 Multiplayer Play Mode 창 + Network Simulator 에 해당합니다. 세션은 `EditorContext::getSubsystem<EditorNetPlaySession>()` 하나이고,
 *          툴바의 Net Mode 드롭다운과 이 창이 같은 세션을 봅니다. 탭은 이 폴더의 `Multiplayer<이름>Tab.cpp` 가 한 줄로 더하고(Session · Conditions),
 *          서버 디버깅의 다음 단위(`docs/plans/ServerDebug.md` 의 S3 · S5 · S7)와 확장 모듈도 같은 줄을 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

namespace sw::editor
{
    /**
     * @class MultiplayerPanel
     * @brief 대상 고르기와 탭 막대를 그립니다. 세션 갱신(`EditorNetPlaySession::update`)은 이 창이 아니라 툴바가 프레임마다 한다(창을 닫아도 로그가 모인다).
     */
    class MultiplayerPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "multiplayer";

        MultiplayerPanel();

        const utf8* getPanelTitle() const override { return "Multiplayer"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;
        float2      getInitialPanelSize() const override { return float2{ 760.0f, 560.0f }; }

    private:
        /** @brief 탭 인스턴스 하나와 그것을 만든 등록입니다. */
        struct TabEntry
        {
            const EditorMultiplayerTabRegistration* _pRegistration{ nullptr };
            unique_ptr<IMultiplayerTab>             _pTab{};
        };

        /** @brief 등록 세대가 바뀌었으면(확장 모듈 로드 · 언로드) 탭 인스턴스를 등록 순서대로 다시 만듭니다. */
        void syncTabs();
        /** @brief 창 위쪽의 대상 고르기(서버 · 클라이언트 N)를 그리고 고른 프로세스 번호를 반환합니다. 세션이 돌지 않으면 -1 입니다. */
        int32 drawTargetPicker( EditorNetPlaySession& session );

        vector<TabEntry> _listTab;
        uint32           _tabGeneration;      ///< `_listTab` 을 맞춘 등록 세대
        int32            _targetProcessIndex; ///< 고른 대상(0 = 서버)
    };
} // namespace sw::editor
