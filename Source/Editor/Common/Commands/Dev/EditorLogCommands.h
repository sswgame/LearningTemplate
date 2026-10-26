/**
 * @file EditorLogCommands.h
 * @brief 출력 로그 행의 소스 위치 찾기, IDE 로 열기, 태그(카테고리) 필터링을 담당합니다. ImGui 에 의존하지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Process/Process.h"

namespace sw::editor
{
    /** @brief 파일 경로와 줄 번호입니다. 줄 번호는 1 부터 시작합니다. */
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
     * @brief 로그 행을 IDE 로 여는 판단과 명령을 ImGui 없이 만듭니다. `Test/EditorTest` 가 테스트합니다.
     */
    class EditorLogCommands
    {
    public:
        /** @brief IDE 명령 템플릿에서 파일 경로로 치환되는 자리 표시자입니다. */
        static constexpr const utf8* kFilePlaceholder = "{file}";
        /** @brief IDE 명령 템플릿에서 줄 번호로 치환되는 자리 표시자입니다. */
        static constexpr const utf8* kLinePlaceholder = "{line}";

        /**
         * @brief 문자열 안에서 첫 소스 위치를 찾습니다. `경로(줄)` 와 `경로(줄,열)`(MSVC, clang-cl), `경로:줄` 과 `경로:줄:열`(clang, gcc) 형식을 읽습니다.
         * @details 경로는 확장자(`.cpp`, `.hlsl`, `.xml` 등)로 끝나야 한다. 드라이브 문자(`D:\`)의 콜론은 숫자가 뒤따르지 않으므로 위치로 보지 않는다.
         * @return 찾으면 true 입니다.
         */
        [[nodiscard]] static bool parseSourceLocation( string_view text, EditorSourceLocation& outLocation );
        /**
         * @brief 로그 행 하나가 가리키는 위치를 구합니다. 메시지 안의 위치(컴파일 오류나 셰이더 오류가 남긴 것)를 먼저 쓰고, 없으면 로그를 쓴 위치를 씁니다.
         * @return 둘 다 없으면 false 입니다.
         */
        [[nodiscard]] static bool findLogEntryLocation( string_view message, string_view entryFile, int32 entryLine, EditorSourceLocation& outLocation );
        /**
         * @brief IDE 명령 템플릿(`code -g {file}:{line}`)을 인자로 나눈 **뒤에** 자리 표시자를 채웁니다. 템플릿이나 파일이 비어 있으면 실행 파일이 빈 명령입니다.
         * @details 나누기를 먼저 하므로 파일 경로의 공백 · 따옴표가 인자를 늘리거나 명령을 바꾸지 못한다(`ProcessCommand::splitArgumentLine`).
         */
        static ProcessCommand makeOpenCommand( string_view commandTemplate, const EditorSourceLocation& location );
        /**
         * @brief 쓸 IDE 명령 템플릿을 반환합니다. 환경설정 General 의 `_ideOpenCommand` 이고, 비어 있으면 플랫폼 기본값(VS Code)입니다.
         * @details Windows 의 `code` 는 `code.cmd` 이므로 `CreateProcess` 가 바로 실행하지 못한다. 그래서 `cmd /c` 를 붙인다.
         */
        static string getOpenCommandTemplate();
        /**
         * @brief 위치를 IDE 로 엽니다. 명령 템플릿은 환경설정 General 의 `_ideOpenCommand` 입니다.
         * @return 프로세스를 실행했으면 true 입니다.
         */
        [[nodiscard]] static bool openInIde( const EditorSourceLocation& location );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorLogTagFilter
     * @brief 출력 로그의 태그(로거 카테고리) 중 숨긴 것을 보관합니다. 태그는 대소문자를 구분하지 않습니다.
     */
    class EditorLogTagFilter
    {
    public:
        EditorLogTagFilter();

        /** @brief 태그를 보이거나 숨깁니다. */
        void setTagVisible( string_view tag, bool bVisible );
        /** @brief 태그가 보이면 true 입니다(숨긴 적 없는 태그는 보입니다). */
        bool isTagVisible( string_view tag ) const;
        /** @brief 숨긴 태그를 모두 다시 보이게 합니다. */
        void showAll();
        /** @brief 숨긴 태그 수입니다. */
        uint32 getHiddenCount() const { return static_cast<uint32>( _uniqueHiddenTag.size() ); }
        /** @brief 바뀔 때마다 올라가는 번호입니다. 필터링해 둔 목록을 다시 만들지 판단할 때 씁니다. */
        uint32 getRevision() const { return _revision; }

    private:
        unordered_set<string> _uniqueHiddenTag; ///< 소문자로 저장한 숨긴 태그
        uint32                _revision;
    };
} // namespace sw::editor
