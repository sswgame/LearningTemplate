#include "pch.h"

#include "Engine/Reflection/TypeRegistry.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/pair.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/MemoryBlock.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionConstants.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionEnumNames.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionValidation.h"

/**
 * @file TypeRegistry.cpp
 * @brief 형식 · 열거형 표 — 등록 · 조회 · 모듈 내림 · 별칭입니다.
 */
namespace sw
{
    namespace
    {
        /** @brief 타입 테이블의 세대입니다(`TypeRegistry::getGeneration`). 타입 · 열거형의 등록, 별칭, 모듈 해제, 레지스트리 파괴마다 오릅니다. */
        atomic<uint32> s_typeTableGeneration{ 0 };

        struct ReflectionInternal
        {
            /** @brief `describeType` 의 인자 목록입니다. `타입 이름 = 기본값` 형태입니다. */
            template <typename TBuilder>
            static void appendParameterList( TBuilder& out, const vector<FunctionParameterInfo>& listParameter )
            {
                for ( size_t paramIndex = 0; paramIndex < listParameter.size(); ++paramIndex )
                {
                    const FunctionParameterInfo& parameter = listParameter[paramIndex];
                    out.appendFormat( "%#%#", paramIndex == 0 ? "" : ", ", parameter._typeName.c_str() );
                    if ( parameter._name.empty() == false )
                        out.appendFormat( " %#", parameter._name.c_str() );
                    if ( parameter.hasDefaultValue() )
                        out.appendFormat( " = %#", parameter._defaultValue.c_str() );
                }
            }

            /**
             * @brief 이름 해시가 같은데 값이 다른 열거자 둘을 찾습니다. 없으면 false 입니다.
             * @details 바이너리는 enum 을 열거자 **이름 해시**로 싣는다(대소문자 무시). 같은 해시의 두 이름(`Red` 와 `RED`, 드물게 FNV 충돌)이 다른 값을
             *          가리키면 저장된 데이터가 어느 쪽으로 읽힐지 정해지지 않는다. 텍스트 읽기도 대소문자를 무시하므로 같은 모호함이다.
             *          같은 값을 가리키는 별칭은 괜찮다. 세 테이블(값 → 이름, 이름 → 값, 옛 이름 리다이렉트)을 모두 본다. 대소문자만 다른 두 이름은 intern 에서 한 이름이 되어 이름 → 값 테이블에는
             *          하나만 남고(뒤의 값이 이긴다), 값 → 이름 테이블에서만 두 값이 같은 이름을 가리킨다.
             */
            [[nodiscard]] static bool tryFindNameHashClash( const EnumInfo& info, hashed_string& outFirstName, int64& outFirstValue, hashed_string& outSecondName,
                                                            int64& outSecondValue )
            {
                unordered_map<uint32, pair<hashed_string, int64>> mapHashToName;
                const auto                                        isClash = [&]( const hashed_string& name, int64 value ) -> bool
                {
                    const auto [iter, bInserted] = mapHashToName.emplace( static_cast<uint32>( name.getHash() ), make_pair( name, value ) );
                    if ( bInserted || iter->second.second == value )
                        return false;
                    outFirstName   = iter->second.first;
                    outFirstValue  = iter->second.second;
                    outSecondName  = name;
                    outSecondValue = value;
                    return true;
                };
                for ( const auto& [value, name] : info._mapValueToName )
                {
                    if ( isClash( name, value ) )
                        return true;
                }
                for ( const auto& [name, value] : info._mapNameToValue )
                {
                    if ( isClash( name, value ) )
                        return true;
                }
                // 옛 이름도 같은 해시 공간이다. 옛 이름이 지금 이름과 해시가 같은데 다른 값이면 저장된 바이트가 어느 쪽으로 읽힐지 정해지지 않는다.
                for ( const auto& [name, value] : info._mapRedirectNameToValue )
                {
                    if ( isClash( name, value ) )
                        return true;
                }
                return false;
            }
            /**
             * @brief canonical FQN 의 네임스페이스를 alias 앞에 붙입니다.
             * @details REFLECT(Alias=Foo) 는 리프 이름만 적으므로, registerClass 가 FQN 과 리프를
             *          모두 등록하는 것과 맞추려면 별칭도 FQN 형태를 함께 등록해야 합니다.
             * @return 네임스페이스가 없거나 alias 가 이미 한정되어 있으면 빈 문자열입니다.
             */
            static string qualifyAliasWithNamespace( const utf8* pAliasName, const utf8* pCanonicalName )
            {
                const string_view alias{ pAliasName };
                const string_view canonical{ pCanonicalName };
                if ( alias.find( constant::reflection::kScopeDelimiter ) != string_view::npos )
                    return {};

                const size_t lastScope = canonical.rfind( constant::reflection::kScopeDelimiter );
                if ( lastScope == string_view::npos )
                    return {};

                string qualified{ canonical.substr( 0, lastScope + 2 ) };
                qualified.append( alias.data(), alias.size() );
                return qualified;
            }
        };
    } // namespace

    namespace generated
    {
        void forceLinkBuiltinTypes();
    } // namespace generated

    TypeRegistry::TypeRegistry()
        : _bAllModuleTypesRegistered{ false }
    {
        generated::forceLinkBuiltinTypes();
    }

