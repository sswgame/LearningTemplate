/**
 * @file EditorLogCommands.h
 * @brief 출력 로그 줄의 소스 위치 찾기 · IDE 로 열기 · 태그(카테고리) 걸러 보기입니다(ImGui 없음).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"

namespace sw::editor
{
    /** @brief 파일 경로와 줄 번호입니다. 줄은 1 부터입니다. */
    struct EditorSourceLocation
    {
        string _file{};
        uint32 _line{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorLogCommands
     * @brief 로그 줄을 IDE 로 여는 판단과 명령을 ImGui 없이 만듭니다(`Test/EditorTest` 가 시험합니다).
     */
    class EditorLogCommands
    {
    public:
        /** @brief IDE 명령 틀에서 파일 경로로 바뀌는 자리입니다. */
        static constexpr const utf8* kFilePlaceholder = "{file}";
        /** @brief IDE 명령 틀에서 줄 번호로 바뀌는 자리입니다. */
        static constexpr const utf8* kLinePlaceholder = "{line}";

        /**
         * @brief 글 안에서 첫 소스 위치를 찾습니다. `경로(줄)` · `경로(줄,열)`(MSVC · clang-cl) · `경로:줄` · `경로:줄:열`(clang · gcc)을 읽습니다.
         * @details 경로는 확장자(`.cpp` · `.hlsl` · `.xml` …)로 끝나야 합니다. 드라이브 문자(`D:\`)의 콜론은 숫자가 뒤따르지 않으므로 위치로 보지 않습니다.
         * @return 찾으면 true
         */
        [[nodiscard]] static bool parseSourceLocation( string_view text, EditorSourceLocation& outLocation );
        /**
         * @brief 로그 줄 하나가 가리키는 위치입니다 — 메시지 안의 위치(컴파일 오류 · 셰이더 오류가 남긴 것)가 먼저, 없으면 로그를 쓴 자리입니다.
         * @return 둘 다 없으면 false
         */
        [[nodiscard]] static bool findLogEntryLocation( string_view message, string_view entryFile, int32 entryLine, EditorSourceLocation& outLocation );
        /** @brief IDE 명령 틀(`code -g {file}:{line}`)의 자리를 채웁니다. 틀이 비면 빈 문자열입니다. */
        static string makeOpenCommand( string_view commandTemplate, const EditorSourceLocation& location );
        /**
         * @brief 쓸 IDE 명령 틀입니다 — 환경설정 General 의 `_ideOpenCommand`, 비었으면 플랫폼 기본값(VS Code)입니다.
         * @details Windows 의 `code` 는 `code.cmd` 라 `CreateProcess` 가 바로 띄우지 못해 `cmd /c` 를 붙입니다.
         */
        static string getOpenCommandTemplate();
        /**
         * @brief 위치를 IDE 로 엽니다. 명령 틀은 환경설정 General 의 `_ideOpenCommand` 입니다.
         * @return 프로세스를 띄웠으면 true
         */
        [[nodiscard]] static bool openInIde( const EditorSourceLocation& location );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorLogTagFilter
     * @brief 출력 로그의 태그(로거 카테고리) 중 숨긴 것을 듭니다. 태그는 대소문자를 가리지 않습니다.
     */
    class EditorLogTagFilter
    {
    public:
        EditorLogTagFilter();

        /** @brief 태그를 보이거나 숨깁니다. */
        void setTagVisible( string_view tag, bool bVisible );
        /** @brief 태그가 보이면 true 입니다(숨긴 적 없는 태그는 보입니다). */
        bool isTagVisible( string_view tag ) const;
        /** @brief 숨긴 태그를 모두 다시 보입니다. */
        void showAll();
        /** @brief 숨긴 태그 수입니다. */
        uint32 getHiddenCount() const { return static_cast<uint32>( _uniqueHiddenTag.size() ); }
        /** @brief 바뀔 때마다 오르는 번호입니다. 걸러 둔 목록을 다시 만들지 판단할 때 씁니다. */
        uint32 getRevision() const { return _revision; }

    private:
        unordered_set<string> _uniqueHiddenTag; ///< 소문자로 적은 숨긴 태그
        uint32                _revision;
    };
} // namespace sw::editor
