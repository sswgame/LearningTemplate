/**
 * @file SceneObjectReferencePickerState.h
 * @brief `SceneObjectReference` 프로퍼티 선택기(그리기 확장 `SceneObjectReferencePropertyDrawer`)가 그리는 값입니다 — 지금 값의 미리보기와, 올라와 있는 씬마다 고를 수 있는 오브젝트 줄.
 *        ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 절). 씬 매니저 · 씬 오브젝트를 읽는 일은 여기서 하고, 그리기 확장은 값 글만 받아 `PropertyGridModel::setPropertyText` 로 쓴다.
 * @details 값은 (씬 경로, 파일 id) 라서 줄을 모으는 순간 대상 오브젝트에 파일 id 를 준다(`SceneObjectReference::makeReference` — 다음 저장이 그 id 를 쓴다).
 *          저장한 적 없는 씬(경로 없음)의 오브젝트는 참조를 만들 수 없어 씬 줄만 남는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/TextAddress.h"

namespace sw
{
    struct PropertyInfo;
} // namespace sw

namespace sw::editor
{
    /** @brief 고를 수 있는 오브젝트 한 줄입니다. */
    struct SceneObjectReferenceChoice
    {
        string _label;         ///< 오브젝트 이름
        string _valueText;     ///< 이 오브젝트를 가리키는 참조의 값 글(`SerializerUtil::formatValueText`)
        uint64 _objectID{ 0 }; ///< 줄 id(ImGui) — 씬 안에서 고유하다
    };

    /** @brief 올라와 있는 씬 하나의 줄입니다. */
    struct SceneObjectReferenceScene
    {
        string                             _title;      ///< 씬 파일 이름. 비어 있으면 저장한 적 없는 씬이라 참조를 만들 수 없다(줄도 없다)
        vector<SceneObjectReferenceChoice> _listChoice; ///< 이름 순
    };

    /**
     * @struct SceneObjectReferencePickerState
     * @brief 선택기 하나가 그리는 값 전부입니다. 그리기 확장이 멤버로 들어 벡터의 저장 공간을 다시 쓴다.
     */
    struct SceneObjectReferencePickerState
    {
        string                            _preview;     ///< 미리보기 글: `<씬 파일> / <오브젝트>` · `(scene not loaded) …` · `(missing) …` · `(none)`
        string                            _currentText; ///< 지금 값의 값 글 — 줄의 `_valueText` 와 같으면 지금 고른 줄이다
        string                            _noneText;    ///< 빈 참조의 값 글. `_currentText` 와 같으면 아무것도 가리키지 않는다
        vector<SceneObjectReferenceScene> _listScene;   ///< 콤보가 열렸을 때만 채운다
        uint8                             _bWarn : 1;   ///< 씬은 올라와 있는데 오브젝트가 없다(결함) — 미리보기를 경고색으로

        SceneObjectReferencePickerState();

        /** @brief @p prop 값(`SceneObjectReference`)의 미리보기 · 상태를 채웁니다. 씬 줄은 비웁니다. 값을 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool gatherPreview( MutableObjectAddress instance, const PropertyInfo& prop, SceneObjectReferencePickerState& outChoices );
        /** @brief 올라와 있는 씬마다 고를 수 있는 오브젝트 줄을 채웁니다(콤보를 연 프레임에만 부른다). `gatherPreview` 뒤에 부릅니다. */
        static void gatherScenes( MutableObjectAddress instance, const PropertyInfo& prop, SceneObjectReferencePickerState& inoutChoices );
    };
} // namespace sw::editor
