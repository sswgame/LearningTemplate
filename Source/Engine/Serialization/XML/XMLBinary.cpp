#include "pch.h"

#include "Engine/Serialization/XML/XMLBinary.h"

#include "Core/Container/unordered_map.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include <cstring>

namespace sw
{
    namespace
    {
        struct XMLBinaryInternal
        {
            /** @brief 굽는 동안의 표입니다. 같은 글은 한 번만 담는다. */
            struct Builder
            {
                vector<XMLBinaryNode>         _listNode{};
                vector<XMLBinaryAttribute>    _listAttribute{};
                vector<utf8>                  _strings{};
                unordered_map<string, uint32> _mapString{};

                uint32 addString( const utf8* pText )
                {
                    const string text( pText != nullptr ? pText : "" );
                    const auto   found = _mapString.find( text );
                    if ( found != _mapString.end() )
                        return found->second;
                    const uint32 offset = static_cast<uint32>( _strings.size() );
                    _strings.insert( _strings.end(), text.c_str(), text.c_str() + text.size() + 1 );
                    _mapString.emplace( text, offset );
                    return offset;
                }

                uint32 addNode( const XMLNode& source )
                {
                    const uint32 index = static_cast<uint32>( _listNode.size() );
                    _listNode.push_back( XMLBinaryNode{ 0, addString( source.getName() ), addString( source.getText() ), XMLBinary::kNone, XMLBinary::kNone, XMLBinary::kNone } );
                    uint32 previousAttribute = XMLBinary::kNone;
                    for ( XMLAttribute attribute = source.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                    {
                        const uint32 attributeIndex = static_cast<uint32>( _listAttribute.size() );
                        _listAttribute.push_back( XMLBinaryAttribute{ 0, addString( attribute.getName() ), addString( attribute.getValue() ), XMLBinary::kNone } );
                        if ( previousAttribute == XMLBinary::kNone )
                            _listNode[index]._firstAttribute = attributeIndex;
                        else
                            _listAttribute[previousAttribute]._next = attributeIndex;
                        previousAttribute = attributeIndex;
                    }
                    uint32 previousChild = XMLBinary::kNone;
                    for ( XMLNode child = source.findChild(); child.isValid(); child = child.findNextSibling() )
                    {
                        if ( child.isElement() == false )
                            continue; // 글 노드는 원소의 글로 담았다
                        const uint32 childIndex = addNode( child );
                        if ( previousChild == XMLBinary::kNone )
                            _listNode[index]._firstChild = childIndex;
                        else
                            _listNode[previousChild]._nextSibling = childIndex;
                        previousChild = childIndex;
                    }
                    return index;
                }
            };

            template <typename TRecord>
            [[nodiscard]] static const XMLBinaryHeader& getHeader( const TRecord& record )
            {
                return *reinterpret_cast<const XMLBinaryHeader*>( reinterpret_cast<const uint8*>( &record ) - record._headerDistance );
            }

            [[nodiscard]] static const XMLBinaryNode*      getNodes( const XMLBinaryHeader& header ) { return reinterpret_cast<const XMLBinaryNode*>( &header + 1 ); }
            [[nodiscard]] static const XMLBinaryAttribute* getAttributes( const XMLBinaryHeader& header )
            {
                return reinterpret_cast<const XMLBinaryAttribute*>( getNodes( header ) + header._nodeCount );
            }
            [[nodiscard]] static const utf8* getStrings( const XMLBinaryHeader& header ) { return reinterpret_cast<const utf8*>( getAttributes( header ) + header._attributeCount ); }