    uint32 TypeRegistry::getGeneration()
    {
        return s_typeTableGeneration.load( std::memory_order_acquire );
    }

    void TypeRegistry::markAllModuleTypesRegistered()
    {
        _bAllModuleTypesRegistered.store( true, std::memory_order_release );
    }

    bool TypeRegistry::areAllModuleTypesRegistered() const
    {
        return _bAllModuleTypesRegistered.load( std::memory_order_acquire );
    }
    TypeRegistry::~TypeRegistry()
    {
        // 이 레지스트리가 가진 TypeInfo · EnumInfo 가 사라진다. 세대를 담은 타입 캐시(`TypeCacheSlot`)가 다음 호출에 다시 찾게 한다.
        s_typeTableGeneration.fetch_add( 1, std::memory_order_acq_rel );
    }

    static atomic<uint32> _s_typeIDCounter{ 0 }; // 실행 중에만 쓰는 로컬 인덱스(직렬화하지 않으므로 플랫폼과 무관하다)

    void TypeRegistry::registerClass( const TypeInfo& info )
    {
        UniqueLock<shared_mutex> lock{ _mutex };

        // 모듈 이름은 생성 코드가 찍은 것(CMake 가 대상마다 `--module` 로 정한다)이 답이다 - 올리는 쪽 이름으로 덮어쓰지 않는다.
        // 예전에는 올리는 쪽 이름이 이겨, 늦게 올라온 공용 모듈의 타입이 남의 모듈로 등록돼 그 모듈을 내릴 때 함께 사라졌다.
        if ( info._moduleName.empty() )
            SW_FATAL( "Reflected type %# has no module name - its registrar was not generated by ReflectionParser --module", info._fullyQualifiedName.c_str() );
        TypeInfo stored = info;
        attachEditorMetadata( stored );

        // 타입 하나가 곧 TypeInfo 하나다. 짧은 이름 키는 복사본이 아니라 FQN 을 가리킨다. 복사본을 두면
        // `findType("sw::Foo")` 와 `findType("Foo")` 가 서로 다른 포인터를 반환하고, `const TypeInfo*` 를 키로 쓰는 쪽이
        // 그 둘을 다른 타입으로 본다.
        const hashed_string canonicalKey =
            stored._fullyQualifiedName.empty() == false ? stored._fullyQualifiedName : stored._name;

        auto existingIt = _mapFqnToClassType.find( canonicalKey );
        if ( existingIt != _mapFqnToClassType.end() )
            stored._typeID = existingIt->second->_typeID;
        else
            stored._typeID = _s_typeIDCounter.fetch_add( 1, std::memory_order_relaxed ) + 1;

        const hashed_string canonicalName = stored._name.empty() == false ? stored._name : stored._fullyQualifiedName;
        const hashed_string shortName     = stored._name; // 아래에서 `stored` 를 옮긴다

        // **주소는 고정이다.** 같은 FQN 이 있으면(재등록, 묘비) 그 객체에 덮어써 복원한다. 밖에서 가진 포인터
        // (`TypeLookupCache`, `_pParentType`, 컴포넌트 풀 키)가 모두 그대로 맞는다. 새 타입은 새 객체다.
        // 캐시는 배치 끝(`populateLookupCaches`)이 단일 스레드에서 만든다.
        if ( existingIt != _mapFqnToClassType.end() )
        {
            *existingIt->second = stored;
            existingIt->second->_castHeader._bAlive.store( SW_TRUE, std::memory_order_release );
        }
        else
        {
            _mapFqnToClassType.emplace( canonicalKey, make_unique<TypeInfo>( std::move( stored ) ) );
        }
        _mapHashToCanonicalName.insert_or_assign( canonicalKey.getHash(), canonicalName );
        // 체인이 바뀌었을 수 있다. 재등록은 부모를 바꿀 수 있고, 새 타입은 누군가의 비어 있던 부모일 수 있다.
        // 조상 테이블과 부모에게서 복사해 온 프로퍼티 목록을 모두 비운다. 배치 끝의 populateLookupCaches 나 첫 조회가 다시 만든다.
        for ( const auto& [storedFqn, pStoredInfo] : _mapFqnToClassType )
        {
            (void)storedFqn;
            pStoredInfo->clearAncestorDisplay();
            pStoredInfo->clearInheritedProperties();
        }
        hashed_string shadowedFqn;
        if ( shortName.empty() == false && shortName != canonicalKey )
        {
            // 짧은 이름이 이미 **다른 살아 있는** 타입을 가리키면 그 타입을 가린다. 씬, 프리팹, `addComponentByName` 은 짧은 이름으로
            // 찾으므로 그 뒤로는 다른 타입이 만들어진다. 나중 것이 이기되 알린다.
            const auto previousIt = _mapAliasToFqn.find( shortName );
            if ( previousIt != _mapAliasToFqn.end() && previousIt->second != canonicalKey )
            {
                const auto previousTypeIt = _mapFqnToClassType.find( previousIt->second );
                if ( previousTypeIt != _mapFqnToClassType.end() && previousTypeIt->second->isAlive() )
                    shadowedFqn = previousIt->second;
            }
            _mapAliasToFqn.insert_or_assign( shortName, canonicalKey );
            _mapHashToCanonicalName.insert_or_assign( shortName.getHash(), canonicalName );
        }
        // 테이블이 바뀌었으니 세대를 올린다. 기록해 둔 빈 결과(`TypeLookupCache` 의 미등록, 찾지 못한 부모)를 다음 조회가 다시 찾는다.
        s_typeTableGeneration.fetch_add( 1, std::memory_order_acq_rel );

        // 로그는 잠금 밖에서 남긴다. 리스너가 레지스트리를 다시 물어도 멈추지 않게 하려는 것이다.
        lock.unlock();
        if ( shadowedFqn.empty() == false )
            SW_LOG_WARNING_ID( "Reflection.TypeShortNameCollision", "Reflected type name '%#' now means %# and no longer %# - short names must be unique (scenes, prefabs and "
                                                                    "addComponentByName look types up by them)",
                               shortName.c_str(), canonicalKey.c_str(), shadowedFqn.c_str() );
    }

