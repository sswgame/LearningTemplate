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
#include "Editor/Panels/Inspector/Model/PropertyGridModel.h"

#include "Engine/Reflection/TextAddress.h"

namespace sw
{
    struct EnumInfo;
    struct FunctionInfo;
    struct IMapContainerWrapper;
    struct ISequenceContainerWrapper;
    struct PropertyInfo;
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorPropertyGrid
     * @brief 리플렉션 객체의 프로퍼티를 그리고 편집합니다. 상속한 프로퍼티, 카테고리, EditCondition, 컨테이너, enum, 중첩 구조체를 지원합니다.
     * @details 언리얼의 `IDetailsView` 에 해당한다. 값을 고치면 대상에 알리고(컴포넌트와 오브젝트는 onPropertyChanged, 그 밖은 `_onEdited`),
     *          씬 오브젝트이면 Undo 를 기록한다(`InspectorPropertyUndo`). 검색 입력란은 그리드가 가지고 있다.
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
         * @brief 프로퍼티 하나에 텍스트 값(`SerializerUtil::formatPropertyText` 형식)을 적용하고 편집으로 기록합니다. 변경을 알리고, 주인 오브젝트에 되돌리기 항목을 하나 남깁니다.
         * @details 편집 모델의 한 길(`PropertyGridModel::setPropertyText`)로 간다. 대상이 묶인 그리는 동안에만 호출한다.
         */
        void applyPropertyTextAsEdit( MutableObjectAddress instance, const PropertyInfo& prop, string_view text, const utf8* pUndoLabel ) { _model.setPropertyText( instance, prop, text, pUndoLabel ); }
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

        /** @brief 이 그리드의 편집 모델입니다(대상 · 배치 캐시 · 명령). 그리기 확장이 값을 바꾸거나 표시 단위를 읽는 길입니다. */
        PropertyGridModel& getModel() { return _model; }
        /** @brief 묶은 대상의 주인 오브젝트가 사는 씬의 오브젝트 관리자입니다(`PropertyGridModel::findEditOwnerManager`). */
        const GameObjectManager* findEditOwnerManager() const { return _model.findEditOwnerManager(); }
        /** @brief @p prop 의 표시 단위입니다(`PropertyGridModel::getDisplayUnit`). */
        const InspectorDisplayUnit& getDisplayUnit( const PropertyInfo& prop ) { return _model.getDisplayUnit( prop ); }
        /** @brief @p pNumberFormat 에 @p prop 의 단위를 붙인 printf 서식입니다(`PropertyGridModel::getUnitNumberFormat`). */
        const utf8* getUnitNumberFormat( const PropertyInfo& prop, const utf8* pNumberFormat ) { return _model.getUnitNumberFormat( prop, pNumberFormat ); }
        /** @brief 배치 묶음을 새로 지은 횟수입니다(프로브 `Editor.InspectorLayoutRebuildCount` — 선택 · 검색 글이 그대로면 늘지 않는다). */
        uint32 getLayoutRebuildCount() const { return _model.getLayoutRebuildCount(); }
        /** @brief 마지막으로 그린 프레임의 메서드 행 수입니다(프로브 `Editor.InspectorMethodRows`). */
        uint32 getMethodRowCount() const { return _methodRowCount; }
        /** @brief 마지막으로 그린 프레임에 `CallInEditor` 가 없어 숨긴 메서드 수입니다(프로브 `Editor.InspectorHiddenMethods`). 진단 보기를 켜면 0 입니다. */
        uint32 getHiddenMethodCount() const { return _hiddenMethodCount; }

