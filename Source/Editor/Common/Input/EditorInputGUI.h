/**
 * @file EditorInputGUI.h
 * @brief 입력 맵에 없는 에디터 입력을 ImGui 로 옮기는 유일한 곳입니다 — 맥락 메뉴(`drawContextMenuFor`)와 끌어 놓기(`beginDragSource` · `acceptDrop`).
 * @details 키 · 단추 · 몸짓은 엔진 입력 맵(`EditorInputActions`)이 판정한다. 여기는 그 맵이 모르는 것(어느 항목 위에서 우클릭했나, 무엇을 끌고 무엇을 받나)만 맡는다.
 *          패널은 "이 대상에 이 메뉴 종류" · "이 위젯은 이 종류를 끌고 이 종류를 받는다" 만 넘기고, 언제 열고 선택을 언제 바꾸는지는 이 한 곳이 정한다(EditorUX 10-3 R1 · R2 · R3 · R6).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Commands/EditorCommandTarget.h"
#include "Editor/Common/Commands/EditorDragPayload.h"
#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @struct EditorContextMenuSubject
     * @brief 맥락 메뉴를 열 자리 하나입니다. 패널이 매 프레임 채워 `drawContextMenuFor` 에 넘깁니다.
     */
    struct EditorContextMenuSubject
    {
        const utf8*              _pPopupID{ nullptr };                   ///< 팝업 id(패널 안에서 유일). 같은 자리를 매 프레임 같은 id 로 부른다
        EditorCommandTargetKind  _kind{ EditorCommandTargetKind::None }; ///< 메뉴 종류 = 대상 종류(R5)
        uint64                   _hoveredID{ 0 };                        ///< 누른 항목 id. 0 이면 빈 곳 메뉴다(창 위, 항목 밖)
        vector<uint64>           _listSelectedID{};                      ///< 지금 선택(같은 종류의 id)
        Delegate<void( uint64 )> _selectOnly{};                          ///< 선택 밖 항목에서 열 때 그 항목만 고른다(R2). 빈 곳 메뉴는 부르지 않는다
        float3                   _worldPosition{};                       ///< 뷰포트: 누른 레이가 바닥 평면과 만난 점
        bool                     _bHasWorldPosition{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorInputGUI
     * @brief 맥락 메뉴 · 끌어 놓기 어댑터입니다. ImGui 프레임 안에서 부릅니다.
     */
    struct SW_EDITOR_API EditorInputGUI
    {
        /** @brief 끌어 놓기 페이로드의 ImGui 형식 이름입니다. 에디터의 끌어 놓기는 이 한 형식(`EditorDragPayload`)이다. */
        static constexpr const utf8* kPayloadTypeName = "SW_EDITOR_PAYLOAD";

        /**
         * @brief 직전 항목(@p subject 의 `_hoveredID` 가 0 이 아니면) 또는 지금 창의 빈 곳(0 이면) 위 우클릭에 맥락 메뉴를 열고, 열린 메뉴를 그립니다.
         * @details 매 프레임 같은 자리에서 부른다. 규칙:
         *          - R1: 오른쪽 단추를 끌기 문턱(에디터 입력 맵의 `dragThreshold`, 한 상수) 안에서 뗀 순간 연다. 끌었으면 열지 않는다.
         *          - R2: 여는 순간 누른 항목이 선택 밖이면 그것만 고른다(`_selectOnly`). 빈 곳은 선택을 바꾸지 않는다.
         *          - R3: 메뉴의 대상은 선택 전체다. 항목은 명령 등록부의 맥락 메뉴 칸에서 나오고 라벨 · 켜짐 · 단축키 글자는 명령이 정한다(R4).
         *          - R6: 켜진 항목이 하나도 없으면 열지 않는다(빈 팝업 금지).
         *          고른 명령은 팝업을 닫은 뒤 실행한다.
         * @return 이번 프레임에 메뉴가 열려 있었으면 true 입니다.
         */
        static bool drawContextMenuFor( const EditorContextMenuSubject& subject );
        /**
         * @brief 직전 항목을 끌기 원본으로 만듭니다. 끌기가 시작됐으면 페이로드를 싣고 @p pPreviewLabel 을 미리보기로 그리고 true 입니다.
         */
        static bool beginDragSource( const EditorDragPayload& payload, const utf8* pPreviewLabel );
        /**
         * @brief 직전 항목을 놓을 자리로 만듭니다. @p acceptedKinds 안의 페이로드가 놓였으면 @p outPayload 에 담고 true 입니다.
         * @details 집합 밖 종류는 놓을 자리로 보이지도 않는다(강조하지 않는다) — 받는 쪽이 이름 문자열을 비교하지 않는다.
         */
        [[nodiscard]] static bool acceptDrop( EditorPayloadKindSet acceptedKinds, EditorDragPayload& outPayload );
    };
} // namespace sw::editor
