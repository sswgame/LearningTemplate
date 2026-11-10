/**
 * @file TypeRegistry.h
 * @brief TypeRegistry 와 정적 Type/Enum 등록기 연결입니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/EnumUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Memory/Memory.h"
#include "Core/Registry/RegistrationChain.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Reflection/TypeLookupCache.h"

namespace sw
{
    struct EnumEditorMetadata;
    struct TypeEditorMetadataSet;

    /**
     * @class TypeRegistry
     * @brief 리플렉션 TypeInfo 와 EnumInfo 를 등록하고 조회하며, 별칭을 관리합니다.
     */
    class SW_API TypeRegistry
    {
    public:
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::Reflection; ///< 할당 용도(`ISubsystem::getSubsystemMemoryTag`)
        /** @brief `subscribeModuleUnloading` 의 알림 모양입니다. 인자는 내려가는 모듈 이름(`TypeInfo::_moduleName` 과 같은 값)입니다. */
        using ModuleUnloadingDelegate = Delegate<void( string_view moduleName )>;
        // ------------------------------------------------------------------------------
        // 1) 수명: 모듈별 등록과 해제, 복제 금지
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
        // 2) 모듈 일괄 등록: registrar 체인을 맵에 넣고, 핫 리로드 때 모듈 단위로 해제한다
        // ------------------------------------------------------------------------------
        /** @brief 모듈의 대기 중인 Type/Enum 등록기 체인을 등록합니다. */
        void registerPendingTypes( struct TypeRegistrar* pClassHead, struct EnumRegistrar* pEnumHead );
        /**
         * @brief 해당 모듈이 등록한 타입을 모두 해제합니다.
         * @details 엔진에서는 핫 리로드(개발 빌드)만 부르지만 **모든 구성에서 컴파일한다**. 구성마다 있고 없는 API 는 부르는 쪽(테스트)마다
         *          `SW_SHIPPING` 가드를 기억해야 하고, 잊은 곳은 Shipping 빌드에서야 깨진다(`b206e2a26`). 레지스트리 연산이라 구성과 무관하게 맞다.
         */
        void unregisterTypesByModule( string_view moduleName );
        /**
         * @brief 모듈을 내리기 직전(그 모듈의 타입 · 코드가 아직 살아 있을 때)의 알림을 받습니다. 구독이 사라지면 뗍니다.
         * @details 모듈 타입의 인스턴스를 든 쪽(UI 화면 · 어빌리티 · 편집기 기본값)이 **타입의 모듈 이름**(데이터)으로 자기 것을 골라 놓는다 —
         *          컴포넌트를 지우는 `destroyComponentsOfModule` 과 같은 규칙이다(언리얼은 클래스의 패키지로 다시 만든다). 코드 주소로 고르지 않는다.
         *          `engine::unregisterModuleTypes` 가 타입을 걷기 전에 부른다(`publishModuleUnloading`). 모듈을 올리고 내리는 스레드에서만 불린다.
         */
        [[nodiscard]] Subscription subscribeModuleUnloading( const ModuleUnloadingDelegate& delegate );
        /** @brief 구독자에게 @p moduleName 이 내려간다고 알립니다(`engine::unregisterModuleTypes`). */
        void publishModuleUnloading( string_view moduleName );
        /**
         * @brief 모듈의 등록 줄을 한 차례(`ModuleObjectRegistrar::getDetachPass`) 뗀 뒤의 알림을 받습니다(그 모듈의 코드가 아직 살아 있을 때). 구독이 사라지면 뗍니다.
         * @details 등록 줄로 인스턴스를 만든 쪽(에디터 패널 · 팝업 · 확장 서브시스템)이 목록의 세대 번호(`RegistrationList::getGeneration`)와 맞춰 빠진 줄의 인스턴스를 거둔다.
         *          목록이 빼는 자리에서 전역 신호를 쏘던 것을 대신한다 — 신호는 모듈 범위의 이 서브시스템이 들고, 바뀜 판단은 받는 쪽이 번호로 한다.
         */
        [[nodiscard]] Subscription subscribeModuleObjectsDetached( const ModuleUnloadingDelegate& delegate );
        /** @brief 구독자에게 @p moduleName 의 등록 줄을 한 차례 뗐다고 알립니다(`engine::unregisterModuleTypes` 의 떼기). */
        void publishModuleObjectsDetached( string_view moduleName );
        /**
         * @brief 호스트의 모든 타입 공급자(엔진, GameFramework, 킷, 게임 모듈)가 등록을 끝냈다고 기록합니다. 기동 단계 `ModuleLoadStage` 가 부릅니다.
         * @details 씬을 읽는 쪽(`SceneManager::requestLoadFuture`, `SceneCooker::cookAllScenes`)은 이 뒤에만 읽는다. 그 전에 읽으면 아직 올라오지
         *          않은 모듈의 컴포넌트가 `MissingComponent` 로 생성된다. 핫 리로드가 모듈을 언로드했다 다시 로드해도 이 값은 내리지 않는다.
         *          그 사이에 만든 씬은 세대를 비교해 다시 만든다(`SceneManager::updateTransitions`).
         */
        void markAllModuleTypesRegistered();
        /** @brief `markAllModuleTypesRegistered` 가 불렸으면 true 입니다. 씬을 읽어도 되는지 나타냅니다. */
        bool areAllModuleTypesRegistered() const;

        // ------------------------------------------------------------------------------
        // 3) 단건 등록과 별칭: REFLECT(Alias=…), ReflectBuiltins 코드젠
        // ------------------------------------------------------------------------------
        /** @brief TypeInfo를 등록합니다. 값으로 받는다 — 생성 코드는 `std::move` 로 넘겨 프로퍼티 목록을 복사하지 않는다. */
        void registerClass( TypeInfo info );
        /** @brief EnumInfo를 등록합니다. */
        void registerEnum( const EnumInfo& info );

        /**
         * @brief 별칭(이름을 바꾸기 전의 이름)을 이미 등록된 canonical TypeInfo 에 연결합니다(직렬화와 컴포넌트 키 호환).
         * @details REFLECT(Alias=…) 와 ReflectBuiltins 별칭 코드젠이 부른다.
         */
        void registerTypeAlias( const utf8* pAliasName, const utf8* pCanonicalName );
        /** @brief 별칭(이름을 바꾸기 전의 이름)을 이미 등록된 canonical EnumInfo 에 연결합니다. */
        void registerEnumAlias( const utf8* pAliasName, const utf8* pCanonicalName );

        // ------------------------------------------------------------------------------
        // 4) 조회: 이름, FQN, 해시로 찾고 별칭도 찾는다
        // ------------------------------------------------------------------------------
        /** @brief 이름 또는 FQN으로 TypeInfo를 찾습니다. */
        const TypeInfo* findType( const hashed_string& nameOrFqn ) const;
        /**
         * @brief 타입 테이블이 바뀔 때마다 오르는 세대입니다. 기록해 둔 빈 결과(`TypeLookupCache` 의 미등록, 찾지 못한 부모)를
         *        다시 찾을지 이 값으로 정합니다.
         * @details 타입 · 열거형의 등록, 별칭, 모듈 해제, 레지스트리 파괴 모두에서 오른다. 0 은 "아직 아무것도 등록되지 않음"이다.
         *          레지스트리는 엔진에 하나뿐이라 정적 함수다(서비스 조회 없이 읽는다). 인라인 경로는 `getTypeTableGeneration` 이 같은 값을 읽는다.
         *          `findType` 이 반환한 포인터는 이 레지스트리가 살아 있는 동안 유효하다(주소 고정, 묘비). 살아 있는지는
         *          `TypeInfo::isAlive()` 가 알려 준다. 레지스트리보다 오래 사는 캐시는 세대를 담은 칸(`TypeCacheSlot`)에 둔다.
         */
        static uint32 getGeneration();
        /**
         * @brief 이름, FQN, 별칭으로 EnumInfo 를 찾습니다. 어느 이름으로 찾든 같은 객체이고, 등록이 더 일어나도 주소는 바뀌지 않습니다.
         * @details 모듈이 언로드되면 nullptr 를 반환하지만 객체는 남는다(묘비). 같은 FQN 이 다시 로드되면 그 객체에 덮어쓴다.
         */
        const EnumInfo* findEnum( const hashed_string& nameOrFqn ) const;
        /**
         * @brief 이름, FQN, 별칭의 **해시**로 EnumInfo 를 찾습니다. 바이너리 태그에 기록된 타입 해시가 enum 일 때 씁니다(`findWireTypeName`).
         * @details 해시로 찾는 테이블이 없어 이름을 전부 훑는다. 타입이 바뀐 필드를 읽는 드문 경우에만 부른다.
         */
        const EnumInfo* findEnumByNameHash( uint32 nameHash ) const;

        /**
         * @brief 이 타입이 **실제로 무엇으로 등록됐는지** 사람이 읽을 수 있는 텍스트로 반환합니다(진단용).
         * @details 모듈, 크기, 부모 체인을 보여 주고, 체인의 단계마다 선언된 프로퍼티(타입, 이 빌드의 실제 오프셋, 비트필드의 바이트와
         *          마스크, 접근자, 컨테이너, 범위, 플래그, 별칭)와 함수를 보여 준다. "왜 인스펙터에 없나", "왜 씬이 이 값을 못 읽나", "부모가 왜
         *          안 붙나"를 확인할 때 쓴다. 파서 쪽의 짝은 `ReflectionParser --dump` 이다.
         *          `-gv_dumpReflection=이름,이름` 을 주면 첫 프레임에 로그로 남긴다. 등록되지 않았으면 그렇다고 적는다.
         */
        string describeType( const hashed_string& nameOrFqn ) const;
        /** @brief 이 enum 의 등록 내용(모듈, 크기, 부호, 플래그, 열거자별 값)을 사람이 읽을 수 있는 텍스트로 반환합니다(`describeType` 의 짝). */
        string describeEnum( const hashed_string& nameOrFqn ) const;

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

        /** @brief 컴파일러가 만든 enum FQN(`typeFqn<E>()`)으로 찾고, 없으면 마지막 이름(leaf)으로 찾습니다. 없으면 nullptr 입니다. */
        const EnumInfo* findEnumByTypeFqn( const hashed_string& fqn ) const;

        /** @brief 템플릿 enum 타입 E의 EnumInfo를 조회합니다. */
        template <typename E>
        const EnumInfo* findEnum() const
        {
            return findEnumOf<E>();
        }

        /**
         * @brief 살아 있는 **모든** 타입의 파생 캐시(이름 맵 · 상속 병합 목록 · 부모 포인터 · 조상 표 · 판정)를 다시 짓습니다.
         * @details 등록 · 별칭 · 해제는 바뀐 타입과 그 자손만 스스로 다시 짓는다(배치 안이면 배치 끝이 한 번에) — 이 함수는 전체를 다시 지을 때
         *          (테스트, 진단)만 쓴다. 조회는 지어 둔 것을 읽기만 한다.
         * @note 레지스트리 잠금을 **잡지 않은 채** 짓는다. 부모를 찾으려고 레지스트리를 다시 잠그는데 `shared_mutex` 는 재귀 잠금이 안 되므로,
         *       잠금 안에서 부르면 그 자리에서 멈춘다.
         */
        void populateLookupCaches() const;

        /** @brief 지금까지 상속 캐시(`TypeInfo::clearInheritedProperties`)를 비운 타입 수의 누계입니다(계측 — `engine::registerModuleTypes` 가 모듈마다 차이를 남긴다). */
        uint64 getInheritedCacheInvalidations() const { return _inheritedCacheInvalidations.load( std::memory_order_relaxed ); }

        /** @brief 등록된 모든 고유 TypeInfo를 순회합니다. */
        template <typename Func>
        void forEachType( Func&& func ) const
        {
            ScopedSharedLock<shared_mutex> lock{ _mutex };
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
            ScopedSharedLock<shared_mutex> lock{ _mutex };
            for ( const auto& [key, pEnumInfo] : _mapNameToEnum )
            {
                if ( key == pEnumInfo->_fullyQualifiedName )
                    func( *pEnumInfo );
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

            ScopedSharedLock<shared_mutex> lock{ _mutex };
            for ( const auto& [fqn, pTypeInfo] : _mapFqnToClassType )
            {
                (void)fqn;
                const bool bDerivedAliveType = pTypeInfo->isAlive() && pTypeInfo.get() != pBaseType && pTypeInfo->isDerivedFrom( baseFqn );
                if ( bDerivedAliveType )
                    listResult.push_back( pTypeInfo.get() );
            }
            return listResult;
        }

        /** @brief 별칭까지 따라가 canonical `_name` 을 반환합니다. 미등록이면 입력을 그대로 반환합니다. */
        hashed_string canonicalTypeName( const hashed_string& nameOrFqn ) const;
        /** @brief 맵 키 해시가 일치하는 항목의 canonical `_name` 을 반환합니다. */
        hashed_string canonicalTypeNameByHash( uint32 nameHash ) const;

        /** @brief nameOrFqn 이 canonicalName 과 같은 등록 타입인지 검사합니다(별칭 포함). */
        bool isType( const hashed_string& nameOrFqn, const hashed_string& canonicalName ) const;
        /** @brief C 문자열 canonical 이름으로 타입 일치 여부를 검사합니다. */
        bool isType( const hashed_string& nameOrFqn, const utf8* pCanonicalName ) const;

        // ------------------------------------------------------------------------------
        // 5) Enum 변환과 비트플래그
        // ------------------------------------------------------------------------------
        /** @brief enumerator 이름을 반환합니다. Invalid 나 Count 이거나 미등록이면 nullptr 입니다. */
        const utf8* enumToString( const hashed_string& enumName, int64 value ) const;
        /** @brief 이름을 값으로 바꿉니다. 실패하면 false 를 반환하고 outValue 는 Invalid(없으면 0)가 됩니다. */
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
         * @details ENUM(Flags) 로 등록된 타입인지 런타임에 검증한다. 타입을 컴파일 타임에 알고
         *          있고 레지스트리 검증이 필요 없는 핫패스라면, 조회 없이 바로 계산하는
         *          sw::EnumUtil::hasFlag(Core/Foundation/EnumUtil.h)를 대신 쓴다.
         *          실제 비트 연산 로직은 EnumUtil 에만 있고, 여기서는 넘기기만 한다.
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
        /**
         * @brief 개발 전용 생성 단위가 올린 에디터 메타데이터(`TypeEditorMetadataSource`)를 @p info 와 그 프로퍼티 · 이벤트 · 메서드에 붙입니다. 잠금 안에서 부른다.
         * @details 줄이 없으면(배포 구성 · 적은 것 없음) 아무것도 하지 않아 포인터가 빈 채다. 묶음은 이 레지스트리가 든다 — 다시 등록하면 새 묶음을 만들고 옛 묶음은
         *          버리지 않고 모아 둔다(밖에서 든 옛 정보 사본이 가리킬 수 있다 — 핫 리로드 한 번에 타입 몇 개 분량이다).
         */
        void attachEditorMetadata( TypeInfo& info ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 열거형 @p info 에 에디터 메타데이터를 붙입니다(`attachEditorMetadata` 의 짝). 잠금 안에서 부른다. */
        void attachEditorMetadata( EnumInfo& info ) SW_REQUIRES_LOCK( _mutex );

        /** @brief 이름 · 별칭으로 타입을 찾습니다(잠금 안). @p bIncludeDead 이면 묘비도 돌려줍니다 — 해제가 내려가는 타입의 자손을 찾을 때 쓴다. */
        TypeInfo* findTypeLocked( const hashed_string& nameOrFqn, bool bIncludeDead ) const SW_REQUIRES_LOCK( _mutex );
        /**
         * @brief @p uniqueRoot 의 타입이거나 그 자손인 살아 있는 타입을 @p outListType 에 더합니다(잠금 안). 부모 이름을 걸어 올라가 본다 — 타입 수 × 사슬 깊이.
         * @details 상속 캐시를 비우고 다시 짓는 범위다. 예전에는 등록 한 번마다 **모든** 타입을 비워 무관한 키트 하나가 오를 때도 모든 상속 목록이
         *          다시 지어졌다(밖에서 든 `const PropertyInfo*` 가 매달리고 등록이 O(타입²)). 언리얼도 클래스를 다시 올릴 때 그 자식 클래스만 다시 잇는다.
         */
        void collectAffectedLocked( const unordered_set<const TypeInfo*>& uniqueRoot, vector<TypeInfo*>& outListType ) const SW_REQUIRES_LOCK( _mutex );
        /** @brief @p listType 의 부모 포인터 · 조상 표 · 상속 캐시를 비웁니다(잠금 안). 다시 짓기 전까지 그 목록을 묻는 쪽은 멈춘다(옛 포인터를 읽지 않는다). */
        void clearAffectedLocked( const vector<TypeInfo*>& listType ) SW_REQUIRES_LOCK( _mutex );
        /** @brief @p listType 의 파생 캐시를 다시 짓습니다(**잠금 밖**, 단일 스레드 — 부모를 찾으려고 레지스트리를 다시 잠근다). */
        void rebuildAffected( const vector<TypeInfo*>& listType ) const SW_EXCLUDES_LOCK( _mutex );

        /** @brief 템플릿 enum 의 EnumInfo 입니다. 찾는 규칙은 `findEnumByTypeFqn` 입니다. */
        template <typename E>
        const EnumInfo* findEnumOf() const
        {
            return findEnumByTypeFqn( typeFqn<E>() );
        }

    private:
        mutable shared_mutex _mutex;
        /**
         * @brief FQN 하나당 TypeInfo **하나**입니다. 짧은 이름과 별칭은 값을 복사하지 않고 `_mapAliasToFqn`
         *        으로 이 항목을 가리킵니다. `const TypeInfo*` 를 키로 쓰는 쪽(컴포넌트 풀 등)이
         *        이름을 무엇으로 조회했느냐에 따라 다른 포인터를 받으면 안 됩니다. 값은 **주소가 고정**입니다. 커져도
         *        옮기지 않고, 모듈을 해제해도 지우지 않고 `_castHeader._bAlive` 를 내립니다(묘비).
         */
        unordered_map<hashed_string, unique_ptr<TypeInfo>> _mapFqnToClassType SW_GUARDED_BY( _mutex );
        /** @brief 짧은 이름과 별칭에서 FQN 으로 가는 맵입니다. 조회는 여기를 거쳐 `_mapFqnToClassType` 한 곳으로 모입니다. */
        unordered_map<hashed_string, hashed_string> _mapAliasToFqn SW_GUARDED_BY( _mutex );
        /**
         * @brief FQN 하나당 EnumInfo **하나**입니다(주소 고정, 지우지 않음. TypeInfo 와 같은 규칙).
         * @details 이 맵은 밀집 배열이라 값으로 보관하면 enum 이 하나 더 등록되어 배열이 커질 때 **모든 EnumInfo 가 옮겨져** `findEnum` 이 반환한
         *          포인터가 죽는다(워커가 씬을 만드는 동안 모듈이 로드되는 경우). 짧은 이름과 별칭도 복사본이 아니라 같은 객체를 가리킨다.
         *          복사본이면 핫 리로드로 열거자가 늘어도 별칭 쪽이 이전 목록으로 답한다.
         */
        unordered_map<hashed_string, unique_ptr<EnumInfo>> _mapFqnToEnum SW_GUARDED_BY( _mutex );
        /** @brief FQN, 짧은 이름, 별칭에서 `_mapFqnToEnum` 의 객체로 가는 맵입니다. 모듈을 해제하면 여기서만 지웁니다. */
        unordered_map<hashed_string, EnumInfo*> _mapNameToEnum       SW_GUARDED_BY( _mutex );
        unordered_map<uint32, hashed_string> _mapHashToCanonicalName SW_GUARDED_BY( _mutex );
        /** @brief 타입 FQN 마다 붙인 에디터 메타데이터 묶음입니다(`attachEditorMetadata`). 배포 구성은 비어 있다. */
        unordered_map<hashed_string, unique_ptr<TypeEditorMetadataSet>> _mapTypeEditorMetadata SW_GUARDED_BY( _mutex );
        /** @brief 열거형 FQN 마다 붙인 에디터 메타데이터입니다. */
        unordered_map<hashed_string, unique_ptr<EnumEditorMetadata>> _mapEnumEditorMetadata SW_GUARDED_BY( _mutex );
        /** @brief 다시 등록되어 물러난 묶음입니다(옛 정보 사본이 가리킬 수 있어 지우지 않는다). */
        vector<unique_ptr<TypeEditorMetadataSet>> _listRetiredTypeEditorMetadata SW_GUARDED_BY( _mutex );
        vector<unique_ptr<EnumEditorMetadata>> _listRetiredEnumEditorMetadata    SW_GUARDED_BY( _mutex );
        /** @brief 모듈 내림 알림 구독자입니다(`subscribeModuleUnloading`). 모듈을 올리고 내리는 스레드만 만진다. */
        MulticastDelegate<void( string_view )> _moduleUnloading;
        /** @brief 등록 줄 떼기 알림 구독자입니다(`subscribeModuleObjectsDetached`). 모듈을 올리고 내리는 스레드만 만진다. */
        MulticastDelegate<void( string_view )> _moduleObjectsDetached;
        /** @brief 모든 타입 공급자가 등록을 끝냈는지 나타냅니다(`markAllModuleTypesRegistered`). 기동 스레드가 쓰고 씬을 읽는 쪽이 읽습니다. */
        atomic<bool> _bAllModuleTypesRegistered;
        /** @brief 상속 캐시를 비운 타입 수의 누계입니다(`getInheritedCacheInvalidations`). 잠금 안에서 더하고 아무 데서나 읽는다. */
        atomic<uint64> _inheritedCacheInvalidations{ 0 };
        /** @brief `registerPendingTypes` 의 깊이입니다. 0 이 아니면 `registerClass` · 별칭은 바뀐 타입만 모으고(`_uniqueBatchRoot`), 배치 끝이 자손을 한 번에 찾아 다시 짓는다. */
        uint32 _batchDepth SW_GUARDED_BY( _mutex ){ 0 };
        /** @brief 이 배치에서 다시 등록됐거나 별칭이 붙은 타입입니다. */
        unordered_set<const TypeInfo*> _uniqueBatchRoot SW_GUARDED_BY( _mutex );
    };

    SW_SERVICE_SLOT( TypeRegistry, Required );
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 7) TypeRegistrar: 정적 초기화로 Type 등록 함수를 사슬(`getChain`)에 단다. 올리는 쪽(`engine::registerModuleTypes`)이 이미지마다 걷어 간다.
    // ------------------------------------------------------------------------------
    struct SW_API TypeRegistrar
    {
        void ( *_registerFunc )( TypeRegistry& ); ///< TypeRegistry에 TypeInfo를 넣는 함수
        TypeRegistrar* _pNext;                    ///< 같은 사슬의 다음(먼저 만든) registrar

        /** @brief 아직 걷히지 않은 registrar 사슬입니다(Engine 이미지가 가진다). */
        static RegistrationChain<TypeRegistrar>& getChain();

        TypeRegistrar( void ( *registerFunc )( TypeRegistry& ) );
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 8) EnumRegistrar: 정적 초기화로 Enum 등록 함수를 사슬(`getChain`)에 단다(TypeRegistrar 와 같다).
    // ------------------------------------------------------------------------------
    struct SW_API EnumRegistrar
    {
        void ( *_registerFunc )( TypeRegistry& ); ///< TypeRegistry에 EnumInfo를 넣는 함수
        EnumRegistrar* _pNext;                    ///< 같은 사슬의 다음(먼저 만든) registrar

        /** @brief 아직 걷히지 않은 enum registrar 사슬입니다(Engine 이미지가 가진다). */
        static RegistrationChain<EnumRegistrar>& getChain();

        EnumRegistrar( void ( *registerFunc )( TypeRegistry& ) );
    };

    // ------------------------------------------------------------------------------
    // 9) 인라인 구현: 조회와 검사
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
            return pLhs->_typeID == pRhs->_typeID;
        return canonicalTypeName( nameOrFqn ) == canonicalTypeName( canonicalName );
    }

    /** @brief C 문자열 canonical 이름으로 타입 일치 여부를 검사합니다. */
    inline bool TypeRegistry::isType( const hashed_string& nameOrFqn, const utf8* pCanonicalName ) const
    {
        return pCanonicalName != nullptr && isType( nameOrFqn, hashed_string( pCanonicalName ) );
    }
} // namespace sw
