/**
 * @file MultiplayerTab.h
 * @brief Multiplayer 창에 탭 하나를 더하는 등록 줄(`SW_EDITOR_MULTIPLAYER_TAB`)과 탭이 받는 문맥입니다. ImGui 에 의존하지 않습니다.
 * @details 창은 탭을 모르고, 등록 순서대로 탭 막대를 그리고 고른 탭의 `drawTab` 만 부른다. Session · Conditions 가 첫 사용처이고
 *          서버 디버깅의 다음 단위(Inspector · Sync)와 확장 모듈(게임별 상태 보기)도 자기 .cpp 에 같은 한 줄을 둔다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

namespace sw::editor
{
    class EditorNetPlaySession;

    /**
     * @struct MultiplayerTabContext
     * @brief 탭이 그릴 때 받는 문맥입니다. 창 위쪽의 대상 고르기가 모든 탭에 같은 대상을 줍니다.
     */
    struct MultiplayerTabContext
    {
        EditorNetPlaySession* _pSession{ nullptr };      ///< 에디터의 멀티 프로세스 재생 세션(언제나 있다 — 돌고 있는지는 `isRunning`)
        int32                 _targetProcessIndex{ -1 }; ///< 고른 대상 프로세스(0 = 서버, N = 클라이언트 N). 세션이 돌지 않으면 -1
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class IMultiplayerTab
     * @brief 탭 하나입니다. 창이 등록마다 하나를 만들어 보관하고 있으므로 편집 필드 같은 상태를 멤버로 둡니다.
     * @details 확장 모듈이 다시 로드되면 창이 등록 세대를 보고 인스턴스를 새로 만든다(상태는 사라진다).
     */
    class IMultiplayerTab
    {
    public:
        virtual ~IMultiplayerTab() = default;
        /** @brief 탭 본문을 그립니다. 탭이 보일 때만 불립니다. */
        virtual void drawTab( const MultiplayerTabContext& context ) = 0;
        /**
         * @brief 창이 열려 있지만 이 탭이 고른 탭이 아닐 때 프레임마다 불립니다(그리지 않는다). 기본은 아무것도 하지 않습니다.
         * @details 탭 기록을 숨김 중에도 잇고 싶은 탭(Profiler 의 "Record while hidden")이 쓴다. 창을 닫으면 불리지 않는다.
         */
        virtual void updateWhileHidden( const MultiplayerTabContext& context ) { (void)context; }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorMultiplayerTabRegistration
     * @brief Multiplayer 창의 탭 한 종류입니다. `_order` 가 탭 순서이고 `_pLabel` 이 탭 제목입니다(`_pID` 는 저장 · 자동화가 부르는 이름).
     */
    struct EditorMultiplayerTabRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "multiplayerTab";

        const utf8* _pLabel;
        unique_ptr<IMultiplayerTab> ( *_pCreate )();
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 등록 항목이 가리키는 탭 생성 함수입니다. */
    template <typename TTab>
    unique_ptr<IMultiplayerTab> createMultiplayerTab()
    {
        return make_unique<TTab>();
    }
} // namespace sw::editor

/** @code SW_EDITOR_MULTIPLAYER_TAB( MultiplayerConditionsTab, "conditions", 200, "Conditions" ); @endcode */
#define SW_EDITOR_MULTIPLAYER_TAB( TTab, pID, order, pLabel )                                                          \
    SW_EDITOR_REGISTER( ::sw::editor::EditorMultiplayerTabRegistration, MultiplayerTab_##TTab, { pID, order }, pLabel, \
                        &::sw::editor::createMultiplayerTab<TTab> )
