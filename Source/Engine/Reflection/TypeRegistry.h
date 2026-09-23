/**
 * @file TypeRegistry.h
 * @brief TypeRegistry 와 정적 Type/Enum 등록기 연결입니다.
 */
#pragma once
#include "Core/Common/EnumUtil.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{

    /**
     * @brief 컴파일러 함수 시그니처에서 템플릿 인자 E 의 타입 이름만 뽑아냅니다.
     * @details `[E = Foo::Bar]`(clang/gcc)와 `typeFqn<enum Foo::Bar>`(MSVC) 두 형태를 모두 받습니다.
     *          반환 타입에도 `<>` 가 들어 있으므로 함수 이름을 기준점으로 삼아야 합니다.
     */
    inline hashed_string parseTypeFromSignature( string_view sig )
    {
        string_view inner;

        const size_t equalPos = sig.find( constants::reflection::kSignatureEq );
        if ( equalPos != string_view::npos )
        {
            const size_t eqLength     = StringUtil::strlen( constants::reflection::kSignatureEq );
            inner                     = sig.substr( equalPos + eqLength );
            const size_t semicolonPos = inner.find( ';' );
            const size_t bracketPos   = inner.find( ']' );
            size_t       end          = inner.size();
            if ( semicolonPos != string_view::npos )
                end = semicolonPos;
            if ( bracketPos != string_view::npos && bracketPos < end )
                end = bracketPos;
            inner = inner.substr( 0, end );
        }
        else
        {
            constexpr string_view kMarker       = constants::reflection::kTypeFqnPrefix;
            const size_t          markerPos     = sig.find( kMarker );
            const size_t          closeAnglePos = sig.rfind( '>' );
            if ( markerPos != string_view::npos && closeAnglePos != string_view::npos && closeAnglePos > markerPos + kMarker.size() )
                inner = sig.substr( markerPos + kMarker.size(), closeAnglePos - markerPos - kMarker.size() );
        }

        inner                                           = StringUtil::trim( inner );
        static constexpr const utf8* kArrTypePrefixes[] = {
            constants::reflection::kEnumClassPrefix,
            constants::reflection::kEnumStructPrefix,
            constants::reflection::kEnumPrefix,
            constants::reflection::kClassPrefix,
            constants::reflection::kStructPrefix,
        };
        for ( const utf8* prefix : kArrTypePrefixes )
        {
            if ( StringUtil::startsWith( inner, prefix ) )
            {
                inner.remove_prefix( StringUtil::strlen( prefix ) );
                break;
            }
        }
        inner = StringUtil::trim( inner );
        if ( inner.empty() )
            return {};
        return hashed_string( inner.data(), static_cast<uint32>( inner.size() ) );
    }

    /** @brief 컴파일러 시그니처에서 enum FQN을 추출합니다. */
    template <typename E>
    hashed_string typeFqn()
    {
        static const hashed_string kFqn = parseTypeFromSignature( SW_FUNCTION_SIGNATURE );
        return kFqn;
    }

    /**
     * @brief 타입 표의 세대입니다(`TypeRegistry::getGeneration()` 의 값). 등록 · 별칭 · 모듈 해제마다 오릅니다.
     * @details 레지스트리 객체 밖, 내보낸 전역에 두는 이유는 `TypeLookupCache::find` 가 인라인이기 때문입니다. 빈 답(미등록)을
     *          다시 찾을지 정하려고 이 값을 읽는데, 캐스트마다 두 번 오는 자리라 서비스 표를 거치거나 DLL 경계를 넘는 호출을
     *          하지 않고 헤더에서 원자 로드로 읽습니다. 레지스트리는 엔진에 하나뿐이라 같은 값입니다. 쓰는 쪽은 `TypeRegistry` 뿐입니다.
     */
    extern SW_API atomic<uint32> gv_typeTableGeneration;

    /**
     * @brief FQN 하나의 `findType` 결과를 적어 두는 칸입니다.
     * @details 코드젠의 `StaticType()` 은 부를 때마다 `findType( hashed_string( "sw::Foo" ) )` 을
     *          했습니다. 문자열 intern(샤드 뮤텍스) + shared_mutex 잠금 + 해시맵 조회입니다. 캐스트 한 번마다
     *          그것이 들어갔습니다. 이 칸은 한 번 찾은 포인터를 그대로 반환합니다.
     *
     *          **세대를 보지 않는 이유.** `TypeInfo` 의 주소는 고정입니다. 레지스트리가 `unique_ptr` 로 들고,
     *          표가 커져도 옮기지 않으며, 모듈 해제는 지우는 대신 `_bAlive` 를 내립니다(묘비). 같은 FQN 이 다시
     *          등록되면 같은 객체에 덮어써 되살립니다. 그래서 적중은 "포인터 하나 + 살아 있나" 로 끝납니다. 예전에는
     *          레지스트리 세대와 자기 세대를 견주는 로드 둘이 더 있었습니다. 캐스트마다 두 번 오는 자리입니다.
     *          세대(`gv_typeTableGeneration`)는 **빈 답**(미등록)을 매번 다시 찾지 않으려고만 씁니다.
     *          핫 리로드로 모듈이 사라지면 그 모듈의 정적 칸도 같이 사라지고, 다시 올라온 모듈의 칸은
     *          비어서 첫 호출에 찾습니다.
     */
    struct SW_API TypeLookupCache
    {
        mutable atomic<const TypeInfo*> _pType{ nullptr };
        mutable atomic<uint32>          _generation{ 0 }; ///< 빈 답을 적은 세대. `_pType` 이 nullptr 일 때만 봅니다

        /**
         * @brief fqn 의 TypeInfo 입니다. 찾아 둔 포인터가 있으면 그것(해제됐으면 nullptr), 없으면 `findSlow` 로 찾습니다. 미등록이면 nullptr 입니다.
         * @details 적중 경로는 여기 인라인입니다. 캐스트마다 `StaticType()` 과 `getTypeInfo()` 로 두 번 오는 자리라
         *          DLL 경계 호출 하나가 곧 비용이었습니다.
         */
        const TypeInfo* find( const hashed_string& fqn ) const
        {
            const TypeInfo* pType = _pType.load( std::memory_order_acquire );
            if ( pType != nullptr )
                return pType->isAlive() ? pType : nullptr;
            if ( _generation.load( std::memory_order_acquire ) == gv_typeTableGeneration.load( std::memory_order_acquire ) )
                return nullptr;
            return findSlow( fqn );
        }
        /** @brief 아직 찾지 못했거나 빈 답의 세대가 지났을 때의 경로입니다. 레지스트리가 없으면 캐시를 건드리지 않고 nullptr 입니다. */
        const TypeInfo* findSlow( const hashed_string& fqn ) const;
        /** @brief 다음 호출이 반드시 다시 찾게 합니다(조회 키가 바뀌었을 때). */
        void reset()
        {
            _pType.store( nullptr, std::memory_order_relaxed );
            _generation.store( 0, std::memory_order_relaxed );
        }
    };

    /**
     * @class TypeRegistry
     * @brief 리플렉션 TypeInfo / EnumInfo 를 등록 · 조회하고 별칭을 관리합니다.
     */
    class SW_API TypeRegistry
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 — 모듈별 등록/해제, 복제 금지
        // ------------------------------------------------------------------------------
        /** @brief 빈 레지스트리를 만듭니다. */
        TypeRegistry();
        /** @brief 등록된 타입을 정리합니다. */
        ~TypeRegistry();

        /** @brief 복사를 금지합니다. */
        TypeRegistry( const TypeRegistry& ) = delete;
        /** @brief 대입을 금지합니다. */
        TypeRegistry& operator=( const TypeRegistry& ) = delete;

        // ------------------------------------------------------------------------------
        // 2) 모듈 일괄 등록 — registrar 체인 → 맵, 핫리로드 시 모듈 단위 해제
        // ------------------------------------------------------------------------------
        /** @brief 모듈의 대기 중인 Type/Enum 등록기 체인을 등록합니다. */
        void registerPendingTypes( string_view moduleName, struct TypeRegistrar* pClassHead, struct EnumRegistrar* pEnumHead );
