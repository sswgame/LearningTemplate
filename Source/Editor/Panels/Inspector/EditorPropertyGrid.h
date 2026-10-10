/**
 * @file EditorPropertyGrid.h
 * @brief 리플렉션 객체의 프로퍼티 · 메서드 · 이벤트를 그리고 고치는 그리드입니다(인스펙터 · 환경설정 · 다중 선택이 같이 씁니다).
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
    /**
     * @struct EditorPropertyGridTarget
     * @brief 그리드가 그리고 고치는 대상 하나입니다. 리플렉션 인스턴스와, 고쳤을 때 알릴 곳 · Undo 를 남길 오브젝트를 담습니다.
     */
    struct EditorPropertyGridTarget
    {
        void*                                 _pInstance{ nullptr };
        const TypeInfo*                       _pType{ nullptr };
        Component*                            _pComponent{ nullptr }; ///< 컴포넌트면 onPropertyChanged 를 받는다
        GameObject*                           _pObject{ nullptr };    ///< Undo 의 주인(컴포넌트면 그 owner). 컴포넌트가 없으면 onPropertyChanged 도 받는다
        Delegate<void( const PropertyInfo& )> _onEdited;              ///< 씬 밖 객체(환경설정 · 문서)가 바뀜을 받는 곳. 비어 있으면 위 둘만
        Delegate<bool( const PropertyInfo& )> _isMixed;               ///< 다중 선택에서 값이 오브젝트마다 다르면 true — 이름 앞에 "—" 를 그린다. 비어 있으면 혼합 없음
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorPropertyGrid
     * @brief 리플렉션 객체의 프로퍼티(상속분 · 카테고리 · EditCondition · 컨테이너 · enum · 중첩 구조체)를 그리고 고칩니다.
     * @details 언리얼 `IDetailsView` 에 해당합니다. 값을 고치면 대상에 알리고(컴포넌트 · 오브젝트의 onPropertyChanged, 그 밖은 `_onEdited`),
     *          씬 오브젝트면 Undo 를 남깁니다(`InspectorPropertyUndo`). 검색 칸은 그리드가 가지고 있습니다.
     */
    class SW_EDITOR_API EditorPropertyGrid
    {
    public:
        EditorPropertyGrid();
        EditorPropertyGrid( const EditorPropertyGrid& )            = delete;
        EditorPropertyGrid& operator=( const EditorPropertyGrid& ) = delete;

        /**
         * @brief 검색 칸을 그립니다(그리드 위 한 줄).
         * @param pMarkKey 주면 지우기 단추 없이 그리고 입력 칸에 이 이름표를 남깁니다(시나리오가 누른다).
         */
        void drawSearchBar( const utf8* pMarkKey = nullptr );
        /** @brief 지금 검색어입니다(비면 빈 글). 환경설정 창이 섹션을 거를 때 씁니다. */
        const utf8* getFilterText() const { return _propertyFilter.c_str(); }
        /**
         * @brief 대상의 프로퍼티를 그립니다. @p listDrawnName 은 이미 다른 곳(인스펙터 확장)이 그린 이름이라 다시 그리지 않습니다.
         * @param pSectionTitle 그릴 것이 있을 때만 위에 두는 구분선 제목입니다. nullptr 이면 없습니다.
         */
        void drawProperties( const EditorPropertyGridTarget& target, const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName );
        /** @brief 대상 타입의 메서드(FUNCTION)와, @p bEvents 면 이벤트를 그립니다(인스펙터의 Methods 구역). */
        void drawMethodsAndEvents( const EditorPropertyGridTarget& target, bool bEvents );

    private:
        void beginTarget( const EditorPropertyGridTarget& target );
        /** @brief 프로퍼티 하나를 기본값(@p pDefaultInstance 의 값, 없으면 메타 글)으로 되돌립니다. 통지 · 되돌리기 기록은 편집과 같다. */
        void resetPropertyToDefault( void* pInstance, const PropertyInfo& prop, const void* pDefaultInstance );
        /** @brief 프로퍼티 하나에 글 값을 입혀 편집으로 남깁니다(통지 · 주인 오브젝트의 되돌리기). Reset · Paste Value 가 같이 쓴다. */
        void applyPropertyTextAsEdit( void* pInstance, const PropertyInfo& prop, string_view text, const utf8* pUndoLabel );
        void endTarget();
        /**
         * @brief 타입의 반사 프로퍼티를 상속분까지 카테고리별로 그립니다(`InspectorPropertyLayout`). 그릴 것이 있을 때만 @p pSectionTitle 구분선을 둡니다.
         * @param listDrawnName 인스펙터 확장이 이미 그린 프로퍼티 이름 — 다시 그리지 않습니다.
         */
        void drawTypeProperties( void* pInstance, const TypeInfo* pTypeInfo, const utf8* pSectionTitle, const vector<hashed_string>& listDrawnName );
        /** @brief 단일 프로퍼티 위젯을 그립니다. 값이 바뀌면 편집 대상에 통지합니다. */
        void drawPropertyWidget( void* pInstance, const PropertyInfo& prop );
        /** @brief 위젯 본문. 통지 판정은 감싸는 drawPropertyWidget 이 합니다. */
        void drawPropertyWidgetBody( void* pInstance, const PropertyInfo& prop );
        /** @brief 열거형 프로퍼티 위젯을 그립니다. */
        void drawEnumProperty( void* pInstance, const PropertyInfo& prop, const EnumInfo& enumInfo, bool bReadOnly );
        /** @brief 컨테이너 프로퍼티 위젯을 그립니다. */
        void drawContainerProperty( void* pInstance, const PropertyInfo& prop, bool bReadOnly );
        /**
         * @brief 맵 프로퍼티를 그립니다. 키는 글이고(정렬 · 해시 키라 제자리에서 고치지 않는다), 값은 타입 위젯으로 제자리 편집, 항목 지우기 · 더하기.
         * @details 더하기는 키 글을 Enter 로 받습니다. 같은 키가 이미 있으면 값을 덮어쓰지 않고 더하지 않습니다.
         */
        void drawMapContainer( void* pContainer, const PropertyInfo& prop, IMapContainerWrapper& mapWrapper, bool bReadOnly );
        /** @brief 원소가 곧 키인 시퀀스(`set`)를 그립니다. 원소는 글 칸이고 Enter 로 고치면 지우고 다시 넣습니다(`replaceElement`). */
        void drawKeyedSequenceContainer( void* pContainer, const PropertyInfo& prop, ISequenceContainerWrapper& sequence, bool bReadOnly );
        /** @brief 컨테이너 끝의 "더하기" 글 칸입니다. Enter 로 빈 글이 아닌 것을 냈으면 true 이고 @p outText 에 담고 칸을 비웁니다. */
        bool drawContainerAddRow( const utf8* pHint, string& outText );
        /** @brief 지금 그리는 프로퍼티의 주인 오브젝트입니다(되돌리기 기록 대상). 없으면 nullptr 입니다. */
        GameObject* getEditOwner() const;
        /** @brief 구조체 · 문자열 프로퍼티 위젯을 그립니다. */
        void drawStructOrStringProperty( void* pInstance, const PropertyInfo& prop, const TypeInfo* pFieldType );
        /**
         * @brief 그리드가 값을 바꿨음을 편집 대상에 알립니다.
         * @details 위젯 대부분이 `getValuePtr<T>()` 로 멤버의 생 포인터를 ImGui 에 넘기므로 리플렉션 `setValue<T>()` 의 통지 분기를 지나지 않습니다.
         *          이 통지를 빠뜨리면 렌더 상태처럼 바뀌면 누군가 반응해야 하는 값이 조용히 어긋납니다.
         */
        void notifyPropertyEdited( const PropertyInfo& prop );
        /** @brief 타입의 메서드(FUNCTION) 목록을 그립니다. */
        void drawTypeMethods( void* pInstance, const TypeInfo* pTypeInfo );
        /** @brief 타입의 이벤트(멀티캐스트 델리게이트 PROPERTY) 목록을 기반부터 그립니다. 인자 없는 이벤트는 그 자리에서 부를 수 있습니다. */
        void drawTypeEvents( void* pInstance, const TypeInfo* pTypeInfo );
        /** @brief FUNCTION 을 호출하고 반환값을 "Last result" 줄에 씁니다. 인자 없는 Run 버튼과 Invoke 버튼이 같은 경로를 씁니다. */
        void invokeTypeMethod( void* pInstance, const TypeInfo* pTypeInfo, const FunctionInfo& method, const TaskArgs& args );

        fixed_string<constant::kMaxBuffer64>                         _propertyFilter;   ///< 프로퍼티 검색 필터
        unordered_map<uint64, vector<InspectorMethodArgSlot>>        _mapMethodArgSlot; ///< FUNCTION() 인자 칸 — 키는 (타입 · 메서드) 이름 해시
        fixed_string<constant::kMaxBuffer256>                        _lastInvokeResult;
        unordered_map<uint32, fixed_string<constant::kMaxBuffer256>> _mapContainerAddText;  ///< 컨테이너 "더하기" 칸의 글 — 키는 그 칸의 ImGui id
        Delegate<void( const PropertyInfo& )>                        _onEdited;             ///< 지금 대상의 `_onEdited`
        Delegate<bool( const PropertyInfo& )>                        _isMixed;              ///< 지금 대상의 `_isMixed`
        Component*                                                   _pEditTargetComponent; ///< 지금 프로퍼티를 그리는 컴포넌트 — 편집 통지를 받는다
        GameObject*                                                  _pEditTargetObject;    ///< 지금 프로퍼티를 그리는 GameObject — 컴포넌트가 없을 때만
        uint32                                                       _propertyDrawDepth;    ///< 중첩 · 컨테이너 재귀 깊이 — 통지는 가장 바깥에서 한 번
    };
} // namespace sw::editor