            /** @brief 원소 하나를 글 문서에 옮깁니다(되살리기). */
            static void copyNode( const XMLBinaryNode& source, const XMLNode& target )
            {
                for ( const XMLBinaryAttribute* pAttribute = XMLBinary::findAttribute( source ); pAttribute != nullptr; pAttribute = XMLBinary::findNextAttribute( *pAttribute ) )
                    target.appendAttribute( XMLBinary::getText( *pAttribute, pAttribute->_name ), XMLBinary::getText( *pAttribute, pAttribute->_value ) );
                const utf8* pText = XMLBinary::getText( source, source._text );
                if ( pText[0] != 0 )
                    target.setValue( pText );
                for ( const XMLBinaryNode* pChild = XMLBinary::findNode( source, source._firstChild ); pChild != nullptr; pChild = XMLBinary::findNode( *pChild, pChild->_nextSibling ) )
                    copyNode( *pChild, target.appendChild( XMLBinary::getText( *pChild, pChild->_name ) ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    static_assert( sizeof( XMLBinaryHeader ) == 5 * sizeof( uint32 ) && sizeof( XMLBinaryNode ) == 6 * sizeof( uint32 ) && sizeof( XMLBinaryAttribute ) == 4 * sizeof( uint32 ),
                   "the cooked document is its records in memory - no padding" );

    bool XMLBinary::isBinary( string_view bytes )
    {
        uint32 magic = 0;
        if ( bytes.size() < sizeof( XMLBinaryHeader ) )
            return false;
        std::memcpy( &magic, bytes.data(), sizeof( magic ) );
        return magic == kMagic;
    }

    bool XMLBinary::encode( const XMLNode& root, vector<uint8>& outBytes )
    {
        outBytes.clear();
        if ( root.isElement() == false )
            return false;
        XMLBinaryInternal::Builder builder;
        (void)builder.addNode( root );
        const size_t nodeBytes      = builder._listNode.size() * sizeof( XMLBinaryNode );
        const size_t attributeBytes = builder._listAttribute.size() * sizeof( XMLBinaryAttribute );
        for ( size_t index = 0; index < builder._listNode.size(); ++index )
            builder._listNode[index]._headerDistance = static_cast<uint32>( sizeof( XMLBinaryHeader ) + index * sizeof( XMLBinaryNode ) );
        for ( size_t index = 0; index < builder._listAttribute.size(); ++index )
            builder._listAttribute[index]._headerDistance = static_cast<uint32>( sizeof( XMLBinaryHeader ) + nodeBytes + index * sizeof( XMLBinaryAttribute ) );
        const XMLBinaryHeader header{ kMagic, kVersion, static_cast<uint32>( builder._listNode.size() ), static_cast<uint32>( builder._listAttribute.size() ),
                                      static_cast<uint32>( builder._strings.size() ) };
        outBytes.resize( sizeof( header ) + nodeBytes + attributeBytes + builder._strings.size() );
        uint8* pOut = outBytes.data();
        std::memcpy( pOut, &header, sizeof( header ) );
        pOut += sizeof( header );
        std::memcpy( pOut, builder._listNode.data(), nodeBytes );
        pOut += nodeBytes;
        if ( attributeBytes > 0 )
            std::memcpy( pOut, builder._listAttribute.data(), attributeBytes );
        pOut += attributeBytes;
        std::memcpy( pOut, builder._strings.data(), builder._strings.size() );
        return true;
    }

    bool XMLBinary::validate( string_view bytes )
    {
        if ( isBinary( bytes ) == false || reinterpret_cast<uintptr_t>( bytes.data() ) % alignof( XMLBinaryHeader ) != 0 )
            return false;
        const XMLBinaryHeader& header        = *reinterpret_cast<const XMLBinaryHeader*>( bytes.data() );
        const uint64           expectedBytes = sizeof( XMLBinaryHeader ) + uint64{ header._nodeCount } * sizeof( XMLBinaryNode ) + uint64{ header._attributeCount } * sizeof( XMLBinaryAttribute ) +
                                     header._stringBytes;
        if ( header._version != kVersion || header._nodeCount == 0 || header._stringBytes == 0 || expectedBytes != bytes.size() || bytes.back() != 0 )
            return false;
        const XMLBinaryNode*      pNodes      = XMLBinaryInternal::getNodes( header );
        const XMLBinaryAttribute* pAttributes = XMLBinaryInternal::getAttributes( header );
        const auto                isLater     = []( uint32 target, uint32 self, uint32 count )
        { return target == kNone || ( self < target && target < count ); }; // 뒤만 가리킨다 — 순환이 없다
        // 레코드마다 가리키는 곳이 하나다 — 나눠 가진 가지가 있으면 따라가는 횟수가 끝없이 불어난다.
        vector<uint8> listNodeUsed( header._nodeCount, uint8{ SW_FALSE } );
        vector<uint8> listAttributeUsed( header._attributeCount, uint8{ SW_FALSE } );
        const auto    claim = []( vector<uint8>& inoutListUsed, uint32 index )
        {
            if ( index == kNone )
                return true;
            const bool bFree     = inoutListUsed[index] == SW_FALSE;
            inoutListUsed[index] = SW_TRUE;
            return bFree;
        };
        for ( uint32 index = 0; index < header._nodeCount; ++index )
        {
            const XMLBinaryNode& node   = pNodes[index];
            const bool           bValid = node._headerDistance == sizeof( XMLBinaryHeader ) + index * sizeof( XMLBinaryNode ) && node._name < header._stringBytes && node._text < header._stringBytes &&
                                ( node._firstAttribute == kNone || node._firstAttribute < header._attributeCount ) && isLater( node._firstChild, index, header._nodeCount ) &&
                                isLater( node._nextSibling, index, header._nodeCount );
            if ( bValid == false || claim( listNodeUsed, node._firstChild ) == false || claim( listNodeUsed, node._nextSibling ) == false || claim( listAttributeUsed, node._firstAttribute ) == false )
                return false;
        }
        const uint64 attributeBase = sizeof( XMLBinaryHeader ) + uint64{ header._nodeCount } * sizeof( XMLBinaryNode );
        for ( uint32 index = 0; index < header._attributeCount; ++index )
        {
            const XMLBinaryAttribute& attribute = pAttributes[index];
            const bool                bValid    = attribute._headerDistance == attributeBase + index * sizeof( XMLBinaryAttribute ) && attribute._name < header._stringBytes &&
                                attribute._value < header._stringBytes && isLater( attribute._next, index, header._attributeCount );
            if ( bValid == false || claim( listAttributeUsed, attribute._next ) == false )
                return false;
        }
        return true;
    }

    bool XMLBinary::decodeText( string_view bytes, string& outText )
    {
        const uint8*        pBytes = reinterpret_cast<const uint8*>( bytes.data() ); // 글 버퍼는 레코드 정렬을 약속하지 않는다 — 옮겨 담는다
        const vector<uint8> aligned( pBytes, pBytes + bytes.size() );
        if ( validate( string_view( reinterpret_cast<const utf8*>( aligned.data() ), aligned.size() ) ) == false )
            return false;
        const XMLBinaryNode* const pRoot = getRoot( aligned.data() );
        XMLDocument                document;
        XMLBinaryInternal::copyNode( *pRoot, document.appendRoot( getText( *pRoot, pRoot->_name ) ) );
        outText = document.saveToString();
        return true;
    }

    const XMLBinaryNode* XMLBinary::getRoot( const uint8* pBytes )
    {
        return XMLBinaryInternal::getNodes( *reinterpret_cast<const XMLBinaryHeader*>( pBytes ) );
    }

    const XMLBinaryNode* XMLBinary::findNode( const XMLBinaryNode& from, uint32 index )
    {
        return index != kNone ? XMLBinaryInternal::getNodes( XMLBinaryInternal::getHeader( from ) ) + index : nullptr;
    }

    const XMLBinaryAttribute* XMLBinary::findAttribute( const XMLBinaryNode& node )
    {
        return node._firstAttribute != kNone ? XMLBinaryInternal::getAttributes( XMLBinaryInternal::getHeader( node ) ) + node._firstAttribute : nullptr;
    }

    const XMLBinaryAttribute* XMLBinary::findNextAttribute( const XMLBinaryAttribute& attribute )
    {
        return attribute._next != kNone ? XMLBinaryInternal::getAttributes( XMLBinaryInternal::getHeader( attribute ) ) + attribute._next : nullptr;
    }

    const utf8* XMLBinary::getText( const XMLBinaryNode& node, uint32 offset )
    {
        return XMLBinaryInternal::getStrings( XMLBinaryInternal::getHeader( node ) ) + offset;
    }

    const utf8* XMLBinary::getText( const XMLBinaryAttribute& attribute, uint32 offset )
    {
        return XMLBinaryInternal::getStrings( XMLBinaryInternal::getHeader( attribute ) ) + offset;
    }
} // namespace sw
