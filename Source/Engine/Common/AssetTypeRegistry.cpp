#include "pch.h"

#include "Engine/Common/AssetTypeRegistry.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/String/StringBuilder.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct AssetTypeRegistryInternal
        {
            /** @brief 접미사 표의 한 칸 — 길이 내림차순(같으면 올린 순)으로 늘어놓는다. */
            struct SuffixEntry
            {
                const AssetTypeDef* _pType;
                const AssetSuffix*  _pSuffix;
                uint64              _order; ///< 올린 순번(길이가 같을 때 앞선 것이 이긴다)
            };

            /** @brief 표 전체입니다. 정적 초기화 순서와 무관하게 처음 쓸 때 만든다. */
            struct Table
            {
                mutex                       _mutex;
                vector<const AssetTypeDef*> _listType;
                vector<SuffixEntry>         _listSuffix;
                vector<string>              _listProblem;
                uint64                      _nextOrder{ 0 };
            };

            static Table& getTable()
            {
                static Table s_table;
                return s_table;
            }

            static bool isLongerFirst( const SuffixEntry& left, const SuffixEntry& right )
            {
                if ( left._pSuffix->_text.size() != right._pSuffix->_text.size() )
                    return left._pSuffix->_text.size() > right._pSuffix->_text.size();
                return left._order < right._order;
            }

            /** @brief 문제 한 줄을 남깁니다(표의 잠금 안에서 부른다). */
            static void addProblem( Table& table, string_view what, string_view first, string_view second )
            {
                StringBuilder<constant::kMaxBuffer256> sb;
                sb.appendFormat( "%#: '%#' and '%#'", what, first, second );
                table._listProblem.emplace_back( sb.c_str(), sb.size() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void AssetTypeRegistry::registerType( const AssetTypeDef& def )
    {
        AssetTypeRegistryInternal::Table& table = AssetTypeRegistryInternal::getTable();
        std::scoped_lock<mutex>           lock( table._mutex );
        for ( const AssetTypeDef* pType : table._listType )
        {
            if ( StringUtil::equals( pType->_name, def._name, true ) )
            {
                AssetTypeRegistryInternal::addProblem( table, "asset type name registered twice", pType->_name, def._name );
                return;
            }
        }
        for ( uint32 index = 0; index < def._suffixCount; ++index )
        {
            if ( isSuffixFormatAllowed( def._pSuffix[index] ) == false )
            {
                AssetTypeRegistryInternal::addProblem( table, "asset suffix read as-is must be XML or binary, not JSON", def._name, def._pSuffix[index]._text );
                return;
            }
        }
        for ( uint32 index = 0; index < def._suffixCount; ++index )
        {
            for ( const AssetTypeRegistryInternal::SuffixEntry& entry : table._listSuffix )
            {
                if ( StringUtil::equals( entry._pSuffix->_text, def._pSuffix[index]._text, true ) )
                {
                    AssetTypeRegistryInternal::addProblem( table, "asset suffix claimed by two types", entry._pType->_name, def._name );
                    return;
                }
            }
        }
        table._listType.push_back( &def );
        for ( uint32 index = 0; index < def._suffixCount; ++index )
        {
            table._listSuffix.push_back( AssetTypeRegistryInternal::SuffixEntry{ &def, &def._pSuffix[index], table._nextOrder++ } );
        }
        std::stable_sort( table._listSuffix.begin(), table._listSuffix.end(), &AssetTypeRegistryInternal::isLongerFirst );
    }

    bool AssetTypeRegistry::isSuffixFormatAllowed( const AssetSuffix& suffix )
    {
        return suffix._role == AssetSuffixRole::CookSource || StringUtil::endsWith( suffix._text, ".json", true ) == false;
    }

    void AssetTypeRegistry::unregisterType( const AssetTypeDef& def )
    {
        AssetTypeRegistryInternal::Table& table = AssetTypeRegistryInternal::getTable();
        std::scoped_lock<mutex>           lock( table._mutex );
        VectorUtil::removeValue( table._listType, &def );
        VectorUtil::removeIf( table._listSuffix, [&def]( const AssetTypeRegistryInternal::SuffixEntry& entry )
        { return entry._pType == &def; } );
    }

    const AssetTypeDef* AssetTypeRegistry::findType( string_view name )
    {
        AssetTypeRegistryInternal::Table& table = AssetTypeRegistryInternal::getTable();
        std::scoped_lock<mutex>           lock( table._mutex );
        for ( const AssetTypeDef* pType : table._listType )
        {
            if ( StringUtil::equals( pType->_name, name, true ) )
                return pType;
        }
        return nullptr;
    }

    vector<const AssetTypeDef*> AssetTypeRegistry::collectTypes()
    {
        AssetTypeRegistryInternal::Table& table = AssetTypeRegistryInternal::getTable();
        std::scoped_lock<mutex>           lock( table._mutex );
        return table._listType;
    }

    AssetSuffixMatch AssetTypeRegistry::findSuffix( string_view path )
    {
        AssetTypeRegistryInternal::Table& table = AssetTypeRegistryInternal::getTable();
        std::scoped_lock<mutex>           lock( table._mutex );
        for ( const AssetTypeRegistryInternal::SuffixEntry& entry : table._listSuffix )
        {
            if ( StringUtil::endsWith( path, entry._pSuffix->_text, true ) )
                return AssetSuffixMatch{ entry._pType, entry._pSuffix };
        }
        return AssetSuffixMatch{};
    }

    bool AssetTypeRegistry::isPathOfType( string_view path, const AssetTypeDef& def )
    {
        const AssetSuffixMatch match = findSuffix( path );
        return match.isMatched() && match._pType->_name == def._name;
    }

    string_view AssetTypeRegistry::findExcludeGroup( string_view path )
    {
        const AssetSuffixMatch match = findSuffix( path );
        return match.isMatched() ? match._pType->_excludeGroup : string_view{};
    }

    bool AssetTypeRegistry::hasExcludeGroup( string_view group )
    {
        AssetTypeRegistryInternal::Table& table = AssetTypeRegistryInternal::getTable();
        std::scoped_lock<mutex>           lock( table._mutex );
        for ( const AssetTypeDef* pType : table._listType )
        {
            if ( group.empty() == false && StringUtil::equals( pType->_excludeGroup, group, true ) )
                return true;
        }
        return false;
    }

    void AssetTypeRegistry::collectProblems( vector<string>& outListProblem )
    {
        AssetTypeRegistryInternal::Table& table = AssetTypeRegistryInternal::getTable();
        std::scoped_lock<mutex>           lock( table._mutex );
        outListProblem.insert( outListProblem.end(), table._listProblem.begin(), table._listProblem.end() );
    }

    AssetTypeRegistrar::AssetTypeRegistrar( const AssetTypeDef& def )
        : _def{ def }
    {
        AssetTypeRegistry::registerType( def );
    }

    AssetTypeRegistrar::~AssetTypeRegistrar()
    {
        AssetTypeRegistry::unregisterType( _def );
    }
} // namespace sw
