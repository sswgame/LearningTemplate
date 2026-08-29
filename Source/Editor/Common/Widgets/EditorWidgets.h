/**
 * @file EditorWidgets.h
 * @brief 에디터 윈도우용 공유 ImGui 위젯
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorSessionPolicy.h"

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
		inline constexpr Color4 kAxisX{ 0.80f, 0.10f, 0.15f, 1.0f };
		inline constexpr Color4 kAxisY{ 0.20f, 0.70f, 0.20f, 1.0f };
		inline constexpr Color4 kAxisZ{ 0.10f, 0.25f, 0.80f, 1.0f };
		inline constexpr Color4 kAccent{ 0.78f, 0.22f, 0.18f, 1.0f };
		inline constexpr Color4 kHeader{ 0.22f, 0.35f, 0.48f, 1.0f };
		inline constexpr Color4 kOk{ 0.20f, 0.65f, 0.30f, 1.0f };
		inline constexpr Color4 kWarn{ 0.85f, 0.65f, 0.15f, 1.0f };
		inline constexpr Color4 kError{ 0.70f, 0.20f, 0.20f, 1.0f };
		inline constexpr Color4 kToggleActive{ 0.22f, 0.45f, 0.75f, 1.0f };
		inline constexpr Color4 kToggleInactive{ 0.20f, 0.20f, 0.20f, 0.60f };
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
		 * @brief 컴포넌트 전용 접이식 카드 UI를 렌더링합니다. (패널 형태)
		 */
		static bool beginComponentCard( const utf8* pName, uint64 id, bool* pBActive, bool* pBRemoveRequested, bool bAccent = false );

		/**
		 * @brief beginComponentCard()가 true를 반환했을 때, 내용 렌더링 후 호출하여 카드를 닫습니다.
		 */
		static void endComponentCard();

		/**
		 * @brief 섹션 상단에 굵은 제목과 선택적인 부제목, 구분선을 그립니다.
		 */
		static void drawSectionHeader( const utf8* pTitle, const utf8* pSubtitle = nullptr );

		/** @brief 툴바 컨트롤 사이 `|` 구분. SameLine 뒤에 이어서 그립니다. */
		static void drawToolbarSeparator();

		/**
		 * @brief 활성/비활성 색이 바뀌는 토글·필터 칩 버튼. 클릭되면 true.
		 */
		static bool drawToggleButton( const utf8* pLabel, bool bActive, const Color4& activeColor = style::kToggleActive );

		/** @brief 비활성 안내 문구. */
		static void drawEmptyHint( const utf8* pText );

		/**
		 * @brief 건수 라벨. total이 0이면 "12 items", 아니면 "12 / 40 lines".
		 */
		static void drawCountLabel( uint32 visible, uint32 total, const utf8* pUnit = nullptr );

		/** @brief 비어 있지 않으면 Separator + 비활성 상태 문구. */
		static void drawPanelStatus( const utf8* pText );

		/**
		 * @brief C 버퍼 검색 필드. width 0이면 가용 너비, 음수면 한 줄 전체.
		 */
		static bool drawSearchField( const utf8* pId, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint = "Search...",
									 float32 width = 0.0f, bool bShowClear = true );

		/**
		 * @brief fixed_string 검색 필드 오버로드
		 */
		template <uint32 N>
		static bool drawSearchField( const utf8* pId, fixed_string<N>& str, const utf8* pHint = "Search...",
									 float32 width = 0.0f, bool bShowClear = true )
		{
			return drawSearchField( pId, str.data(), str.capacity(), pHint, width, bShowClear );
		}

		/**
		 * @brief 작은 형태의 색상이 들어간 태그(칩) 버튼을 그립니다.
		 */
		static void drawChip( const utf8* pLabel, const Color4& color );

		/**
		 * @brief 프로퍼티 항목의 라벨을 왼쪽에 표시하고, 오른쪽 편집 컨트롤을 배치할 준비를 합니다.
		 */
		static bool drawPropertyRowBegin( const utf8* pLabel, float32 labelWidth = 120.0f );

		/**
		 * @brief drawPropertyRowBegin() 이후에 호출하여 프로퍼티 행 그리기를 종료합니다.
		 */
		static void drawPropertyRowEnd();

		/**
		 * @brief 검색 텍스트 필터 및 클리어(X) 버튼을 포함하는 일체형 검색 위젯을 그립니다.
		 */
		static bool drawSearchFilter( const utf8* pId, string& filterText, float32 width = 0.0f );

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
		 * @brief 검색 결과 목록에서 Up/Down으로 선택을 움직이고 Enter면 true.
		 */
		static bool updateListSelection( int32& selectedIndex, int32 itemCount, bool bRepeat = true );

		/**
		 * @brief Save / Don't Save / Cancel 모달. 버튼이 눌리기 전에는 None입니다.
		 */
		static EditorUnsavedChoice drawUnsavedChangesModal( const utf8* pPopupId, const utf8* pMessage );
	};
} // namespace sw::editor
