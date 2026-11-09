/**
 * @file ChatWordFilter.h
 * @brief 금칙어 필터입니다. 아호-코라식 자동자(코드 포인트 단위)로 글을 한 번 훑어 걸린 구간을 가리거나 거절합니다.
 * @details 정규화는 `ChatTextNormalizer` 다(대소문자 · 전각, 건너뛸 글자는 데이터). 건너뛸 글자는 건너뛰고 맞추고, 가릴 때는 걸린 구간 안의 그 글자도 가린다
 *          (`b.a.d` 는 `*****` 가 된다).
 *          낱말도 같은 정규화를 거쳐 넣는다. 만든 뒤에는 읽기만 하므로 여러 스레드가 `apply` 를 함께 불러도 된다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatTextNormalizer.h"

namespace sw
{
    /** @brief 걸렸을 때 할 일입니다. */
    enum class ChatFilterMode : uint8
    {
        Mask = 0, ///< 걸린 코드 포인트를 '*' 로 가린다
        Reject    ///< 걸리면 글 전체를 거절한다
    };

    /** @brief 필터링 판정 결과입니다. */
    enum class ChatFilterResult : uint8
    {
        Clean = 0,
        Masked,
        Rejected,
        InvalidText ///< 잘못된 UTF-8 이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ChatWordFilter
     * @brief 금칙어 필터입니다.
     */
    class SW_KIT_SERVER_API ChatWordFilter
    {
    public:
        ChatWordFilter();

        /** @brief 낱말 목록으로 자동자를 만들고 들어간 낱말 수를 반환합니다. 빈 낱말, 건너뛸 글자만 있는 낱말, 잘못된 UTF-8 낱말은 버립니다. 정규화는 @p normalizer 를 복사해 둔다. */
        int32 initialize( const vector<string>& listWord, ChatFilterMode mode, const ChatTextNormalizer& normalizer );
        /**
         * @brief 텍스트 파일(한 줄에 낱말 하나, `#` 으로 시작하면 주석, UTF-8)을 읽어 `initialize` 합니다.
         * @return 파일을 읽지 못하면 false 를 반환하고 낱말 0개(거르지 않음)로 둡니다. 운영 서버는 기동할 때 이것을 오류로 다룹니다.
         */
        [[nodiscard]] bool loadFromFile( string_view path, ChatFilterMode mode, const ChatTextNormalizer& normalizer );

        /** @brief @p text 를 거릅니다. Masked 이면 @p outText 가 가린 글이고, Clean 이면 원문 그대로이며, Rejected 나 InvalidText 이면 건드리지 않습니다. */
        ChatFilterResult apply( string_view text, string& outText ) const;

        int32          getWordCount() const { return _wordCount; }
        ChatFilterMode getMode() const { return _mode; }

    private:
        /** @brief 자동자 노드입니다. 0번이 루트입니다. */
        struct Node
        {
            unordered_map<uint32, int32> _mapChild{};
            int32                        _fail{ 0 };
            int32                        _matchLength{ 0 }; ///< 여기서 끝나는 가장 긴 낱말의 길이이다. 정규화한 코드 포인트 수이며 실패 링크에서도 물려받는다
        };

        /** @brief @p nodeIndex 노드의 @p codepoint 자식 번호를 반환합니다. 없으면 비어 있습니다. */
        optional<int32> findChild( int32 nodeIndex, uint32 codepoint ) const;
        void            computeFailLinks();

    private:
        vector<Node>       _listNode;
        ChatTextNormalizer _normalizer;
        int32              _wordCount;
        ChatFilterMode     _mode;
    };
} // namespace sw
