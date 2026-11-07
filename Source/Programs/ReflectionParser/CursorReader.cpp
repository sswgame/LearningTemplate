#include "pch.h"

#include "Programs/ReflectionParser/CursorReader.h"

#include "Core/String/StringBuilder.h"

#include "Programs/ReflectionParser/ParsedReflection.h"

namespace sw
{
    string CursorReader::takeString( const CXString cxString )
    {
        string      result;
        const utf8* pText = clang_getCString( cxString );
        if ( pText != nullptr )
            result = pText;
        clang_disposeString( cxString );
        return result;
    }

    bool CursorReader::isStringEqual( const CXString cxString, const string_view target )
    {
        const utf8* pText  = clang_getCString( cxString );
        const bool  bEqual = ( pText != nullptr && string_view( pText ) == target );
        clang_disposeString( cxString );
        return bEqual;
    }

    string CursorReader::getCursorSpelling( const CXCursor cursor )
    {
        return takeString( clang_getCursorSpelling( cursor ) );
    }

    string CursorReader::makeFullyQualifiedName( const CXCursor cursor )
    {
        vector<string> listPart;
        CXCursor       current = cursor;
        while ( clang_getCursorKind( current ) != CXCursor_TranslationUnit )
        {
            string spelling = getCursorSpelling( current );
            if ( spelling.empty() == false )
                listPart.push_back( std::move( spelling ) );
            current = clang_getCursorSemanticParent( current );
        }

        StringBuilder<constant::buffer::kMax1024> fqn;
        for ( auto it = listPart.rbegin(); it != listPart.rend(); ++it )
        {
            if ( fqn.size() > 0 )
                fqn.append( "::" );
            fqn.append( *it );
        }
        return string( fqn.view() );
    }

    string CursorReader::makeMemberOwnerName( const string_view ownerFQN, const string_view memberName )
    {
        StringBuilder<constant::buffer::kMax1024> b;
        b.appendFormat( "%#::%#", ownerFQN, memberName );
        return string( b.view() );
    }

    CXCursor CursorReader::getBaseClassDeclaration( const CXCursor baseSpecifier )
    {
        return clang_getTypeDeclaration( clang_getCanonicalType( clang_getCursorType( baseSpecifier ) ) );
    }

    CXChildVisitResult CursorReader::enumeratorCollectorVisitor( CXCursor cursor, CXCursor parent, CXClientData data )
    {
        if ( clang_getCursorKind( cursor ) != CXCursor_EnumConstantDecl )
            return CXChildVisit_Continue;

        ParsedEnumeratorInfo enumerator;
        enumerator._name = getCursorSpelling( cursor );
        // 밑바탕이 부호 없는 enum 은 부호 없는 값으로 읽는다 — `clang_getEnumConstantDeclValue` 는 늘 부호 있게 넓혀 `uint8` 의 200 이
        // -56 이 된다(생성 코드는 컴파일러 식으로 내므로 맞지만, `--dump` 와 파서 안의 판단이 이 값을 본다).
        const CXType integerType = clang_getCanonicalType( clang_getEnumDeclIntegerType( parent ) );
        const bool   bUnsigned   = integerType.kind == CXType_Bool || integerType.kind == CXType_Char_U || integerType.kind == CXType_UChar ||
                               integerType.kind == CXType_UShort || integerType.kind == CXType_UInt || integerType.kind == CXType_ULong ||
                               integerType.kind == CXType_ULongLong;
        enumerator._value = bUnsigned ? static_cast<int64>( clang_getEnumConstantDeclUnsignedValue( cursor ) ) : clang_getEnumConstantDeclValue( cursor );
        static_cast<vector<ParsedEnumeratorInfo>*>( data )->push_back( std::move( enumerator ) );
        return CXChildVisit_Continue;
    }

    void CursorReader::collectEnumerators( const CXCursor enumCursor, vector<ParsedEnumeratorInfo>& outListEnumerator )
    {
        clang_visitChildren( enumCursor, enumeratorCollectorVisitor, &outListEnumerator );
    }
} // namespace sw
