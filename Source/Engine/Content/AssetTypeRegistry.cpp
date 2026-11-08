#include "pch.h"

#include "Engine/Content/AssetTypeRegistry.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Memory/Memory.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        struct AssetTypeRegistryInternal
        {
            /**
             * @brief 올린 종류 목록입니다(이름 · 대소문자 무시, 함수 정적 — 정적 초기화 순서와 무관하게 처음 쓸 때 선다). 잠금은 목록의 주인 잠금(`getOwnerMutex`)이다.
             * @details 공용 `RegistrationList` 라 핫 리로드가 올리는 교체 이미지의 같은 이름을 대체하고 옛 이미지가 내려가면 그대로 남는다. 접미사 찾기는 이 목록을 그대로 훑는다
             *          (예전에는 파생 접미사 표를 따로 들고 등록마다 다시 지었다 — 종류는 백 개 안팎이라 훑기로 충분하다).
             */
            static SW_DEFINE_REGISTRATION_LIST( getTypes, const AssetTypeDef, "AssetTypeRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase )

                /** @brief 받지 못한 등록입니다(겹친 이름 · 접미사 · JSON 으로 그대로 읽는 접미사). 까닭은 `collectProblems` 가 목록과 견줘 다시 낸다 — 문장을 들고 있지 않는다. */
                static SW_DEFINE_REGISTRATION_LIST( getRejected, const AssetTypeDef, "AssetTypeRejected" )

                /** @brief @p def 의 접미사 중 그대로 읽는데 JSON 인 것입니다. 없으면 nullptr 입니다. */
                static const AssetSuffix* findDisallowedSuffix( const AssetTypeDef& def )
            {
                for ( uint32 index = 0; index < def._suffixCount; ++index )
                {
                    if ( AssetTypeRegistry::isSuffixFormatAllowed( def._pSuffix[index] ) == false )
                        return &def._pSuffix[index];
                }
                return nullptr;
            }

            /** @brief @p def 의 접미사를 이름이 다른 올린 종류가 이미 쥐었으면 그 종류입니다(잠금 안에서 부른다). 같은 이름은 리로드 대체 · 이름 중복이라 여기서 보지 않는다. */
            static const AssetTypeDef* findSuffixOwner( const AssetTypeDef& def )
            {
                for ( const AssetTypeDef* pType : getTypes().getItems() )
                {
                    if ( pType == &def || StringUtil::equals( pType->_name, def._name, true ) )
                        continue;
                    for ( uint32 own = 0; own < pType->_suffixCount; ++own )
                    {
                        for ( uint32 index = 0; index < def._suffixCount; ++index )
                        {
                            if ( StringUtil::equals( pType->_pSuffix[own]._text, def._pSuffix[index]._text, true ) )
                                return pType;
                        }
                    }
                }
                return nullptr;
            }

            /** @brief 문제 한 줄입니다. */
            static string makeProblem( string_view what, string_view first, string_view second )
            {
                StringBuilder<constant::buffer::kMax256> sb;
                sb.appendFormat( "%#: '%#' and '%#'", what, first, second );
                return string( sb.c_str(), sb.size() );
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
        RegistrationList<const AssetTypeDef>& types = AssetTypeRegistryInternal::getTypes();
        ScopedLock<mutex>                     lock{ types.getOwnerMutex() };
        const bool                            bRejected = AssetTypeRegistryInternal::findDisallowedSuffix( def ) != nullptr || AssetTypeRegistryInternal::findSuffixOwner( def ) != nullptr;
        if ( bRejected )
        {
            (void)AssetTypeRegistryInternal::getRejected().add( &def ); // 까닭은 `collectProblems` 가 다시 낸다
            return;
        }
        const RegistrationResult result = types.add( &def, def._name );
        if ( result == RegistrationResult::DuplicateName || result == RegistrationResult::EmptyName )
            (void)AssetTypeRegistryInternal::getRejected().add( &def );
    }

    bool AssetTypeRegistry::isSuffixFormatAllowed( const AssetSuffix& suffix )
    {
        return suffix._role == AssetSuffixRole::CookSource || StringUtil::endsWith( suffix._text, ".json", true ) == false;
    }

    void AssetTypeRegistry::unregisterType( const AssetTypeDef& def )
    {
        RegistrationList<const AssetTypeDef>& types = AssetTypeRegistryInternal::getTypes();
        ScopedLock<mutex>                     lock{ types.getOwnerMutex() };
        if ( types.remove( &def ) == false ) // 빼면 대체당했던 옛 등록이 돌아올 수 있다 — 접미사 찾기는 목록을 그대로 훑는다
            (void)AssetTypeRegistryInternal::getRejected().remove( &def );
    }

    const AssetTypeDef* AssetTypeRegistry::findType( string_view name )
    {
        const RegistrationList<const AssetTypeDef>& types = AssetTypeRegistryInternal::getTypes();
        ScopedLock<mutex>                           lock{ types.getOwnerMutex() };
        return types.findByName( name );
    }

    void AssetTypeRegistry::collectPathMatches( string_view assetID, vector<const AssetTypeDef*>& outListType )
    {
        outListType.clear();
        const RegistrationList<const AssetTypeDef>& types = AssetTypeRegistryInternal::getTypes();
        ScopedLock<mutex>                           lock{ types.getOwnerMutex() };
        for ( const AssetTypeDef* pType : types.getItems() )
        {
            if ( pType->_pathRule.matches( assetID ) )
                outListType.push_back( pType );
        }
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
        const RegistrationList<const AssetTypeDef>& types = AssetTypeRegistryInternal::getTypes();
        ScopedLock<mutex>                           lock{ types.getOwnerMutex() };
        return types.getItems();
    }

    AssetSuffixMatch AssetTypeRegistry::findSuffix( string_view path )
    {
        const RegistrationList<const AssetTypeDef>& types = AssetTypeRegistryInternal::getTypes();
        ScopedLock<mutex>                           lock{ types.getOwnerMutex() };
        AssetSuffixMatch                            best{};
        for ( const AssetTypeDef* pType : types.getItems() )
        {
            for ( uint32 index = 0; index < pType->_suffixCount; ++index )
            {
                const AssetSuffix& suffix  = pType->_pSuffix[index];
                const bool         bLonger = best.isMatched() == false || best._pSuffix->_text.size() < suffix._text.size(); // 가장 긴 것, 같으면 먼저 올린 것
                if ( bLonger && StringUtil::endsWith( path, suffix._text, true ) )
                    best = AssetSuffixMatch{ pType, &suffix };
            }
        }
        return best;
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
        const RegistrationList<const AssetTypeDef>& types = AssetTypeRegistryInternal::getTypes();
        ScopedLock<mutex>                           lock{ types.getOwnerMutex() };
        for ( const AssetTypeDef* pType : types.getItems() )
        {
            if ( group.empty() == false && StringUtil::equals( pType->_excludeGroup, group, true ) )
                return true;
        }
        return false;
    }

    void AssetTypeRegistry::collectProblems( vector<string>& outListProblem )
    {
        const RegistrationList<const AssetTypeDef>& types = AssetTypeRegistryInternal::getTypes();
        ScopedLock<mutex>                           lock{ types.getOwnerMutex() };
        for ( const AssetTypeDef* pDef : AssetTypeRegistryInternal::getRejected().getItems() )
        {
            const AssetSuffix* const  pDisallowed = AssetTypeRegistryInternal::findDisallowedSuffix( *pDef );
            const AssetTypeDef* const pOwner      = pDisallowed == nullptr ? AssetTypeRegistryInternal::findSuffixOwner( *pDef ) : nullptr;
            const AssetTypeDef* const pSameName   = types.findByName( pDef->_name );
            if ( pDisallowed != nullptr )
                outListProblem.push_back( AssetTypeRegistryInternal::makeProblem( "asset suffix read as-is must be XML or binary, not JSON", pDef->_name, pDisallowed->_text ) );
            else if ( pOwner != nullptr )
                outListProblem.push_back( AssetTypeRegistryInternal::makeProblem( "asset suffix claimed by two types", pOwner->_name, pDef->_name ) );
            else if ( pSameName != nullptr )
                outListProblem.push_back( AssetTypeRegistryInternal::makeProblem( "asset type name registered twice", pSameName->_name, pDef->_name ) );
            else
                outListProblem.push_back( AssetTypeRegistryInternal::makeProblem( "asset type has no name", pDef->_name, pDef->_name ) );
        }
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
