/**
 * @file EditorPropertyGrid.h
 * @brief 리플렉션 객체의 프로퍼티, 메서드, 이벤트를 그리고 편집하는 그리드입니다. 인스펙터, 환경설정, 다중 선택이 함께 씁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Panels/Inspector/Model/InspectorBuiltinValue.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentPicker.h"
#include "Editor/Panels/Inspector/Model/PropertyGridModel.h"
#include "Editor/Panels/Inspector/Model/PropertyGridViewState.h"

namespace sw
{
    struct FunctionInfo;
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorPropertyGrid
     * @brief 리플렉션 객체의 프로퍼티를 그리고 편집합니다. 상속한 프로퍼티, 카테고리, EditCondition, 컨테이너, enum, 중첩 구조체를 지원합니다.
     * @details 언리얼의 `IDetailsView` 에 해당한다. 대상 하나마다 모델이 값 행을 모으고(`PropertyGridModel::gather`), 그리드는 행을 그려 편집을 행 번호로
     *          모델 명령에 돌려준다 — 멤버 주소를 보지 않는다. 화면 밖 행은 높이만 차지한다(값을 채우지 않는다). 검색 입력란은 그리드가 가지고 있다.
     */
    class SW_EDITOR_API EditorPropertyGrid
    {
    public:
        EditorPropertyGrid();
        EditorPropertyGrid( const EditorPropertyGrid& )            = delete;
        EditorPropertyGrid& operator=( const EditorPropertyGrid& ) = delete;

        /**
         * @brief 그리드 위에 검색 입력란을 한 줄로 그립니다.
         * @param pMarkKey 값을 주면 지우기 버튼 없이 그리고 입력란에 이 이름을 남깁니다. 시나리오가 이 입력란을 누를 때 씁니다.
         */
        void drawSearchBar( const utf8* pMarkKey = nullptr );
        /**
         * @brief 검색 입력란과 그 오른쪽의 보기 옵션 버튼을 한 줄로 그립니다. 옵션은 바뀐 프로퍼티만 보기, 모두 펼치기, 모두 접기입니다.
         * @details 언리얼 Details 의 View Options(Show Only Modified Properties · Expand All · Collapse All)에 해당한다.
         *          펼치기 · 접기 요청은 이번 프레임에 그리는 모든 카테고리 머리에 적용하고 `endFrame` 이 지운다.
         */
        void drawSearchBarWithOptions( const utf8* pFieldTooltip = nullptr );
        /** @brief 이번 프레임의 펼치기(1) · 접기(-1) 요청을 반환합니다. 0 이면 요청이 없습니다. 컴포넌트 카드처럼 그리드 밖 머리에도 적용할 때 씁니다. */
        int32 getOpenAllRequest() const { return _openAllRequest; }
        /** @brief 프레임 끝에 부릅니다. 펼치기 · 접기 요청을 지웁니다. */
        void endFrame() { _openAllRequest = 0; }
        /** @brief 현재 검색어를 반환합니다. 비어 있으면 빈 문자열입니다. 환경설정 창이 섹션을 거를 때 씁니다. */
        const utf8* getFilterText() const { return _propertyFilter.c_str(); }
        /**
         * @brief 대상의 프로퍼티를 그립니다. @p listDrawnName 은 이미 다른 곳(인스펙터 확장)이 그린 이름이므로 다시 그리지 않습니다.
         * @param pSectionTitle 그릴 프로퍼티가 있을 때만 위에 두는 구분선의 제목입니다. nullptr 이면 구분선이 없습니다.
         */
        void drawProperties( const EditorPropertyGridTarget& target, const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName );
        /** @brief 대상 타입의 메서드(FUNCTION)를 그립니다. @p bEvents 가 true 이면 이벤트도 그립니다. 인스펙터의 Methods 영역입니다. */
        void drawMethodsAndEvents( const EditorPropertyGridTarget& target, bool bEvents );
        /**
         * @brief 즐겨찾기 집합을 연결합니다. 연결하면 프로퍼티 행의 우클릭 메뉴에 즐겨찾기 토글이 생기고 즐겨찾기 행 이름 앞에 별을 그립니다. nullptr 이면 끕니다.
         * @details 집합의 주인은 인스펙터 패널이다. 환경설정 창처럼 즐겨찾기가 없는 그리드는 연결하지 않는다. 토글하면 저장 파일에 바로 쓴다.
         */
        void setFavorites( InspectorFavorites* pFavorites ) { _model.setFavorites( pFavorites ); }
        /**
         * @brief 대상의 프로퍼티 중 즐겨찾기한 것만 타입 이름 머리 아래에 그립니다. 그릴 것이 없으면 아무것도 그리지 않습니다.
         * @details 인스펙터 맨 위 Favorites 구역이 쓴다. 키는 대상 타입 이름과 프로퍼티 이름이다(`InspectorFavorites::makeKey`).
         */
        void drawFavoriteProperties( const EditorPropertyGridTarget& target );

        /** @brief 배치 묶음을 새로 지은 횟수입니다(프로브 `Editor.InspectorLayoutRebuildCount` — 선택 · 검색 글이 그대로면 늘지 않는다). */
        uint32 getLayoutRebuildCount() const { return _model.getLayoutRebuildCount(); }
        /** @brief 마지막으로 그린 프레임의 메서드 행 수입니다(프로브 `Editor.InspectorMethodRows`). */
        uint32 getMethodRowCount() const { return _methodRowCount; }
        /** @brief 마지막으로 그린 프레임에 `CallInEditor` 가 없어 숨긴 메서드 수입니다(프로브 `Editor.InspectorHiddenMethods`). 진단 보기를 켜면 0 입니다. */
        uint32 getHiddenMethodCount() const { return _hiddenMethodCount; }
        /** @brief 마지막으로 그린 프레임에 값을 채운 프로퍼티 행 수입니다(프로브 `Editor.InspectorFilledRows` — 화면 밖 행은 세지 않는다). */
        uint32 getFilledRowCount() const { return _filledRowCount; }
        /** @brief 마지막으로 그린 프레임에 화면 밖이라 높이만 둔 깊이 0 행 수입니다(프로브 `Editor.InspectorSkippedRows`). */
        uint32 getSkippedRowCount() const { return _skippedRowCount; }

    private:
        /** @brief 행 하나가 연 범위입니다(ImGui id · 막기 · 트리 노드). 다음 행의 깊이가 이 깊이 이하이면 닫는다. */
        struct OpenRowScope
        {
            uint32 _rowID{ 0 };
            uint16 _depth{ 0 };
            uint8  _bTreeOpen{ 0 }; ///< 트리 노드를 열었다(`TreePop` 을 부른다)
        };

        /** @brief ImGui 프레임이 바뀌었으면 프레임마다 세는 수(메서드 · 채운 행 · 건너뛴 행)를 0 으로 되돌립니다. */
        void resetFrameCounts();
        /** @brief 이번 프레임의 화면 · 행 높이로 gather 가 받는 그리기 영역을 만듭니다. */
        static PropertyGridRowWindow makeRowWindow();
        /** @brief 묶은 대상의 값 행을 모아 그립니다. 그릴 프로퍼티가 있을 때만 @p pSectionTitle 구분선을 둡니다. */
        void drawTargetProperties( const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName );
        /** @brief `_viewState` 의 행을 위에서 아래로 그립니다 — 카테고리마다 표 하나, 자식 행은 부모 칸 안에. */
        void drawRows();
        /** @brief 카테고리 머리를 그리고 펴져 있으면 표를 엽니다. 표를 열었으면 true 입니다. */
        bool drawCategoryRow( const PropertyValueRow& row );
        /** @brief 화면 밖 행 묶음(`Skipped`, 행 @p firstIndex 부터)을 높이만 차지하는 표 행 하나로 둡니다. 다음 행 자리를 돌려줍니다. */
        uint32 drawSkippedRows( uint32 firstIndex );
        /** @brief 표의 새 행을 엽니다. 앞 깊이 0 행의 높이를 잰다(행 시작에서 행 시작까지). */
        void beginTableRow( float32 minHeight );
        /** @brief 열린 표를 닫습니다. 마지막 깊이 0 행의 높이를 잰다. */
        void endTable();
        /** @brief 깊이 0 행의 이름 칸(혼합 표시 · 즐겨찾기 별 · 이름 · 행 메뉴 · 되돌리기 단추)을 그립니다. */
        void drawRowNameColumn( const PropertyValueRow& row );
        /** @brief 이름 글을 오른쪽 클릭하면 열리는 행 메뉴(즐겨찾기 · 기본값 · 복사 · 붙여넣기)입니다. */
        void drawPropertyRowMenu( const PropertyValueRow& row );
        /** @brief 행 하나의 범위를 열고(ImGui id · 막기) 자식 행 앞머리와 값 위젯을 그립니다. 노드가 펴져 있으면 범위가 자식 행까지 이어진다. */
        void openRowScope( const PropertyValueRow& row );
        /** @brief 범위 하나를 닫습니다 — 컨테이너의 "추가" 입력란, `TreePop`, 막기 끝, 깊이 0 행의 값 표식. */
        void closeRowScope();
        /** @brief 값 위젯을 그립니다. 그리기 확장이 먼저 그리고, 맡지 않으면 행 종류대로 그립니다. 펴진 노드면 true 입니다(범위가 자식까지 이어진다). */
        bool drawRowWidget( const PropertyValueRow& row );
        /** @brief 노드(구조체 · 컨테이너) 머리입니다. 펴졌으면 true 이고, 상태가 바뀌었으면 모델에 돌려준다. */
        bool drawNodeHeader( const PropertyValueRow& row );
        /** @brief 열거형 값 행의 콤보입니다. 고른 값은 모델 명령으로 돌려준다. */
        void drawEnumRow( const PropertyValueRow& row );
        /** @brief 원소가 곧 키인 시퀀스(`set`)의 원소 하나입니다(지우기 단추 · 글 입력란 — Enter 로 고치면 지우고 다시 넣는다). */
        void drawKeyedElementRow( const PropertyValueRow& row );
        /** @brief 컨테이너 끝의 "추가" 텍스트 입력란입니다. Enter 로 비어 있지 않은 값을 입력했으면 true 를 반환하고, @p outText 에 담은 뒤 입력란을 비웁니다. */
        bool drawContainerAddRow( const utf8* pHint, string& outText );
        /** @brief 이번 프레임에 미룬 편집(컨테이너 더하기 · 지우기 · 바꾸기, 행 메뉴의 붙여넣기 · 기본값)을 모델에 부릅니다. 행을 다 그린 뒤 — 행 표의 원소 자리가 그리는 동안 살아 있게 한다. */
        void applyPendingRowEdit();
        /** @brief 타입의 메서드(FUNCTION) 목록을 그립니다. */
        void drawTypeMethods( const TypeInfo* pTypeInfo );
        /**
         * @brief 메서드 하나를 그립니다. 인자 없는 CallInEditor 는 "Run" 단추 하나, 나머지는 머리 · 인자 입력란 · "Invoke" 단추입니다. 생성자는 건너뜁니다.
         * @details 기본 목록은 `CallInEditor` 만이다(언리얼 Details 의 CallInEditor 단추와 같다). 나머지는 보기 옵션의 진단 보기(`_bShowAllMethods`)에서만 그린다.
         */
        void drawTypeMethod( const TypeInfo* pTypeInfo, const FunctionInfo& method );
        /** @brief 이 메서드를 목록에 그리면 true 입니다 — `CallInEditor` 이거나 진단 보기가 켜져 있을 때입니다. */
        [[nodiscard]] bool isMethodListed( const FunctionInfo& method ) const;
        /** @brief 메서드의 인자 입력란을 인자 슬롯마다 그립니다. */
        void drawMethodArgInputs( const FunctionInfo& method, vector<InspectorMethodArgSlot>& inoutListSlot );
        /** @brief 타입의 이벤트(멀티캐스트 델리게이트 PROPERTY) 목록을 기반 클래스부터 그립니다. 인자 없는 이벤트는 즉시 호출할 수 있습니다. */
        void drawTypeEvents( const TypeInfo* pTypeInfo );

    private:
        /** @brief 행을 다 그린 뒤 부르는 편집 하나입니다(한 프레임에 누름은 하나다). 구조를 바꾸는 편집이라 뒤의 자식 행이 옛 자리를 그리지 않게 미룬다. */
        enum class PendingRowEdit : uint8
        {
            None,
            PasteText,
            ResetToDefault,
            AddElement,
            ClearElements,
            RemoveMapEntry,
            AddMapEntry,
            ReplaceKeyedElement,
            RemoveKeyedElement,
            AddKeyedElement,
        };

        fixed_string<constant::buffer::kMax64>                         _propertyFilter;      ///< 프로퍼티 검색 필터
        unordered_map<string, vector<InspectorMethodArgSlot>>          _mapMethodArgSlot;    ///< FUNCTION() 인자 슬롯. 키는 타입과 메서드 이름 원문
        unordered_map<uint32, fixed_string<constant::buffer::kMax256>> _mapContainerAddText; ///< 컨테이너 "추가" 입력란의 텍스트. 키는 그 입력란의 ImGui id
        vector<InspectorComponentChoice>                               _listMethodArgChoice; ///< 메서드 인자 핸들 선택기의 선택지 — 콤보가 열린 프레임에만 채운다
        PropertyGridModel                                              _model;               ///< 편집 모델 — 대상 · 값 행 · 배치 캐시 · 즐겨찾기 · 명령
        PropertyGridViewState                                          _viewState;           ///< 그리는 대상의 값 행(저장 공간을 다시 쓴다)
        vector<OpenRowScope>                                           _listOpenScope;       ///< 지금 열린 행 범위(바깥에서 안쪽 순서)
        string                                                         _pendingEditText;     ///< 미룬 편집의 글(붙여넣기 · 키 · 원소)
        PendingRowEdit                                                 _pendingEdit;         ///< 미룬 편집
        uint32                                                         _pendingEditRowID;    ///< 미룬 편집의 행(컨테이너 편집은 컨테이너 행)
        uint32                                                         _pendingEditIndex;    ///< 미룬 편집의 원소 · 항목 자리
        uint32                                                         _measureRowID;        ///< 높이를 재는 중인 깊이 0 행. `constant::invalid_index::kUint32` 면 없다
        float32                                                        _measureStartY;       ///< 그 행이 시작한 높이(칸 여백 안)
        int32                                                          _openAllRequest;      ///< 이번 프레임의 모두 펼치기(1) · 접기(-1) 요청. 0 이면 없다
        uint8                                                          _bFavoritesOnly  : 1; ///< 다음 `drawTargetProperties` 한 번만 즐겨찾기 프로퍼티로 거른다
        uint8                                                          _bShowAllMethods : 1; ///< 진단 보기: `CallInEditor` 가 없는 메서드도 그린다(보기 옵션). 기본은 꺼져 있다
        uint8                                                          _bTableOpen      : 1; ///< 카테고리 표가 열려 있다
        bool                                                           _bShowModifiedOnly;   ///< 기본값과 다른 프로퍼티만 그린다(언리얼 Show Only Modified Properties) NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
        int32                                                          _countFrame;          ///< 아래 수를 센 ImGui 프레임 번호. 프레임이 바뀌면 처음 그릴 때 다시 센다
        uint32                                                         _methodRowCount;      ///< 그 프레임에 그린 메서드 행 수
        uint32                                                         _hiddenMethodCount;   ///< 그 프레임에 숨긴(`CallInEditor` 없는) 메서드 수
        uint32                                                         _filledRowCount;      ///< 그 프레임에 값을 채운 행 수
        uint32                                                         _skippedRowCount;     ///< 그 프레임에 화면 밖이라 높이만 둔 깊이 0 행 수
    };
} // namespace sw::editor
