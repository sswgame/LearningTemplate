/**
 * @file EditorCommandText.h
 * @brief 에디터 명령의 표시 · 배치 데이터(라벨 · 아이콘 · 분류 · 툴팁 · 설명 · 단축키 액션 · 메뉴 · 맥락 메뉴 · 팔레트)입니다. 코드는 id 와 동작만 든다.
 * @details 명령 하나는 코드의 동작(`SW_EDITOR_COMMAND` · 명령 표)과 데이터의 한 줄(`*.commands.xml`)이 id 로 짝을 이룬다. 모듈마다 자기 파일을 등록한다
 *          (`SW_EDITOR_COMMAND_TEXT`, 입력 맵의 `SW_EDITOR_INPUT_MAP` 과 같은 모양). 짝이 없는 쪽(코드만 · 데이터만 있는 id)은 오류다.
 *          언리얼 `FUICommandInfo` 는 라벨을 코드(`UI_COMMAND`)에 두지만, 이 저장소는 정책 · 표시 값을 데이터에 둔다(설정 파일 규칙).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/EditorCommandTarget.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorExtensionRegistry.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    struct EditorCommandDesc;

    /**
     * @brief 명령 하나의 표시 · 배치입니다. 칸은 모두 적는다(빈 글은 "없음").
     * @code
     *     <EditorCommandText _id="scene.new" _label="New Scene" _icon="File" _category="Scene" _tooltip="..." _detail="..." _inputAction=""
     *                        _menuPath="MainMenu/File" _menuOrder="1100" _contextMenu="None" _contextMenuOrder="0" _bPaletteVisible="true" />
     * @endcode
     */
    REFLECT()
    struct EditorCommandText
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Command id the code registers (scene.save)" )
        string _id{};
        PROPERTY( Required, Meta = "Localizable", Tooltip = "Name in menus and the palette (English source - the text is its localization key)" )
        string _label{};
        PROPERTY( Required, Tooltip = "Icon name in the editor icon table (empty = no icon)" )
        string _icon{};
        PROPERTY( Required, Tooltip = "Palette group" )
        string _category{};
        PROPERTY( Required, Meta = "Localizable", Tooltip = "Menu item tooltip (English source - the text is its localization key)" )
        string _tooltip{};
        PROPERTY( Required, Meta = "Localizable", Tooltip = "Palette detail text (English source - the text is its localization key)" )
        string _detail{};
        PROPERTY( Required, Tooltip = "Editor input map action that runs it (empty = no shortcut)" )
        string _inputAction{};
        PROPERTY( Required, Tooltip = "Menu path (MainMenu/File); empty = in no menu" )
        string _menuPath{};
        PROPERTY( Required, Tooltip = "Order inside the menu; a new hundred starts a separator group" )
        int32 _menuOrder{ 0 };
        PROPERTY( Required, Tooltip = "Context menu kind it sits in (None = none)" )
        EditorCommandTargetKind _contextMenu{ EditorCommandTargetKind::None };
        PROPERTY( Required, Tooltip = "Order inside the context menu" )
        int32 _contextMenuOrder{ 0 };
        PROPERTY( Required, Tooltip = "Whether the command palette lists it" )
        bool _bPaletteVisible{ true };
    };

    /** @brief 데이터가 만드는 메뉴 하나의 보이는 이름입니다(`_menuPath` 의 마디는 ID 라 번역하지 않는다 — 이름은 이 줄이 든다). */
    REFLECT()
    struct EditorMenuText
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Menu path the command rows use (MainMenu/File)" )
        string _path{};
        PROPERTY( Required, Meta = "Localizable", Tooltip = "Name shown in the menu bar (English source - the text is its localization key)" )
        string _label{};
    };

    /** @brief 명령 표시 파일 한 벌(`*.commands.xml`)입니다. */
    REFLECT()
    struct EditorCommandTextTable
    {
        REFLECT_BODY();
        PROPERTY( Required, Tooltip = "Commands of the module" )
        vector<EditorCommandText> _listCommand;
        PROPERTY( Tooltip = "Names of the menus this file's rows open (a module that only adds items to an existing menu has none)" )
        vector<EditorMenuText> _listMenu;
    };

    /** @brief 모듈 하나가 명령 표시 파일을 더하는 등록 줄입니다(`SW_EDITOR_COMMAND_TEXT`). */
    struct EditorCommandTextRegistration : EditorExtensionRegistration
    {
        SW_EDITOR_EXTENSION_KIND( "commandText" );

        const utf8* _pAssetPath; ///< 리소스 경로(`editor/commands/editor.commands.xml`)
    };

    /**
     * @class EditorCommandTextSet
     * @brief 등록된 표시 파일을 모두 읽어 명령 설명에 채웁니다. 명령 목록을 다시 만들 때마다 새로 만든다(모듈 적재 · 해제로 파일이 바뀐다).
     */
    class SW_EDITOR_API EditorCommandTextSet
    {
    public:
        /** @brief 등록된 파일을 모두 엄격하게 읽습니다. 못 읽은 파일 · 두 번 나온 id 는 오류를 남기고 건너뜁니다. */
        void loadRegistered();
        /** @brief @p inoutDesc 의 id 줄로 표시 · 배치 칸을 채웁니다. 줄이 없으면 오류를 남기고 false 입니다(그 명령은 등록하지 않는다). */
        [[nodiscard]] bool apply( EditorCommandDesc& inoutDesc );
        /** @brief 어느 코드 명령도 쓰지 않은 줄을 오류로 알립니다(지운 명령의 남은 줄 · id 오타). */
        void reportUnused() const;
        /** @brief 메뉴 @p menuPath 의 보이는 이름(지금 언어)을 @p outLabel 에 담습니다. 이름 줄이 없으면 false 입니다. */
        [[nodiscard]] bool findMenuLabel( string_view menuPath, string& outLabel ) const;

    private:
        vector<EditorCommandText> _listRow;
        vector<uint8>             _listUsed;
        vector<EditorMenuText>    _listMenu;
    };
} // namespace sw::editor

/**
 * @brief 모듈의 명령 표시 파일 하나를 등록합니다. 그 모듈의 명령 .cpp 하나에 한 줄 둡니다.
 * @code SW_EDITOR_COMMAND_TEXT( ParkCommands, "themepark.commands", "editor/commands/themepark.commands.xml" ); @endcode
 */
#define SW_EDITOR_COMMAND_TEXT( name, pID, pAssetPath ) SW_EDITOR_REGISTER( ::sw::editor::EditorCommandTextRegistration, CommandText_##name, { pID, 0 }, pAssetPath )
