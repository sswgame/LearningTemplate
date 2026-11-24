/**
 * @file EditorIconName.h
 * @brief 에디터 아이콘 이름 형(`EditorIconName`)과 `SW_EDITOR_ICON_NAME` 입니다 — 등록 줄 · 상수 표가 아이콘 표(`EditorIconGlyphTable.h`)를 끌어오지 않고 이름을 든다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /**
     * @class EditorIconName
     * @brief 아이콘 이름입니다(글리프가 아니다 — 그릴 때 `EditorIconGlyphTable::findGlyphByName` 으로 바꾼다). `SW_EDITOR_ICON_NAME` 만 만든다.
     * @details 코드가 쓸 수 있는 이름은 `kArrName` 이 정한다 — 목록에 없는 리터럴은 컴파일되지 않는다. 표(`sweditoricons.glyphs.xml`)에 이 이름이 모두 있는지는 표를 읽을 때
     *          본다(`EditorIconGlyphTable::loadFromAsset` — 없으면 에디터가 서지 않는다). 예전에는 소스를 훑는 단위 테스트만 봐서, 그 테스트가 돌지 않은 묶음에서 편집기 플러그인의
     *          없는 이름 하나로 에디터 시나리오 25 개가 졌다. 데이터(명령 글 · 컴포넌트 아이콘 표 · 메타 `Icon=`)가 쓰는 이름은 표에서 찾는다.
     */
    class EditorIconName
    {
    public:
        /** @brief 코드가 쓸 수 있는 이름입니다. 아이콘을 글꼴과 표에 더하면 여기에도 더한다. */
        static constexpr const utf8* kArrName[] = {
            "File", "Folder", "FolderOpen", "Save", "Undo", "Redo", "Search", "Terminal", "Exit", "Settings", "Palette", "Layout", "Table", "Hammer",
            "Wrench", "BuildAll", "Cancel", "Spinner", "Check", "Close", "Success", "Warning", "Error", "Info", "Lock", "Unlock", "Plus", "Minus",
            "Trash", "Copy", "Refresh", "Filter", "Star", "Link", "Eye", "EyeSlash", "Play", "Pause", "Stop", "StepForward", "Translate", "Rotate",
            "Scale", "Globe", "Axes", "Magnet", "Grid", "Bookmark", "Align", "Chart", "Camera", "Cube", "Scene", "Prefab", "Texture", "Shader",
            "Material", "Audio", "AnimationGraph", "Dialogue", "Sprite", "TileMap", "Sequence", "Skeleton", "AnimationClip", "Rig", "Heightfield",
            "Fracture", "Widget", "Font", "GameObject", "Component", "LightPoint", "LightDirectional", "LightSpot", "LightGlobal", "AudioEmitter",
            "AudioListener", "AudioZone", "Physics", "Collider", "Character", "Animation", "Controller", "AI", "Navigation", "Trigger", "SpawnPoint",
            "Spline", "Water", "Foliage", "Wind", "Health", "Ability", "Vehicle", "Tag", "Missing", "Bug", "Map" };
        static constexpr uint32 kCount = static_cast<uint32>( SW_COUNT_OF( kArrName ) );

        constexpr EditorIconName() = default;

        /** @brief @p pName 의 번호입니다. 없으면 `kCount` 입니다. */
        static constexpr uint32 findIndex( const utf8* pName )
        {
            for ( uint32 index = 0; index < kCount; ++index )
            {
                uint32 cursor{ 0 };
                while ( kArrName[index][cursor] != 0 && kArrName[index][cursor] == pName[cursor] )
                    ++cursor;
                if ( kArrName[index][cursor] == pName[cursor] )
                    return index;
            }
            return kCount;
        }

        template <uint32 kIndex>
        static constexpr EditorIconName makeAt()
        {
            static_assert( kIndex < kCount, "the icon name is not in EditorIconName::kArrName - add the icon to the font, sweditoricons.glyphs.xml and the list" );
            return EditorIconName{ kArrName[kIndex] };
        }

        /** @brief 이름 글입니다. 비었으면 nullptr 입니다. */
        constexpr const utf8* c_str() const { return _pName; }
        constexpr bool        isEmpty() const { return _pName == nullptr; }

    private:
        constexpr explicit EditorIconName( const utf8* pName )
            : _pName{ pName }
        {
        }

    private:
        const utf8* _pName{ nullptr };
    };
} // namespace sw::editor

/**
 * @brief 아이콘 이름 리터럴입니다(`EditorIconName`). 등록 줄 · 상수 표처럼 나중에 찾는 자리가 쓴다. 목록(`EditorIconName::kArrName`)에 없는 이름은 컴파일되지 않는다.
 * @details 글이 아니라 형이다 — 이름을 단추 글로 넘기면(`ImGui::Button( 이름 )`) 컴파일되지 않는다. 예전에는 같은 `const utf8*` 라 출력 로그 설정 단추가 "Set",
 *          인스펙터 잠금 단추가 "Unl" 로 이름 글이 잘려 그려졌다(uiaudit 00). 그리는 쪽은 `EditorIconGlyphTable::findGlyphByName` 으로 글리프를 받는다.
 */
#define SW_EDITOR_ICON_NAME( NameLiteral ) ::sw::editor::EditorIconName::makeAt<::sw::editor::EditorIconName::findIndex( NameLiteral )>()
