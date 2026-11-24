/**
 * @file EditorIconName.h
 * @brief 에디터 아이콘 이름 형(`EditorIconName`)과 `SW_EDITOR_ICON_NAME` 입니다 — 등록 줄 · 상수 표가 아이콘 표(`EditorIconGlyphTable.h`)를 끌어오지 않고 이름을 든다.
 */
#pragma once
#include "Editor/Common/Config/EditorListedName.h"

namespace sw::editor
{
    /** @brief 코드가 쓸 수 있는 아이콘 이름입니다. 아이콘을 글꼴과 표(`sweditoricons.glyphs.xml`)에 더하면 여기에도 더한다(대조: `EditorIconGlyphTable::loadFromAsset`). */
    struct EditorIconNameList
    {
        static constexpr const utf8* kArrName[] = {
            "File", "Folder", "FolderOpen", "Save", "Undo", "Redo", "Search", "Terminal", "Exit", "Settings", "Palette", "Layout", "Table", "Hammer",
            "Wrench", "BuildAll", "Cancel", "Spinner", "Check", "Close", "Success", "Warning", "Error", "Info", "Lock", "Unlock", "Plus", "Minus",
            "Trash", "Copy", "Refresh", "Filter", "Star", "Link", "Eye", "EyeSlash", "Play", "Pause", "Stop", "StepForward", "Translate", "Rotate",
            "Scale", "Globe", "Axes", "Magnet", "Grid", "Bookmark", "Align", "Chart", "Camera", "Cube", "Scene", "Prefab", "Texture", "Shader",
            "Material", "Audio", "AnimationGraph", "Dialogue", "Sprite", "TileMap", "Sequence", "Skeleton", "AnimationClip", "Rig", "Heightfield",
            "Fracture", "Widget", "Font", "GameObject", "Component", "LightPoint", "LightDirectional", "LightSpot", "LightGlobal", "AudioEmitter",
            "AudioListener", "AudioZone", "Physics", "Collider", "Character", "Animation", "Controller", "AI", "Navigation", "Trigger", "SpawnPoint",
            "Spline", "Water", "Foliage", "Wind", "Health", "Ability", "Vehicle", "Tag", "Missing", "Bug", "Map" };
    };

    /** @brief 아이콘 이름입니다(글리프가 아니다 — 그릴 때 `EditorIconGlyphTable::findGlyphByName` 으로 바꾼다). `SW_EDITOR_ICON_NAME` 만 만든다. */
    using EditorIconName = EditorListedName<EditorIconNameList>;
} // namespace sw::editor

/**
 * @brief 아이콘 이름 리터럴입니다(`EditorIconName`). 등록 줄 · 상수 표처럼 나중에 찾는 자리가 쓴다. 목록(`EditorIconNameList`)에 없는 이름은 컴파일되지 않는다.
 * @details 글이 아니라 형이다 — 이름을 단추 글로 넘기면(`ImGui::Button( 이름 )`) 컴파일되지 않는다. 예전에는 같은 `const utf8*` 라 출력 로그 설정 단추가 "Set",
 *          인스펙터 잠금 단추가 "Unl" 로 이름 글이 잘려 그려졌다(uiaudit 00). 그리는 쪽은 `EditorIconGlyphTable::findGlyphByName` 으로 글리프를 받는다.
 */
#define SW_EDITOR_ICON_NAME( NameLiteral ) ::sw::editor::EditorIconName::makeAt<::sw::editor::EditorIconName::findIndex( NameLiteral )>()
