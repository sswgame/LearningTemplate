/**
 * @file ShaderBatchRecord.h
 * @brief 쿠킹이 한 소스의 변형 모두를 한 파일에 적는 컴파일 기록입니다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/CookJob.h"

namespace sw
{
    /**
     * @class ShaderBatchRecord
     * @brief 동작(변형) → 입력 목록 · 바이트코드 해시의 표입니다. 같은 입력 목록은 한 번만 듭니다.
     * @details 변형마다 기록 파일 하나(`<동작 키>.cookrec` — 입력 25 줄)를 두면 쿠킹 한 번이 작은 파일 수만 개를 쓰고, 무변경 재쿠킹이 그것을 모두 다시 연다. 한 소스의 변형은 거의 같은 헤더를
     *          열어 입력 목록이 몇 개뿐이라 한 파일이면 된다. 열쇠는 묶음의 동작 키 목록이다 — 변형이 하나 늘면 그 소스의 변형만 다시 전처리하고 바이트코드는 글 키로 맞는다.
     *          실행 중 컴파일 · 데우기는 요청마다의 기록을 쓴다(요청 하나를 묶음 없이 찾는다). 언리얼 셰이더 맵이 머티리얼 하나의 셰이더를 한 DDC 항목에 담는 것과 같은 자리다.
     */
    class SW_API ShaderBatchRecord
    {
    public:
        /** @brief 동작 하나의 결과입니다. */
        struct Entry
        {
            uint64 _bytecodeHash{ 0 };
            uint32 _inputListIndex{ 0 };
        };

        ShaderBatchRecord();

        /** @brief 동작 @p actionKey 의 결과를 더합니다. 내용이 같은 입력 목록이 이미 있으면 그것을 씁니다. */
        void add( uint64 actionKey, uint64 bytecodeHash, const vector<CookInput>& listInput );
        /** @brief 동작의 결과입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const Entry* find( uint64 actionKey ) const;
        /** @brief 입력 목록들입니다(`Entry::_inputListIndex` 의 자리). */
        [[nodiscard]] const vector<vector<CookInput>>& getInputLists() const { return _listInputList; }
        [[nodiscard]] size_t                           getEntryCount() const { return _mapEntry.size(); }

        /** @brief 기록을 바이트로 씁니다. 더한 차례와 무관하게 같은 동작 · 결과면 항목 순서가 같습니다. */
        void write( vector<uint8>& outBytes ) const;
        /** @brief `write` 의 바이트를 읽습니다. 잘렸거나 형식이 아니면 false 입니다. */
        [[nodiscard]] bool read( const vector<uint8>& bytes );

        /** @brief 묶음의 DDC 항목 서술입니다 — 동작 키 목록(정렬 · 중복 없음)의 해시. 동작 키에 도구 지문 · 소스 내용 · define 이 이미 든다. */
        [[nodiscard]] static string makeKeyDescription( vector<uint64> listActionKey );

    private:
        vector<vector<CookInput>>     _listInputList;
        unordered_map<uint64, uint32> _mapInputListIndex; ///< 입력 목록의 해시 → 번호(해시가 겹치면 내용을 견준다)
        unordered_map<uint64, Entry>  _mapEntry;
    };
} // namespace sw
