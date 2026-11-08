#include "pch.h"

#include "Engine/Content/AssetTypeRegistry.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Memory/Memory.h"
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

                static bool isLongerFirst( const SuffixEntry& left, const SuffixEntry& right )
                {
                    if ( left._pSuffix->_text.size() != right._pSuffix->_text.size() )
                        return left._pSuffix->_text.size() > right._pSuffix->_text.size();
                    return left._order < right._order;
                }
            };

            /**
             * @brief 표 전체와 그 표를 읽고 바꾸는 일입니다. 모든 멤버가 스스로 잠근다. 정적 초기화 순서와 무관하게 처음 쓸 때 만든다(`getTable`).
             * @details 종류 목록은 공용 `RegistrationList`(이름 · 대소문자 무시)라 핫 리로드가 올리는 교체 이미지의 같은 이름을 대체하고 옛 이미지가 내려가면
             *          그대로 남는다(손짠 목록은 교체 등록을 "두 번 올렸다" 로 거절해 옛 이미지가 내려간 뒤 종류가 사라졌다). 접미사 표는 그 목록에서 다시 짓는 파생물이다.
             */
            class Table
            {
            public:
                Table()
                    : _mutex{}
                    , _listType{ "AssetTypeRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase }
                    , _listSuffix{}
                    , _listProblem{}
                {
                }

                /** @brief 종류 하나를 올립니다. 접미사 형식 · 겹침 · 이름이 틀리면 문제 한 줄을 남기고 올리지 않는다. */
                void registerType( const AssetTypeDef& def )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    for ( uint32 index = 0; index < def._suffixCount; ++index )
                    {
                        if ( AssetTypeRegistry::isSuffixFormatAllowed( def._pSuffix[index] ) == false )
                        {
                            addProblem( "asset suffix read as-is must be XML or binary, not JSON", def._name, def._pSuffix[index]._text );
                            return;
                        }
                    }
                    for ( uint32 index = 0; index < def._suffixCount; ++index )
                    {
                        for ( const SuffixEntry& entry : _listSuffix )
                        {
                            // 같은 이름의 종류는 접미사가 겹쳐도 여기서 거절하지 않는다 — 리로드면 대체, 아니면 목록이 이름 중복으로 거절한다.
                            if ( StringUtil::equals( entry._pType->_name, def._name, true ) )
                                continue;
                            if ( StringUtil::equals( entry._pSuffix->_text, def._pSuffix[index]._text, true ) )
                            {
                                addProblem( "asset suffix claimed by two types", entry._pType->_name, def._name );
                                return;
                            }
                        }
                    }
                    const RegistrationResult result = _listType.add( &def, def._name );
                    if ( result == RegistrationResult::DuplicateName )
                    {
                        addProblem( "asset type name registered twice", _listType.findByName( def._name )->_name, def._name );
                        return;
                    }
                    if ( result == RegistrationResult::EmptyName )
                    {
                        addProblem( "asset type has no name", def._name, def._name );
                        return;
                    }
                    rebuildSuffixes();
                }

                void unregisterType( const AssetTypeDef& def )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    if ( _listType.remove( &def ) ) // 빼면 대체당했던 옛 등록이 돌아올 수 있다 — 접미사 표를 다시 짓는다
                        rebuildSuffixes();
                }

                const AssetTypeDef* findType( string_view name )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    return _listType.findByName( name );
                }

                void collectPathMatches( string_view assetID, vector<const AssetTypeDef*>& outListType )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    for ( const AssetTypeDef* pType : _listType.getItems() )
                    {
                        if ( pType->_pathRule.matches( assetID ) )
                            outListType.push_back( pType );
                    }
                }

                vector<const AssetTypeDef*> collectTypes()
                {
                    ScopedLock<mutex> lock{ _mutex };
                    return _listType.getItems();
                }

                AssetSuffixMatch findSuffix( string_view path )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    for ( const SuffixEntry& entry : _listSuffix )
                    {
                        if ( StringUtil::endsWith( path, entry._pSuffix->_text, true ) )
                            return AssetSuffixMatch{ entry._pType, entry._pSuffix };
                    }
                    return AssetSuffixMatch{};
                }

                bool hasExcludeGroup( string_view group )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    for ( const AssetTypeDef* pType : _listType.getItems() )
                    {
                        if ( group.empty() == false && StringUtil::equals( pType->_excludeGroup, group, true ) )
                            return true;
                    }
                    return false;
                }

                void collectProblems( vector<string>& outListProblem )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    outListProblem.insert( outListProblem.end(), _listProblem.begin(), _listProblem.end() );
                }

            private:
                /** @brief 접미사 표를 종류 목록(올린 순)에서 다시 짓습니다 — 가장 긴 것이 먼저, 길이가 같으면 올린 순. 잠금 안에서 부른다. */
                void rebuildSuffixes() SW_REQUIRES_LOCK( _mutex )
                {
                    _listSuffix.clear();
                    uint64 order = 0;
                    for ( const AssetTypeDef* pType : _listType.getItems() )
                    {
                        for ( uint32 index = 0; index < pType->_suffixCount; ++index )
                        {
                            _listSuffix.push_back( SuffixEntry{ pType, &pType->_pSuffix[index], order++ } );
                        }
                    }
                    VectorUtil::stableSort( _listSuffix, &SuffixEntry::isLongerFirst );
                }

                /** @brief 문제 한 줄을 남깁니다(잠금 안에서 부른다). */
                void addProblem( string_view what, string_view first, string_view second ) SW_REQUIRES_LOCK( _mutex )
                {
                    StringBuilder<constant::buffer::kMax256> sb;
                    sb.appendFormat( "%#: '%#' and '%#'", what, first, second );
                    _listProblem.emplace_back( sb.c_str(), sb.size() );
                }

            private:
                mutex                                          _mutex;
                RegistrationList<const AssetTypeDef> _listType SW_GUARDED_BY( _mutex );
                vector<SuffixEntry> _listSuffix                SW_GUARDED_BY( _mutex );
                vector<string> _listProblem                    SW_GUARDED_BY( _mutex );
            };

            static Table& getTable()
            {
                static Table s_table;
                return s_table;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AssetPathRule::matches( string_view assetID ) const
    {
        if ( isEmpty() )
            return false;
        if ( _prefix.empty() == false && StringUtil::startsWith( assetID, _prefix, true ) == false )
            return false;
        if ( _contains.empty() == false && StringUtil::contains( assetID, _contains, true ) == false )
            return false;
        return _suffix.empty() || StringUtil::endsWith( assetID, _suffix, true );
    }

    void AssetTypeRegistry::registerType( const AssetTypeDef& def )
    {
        AssetTypeRegistryInternal::getTable().registerType( def );
    }

    bool AssetTypeRegistry::isSuffixFormatAllowed( const AssetSuffix& suffix )
    {
        return suffix._role == AssetSuffixRole::CookSource || StringUtil::endsWith( suffix._text, ".json", true ) == false;
    }

    void AssetTypeRegistry::unregisterType( const AssetTypeDef& def )
    {
        AssetTypeRegistryInternal::getTable().unregisterType( def );
    }

    const AssetTypeDef* AssetTypeRegistry::findType( string_view name )
    {
        return AssetTypeRegistryInternal::getTable().findType( name );
    }

    void AssetTypeRegistry::collectPathMatches( string_view assetID, vector<const AssetTypeDef*>& outListType )
    {
        outListType.clear();
        AssetTypeRegistryInternal::getTable().collectPathMatches( assetID, outListType );
    }

    bool AssetTypeRegistry::checkLoad( const AssetTypeDef& def, const AssetID& assetID )
    {
        if ( def._loader._pCheckLoad == nullptr )
            return false;
        const ScopedMemoryTag memoryScope{ def._loader._memoryTag };
        return def._loader._pCheckLoad( assetID ); // 잠금 밖이다 — 로더가 다른 종류를 찾는다
    }

    vector<const AssetTypeDef*> AssetTypeRegistry::collectTypes()
    {
        return AssetTypeRegistryInternal::getTable().collectTypes();
    }

    AssetSuffixMatch AssetTypeRegistry::findSuffix( string_view path )
    {
        return AssetTypeRegistryInternal::getTable().findSuffix( path );
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
        return AssetTypeRegistryInternal::getTable().hasExcludeGroup( group );
    }

    void AssetTypeRegistry::collectProblems( vector<string>& outListProblem )
    {
        AssetTypeRegistryInternal::getTable().collectProblems( outListProblem );
    }

    AssetTypeRegistrar::AssetTypeRegistrar( const AssetTypeDef& def )
        : _def{ def }
    {
        AssetTypeRegistry::registerType( _def ); // 인자는 등록 줄의 임시값일 수 있다 — 레지스트리는 주소를 들므로 자기 사본을 올린다
    }

    AssetTypeRegistrar::~AssetTypeRegistrar()
    {
        AssetTypeRegistry::unregisterType( _def );
    }
} // namespace sw
