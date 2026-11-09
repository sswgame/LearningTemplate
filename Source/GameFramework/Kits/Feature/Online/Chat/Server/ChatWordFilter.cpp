#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatWordFilter.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ChatWordFilterInternal
        {
            static constexpr uint32 kReplacementCodepoint = 0xFFFDu;
            static constexpr uint32 kMaskCodepoint        = '*';

            /** @brief 글을 코드 포인트로 분해합니다. 잘못된 UTF-8 이면 false 를 반환하며, 글에 그대로 적힌 U+FFFD 는 받아들입니다. */
            static bool decode( string_view text, vector<uint32>& outListCodepoint )
            {
                size_t offset = 0;
                while ( offset < text.size() )
                {
                    const size_t start     = offset;
                    const uint32 codepoint = StringUtil::decodeUTF8( text, offset );
                    if ( codepoint == kReplacementCodepoint && text.substr( start, offset - start ) != "\xEF\xBF\xBD" )
                        return false;
                    outListCodepoint.push_back( codepoint );
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ChatWordFilter::ChatWordFilter()
        : _listNode{}
        , _wordCount{ 0 }
        , _mode{ ChatFilterMode::Mask }
    {
        _listNode.emplace_back(); // 루트이다. 낱말이 없어도 `apply` 가 0번 노드를 본다
    }

    optional<int32> ChatWordFilter::findChild( int32 nodeIndex, uint32 codepoint ) const
    {
        const unordered_map<uint32, int32>& mapChild = _listNode[static_cast<size_t>( nodeIndex )]._mapChild;
        const auto                          childIt  = mapChild.find( codepoint );
        return childIt != mapChild.end() ? optional<int32>( childIt->second ) : nullopt;
    }

    int32 ChatWordFilter::initialize( const vector<string>& listWord, ChatFilterMode mode, const ChatTextNormalizer& normalizer )
    {
        _normalizer = normalizer;
        _listNode.assign( 1, Node{} );
        _wordCount = 0;
        _mode      = mode;

        vector<uint32> listCodepoint;
        for ( const string& word : listWord )
        {
            listCodepoint.clear();
            if ( ChatWordFilterInternal::decode( word, listCodepoint ) == false )
                continue;
            int32 nodeIndex = 0;
            int32 length    = 0;
            for ( const uint32 rawCodepoint : listCodepoint )
            {
                const uint32 codepoint = _normalizer.normalize( rawCodepoint );
                if ( codepoint == 0 )
                    continue;
                const optional<int32> foundChild = findChild( nodeIndex, codepoint );
                const int32           childIndex = foundChild.value_or( static_cast<int32>( _listNode.size() ) );
                if ( foundChild.has_value() == false )
                {
                    _listNode.emplace_back();
                    _listNode[static_cast<size_t>( nodeIndex )]._mapChild.emplace( codepoint, childIndex );
                }
                nodeIndex = childIndex;
                ++length;
            }
            if ( length == 0 )
                continue;
            Node& node        = _listNode[static_cast<size_t>( nodeIndex )];
            node._matchLength = MathUtil::max( node._matchLength, length );
            ++_wordCount;
        }
        computeFailLinks();
        return _wordCount;
    }

    void ChatWordFilter::computeFailLinks()
    {
        // 너비 우선으로 처리한다. 부모의 실패 링크를 따라가며 같은 글자의 자식을 찾으며, 루트의 자식은 루트로 실패한다.
        vector<int32> listQueue;
        for ( const auto& [codepoint, childIndex] : _listNode[0]._mapChild )
        {
            _listNode[static_cast<size_t>( childIndex )]._fail = 0;
            listQueue.push_back( childIndex );
        }
        for ( size_t head = 0; head < listQueue.size(); ++head )
        {
            const int32 nodeIndex = listQueue[head];
            for ( const auto& [codepoint, childIndex] : _listNode[static_cast<size_t>( nodeIndex )]._mapChild )
            {
                int32 failIndex = _listNode[static_cast<size_t>( nodeIndex )]._fail;
                while ( failIndex != 0 && findChild( failIndex, codepoint ).has_value() == false )
                {
                    failIndex = _listNode[static_cast<size_t>( failIndex )]._fail;
                }
                Node& child = _listNode[static_cast<size_t>( childIndex )];
                child._fail = findChild( failIndex, codepoint ).value_or( 0 ); // 맞는 자식이 없으면 루트로 실패한다
                // 긴 낱말의 경로 위에 있는 짧은 낱말(abcd 안의 bc)은 실패 링크로만 보인다. 물려받아야 잡을 수 있다.
                child._matchLength = MathUtil::max( child._matchLength, _listNode[static_cast<size_t>( child._fail )]._matchLength );
                listQueue.push_back( childIndex );
            }
        }
    }

    bool ChatWordFilter::loadFromFile( string_view path, ChatFilterMode mode, const ChatTextNormalizer& normalizer )
    {
        string content;
        if ( FileUtil::readTextFile( path, content ) == false )
        {
            initialize( vector<string>{}, mode, normalizer );
            return false;
        }
        vector<string> listWord;
        StringUtil::forEachLine( content, [&]( string_view line )
        {
            line = StringUtil::trimEnd( line );
            if ( line.empty() == false && line.front() != '#' )
                listWord.emplace_back( line );
        } );
        initialize( listWord, mode, normalizer );
        return true;
    }

    ChatFilterResult ChatWordFilter::apply( string_view text, string& outText ) const
    {
        vector<uint32> listCodepoint;
        if ( ChatWordFilterInternal::decode( text, listCodepoint ) == false )
            return ChatFilterResult::InvalidText;

        // 건너뛸 글자를 건너뛴 정규화 글의 각 글자가 원문의 몇 번째 글자인지 기록한다. 걸린 구간을 원문 위치로 되돌리는 데 쓴다.
        vector<int32> listOriginalIndex;
        vector<uint8> listMasked( listCodepoint.size(), 0 );
        listOriginalIndex.reserve( listCodepoint.size() );
        int32 nodeIndex = 0;
        bool  bAnyMatch = false;
        for ( int32 index = 0; index < static_cast<int32>( listCodepoint.size() ); ++index )
        {
            const uint32 codepoint = _normalizer.normalize( listCodepoint[static_cast<size_t>( index )] );
            if ( codepoint == 0 )
                continue;
            listOriginalIndex.push_back( index );
            while ( nodeIndex != 0 && findChild( nodeIndex, codepoint ).has_value() == false )
            {
                nodeIndex = _listNode[static_cast<size_t>( nodeIndex )]._fail;
            }
            nodeIndex               = findChild( nodeIndex, codepoint ).value_or( 0 ); // 맞는 자식이 없으면 루트로 돌아간다
            const int32 matchLength = _listNode[static_cast<size_t>( nodeIndex )]._matchLength;
            if ( matchLength == 0 )
                continue;
            bAnyMatch                 = true;
            const int32 filteredLast  = static_cast<int32>( listOriginalIndex.size() ) - 1;
            const int32 originalFirst = listOriginalIndex[static_cast<size_t>( filteredLast - matchLength + 1 )];
            for ( int32 maskIndex = originalFirst; maskIndex <= index; ++maskIndex )
            {
                listMasked[static_cast<size_t>( maskIndex )] = 1;
            }
        }

        if ( bAnyMatch == false )
        {
            outText.assign( text.data(), text.size() );
            return ChatFilterResult::Clean;
        }
        if ( _mode == ChatFilterMode::Reject )
            return ChatFilterResult::Rejected;
        outText.clear();
        for ( size_t index = 0; index < listCodepoint.size(); ++index )
        {
            StringUtil::appendUTF8( outText, listMasked[index] != 0 ? ChatWordFilterInternal::kMaskCodepoint : listCodepoint[index] );
        }
        return ChatFilterResult::Masked;
    }
} // namespace sw