    void TypeRegistry::registerEnum( const EnumInfo& info )
    {
        UniqueLock<shared_mutex> lock{ _mutex };
        if ( info._moduleName.empty() )
            SW_FATAL( "Reflected enum %# has no module name - its registrar was not generated by ReflectionParser --module", info._fullyQualifiedName.c_str() );
        EnumInfo stored = info;
        attachEditorMetadata( stored );

        // 같은 FQN 이면 그 객체에 덮어쓴다(주소 고정이라 별칭과 밖에서 가진 포인터가 새 내용을 본다). 새 enum 은 새 객체다.
        unique_ptr<EnumInfo>& pOwned = _mapFqnToEnum[stored._fullyQualifiedName];
        if ( pOwned == nullptr )
            pOwned = make_unique<EnumInfo>( std::move( stored ) );
        else
            *pOwned = std::move( stored );
        EnumInfo* pEnumInfo = pOwned.get();
        _mapNameToEnum.insert_or_assign( pEnumInfo->_fullyQualifiedName, pEnumInfo );
        hashed_string shadowedFqn;
        if ( pEnumInfo->_name.empty() == false && pEnumInfo->_name != pEnumInfo->_fullyQualifiedName )
        {
            // 타입과 같은 규칙이다. 짧은 이름이 다른 enum 을 가리키고 있었으면 알린다(에셋의 enum 필드는 짧은 이름으로 찾기도 한다).
            const auto previousIt = _mapNameToEnum.find( pEnumInfo->_name );
            if ( previousIt != _mapNameToEnum.end() && previousIt->second != pEnumInfo )
                shadowedFqn = previousIt->second->_fullyQualifiedName;
            _mapNameToEnum.insert_or_assign( pEnumInfo->_name, pEnumInfo );
        }
        // 이름의 답이 없음에서 이 열거형으로 바뀔 수 있다. 미등록을 기록해 둔 열거형 캐시(`EnumNameCache`)가 다시 찾게 한다.
        s_typeTableGeneration.fetch_add( 1, std::memory_order_acq_rel );

        hashed_string clashFirst;
        hashed_string clashSecond;
        int64         clashFirstValue{ 0 };
        int64         clashSecondValue{ 0 };
        const bool    bNameClash = ReflectionInternal::tryFindNameHashClash( *pEnumInfo, clashFirst, clashFirstValue, clashSecond, clashSecondValue );
        const string  enumName( pEnumInfo->_fullyQualifiedName.c_str() );

        lock.unlock();
        if ( shadowedFqn.empty() == false )
            SW_LOG_WARNING_ID( "Reflection.EnumShortNameCollision", "Reflected enum name '%#' now means %# and no longer %# - short names must be unique",
                               pEnumInfo->_name.c_str(), pEnumInfo->_fullyQualifiedName.c_str(), shadowedFqn.c_str() );
        if ( bNameClash )
            SW_LOG_ERROR_ID( "Reflection.EnumeratorHashClash", "Enum %# has enumerators '%#' (%#) and '%#' (%#) with the same name hash but different values - binary saves and "
                                                               "case-insensitive text cannot tell them apart; rename one",
                             enumName.c_str(), clashFirst.c_str(), clashFirstValue, clashSecond.c_str(), clashSecondValue );
    }

    void TypeRegistry::attachEditorMetadata( TypeInfo& info )
    {
        const TypeEditorMetadataSource* pSource = TypeEditorMetadataSource::find( info._fullyQualifiedName.view() );
        if ( pSource == nullptr )
            return;
        unique_ptr<TypeEditorMetadataSet> pSet = make_unique<TypeEditorMetadataSet>();
        pSource->_pFill( *pSet );
        info._metadata->_pEditor = &pSet->_type;
        for ( PropertyInfo& prop : info._listProperty )
        {
            const auto iter = pSet->_mapProperty.find( prop._name );
            if ( iter != pSet->_mapProperty.end() )
                prop._metadata->_pEditor = &iter->second;
        }
        for ( EventInfo& event : info._listEvent )
        {
            const auto iter = pSet->_mapEvent.find( event._name );
            if ( iter != pSet->_mapEvent.end() )
                event._metadata->_pEditor = &iter->second;
        }
        for ( FunctionInfo& method : info._listMethod )
        {
            const auto iter = pSet->_mapFunction.find( method._hashName );
            if ( iter != pSet->_mapFunction.end() )
                method._metadata->_pEditor = &iter->second;
        }
        unique_ptr<TypeEditorMetadataSet>& pSlot = _mapTypeEditorMetadata[info._fullyQualifiedName];
        if ( pSlot != nullptr )
            _listRetiredTypeEditorMetadata.push_back( std::move( pSlot ) );
        pSlot = std::move( pSet );
    }

