#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatWordFilter.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ChatWordFilterInternal
        {
            static constexpr uint32 kReplacementCodepoint = 0xFFFDu;
            static constexpr uint32 kMaskCodepoint        = '*';

            /** @brief 글을 코드 포인트로 풉니다. 잘못된 UTF-8 이면 false(글에 그대로 적힌 U+FFFD 는 받는다). */
            static bool decode( string_view text, vector<uint32>& outListCodepoint )
            {
                size_t offset = 0;
                while ( offset < text.size() )
                {
                    const size_t start     = offset;
                    const uint32 codepoint = StringUtil::decodeUtf8( text, offset );
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
        _listNode.emplace_back(); // 뿌리 — 낱말이 없어도 `apply` 가 0 번 노드를 본다
    }

    uint32 ChatWordFilter::normalizeCodepoint( uint32 codepoint )
    {
        if ( 0xFF01u <= codepoint && codepoint <= 0xFF5Eu )
            codepoint -= 0xFEE0u; // 전각 ASCII → 반각
        if ( 'A' <= codepoint && codepoint <= 'Z' )
            return codepoint - 'A' + 'a';
        switch ( codepoint )
        {
            case ' ':
            case '.':
            case ',':
            case '-':
            case '_':
            case '*':
            case '~':
            case '!':
            case '|':
            case 0x3000u: // 전각 공백
                return 0;
            default:
                return codepoint;
        }
    }

    int32 ChatWordFilter::findChild( int32 nodeIndex, uint32 codepoint ) const
    {
        const unordered_map<uint32, int32>& mapChild = _listNode[static_cast<size_t>( nodeIndex )]._mapChild;
        const auto                          childIt  = mapChild.find( codepoint );
        return childIt != mapChild.end() ? childIt->second : -1;
    }

    int32 ChatWordFilter::initialize( const vector<string>& listWord, ChatFilterMode mode )
    {
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
                const uint32 codepoint = normalizeCodepoint( rawCodepoint );
                if ( codepoint == 0 )
                    continue;
                int32 childIndex = findChild( nodeIndex, codepoint );
                if ( childIndex < 0 )
                {
                    childIndex = static_cast<int32>( _listNode.size() );
                    _listNode.emplace_back();
                    _listNode[static_cast<size_t>( nodeIndex )]._mapChild.emplace( codepoint, childIndex );
                }
                nodeIndex = childIndex;
                ++length;
            }
            if ( length == 0 )
                continue;
            Node& node        = _listNode[static_cast<size_t>( nodeIndex )];
            node._matchLength = std::max( node._matchLength, length );
            ++_wordCount;
        }
        buildFailLinks();
        return _wordCount;
    }

    void ChatWordFilter::buildFailLinks()
    {
        // 너비 우선 — 부모의 실패 고리를 따라가며 같은 글자의 자식을 찾는다. 뿌리의 자식은 뿌리로 실패한다.
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
                while ( failIndex != 0 && findChild( failIndex, codepoint ) < 0 )
                {
                    failIndex = _listNode[static_cast<size_t>( failIndex )]._fail;
                }
                const int32 failChild = findChild( failIndex, codepoint );
                Node&       child     = _listNode[static_cast<size_t>( childIndex )];
                child._fail           = failChild >= 0 ? failChild : 0;
                // 긴 낱말의 길 위에 있는 짧은 낱말(abcd 안의 bc)은 실패 고리로만 보인다 — 물려받아야 잡힌다.
                child._matchLength = std::max( child._matchLength, _listNode[static_cast<size_t>( child._fail )]._matchLength );
                listQueue.push_back( childIndex );
            }
        }
    }

    bool ChatWordFilter::loadFile( string_view path, ChatFilterMode mode )
    {
        string content;
        if ( FileUtil::readTextFile( path, content ) == false )
        {
            initialize( vector<string>{}, mode );
            return false;
        }
        vector<string> listWord;
        size_t         lineStart = 0;
        while ( lineStart < content.size() )
        {
            size_t lineEnd = content.find( '\n', lineStart );
            if ( lineEnd == string::npos )
                lineEnd = content.size();
            string_view line( content.data() + lineStart, lineEnd - lineStart );
            line = StringUtil::trimEnd( line );
            if ( line.empty() == false && line.front() != '#' )
                listWord.emplace_back( line );
            lineStart = lineEnd + 1;
        }
        initialize( listWord, mode );
        return true;
    }

    ChatFilterVerdict ChatWordFilter::apply( string_view text, string& outText ) const
    {
        vector<uint32> listCodepoint;
        if ( ChatWordFilterInternal::decode( text, listCodepoint ) == false )
            return ChatFilterVerdict::InvalidText;

        // 끼움 글자를 건너뛴 정규화 글의 각 글자가 원문 몇째 글자인지 — 걸린 구간을 원문 자리로 되돌린다.
        vector<int32> listOriginalIndex;
        vector<uint8> listMasked( listCodepoint.size(), 0 );
        listOriginalIndex.reserve( listCodepoint.size() );
        int32 nodeIndex = 0;
        bool  bAnyMatch = false;
        for ( int32 index = 0; index < static_cast<int32>( listCodepoint.size() ); ++index )
        {
            const uint32 codepoint = normalizeCodepoint( listCodepoint[static_cast<size_t>( index )] );
            if ( codepoint == 0 )
                continue;
            listOriginalIndex.push_back( index );
            while ( nodeIndex != 0 && findChild( nodeIndex, codepoint ) < 0 )
            {
                nodeIndex = _listNode[static_cast<size_t>( nodeIndex )]._fail;
            }
            const int32 childIndex  = findChild( nodeIndex, codepoint );
            nodeIndex               = childIndex >= 0 ? childIndex : 0;
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
            return ChatFilterVerdict::Clean;
        }
        if ( _mode == ChatFilterMode::Reject )
            return ChatFilterVerdict::Rejected;
        outText.clear();
        for ( size_t index = 0; index < listCodepoint.size(); ++index )
        {
            StringUtil::appendUtf8( outText, listMasked[index] != 0 ? ChatWordFilterInternal::kMaskCodepoint : listCodepoint[index] );
        }
        return ChatFilterVerdict::Masked;
    }
} // namespace sw
