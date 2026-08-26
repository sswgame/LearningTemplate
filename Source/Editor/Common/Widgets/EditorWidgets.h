/**
 * @file EditorWidgets.h
 * @brief 에디터 윈도우용 공유 ImGui 위젯
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

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
	} // namespace style

	// ------------------------------------------------------------------------------
	// 2) 공유 위젯 — Vec3 / 컴포넌트 카드 / 헤더 / 칩 / 프로퍼티 행
	//    begin* 가 true면 반드시 짝이 되는 end* 를 호출
	// ------------------------------------------------------------------------------
	/**
	 * @brief 레이블, RGB 축 버튼(리셋 기능 포함), 3개의 DragFloat 컨트롤을 가로로 배치합니다.
	 * @param pLabel UI에 표시될 라벨 텍스트
	 * @param values 편집할 float3 벡터 참조
	 * @param resetValue 축 버튼을 클릭했을 때 초기화될 값
	 * @param columnWidth 라벨 컬럼의 너비
	 * @param speed DragFloat의 드래그 속도
	 * @return 값이 편집(변경)되었으면 true를 반환합니다.
	 */
	bool drawVec3Control( const utf8* pLabel, float3& values, float32 resetValue = 0.0f, float32 columnWidth = 100.0f,
						  float32 speed = 0.1f );

	/**
	 * @brief 컴포넌트 전용 접이식 카드 UI를 렌더링합니다. (패널 형태)
	 * @param pName 카드 헤더에 표시될 컴포넌트 이름
	 * @param id 카드를 식별하기 위한 고유 ID
	 * @param pBActive 컴포넌트 활성화 여부 (체크박스와 연동), nullptr이면 표시하지 않음
	 * @param pBRemoveRequested 컴포넌트 삭제 요청 상태 반환용, nullptr이면 삭제 메뉴 비활성
	 * @param bAccent 카드 헤더 색상에 강조(Accent) 색상을 사용할지 여부
	 * @return 카드가 펼쳐져서 내부를 그려야 하면 true를 반환합니다. true 반환 시 반드시 endComponentCard()를 호출해야 합니다.
	 */
	bool beginComponentCard( const utf8* pName, uint64 id, bool* pBActive, bool* pBRemoveRequested, bool bAccent = false );

	/**
	 * @brief beginComponentCard()가 true를 반환했을 때, 내용 렌더링 후 호출하여 카드를 닫습니다.
	 */
	void endComponentCard();

	/**
	 * @brief 섹션 상단에 굵은 제목과 선택적인 부제목, 구분선을 그립니다.
	 * @param pTitle 섹션 제목
	 * @param pSubtitle 섹션 부제목 (nullptr 가능)
	 */
	void drawSectionHeader( const utf8* pTitle, const utf8* pSubtitle = nullptr );

	/** @brief 툴바 컨트롤 사이 `|` 구분. SameLine 뒤에 이어서 그립니다. */
	void drawToolbarSeparator();

	/** @brief 비활성 안내 문구. */
	void drawEmptyHint( const utf8* pText );

	/**
	 * @brief C 버퍼 검색 필드. width 0이면 가용 너비, 음수면 한 줄 전체.
	 * @return 텍스트가 바뀌면 true
	 */
	bool drawSearchField( const utf8* pId, utf8* pBuffer, uint32 bufferBytes, const utf8* pHint = "Search...",
						  float32 width = 0.0f, bool bShowClear = true );

	/**
	 * @brief 작은 형태의 색상이 들어간 태그(칩) 버튼을 그립니다. 클릭 가능한 요소로 사용할 수 있습니다.
	 * @param pLabel 칩 내부에 표시될 텍스트
	 * @param color 칩의 배경 색상
	 */
	void drawChip( const utf8* pLabel, const Color4& color );

	/**
	 * @brief 프로퍼티 항목의 라벨을 왼쪽에 표시하고, 오른쪽 편집 컨트롤을 배치할 준비를 합니다.
	 * @param pLabel 프로퍼티 라벨 텍스트
	 * @param labelWidth 라벨 영역의 너비
	 * @return 항상 true를 반환합니다.
	 */
	bool drawPropertyRowBegin( const utf8* pLabel, float32 labelWidth = 120.0f );

	/**
	 * @brief drawPropertyRowBegin() 이후에 호출하여 프로퍼티 행 그리기를 종료합니다.
	 */
	void drawPropertyRowEnd();

	/**
	 * @brief 검색 텍스트 필터 및 클리어(X) 버튼을 포함하는 일체형 검색 위젯을 그립니다.
	 * @param pId ImGui ID
	 * @param filterText 검색어 버퍼 (수정됨)
	 * @param width 검색바 너비 (0.0f면 가용 너비 전체 사용)
	 * @return 검색 텍스트가 변경되었으면 true 반환
	 */
	bool drawSearchFilter( const utf8* pId, string& filterText, float32 width = 0.0f );

	/**
	 * @brief 애셋 경로 표시, 드래그앤드롭 수신(Payload), 클리어/찾아보기 버튼을 지원하는 애셋 슬롯을 그립니다.
	 * @param pLabel 라벨 텍스트
	 * @param assetPath 애셋 경로 버퍼 (수정됨)
	 * @param pExpectedExt 요구 확장자 (예: ".png", ".prefab", nullptr이면 모든 파일)
	 * @param labelWidth 라벨 영역 너비
	 * @return 경로가 변경되었으면 true 반환
	 */
	bool drawAssetSlot( const utf8* pLabel, string& assetPath, const utf8* pExpectedExt = nullptr,
						float32 labelWidth = 120.0f );

	/**
	 * @brief 패널 사이의 영역 크기를 조절할 수 있는 스플리터를 그립니다.
	 * @param pId 스플리터 고유 ID
	 * @param bVertical 세로 분할 여부 (true: 좌우 분할, false: 상하 분할)
	 * @param thickness 스플리터 두께 (픽셀)
	 * @param pSize1 첫 번째 영역 크기 (포인터)
	 * @param pSize2 두 번째 영역 크기 (포인터)
	 * @param minSize1 첫 번째 영역 최소 크기
	 * @param minSize2 두 번째 영역 최소 크기
	 * @return 스플리터 드래그로 크기가 변경되었으면 true 반환
	 */
	bool drawSplitter( const utf8* pId, bool bVertical, float32 thickness, float32* pSize1, float32* pSize2,
					   float32 minSize1 = 50.0f, float32 minSize2 = 50.0f );

	/**
	 * @brief Color4 색상을 편집할 수 있는 일관된 색상 편집기 위젯을 그립니다.
	 * @param pLabel 라벨 텍스트
	 * @param color 편집 대상 Color4
	 * @param labelWidth 라벨 너비
	 * @return 색상이 변경되었으면 true 반환
	 */
	bool drawColorEdit( const utf8* pLabel, Color4& color, float32 labelWidth = 120.0f );

	// ------------------------------------------------------------------------------
	// 3) 인스펙터 ImGui 스타일 스택
	// ------------------------------------------------------------------------------
	/**
	 * @brief 인스펙터 패널 등에서 사용할 공통 ImGui 스타일(라운딩, 패딩 등)을 스택에 푸시합니다.
	 */
	void pushInspectorStyle();

	/**
	 * @brief pushInspectorStyle()로 적용한 스타일을 스택에서 팝하여 원래대로 되돌립니다.
	 */
	void popInspectorStyle();
} // namespace sw::editor
