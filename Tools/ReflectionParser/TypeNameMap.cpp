#include "pch.h"

#include "ReflectionParser/TypeNameMap.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "ReflectionParser/ParserContext.h"
#include "ReflectionParser/ParserDefines.h"
#include "ReflectionParser/ParserUtil.h"

SW_LOG_CALLER( "TypeNameMap" );
namespace sw
{
    namespace
    {
        struct TypeNameMapInternal
        {
            /** @brief clang 수식어(const/class 등)와 참조를 제거합니다. */
            static string stripClangDecorations( string_view tView )
            {
                tView                        = StringUtil::trim( tView );
                const ParserClangConfig& cfg = ParserContext::getSharedConfig();

                bool bStripped{ true };
                while ( bStripped && tView.empty() == false )
                {
                    bStripped = false;
                    for ( const string& prefix : cfg._listTypeStripPrefix )
                    {
                        if ( StringUtil::startsWith( tView, prefix ) )
                        {
                            tView.remove_prefix( prefix.size() );
                            tView     = StringUtil::trim( tView );
                            bStripped = true;
                        }
                    }
                }

                while ( tView.empty() == false && tView.back() == '&' )
                {
                    tView.remove_suffix( 1 );
                    tView = StringUtil::trim( tView );
                }
                return string( tView );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TypeNameMap::TypeNameMap()
        : _mapAliasToCanonical{}
        , _bLoaded{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    TypeNameMap& TypeNameMap::instance()
    {
        static TypeNameMap s_map;
        return s_map;
    }

    void TypeNameMap::clear()
    {
        _mapAliasToCanonical.clear();
        _bLoaded = SW_FALSE;
    }

    void TypeNameMap::addKey( const string& key, const string& canonical )
    {
        if ( key.empty() )
            return;
        _mapAliasToCanonical.insert_or_assign( key, canonical );
    }

    void TypeNameMap::registerEntry( const string& canonical, const string& nameSpace,
                                     const vector<string>& aliases )
    {
        if ( canonical.empty() )
            return;

        addKey( canonical, canonical );
        if ( nameSpace.empty() == false )
        {
            StringBuilder<constant::kMaxBuffer128> qualified;
            qualified.appendFormat( "%#::%#", nameSpace, canonical );
            addKey( string( qualified.view() ), canonical );
        }

        for ( const string& alias : aliases )
        {
            addKey( alias, canonical );
            if ( nameSpace.empty() == false )
            {
                StringBuilder<constant::kMaxBuffer128> qualified;
                qualified.appendFormat( "%#::%#", nameSpace, alias );
                addKey( string( qualified.view() ), canonical );
            }
        }
    }

    string TypeNameMap::normalize( const string& clangSpelling ) const
    {
        string t = TypeNameMapInternal::stripClangDecorations( clangSpelling );
        if ( t.empty() )
            return t;

        // 1) std::__cxx11:: 인라인 네임스페이스 제거 (Linux Clang libstdc++ 대응)
        constexpr string_view kCxx11Prefix = "std::__cxx11::";
        if ( StringUtil::startsWith( t, kCxx11Prefix ) )
        {
            t = "std::" + t.substr( kCxx11Prefix.size() );
        }

        // 2) 템플릿 타입(예: map<K, V>, vector<T>)이면 재귀적으로 정규화
        const size_t openAngle  = t.find( '<' );
        const size_t closeAngle = t.rfind( '>' );
        if ( openAngle != string::npos && closeAngle != string::npos && openAngle < closeAngle )
        {
            const string stem  = string{ StringUtil::trim( string_view{ t }.substr( 0, openAngle ) ) };
            const string inner = string{ StringUtil::trim( string_view{ t }.substr( openAngle + 1, closeAngle - openAngle - 1 ) ) };

            const string         normStem = normalize( stem );
            const vector<string> listArg  = ParserUtil::splitCommaRespectingAngles( inner );

            string result = normStem + "<";
            for ( size_t index = 0; index < listArg.size(); ++index )
            {
                if ( index > 0 )
                    result += ", ";
                result += normalize( listArg[index] );
            }
            result += ">";
            return result;
        }

        // 3) 사전(ReflectBuiltins 등)에서 직접 일치 확인
        const auto it = _mapAliasToCanonical.find( t );
        if ( it != _mapAliasToCanonical.end() )
            return it->second;

        // 4) sw:: 최상위 엔진 네임스페이스 접두사 제거 후 재검색 (Linux Clang이 내부 타입을 FQN으로 내보내는 문제 대응)
        constexpr string_view kSwPrefix = "sw::";
        if ( StringUtil::startsWith( t, kSwPrefix ) )
        {
            string     stripped   = t.substr( kSwPrefix.size() );
            const auto itStripped = _mapAliasToCanonical.find( stripped );
            if ( itStripped != _mapAliasToCanonical.end() )
                return itStripped->second;
            return stripped;
        }

        // 5) scopeLeaf fallback
        const string_view bare = ParserUtil::scopeLeaf( t );
        if ( bare != t )
        {
            const auto itBare = _mapAliasToCanonical.find( string{ bare } );
            if ( itBare != _mapAliasToCanonical.end() )
                return itBare->second;
        }

        return t;
    }

    string normalizeTypeName( const string& clangSpelling )
    {
        return TypeNameMap::instance().normalize( clangSpelling );
    }
} // namespace sw