    void TypeRegistry::attachEditorMetadata( EnumInfo& info )
    {
        const EnumEditorMetadataSource* pSource = EnumEditorMetadataSource::find( info._fullyQualifiedName.view() );
        if ( pSource == nullptr )
            return;
        unique_ptr<EnumEditorMetadata> pMetadata = make_unique<EnumEditorMetadata>();
        pSource->_pFill( *pMetadata );
        info._pEditor                         = pMetadata.get();
        unique_ptr<EnumEditorMetadata>& pSlot = _mapEnumEditorMetadata[info._fullyQualifiedName];
        if ( pSlot != nullptr )
            _listRetiredEnumEditorMetadata.push_back( std::move( pSlot ) );
        pSlot = std::move( pMetadata );
    }

    void TypeRegistry::registerPendingTypes( TypeRegistrar* pClassHead, EnumRegistrar* pEnumHead )
    {
        TypeRegistrar* pCurrClass = pClassHead;
        while ( pCurrClass != nullptr )
        {
            if ( pCurrClass->_registerFunc != nullptr )
                pCurrClass->_registerFunc( *this );
            pCurrClass = pCurrClass->_pNext;
        }

        EnumRegistrar* pCurrEnum = pEnumHead;
        while ( pCurrEnum != nullptr )
        {
            if ( pCurrEnum->_registerFunc != nullptr )
                pCurrEnum->_registerFunc( *this );
            pCurrEnum = pCurrEnum->_pNext;
        }

        // 이 배치의 마지막 삽입까지 끝난 지금 캐시를 만든다. 여기는 아직 단일 스레드다.
        populateLookupCaches();
    }

    string TypeRegistry::describeType( const hashed_string& nameOrFqn ) const
    {
        const TypeInfo* pType = findType( nameOrFqn );
        if ( pType == nullptr )
            return string( "TYPE '" ) + nameOrFqn.c_str() + "' is not registered (no REFLECT, its module is not loaded, or a typo)\n";

        // 부모 체인은 기반부터 찍으려고 모은다. 찾을 수 없는 부모 이름도 그대로 적는다(부모가 늦게 등록된 경우가 여기서 보인다).
        vector<const TypeInfo*> listChain;
        hashed_string           unresolvedParent;
        for ( const TypeInfo* pCurrent = pType; pCurrent != nullptr && listChain.size() < 64; )
        {
            listChain.push_back( pCurrent );
            if ( pCurrent->_parentFQN.empty() )
                break;
            const TypeInfo* pParent = findType( pCurrent->_parentFQN );
            if ( pParent == nullptr )
                unresolvedParent = pCurrent->_parentFQN;
            pCurrent = pParent;
        }

        StringBuilder<constant::buffer::kMax8192> out;
        out.appendFormat( "TYPE %# (module %#, %# bytes%#)\n", pType->_fullyQualifiedName.c_str(),
                          pType->_moduleName.empty() ? "?" : pType->_moduleName.c_str(), static_cast<uint64>( pType->_size ),
                          pType->isAlive() ? "" : ", module unloaded" );
        if ( listChain.size() > 1 )
        {
            out.append( "  parents: " );
            for ( size_t chainIndex = 1; chainIndex < listChain.size(); ++chainIndex )
            {
                out.appendFormat( "%#%#", chainIndex == 1 ? "" : " -> ", listChain[chainIndex]->_fullyQualifiedName.c_str() );
            }
            out.append( "\n" );
        }
        if ( unresolvedParent.empty() == false )
            out.appendFormat( "  parent '%#' is not registered (not reflected, or its module is not loaded) - nothing it declares is listed\n",
                              unresolvedParent.c_str() );

        for ( size_t chainIndex = listChain.size(); chainIndex-- > 0; )
        {
            const TypeInfo& level = *listChain[chainIndex];
            out.appendFormat( "  [%#]\n", level._fullyQualifiedName.c_str() );
            for ( const PropertyInfo& prop : level._listProperty )
            {
                out.appendFormat( "    PROPERTY %# : %#", prop._name.c_str(), prop._typeName.c_str() );
                if ( prop._bIsBitField == SW_TRUE )
                    out.appendFormat( "  @byte %# mask 0x%# (bit field)", static_cast<uint64>( prop.getOffset() ), Fmt( static_cast<uint32>( prop.getBitMask() ), Format().hex() ) );
                else if ( prop._pValueAccessor != nullptr )
                    out.append( "  @accessor" );
                else
                    out.appendFormat( "  @offset %#", static_cast<uint64>( prop.getOffset() ) );
                if ( prop._bIsContainer == SW_TRUE )
                    out.appendFormat( "  [%# element=%#%#%#]", toString( prop._containerKind ), prop._elementTypeName.c_str(),
                                      prop._keyTypeName.empty() ? "" : " key=", prop._keyTypeName.c_str() );
                if ( prop._metadata->_bHasMinRange == SW_TRUE )
                    out.appendFormat( "  Min=%#", prop._metadata->_minRange );
                if ( prop._metadata->_bHasMaxRange == SW_TRUE )
                    out.appendFormat( "  Max=%#", prop._metadata->_maxRange );
                if ( prop._metadata->_maxCount != 0 )
                    out.appendFormat( "  MaxCount=%#", prop._metadata->_maxCount );
                if ( prop._metadata->_defaultValue.empty() == false )
                    out.appendFormat( "  Default=\"%#\"", prop._metadata->_defaultValue.c_str() );
                const pair<bool, const utf8*> arrFlag[] = {
                    {    prop._metadata->_bReadOnly != SW_FALSE,     "ReadOnly"},
                    {   prop._metadata->_bTransient != SW_FALSE,    "Transient"},
                    {prop._metadata->_bXMLAttribute != SW_FALSE, "XMLAttribute"},
                    {   prop._metadata->_bAssetPath != SW_FALSE,    "AssetPath"},
                    { prop._metadata->_bPolymorphic != SW_FALSE,  "Polymorphic"},
                    { prop._metadata->_bSkipIfEmpty != SW_FALSE,  "SkipIfEmpty"},
                    {    prop._metadata->_bRequired != SW_FALSE,     "Required"},
                    {  prop._metadata->_bInheritKey != SW_FALSE,   "InheritKey"},
                    {prop._metadata->_bRuntimeState != SW_FALSE, "RuntimeState"},
                };
                for ( const auto& [bSet, pFlagName] : arrFlag )
                {
                    if ( bSet )
                        out.appendFormat( "  [%#]", pFlagName );
                }
                for ( const hashed_string& alias : prop._listAlias )
                {
                    out.appendFormat( "  alias=%#", alias.c_str() );
                }
                out.append( "\n" );
            }
            for ( const FunctionInfo& method : level._listMethod )
            {
                out.appendFormat( "    FUNCTION %#(", method._name.c_str() );
                ReflectionInternal::appendParameterList( out, method._listParameter );
                out.appendFormat( ") -> %#\n", method._returnTypeName.empty() ? "void" : method._returnTypeName.c_str() );
            }
            for ( const EventInfo& event : level._listEvent )
            {
                out.appendFormat( "    EVENT %#(", event._name.c_str() );
                ReflectionInternal::appendParameterList( out, event._listParameter );
                out.append( ")\n" );
            }
        }
        return string( out.view() );
    }