#if !defined( SW_SHIPPING )
        /** @brief 해당 모듈이 등록한 타입을 모두 해제합니다. */
        void unregisterTypesByModule( string_view moduleName );
#endif

        // ------------------------------------------------------------------------------
        // 3) 단건 등록 · 별칭 — REFLECT(Alias=…) / ReflectBuiltins codegen
        // ------------------------------------------------------------------------------
        /** @brief TypeInfo를 등록합니다. */
        void registerClass( const TypeInfo& info );
        /** @brief EnumInfo를 등록합니다. */
        void registerEnum( const EnumInfo& info );

        /**
         * @brief 옛 이름을 이미 등록된 canonical TypeInfo 에 연결합니다(직렬화 · 컴포넌트 키 호환).
         * @details REFLECT(Alias=…) / ReflectBuiltins 별칭 코드젠이 부릅니다.
         */
        void registerTypeAlias( const utf8* pAliasName, const utf8* pCanonicalName );
        /** @brief 옛 이름을 이미 등록된 canonical EnumInfo 에 연결합니다. */
        void registerEnumAlias( const utf8* pAliasName, const utf8* pCanonicalName );

        // ------------------------------------------------------------------------------
        // 4) 조회 — 이름/FQN/해시, 별칭 포함
        // ------------------------------------------------------------------------------
        /** @brief 이름 또는 FQN으로 TypeInfo를 찾습니다. */
        const TypeInfo* findType( const hashed_string& nameOrFqn ) const;
        /**
         * @brief 타입 표가 바뀔 때마다 오르는 세대입니다. 적어 둔 빈 답(`TypeLookupCache` 의 미등록 · 풀지 못한 부모)을
         *        다시 찾을지 이것으로 정합니다.
         * @details 등록 · 별칭 · 모듈 해제 모두에서 오릅니다. 0 은 "아직 아무것도 등록되지 않음" 이라
         *          `TypeLookupCache` 의 초기값과 구별됩니다. 값은 `gv_typeTableGeneration` 입니다. 왜 레지스트리 객체
         *          밖에 있는지는 그 선언에 적혀 있습니다. `findType` 이 내준 포인터는 세대와 무관하게 **영원히 유효**합니다
         *          (주소 고정 · 묘비). 살아 있는지는 `TypeInfo::isAlive()` 가 답합니다.
         */
        uint32 getGeneration() const;
        /** @brief 이름 또는 FQN으로 EnumInfo를 찾습니다. */
        const EnumInfo* findEnum( const hashed_string& nameOrFqn ) const;

        /** @brief 템플릿 인자 타입 T의 TypeInfo를 조회합니다. */
        template <typename T>
        const TypeInfo* findType() const
        {
            if constexpr ( HasStaticType_v<T> )
                return T::StaticType();
            else if constexpr ( HasReflectStaticType_v<T> )
                return ReflectTypeTraits<T>::StaticType();
            else
                return findType( typeFqn<T>() );
        }

        /** @brief 템플릿 enum 타입 E의 EnumInfo를 조회합니다. */
        template <typename E>
        const EnumInfo* findEnum() const
        {
            return findEnumOf<E>();
        }

        /**
         * @brief 등록된 모든 타입의 지연 조회 캐시를 만들어 둡니다. 등록 배치가 끝난 뒤 부릅니다.
         * @details `TypeInfo` 는 이름→프로퍼티 맵과 상속 병합 목록을 **첫 조회 때 `mutable` 로,
         *          잠금 없이** 채웁니다. 그래서 워커 둘이 같은 타입을 처음 조회하면 같은 맵에 동시에
         *          삽입합니다. 등록이 끝난 직후 **단일 스레드에서** 한 번 만들어 그 틈을 없앱니다.
         *
         *          `TypeInfo` 는 `unique_ptr` 로 들어 주소가 고정이므로 등록하는 자리에서 만들어도 날아가지는
         *          않습니다. 그래도 배치 끝 한 자리에서 만드는 이유는 위의 단일 스레드 보장입니다. 재등록은 같은
         *          객체에 덮어쓰며 캐시를 비우므로, 그 뒤에도 여기가 다시 만듭니다.
         * @note 레지스트리 잠금을 **잡지 않은 채** 만듭니다. 상속 병합이 부모를 찾으려고 레지스트리를
         *       다시 잠그는데 `shared_mutex` 는 재귀가 아니라서 잠금 안에서 부르면 그 자리에서 멈춥니다.
         */
        void buildLookupCaches() const;

        /** @brief 등록된 모든 고유 TypeInfo를 순회합니다. */
        template <typename Func>
        void forEachType( Func&& func ) const
        {
            std::shared_lock<std::shared_mutex> lock( _mutex );
            for ( const auto& [fqn, pTypeInfo] : _mapFqnToClassType )
            {
                (void)fqn;
                if ( pTypeInfo->isAlive() )
                    func( *pTypeInfo );
            }
        }

        /** @brief 등록된 모든 고유 EnumInfo를 순회합니다. */
        template <typename Func>
        void forEachEnum( Func&& func ) const
        {
            std::shared_lock<std::shared_mutex> lock( _mutex );
            for ( const auto& [key, enumInfo] : _mapNameToEnum )
            {
                if ( key == enumInfo._fullyQualifiedName )
                    func( enumInfo );
            }
        }

        /** @brief BaseType으로부터 파생된 모든 등록 TypeInfo 목록을 반환합니다. */
        template <typename BaseType>
        vector<const TypeInfo*> getDerivedTypes() const
        {
            const TypeInfo* pBaseType = findType<BaseType>();
            if ( pBaseType == nullptr )
                return {};

            const hashed_string&    baseFqn = pBaseType->_fullyQualifiedName;
            vector<const TypeInfo*> listResult;

            std::shared_lock<std::shared_mutex> lock( _mutex );
            for ( const auto& [fqn, pTypeInfo] : _mapFqnToClassType )
            {
                (void)fqn;
                if ( pTypeInfo->isAlive() && pTypeInfo.get() != pBaseType && pTypeInfo->isDerivedFrom( baseFqn ) )
                    listResult.push_back( pTypeInfo.get() );
            }
            return listResult;
        }

        /** @brief 별칭까지 따라가 canonical `_name` 을 반환합니다(미등록이면 입력 그대로). */
        hashed_string canonicalTypeName( const hashed_string& nameOrFqn ) const;
        /** @brief 맵 키 해시가 일치하는 항목의 canonical `_name` 입니다. */
        hashed_string canonicalTypeNameByHash( uint32 nameHash ) const;

        /** @brief nameOrFqn 이 canonicalName 과 같은 등록 타입인지 검사합니다(별칭 포함). */
        bool isType( const hashed_string& nameOrFqn, const hashed_string& canonicalName ) const;
        /** @brief C 문자열 canonical 이름으로 타입 일치 여부를 검사합니다. */
        bool isType( const hashed_string& nameOrFqn, const utf8* pCanonicalName ) const;

        // ------------------------------------------------------------------------------
        // 5) Enum 변환 · 비트플래그
        // ------------------------------------------------------------------------------
        /** @brief enumerator 이름입니다. Invalid/Count 이거나 미등록이면 nullptr 입니다. */
        const utf8* enumToString( const hashed_string& enumName, int64 value ) const;
        /** @brief 이름을 값으로 바꿉니다. 실패하면 false 이고 outValue 는 Invalid(없으면 0)입니다. */
        bool enumFromString( const hashed_string& enumName, string_view name, int64& outValue ) const;
        /** @brief ENUM(Flags) 비트플래그 EnumInfo 에 대해 `(flags & contains) == contains` 인지 검사합니다. */
        bool hasFlag( const hashed_string& enumName, int64 flags, int64 contains ) const;

        /** @brief 템플릿 enum 값을 등록된 이름으로 변환합니다. */
        template <typename E>
        const utf8* enumToString( E value ) const
        {
            static_assert( std::is_enum_v<E>, "enumToString requires an enum type" );
            const EnumInfo* pInfo = findEnumOf<E>();
            return pInfo != nullptr ? pInfo->valueToCString( static_cast<int64>( value ) ) : nullptr;
        }

        /** @brief 문자열을 템플릿 enum 값으로 파싱합니다. */
        template <typename E>
        bool enumFromString( string_view name, E& outValue ) const
        {
            static_assert( std::is_enum_v<E>, "enumFromString requires an enum type" );
            const EnumInfo* pInfo = findEnumOf<E>();
            int64           raw{ 0 };
            if ( pInfo == nullptr )
            {
                outValue = E{};
                return false;
            }
            if ( pInfo->tryParse( name, raw ) && pInfo->isValidValue( raw ) )
            {
                outValue = static_cast<E>( raw );
                return true;
            }
            outValue = static_cast<E>( ( pInfo->_bHasInvalid != SW_FALSE ) ? pInfo->_invalidValue : 0 );
            return false;
        }

        /** @brief 파싱에 실패하면 Invalid(없으면 0)를 반환합니다. */
        template <typename E>
        E enumFromString( string_view name ) const
        {
            E value{};
            enumFromString( name, value );
            return value;
        }

        /**
         * @brief 템플릿 비트플래그 enum 에 대해 포함 여부를 검사합니다.
         * @details ENUM(Flags) 로 등록된 타입인지 런타임에 검증합니다. 타입을 컴파일 타임에 알고
         *          있고 레지스트리 검증이 필요 없는 핫패스라면, 조회 없이 바로 계산하는
         *          sw::EnumUtil::hasFlag(Core/Common/EnumUtil.h)를 대신 쓰십시오.
         *          실제 비트 연산 로직은 EnumUtil 에만 있고, 여기서는 넘기기만 합니다.
         */
        template <typename E>
        bool hasFlag( E flags, E contains ) const
        {
            static_assert( std::is_enum_v<E>, "hasFlag requires an enum type" );
            const EnumInfo* pInfo = findEnumOf<E>();
            if ( pInfo == nullptr || pInfo->_bIsBitFlag == SW_FALSE )
                return false;
            return EnumUtil::hasFlag( flags, contains );
        }

        // ------------------------------------------------------------------------------
        // 6) 리플렉션 호출
        // ------------------------------------------------------------------------------
        /** @brief 등록된 메서드를 인자 목록으로 호출합니다. */
        TaskValue invokeMethod( void* pInstance, const hashed_string& classFqn, const hashed_string& methodName, const TaskArgs& args = {} ) const;

    private:
        /** @brief 템플릿 enum의 FQN/리프로 EnumInfo를 찾습니다. */
        template <typename E>
        const EnumInfo* findEnumOf() const
        {
            const hashed_string fqn   = typeFqn<E>();
            const EnumInfo*     pInfo = findEnum( fqn );
            if ( pInfo != nullptr )
                return pInfo;
            const utf8* pCstr = fqn.c_str();
            if ( pCstr == nullptr || *pCstr == 0 )
                return nullptr;
            const string_view fqnView{ pCstr };
            const size_t      lastScope = fqnView.rfind( constants::reflection::kScopeDelimiter );
            if ( lastScope != string_view::npos )
            {
                const size_t      delimiterLength = StringUtil::strlen( constants::reflection::kScopeDelimiter );
                const string_view leaf            = fqnView.substr( lastScope + delimiterLength );
                return findEnum( hashed_string( leaf.data(), static_cast<uint32>( leaf.size() ) ) );
            }
            return nullptr;
        }

        mutable std::shared_mutex _mutex;
        /**
         * @brief FQN 하나당 TypeInfo **하나**입니다. 짧은 이름 · 별칭은 값을 복사하지 않고 `_mapAliasToFqn`
         *        으로 이 항목을 가리킵니다. `const TypeInfo*` 를 키로 쓰는 쪽(컴포넌트 풀 등)이
         *        이름을 무엇으로 조회했느냐에 따라 다른 포인터를 받으면 안 됩니다. 값은 **주소 고정**입니다. 커져도
         *        옮기지 않고, 모듈 해제는 지우지 않고 `_bAlive` 를 내립니다(묘비).
         */
        unordered_map<hashed_string, unique_ptr<TypeInfo>> _mapFqnToClassType;
        /** @brief 짧은 이름 · 별칭 → FQN 입니다. 조회는 여기를 거쳐 `_mapFqnToClassType` 한 곳으로 모입니다. */
        unordered_map<hashed_string, hashed_string> _mapAliasToFqn;
        unordered_map<hashed_string, EnumInfo>      _mapNameToEnum;
        unordered_map<uint32, hashed_string>        _mapHashToCanonicalName;
        hashed_string                               _activeModuleName;
    };

    // ------------------------------------------------------------------------------
    // 7) TypeRegistrar — 정적 초기화로 Type 등록 함수를 체인에 연결
    //    Core TU는 getHead(), 핫리로드 모듈은 모듈 로컬 헤드
    // ------------------------------------------------------------------------------
    struct SW_API TypeRegistrar
    {
        void ( *_registerFunc )( TypeRegistry& ); ///< TypeRegistry에 TypeInfo를 넣는 함수
        TypeRegistrar* _pNext;                    ///< 같은 헤드의 다음 registrar

        /** @brief Engine.dll(Core OBJECT) 전용 registrar 리스트 헤드. */
        static TypeRegistrar*& getHead();

        /** @brief Engine.dll(Core OBJECT) 정적 등록에 사용합니다. */
        TypeRegistrar( void ( *registerFunc )( TypeRegistry& ) );
        /** @brief 핫리로드 모듈 등 외부 registrar 등록에 사용합니다. */
        TypeRegistrar( void ( *registerFunc )( TypeRegistry& ), TypeRegistrar*& pModuleHead );
    };

    // ------------------------------------------------------------------------------
    // 8) EnumRegistrar — 정적 초기화로 Enum 등록 함수를 체인에 연결
    //    Core TU는 getHead(), 핫리로드 모듈은 모듈 로컬 헤드
    // ------------------------------------------------------------------------------
    struct SW_API EnumRegistrar
    {
        void ( *_registerFunc )( TypeRegistry& ); ///< TypeRegistry에 EnumInfo를 넣는 함수
        EnumRegistrar* _pNext;                    ///< 같은 헤드의 다음 registrar

        /** @brief Engine.dll(Core OBJECT) 전용 enum registrar 리스트 헤드. */
        static EnumRegistrar*& getHead();

        /** @brief Engine.dll(Core OBJECT) 정적 등록에 사용합니다. */
        EnumRegistrar( void ( *registerFunc )( TypeRegistry& ) );
        /** @brief 핫리로드 모듈 등 외부 registrar 등록에 사용합니다. */
        EnumRegistrar( void ( *registerFunc )( TypeRegistry& ), EnumRegistrar*& pModuleHead );
    };

    // ------------------------------------------------------------------------------
    // 9) 인라인 구현 — 조회 / 검사
    // ------------------------------------------------------------------------------
    inline hashed_string TypeRegistry::canonicalTypeName( const hashed_string& nameOrFqn ) const
    {
        const TypeInfo* pInfo = findType( nameOrFqn );
        if ( pInfo != nullptr )
        {
            if ( pInfo->_name.empty() == false )
                return pInfo->_name;
            return pInfo->_fullyQualifiedName;
        }
        return nameOrFqn;
    }

    /** @brief nameOrFqn 이 canonicalName 과 같은 등록 타입인지 검사합니다(별칭 포함). */
    inline bool TypeRegistry::isType( const hashed_string& nameOrFqn, const hashed_string& canonicalName ) const
    {
        const TypeInfo* pLhs = findType( nameOrFqn );
        const TypeInfo* pRhs = findType( canonicalName );
        if ( pLhs != nullptr && pRhs != nullptr )
            return pLhs->_typeId == pRhs->_typeId;
        return canonicalTypeName( nameOrFqn ) == canonicalTypeName( canonicalName );
    }

    /** @brief C 문자열 canonical 이름으로 타입 일치 여부를 검사합니다. */
    inline bool TypeRegistry::isType( const hashed_string& nameOrFqn, const utf8* pCanonicalName ) const { return pCanonicalName != nullptr && isType( nameOrFqn, hashed_string( pCanonicalName ) ); }
} // namespace sw

namespace sw::engine
{
    /** @brief 전역 TypeRegistry 인스턴스를 반환합니다. */
    SW_API TypeRegistry& getTypeRegistry();
} // namespace sw::engine

#ifndef SW_TYPE_MODULE_HEAD
    #define SW_TYPE_MODULE_HEAD() ( ::sw::TypeRegistrar::getHead() )
#endif
#ifndef SW_ENUM_MODULE_HEAD
    #define SW_ENUM_MODULE_HEAD() ( ::sw::EnumRegistrar::getHead() )
#endif
