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
#include "Editor/Common/Commands/EditorMultiEdit.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"

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

    /** @brief 아웃라이너에서 현재 선택한 것을 살펴보고 편집하는 패널입니다. */
    class InspectorPanel : public IEditorPanel
    {
    public:
        InspectorPanel();
        ~InspectorPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel: 제목과 그리기
        // ------------------------------------------------------------------------------
        /** @brief 패널 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Inspector"; }
        /** @brief 인스펙터 UI 를 그립니다. */
        void drawContent() override;

    private:
        // ------------------------------------------------------------------------------
        // 2) 선택과 컴포넌트
        // ------------------------------------------------------------------------------
        /** @brief 현재 선택 섹션을 그립니다. */
        void drawSelectionSection();
        /** @brief 둘 이상 선택했을 때의 영역을 그립니다. 공통 컴포넌트만 그리고, 다른 값은 혼합으로 보여 주며, 고친 프로퍼티를 나머지에 적용합니다. 언리얼과 유니티와 같습니다. */
        void drawMultiSelection( const vector<GameObject*>& listObject );
        /** @brief 다중 편집에서 고친 프로퍼티를 나머지 오브젝트의 같은 컴포넌트에 적용합니다. 그리드의 `_onEdited` 가 호출합니다. */
        void copyEditToOthers( const PropertyInfo& prop );
        /** @brief 다중 편집에서 그 프로퍼티 값이 오브젝트마다 다른지 확인합니다. 그리드의 `_isMixed` 가 호출합니다. */
        bool isMultiEditMixed( const PropertyInfo& prop );
        /** @brief 프리팹 인스턴스일 때 적용, 되돌리기, 연결 해제 버튼을 그립니다. */
        void drawPrefabLinkSection( GameObject* pObj, string_view prefabPath );
        /** @brief 선택한 오브젝트의 컴포넌트 카드 목록을 그립니다. */
        void drawComponentList( GameObject* pObj, EditorWorkspace& workspace );
        /** @brief 컴포넌트 카드의 우클릭 메뉴를 그립니다. */
        void drawComponentContextMenu( GameObject* pObj, Component* pComp, EditorWorkspace& workspace, bool& bOutRemove );
        /** @brief GameObject 헤더(이름 등)를 그립니다. */
        void drawGameObjectHeader( GameObject* pObj );
        /** @brief 컴포넌트 섹션을 그립니다. */
        void drawComponentSection( Component* pComp, IRHIDevice* pRHIDevice );

    private:
        /** @brief 리플렉션 프로퍼티, 메서드, 이벤트를 그립니다. 검색 입력란을 포함하며, 컴포넌트와 오브젝트마다 대상을 바꿔 호출합니다. */
        EditorPropertyGrid _propertyGrid;
        /** @brief 다중 선택의 공통 컴포넌트입니다. 그리는 프레임마다 다시 모읍니다. */
        vector<EditorMultiEditComponent> _listMultiEditComponent;
        /** @brief 현재 그리는 공통 컴포넌트입니다. 그리드 콜백이 읽습니다. */
        EditorMultiEditComponent* _pMultiEditCurrent;
        /** @brief 이름 입력란에서 편집 중인 텍스트입니다. 편집 중이 아니면 프레임마다 오브젝트 이름으로 채우며, 입력란을 떠날 때 적용할 텍스트를 보관합니다. */
        fixed_string<constant::kMaxBuffer256> _nameEditBuffer;
        EditorFileCollectJob                  _componentPresetJob;
        vector<string>                        _listComponentPresetFile;
        /** @brief `_nameEditBuffer` 가 가리키는 오브젝트입니다. 선택이 바뀌면 버립니다. */
        uint64                               _nameEditObjectID;
        uint64                               _lockedObjectID;        ///< 자물쇠로 잠근 오브젝트다. 0 이면 선택을 따라간다
        Component*                           _pPendingMoveComponent; ///< 카드 메뉴의 Move Up 과 Move Down 이 지정한 컴포넌트다. 목록을 다 그린 뒤가 아니라 그 카드 다음에 적용한다
        int32                                _pendingMoveDirection;
        fixed_string<constant::kMaxBuffer64> _addComponentSearch; ///< Add Component 팝업의 검색어
        uint8                                _bComponentPresetDirty : 1;
        [[maybe_unused]] uint8               _reserved              : 7;
    };
} // namespace sw::editor