    string TypeRegistry::describeEnum( const hashed_string& nameOrFqn ) const
    {
        const EnumInfo* pEnum = findEnum( nameOrFqn );
        if ( pEnum == nullptr )
            return string( "ENUM '" ) + nameOrFqn.c_str() + "' is not registered (no ENUM(), its module is not loaded, or a typo)\n";

        StringBuilder<constant::buffer::kMax4096> out;
        out.appendFormat( "ENUM %# (module %#, %# bytes, %#%#)\n", pEnum->_fullyQualifiedName.c_str(),
                          pEnum->_moduleName.empty() ? "?" : pEnum->_moduleName.c_str(), static_cast<uint32>( pEnum->_size ),
                          pEnum->_bIsSigned != SW_FALSE ? "signed" : "unsigned", pEnum->_bIsBitFlag != SW_FALSE ? ", Flags" : "" );
        // 값 순으로 정렬한다. 이름은 밀집 해시라 등록 순서가 남지 않는다.
        vector<pair<int64, const utf8*>> listEntry;
        for ( const auto& [value, name] : pEnum->_mapValueToName )
        {
            listEntry.emplace_back( value, name.c_str() );
        }
        VectorUtil::sort( listEntry, []( const auto& lhs, const auto& rhs )
        { return lhs.first < rhs.first; } );
        for ( const auto& [value, pName] : listEntry )
        {
            out.appendFormat( "  %# = %#\n", pName, value );
        }
        if ( pEnum->_bHasInvalid != SW_FALSE )
            out.appendFormat( "  (Invalid = %#)\n", pEnum->_invalidValue );
        if ( pEnum->_bHasCount != SW_FALSE )
            out.appendFormat( "  (Count = %#)\n", pEnum->_countValue );
        return string( out.view() );
    }

    void TypeRegistry::populateLookupCaches() const
    {
        vector<const TypeInfo*> listType;
        {
            ScopedSharedLock<shared_mutex> lock{ _mutex };
            listType.reserve( _mapFqnToClassType.size() );
            for ( const auto& [fqn, pInfo] : _mapFqnToClassType )
            {
                (void)fqn;
                if ( pInfo->isAlive() )
                    listType.push_back( pInfo.get() );
            }
        }

        // **잠금 밖에서** 만든다. getPropertiesWithBase 가 부모를 찾으려고 레지스트리를 다시
        // 잠그는데, shared_mutex 는 재귀가 아니라서 잠금 안에서 부르면 그 자리에서 멈춘다.
        for ( const TypeInfo* pType : listType )
        {
            if ( pType == nullptr )
                continue;
            // 부모 포인터부터 찾는다. 아래 두 캐시가 부모를 따라가고, 캐스트의 핫패스는 이것만 본다.
            pType->resolveParentType();
            pType->populateLookupCache();
            (void)pType->getPropertiesWithBase();
        }
        // 부모 포인터가 **모두** 찾아진 뒤에 조상 테이블을 만든다. 테이블은 체인 끝까지 따라가므로 한 바퀴 뒤여야 한다.
        for ( const TypeInfo* pType : listType )
        {
            if ( pType != nullptr )
                (void)pType->computeAncestorDisplay();
        }
        // 지연 판정(세이브 옵트인, 검증 함수 유무)도 여기서 한 번 처리한다. 로드 워커가 처음 물을 때 같은 바이트의 캐시 비트를 함께 쓰지 않게 하려는 것이다.
        for ( const TypeInfo* pType : listType )
        {
            if ( pType == nullptr )
                continue;
            (void)pType->hasSaveGameProperty();
            (void)ReflectionValidation::hasValidator( *pType );
        }
    }

