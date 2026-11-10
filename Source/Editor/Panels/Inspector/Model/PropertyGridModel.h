/**
 * @file PropertyGridModel.h
 * @brief 프로퍼티 그리드의 편집 모델입니다 — 대상 값(`EditorPropertyGridTarget`)을 프레임마다 풀어 보이는 프로퍼티마다 값 행(`PropertyValueRow`)을 내고(`gather`),
 *        행 번호로 값을 바꾸는 명령의 한 길입니다. 배치 캐시 · 즐겨찾기 · 기본값 비교 · 펼침 상태 · 행 높이를 듭니다.
 *        ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 절 '프로퍼티 그리드 편집 모델'). 그리기는 View `EditorPropertyGrid` 가 한다.
 * @details 언리얼 `IPropertyHandle` · 유니티 `SerializedProperty` 자리다. 씬 대상은 포인터가 아니라 (선택 범위, 오브젝트 id, 컴포넌트 id) 값이라 대상이 그리는 사이에
 *          사라져도 매달린 포인터가 남지 않는다. 씬 밖 대상(환경설정 · 문서)은 인스턴스 주소와 타입을 그대로 받는다 — 그 주인 패널이 수명을 든다.
 *          인스턴스 · 멤버 주소는 모델의 행 표만 들고 View 에는 행 번호와 값 사본만 나간다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Panels/Inspector/Model/InspectorLayoutCache.h"
#include "Editor/Panels/Inspector/Model/PropertyGridViewState.h"

#include "Engine/Reflection/TextAddress.h"

namespace sw
{
    struct EventInfo;
    struct FunctionInfo;
    struct PropertyInfo;
    struct TypeInfo;

    class Component;
    class ComponentHandle;
    class TaskArgs;
    class GameObject;
    class GameObjectManager;
    class TypeRegistry;
} // namespace sw

namespace sw::editor
{
    struct EditorAssetIndexEntry;

    class InspectorFavorites;
    class InspectorPropertyManager;
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 씬 대상의 오브젝트를 찾을 선택 범위입니다. */
    enum class EditorPropertyGridScope : uint8
    {
        None,           ///< 씬 밖 대상 — `_pInstance` · `_pType` 를 그대로 쓴다
        MainScene,      ///< 활성(메인) 씬의 오브젝트
        FocusedPreview, ///< 지금 겨눈 미리보기 창 선택의 씬 오브젝트
    };

    /**
     * @struct EditorPropertyGridTarget
     * @brief 그리드가 그리고 편집하는 대상 하나의 값입니다. 씬 대상은 (범위, 오브젝트 id, 컴포넌트 id)이고, 씬 밖 대상은 인스턴스 주소와 타입입니다.
     * @details 씬 대상은 `PropertyGridModel::beginTarget` 이 그 프레임에 오브젝트 · 컴포넌트를 찾아 인스턴스로 쓴다. 컴포넌트 대상은 그 컴포넌트가 `onPropertyChanged` 를 받고,
     *          오브젝트 대상은 오브젝트가 받는다. 되돌리기의 주인은 언제나 그 오브젝트다.
     */
    struct EditorPropertyGridTarget
    {
        void*                                 _pInstance{ nullptr };                   ///< 씬 밖 대상의 인스턴스. 씬 대상은 비워 둔다
        const TypeInfo*                       _pType{ nullptr };                       ///< 씬 밖 대상의 타입. 씬 대상은 비워 둔다
        uint64                                _objectID{ 0 };                          ///< 씬 대상의 오브젝트 id(되돌리기의 주인)
        uint64                                _componentID{ 0 };                       ///< 씬 대상의 컴포넌트 id. 0 이면 오브젝트 자신의 프로퍼티다
        EditorPropertyGridScope               _scope{ EditorPropertyGridScope::None }; ///< 오브젝트를 찾을 씬
        Delegate<void( const PropertyInfo& )> _onEdited;                               ///< 씬 밖 객체(환경설정, 문서)의 변경을 받는 델리게이트. 비어 있으면 씬 대상만 받는다
        Delegate<bool( const PropertyInfo& )> _isMixed;                                ///< 다중 선택에서 값이 오브젝트마다 다르면 true 이다. 비어 있으면 혼합이 없다

        /** @brief @p scope 씬의 오브젝트 @p objectID 의 컴포넌트 @p componentID(0 이면 오브젝트 자신) 대상입니다. */
        static EditorPropertyGridTarget makeSceneTarget( EditorPropertyGridScope scope, uint64 objectID, uint64 componentID );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class PropertyGridModel
     * @brief 프로퍼티 그리드 하나의 편집 모델입니다. 그리드(View)가 하나씩 들고, 대상을 그리는 동안(`beginTarget` ~ `endTarget`) 그 대상에 묶입니다.
     * @details 한 대상을 그리는 차례: `beginTarget` → `gather`(행 표를 다시 짓고 값 행을 채운다) → View 가 행을 그리며 행 번호로 명령을 부른다 → `endTarget`.
     *          행 번호는 그 `gather` 의 행 표 자리라 다음 `gather` · `endTarget` 뒤에는 쓰지 않는다(없는 번호는 명령이 무시한다).
     */
    class SW_EDITOR_API PropertyGridModel
    {
    public:
        PropertyGridModel();
        ~PropertyGridModel();
        PropertyGridModel( const PropertyGridModel& )            = delete;
        PropertyGridModel& operator=( const PropertyGridModel& ) = delete;

        // ------------------------------------------------------------------------------
        // 대상
        // ------------------------------------------------------------------------------
        /** @brief @p target 을 풀어 이번 그리기의 대상으로 묶습니다. 씬 대상의 오브젝트 · 컴포넌트가 없거나 타입이 없으면 false 이고 아무것도 묶지 않습니다. */
        [[nodiscard]] bool beginTarget( const EditorPropertyGridTarget& target );
        /** @brief 묶은 대상을 풉니다. 행 표도 비웁니다. */
        void endTarget();
        /** @brief 묶은 대상의 인스턴스 주소입니다. 묶지 않았으면 nullptr 입니다. Model 층(테스트 · 값 모음)만 쓴다 — View 는 행 번호로 명령을 부른다. */
        void* getInstance() const { return _pInstance; }
        /** @brief 묶은 대상의 타입입니다(리플렉션 메타 — 메서드 · 이벤트 목록을 그릴 때 읽는다). */
        const TypeInfo* getType() const { return _pType; }
        /** @brief @p pInstance 가 묶은 대상 자신(컴포넌트나 오브젝트)이면 true 입니다. 중첩 구조체 필드는 false 입니다(즐겨찾기 키가 대상 타입이 아니다). */
        bool isTargetInstance( const void* pInstance ) const;
        /** @brief 다중 선택 대상이면 true 입니다(`_isMixed` 가 걸렸다). */
        bool isMultiEdit() const { return _isMixed.isBound(); }
        /**
         * @brief 묶은 대상의 주인 오브젝트가 사는 씬의 오브젝트 관리자입니다. 없으면 nullptr 입니다.
         * @details 타입 핸들 선택기가 편집 대상의 씬을 얻는 길이다. 전역의 활성 씬에서 찾으면 플레이 세션 · 프리팹 편집처럼 그리드 대상과
         *          활성 씬이 다를 때 다른 씬의 컴포넌트가 목록에 섞인다.
         */
        const GameObjectManager* findEditOwnerManager() const;

        // ------------------------------------------------------------------------------
        // 값 행
        // ------------------------------------------------------------------------------
        /**
         * @brief 묶은 대상의 값 행을 모읍니다(@p outState 를 비우고 채운다). 대상 하나를 그리기 전에 한 번 — View 와 모델의 동기화 지점이다.
         * @details 카테고리 머리와 깊이 0 행은 지난 프레임에 잰 높이로 이번 프레임의 자리를 셈해, 보이는 영역(@p params `_window`) 밖이면 값을 채우지 않고 `Skipped` 로 낸다
         *          (`HierarchyViewModel` 과 같은 화면 밖 건너뛰기 — 언리얼 `STreeView` 가상화). 보이는 행은 펼친 자식(구조체 필드 · 컨테이너 원소)까지 값을 채운다.
         *          숨김(`EditConditionHides`) · 검색 · 즐겨찾기 · 바뀐 것만 보기 거르기는 화면 밖 행에도 적용한다(자리 셈이 맞게).
         */
        void gather( const PropertyGridGatherParams& params, PropertyGridViewState& outState );
        /**
         * @brief 행 @p rowID 의 값 자리와 프로퍼티를 찾습니다. 값이 없는 행(카테고리 머리 · 화면 밖)이거나 없는 번호면 false 입니다.
         * @details Model 층의 값 모음(`SceneObjectReferencePickerState`)이 행 하나의 값을 읽는 길이다. View 는 부르지 않는다(값은 행에 있다).
         */
        [[nodiscard]] bool findRowValue( uint32 rowID, MutableObjectAddress& outInstance, const PropertyInfo*& outProp ) const;
        /** @brief 숫자 행 @p rowID 의 printf 서식입니다 — @p pNumberFormat 에 단위를 붙인다(`InspectorPropertyLayout::appendUnitSuffix`). 묶음에 든 프로퍼티는 처음 한 번만 짓습니다. */
        const utf8* getUnitNumberFormat( uint32 rowID, const utf8* pNumberFormat );
        /** @brief 배치 묶음을 새로 지은 횟수입니다(프로브 `Editor.InspectorLayoutRebuildCount`). */
        uint32 getLayoutRebuildCount() const { return _layoutCache.getRebuildCount(); }

        // ------------------------------------------------------------------------------
        // 보기만 바꾸는 명령 — 되돌리기 · 씬 dirty 에 남지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 행 @p rowID(카테고리 머리 · 노드)를 펼치거나 접습니다(머리 · 트리 노드의 결과). 다음 `gather` 가 자식을 냅니다. */
        void setRowExpanded( uint32 rowID, bool bExpanded );
        /** @brief 행 @p rowID 가 차지한 높이(자식 포함)를 적어 둡니다. 다음 `gather` 가 화면 밖 행의 자리를 이것으로 셈한다. */
        void setRowHeight( uint32 rowID, float32 height );

        // ------------------------------------------------------------------------------
        // 즐겨찾기
        // ------------------------------------------------------------------------------
        /** @brief 즐겨찾기 집합을 연결합니다. nullptr 이면 끕니다. 집합의 주인은 인스펙터 패널이다. */
        void setFavorites( InspectorFavorites* pFavorites ) { _pFavorites = pFavorites; }
        /** @brief 즐겨찾기를 쓰는 그리드이면 true 입니다. */
        bool hasFavorites() const { return _pFavorites != nullptr; }
        /** @brief 키 @p favoriteKey 의 즐겨찾기를 켜고 끄고 바로 저장합니다(언리얼 Details 의 Add to Favorites · Remove from Favorites). */
        void toggleFavorite( string_view favoriteKey );

        // ------------------------------------------------------------------------------
        // 명령 — 값을 바꾸는 한 길. 행 번호로 받는다. 묶은 대상에 알리고(`onPropertyChanged` · `_onEdited` — 자식 행은 깊이 0 조상 프로퍼티로),
        // 씬 대상이면 되돌리기 한 단계를 남긴다(`setRowValue` · `setRowNumber` 는 그리드가 위젯의 활성 ~ 해제를 한 단계로 묶는다)
        // ------------------------------------------------------------------------------
        /**
         * @brief 내장 값 행 @p rowID 에 위젯이 고친 사본 @p value 를 씁니다(`Builtin` 은 그 C++ 타입, `BitField` 는 bool). 끄는 동안 프레임마다 불린다.
         * @details 되돌리기는 여기서 남기지 않는다 — 그리드가 위젯의 활성 ~ 해제를 한 단계로 묶는다(`InspectorPropertyUndo::trackRowEdit` 를 이 명령보다 먼저 부른다 — 편집 전을 찍는다).
         */
        void setRowValue( uint32 rowID, const TaskValue& value );
        /** @brief 숫자 행 @p rowID 에 저장 단위 값 @p storedValue 를 허용 범위(`Min` · `Max`)로 자른 뒤 씁니다. 되돌리기는 `setRowValue` 와 같다. */
        void setRowNumber( uint32 rowID, float64 storedValue );
        /**
         * @brief 행 @p rowID 에 텍스트 값(`SerializerUtil::formatPropertyText` 형식)을 적용하고 편집으로 기록합니다.
         * @details Reset, Paste Value, 그리기 확장(`IInspectorProperty`)의 팝업 편집기 · 선택기가 함께 쓴다.
         *          다중 선택은 선택 전부를 찍고(`InspectorEditRecorder::recordImmediateEdit`), `_onEdited` 가 값을 나머지에도 입힌다.
         */
        void setPropertyText( uint32 rowID, string_view text, const utf8* pUndoLabel );
        /** @brief 에셋 경로 행 @p rowID 에 끌어 놓거나 고른 경로 @p path 를 씁니다. 그 칸이 받는 종류(`FileFilter` · `AssetRef<T>` 의 대상 종류)가 아니면 쓰지 않고 false 입니다. */
        bool setAssetPath( uint32 rowID, string_view path );
        /** @brief 행 @p rowID 를 기본값으로 되돌립니다. 기본값은 대상 컴포넌트의 기본 인스턴스(CDO)이고, 없으면 메타 텍스트입니다. */
        void resetToDefault( uint32 rowID );
        /** @brief 행 @p rowID 의 값 글(`SerializerUtil::formatPropertyText`)입니다 — Copy Value 가 부른다. 없는 행은 빈 글입니다. */
        string formatPropertyText( uint32 rowID ) const;
        /** @brief 열거형 행 @p rowID 를 @p value 로 씁니다. 고르는 순간 바뀌는 콤보라 쓰기 직전에 편집 전을 찍어 되돌리기에 남깁니다. */
        void setEnumValue( uint32 rowID, int64 value );
        /** @brief 시퀀스 행 @p rowID 끝에 기본값 원소를 하나 더합니다. */
        void addElement( uint32 rowID );
        /** @brief 시퀀스 행 @p rowID 를 비웁니다. */
        void clearElements( uint32 rowID );
        /** @brief 맵 행 @p rowID 의 @p ordinal 번째 항목을 지웁니다. */
        void removeMapEntry( uint32 rowID, size_t ordinal );
        /** @brief 맵 행 @p rowID 에 키 글 @p keyText 로 기본값 항목을 더합니다. 키를 읽지 못하거나 같은 키가 있으면 아무것도 하지 않습니다. */
        void addMapEntry( uint32 rowID, string_view keyText );
        /** @brief 원소가 곧 키인 시퀀스 행 @p rowID 의 @p index 번째 원소를 글 @p text 로 바꿉니다(지우고 다시 넣는다). */
        void replaceKeyedElement( uint32 rowID, size_t index, string_view text );
        /** @brief 원소가 곧 키인 시퀀스 행 @p rowID 의 @p index 번째 원소를 지웁니다. */
        void removeKeyedElement( uint32 rowID, size_t index );
        /** @brief 원소가 곧 키인 시퀀스 행 @p rowID 에 글 @p text 의 원소를 더합니다. */
        void addKeyedElement( uint32 rowID, string_view text );
        /**
         * @brief 묶은 대상의 FUNCTION @p method 를 호출하고 반환값 글을 `getLastInvokeResult` 에 둡니다. 인자 없는 Run 단추와 Invoke 단추가 같은 길을 씁니다.
         * @details 주인 오브젝트가 있으면 호출을 오브젝트 편집 하나로 감싼다 — 바뀐 것이 있으면 되돌리기 한 단계와 씬 dirty, 없으면 아무것도 남기지 않는다.
         *          Play 중에는 기록 없이 호출한다(`InspectorObjectEdit::applyObjectEdit` 와 같은 규칙).
         */
        void invokeMethod( const FunctionInfo& method, const TaskArgs& args );
        /** @brief 마지막으로 부른 메서드의 반환값 글입니다. 부른 적이 없으면 빈 글입니다. */
        const utf8* getLastInvokeResult() const { return _lastInvokeResult.c_str(); }
        /** @brief 묶은 대상의 이벤트 @p event 에 받는 쪽이 걸려 있으면 true 입니다. */
        [[nodiscard]] bool isEventBound( const EventInfo& event ) const;
        /** @brief 묶은 대상의 이벤트 @p event 를 인자 없이 방송합니다. */
        void broadcastEvent( const EventInfo& event );

        // ------------------------------------------------------------------------------
        // 값 모음 — 팝업 · 콤보가 열린 프레임에만 부른다
        // ------------------------------------------------------------------------------
        /** @brief 에셋 경로 행 @p rowID 의 선택 팝업이 고를 수 있는 리소스를 모읍니다(그 칸이 받는 종류만 — 팝업을 열 때 한 번, 파일 목록을 프레임마다 훑지 않는다). */
        void collectAssetEntries( uint32 rowID, vector<EditorAssetIndexEntry>& outListEntry ) const;
        /**
         * @brief 대상 타입 @p targetTypeName 의 핸들 @p handle 을 편집 대상의 씬에서 풀어 미리보기 글과 상태를 채웁니다(타입 핸들 선택기).
         * @return 대상 타입이 레지스트리에 없거나 편집 대상의 씬이 없으면 false 이고 @p outPreview 가 오류 글입니다(활성 씬으로 넓히지 않는다).
         */
        bool describeComponentHandle( const hashed_string& targetTypeName, const ComponentHandle& handle, string& outPreview, InspectorHandleState& outState ) const;
        /** @brief 편집 대상의 씬에서 @p targetTypeName 이나 그 파생 컴포넌트를 라벨 순으로 모읍니다. 타입 · 씬이 없으면 비웁니다. */
        void collectComponentChoices( const hashed_string& targetTypeName, vector<InspectorComponentChoice>& outListChoice ) const;

        // ------------------------------------------------------------------------------
        // 창구
        // ------------------------------------------------------------------------------
        /** @brief 프로퍼티 타입별 그리기 확장 관리자입니다(에디터 컨텍스트 서브시스템). View 가 작업 공간 헤더를 보지 않고 찾는 길이다. */
        static InspectorPropertyManager& getPropertyManager();
        /** @brief @p scope 씬의 오브젝트 @p objectID 입니다. 미리보기 범위는 지금 겨눈 미리보기 선택의 씬이고, 겨눈 창이 미리보기가 아니거나 없으면 nullptr 입니다. */
        static GameObject* findSceneObject( EditorPropertyGridScope scope, uint64 objectID );
        /** @brief 리플렉션 타입 레지스트리입니다. */
        static const TypeRegistry& getTypeRegistry();

    private:
        /** @brief 행 표의 한 줄입니다 — 값 행이 가리키는 값 자리. 이 표만 인스턴스 주소를 든다. */
        struct RowBinding
        {
            MutableObjectAddress _instance{ nullptr }; ///< 프로퍼티를 가진 객체(원소 · 맵 값은 그 값 자리 — 원소 프로퍼티의 오프셋이 0 이다)
            const PropertyInfo*  _pProp{ nullptr };    ///< 프로퍼티(원소 프로퍼티는 `_listElementProp` 의 것). 카테고리 머리 · 화면 밖 행은 nullptr
            uint64               _key{ 0 };            ///< `PropertyValueRow::_key`
            uint32               _rootRowID{ 0 };      ///< 깊이 0 조상 행 — 알림은 그 프로퍼티로 한다
            uint8                _bDefaultOpen{ 0 };   ///< 펼침 상태를 적지 않았을 때 펼쳐져 있다(카테고리 머리)
        };

        /** @brief 행 @p rowID 의 행 표 줄입니다. 없으면 nullptr 입니다. */
        const RowBinding* findBinding( uint32 rowID ) const;
        /** @brief 값이 있는 행(프로퍼티 행)이면 그 줄, 아니면 nullptr 입니다. */
        const RowBinding* findValueBinding( uint32 rowID ) const;
        /** @brief 키 @p key 가 펼쳐져 있으면 true 입니다. 적지 않았으면 @p bDefaultOpen 입니다. */
        bool isExpanded( uint64 key, bool bDefaultOpen ) const;
        /** @brief 키 @p key 를 @p bExpanded 로 적습니다(기본과 같으면 지운다). */
        void storeExpanded( uint64 key, bool bDefaultOpen, bool bExpanded );
        /** @brief 키 @p key 의 잰 높이입니다. 잰 적 없으면 @p defaultHeight 입니다. */
        float32 findRowHeight( uint64 key, float32 defaultHeight ) const;
        /** @brief 행 하나를 더하고 그 행을 돌려줍니다(저장 공간을 다시 쓴다 — 값은 기본으로 되돌린다). 행 표에도 줄을 더합니다. */
        PropertyValueRow& appendRow( PropertyGridViewState& outState, PropertyRowKind kind, uint16 depth, uint64 key );
        /**
         * @brief 프로퍼티 @p prop 의 값 행(과 펼친 자식)을 채웁니다.
         * @param rootRowID 깊이 0 조상 행. 깊이 0 행이면 `constant::invalid_index::kUint32` 를 넘긴다(자신이 된다).
         * @param bDisabled 편집을 막는다(조상이 막혔거나 `EditCondition` 이 거짓이거나 막힌 컨테이너의 원소).
         */
        void appendValueRow( PropertyGridViewState& outState, MutableObjectAddress instance, const PropertyInfo& prop, uint16 depth, uint32 rootRowID, uint64 key,
                             PropertyRowChild child, bool bDisabled );
        /** @brief 펼친 노드 행 @p rowID 의 자식(구조체 필드 · 원소 · 맵 값)을 채웁니다. */
        void appendChildRows( PropertyGridViewState& outState, uint32 rowID, MutableObjectAddress instance, const PropertyInfo& prop );
        /** @brief 내장 값 행의 사본 · 숫자 범위 · 단위 · 읽기 전용 글을 채웁니다. */
        void fillBuiltinRow( PropertyValueRow& row, MutableObjectAddress instance, const PropertyInfo& prop, const InspectorBuiltinValue& builtin );
        /** @brief 묶음 밖 임시 프로퍼티(컨테이너 원소 · 맵 값)를 하나 더해 @p containerProp 의 원소 프로퍼티로 채웁니다. 다음 `gather` 까지 주소가 그대로다. */
        const PropertyInfo& appendElementProperty( const PropertyInfo& containerProp );
        /** @brief @p prop 의 표시 단위입니다. 묶음에 든 프로퍼티는 캐시한 값이고, 컨테이너 원소처럼 값으로 만든 임시 프로퍼티는 이번 호출에 계산합니다. */
        const InspectorDisplayUnit& getDisplayUnit( const PropertyInfo& prop );
        /** @brief @p pInstance 의 기본값 비교 대상입니다. 묶은 컴포넌트 자신이면 그 타입의 기본 인스턴스(CDO), 그 밖(중첩 구조체 · 씬 밖 객체)은 비어 있습니다(메타 기본값 텍스트를 쓴다). */
        ObjectAddress findDefaultInstance( const void* pInstance, const TypeInfo& type ) const;
        /** @brief 즐겨찾기한 프로퍼티만 타입 표시 이름 머리 하나로 모아 @p inoutListGroup 을 바꿉니다. 하나도 없으면 비웁니다. */
        void keepFavoriteProperties( const TypeInfo& type, vector<InspectorPropertyGroup>& inoutListGroup ) const;
        /** @brief 기본값과 같은 프로퍼티를 빼고, 남은 것이 없는 카테고리는 머리째 뺍니다. 다중 선택에서 값이 갈린 프로퍼티는 남깁니다. */
        void removeDefaultValued( ObjectAddress instance, ObjectAddress defaultInstance, vector<InspectorPropertyGroup>& inoutListGroup );
        /** @brief 다중 선택에서 @p prop 값이 오브젝트마다 다르면 true 입니다. */
        bool isMixed( const PropertyInfo& prop );
        /** @brief 키 @p favoriteKey 가 즐겨찾기이면 true 입니다. */
        bool isFavorite( string_view favoriteKey ) const;
        /** @brief 행 @p rowID 의 편집을 묶은 대상에 알립니다 — 깊이 0 조상 프로퍼티로(`notifyPropertyEdited`). */
        void notifyRowEdited( uint32 rowID );
        /**
         * @brief 프로퍼티 @p prop 의 편집을 묶은 대상에 알립니다. 고친 값을 검증해 맵 검사 결과도 바꿉니다.
         * @details 이 알림을 빠뜨리면 렌더 상태처럼 값이 바뀔 때 누군가 반응해야 하는 데이터가 조용히 어긋난다.
         */
        void notifyPropertyEdited( const PropertyInfo& prop );
        /** @brief 컨테이너 행 @p rowID 의 편집 하나: 컨테이너 자리 · 프로퍼티로 @p edit 를 되돌리기 한 단계로 감싸 부르고 알립니다. 없는 행이면 아무것도 하지 않습니다. */
        template <typename EditFunc>
        void applyContainerEdit( uint32 rowID, string_view undoLabel, EditFunc&& edit );

    private:
        Component*                              _pComponent;        ///< 묶은 컴포넌트 대상. `onPropertyChanged` 를 받는다
        GameObject*                             _pObject;           ///< 묶은 대상의 주인 오브젝트(되돌리기의 주인). 컴포넌트가 없으면 `onPropertyChanged` 도 받는다
        void*                                   _pInstance;         ///< 묶은 대상의 인스턴스
        const TypeInfo*                         _pType;             ///< 묶은 대상의 타입
        Delegate<void( const PropertyInfo& )>   _onEdited;          ///< 묶은 대상의 `_onEdited`
        Delegate<bool( const PropertyInfo& )>   _isMixed;           ///< 묶은 대상의 `_isMixed`
        InspectorFavorites*                     _pFavorites;        ///< 즐겨찾기 집합. nullptr 이면 즐겨찾기를 쓰지 않는다
        InspectorLayoutCache                    _layoutCache;       ///< 타입 · 확장이 그린 이름 · 검색 글 · 레지스트리 세대마다 지은 묶음과 프로퍼티별 표시 정보
        InspectorPropertyDisplay                _scratchDisplay;    ///< 묶음 밖 임시 프로퍼티(컨테이너 원소)의 표시 정보 — 주소가 매번 달라 캐시하지 않고 부를 때마다 다시 채운다
        vector<InspectorPropertyGroup>          _listFilteredGroup; ///< 즐겨찾기 · 바뀐 것만 보기로 거른 묶음 사본(저장 공간을 다시 쓴다)
        vector<RowBinding>                      _listBinding;       ///< 이번 `gather` 의 행 표(행 번호 → 값 자리)
        vector<unique_ptr<PropertyInfo>>        _listElementProp;   ///< 이번 `gather` 의 원소 · 맵 값 프로퍼티(행 표가 주소를 들므로 따로 할당한다)
        unordered_map<uint64, float32>          _mapRowHeight;      ///< 행 키 → 지난번에 잰 높이
        unordered_set<uint64>                   _setFlippedKey;     ///< 펼침 상태가 기본과 다른 행 키
        unordered_set<uint64>                   _setReportedHandle; ///< 선택기를 그릴 수 없다는 오류를 이미 남긴 행 키
        uint64                                  _targetSalt;        ///< 묶은 대상의 키 씨앗(씬 대상은 범위 · id, 씬 밖 대상은 타입) — 같은 타입의 두 컴포넌트가 펼침 · 높이를 나눠 갖지 않는다
        fixed_string<constant::buffer::kMax256> _lastInvokeResult;  ///< 마지막으로 부른 메서드의 반환값 글
    };
} // namespace sw::editor
