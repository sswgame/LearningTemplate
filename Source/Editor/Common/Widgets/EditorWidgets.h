/**
 * @file EditorWidgets.h
 * @brief 에디터 창들이 함께 쓰는 ImGui 위젯입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorColor.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"

namespace sw
{
    struct float3;
} // namespace sw

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // 1) 드래그 페이로드 (색 타입 Color 와 style 은 EditorColor.h)
    // ------------------------------------------------------------------------------
    /** @brief 콘텐츠 브라우저에서 뷰포트나 인스펙터로 애셋을 드래그할 때 쓰는 페이로드 이름입니다. */
    inline constexpr const utf8* kAssetPathPayload = "SW_ASSET_PATH";

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
         */
        static void drawGizmoOperationControls( bool bVertical = false );

        /**
         * @brief 활성과 비활성에 따라 색이 바뀌는 토글 및 필터 칩 버튼을 그립니다. 클릭되면 true 를 반환합니다.
         */
        static bool drawToggleButton( const utf8* pLabel, bool bActive, const Color& activeColor );
        /** @brief 켜진 색이 테마 강조색인 토글입니다. */
        static bool drawToggleButton( const utf8* pLabel, bool bActive );
        /**
         * @brief 켬/끔 아이콘 버튼(정사각, 한 줄 높이)을 그립니다. 눌렸으면 true 입니다. 툴팁은 상태에 따라 @p pTooltipOn 과 @p pTooltipOff 중 하나입니다.
         * @details 한 변이 `GetFrameHeight()` 이므로 글꼴과 DPI 배율이 함께 반영된다. 픽셀 상수로 폭을 주면 150 % 배율에서 글자가 잘린다.
         */
        static bool drawToggleIconButton( const utf8* pID, bool bOn, const utf8* pIconOn, const utf8* pIconOff, const utf8* pTooltipOn,
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

        /**
         * @brief C 버퍼를 쓰는 검색 필드를 그립니다. width 가 0 이면 사용 가능한 너비, 음수면 한 줄 전체입니다.
         * @details @p pHint 가 nullptr 이면 현재 로케일의 "Search..." 를 쓴다.
         */
        static bool drawSearchField( const utf8* pID, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint = nullptr,
                                     float32 width = 0.0f, bool bShowClear = true );

        /**
         * @brief fixed_string 검색 필드 오버로드입니다.
         */
        template <uint32 N>
        static bool drawSearchField( const utf8* pID, fixed_string<N>& str, const utf8* pHint = nullptr,
                                     float32 width = 0.0f, bool bShowClear = true )
        {
            return drawSearchField( pID, str.data(), str.capacity(), pHint, width, bShowClear );
        }

        /**
         * @brief 색이 들어간 작은 태그(칩) 버튼을 그립니다.
         */
        static void drawChip( const utf8* pLabel, const Color& color );

        /**
         * @brief 프로퍼티 행의 라벨을 왼쪽에 그리고, 다음 항목이 @p labelWidth 부터 남은 폭을 모두 쓰게 합니다.
         * @details 라벨이 붙는 위젯(`drawAssetSlot`, `drawColorEdit`)이 이 함수로 라벨 열을 맞춘다. 행을 열고 닫는 상태가 없어 짝 함수(end)를 두지 않는다. 다음 항목이 행의 끝이다.
         */
        static void drawPropertyRowLabel( const utf8* pLabel, float32 labelWidth = 120.0f );

        /**
         * @brief `string` 을 직접 편집하는 한 줄 텍스트 입력을 그립니다. 값이 바뀌면 true 입니다.
         * @note `string&` 을 받는 입력은 이 함수 하나입니다. 검색창이 필요하면 `drawSearchField`(호출하는 쪽이 버퍼를 소유한다)나
         *       이 함수를 쓰세요.
         * @details ImGui 의 크기 변경 콜백으로 `string` 자체를 버퍼로 쓰므로 길이 제한이 없다. `fixed_string<N>` 임시 버퍼에
         *          복사해 편집한 뒤 되쓰면 그 크기를 넘는 문자열이 잘려서 긴 대사의 뒷부분이 사라진다.
         * @param pLabel ImGui 라벨입니다. `##` 로 시작하면 라벨을 감춥니다.
         * @param text   편집 대상입니다. 값이 바뀔 때만 갱신됩니다.
         * @param width  0 이면 ImGui 기본 폭, 음수면 남은 폭 전부, 양수면 그 폭입니다.
         */
        static bool drawTextField( const utf8* pLabel, string& text, float32 width = 0.0f );
        /** @brief `drawTextField` 의 여러 줄 버전입니다(`PROPERTY( Multiline )`). 높이는 @p lineCount 줄만큼이고 폭은 남은 폭 전부입니다. */
        static bool drawTextFieldMultiline( const utf8* pLabel, string& text, uint32 lineCount = 4 );

        /**
         * @brief 애셋 경로 표시, 드래그앤드롭 수신(Payload), 클리어/찾아보기 버튼을 지원하는 애셋 슬롯을 그립니다.
         * @param pExpectedExt 받을 확장자 목록입니다. 쉼표 또는 공백으로 나눕니다(`".png,.jpg,.tga"`). nullptr 이나 빈 문자열이면 모두 받습니다(`EditorAssetFilter`).
         */
        static bool drawAssetSlot( const utf8* pLabel, string& assetPath, const utf8* pExpectedExt = nullptr,
                                   float32 labelWidth = 120.0f );

        /**
         * @brief Color 색상을 편집할 수 있는 일관된 색상 편집기 위젯을 그립니다.
         */
        static bool drawColorEdit( const utf8* pLabel, Color& color, float32 labelWidth = 120.0f );

        /**
         * @brief 인스펙터 패널 등에서 사용할 공통 ImGui 스타일을 스택에 푸시합니다.
         */
        static void pushInspectorStyle();

        /**
         * @brief pushInspectorStyle()로 적용한 스타일을 스택에서 팝하여 원래대로 되돌립니다.
         */
        static void popInspectorStyle();

        /**
         * @brief 현재 아이템을 애셋 경로 드래그 소스로 등록합니다.
         */
        static void drawAssetDragSource( const utf8* pRelativePath, bool bAllowNullID = false );

        /**
         * @brief BeginDragDropTarget 안에서 애셋 경로 페이로드를 받습니다.
         */
        [[nodiscard]] static bool tryAcceptAssetPayload( string& outPath );

        /**
         * @brief 현재 아이템을 애셋 드롭 타깃으로 만들고 경로를 받습니다.
         */
        static bool acceptAssetDrop( string& outPath );
        /**
         * @brief 화면 사각형 [@p minPos, @p maxPos] 를 애셋 드롭 타깃으로 만들고 경로를 받습니다. @p pTargetID 는 창 안에서 타깃을 가리는 이름입니다.
         * @details 뷰포트 캔버스처럼 위에 오버레이 · 기즈모 · HUD 가 그려지는 영역에 씁니다 — "마지막 아이템" 을 타깃으로 잡는 `acceptAssetDrop` 은
         *          캔버스 다음에 그린 아이템(오버레이 단추 등)에 걸려 캔버스에 놓은 것을 받지 못한다.
         */
        static bool acceptAssetDropInRect( const float2& minPos, const float2& maxPos, const utf8* pTargetID, string& outPath );

        /**
         * @brief 검색 결과 목록에서 Up/Down 으로 선택을 움직입니다. Enter 면 true 입니다.
         */
        static bool updateListSelection( int32& selectedIndex, int32 itemCount, bool bRepeat = true );

        /**
         * @brief 열릴 때마다 호스트(주) 뷰포트 가운데에 뜨는 자동 크기 모달을 시작합니다. 에디터의 모달은 `ImGui::BeginPopupModal` 대신 이것을 씁니다.
         * @details ImGui 의 기본 위치는 창 이름마다 처음 한 번만 가운데라서, 창 크기가 바뀐 뒤 다시 열면 옛 자리(작아진 창의 오른쪽 아래 가장자리)에 붙는다.
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