    Subscription TypeRegistry::subscribeModuleUnloading( const ModuleUnloadingDelegate& delegate )
    {
        return _moduleUnloading.add( delegate );
    }

    void TypeRegistry::publishModuleUnloading( string_view moduleName )
    {
        _moduleUnloading.broadcast( moduleName );
    }

    Subscription TypeRegistry::subscribeModuleObjectsDetached( const ModuleUnloadingDelegate& delegate )
    {
        return _moduleObjectsDetached.add( delegate );
    }

    void TypeRegistry::publishModuleObjectsDetached( string_view moduleName )
    {
        _moduleObjectsDetached.broadcast( moduleName );
    }

    void TypeRegistry::unregisterTypesByModule( string_view moduleName )
    {
        UniqueLock<shared_mutex> lock{ _mutex };
        hashed_string            hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );

        // 지우지 않고 **묘비**를 남긴다. 밖에서 가진 포인터(`TypeLookupCache` 의 정적 슬롯, 다른 모듈 타입의 부모 포인터)가
        // 허공을 가리키지 않도록 객체는 그 자리에 둔다. `findType` 은 해제된 타입을 nullptr 로 답하고, 같은 FQN 이
        // 다시 로드되면 같은 객체를 복원한다.
        for ( auto& [fqn, pInfo] : _mapFqnToClassType )
        {
            (void)fqn;
            if ( pInfo->_moduleName != hashModule )
                continue;
            // 내용은 지금 비운다. 모듈 코드를 가리키는 델리게이트를 모듈이 언로드된 뒤에 파괴하면 안 된다.
            pInfo->clearContent();
            pInfo->_castHeader._bAlive.store( SW_FALSE, std::memory_order_release );
        }

        // 사라진 타입을 가리키던 별칭도 같이 정리한다. 남겨 두면 조회가 빈 항목을 따라가 nullptr 를 반환한다.
        for ( auto it = _mapAliasToFqn.begin(); it != _mapAliasToFqn.end(); )
        {
            const auto typeIt = _mapFqnToClassType.find( it->second );
            if ( typeIt == _mapFqnToClassType.end() || typeIt->second->isAlive() == false )
                it = _mapAliasToFqn.erase( it );
            else
                ++it;
        }

        // enum 은 이름만 정리한다. 객체는 `_mapFqnToEnum` 에 남는다(묘비). 반환한 포인터가 허공을 가리키지 않고, 다시 로드되면 그 위치에 덮어쓴다.
        // 묘비의 이름 목록(`_listDenseName`, `_mapValueToName`)은 `EnumInfo` 가 가진 intern 된 이름이라 모듈 해제와 무관하게 그대로 둔다.
        for ( auto it = _mapNameToEnum.begin(); it != _mapNameToEnum.end(); )
        {
            if ( it->second->_moduleName == hashModule )
                it = _mapNameToEnum.erase( it );
            else
                ++it;
        }

