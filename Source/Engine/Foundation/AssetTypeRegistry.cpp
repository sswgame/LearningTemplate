#include "pch.h"

#include "Engine/Foundation/AssetTypeRegistry.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/RegistrationList.h"
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

            /**
             * @brief 표 전체입니다. 정적 초기화 순서와 무관하게 처음 쓸 때 만든다.
             * @details 종류 목록은 공용 `RegistrationList`(이름 · 대소문자 무시)라 핫 리로드가 올리는 교체 이미지의 같은 이름을 대체하고 옛 이미지가 내려가면
             *          그대로 남는다(손짠 목록은 교체 등록을 "두 번 올렸다" 로 거절해 옛 이미지가 내려간 뒤 종류가 사라졌다). 접미사 표는 그 목록에서 다시 짓는 파생물이다.
             */
            struct Table
            {
                mutex                                _mutex;
                RegistrationList<const AssetTypeDef> _listType{ "AssetTypeRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase };
                vector<SuffixEntry>                  _listSuffix;
                vector<string>                       _listProblem;
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

            /** @brief 접미사 표를 종류 목록(올린 순)에서 다시 짓습니다 — 가장 긴 것이 먼저, 길이가 같으면 올린 순. 표의 잠금 안에서 부른다. */
            static void rebuildSuffixes( Table& table )
            {
                table._listSuffix.clear();
                uint64 order = 0;
                for ( const AssetTypeDef* pType : table._listType.getItems() )
                {
                    for ( uint32 index = 0; index < pType->_suffixCount; ++index )
                    {
                        table._listSuffix.push_back( SuffixEntry{ pType, &pType->_pSuffix[index], order++ } );
                    }
                }
                std::stable_sort( table._listSuffix.begin(), table._listSuffix.end(), &isLongerFirst );
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
        ScopedLock<mutex>                 lock{ table._mutex };
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
                // 같은 이름의 종류는 접미사가 겹쳐도 여기서 거절하지 않는다 — 리로드면 대체, 아니면 목록이 이름 중복으로 거절한다.
                if ( StringUtil::equals( entry._pType->_name, def._name, true ) )
                    continue;
                if ( StringUtil::equals( entry._pSuffix->_text, def._pSuffix[index]._text, true ) )
                {
                    AssetTypeRegistryInternal::addProblem( table, "asset suffix claimed by two types", entry._pType->_name, def._name );
                    return;
                }
            }
        }
        const RegistrationResult result = table._listType.add( &def, def._name );
        if ( result == RegistrationResult::DuplicateName )
        {
            AssetTypeRegistryInternal::addProblem( table, "asset type name registered twice", table._listType.findByName( def._name )->_name, def._name );
            return;
        }
        if ( result == RegistrationResult::EmptyName )
        {
            AssetTypeRegistryInternal::addProblem( table, "asset type has no name", def._name, def._name );
            return;
        }
        AssetTypeRegistryInternal::rebuildSuffixes( table );
    }

    bool AssetTypeRegistry::isSuffixFormatAllowed( const AssetSuffix& suffix )
    {
        return suffix._role == AssetSuffixRole::CookSource || StringUtil::endsWith( suffix._text, ".json", true ) == false;
    }

    void AssetTypeRegistry::unregisterType( const AssetTypeDef& def )
    {
        AssetTypeRegistryInternal::Table& table = AssetTypeRegistryInternal::getTable();
        ScopedLock<mutex>                 lock{ table._mutex };
        if ( table._listType.remove( &def ) ) // 빼면 대체당했던 옛 등록이 돌아올 수 있다 — 접미사 표를 다시 짓는다
            AssetTypeRegistryInternal::rebuildSuffixes( table );
    }

    const AssetTypeDef* AssetTypeRegistry::findType( string_view name )
    {
        AssetTypeRegistryInternal::Table& table = AssetTypeRegistryInternal::getTable();
        ScopedLock<mutex>                 lock{ table._mutex };
        return table._listType.findByName( name );
    }

    vector<const AssetTypeDef*> AssetTypeRegistry::collectTypes()
    {
        AssetTypeRegistryInternal::Table& table = AssetTypeRegistryInternal::getTable();
        ScopedLock<mutex>                 lock{ table._mutex };
        return table._listType.getItems();
    }

    AssetSuffixMatch AssetTypeRegistry::findSuffix( string_view path )
    {
        AssetTypeRegistryInternal::Table& table = AssetTypeRegistryInternal::getTable();
        ScopedLock<mutex>                 lock{ table._mutex };
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
        ScopedLock<mutex>                 lock{ table._mutex };
        for ( const AssetTypeDef* pType : table._listType.getItems() )
        {
            if ( group.empty() == false && StringUtil::equals( pType->_excludeGroup, group, true ) )
                return true;
        }
        return false;
    }

    void AssetTypeRegistry::collectProblems( vector<string>& outListProblem )
    {
        AssetTypeRegistryInternal::Table& table = AssetTypeRegistryInternal::getTable();
        ScopedLock<mutex>                 lock{ table._mutex };
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
