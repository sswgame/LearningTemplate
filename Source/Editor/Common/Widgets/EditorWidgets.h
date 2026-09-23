/**
 * @file EditorWidgets.h
 * @brief 에디터 창들이 함께 쓰는 ImGui 위젯입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Workspace/EditorSessionPolicy.h"

namespace sw
{
    struct float3;
} // namespace sw

namespace sw::editor
{
    // ------------------------------------------------------------------------------
    // 1) Color4 · style — 축/강조/상태 색 (0~1)
    // ------------------------------------------------------------------------------
    /** @brief RGBA 색 (0~1) */
    struct Color4
    {
        float32 _r{ 1.0f };
        float32 _g{ 1.0f };
        float32 _b{ 1.0f };
        float32 _a{ 1.0f };
    };

    /** @brief 에디터 UI 공통 색 (축 / 강조 / 헤더 / 상태) */
    namespace style
    {
        inline constexpr Color4 kAxisX{ 0.85f, 0.25f, 0.25f, 1.0f };
        inline constexpr Color4 kAxisY{ 0.30f, 0.75f, 0.35f, 1.0f };
        inline constexpr Color4 kAxisZ{ 0.25f, 0.55f, 0.95f, 1.0f };
        inline constexpr Color4 kAccent{ 0.27f, 0.57f, 1.0f, 1.0f };
        inline constexpr Color4 kHeader{ 0.18f, 0.21f, 0.28f, 1.0f };
        inline constexpr Color4 kOk{ 0.20f, 0.75f, 0.35f, 1.0f };
        inline constexpr Color4 kWarn{ 0.95f, 0.70f, 0.15f, 1.0f };
        inline constexpr Color4 kError{ 0.95f, 0.30f, 0.25f, 1.0f };
        inline constexpr Color4 kToggleActive{ 0.27f, 0.57f, 1.0f, 1.0f };
        inline constexpr Color4 kToggleInactive{ 0.20f, 0.22f, 0.26f, 0.60f };
    } // namespace style

    /** @brief 콘텐츠 브라우저 → 뷰포트/인스펙터 애셋 드래그 페이로드 */
    inline constexpr const utf8* kAssetPathPayload = "SW_ASSET_PATH";

    // ------------------------------------------------------------------------------
    // 2) 공유 위젯 클래스 — Vec3 / 컴포넌트 카드 / 헤더 / 칩 / 프로퍼티 행
    // ------------------------------------------------------------------------------
    class EditorWidgets
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

        /** @brief 툴바 컨트롤 사이의 `|` 구분선입니다. SameLine 뒤에 이어서 그립니다. */
        static void drawToolbarSeparator();

        /**
         * @brief 기즈모 조작 모드(이동 · 회전 · 크기)와 로컬 공간 토글을 한 줄로 그립니다.
         * @details 뷰포트 툴바와 인스펙터 **둘이 같은 다섯 줄을 각자** 그리고 있었습니다. 라디오 값 `0` · `1` · `2` 가 양쪽에
         *          숫자 그대로 박혀 있어서, 모드를 하나 더하려면 두 곳을 고쳐야 했고 **한쪽만 고치면 두 화면이 서로 다른 모드를
         *          가리킵니다.** 값은 워크스페이스가 들고 있으므로(`getGizmoOperation`), 그리는 방법만 여기로 모읍니다.
         */
        static void drawGizmoOperationControls();

        /**
         * @brief 활성 · 비활성 색이 바뀌는 토글 · 필터 칩 버튼입니다. 클릭되면 true 입니다.
         */
        static bool drawToggleButton( const utf8* pLabel, bool bActive, const Color4& activeColor = style::kToggleActive );

        /** @brief 흐린 글씨의 안내 문구를 그립니다. */
        static void drawEmptyHint( const utf8* pText );

        /**
         * @brief 검색 결과가 0건일 때의 안내를 그립니다.
         * @details **데이터가 없는 것과 필터가 걸러낸 것은 다릅니다.** 기존 안내 문구 20곳은 모두 앞의 경우(데이터 · 서비스
         *          없음)여서, 아무것도 맞지 않는 검색어를 치면 설명 없는 빈 상자만 남아 고장처럼 보였습니다. 필터를 되짚어 주면
         *          무엇을 지워야 하는지 바로 압니다.
         * @param filter 현재 걸린 검색 문자열. 비어 있으면 필터 언급 없이 안내합니다.
         */
        static void drawNoSearchResultHint( string_view filter );

        /**
         * @brief 건수 라벨을 그립니다. total 이 0 이면 "12 items", 아니면 "12 / 40 lines" 형태입니다.
         */
        static void drawCountLabel( uint32 visible, uint32 total, const utf8* pUnit = nullptr );

        /** @brief 문구가 비어 있지 않으면 구분선과 흐린 상태 문구를 그립니다. */
        static void drawPanelStatus( const utf8* pText );

        /**
         * @brief C 버퍼를 쓰는 검색 필드입니다. width 가 0 이면 사용 가능한 너비, 음수면 한 줄 전체입니다.
         */
        static bool drawSearchField( const utf8* pId, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint = "Search...",
                                     float32 width = 0.0f, bool bShowClear = true );

        /**
         * @brief fixed_string 검색 필드 오버로드입니다.
         */
        template <uint32 N>
        static bool drawSearchField( const utf8* pId, fixed_string<N>& str, const utf8* pHint = "Search...",
                                     float32 width = 0.0f, bool bShowClear = true )
        {
            return drawSearchField( pId, str.data(), str.capacity(), pHint, width, bShowClear );
        }

        /**
         * @brief 색이 들어간 작은 태그(칩) 버튼을 그립니다.
         */
        static void drawChip( const utf8* pLabel, const Color4& color );

        /**
         * @brief 프로퍼티 항목의 라벨을 왼쪽에 표시하고, 오른쪽 편집 컨트롤을 배치할 준비를 합니다.
         */
        static bool drawPropertyRowBegin( const utf8* pLabel, float32 labelWidth = 120.0f );

        /**
         * @brief drawPropertyRowBegin() 뒤에 불러 프로퍼티 행을 마칩니다.
         */
        static void drawPropertyRowEnd();

        /**
         * @brief `string` 을 직접 편집하는 한 줄 텍스트 입력입니다. 값이 바뀌면 true 입니다.
         * @note **`string&` 을 받는 입력은 이것 하나입니다.** 예전에 `drawSearchFilter` 가 하나 더 있었는데, 그쪽은 `string` 을
         *       256바이트 스택 버퍼에 베껴 편집하고 되쓰는 방식이라 **그보다 긴 필터를 조용히 잘랐습니다.** 부르는 곳이 하나도
         *       없었고(죽은 API), 남겨 두면 "문자열 검색 위젯" 을 찾는 사람이 그 함정으로 가게 됩니다. 그래서 지웠습니다.
         *       검색창이 필요하면 `drawSearchField`(호출부가 버퍼를 소유합니다)나 이 함수를 쓰십시오.
         * @details 패널들이 `fixed_string<N> buf{ text.c_str() }` 로 임시 버퍼를 만들어 넣었다 빼는 일을 16곳에서 각자 하고
         *          있었습니다. 버퍼 크기(64 · 128 · 256 · 512)를 자리마다 골랐고, **그 크기를 넘는 문자열은 잘려서
         *          되돌아갔습니다.** 긴 대사 한 줄을 편집하면 뒷부분이 사라졌습니다. 여기서는 ImGui 의 크기 변경 콜백으로
         *          `string` 자체를 버퍼로 쓰므로 길이 제한이 없습니다.
         * @param pLabel ImGui 라벨(`##` 접두면 라벨을 감춥니다)
         * @param text   편집 대상. 바뀔 때만 갱신됩니다.
         * @param width  0 이면 ImGui 기본, 음수면 남는 자리 전부, 양수면 그 폭
         */
        static bool drawTextField( const utf8* pLabel, string& text, float32 width = 0.0f );

        /**
         * @brief 애셋 경로 표시, 드래그앤드롭 수신(Payload), 클리어/찾아보기 버튼을 지원하는 애셋 슬롯을 그립니다.
         */
        static bool drawAssetSlot( const utf8* pLabel, string& assetPath, const utf8* pExpectedExt = nullptr,
                                   float32 labelWidth = 120.0f );

        /**
         * @brief 패널 사이의 영역 크기를 조절할 수 있는 스플리터를 그립니다.
         */
        static bool drawSplitter( const utf8* pId, bool bVertical, float32 thickness, float32* pSize1, float32* pSize2,
                                  float32 minSize1 = 50.0f, float32 minSize2 = 50.0f );

        /**
         * @brief Color4 색상을 편집할 수 있는 일관된 색상 편집기 위젯을 그립니다.
         */
        static bool drawColorEdit( const utf8* pLabel, Color4& color, float32 labelWidth = 120.0f );

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
        static void drawAssetDragSource( const utf8* pRelativePath, bool bAllowNullId = false );

        /**
         * @brief BeginDragDropTarget 안에서 애셋 경로 페이로드를 받습니다.
         */
        static bool tryAcceptAssetPayload( string& outPath );

        /**
         * @brief 현재 아이템을 애셋 드롭 타깃으로 만들고 경로를 받습니다.
         */
        static bool acceptAssetDrop( string& outPath );

        /**
         * @brief 검색 결과 목록에서 Up/Down 으로 선택을 움직입니다. Enter 면 true 입니다.
         */
        static bool updateListSelection( int32& selectedIndex, int32 itemCount, bool bRepeat = true );

        /**
         * @brief Save / Don't Save / Cancel 모달을 그립니다. 버튼이 눌리기 전에는 None 입니다.
         */
        static EditorUnsavedChoice drawUnsavedChangesModal( const utf8* pPopupId, const utf8* pMessage );

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
