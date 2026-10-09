/**
 * @file DevConsole.h
 * @brief 개발 콘솔의 판단(명령 해석 · 전역 변수 get/set · 자동완성 · 기록)입니다. 에디터 Output Log 의 입력 줄과 게임 창 오버레이가 같이 씁니다.
 */
#pragma once
#include "Engine/Console/DevCommandRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    class GlobalVariableManager;

    /** @brief 한 줄을 실행한 결과입니다. */
    enum class DevConsoleResult : uint8
    {
        Ok = 0,
        Empty,          ///< 빈 줄
        UnknownCommand, ///< 명령도 전역 변수도 아니다
        Failed          ///< 인자가 잘못됐거나 명령이 실패했다
    };
} // namespace sw

namespace sw
{
    /** @brief 콘솔 출력 한 줄입니다. */
    struct DevConsoleLine
    {
        string _text;
        uint8  _bError{ SW_FALSE }; ///< 실패한 명령의 답(오버레이가 색을 바꾼다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class DevConsole
     * @brief 개발 콘솔 한 개입니다(입력 기록 · 출력 줄을 듭니다).
     * @details 한 줄의 해석 순서 — ① `help [접두어]` · `set <변수> <값>` · `get <변수>` ② 개발 명령(`DevCommandRegistry`)
     *          ③ 전역 변수 이름으로 시작하면 값 하나는 읽기, 그 뒤가 있으면 쓰기(`gv_timeScale 0.5`). 쓰기는 `setValueFromString` 이라
     *          변경 콜백이 불린다(패널 · 명령줄과 같은 길). 답은 출력 줄에 쌓고 로그(`DevConsole`)에도 남긴다 — 에디터 Output Log 에 보인다.
     */
    class SW_API DevConsole
    {
    public:
        static constexpr uint32 kMaxHistoryCount = 64;
        static constexpr uint32 kMaxOutputCount  = 256;

        /** @param pVariables 전역 변수 표. nullptr 이면 엔진 서비스(`engine::getGlobalVariableManager`)를 씁니다. */
        explicit DevConsole( GlobalVariableManager* pVariables = nullptr );

        /** @brief 한 줄을 실행하고, 기록에 넣고, 입력 · 답을 출력 줄과 로그에 남깁니다. */
        DevConsoleResult submit( string_view line );
        /** @brief 한 줄을 실행해 답만 돌려줍니다(기록 · 출력 · 로그를 건드리지 않습니다). */
        DevConsoleResult execute( string_view line, string& outReply ) const;

        /** @brief 줄을 낱말로 나눕니다. 큰따옴표로 묶은 것은 공백이 있어도 한 낱말입니다. */
        static void tokenize( string_view line, vector<string>& outListToken );

        /**
         * @brief 줄의 마지막 낱말을 마칠 후보를 사전순으로 채웁니다. 첫 낱말은 내장 명령 · 개발 명령 · 전역 변수, `set`/`get` 뒤는 전역 변수,
         *        `help` 뒤는 개발 명령입니다.
         */
        void collectCompletions( string_view line, vector<string>& outListCandidate ) const;
        /**
         * @brief 자동완성(Tab)입니다. 후보가 하나면 그 낱말로 바꾸고 공백을 붙이며, 여럿이면 공통 접두어까지 늘립니다.
         * @return 줄이 바뀌었으면 true. @p outListCandidate 에 후보를 남깁니다(여럿일 때 보여 줄 것)
         */
        bool complete( string& inoutLine, vector<string>& outListCandidate ) const;

        /** @brief 기록에서 하나 앞(오래된 쪽)으로 갑니다. 더 없으면 nullptr 입니다. */
        const string* moveHistoryBack();
        /** @brief 기록에서 하나 뒤(새 쪽)로 갑니다. 끝을 지나면 빈 줄을 돌려줍니다. 기록 밖이면 nullptr 입니다. */
        const string* moveHistoryForward();
        /** @brief 기록 위치를 새 줄로 되돌립니다. */
        void resetHistoryCursor() { _historyCursor = static_cast<uint32>( _listHistory.size() ); }

        const vector<string>&         getHistory() const { return _listHistory; }
        const vector<DevConsoleLine>& getOutput() const { return _listOutput; }
        /** @brief 출력 줄을 비웁니다. */
        void clearOutput() { _listOutput.clear(); }
        /** @brief 실행 결과가 아닌 안내(자동완성 후보 등)를 출력 줄에 더합니다. */
        void appendNote( string_view text ) { appendOutput( text, false ); }

    private:
        /** @brief 전역 변수 표입니다. 없으면 nullptr 입니다. */
        GlobalVariableManager* getVariables() const;
        /** @brief 출력 줄을 더합니다(넘치면 오래된 것을 버립니다). */
        void appendOutput( string_view text, bool bError );

    private:
        GlobalVariableManager* _pVariables;
        vector<string>         _listHistory;
        vector<DevConsoleLine> _listOutput;
        uint32                 _historyCursor;
    };
} // namespace sw

#endif
