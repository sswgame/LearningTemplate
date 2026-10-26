/**
 * @file EditorInputGUI.h
 * @brief 입력 맵에 없는 에디터 입력을 ImGui 로 옮기는 유일한 곳입니다 — 맥락 메뉴, 끌어 놓기, 항목 클릭 · 포인터 상태, 비맥락 팝업 열기.
 * @details 키 · 단축키는 엔진 입력 맵(`EditorInputActions`)이 판정한다. 여기는 그 맵이 모르는 것(어느 항목 위에서 눌렀나, 무엇을 끄나, 캔버스 도구의 포인터)만 맡는다.
 *          패널 티어(Panels · AssetActions · Viewport)는 ImGui 입력 함수를 직접 부르지 않고 이 파일만 쓴다(`EditorInputBan.h`, EditorUX 10-4).
 *          클릭 · 끌기 · 더블클릭의 문턱은 입력 맵의 값 하나다 — `EditorInputActions::processFrame` 이 ImGui 에 같은 값을 넣는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Commands/EditorCommandTarget.h"
#include "Editor/Common/Commands/EditorDragPayload.h"
#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /** @brief 포인터 단추입니다(ImGui 단추 번호와 같은 순서). */
    enum class EditorMouseButton : uint8
    {
        Left = 0,
        Right,
        Middle,
        Back,    ///< 뒤로 단추(X1)
        Forward, ///< 앞으로 단추(X2)
        Count
    };

    /**
     * @struct EditorPointerState
     * @brief 이번 프레임의 포인터 · 수정 키 상태입니다(캔버스 · 뷰포트 도구가 읽는다). 패널이 `ImGuiIO` 필드를 직접 읽지 않게 한 곳에서 채운다.
     * @details 클릭 · 끌기 구분(`_arrClickedWithoutDrag` · `_arrDragging`)은 입력 맵의 `dragThreshold` 하나로 판정한다(R1 · R7).
     */
    struct EditorPointerState
    {
        static constexpr size_t kButtonCount = static_cast<size_t>( EditorMouseButton::Count );

        float2  _position{};              ///< 화면 좌표(ImGui 주 뷰포트 기준)
        float2  _delta{};                 ///< 이번 프레임 이동
        float32 _wheel{ 0.0f };           ///< 세로 휠(한 칸 = 1)
        float32 _wheelHorizontal{ 0.0f }; ///< 가로 휠
        uint8   _modifierMask{ 0 };       ///< `ModifierKey` 비트(Ctrl · Shift · Alt · Super)
        bool    _arrDown[kButtonCount]{};
        bool    _arrPressed[kButtonCount]{};            ///< 이번 프레임에 눌렸다
        bool    _arrReleased[kButtonCount]{};           ///< 이번 프레임에 뗐다
        bool    _arrDoubleClicked[kButtonCount]{};      ///< 이번 프레임의 누름이 더블클릭이다
        bool    _arrDragging[kButtonCount]{};           ///< 누른 채 끌기 문턱을 넘었다
        bool    _arrClickedWithoutDrag[kButtonCount]{}; ///< 이번 프레임에 문턱 안에서 뗐다(클릭 — 메뉴 · 선택)
        float2  _arrDragDelta[kButtonCount]{};          ///< 누른 자리에서 지금까지의 끌기

        bool isDown( EditorMouseButton button ) const { return _arrDown[static_cast<size_t>( button )]; }
        bool wasPressed( EditorMouseButton button ) const { return _arrPressed[static_cast<size_t>( button )]; }
        bool wasReleased( EditorMouseButton button ) const { return _arrReleased[static_cast<size_t>( button )]; }
        bool wasDoubleClicked( EditorMouseButton button ) const { return _arrDoubleClicked[static_cast<size_t>( button )]; }
        bool isDragging( EditorMouseButton button ) const { return _arrDragging[static_cast<size_t>( button )]; }
        bool wasClickedWithoutDrag( EditorMouseButton button ) const { return _arrClickedWithoutDrag[static_cast<size_t>( button )]; }
        bool hasModifier( uint8 modifierBit ) const { return ( _modifierMask & modifierBit ) != 0; }
    };
} // namespace sw::editor

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
        /** @brief 자리 판정을 부른 쪽이 한다(뷰포트처럼 항목이 아닌 캔버스 — 비행과 메뉴를 그쪽이 가른다). true 면 `_bOpenRequested` 만 본다. */
        bool _bCallerDecidesOpen{ false };
        bool _bOpenRequested{ false }; ///< `_bCallerDecidesOpen` 일 때 이번 프레임에 연다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorInputGUI
     * @brief 맥락 메뉴 · 끌어 놓기 · 포인터 어댑터입니다. ImGui 프레임 안에서 부릅니다.
     */
    struct SW_EDITOR_API EditorInputGUI
    {
        /** @brief 끌어 놓기 페이로드의 ImGui 형식 이름입니다. 에디터의 끌어 놓기는 이 한 형식(`EditorDragPayload`)이다. */
        static constexpr const utf8* kPayloadTypeName = "SW_EDITOR_PAYLOAD";

        // ------------------------------------------------------------------------------
        // 1) 맥락 메뉴(R1 · R2 · R3 · R6)
        // ------------------------------------------------------------------------------
        /**
         * @brief 직전 항목(@p subject 의 `_hoveredID` 가 0 이 아니면) 또는 지금 창의 빈 곳(0 이면) 위 우클릭에 맥락 메뉴를 열고, 열린 메뉴를 그립니다.
         * @details 매 프레임 같은 자리에서 부른다. 규칙:
         *          - R1: 오른쪽 단추를 끌기 문턱(에디터 입력 맵의 `dragThreshold`, 한 상수) 안에서 뗀 순간 연다. 끌었으면 열지 않는다.
         *          - R2: 여는 순간 누른 항목이 선택 밖이면 그것만 고른다(`_selectOnly`). 빈 곳은 선택을 바꾸지 않는다.
         *          - R3: 메뉴의 대상은 선택 전체다. 항목은 명령 등록부의 맥락 메뉴 칸에서 나오고 라벨 · 켜짐 · 단축키 글자는 명령이 정한다(R4).
         *          - R6: 켜진 항목이 하나도 없으면 열지 않는다(빈 팝업 금지).
         *          고른 명령은 팝업을 닫은 뒤 실행한다. 하위 메뉴 명령(`EditorCommandDesc::_submenu`)은 그 함수가 안을 그린다.
         * @return 이번 프레임에 메뉴가 열려 있었으면 true 입니다.
         */
        static bool drawContextMenuFor( const EditorContextMenuSubject& subject );
        /**
         * @brief 직전 항목 위 우클릭(R1 — 끌기 문턱 안에서 뗀 순간)에 @p pPopupID 팝업을 열고, 열려 있으면 true 입니다. true 면 부른 쪽이 `ImGui::EndPopup` 을 부릅니다.
         * @details 항목이 그 행에만 뜻이 있는 메뉴(속성 행의 값 복사 · 로그 행 · 표 칸 — 대상이 그 프레임의 주소라 id 로 명령에 넘길 수 없는 것)가 쓴다.
         *          대상이 id 로 풀리는 메뉴(오브젝트 · 컴포넌트 · 에셋 · 뷰포트)는 명령 등록부를 쓰는 `drawContextMenuFor` 다.
         */
        static bool beginItemContextMenu( const utf8* pPopupID );
        /** @brief 창의 빈 곳(어느 항목 밖) 우클릭에 @p pPopupID 팝업을 엽니다. 나머지는 `beginItemContextMenu` 와 같습니다. */
        static bool beginWindowContextMenu( const utf8* pPopupID );

        // ------------------------------------------------------------------------------
        // 2) 끌어 놓기 — {종류, id} 한 형식. 이름(에셋 경로 · 위젯 타입)은 id 표로 바꾼다
        // ------------------------------------------------------------------------------
        /** @brief 직전 항목을 끌기 원본으로 만듭니다. 끌기가 시작됐으면 페이로드를 싣고 @p pPreviewLabel 을 미리보기로 그리고 true 입니다. */
        static bool beginDragSource( const EditorDragPayload& payload, const utf8* pPreviewLabel, bool bAllowNullItem = false );
        /**
         * @brief 직전 항목을 놓을 자리로 만듭니다. @p acceptedKinds 안의 페이로드가 놓였으면 @p outPayload 에 담고 true 입니다.
         * @details 집합 밖 종류는 놓을 자리로 보이지도 않는다(강조하지 않는다) — 받는 쪽이 이름 문자열을 비교하지 않는다.
         */
        [[nodiscard]] static bool acceptDrop( EditorPayloadKindSet acceptedKinds, EditorDragPayload& outPayload );
        /**
         * @brief 직전 항목 위에 받을 종류의 페이로드가 끌려 와 있으면(놓기 전 포함) @p outPayload 에 담고 true 입니다. 놓였으면 @p outbDelivered 가 true 입니다.
         * @details 놓을 위치 표시(앞 · 뒤 · 안)를 놓기 전에 그리는 곳이 쓴다. 기본 강조 사각형은 그리지 않는다.
         */
        [[nodiscard]] static bool previewDrop( EditorPayloadKindSet acceptedKinds, EditorDragPayload& outPayload, bool& outbDelivered );
        /** @brief 화면 사각형(@p minPos ~ @p maxPos)을 놓을 자리로 만듭니다(뷰포트 캔버스처럼 위에 다른 것이 그려지는 영역). 그 밖은 `acceptDrop` 과 같습니다. */
        [[nodiscard]] static bool acceptDropInRect( const float2& minPos, const float2& maxPos, const utf8* pTargetID, EditorPayloadKindSet acceptedKinds,
                                                    EditorDragPayload& outPayload );
        /** @brief 이름(에셋 상대 경로 · 위젯 타입 이름)을 페이로드 id 로 바꿉니다. 같은 이름은 같은 id 이고 `findPayloadName` 으로 되돌린다. */
        static uint64 registerPayloadName( EditorPayloadKind kind, string_view name );
        /** @brief `registerPayloadName` 이 만든 id 의 이름입니다. 모르는 id 면 false 입니다. */
        [[nodiscard]] static bool findPayloadName( EditorPayloadKind kind, uint64 id, string& outName );

        // ------------------------------------------------------------------------------
        // 3) 항목 클릭 · 포인터 · 수정 키 — 캔버스 · 목록 위젯용. 문턱은 입력 맵의 값이다
        // ------------------------------------------------------------------------------
        /** @brief 직전 항목 위에서 @p button 을 이번 프레임에 눌렀으면 true 입니다(선택 · 펼치기). */
        static bool wasItemClicked( EditorMouseButton button = EditorMouseButton::Left );
        /** @brief 직전 항목 위에서 @p button 을 이번 프레임에 더블클릭했으면 true 입니다(열기). 간격 · 거리는 입력 맵의 `doubleClickTime` · `doubleClickMaxDistance` 입니다. */
        static bool wasItemDoubleClicked( EditorMouseButton button = EditorMouseButton::Left );
        /** @brief 이번 프레임의 포인터 · 수정 키 상태입니다. */
        static EditorPointerState readPointer();
        /** @brief 지금 누른 수정 키(`ModifierKey` 비트)입니다. */
        static uint8 getModifierMask();
        /**
         * @brief 이번 프레임에 Esc 로 취소했으면 true 입니다(글 칸의 편집 취소 · 확인 창 닫기).
         * @details 단축키가 아니라 글 칸 · 모달의 편집 키라 입력 맵에 두지 않는다 — 글을 치는 동안 액션은 멈추는데 이 키는 그때 들어야 한다.
         */
        static bool wasCancelPressed();

        // ------------------------------------------------------------------------------
        // 4) 비맥락 팝업(확인 창 · 도구 팝업) 열기
        // ------------------------------------------------------------------------------
        /** @brief 이름 붙인 팝업(모달 확인 창 · 툴바 팝업)을 엽니다. 맥락 메뉴는 `drawContextMenuFor` 를 씁니다. */
        static void openPopup( const utf8* pPopupID );

        // ------------------------------------------------------------------------------
        // 5) 패널 액션(명령이 아닌 키 — 행 이동 · 비행 · 이름 바꾸기 시작). 범위는 액션의 레이어
        // ------------------------------------------------------------------------------
        /** @brief 에디터 입력 맵의 액션이 이번 프레임에 발화했으면 true 입니다(`EditorInputActions::wasActionTriggered`). */
        static bool wasActionTriggered( string_view action );
        /** @brief 에디터 입력 맵의 액션이 눌려 있으면 true 입니다(`EditorInputActions::isActionDown`). */
        static bool isActionDown( string_view action );
        /**
         * @brief 몸짓 동안만 켜는 모드 레이어(`Editor.Viewport.Fly` — 오른쪽 단추를 누른 동안의 WASD)를 켜고 끕니다(`EditorInputActions::setModeLayerEnabled`).
         * @details 패널 레이어는 포커스가 정하지만 모드 레이어는 몸짓의 주인(뷰포트)이 정한다. 같은 키(W)를 패널 레이어(기즈모)와 나눠 쓰려면 레이어가 달라야 한다.
         */
        static void setModeLayerEnabled( string_view layer, bool bEnabled );
    };
} // namespace sw::editor
