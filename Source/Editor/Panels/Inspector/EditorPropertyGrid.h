/**
 * @file EditorPropertyGrid.h
 * @brief 리플렉션 객체의 프로퍼티, 메서드, 이벤트를 그리고 편집하는 그리드입니다. 인스펙터, 환경설정, 다중 선택이 함께 씁니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Panels/Inspector/InspectorBuiltinValue.h"

#include "Engine/Reflection/TextAddress.h"

namespace sw
{
    struct EnumInfo;
    struct FunctionInfo;
    struct IMapContainerWrapper;
    struct ISequenceContainerWrapper;
    struct PropertyInfo;
    struct TypeInfo;

    class Component;
    class GameObject;
    class TaskArgs;
} // namespace sw

namespace sw::editor
{
    class InspectorFavorites;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorPropertyGridTarget
     * @brief 그리드가 그리고 편집하는 대상 하나입니다. 리플렉션 인스턴스와 편집했을 때 알릴 대상, Undo 를 기록할 오브젝트를 담습니다.
     */
    struct EditorPropertyGridTarget
    {
        void*                                 _pInstance{ nullptr };
        const TypeInfo*                       _pType{ nullptr };
        Component*                            _pComponent{ nullptr }; ///< 컴포넌트이면 onPropertyChanged 를 받는다
        GameObject*                           _pObject{ nullptr };    ///< Undo 의 주인이다. 컴포넌트이면 그 owner 이며, 컴포넌트가 없으면 onPropertyChanged 도 받는다
        Delegate<void( const PropertyInfo& )> _onEdited;              ///< 씬 밖 객체(환경설정, 문서)의 변경을 받는 델리게이트. 비어 있으면 위 둘만 받는다
        Delegate<bool( const PropertyInfo& )> _isMixed;               ///< 다중 선택에서 값이 오브젝트마다 다르면 true 이고, 이름 앞에 "—" 를 그린다. 비어 있으면 혼합이 없다
    };
} // namespace sw::editor

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
         * @details Reset, Paste Value, 그리기 확장(`IInspectorProperty`)의 팝업 편집기가 함께 쓴다. 대상이 설정된 그리는 동안에만 호출한다.
         */
        void applyPropertyTextAsEdit( MutableObjectAddress instance, const PropertyInfo& prop, string_view text, const utf8* pUndoLabel );
        /**
         * @brief 즐겨찾기 집합을 연결합니다. 연결하면 프로퍼티 행의 우클릭 메뉴에 즐겨찾기 토글이 생기고 즐겨찾기 행 이름 앞에 별을 그립니다. nullptr 이면 끕니다.
         * @details 집합의 주인은 인스펙터 패널이다. 환경설정 창처럼 즐겨찾기가 없는 그리드는 연결하지 않는다. 토글하면 저장 파일에 바로 쓴다.
         */
        void setFavorites( InspectorFavorites* pFavorites ) { _pFavorites = pFavorites; }
        /**
         * @brief 대상의 프로퍼티 중 즐겨찾기한 것만 타입 이름 머리 아래에 그립니다. 그릴 것이 없으면 아무것도 그리지 않습니다.
         * @details 인스펙터 맨 위 Favorites 구역이 쓴다. 키는 대상 타입 이름과 프로퍼티 이름이다(`InspectorFavorites::makeKey`).
         */
        void drawFavoriteProperties( const EditorPropertyGridTarget& target );

        /**
         * @brief 현재 그리는 프로퍼티의 주인 오브젝트입니다. 되돌리기를 기록할 대상이며, 없으면 nullptr 입니다.
         * @details 타입 핸들 선택기가 편집 대상의 씬(`getManager()`)을 얻는 길이다. 전역의 활성 씬에서 찾으면 플레이 세션 · 프리팹 편집처럼 그리드 대상과
         *          활성 씬이 다를 때 다른 씬의 컴포넌트가 목록에 섞인다.
         */
        GameObject* getEditOwner() const;

    private:
        void beginTarget( const EditorPropertyGridTarget& target );
        /** @brief 프로퍼티 하나를 기본값으로 되돌립니다. 기본값은 @p defaultInstance 의 값이고, nullptr 이면 메타 텍스트입니다. 알림과 되돌리기 기록은 일반 편집과 같습니다. */
        void resetPropertyToDefault( MutableObjectAddress instance, const PropertyInfo& prop, ObjectAddress defaultInstance );
        void endTarget();
        /**
         * @brief 타입의 리플렉션 프로퍼티를 상속한 것까지 카테고리별로 그립니다(`InspectorPropertyLayout`). 그릴 프로퍼티가 있을 때만 @p pSectionTitle 구분선을 둡니다.
         * @param listDrawnName 인스펙터 확장이 이미 그린 프로퍼티 이름입니다. 다시 그리지 않습니다.
         */
        void drawTypeProperties( void* pInstance, const TypeInfo* pTypeInfo, const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName );
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
        /**
         * @brief 그리드가 값을 바꿨음을 편집 대상에 알립니다.
         * @details 위젯 대부분이 `getValuePtr<T>()` 로 멤버의 raw 포인터를 ImGui 에 넘기므로 리플렉션 `setValue<T>()` 의 알림 분기를 지나지 않는다.
         *          이 알림을 빠뜨리면 렌더 상태처럼 값이 바뀔 때 누군가 반응해야 하는 데이터가 조용히 어긋난다.
         */
        void notifyPropertyEdited( const PropertyInfo& prop );
        /** @brief 타입의 메서드(FUNCTION) 목록을 그립니다. */
        void drawTypeMethods( void* pInstance, const TypeInfo* pTypeInfo );
        /** @brief 타입의 이벤트(멀티캐스트 델리게이트 PROPERTY) 목록을 기반 클래스부터 그립니다. 인자 없는 이벤트는 그 자리에서 호출할 수 있습니다. */
        void drawTypeEvents( void* pInstance, const TypeInfo* pTypeInfo );
        /** @brief FUNCTION 을 호출하고 반환값을 "Last result" 행에 씁니다. 인자 없는 Run 버튼과 Invoke 버튼이 같은 경로를 씁니다. */
        void invokeTypeMethod( void* pInstance, const TypeInfo* pTypeInfo, const FunctionInfo& method, const TaskArgs& args );

        fixed_string<constant::kMaxBuffer64>                         _propertyFilter;   ///< 프로퍼티 검색 필터
        unordered_map<uint64, vector<InspectorMethodArgSlot>>        _mapMethodArgSlot; ///< FUNCTION() 인자 슬롯. 키는 타입과 메서드 이름의 해시
        fixed_string<constant::kMaxBuffer256>                        _lastInvokeResult;
        unordered_map<uint32, fixed_string<constant::kMaxBuffer256>> _mapContainerAddText;  ///< 컨테이너 "추가" 입력란의 텍스트. 키는 그 입력란의 ImGui id
        Delegate<void( const PropertyInfo& )>                        _onEdited;             ///< 지금 대상의 `_onEdited`
        Delegate<bool( const PropertyInfo& )>                        _isMixed;              ///< 지금 대상의 `_isMixed`
        Component*                                                   _pEditTargetComponent; ///< 현재 프로퍼티를 그리는 컴포넌트. 편집 알림을 받는다
        GameObject*                                                  _pEditTargetObject;    ///< 현재 프로퍼티를 그리는 GameObject. 컴포넌트가 없을 때만 쓴다
        uint32                                                       _propertyDrawDepth;    ///< 중첩과 컨테이너의 재귀 깊이. 알림은 가장 바깥에서 한 번만 보낸다
        InspectorFavorites*                                          _pFavorites;           ///< 즐겨찾기 집합. nullptr 이면 즐겨찾기를 쓰지 않는다
        int32                                                        _openAllRequest;       ///< 이번 프레임의 모두 펼치기(1) · 접기(-1) 요청. 0 이면 없다
        bool                                                         _bFavoritesOnly;       ///< 다음 `drawTypeProperties` 한 번만 즐겨찾기 프로퍼티로 거른다
        bool                                                         _bShowModifiedOnly;    ///< 기본값과 다른 프로퍼티만 그린다(언리얼 Show Only Modified Properties)
    };
} // namespace sw::editor
