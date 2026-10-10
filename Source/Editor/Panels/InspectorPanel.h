/**
 * @file InspectorPanel.h
 * @brief 선택한 GameObject / Component 의 프로퍼티를 편집하는 인스펙터입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/Gui/IEditorPanel.h"
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
    class IRHIDevice;
    class TaskArgs;
} // namespace sw

namespace sw::editor
{
    class EditorWorkspace;

    /** @brief 아웃라이너에서 지금 선택한 것을 살펴보고 편집합니다. */
    class InspectorPanel : public IEditorPanel
    {
    public:
        InspectorPanel();
        ~InspectorPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 패널 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Inspector"; }
        /** @brief 인스펙터 UI를 그립니다. */
        void drawContent() override;

    private:
        // ------------------------------------------------------------------------------
        // 2) 선택 / 컴포넌트
        // ------------------------------------------------------------------------------
        /** @brief 현재 선택 섹션을 그립니다. */
        void drawSelectionSection();
        /** @brief 프리팹 인스턴스일 때 적용·되돌리기·연결 해제 버튼을 그립니다. */
        void drawPrefabLinkSection( GameObject* pObj, const string& prefabPath );
        /** @brief 선택된 오브젝트의 컴포넌트 카드 목록을 그립니다. */
        void drawComponentList( GameObject* pObj, EditorWorkspace& workspace );
        /** @brief 컴포넌트 카드의 우클릭 메뉴를 그립니다. */
        void drawComponentContextMenu( GameObject* pObj, Component* pComp, EditorWorkspace& workspace, bool& bOutRemove );
        /** @brief GameObject 헤더(이름 등)를 그립니다. */
        void drawGameObjectHeader( GameObject* pObj );
        /** @brief 컴포넌트 섹션을 그립니다. */
        void drawComponentSection( Component* pComp, IRHIDevice* pRhiDevice );

        // ------------------------------------------------------------------------------
        // 3) 리플렉션 위젯
        // ------------------------------------------------------------------------------
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
         * @brief 맵 프로퍼티를 그립니다 — 키는 글(정렬 · 해시 키라 제자리에서 고치지 않는다), 값은 타입 위젯으로 제자리 편집, 항목 지우기 · 더하기.
         * @details 더하기는 키 글을 Enter 로 받습니다. 같은 키가 이미 있으면 값을 덮어쓰지 않고 더하지 않습니다.
         */
        void drawMapContainer( void* pContainer, const PropertyInfo& prop, IMapContainerWrapper& mapWrapper, bool bReadOnly );
        /**
         * @brief 원소가 곧 키인 시퀀스(`set`)를 그립니다 — 원소는 글 칸이고 Enter 로 고치면 지우고 다시 넣습니다(`replaceElement`), 지우기 · 더하기.
         */
        void drawKeyedSequenceContainer( void* pContainer, const PropertyInfo& prop, ISequenceContainerWrapper& sequence, bool bReadOnly );
        /** @brief 컨테이너 끝의 "더하기" 글 칸입니다. Enter 로 빈 글이 아닌 것을 냈으면 true 이고 @p outText 에 담고 칸을 비웁니다. */
        bool drawContainerAddRow( const utf8* pHint, string& outText );
        /** @brief 지금 그리는 프로퍼티의 주인 오브젝트입니다(되돌리기 기록 대상). 없으면 nullptr 입니다. */
        GameObject* getEditOwner() const;
        /** @brief 구조체·문자열 프로퍼티 위젯을 그립니다. */
        void drawStructOrStringProperty( void* pInstance, const PropertyInfo& prop, const TypeInfo* pFieldType );
        /**
         * @brief 인스펙터가 값을 바꿨음을 편집 대상에 알립니다.
         * @details 이것이 없으면 인스펙터 편집은 아무에게도 보이지 않는 변경이 됩니다. 위젯 대부분이 `getValuePtr<T>()` 로
         *          멤버의 생 포인터를 뽑아 ImGui 에 넘기기 때문에, 리플렉션 `setValue<T>()` 안의 통지 분기를 타지 않습니다.
         *          빠뜨리면 렌더 상태처럼 "바뀌면 누군가 반응해야 하는" 값들이 조용히 어긋납니다.
         */
        void notifyPropertyEdited( const PropertyInfo& prop );
        /** @brief 타입의 메서드(FUNCTION) 목록을 그립니다. */
        void drawTypeMethods( void* pInstance, const TypeInfo* pTypeInfo );
        /** @brief 타입의 이벤트(멀티캐스트 델리게이트 PROPERTY) 목록을 기반부터 그립니다. 인자 없는 이벤트는 그 자리에서 부를 수 있습니다. 없으면 아무것도 그리지 않습니다. */
        void drawTypeEvents( void* pInstance, const TypeInfo* pTypeInfo );
        /** @brief FUNCTION 을 호출하고 반환값을 "Last result" 줄에 씁니다. 인자 없는 Run 버튼과 Invoke 버튼이 같은 경로를 씁니다. */
        void invokeTypeMethod( void* pInstance, const TypeInfo* pTypeInfo, const FunctionInfo& method, const TaskArgs& args );

    private:
        /** @brief 프로퍼티 검색 필터 버퍼 */
        fixed_string<constant::kMaxBuffer64> _propertyFilter;
        /** @brief FUNCTION() 인자 칸입니다. 키는 (타입 · 메서드) 이름 해시라, 메서드마다 인자 타입 그대로의 값을 따로 듭니다. */
        unordered_map<uint64, vector<InspectorMethodArgSlot>> _mapMethodArgSlot;
        fixed_string<constant::kMaxBuffer256>                 _lastInvokeResult;
        /** @brief 이름 칸이 편집 중인 글입니다(편집 중이 아니면 프레임마다 오브젝트 이름으로 채운다 — 칸을 떠날 때 적용할 글을 잡아 둔다). */
        fixed_string<constant::kMaxBuffer256> _nameEditBuffer;
        EditorFileCollectJob                  _componentPresetJob;
        vector<string>                        _listComponentPresetFile;
        /** @brief 지금 프로퍼티를 그리는 중인 컴포넌트입니다. 편집 통지를 받습니다. */
        Component* _pEditTargetComponent;
        /** @brief 지금 프로퍼티를 그리는 중인 GameObject 입니다. 컴포넌트가 없을 때만 씁니다. */
        GameObject* _pEditTargetObject;
        /** @brief 컨테이너 "더하기" 칸의 글입니다. 키는 그 칸의 ImGui id 라 컨테이너마다 따로 듭니다. */
        unordered_map<uint32, fixed_string<constant::kMaxBuffer256>> _mapContainerAddText;
        /** @brief `_nameEditBuffer` 가 가리키는 오브젝트입니다(선택이 바뀌면 버린다). */
        uint64 _nameEditObjectId;
        /** @brief 중첩 · 컨테이너 재귀 깊이입니다. 통지는 가장 바깥에서 한 번만 합니다. */
        uint32                 _propertyDrawDepth;
        uint8                  _bComponentPresetDirty : 1;
        [[maybe_unused]] uint8 _reserved              : 7;
    };
} // namespace sw::editor
