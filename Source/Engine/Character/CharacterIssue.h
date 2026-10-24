/**
 * @file CharacterIssue.h
 * @brief 캐릭터 데이터 검사가 내는 문제의 모음입니다. 문제마다 종류 id 와 글을 따로 담습니다.
 * @details 검사 함수가 "오류 문장을 한 문자열에 이어 붙이는" 모양(`string* pOutError`)이면 종류가 글에 묻혀, 부르는 쪽이 로그 id 를 정할 수 없고 테스트는 글을 맞춰야 한다.
 *          이 모음은 문제마다 id(`"영역.사건"` 글자 상수)를 같이 받는다. `CharacterDataReader::addIssues` 가 id 를 그대로 로그에 단다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 문제 하나입니다. `_pID` 는 글자 상수입니다. */
    struct CharacterIssue
    {
        const utf8* _pID{ nullptr };
        string      _text;
    };
} // namespace sw

namespace sw
{
    /** @brief 검사가 모은 문제 목록입니다. */
    class CharacterIssueList
    {
    public:
        /** @param pID `"영역.사건"` 꼴의 **글자 상수**입니다(`SW_LOG_ERROR_ID` 와 같다). */
        void add( const utf8* pID, string text ) { _listIssue.push_back( { pID, std::move( text ) } ); }

        bool                          empty() const { return _listIssue.empty(); }
        const vector<CharacterIssue>& getIssues() const { return _listIssue; }

        /** @brief 글을 한 줄에 하나씩 이어 붙입니다 — 사람이 읽는 메시지에 붙일 때 씁니다. */
        string joined() const
        {
            string result;
            for ( const CharacterIssue& issue : _listIssue )
            {
                if ( result.empty() == false )
                    result += "\n";
                result += issue._text;
            }
            return result;
        }

    private:
        vector<CharacterIssue> _listIssue;
    };
} // namespace sw
