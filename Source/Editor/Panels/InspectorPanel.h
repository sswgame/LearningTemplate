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
        void drawComponentSection( Component* pComp, IRHIDevice* pRHIDevice );

    private:
        /** @brief 리플렉션 프로퍼티 · 메서드 · 이벤트 그리기(검색 칸 포함)입니다. 컴포넌트와 오브젝트마다 대상을 바꿔 부른다. */
        EditorPropertyGrid _propertyGrid;
        /** @brief 이름 칸이 편집 중인 글입니다(편집 중이 아니면 프레임마다 오브젝트 이름으로 채운다 — 칸을 떠날 때 적용할 글을 잡아 둔다). */
        fixed_string<constant::kMaxBuffer256> _nameEditBuffer;
        EditorFileCollectJob                  _componentPresetJob;
        vector<string>                        _listComponentPresetFile;
        /** @brief `_nameEditBuffer` 가 가리키는 오브젝트입니다(선택이 바뀌면 버린다). */
        uint64                 _nameEditObjectID;
        uint8                  _bComponentPresetDirty : 1;
        [[maybe_unused]] uint8 _reserved              : 7;
    };
} // namespace sw::editor
