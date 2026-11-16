/**
 * @file EditorWidgets.h
 * @brief 에디터 창들이 함께 쓰는 ImGui 위젯입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorColor.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Widgets/EditorWidgetID.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"

#include <cstddef>

struct ImVec2;
struct ImGuiInputTextCallbackData;

namespace sw
{
    struct float3;
} // namespace sw

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // 1) 공유 위젯 클래스(색 타입 Color 와 style 은 EditorColor.h). 에셋 끌어 놓기는 `{Asset, id}` 한 형식(`EditorInputGUI`)이다
    // ------------------------------------------------------------------------------
    // 2) 공유 위젯 클래스: Vec3, 컴포넌트 카드, 헤더, 칩, 프로퍼티 행
    // ------------------------------------------------------------------------------
    class SW_EDITOR_API EditorWidgets
    {
    public:
        /**
         * @brief 레이블, RGB 축 버튼(리셋 기능 포함), 3개의 DragFloat 컨트롤을 가로로 배치합니다.
         */
        static bool drawVec3Control( const utf8* pLabel, float3& values, float32 resetValue = 0.0f, float32 columnWidth = 100.0f,
                                     float32 speed = 0.1f );

        /**
         * @brief 컴포넌트용 접이식 카드 UI 를 그립니다(패널 형태).
         */
        static bool beginComponentCard( const utf8* pName, uint64 id, bool* pBActive, bool* pBRemoveRequested, bool bAccent = false );

        /**
         * @brief beginComponentCard() 가 true 를 반환했으면, 내용을 그린 뒤 불러 카드를 닫습니다.
         */
        static void endComponentCard();

        /**
         * @brief 섹션 상단에 굵은 제목과 선택적인 부제목, 구분선을 그립니다.
         */
        static void drawSectionHeader( const utf8* pTitle, const utf8* pSubtitle = nullptr );

        /** @brief 툴바 컨트롤 사이에 `|` 구분선을 그립니다. SameLine 뒤에 이어서 그립니다. */
        static void drawToolbarSeparator();

        /**
         * @brief 기즈모 조작 모드(이동, 회전, 크기)와 로컬 공간 토글을 한 줄로 그립니다.
         * @details 뷰포트 툴바와 인스펙터가 함께 쓴다. 각자 그리면 한쪽만 고쳤을 때 두 화면이 서로 다른 모드를 가리킨다.
         *          값은 워크스페이스가 보관하므로(`getGizmoOperation`) 여기에는 그리는 방법만 모은다.
         * @param pMarkScope 단추 레이블(`<scope>.translate` · `.rotate` · `.scale` · `.space`)의 앞부분입니다. 그리는 자리마다 다르다 — 한 프레임에 같은 레이블이
         *                   두 자리에 남으면 자체 테스트가 어느 쪽인지 모르고 자리 잡기를 끝내 못 본다.
         */
        static void drawGizmoOperationControls( const utf8* pMarkScope, bool bVertical = false );

        /**
         * @brief 토글 · 필터 칩 버튼을 그립니다. 클릭되면 true 를 반환합니다.
         * @details 켜짐은 중립 한 단계(`_toggleActive`)다 — 단추마다 색을 받던 겹쳐 쓰기를 없앴다(초록 "Collapse" · 빨간 "Error Pause" 처럼 크롬이 신호색을 띠어
         *          진짜 신호 — 오류 수 — 와 구분되지 않았다). 뜻이 있는 색은 글자(데이터)에 둔다.
         */
        static bool drawToggleButton( const utf8* pLabel, bool bActive );
        /**
         * @brief 켬/끔 아이콘 버튼(정사각, 한 줄 높이)을 그립니다. 눌렸으면 true 입니다. 툴팁은 상태에 따라 @p pTooltipOn 과 @p pTooltipOff 중 하나입니다.
         * @details 한 변이 `GetFrameHeight()` 이므로 글꼴과 DPI 배율이 함께 반영된다. 픽셀 상수로 폭을 주면 150 % 배율에서 글자가 잘린다.
         */
        static bool drawToggleIconButton( const utf8* pID, bool bOn, const utf8* pIconOn, const utf8* pIconOff, const utf8* pTooltipOn,
                                          const utf8* pTooltipOff );
        /**
         * @brief 목록 행 안의 켬/끔 아이콘(눈 · 자물쇠)입니다 — 테두리 · 바탕 없이 아이콘만, 호버에만 옅은 바탕이 보인다. 눌렸으면 true 입니다.
         * @details 기본 상태(@p bOn == @p bDefaultOn)이면 흐린 글자색이라 행 이름이 먼저 읽히고, 기본에서 벗어난 행(숨김 · 잠금)만 본문 색으로 드러난다
         *          (앱 화면 규칙: 행마다 상자에 든 아이콘을 되풀이하지 않는다 — 언리얼 Outliner · 유니티 Hierarchy 도 호버 · 바뀐 행에서만 보인다).
         */
        static bool drawRowIconToggle( const utf8* pID, bool bOn, bool bDefaultOn, const utf8* pIconOn, const utf8* pIconOff, const utf8* pTooltipOn,
                                       const utf8* pTooltipOff );
        /**
         * @brief 켜진 상태를 색으로 보여 주는 아이콘 버튼(정사각, 한 줄 높이)을 그립니다. 눌렸으면 true 입니다. 상태는 호출하는 쪽이 바꿉니다.
         * @details 기즈모 모드나 뷰포트 토글처럼 켜졌는지가 색으로 보여야 할 때 쓴다. 툴팁은 @p pTooltip 이다.
         */
        static bool drawIconToggle( const utf8* pID, const utf8* pIcon, bool bActive, const utf8* pTooltip );
        /**
         * @brief @p width 안에 이름을 최대 @p maxLineCount 줄로 그립니다. 넘치면 마지막 줄 끝을 말줄임표로 줄이고 툴팁에 전체를 보여 줍니다.
         * @details 줄 바꿈은 공백, '_', '-', '.' 뒤에서 먼저 찾고, 없을 때만 글자 단위로 나눈다(`EditorLabelLayoutUtil::breakLines`).
         *          높이는 항상 @p maxLineCount 줄이다. 이름마다 높이가 달라져 타일 격자가 흐트러지는 것을 막는다.
         */
        static void drawClampedLabel( string_view text, float32 width, uint32 maxLineCount );

        /** @brief 흐린 글씨의 안내 문구를 그립니다. */
        static void drawEmptyHint( const utf8* pText );
        /**
         * @brief 빈 목록 · 빈 패널의 안내(왜 비었는지 · 다음 할 일 한 문장)와 그 다음 할 일 단추를 그립니다. 단추가 눌렸으면 true 입니다.
         * @details 틀과 머리글은 부르는 쪽이 그대로 둔다(앱 화면 규칙: 빈 절은 틀을 지키고 한 문장을 더한다). 언리얼 Content Browser 의 빈 폴더 안내 ·
         *          Godot 빈 씬 독의 "루트 노드 만들기" 단추와 같은 자리다 — 안내만 있고 할 일 단추가 없으면 사람이 메뉴를 뒤진다.
         * @param pActionLabel 다음 할 일 단추 이름표입니다. 비어 있으면 안내만 그리고 false 입니다.
         */
        [[nodiscard]] static bool drawEmptyState( const utf8* pText, const utf8* pActionLabel );
        /**
         * @brief 접힌 채로 시작하는 "고급" 절의 머리글을 그립니다. 펼쳐졌으면 true 이고, 그때만 고급 항목을 그립니다(짝 함수 없음).
         * @details 자주 쓰지 않는 설정을 기본 화면에서 걷어 내는 공용 입구다 — 언리얼 Details 패널의 Advanced 접기 · 유니티 Inspector 의 접힌 Debug 와 같다.
         *          펼침 상태는 ImGui 가 창 · @p pID 별로 기억한다. @p pID 는 한 창 안에서 절을 가리는 이름이다(`"##advanced.network"`).
         */
        [[nodiscard]] static bool beginAdvancedSection( const utf8* pID );
        /**
         * @brief 목록 위의 거르개 줄(검색 칸 + 오른쪽 끝 건수)을 그립니다. 검색어가 바뀌었으면 true 입니다.
         * @details 검색 칸이 건수를 뺀 남은 폭을 채운다. 패널마다 검색 칸 · 건수 · 폭 계산을 손으로 이으면 건수가 창 밖으로 밀리거나 칸 폭이 패널마다 달랐다
         *          (언리얼 · 유니티 · Godot 모두 목록 패널 맨 위에 같은 모양의 검색 줄을 둔다). 건수 형식은 `drawCountLabel` 과 같다.
         *          @p pMark 는 검색 칸 자리의 자체 시험 표시다 — 부른 뒤에 `note` 하면 마지막 항목인 건수 글에 찍혀 글자를 허공에 친다.
         */
        static bool drawFilterBar( const utf8* pID, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint, uint32 visible, uint32 total, const utf8* pUnit, const utf8* pMark );
        template <uint32 N>
        static bool drawFilterBar( const utf8* pID, fixed_string<N>& str, const utf8* pHint, uint32 visible, uint32 total, const utf8* pUnit, const utf8* pMark )
        {
            return drawFilterBar( pID, str.data(), str.capacity(), pHint, visible, total, pUnit, pMark );
        }

        /**
         * @brief 검색 결과가 0건일 때의 안내를 그립니다.
         * @details 데이터가 없는 것과 필터가 걸러 낸 것은 다르다. 맞는 항목이 없는 검색어를 입력했을 때 설명 없는 빈 상자만 남으면 고장처럼 보인다.
         *          현재 필터를 알려 주면 무엇을 지워야 하는지 바로 알 수 있다.
         * @param filter 현재 걸려 있는 검색 문자열입니다. 비어 있으면 필터를 언급하지 않고 안내합니다.
         */
        static void drawNoSearchResultHint( string_view filter );
        /** @brief 0건 안내를 그린 횟수를 반환합니다. 에디터 자체 테스트가 안내가 그려졌는지 확인할 때 씁니다. */
        static uint32 getNoSearchResultHintCount();
        /** @brief 검색어가 있는 0건 안내를 이번 프레임이나 지난 프레임에 그렸으면 true 입니다. 자동화 시나리오의 프로브 `Editor.NoSearchResultHintShown` 이 씁니다. */
        static bool wasFilteredNoResultHintDrawnRecently();

        /**
         * @brief 건수 라벨을 그립니다. total 이 0 이면 "12 items", 아니면 "12 / 40 lines" 형태입니다.
         */
        static void drawCountLabel( uint32 visible, uint32 total, const utf8* pUnit = nullptr );

        /** @brief 문구가 비어 있지 않으면 구분선과 흐린 상태 문구를 그립니다. */
        static void drawPanelStatus( const utf8* pText );
        /** @brief 편집 모델의 실패를 상태 줄 글로 만듭니다 — "문장: 대상"(대상이 없으면 문장만). */
        static string makeStatusText( const Error& error );

        /**
         * @brief C 버퍼를 쓰는 검색 필드를 그립니다. width 가 0 이면 사용 가능한 너비, 음수면 한 줄 전체입니다.
         * @details @p pHint 가 nullptr 이면 현재 로케일의 "Search..." 를 쓴다.
         * @param pMark 시나리오가 누를 레이블입니다. 입력 칸에 찍는다 - 부르는 쪽이 뒤에 찍으면 마지막 항목인 지우기 단추에 찍힌다.
         */
        static bool drawSearchField( const utf8* pID, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint = nullptr,
                                     float32 width = 0.0f, bool bShowClear = true, const utf8* pMark = nullptr );

        /**
         * @brief fixed_string 검색 필드 오버로드입니다.
         */
        template <uint32 N>
        static bool drawSearchField( const utf8* pID, fixed_string<N>& str, const utf8* pHint = nullptr,
                                     float32 width = 0.0f, bool bShowClear = true, const utf8* pMark = nullptr )
        {
            return drawSearchField( pID, str.data(), str.capacity(), pHint, width, bShowClear, pMark );
        }

        /**
         * @brief 색이 들어간 작은 태그(칩) 버튼을 그립니다.
         */
        static void drawChip( const utf8* pLabel, const Color& color );

        /**
         * @brief 프로퍼티 행의 라벨을 왼쪽에 그리고, 다음 항목이 라벨 열(테마 `Layout.PropertyLabelWidth` × DPI) 뒤의 남은 폭을 모두 쓰게 합니다.
         * @details 라벨이 붙는 위젯(`drawAssetSlot`, `drawColorEdit`)이 이 함수로 라벨 열을 맞춘다. 행을 열고 닫는 상태가 없어 짝 함수(end)를 두지 않는다. 다음 항목이 행의 끝이다.
         *          라벨이 열보다 길면 열 끝에서 말줄임으로 자르고 호버에 전체를 보인다 — 라벨이 입력 칸을 밀거나 겹치지 않는다(언리얼 Details 의 이름 열과 같다).
         * @param pTooltip 라벨에 호버하면 보일 설명입니다. 라벨은 짧게, 긴 설명은 여기에 둔다(유니티 Inspector 의 `[Tooltip]`). nullptr 이면 없다.
         */
        static void drawPropertyRowLabel( const utf8* pLabel, const utf8* pTooltip = nullptr );
        /**
         * @brief 도구줄 칸의 이름표를 칸 앞(왼쪽)에 붙입니다. 다음 항목의 폭은 건드리지 않습니다(도구줄은 칸마다 폭을 정한다).
         * @details ImGui 입력 위젯에 보이는 이름표를 주면 칸 오른쪽에 붙어 도구줄이 읽기 어렵다 — 이름표는 앞, 위젯 이름은 `##id` 로.
         */
        static void drawInlineLabel( const utf8* pLabel, const utf8* pTooltip = nullptr );

        /**
         * @brief `string` 을 직접 편집하는 한 줄 텍스트 입력을 그립니다. 값이 바뀌면 true 입니다.
         * @note `string&` 을 받는 입력은 이 함수 하나입니다. 검색창이 필요하면 `drawSearchField`(호출하는 쪽이 버퍼를 소유한다)나
         *       이 함수를 쓰세요.
         * @details ImGui 의 크기 변경 콜백으로 `string` 자체를 버퍼로 쓰므로 길이 제한이 없다. `fixed_string<N>` 임시 버퍼에
         *          복사해 편집한 뒤 되쓰면 그 크기를 넘는 문자열이 잘려서 긴 대사의 뒷부분이 사라진다.
         * @param pLabel ImGui 라벨입니다. `##` 로 시작하면 라벨을 감추고, 보이는 부분이 있으면 칸 **앞**(왼쪽)에 흐린 이름표로 그립니다(ImGui 기본은 오른쪽).
         * @param text   편집 대상입니다. 값이 바뀔 때만 갱신됩니다.
         * @param width  0 이면 ImGui 기본 폭, 음수면 남은 폭 전부, 양수면 그 폭입니다.
         */
        static bool drawTextField( const utf8* pLabel, string& text, float32 width = 0.0f );
        /**
         * @brief `drawTextField` 의 여러 줄 버전입니다(`PROPERTY( Multiline )`). 높이는 @p lineCount 줄만큼이고 폭은 남은 폭 전부입니다.
         * @param pHint 비어 있고 치는 중이 아닐 때 칸 안에 흐리게 보일 안내입니다(ImGui 여러 줄 칸에는 힌트가 없어 빈 상자만 남았다). nullptr 이면 없다.
         * @param flags 더할 `ImGuiInputTextFlags` 입니다(원문 편집의 탭 입력).
         */
        static bool drawTextFieldMultiline( const utf8* pLabel, string& text, uint32 lineCount = 4, const utf8* pHint = nullptr, int32 flags = 0 );
        /**
         * @brief 이름표를 칸 앞(왼쪽)에 둔 콤보를 엽니다. 열렸으면 true 이고 `ImGui::EndCombo()` 로 닫습니다. 폭은 부르는 쪽이 `SetNextItemWidth` 로 정합니다(이름표 뒤 칸에 걸린다).
         * @details `ImGui::BeginCombo( "Name###id" )` 는 이름표를 오른쪽에 붙여 다음 칸의 이름처럼 읽혔다(Packaging "Client [v] Target Empty [v]"). id 는 @p pLabel 전체에서 뽑는다.
         */
        [[nodiscard]] static bool beginCombo( const utf8* pLabel, const utf8* pPreview, int32 flags = 0 );

        // ------------------------------------------------------------------------------
        // ImGui 입력 위젯 — 에디터가 부르는 유일한 길이다(`EditorInputBan.h` 가 ImGui 의 것을 막는다). id 는 숨은 id 형식이라 이름표를 오른쪽에 둘 수 없다.
        // 이름표는 앞에 따로 그린다(`drawPropertyRowLabel` · `drawInlineLabel`). 인자 · 기본값은 ImGui 1.92 의 같은 이름 함수와 같다(플래그는 `ImGui*Flags_` 값).
        // ------------------------------------------------------------------------------
        static constexpr int32 kImGuiDefaultPopupHeight = -1;  ///< 콤보가 펼치는 높이 — ImGui 기본(항목 8 개)
        static constexpr int32 kImGuiDefaultIntStepFast = 100; ///< `inputInt` 의 Ctrl 단계 — ImGui 기본

        using InputTextCallback = int ( * )( ImGuiInputTextCallbackData* pData );
        using ComboItemGetter   = const utf8* (*)( void* pUserData, int32 index );

        /** @brief 단계 단추(-, +)가 붙는 숫자 칸입니다. @p pMark 는 시나리오가 누를 레이블로, 글 칸 부분에 찍는다 — 부르는 쪽이 뒤에 찍으면 마지막 항목(단추 묶음)에 찍혀 클릭이 `+` 를 누른다. */
        static bool inputInt( EditorWidgetID id, int32* pValue, int32 step = 1, int32 stepFast = kImGuiDefaultIntStepFast, int32 flags = 0, const utf8* pMark = nullptr );
        static bool inputInt2( EditorWidgetID id, int32* arrValue, int32 flags = 0 );
        static bool inputFloat( EditorWidgetID id, float32* pValue, float32 step = 0.0f, float32 stepFast = 0.0f, const utf8* pFormat = "%.3f", int32 flags = 0, const utf8* pMark = nullptr );
        static bool inputFloat2( EditorWidgetID id, float32* arrValue, const utf8* pFormat = "%.3f", int32 flags = 0 );
        static bool inputFloat4( EditorWidgetID id, float32* arrValue, const utf8* pFormat = "%.3f", int32 flags = 0 );
        static bool inputScalar( EditorWidgetID id, int32 dataType, void* pData, const void* pStep = nullptr, const void* pStepFast = nullptr, const utf8* pFormat = nullptr,
                                 int32 flags = 0, const utf8* pMark = nullptr );
        static bool inputScalarN( EditorWidgetID id, int32 dataType, void* pData, int32 components, const void* pStep = nullptr, const void* pStepFast = nullptr,
                                  const utf8* pFormat = nullptr, int32 flags = 0 );
        static bool inputText( EditorWidgetID id, utf8* pBuffer, size_t bufferSize, int32 flags = 0, InputTextCallback pfnCallback = nullptr, void* pUserData = nullptr );
        static bool inputTextMultiline( EditorWidgetID id, utf8* pBuffer, size_t bufferSize, const ImVec2& size, int32 flags = 0, InputTextCallback pfnCallback = nullptr,
                                        void* pUserData = nullptr );
        static bool inputTextWithHint( EditorWidgetID id, const utf8* pHint, utf8* pBuffer, size_t bufferSize, int32 flags = 0, InputTextCallback pfnCallback = nullptr,
                                       void* pUserData = nullptr );
        static bool dragFloat( EditorWidgetID id, float32* pValue, float32 speed = 1.0f, float32 minValue = 0.0f, float32 maxValue = 0.0f, const utf8* pFormat = "%.3f",
                               int32 flags = 0 );
        static bool dragFloat2( EditorWidgetID id, float32* arrValue, float32 speed = 1.0f, float32 minValue = 0.0f, float32 maxValue = 0.0f, const utf8* pFormat = "%.3f",
                                int32 flags = 0 );
        static bool dragFloat3( EditorWidgetID id, float32* arrValue, float32 speed = 1.0f, float32 minValue = 0.0f, float32 maxValue = 0.0f, const utf8* pFormat = "%.3f",
                                int32 flags = 0 );
        static bool dragFloat4( EditorWidgetID id, float32* arrValue, float32 speed = 1.0f, float32 minValue = 0.0f, float32 maxValue = 0.0f, const utf8* pFormat = "%.3f",
                                int32 flags = 0 );
        static bool dragInt( EditorWidgetID id, int32* pValue, float32 speed = 1.0f, int32 minValue = 0, int32 maxValue = 0, const utf8* pFormat = "%d", int32 flags = 0 );
        static bool dragScalar( EditorWidgetID id, int32 dataType, void* pData, float32 speed = 1.0f, const void* pMin = nullptr, const void* pMax = nullptr,
                                const utf8* pFormat = nullptr, int32 flags = 0 );
        static bool dragScalarN( EditorWidgetID id, int32 dataType, void* pData, int32 components, float32 speed = 1.0f, const void* pMin = nullptr, const void* pMax = nullptr,
                                 const utf8* pFormat = nullptr, int32 flags = 0 );
        static bool sliderFloat( EditorWidgetID id, float32* pValue, float32 minValue, float32 maxValue, const utf8* pFormat = "%.3f", int32 flags = 0 );
        static bool sliderInt( EditorWidgetID id, int32* pValue, int32 minValue, int32 maxValue, const utf8* pFormat = "%d", int32 flags = 0 );
        static bool sliderScalar( EditorWidgetID id, int32 dataType, void* pData, const void* pMin, const void* pMax, const utf8* pFormat = nullptr, int32 flags = 0 );
        static bool vSliderFloat( EditorWidgetID id, const ImVec2& size, float32* pValue, float32 minValue, float32 maxValue, const utf8* pFormat = "%.3f", int32 flags = 0 );
        static bool combo( EditorWidgetID id, int32* pCurrent, const utf8* const* arrItem, int32 itemCount, int32 popupMaxHeightInItems = kImGuiDefaultPopupHeight );
        static bool combo( EditorWidgetID id, int32* pCurrent, const utf8* pItemsSeparatedByZeros, int32 popupMaxHeightInItems = kImGuiDefaultPopupHeight );
        static bool combo( EditorWidgetID id, int32* pCurrent, ComboItemGetter pfnGetter, void* pUserData, int32 itemCount, int32 popupMaxHeightInItems = kImGuiDefaultPopupHeight );
        static bool colorEdit3( EditorWidgetID id, float32* arrColor, int32 flags = 0 );
        static bool colorEdit4( EditorWidgetID id, float32* arrColor, int32 flags = 0 );
        /** @brief 목록 상자를 엽니다. 열렸으면 true 이고 `ImGui::EndListBox()` 로 닫습니다. */
        [[nodiscard]] static bool beginListBox( EditorWidgetID id, const ImVec2& size );

        /** @brief 힌트 글 입력 칸 하나의 결과입니다. */
        struct HintFieldResult
        {
            bool _bChanged{ SW_FALSE };           ///< 이번 프레임에 글이 바뀌었다(끌어 놓기 포함) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool _bSubmitted{ SW_FALSE };         ///< Enter 로 냈다(`bSubmitOnEnter` 일 때만) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool _bDeactivatedAfterEdit{ false }; ///< 고친 뒤 칸을 떠났거나 끌어 놓았다(그때 적용하는 칸) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };
        /**
         * @brief 힌트가 있는 한 줄 입력 칸을 그립니다(검색 팝업 · 새 경로 · 주소). 폭은 부르는 쪽이 `SetNextItemWidth` 로 정합니다.
         * @details @p pMark 가 있으면 입력 칸 자리에 자체 시험 표시(`EditorSelfTestMarks::note`)를 남긴다 — 위젯이 단추를 덧붙여도 표시가 칸에 붙는다.
         *          패널마다 `InputTextWithHint` 와 표시를 손으로 이으면 표시가 다른 항목에 붙거나 Enter 판정이 갈렸다.
         */
        static HintFieldResult drawHintField( const utf8* pID, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint, const utf8* pMark, bool bSubmitOnEnter = false );
        template <uint32 N>
        static HintFieldResult drawHintField( const utf8* pID, fixed_string<N>& str, const utf8* pHint, const utf8* pMark, bool bSubmitOnEnter = false )
        {
            return drawHintField( pID, str.data(), str.capacity(), pHint, pMark, bSubmitOnEnter );
        }
        /**
         * @brief 리소스 id 를 치거나 콘텐츠 브라우저에서 끌어 놓는 경로 칸입니다(`drawHintField` + 끌어 놓기). @p pExpectedExt 에 맞는 애셋만 받습니다(`drawAssetSlot` 과 같은 거르개).
         * @details 끌어 놓기만 받는 `drawAssetSlot` 과 달리 글로 칠 수 있다 — 시나리오가 경로를 치는 칸(골격 · 미리보기 프리팹 · 클립)이 쓴다.
         */
        static HintFieldResult drawAssetPathField( const utf8* pID, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint, const utf8* pExpectedExt, const utf8* pMark );
        template <uint32 N>
        static HintFieldResult drawAssetPathField( const utf8* pID, fixed_string<N>& str, const utf8* pHint, const utf8* pExpectedExt, const utf8* pMark )
        {
            return drawAssetPathField( pID, str.data(), str.capacity(), pHint, pExpectedExt, pMark );
        }

        /**
         * @brief 애셋 경로 표시, 드래그앤드롭 수신(Payload), 클리어/찾아보기 버튼을 지원하는 애셋 슬롯을 그립니다.
         * @details 돌아온 뒤의 "마지막 항목" 은 슬롯이다 — 뒤의 자체 시험 표시 · 툴팁이 지우기 단추가 아니라 슬롯에 붙는다.
         * @param pExpectedExt 받을 확장자 목록입니다. 쉼표 또는 공백으로 나눕니다(`".png,.jpg,.tga"`). nullptr 이나 빈 문자열이면 모두 받습니다(`EditorAssetFilter`).
         */
        static bool drawAssetSlot( const utf8* pLabel, string& assetPath, const utf8* pExpectedExt = nullptr );

        /**
         * @brief Color 색상을 편집할 수 있는 일관된 색상 편집기 위젯을 그립니다.
         */
        static bool drawColorEdit( const utf8* pLabel, Color& color );

        /**
         * @brief 인스펙터 패널 등에서 사용할 공통 ImGui 스타일을 스택에 푸시합니다.
         */
        static void pushInspectorStyle();

        /**
         * @brief pushInspectorStyle()로 적용한 스타일을 스택에서 팝하여 원래대로 되돌립니다.
         */
        static void popInspectorStyle();

        /**
         * @brief 현재 아이템을 애셋 끌기 원본으로 만듭니다. 페이로드는 `{Asset, 경로의 id}` 이고 경로는 id 표에 둔다(`EditorInputGUI::registerPayloadName`).
         */
        static void drawAssetDragSource( const utf8* pRelativePath, bool bAllowNullID = false );

        /**
         * @brief 현재 아이템을 애셋 드롭 타깃으로 만들고, 애셋이 놓였으면 그 상대 경로를 @p outPath 에 담고 true 입니다.
         */
        [[nodiscard]] static bool acceptAssetDrop( string& outPath );
        /** @brief `acceptAssetDrop` 에 @p pExpectedExt 거르개(`drawAssetSlot` 과 같은 규칙)를 더합니다. 맞지 않는 애셋은 받지 않습니다. 글 칸에 끌어 놓기를 붙일 때 씁니다. */
        [[nodiscard]] static bool acceptAssetDrop( string& outPath, const utf8* pExpectedExt );
        /**
         * @brief 화면 사각형 [@p minPos, @p maxPos] 를 애셋 드롭 타깃으로 만들고, 애셋이 그 위로 끌려 와 있으면(놓기 전 포함) 경로를 받아 true 입니다.
         *        놓였으면 @p outbDelivered 가 true 입니다. @p pTargetID 는 창 안에서 타깃을 가리는 이름입니다.
         * @details 뷰포트 캔버스처럼 위에 오버레이 · 기즈모 · HUD 가 그려지는 영역에 씁니다 — "마지막 아이템" 을 타깃으로 잡는 `acceptAssetDrop` 은
         *          캔버스 다음에 그린 아이템(오버레이 단추 등)에 걸려 캔버스에 놓은 것을 받지 못한다. 놓기 전 프레임은 배치 고스트를 그린다.
         */
        [[nodiscard]] static bool previewAssetDropInRect( const float2& minPos, const float2& maxPos, const utf8* pTargetID, string& outPath, bool& outbDelivered );

        /**
         * @brief 검색 결과 목록에서 Up/Down 으로 선택을 움직입니다. Enter 면 true 입니다.
         */
        static bool updateListSelection( int32& selectedIndex, int32 itemCount, bool bRepeat = true );

        /**
         * @brief 열릴 때마다 호스트(주) 뷰포트 가운데에 뜨는 자동 크기 모달을 시작합니다. 에디터의 모달은 `ImGui::BeginPopupModal` 대신 이것을 씁니다.
         * @details ImGui 의 기본 위치는 창 이름마다 처음 한 번만 가운데라서, 창 크기가 바뀐 뒤 다시 열면 옛 위치(작아진 창의 오른쪽 아래 가장자리)에 붙는다.
         *          true 이면 `ImGui::EndPopup()` 으로 닫습니다.
         */
        static bool beginCenteredModal( const utf8* pPopupID );

        /**
         * @brief Save / Don't Save / Cancel 모달을 그립니다. 버튼이 눌리기 전에는 None 입니다.
         */
        static EditorUnsavedChoice drawUnsavedChangesModal( const utf8* pPopupID, const utf8* pMessage );

        /**
         * @brief 직전에 그린 UI 항목 위에 마우스를 올리면 보일 툴팁을 그립니다.
         */
        static void drawTooltip( const utf8* pText );

        /**
         * @brief 물음표 '(?)' 도움말 마커를 표시하고 호버 시 툴팁을 띄웁니다.
         */
        static void drawHelpMarker( const utf8* pDesc );
    };
} // namespace sw::editor
