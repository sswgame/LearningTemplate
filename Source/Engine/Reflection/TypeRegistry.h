/**
 * @file TypeRegistry.h
 * @brief TypeRegistry와 정적 Type/Enum registrar 연결
 */
#pragma once
#include "Core/Common/EnumUtil.h"
#include "Core/Common/StdHeaders.h"
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
     * @brief FQN 하나의 `findType` 결과를 레지스트리 세대와 함께 적어 두는 칸.
     * @details 코드젠의 `StaticType()` 은 부를 때마다 `findType( hashed_string( "sw::Foo" ) )` 을
     *          했다 — 문자열 intern(샤드 뮤텍스) + shared_mutex 잠금 + 해시맵 조회. 캐스트 한 번마다
     *          그것이 들어갔다. 이 칸은 `TypeRegistry::getGeneration()` 이 같은 동안 지난 답을 그대로
     *          돌려주고, 등록·해제로 세대가 바뀌면 한 번만 다시 찾는다.
     *
     *          **왜 세대인가.** `findType` 이 내준 포인터는 다음 등록에서 무효가 된다(타입 표가 밀집
     *          배열이라 커질 때 원소를 옮긴다). 세대는 그 모든 사건에서 오르므로, 세대가 같으면
     *          포인터는 아직 그 자리다. 핫리로드로 모듈이 사라지면 그 모듈의 정적 칸도 같이 사라지고,
     *          다시 올라온 모듈의 칸은 세대 0 으로 시작해 첫 호출에 다시 찾는다.
     *
     *          두 원자값을 따로 쓰는 경쟁은 무해하다: 쓰는 쪽은 포인터를 먼저, 세대를 나중에(release)
     *          적고, 읽는 쪽은 세대를 먼저(acquire) 본다. 세대를 읽고 나서 찾는 사이에 등록이 끼면
     *          "옛 세대 도장 + 새 포인터" 가 남는데, 다음 호출이 도장이 다르다고 보고 다시 찾는다 —
     *          "새 도장 + 옛 포인터" 는 만들어지지 않는다.
     */
    struct SW_API TypeLookupCache
    {
        mutable atomic<const TypeInfo*> _pType{ nullptr };
        mutable atomic<uint32>          _generation{ 0 };

        /** @brief fqn 의 TypeInfo. 세대가 같으면 캐시, 아니면 `findType` 뒤 갱신. 미등록이면 nullptr. */
        const TypeInfo* find( const hashed_string& fqn ) const;
        /** @brief 다음 호출이 반드시 다시 찾게 한다(조회 키가 바뀌었을 때). */
        void reset() { _generation.store( 0, std::memory_order_relaxed ); }
    };

    /**
     * @class TypeRegistry
     * @brief 리플렉션 TypeInfo / EnumInfo 등록·조회·별칭
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
        /** @brief 모듈의 pending Type/Enum registrar 체인을 등록합니다. */
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
         * @brief 옛 이름 → 이미 등록된 canonical TypeInfo (직렬화/컴포넌트 키 호환).
         * @details REFLECT(Alias=…) / ReflectBuiltins 별칭 codegen이 호출한다.
         */
        void registerTypeAlias( const utf8* pAliasName, const utf8* pCanonicalName );
        /** @brief 옛 이름 → 이미 등록된 canonical EnumInfo. */
        void registerEnumAlias( const utf8* pAliasName, const utf8* pCanonicalName );

        // ------------------------------------------------------------------------------
        // 4) 조회 — 이름/FQN/해시, 별칭 포함
        // ------------------------------------------------------------------------------
        /** @brief 이름 또는 FQN으로 TypeInfo를 찾습니다. */
        const TypeInfo* findType( const hashed_string& nameOrFqn ) const;
        /**
         * @brief 타입 표가 바뀔 때마다 오르는 세대. `findType` 이 내준 포인터는 이 값이 같은 동안만 유효하다.
         * @details 등록·별칭·모듈 해제 모두에서 오른다. 0 은 "아직 아무것도 등록되지 않음" 이라
         *          `TypeLookupCache` 의 초기값과 구별된다.
         */
        uint32 getGeneration() const { return _generation.load( std::memory_order_acquire ); }
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
         *          잠금 없이** 채운다. 그래서 워커 둘이 같은 타입을 처음 조회하면 같은 맵에 동시에
         *          삽입한다. 등록이 끝난 직후 **단일 스레드에서** 한 번 만들어 그 창을 없앤다.
         *
         *          **등록하는 자리에서 하나씩 만들 수는 없다.** `_mapFqnToClassType` 은
         *          `sw::unordered_map`(밀집 배열)이라 커질 때 원소를 **옮기고**, `TypeInfo` 이동
         *          생성자는 `mutable` 캐시를 비운다 — 그래서 뒤이은 등록 하나가 앞서 만든 캐시를
         *          전부 날린다. 배치의 마지막 삽입 뒤에 한 번 도는 것이 유일하게 성립하는 자리다.
         *          같은 이유로 `findType()` 이 내준 `const TypeInfo*` 도 **다음 등록에서 무효가 된다**
         *          (`GameObjectManager::rebindAllCachedTypeInfo` 가 그래서 있다).
         * @note 레지스트리 잠금을 **잡지 않은 채** 만든다 — 상속 병합이 부모를 찾으려고 레지스트리를
         *       다시 잠그는데 `shared_mutex` 는 재귀가 아니라서 잠금 안에서 부르면 그 자리에서 멈춘다.
         */
        void buildLookupCaches() const;

        /** @brief 등록된 모든 고유 TypeInfo를 순회합니다. */
        template <typename Func>
        void forEachType( Func&& func ) const
        {
            std::shared_lock<std::shared_mutex> lock( _mutex );
            for ( const auto& [fqn, typeInfo] : _mapFqnToClassType )
            {
                (void)fqn;
                func( typeInfo );
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
            for ( const auto& [fqn, typeInfo] : _mapFqnToClassType )
            {
                (void)fqn;
                if ( &typeInfo != pBaseType && typeInfo.isDerivedFrom( baseFqn ) )
                    listResult.push_back( &typeInfo );
            }
            return listResult;
        }

        /** @brief 별칭 포함 조회 후 canonical `_name` (미등록이면 입력 그대로). */
        hashed_string canonicalTypeName( const hashed_string& nameOrFqn ) const;
        /** @brief 맵 키 해시가 일치하는 항목의 canonical `_name`. */
        hashed_string canonicalTypeNameByHash( uint32 nameHash ) const;

        /** @brief nameOrFqn이 canonicalName과 같은 등록 타입인지 (별칭 포함). */
        bool isType( const hashed_string& nameOrFqn, const hashed_string& canonicalName ) const;
        /** @brief C 문자열 canonical 이름으로 타입 일치 여부를 검사합니다. */
        bool isType( const hashed_string& nameOrFqn, const utf8* pCanonicalName ) const;

        // ------------------------------------------------------------------------------
        // 5) Enum 변환 · 비트플래그
        // ------------------------------------------------------------------------------
        /** @brief enumerator 이름. Invalid/Count이거나 미등록이면 nullptr. */
        const utf8* enumToString( const hashed_string& enumName, int64 value ) const;
        /** @brief 이름 → 값. 실패 시 false이고 outValue는 Invalid(없으면 0). */
        bool enumFromString( const hashed_string& enumName, string_view name, int64& outValue ) const;
        /** @brief ENUM(Flags) / 비트플래그 EnumInfo에 대해 `(flags & contains) == contains`. */
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

        /** @brief 파싱에 실패하면 Invalid(없으면 0)를 돌려줍니다. */
        template <typename E>
        E enumFromString( string_view name ) const
        {
            E value{};
            enumFromString( name, value );
            return value;
        }

        /**
         * @brief 템플릿 비트플래그 enum에 대해 포함 여부를 검사합니다.
         * @details ENUM(Flags)로 등록된 타입인지 런타임에 검증합니다. 타입을 컴파일 타임에 알고
         *          있고 레지스트리 검증이 필요 없는 핫패스라면, 조회 없이 바로 계산하는
         *          sw::EnumUtil::hasFlag(Core/Common/EnumUtil.h)를 대신 사용하세요.
         *          실제 비트 연산 로직은 EnumUtil에만 있고, 여기서는 위임만 합니다.
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
         * @brief FQN 하나당 TypeInfo **하나**. 짧은 이름·별칭은 값을 복사하지 않고 `_mapAliasToFqn`
         *        으로 이 항목을 가리킨다 — `const TypeInfo*` 를 키로 쓰는 쪽(컴포넌트 풀 등)이
         *        이름을 무엇으로 조회했느냐에 따라 다른 포인터를 받으면 안 된다.
         */
        unordered_map<hashed_string, TypeInfo> _mapFqnToClassType;
        /** @brief 짧은 이름·별칭 → FQN. 조회는 여기를 거쳐 `_mapFqnToClassType` 한 곳으로 모인다. */
        unordered_map<hashed_string, hashed_string> _mapAliasToFqn;
        unordered_map<hashed_string, EnumInfo>      _mapNameToEnum;
        unordered_map<uint32, hashed_string>        _mapHashToCanonicalName;
        hashed_string                               _activeModuleName;
        atomic<uint32>                              _generation;
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

    /** @brief nameOrFqn이 canonicalName과 같은 등록 타입인지 (별칭 포함). */
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

    /** @brief 부모 FQN을 따라가며 targetFqn에서 파생됐는지 검사합니다. */
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
