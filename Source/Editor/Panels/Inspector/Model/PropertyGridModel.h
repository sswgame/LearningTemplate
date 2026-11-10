/**
 * @file PropertyGridModel.h
 * @brief 프로퍼티 그리드의 편집 모델입니다 — 대상 값(`EditorPropertyGridTarget`)을 프레임마다 풀고, 배치 캐시 · 즐겨찾기 · 기본값 비교를 들며, 값을 바꾸는 명령의 한 길입니다.
 *        ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 절 '프로퍼티 그리드 편집 모델'). 그리기는 View `EditorPropertyGrid` 가 한다.
 * @details 언리얼 `IPropertyHandle` · 유니티 `SerializedProperty` 자리다. 씬 대상은 포인터가 아니라 (선택 범위, 오브젝트 id, 컴포넌트 id) 값이라 대상이 그리는 사이에
 *          사라져도 매달린 포인터가 남지 않는다. 씬 밖 대상(환경설정 · 문서)은 인스턴스 주소와 타입을 그대로 받는다 — 그 주인 패널이 수명을 든다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Panels/Inspector/Model/InspectorLayoutCache.h"

#include "Engine/Reflection/TextAddress.h"

namespace sw
{
    struct EnumInfo;
    struct EventInfo;
    struct FunctionInfo;
    struct PropertyInfo;
    struct TypeInfo;

    class Component;
    class TaskArgs;
    class GameObject;
    class GameObjectManager;
    class TypeRegistry;
} // namespace sw

namespace sw::editor
{
    struct EditorAssetIndexEntry;

    class EditorListFilter;
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
     */
    class SW_EDITOR_API PropertyGridModel
    {
    public:
        PropertyGridModel();
        PropertyGridModel( const PropertyGridModel& )            = delete;
        PropertyGridModel& operator=( const PropertyGridModel& ) = delete;

        // ------------------------------------------------------------------------------
        // 대상
        // ------------------------------------------------------------------------------
        /** @brief @p target 을 풀어 이번 그리기의 대상으로 묶습니다. 씬 대상의 오브젝트 · 컴포넌트가 없거나 타입이 없으면 false 이고 아무것도 묶지 않습니다. */
        [[nodiscard]] bool beginTarget( const EditorPropertyGridTarget& target );
        /** @brief 묶은 대상을 풉니다. */
        void endTarget();
        /** @brief 묶은 대상의 인스턴스 주소입니다. 묶지 않았으면 nullptr 입니다. */
        void* getInstance() const { return _pInstance; }
        /** @brief 묶은 대상의 타입입니다. */
        const TypeInfo* getType() const { return _pType; }
        /** @brief @p pInstance 가 묶은 대상 자신(컴포넌트나 오브젝트)이면 true 입니다. 중첩 구조체 필드는 false 입니다(즐겨찾기 키가 대상 타입이 아니다). */
        bool isTargetInstance( const void* pInstance ) const;
        /** @brief 다중 선택 대상이면 true 입니다(`_isMixed` 가 걸렸다). */
        bool isMultiEdit() const { return _isMixed.isBound(); }
        /** @brief 다중 선택에서 @p prop 값이 오브젝트마다 다르면 true 입니다. */
        bool isMixed( const PropertyInfo& prop );
        /**
         * @brief 묶은 대상의 주인 오브젝트가 사는 씬의 오브젝트 관리자입니다. 없으면 nullptr 입니다.
         * @details 타입 핸들 선택기가 편집 대상의 씬을 얻는 길이다. 전역의 활성 씬에서 찾으면 플레이 세션 · 프리팹 편집처럼 그리드 대상과
         *          활성 씬이 다를 때 다른 씬의 컴포넌트가 목록에 섞인다.
         */
        const GameObjectManager* findEditOwnerManager() const;
        /** @brief @p pInstance 의 기본값 비교 대상입니다. 묶은 컴포넌트 자신이면 그 타입의 기본 인스턴스(CDO), 그 밖(중첩 구조체 · 씬 밖 객체)은 비어 있습니다(메타 기본값 텍스트를 쓴다). */
        ObjectAddress findDefaultInstance( const void* pInstance, const TypeInfo& type ) const;

        // ------------------------------------------------------------------------------
        // 배치 캐시
        // ------------------------------------------------------------------------------
        /** @brief @p type 의 카테고리 묶음입니다(`InspectorLayoutCache::getOrCreateGroups` — 레지스트리 세대는 여기서 읽는다). */
        const vector<InspectorPropertyGroup>& getOrCreateGroups( const TypeInfo& type, const vector<hashed_string>& listDrawnName, const EditorListFilter& filter,
                                                                 string_view filterText, bool bMayEvict );
        /** @brief 묶음에 든 @p prop 의 표시 정보입니다(`InspectorLayoutCache::getLaidOutDisplay`). */
        const InspectorPropertyDisplay& getLaidOutDisplay( const PropertyInfo& prop ) const { return _layoutCache.getLaidOutDisplay( prop ); }
        /** @brief @p prop 의 표시 단위입니다. 묶음에 든 프로퍼티는 캐시한 값이고, 컨테이너 원소처럼 값으로 만든 임시 프로퍼티는 이번 호출에 계산합니다. 숫자 · 벡터 위젯이 씁니다. */
        const InspectorDisplayUnit& getDisplayUnit( const PropertyInfo& prop );
        /** @brief @p pNumberFormat 에 @p prop 의 단위를 붙인 printf 서식입니다(`InspectorPropertyLayout::appendUnitSuffix`). 묶음에 든 프로퍼티는 처음 한 번만 짓습니다. */
        const utf8* getUnitNumberFormat( const PropertyInfo& prop, const utf8* pNumberFormat );
        /** @brief 배치 묶음을 새로 지은 횟수입니다(프로브 `Editor.InspectorLayoutRebuildCount`). */
        uint32 getLayoutRebuildCount() const { return _layoutCache.getRebuildCount(); }

        // ------------------------------------------------------------------------------
        // 즐겨찾기
        // ------------------------------------------------------------------------------
        /** @brief 즐겨찾기 집합을 연결합니다. nullptr 이면 끕니다. 집합의 주인은 인스펙터 패널이다. */
        void setFavorites( InspectorFavorites* pFavorites ) { _pFavorites = pFavorites; }
        /** @brief 즐겨찾기를 쓰는 그리드이면 true 입니다. */
        bool hasFavorites() const { return _pFavorites != nullptr; }
        /** @brief @p type 이나 그 기반에 즐겨찾기한 프로퍼티가 있으면 true 입니다. */
        bool hasAnyFavoriteFor( const TypeInfo& type ) const;
        /** @brief 키 @p favoriteKey 가 즐겨찾기이면 true 입니다. */
        bool isFavorite( string_view favoriteKey ) const;
        /** @brief 즐겨찾기한 프로퍼티만 타입 표시 이름 머리 하나로 모아 @p inoutListGroup 을 바꿉니다. 하나도 없으면 비웁니다. */
        void keepFavoriteProperties( const TypeInfo& type, vector<InspectorPropertyGroup>& inoutListGroup ) const;

        /** @brief 키 @p favoriteKey 의 즐겨찾기를 켜고 끄고 바로 저장합니다(언리얼 Details 의 Add to Favorites · Remove from Favorites). */
        void toggleFavorite( string_view favoriteKey );
        /**
         * @brief 기본값과 같은 프로퍼티를 빼고, 남은 것이 없는 카테고리는 머리째 뺍니다(바뀐 것만 보기).
         * @details 다중 선택에서 값이 갈린(혼합) 프로퍼티는 남깁니다 — 값이 다르면 선택 중 적어도 하나는 기본값이 아닙니다.
         */
        void removeDefaultValued( ObjectAddress instance, ObjectAddress defaultInstance, vector<InspectorPropertyGroup>& inoutListGroup );

        // ------------------------------------------------------------------------------
        // 명령 — 값을 바꾸는 한 길. 묶은 대상에 알리고(`onPropertyChanged` · `_onEdited`), 씬 대상이면 되돌리기 한 단계를 남긴다
        // ------------------------------------------------------------------------------
        /**
         * @brief 프로퍼티 하나에 텍스트 값(`SerializerUtil::formatPropertyText` 형식)을 적용하고 편집으로 기록합니다.
         * @details Reset, Paste Value, 그리기 확장(`IInspectorProperty`)의 팝업 편집기 · 선택기가 함께 쓴다. 대상을 묶은 동안에만 부른다.
         *          다중 선택은 선택 전부를 찍고(`InspectorEditRecorder::recordImmediateEdit`), `_onEdited` 가 값을 나머지에도 입힌다.
         */
        void setPropertyText( MutableObjectAddress instance, const PropertyInfo& prop, string_view text, const utf8* pUndoLabel );
        /** @brief 프로퍼티 하나를 기본값으로 되돌립니다. 기본값은 @p defaultInstance 의 값이고, 비어 있으면 메타 텍스트입니다. */
        void resetToDefault( MutableObjectAddress instance, const PropertyInfo& prop, ObjectAddress defaultInstance );
        /**
         * @brief 위젯이 멤버 주소에 바로 쓴 편집을 묶은 대상에 알립니다. 고친 값을 검증해 맵 검사 결과도 바꿉니다.
         * @details 위젯 대부분이 `getValuePtr<T>()` 로 멤버의 raw 포인터를 ImGui 에 넘기므로 리플렉션 `setValue<T>()` 의 알림 분기를 지나지 않는다.
         *          이 알림을 빠뜨리면 렌더 상태처럼 값이 바뀔 때 누군가 반응해야 하는 데이터가 조용히 어긋난다.
         */
        void notifyPropertyEdited( const PropertyInfo& prop );
        /** @brief 열거형 값을 @p value 로 씁니다. 고르는 순간 바뀌는 콤보라 쓰기 직전에 편집 전을 찍어 되돌리기에 남깁니다. */
        void setEnumValue( const PropertyInfo& prop, MutableValueAddress enumMemory, const EnumInfo& enumInfo, int64 value );
        /** @brief 시퀀스 컨테이너 @p container 끝에 기본값 원소를 하나 더합니다. */
        void addElement( const PropertyInfo& prop, MutableValueAddress container );
        /** @brief 시퀀스 컨테이너 @p container 를 비웁니다. */
        void clearElements( const PropertyInfo& prop, MutableValueAddress container );
        /** @brief 맵 @p container 의 @p ordinal 번째 항목을 지웁니다. */
        void removeMapEntry( const PropertyInfo& prop, MutableValueAddress container, size_t ordinal );
        /** @brief 맵 @p container 에 키 글 @p keyText 로 기본값 항목을 더합니다. 키를 읽지 못하거나 같은 키가 있으면 아무것도 하지 않습니다. */
        void addMapEntry( const PropertyInfo& prop, MutableValueAddress container, string_view keyText );
        /** @brief 원소가 곧 키인 시퀀스(`set`)의 @p index 번째 원소를 글 @p text 로 바꿉니다(지우고 다시 넣는다). */
        void replaceKeyedElement( const PropertyInfo& prop, MutableValueAddress container, size_t index, string_view text );
        /** @brief 원소가 곧 키인 시퀀스의 @p index 번째 원소를 지웁니다. */
        void removeKeyedElement( const PropertyInfo& prop, MutableValueAddress container, size_t index );
        /** @brief 원소가 곧 키인 시퀀스에 글 @p text 의 원소를 더합니다. */
        void addKeyedElement( const PropertyInfo& prop, MutableValueAddress container, string_view text );
        /**
         * @brief FUNCTION 을 호출하고 반환값 글을 `getLastInvokeResult` 에 둡니다. 인자 없는 Run 단추와 Invoke 단추가 같은 길을 씁니다.
         * @details 주인 오브젝트가 있으면 호출을 오브젝트 편집 하나로 감싼다 — 바뀐 것이 있으면 되돌리기 한 단계와 씬 dirty, 없으면 아무것도 남기지 않는다.
         *          Play 중에는 기록 없이 호출한다(`InspectorObjectEdit::applyObjectEdit` 와 같은 규칙).
         */
        void invokeMethod( void* pInstance, const TypeInfo& type, const FunctionInfo& method, const TaskArgs& args );
        /** @brief 마지막으로 부른 메서드의 반환값 글입니다. 부른 적이 없으면 빈 글입니다. */
        const utf8* getLastInvokeResult() const { return _lastInvokeResult.c_str(); }
        /** @brief 이벤트 @p event 를 인자 없이 방송합니다. */
        static void broadcastEvent( const EventInfo& event, void* pInstance );

        // ------------------------------------------------------------------------------
        // 창구
        // ------------------------------------------------------------------------------
        /** @brief 프로퍼티 타입별 그리기 확장 관리자입니다(에디터 컨텍스트 서브시스템). View 가 작업 공간 헤더를 보지 않고 찾는 길이다. */
        static InspectorPropertyManager& getPropertyManager();
        /** @brief @p scope 씬의 오브젝트 @p objectID 입니다. 미리보기 범위는 지금 겨눈 미리보기 선택의 씬이고, 겨눈 창이 미리보기가 아니거나 없으면 nullptr 입니다. */
        static GameObject* findSceneObject( EditorPropertyGridScope scope, uint64 objectID );
        /** @brief 리플렉션 타입 레지스트리입니다. */
        static const TypeRegistry& getTypeRegistry();
        /** @brief 에셋 선택 팝업이 고를 수 있는 리소스를 모아 @p outListEntry 를 채웁니다(팝업을 열 때 한 번 — 파일 목록을 프레임마다 훑지 않는다). */
        static void collectAssetEntries( vector<EditorAssetIndexEntry>& outListEntry );

    private:
        Component*                              _pComponent;       ///< 묶은 컴포넌트 대상. `onPropertyChanged` 를 받는다
        GameObject*                             _pObject;          ///< 묶은 대상의 주인 오브젝트(되돌리기의 주인). 컴포넌트가 없으면 `onPropertyChanged` 도 받는다
        void*                                   _pInstance;        ///< 묶은 대상의 인스턴스
        const TypeInfo*                         _pType;            ///< 묶은 대상의 타입
        Delegate<void( const PropertyInfo& )>   _onEdited;         ///< 묶은 대상의 `_onEdited`
        Delegate<bool( const PropertyInfo& )>   _isMixed;          ///< 묶은 대상의 `_isMixed`
        InspectorFavorites*                     _pFavorites;       ///< 즐겨찾기 집합. nullptr 이면 즐겨찾기를 쓰지 않는다
        InspectorLayoutCache                    _layoutCache;      ///< 타입 · 확장이 그린 이름 · 검색 글 · 레지스트리 세대마다 지은 묶음과 프로퍼티별 표시 정보
        InspectorPropertyDisplay                _scratchDisplay;   ///< 묶음 밖 임시 프로퍼티(컨테이너 원소)의 표시 정보 — 주소가 매번 달라 캐시하지 않고 부를 때마다 다시 채운다
        fixed_string<constant::buffer::kMax256> _lastInvokeResult; ///< 마지막으로 부른 메서드의 반환값 글
    };
} // namespace sw::editor
