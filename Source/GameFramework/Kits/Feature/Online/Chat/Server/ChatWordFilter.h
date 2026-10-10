/**
 * @file ChatWordFilter.h
 * @brief 금칙어 거르개 — 아호-코라식 자동자(코드 포인트 단위)로 글을 한 번 훑어 걸린 구간을 가리거나 거절합니다.
 * @details 정규화: ASCII 대문자 → 소문자, 전각 ASCII(U+FF01..FF5E) → 반각. 끼움 글자(공백 · . , - _ * ~ ! | · 전각 공백)는 건너뛰고 맞추되,
 *          가릴 때는 걸린 구간 안의 끼움 글자도 가린다(`b.a.d` → `*****`). 낱말도 같은 정규화를 거쳐 넣는다.
 *          만든 뒤에는 읽기만 하므로 여러 스레드가 `apply` 를 함께 불러도 된다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 걸렸을 때 할 일입니다. */
    enum class ChatFilterMode : uint8
    {
        Mask = 0, ///< 걸린 코드 포인트를 '*' 로
        Reject    ///< 걸리면 글 전체를 거절
    };

    /** @brief 거르기 판정입니다. */
    enum class ChatFilterVerdict : uint8
    {
        Clean = 0,
        Masked,
        Rejected,
        InvalidText ///< 잘못된 UTF-8
    };
} // namespace sw

namespace sw
{
    /**
     * @class ChatWordFilter
     * @brief 금칙어 거르개입니다.
     */
    class SW_GF_API ChatWordFilter
    {
    public:
        ChatWordFilter();

        /** @brief 낱말 목록으로 자동자를 만들고 들어간 낱말 수를 돌려줍니다. 빈 낱말 · 끼움 글자만인 낱말 · 잘못된 UTF-8 낱말은 버립니다. */
        int32 initialize( const vector<string>& listWord, ChatFilterMode mode );
        /**
         * @brief 글 파일(한 줄 한 낱말, `#` 로 시작하면 주석, UTF-8)을 읽어 `initialize` 합니다.
         * @return 파일을 읽지 못하면 false — 낱말 0 개(거르지 않음)로 둔다. 운영 서버는 기동 때 이것을 오류로 다룬다.
         */
        [[nodiscard]] bool loadFile( string_view path, ChatFilterMode mode );

        /** @brief @p text 를 거릅니다. Masked 면 @p outText 가 가린 글, Clean 이면 원문 그대로이고, Rejected · InvalidText 면 건드리지 않습니다. */
        ChatFilterVerdict apply( string_view text, string& outText ) const;

        int32          getWordCount() const { return _wordCount; }
        ChatFilterMode getMode() const { return _mode; }

        /** @brief 정규화한 한 글자입니다. 끼움 글자면 0 입니다(도배 막이의 반복 해시도 같은 정규화를 쓴다). */
        static uint32 normalizeCodepoint( uint32 codepoint );

    private:
        /** @brief 자동자 노드입니다. 0 번이 뿌리입니다. */
        struct Node
        {
            unordered_map<uint32, int32> _mapChild{};
            int32                        _fail{ 0 };
            int32                        _matchLength{ 0 }; ///< 여기서 끝나는 가장 긴 낱말의 길이(정규화 코드 포인트 수, 실패 고리에서도 물려받는다)
        };

        /** @brief @p nodeIndex 의 @p codepoint 자식 번호입니다. 없으면 -1. */
        int32 findChild( int32 nodeIndex, uint32 codepoint ) const;
        void  computeFailLinks();

        vector<Node>   _listNode;
        int32          _wordCount;
        ChatFilterMode _mode;
    };
} // namespace sw