        _mapHashToCanonicalName.clear();
        for ( const auto& [fqn, pInfo] : _mapFqnToClassType )
        {
            if ( pInfo->isAlive() == false )
                continue;
            const hashed_string canonicalName = pInfo->_name.empty() == false ? pInfo->_name : pInfo->_fullyQualifiedName;
            _mapHashToCanonicalName.insert_or_assign( fqn.getHash(), canonicalName );
        }
        for ( const auto& [alias, fqn] : _mapAliasToFqn )
        {
            const auto typeIt = _mapFqnToClassType.find( fqn );
            if ( typeIt == _mapFqnToClassType.end() )
                continue;
            const TypeInfo&     info          = *typeIt->second;
            const hashed_string canonicalName = info._name.empty() == false ? info._name : info._fullyQualifiedName;
            _mapHashToCanonicalName.insert_or_assign( alias.getHash(), canonicalName );
        }
        // 부모 포인터는 옮겨지지 않지만 **해제된 부모**를 가리킬 수 있다. 비워서 다음 조회가 이름으로 다시 찾게 한다
        // (해제된 타입은 `findType` 이 nullptr 를 반환하므로 체인은 거기서 끝난다). 조상 테이블도, 부모에게서 복사해 온 프로퍼티도 같이 비운다.
        // 그 사본의 접근자는 지금 언로드되는 모듈의 코드를 가리킨다(모듈이 아직 살아 있는 지금 버려야 한다. `clearContent` 와 같은 이유다).
        for ( const auto& [fqn, pInfo] : _mapFqnToClassType )
        {
            (void)fqn;
            pInfo->clearParentType();
            pInfo->clearAncestorDisplay();
            pInfo->clearInheritedProperties();
        }
        s_typeTableGeneration.fetch_add( 1, std::memory_order_acq_rel );
    }

    void TypeRegistry::registerTypeAlias( const utf8* pAliasName, const utf8* pCanonicalName )
    {
        if ( pAliasName == nullptr || pCanonicalName == nullptr )
            return;
        if ( StringUtil::equals( pAliasName, pCanonicalName ) )
            return;

        UniqueLock<shared_mutex> lock{ _mutex };

        // pCanonicalName 자체가 짧은 이름(별칭)일 수 있으므로 FQN 까지 한 번 더 따라간다.
        hashed_string canonicalKey{ pCanonicalName };
        const auto    directIt = _mapFqnToClassType.find( canonicalKey );
        if ( directIt == _mapFqnToClassType.end() || directIt->second->isAlive() == false )
        {
            const auto redirectIt = _mapAliasToFqn.find( canonicalKey );
            if ( redirectIt == _mapAliasToFqn.end() )
                return;
            canonicalKey = redirectIt->second;
        }

        const auto typeIt = _mapFqnToClassType.find( canonicalKey );
        if ( typeIt == _mapFqnToClassType.end() || typeIt->second->isAlive() == false )
            return;

        // insert_or_assign: 핫 리로드로 다시 등록할 때 옛 별칭이 남지 않게 한다.
        const TypeInfo&     stored        = *typeIt->second;
        const hashed_string canonicalName = stored._name.empty() == false ? stored._name : stored._fullyQualifiedName;
        const hashed_string aliasHash{ pAliasName };

        _mapAliasToFqn.insert_or_assign( aliasHash, canonicalKey );
        _mapHashToCanonicalName.insert_or_assign( aliasHash.getHash(), canonicalName );
        // 별칭이 생기면 같은 이름의 답이 nullptr 에서 타입으로 바뀔 수 있다.
        s_typeTableGeneration.fetch_add( 1, std::memory_order_acq_rel );

        const string qualified = ReflectionInternal::qualifyAliasWithNamespace( pAliasName, pCanonicalName );
        if ( qualified.empty() == false )
        {
            const hashed_string qualifiedHash{ qualified.c_str() };
            _mapAliasToFqn.insert_or_assign( qualifiedHash, canonicalKey );
            _mapHashToCanonicalName.insert_or_assign( qualifiedHash.getHash(), canonicalName );
        }
    }

    void TypeRegistry::registerEnumAlias( const utf8* pAliasName, const utf8* pCanonicalName )
    {
        if ( pAliasName == nullptr || pCanonicalName == nullptr )
            return;
        if ( StringUtil::equals( pAliasName, pCanonicalName ) )
            return;

        UniqueLock<shared_mutex> lock{ _mutex };
        auto                     it = _mapNameToEnum.find( hashed_string( pCanonicalName ) );
        if ( it == _mapNameToEnum.end() )
            return;

        // 별칭은 같은 객체를 가리킨다(복사본이면 다시 등록된 열거자를 못 본다).
        EnumInfo* const pEnumInfo = it->second;
        _mapNameToEnum.insert_or_assign( hashed_string( pAliasName ), pEnumInfo );

        const string qualified = ReflectionInternal::qualifyAliasWithNamespace( pAliasName, pCanonicalName );
        if ( qualified.empty() == false )
            _mapNameToEnum.insert_or_assign( hashed_string( qualified.c_str() ), pEnumInfo );
        // 별칭이 생기면 같은 이름의 답이 nullptr 에서 열거형으로 바뀔 수 있다(타입 별칭과 같은 규칙).
        s_typeTableGeneration.fetch_add( 1, std::memory_order_acq_rel );
    }

    const TypeInfo* TypeRegistry::findType( const hashed_string& nameOrFqn ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };

        // 해제된 타입(모듈 해제, 묘비)은 없는 것으로 답한다. 객체는 남아 있어도 이름 조회는 살아 있는 것만 반환한다.
        const auto it = _mapFqnToClassType.find( nameOrFqn );
        if ( it != _mapFqnToClassType.end() )
            return it->second->isAlive() ? it->second.get() : nullptr;

        const auto aliasIt = _mapAliasToFqn.find( nameOrFqn );
        if ( aliasIt == _mapAliasToFqn.end() )
            return nullptr;

        const auto canonicalIt = _mapFqnToClassType.find( aliasIt->second );
        if ( canonicalIt == _mapFqnToClassType.end() || canonicalIt->second->isAlive() == false )
            return nullptr;
        return canonicalIt->second.get();
    }

    const atomic<uint32>& getTypeTableGenerationCounter()
    {
        return s_typeTableGeneration;
    }

    const TypeInfo* TypeLookupCache::findSlow( const hashed_string& fqn ) const
    {
        // 레지스트리가 아직 없으면 칸을 건드리지 않고 nullptr 를 반환한다(다음 호출이 다시 본다).
        if ( engine::areEngineServicesBound() == false )
            return nullptr;
        // 세대는 찾기 **전에** 읽는다. 찾는 사이 등록이 끼면 옛 세대가 남아 다음 호출이 다시 찾는다(그 반대는 없다).
        _pGenerationCounter.store( &s_typeTableGeneration, std::memory_order_release );
        const uint32    generation = s_typeTableGeneration.load( std::memory_order_acquire );
        const TypeInfo* pFound     = engine::getSubsystem<TypeRegistry>().findType( fqn );
        // 해제된 타입(묘비)은 "없음" 으로 적는다 — 적중 경로(`tryGetCached`)가 `isAlive` 를 보지 않는다. 되살림은 세대를 올려 다시 찾게 한다.
        const TypeInfo* pType = pFound != nullptr && pFound->isAlive() ? pFound : nullptr;
        _slot.set( generation, pType );
        return pType;
    }

    const EnumInfo* TypeRegistry::findEnum( const hashed_string& nameOrFqn ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        auto                           it = _mapNameToEnum.find( nameOrFqn );
        return it != _mapNameToEnum.end() ? it->second : nullptr;
    }

    const EnumInfo* TypeRegistry::findEnumByTypeFqn( const hashed_string& fqn ) const
    {
        const EnumInfo* pInfo = findEnum( fqn );
        if ( pInfo != nullptr )
            return pInfo;
        const utf8* pCstr = fqn.c_str();
        if ( pCstr == nullptr || *pCstr == 0 )
            return nullptr;
        const string_view fqnView{ pCstr };
        const size_t      lastScope = fqnView.rfind( constant::reflection::kScopeDelimiter );
        if ( lastScope == string_view::npos )
            return nullptr;
        const size_t      delimiterLength = StringUtil::strlen( constant::reflection::kScopeDelimiter );
        const string_view leaf            = fqnView.substr( lastScope + delimiterLength );
        return findEnum( hashed_string( leaf.data(), static_cast<uint32>( leaf.size() ) ) );
    }

    const EnumInfo* TypeRegistry::findEnumByNameHash( const uint32 nameHash ) const
    {
        if ( nameHash == 0 )
            return nullptr;
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        for ( const auto& [name, pEnumInfo] : _mapNameToEnum )
        {
            if ( name.getHash() == nameHash )
                return pEnumInfo;
        }
        return nullptr;
    }

    hashed_string TypeRegistry::canonicalTypeNameByHash( const uint32 nameHash ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _mutex };
        auto                           it = _mapHashToCanonicalName.find( nameHash );
        return it != _mapHashToCanonicalName.end() ? it->second : hashed_string{};
    }

    const utf8* TypeRegistry::enumToString( const hashed_string& enumName, int64 value ) const
    {
        const EnumInfo* pInfo = findEnum( enumName );
        return pInfo != nullptr ? pInfo->valueToCString( value ) : nullptr;
    }

    bool TypeRegistry::enumFromString( const hashed_string& enumName, string_view name, int64& outValue ) const
    {
        const EnumInfo* pInfo = findEnum( enumName );
        if ( pInfo == nullptr )
        {
            outValue = 0;
            return false;
        }
        if ( pInfo->tryParse( name, outValue ) && pInfo->isValidValue( outValue ) )
            return true;
        outValue = ( pInfo->_bHasInvalid != SW_FALSE ) ? pInfo->_invalidValue : 0;
        return false;
    }

    bool TypeRegistry::hasFlag( const hashed_string& enumName, int64 flags, int64 contains ) const
    {
        const EnumInfo* pInfo = findEnum( enumName );
        if ( pInfo == nullptr || pInfo->_bIsBitFlag == SW_FALSE )
            return false;
        return ( flags & contains ) == contains;
    }

    TaskValue TypeRegistry::invokeMethod( void* pInstance, const hashed_string& classFqn, const hashed_string& methodName, const TaskArgs& args ) const
    {
        const TypeInfo* pTypeInfo = findType( classFqn );
        if ( pTypeInfo != nullptr )
        {
            const FunctionInfo* pFunc = pTypeInfo->findMethod( methodName );
            if ( pFunc != nullptr && pFunc->_invoker.isBound() )
                return pFunc->_invoker( pInstance, args );
        }
        return TaskValue{};
    }

    TypeRegistrar*& TypeRegistrar::getHead()
    {
        static TypeRegistrar* s_pHead{ nullptr };
        return s_pHead;
    }

    TypeRegistrar::TypeRegistrar( void ( *registerFunc )( TypeRegistry& ) )
        : TypeRegistrar( registerFunc, getHead() )
    {
    }

    TypeRegistrar::TypeRegistrar( void ( *registerFunc )( TypeRegistry& ), TypeRegistrar*& pModuleHead )
        : _registerFunc{ registerFunc }
        , _pNext{ nullptr }
    {
        _pNext      = pModuleHead;
        pModuleHead = this;
    }

    EnumRegistrar*& EnumRegistrar::getHead()
    {
        static EnumRegistrar* s_pHead{ nullptr };
        return s_pHead;
    }

    EnumRegistrar::EnumRegistrar( void ( *registerFunc )( TypeRegistry& ) )
        : EnumRegistrar( registerFunc, getHead() )
    {
    }

    EnumRegistrar::EnumRegistrar( void ( *registerFunc )( TypeRegistry& ), EnumRegistrar*& pModuleHead )
        : _registerFunc{ registerFunc }
        , _pNext{ nullptr }
    {
        _pNext      = pModuleHead;
        pModuleHead = this;
    }
} // namespace sw
