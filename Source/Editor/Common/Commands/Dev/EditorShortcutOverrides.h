/**
 * @file EditorShortcutOverrides.h
 * @brief 사용자가 바꾼 커맨드 단축키입니다(`Saved/Editor/Shortcuts.json`, 기본과 다른 커맨드만 — ImGui 없음).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /** @brief 커맨드 하나의 사용자 단축키입니다. */
    struct EditorShortcutOverride
    {
        string                _commandID;
        EditorCommandShortcut _shortcut;
        EditorCommandShortcut _altShortcut;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorShortcutOverrides
     * @brief 사용자가 바꾼 단축키입니다(기본과 다른 커맨드만). 파일은 `{ "edit.undo": [ "Ctrl+U", "" ] }` 꼴입니다(첫 값이 주 조합, 둘째가 보조, "" 는 없음).
     * @details 커맨드 레지스트리를 만든 뒤(표 + 등록 줄) 입힙니다. 없는 커맨드 id 는 남겨 둡니다 — 확장 모듈을 다시 켜면 돌아옵니다(유니티 Shortcuts Manager 와 같다).
     */
    class SW_EDITOR_API EditorShortcutOverrides
    {
    public:
        /** @brief 파일을 읽습니다(앞 내용은 버린다). 파일이 없으면 false 입니다(처음 — 정상). 형식이 틀린 줄은 경고하고 건너뜁니다. */
        [[nodiscard]] bool loadFromFile( string_view filePath );
        /** @brief 파일에 씁니다. 덮어쓰기가 없으면 빈 오브젝트입니다. */
        [[nodiscard]] bool saveToFile( string_view filePath ) const;
        /** @brief 레지스트리의 커맨드에 덮어쓰기를 입힙니다. 입힌 수입니다. */
        uint32 applyTo( EditorCommandRegistry& registry ) const;
        /** @brief @p commandID 의 덮어쓰기를 둡니다. 기본(@p defaultShortcut · @p defaultAlt)과 같으면 지웁니다. */
        void                                  setOverride( string_view commandID, const EditorCommandShortcut& shortcut, const EditorCommandShortcut& altShortcut,
                                                           const EditorCommandShortcut& defaultShortcut, const EditorCommandShortcut& defaultAlt );
        void                                  removeOverride( string_view commandID );
        void                                  clear() { _listOverride.clear(); }
        const vector<EditorShortcutOverride>& getOverrides() const { return _listOverride; }

        /** @brief "Ctrl+Shift+B" 를 읽습니다(`formatShortcutLabel` 의 짝). 빈 글은 None 입니다. 모르는 키 이름이면 false 입니다. */
        [[nodiscard]] static bool parseShortcut( string_view text, EditorCommandShortcut& outShortcut );
        /** @brief 조합 하나를 "Ctrl+U" 글로 씁니다. None 이면 빈 글입니다. */
        static string formatShortcut( const EditorCommandShortcut& shortcut );
        /** @brief 저장 파일 경로입니다(에디터 상태 폴더의 `Shortcuts.json`). */
        static string getDefaultFilePath();
        /** @brief 저장 파일 이름입니다. */
        static constexpr const utf8* kFileName = "Shortcuts.json";

    private:
        vector<EditorShortcutOverride> _listOverride;
    };
} // namespace sw::editor