    private:
        /**
         * @brief 타입의 리플렉션 프로퍼티를 상속한 것까지 카테고리별로 그립니다(`InspectorPropertyLayout`). 그릴 프로퍼티가 있을 때만 @p pSectionTitle 구분선을 둡니다.
         * @param listDrawnName 인스펙터 확장이 이미 그린 프로퍼티 이름입니다. 다시 그리지 않습니다.
         */
        void drawTypeProperties( void* pInstance, const TypeInfo* pTypeInfo, const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName );
        /** @brief 속성 테이블의 한 행을 그립니다 — 이름 필드(혼합 표시 · 즐겨찾기 별 · 이름 · 행 메뉴 · 되돌리기 버튼)과 값 필드(위젯). @p bDisabled 면 값 위젯을 막습니다. */
        void drawPropertyRow( MutableObjectAddress instance, const TypeInfo& typeInfo, const PropertyInfo& prop, bool bDisabled, bool bFavoriteTarget, ObjectAddress defaultInstance ); /** @brief 이름 글을 오른쪽 클릭하면 열리는 행 메뉴(즐겨찾기 · 기본값 · 복사 · 붙여넣기)입니다. */
        void drawPropertyRowMenu( MutableObjectAddress instance, const PropertyInfo& prop, const string& favoriteKey, bool bFavoriteTarget, bool bFavorite, ObjectAddress defaultInstance );
        /**
         * @brief 단일 프로퍼티 위젯을 그립니다. 값이 바뀌면 편집 대상에 통지합니다.
         * @details @p instance 는 @p prop 을 가진 객체다. 컨테이너 원소 · 맵 값은 오프셋 0 인 프로퍼티(`makeElementProperty`)와 그 값 주소를 객체로 넘긴다.
         */
        void drawPropertyWidget( MutableObjectAddress instance, const PropertyInfo& prop );
        /** @brief 위젯 본문을 그립니다. 변경 알림 판정은 이 함수를 감싸는 drawPropertyWidget 이 합니다. */
        void drawPropertyWidgetBody( MutableObjectAddress instance, const PropertyInfo& prop );
        /** @brief 열거형 프로퍼티 위젯을 그립니다. */
        void drawEnumProperty( MutableObjectAddress instance, const PropertyInfo& prop, const EnumInfo& enumInfo, bool bReadOnly );
        /** @brief 컨테이너 프로퍼티 위젯을 그립니다. */
        void drawContainerProperty( MutableObjectAddress instance, const PropertyInfo& prop, bool bReadOnly );
        /**
         * @brief 맵 프로퍼티를 그립니다. 키는 문자열이며 정렬 키나 해시 키이므로 제자리에서 고치지 않습니다. 값은 타입별 위젯으로 제자리에서 편집하고, 항목을 지우거나 추가할 수 있습니다.
         * @details 추가는 키 문자열을 Enter 로 받는다. 같은 키가 이미 있으면 값을 덮어쓰지 않고 추가하지 않는다.
         */
        void drawMapContainer( MutableValueAddress container, const PropertyInfo& prop, IMapContainerWrapper& mapWrapper, bool bReadOnly );
        /** @brief 원소가 곧 키인 시퀀스(`set`)를 그립니다. 원소는 텍스트 입력란이며, Enter 로 고치면 지우고 다시 넣습니다(`replaceElement`). */
        void drawKeyedSequenceContainer( MutableValueAddress container, const PropertyInfo& prop, ISequenceContainerWrapper& sequence, bool bReadOnly );
        /** @brief 컨테이너 끝의 "추가" 텍스트 입력란입니다. Enter 로 비어 있지 않은 값을 입력했으면 true 를 반환하고, @p outText 에 담은 뒤 입력란을 비웁니다. */
        bool drawContainerAddRow( const utf8* pHint, string& outText );
        /** @brief 구조체와 문자열 프로퍼티 위젯을 그립니다. */
        void drawStructOrStringProperty( MutableObjectAddress instance, const PropertyInfo& prop, const TypeInfo* pFieldType );
        /** @brief 타입의 메서드(FUNCTION) 목록을 그립니다. */
        void drawTypeMethods( void* pInstance, const TypeInfo* pTypeInfo );
        /**
         * @brief 메서드 하나를 그립니다. 인자 없는 CallInEditor 는 "Run" 단추 하나, 나머지는 머리 · 인자 입력란 · "Invoke" 단추입니다. 생성자는 건너뜁니다.
         * @details 기본 목록은 `CallInEditor` 만이다(언리얼 Details 의 CallInEditor 단추와 같다). 나머지는 보기 옵션의 진단 보기(`_bShowAllMethods`)에서만 그린다.
         */
        void drawTypeMethod( void* pInstance, const TypeInfo* pTypeInfo, const FunctionInfo& method );
        /** @brief 이 메서드를 목록에 그리면 true 입니다 — `CallInEditor` 이거나 진단 보기가 켜져 있을 때입니다. */
        [[nodiscard]] bool isMethodListed( const FunctionInfo& method ) const;
        /** @brief 메서드의 인자 입력란을 인자 슬롯마다 그립니다. */
        void drawMethodArgInputs( const FunctionInfo& method, vector<InspectorMethodArgSlot>& inoutListSlot );
        /** @brief 타입의 이벤트(멀티캐스트 델리게이트 PROPERTY) 목록을 기반 클래스부터 그립니다. 인자 없는 이벤트는 즉시 호출할 수 있습니다. */
        void drawTypeEvents( void* pInstance, const TypeInfo* pTypeInfo );

    private:
        fixed_string<constant::buffer::kMax64>                         _propertyFilter;      ///< 프로퍼티 검색 필터
        unordered_map<string, vector<InspectorMethodArgSlot>>          _mapMethodArgSlot;    ///< FUNCTION() 인자 슬롯. 키는 타입과 메서드 이름 원문
        unordered_map<uint32, fixed_string<constant::buffer::kMax256>> _mapContainerAddText; ///< 컨테이너 "추가" 입력란의 텍스트. 키는 그 입력란의 ImGui id
        PropertyGridModel                                              _model;               ///< 편집 모델 — 대상 · 배치 캐시 · 즐겨찾기 · 명령
        uint32                                                         _propertyDrawDepth;   ///< 중첩과 컨테이너의 재귀 깊이. 알림은 가장 바깥에서 한 번만 보낸다
        int32                                                          _openAllRequest;      ///< 이번 프레임의 모두 펼치기(1) · 접기(-1) 요청. 0 이면 없다
        uint8                                                          _bFavoritesOnly  : 1; ///< 다음 `drawTypeProperties` 한 번만 즐겨찾기 프로퍼티로 거른다
        uint8                                                          _bShowAllMethods : 1; ///< 진단 보기: `CallInEditor` 가 없는 메서드도 그린다(보기 옵션). 기본은 꺼져 있다
        bool                                                           _bShowModifiedOnly;   ///< 기본값과 다른 프로퍼티만 그린다(언리얼 Show Only Modified Properties) NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
        uint32                                                         _layoutUseDepth;      ///< 돌려받은 묶음을 도는 `drawTypeProperties` 의 겹 수. 0 일 때만 캐시가 묶음을 비울 수 있다
        int32                                                          _methodCountFrame;    ///< 아래 두 수를 센 ImGui 프레임 번호. 프레임이 바뀌면 처음 그릴 때 다시 센다
        uint32                                                         _methodRowCount;      ///< 그 프레임에 그린 메서드 행 수
        uint32                                                         _hiddenMethodCount;   ///< 그 프레임에 숨긴(`CallInEditor` 없는) 메서드 수
    };
} // namespace sw::editor
