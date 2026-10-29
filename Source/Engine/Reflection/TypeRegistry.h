/**
 * @file TypeRegistry.h
 * @brief TypeRegistry 와 정적 Type/Enum 등록기 연결입니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/EnumUtil.h"
#include "Core/Foundation/StdHeaders.h"
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

        const size_t equalPos = sig.find( constant::reflection::kSignatureEq );
        if ( equalPos != string_view::npos )
        {
            const size_t eqLength     = StringUtil::strlen( constant::reflection::kSignatureEq );
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
            constexpr string_view kMarker       = constant::reflection::kTypeFqnPrefix;
            const size_t          markerPos     = sig.find( kMarker );
            const size_t          closeAnglePos = sig.rfind( '>' );
            const bool            bMarkerFound  = markerPos != string_view::npos && closeAnglePos != string_view::npos && closeAnglePos > markerPos + kMarker.size();
            if ( bMarkerFound )
                inner = sig.substr( markerPos + kMarker.size(), closeAnglePos - markerPos - kMarker.size() );
        }

        inner                                           = StringUtil::trim( inner );
        static constexpr const utf8* kArrTypePrefixes[] = {
            constant::reflection::kEnumClassPrefix,
            constant::reflection::kEnumStructPrefix,
            constant::reflection::kEnumPrefix,
            constant::reflection::kClassPrefix,
            constant::reflection::kStructPrefix,
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
     * @brief 타입 테이블 세대의 카운터입니다(`TypeRegistry::getGeneration` 과 같은 값). 인라인 적중 경로가 모듈마다 한 번 받아 둡니다.
     * @details 카운터는 Engine 안의 정적 저장소라 프로세스 수명 동안 주소가 고정이다. 데이터 export 대신 함수로 주소를 내준다(데이터 export 는 모듈 경계에 걸린다).
     */
    SW_API const atomic<uint32>& getTypeTableGenerationCounter();

    /** @brief 지금 타입 테이블 세대입니다. 처음 한 번만 DLL 경계를 넘어 카운터 주소를 받고, 그 뒤로는 원자 로드 하나입니다. */
    inline uint32 getTypeTableGeneration()
    {
        static const atomic<uint32>& s_generationCounter = getTypeTableGenerationCounter();
        return s_generationCounter.load( std::memory_order_acquire );
    }

    /**
     * @brief 타입 캐시 한 칸입니다. 찾은 포인터와 그것을 찾은 타입 테이블 세대를 **한 쌍으로** 쓰고 읽습니다.
     * @details 리플렉션 객체(`TypeInfo` · `EnumInfo`)를 함수 지역 정적 칸에 들고 있는 캐시는 모두 이 칸을 쓴다(`TypeLookupCache`, `EnumNameCache`).
     *          포인터와 세대를 따로 저장하면 두 스레드가 세대 경계에서 겹쳐 쓸 때 새 세대에 옛 포인터가 붙는다. 저장 순서를 바꿔서는 막히지 않는다.
     *          그래서 쌍을 순서 번호(seqlock)로 묶는다. 쓰는 쪽은 번호를 홀수로 올려 칸을 잡고(이미 잡혀 있으면 쓰지 않는다) 두 값을 쓴 뒤 짝수로 내린다.
     *          읽는 쪽은 앞뒤 번호가 같은 짝수일 때만 쌍을 믿는다. 쓰기와 겹친 읽기는 칸을 건너뛰고 레지스트리에서 찾는다(기다리지 않는다).
     *
     *          적중은 기록한 세대가 지금 세대와 같을 때뿐이다. 레지스트리 파괴도 세대를 올리므로(`~TypeRegistry`), 엔진을 다시 만든 뒤에
     *          해제된 객체를 내주지 않는다. 기록한 값은 "없음"(nullptr)일 수 있다. 세대가 그대로인 동안 미등록 이름을 매번 다시 찾지 않는다.
     *          처음 상태는 "세대 0 에 없음"이다. 세대 0 은 아직 아무것도 등록되지 않은 상태라 그 답이 맞다.
     */
    template <typename T>
    class TypeCacheSlot
    {
    public:
        /** @brief @p generation 에 기록한 값이 있으면 @p outValue 에 넣고 true 입니다. 기록한 값이 "없음"이면 @p outValue 는 nullptr 입니다. */
        [[nodiscard]] bool tryGet( uint32 generation, const T*& outValue ) const
        {
            const uint32 sequenceBefore = _sequence.load( std::memory_order_acquire );
            if ( ( sequenceBefore & 1u ) != 0 )
                return false;
            const T*     pValue             = _pValue.load( std::memory_order_relaxed );
            const uint32 recordedGeneration = _generation.load( std::memory_order_relaxed );
            std::atomic_thread_fence( std::memory_order_acquire );
            if ( _sequence.load( std::memory_order_relaxed ) != sequenceBefore )
                return false;
            if ( recordedGeneration != generation )
                return false;
            outValue = pValue;
            return true;
        }

        /** @brief @p generation 에 찾은 @p pValue(없으면 nullptr)를 기록합니다. 다른 스레드가 쓰는 중이면 기록하지 않습니다(다음 호출이 다시 찾습니다). */
        void set( uint32 generation, const T* pValue ) const
        {
            uint32 sequence = _sequence.load( std::memory_order_relaxed );
            if ( ( sequence & 1u ) != 0 )
                return;
            if ( _sequence.compare_exchange_strong( sequence, sequence + 1, std::memory_order_relaxed, std::memory_order_relaxed ) == false )
                return;
            std::atomic_thread_fence( std::memory_order_release );
            _pValue.store( pValue, std::memory_order_relaxed );
            _generation.store( generation, std::memory_order_relaxed );
            _sequence.store( sequence + 2, std::memory_order_release );
        }

    private:
        mutable atomic<uint32>   _sequence{ 0 };     ///< 짝수면 쌍이 온전하고, 홀수면 쓰는 중입니다
        mutable atomic<const T*> _pValue{ nullptr }; ///< 찾은 객체입니다. 없으면 nullptr 입니다
        mutable atomic<uint32>   _generation{ 0 };   ///< `_pValue` 를 찾은 타입 테이블 세대입니다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief FQN 하나의 `findType` 결과를 기록해 두는 캐시입니다.
     * @details 코드젠의 `StaticType()` 이 캐스트마다 `findType( hashed_string( "sw::Foo" ) )`(문자열 intern, shared_mutex 잠금,
     *          해시맵 조회)을 하지 않도록, 이 캐시가 한 번 찾은 포인터를 그대로 반환한다.
     *
     *          `TypeInfo` 의 주소는 **한 레지스트리 안에서만** 고정이다(묘비). 레지스트리를 파괴하면 객체도 지워지므로, 포인터만 들고 있으면
     *          엔진을 다시 만든 뒤 해제된 `TypeInfo` 를 가리킨다. 그래서 다른 타입 캐시와 같이 세대를 담은 칸(`TypeCacheSlot`)에 둔다.
     *          적중 경로는 인라인이고 DLL 경계 호출이 없다(세대는 `getTypeTableGeneration`). 캐스트마다 두 번 거치는 경로다.
     *          핫 리로드로 모듈이 사라지면 그 모듈의 정적 캐시도 같이 사라지고, 다시 로드된 모듈의 캐시는 비어 있어서 첫 호출에 찾는다.
     */
    struct SW_API TypeLookupCache
    {
        TypeCacheSlot<TypeInfo> _slot;

        /**
         * @brief fqn 의 TypeInfo 입니다. 지금 세대에 찾아 둔 답이 있으면 그것(해제됐으면 nullptr), 없으면 `findSlow` 로 찾습니다. 미등록이면 nullptr 입니다.
         * @details 적중 경로는 여기 인라인이다. 캐스트마다 `StaticType()` 과 `getTypeInfo()` 로 두 번 거치므로
         *          DLL 경계 호출 하나가 곧 비용이다. 놓친 경로는 `findSlow` 호출 하나다.
         */
        const TypeInfo* find( const hashed_string& fqn ) const
        {
            const TypeInfo* pType = nullptr;
            if ( _slot.tryGet( getTypeTableGeneration(), pType ) )
                return ( pType != nullptr && pType->isAlive() ) ? pType : nullptr;
            return findSlow( fqn );
        }
        /** @brief 지금 세대의 답이 없을 때 레지스트리에서 찾아 기록합니다. 레지스트리가 없으면 칸을 건드리지 않고 nullptr 을 반환합니다. */
        const TypeInfo* findSlow( const hashed_string& fqn ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class TypeRegistry
     * @brief 리플렉션 TypeInfo 와 EnumInfo 를 등록하고 조회하며, 별칭을 관리합니다.
     */
    class SW_API TypeRegistry
    {
    public:
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
        void registerPendingTypes( string_view moduleName, struct TypeRegistrar* pClassHead, struct EnumRegistrar* pEnumHead );
        /**
         * @brief 해당 모듈이 등록한 타입을 모두 해제합니다.
         * @details 엔진에서는 핫 리로드(개발 빌드)만 부르지만 **모든 구성에서 컴파일한다**. 구성마다 있고 없는 API 는 부르는 쪽(테스트)마다
         *          `SW_SHIPPING` 가드를 기억해야 하고, 잊은 곳은 Shipping 빌드에서야 깨진다(`b206e2a26`). 레지스트리 연산이라 구성과 무관하게 맞다.
         */
        void unregisterTypesByModule( string_view moduleName );
        /** @brief `subscribeModuleUnloading` 의 알림 모양입니다. 인자는 내려가는 모듈 이름(`TypeInfo::_moduleName` 과 같은 값)입니다. */
        using ModuleUnloadingDelegate = Delegate<void( string_view moduleName )>;
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
        /** @brief TypeInfo를 등록합니다. */
        void registerClass( const TypeInfo& info );
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
         * @brief 등록된 모든 타입의 지연 조회 캐시를 만들어 둡니다. 등록 배치가 끝난 뒤 부릅니다.
         * @details `TypeInfo` 는 이름→프로퍼티 맵과 상속 병합 목록을 **첫 조회 때 `mutable` 로,
         *          잠금 없이** 채운다. 그래서 워커 둘이 같은 타입을 처음 조회하면 같은 맵에 동시에
         *          삽입한다. 등록이 끝난 직후 **단일 스레드에서** 한 번 만들어 그 틈을 없앤다.
         *
         *          `TypeInfo` 는 `unique_ptr` 로 보관해 주소가 고정이므로 등록하는 곳에서 만들어도 사라지지는
         *          않는다. 그래도 배치 끝 한 곳에서 만드는 이유는 위의 단일 스레드 보장이다. 재등록은 같은
         *          객체에 덮어쓰며 캐시를 비우므로, 그 뒤에도 여기서 다시 만든다.
         * @note 레지스트리 잠금을 **잡지 않은 채** 만든다. 상속 병합이 부모를 찾으려고 레지스트리를
         *       다시 잠그는데 `shared_mutex` 는 재귀 잠금이 안 되므로, 잠금 안에서 부르면 그 자리에서 멈춘다.
         */
        void populateLookupCaches() const;

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
        /** @brief 템플릿 enum 의 EnumInfo 입니다. 찾는 규칙은 `findEnumByTypeFqn` 입니다. */
        template <typename E>
        const EnumInfo* findEnumOf() const
        {
            return findEnumByTypeFqn( typeFqn<E>() );
        }

        mutable shared_mutex _mutex;
        /**
         * @brief FQN 하나당 TypeInfo **하나**입니다. 짧은 이름과 별칭은 값을 복사하지 않고 `_mapAliasToFqn`
         *        으로 이 항목을 가리킵니다. `const TypeInfo*` 를 키로 쓰는 쪽(컴포넌트 풀 등)이
         *        이름을 무엇으로 조회했느냐에 따라 다른 포인터를 받으면 안 됩니다. 값은 **주소가 고정**입니다. 커져도
         *        옮기지 않고, 모듈을 해제해도 지우지 않고 `_bAlive` 를 내립니다(묘비).
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
        hashed_string                                                _activeModuleName;
        /** @brief 모듈 내림 알림 구독자입니다(`subscribeModuleUnloading`). 모듈을 올리고 내리는 스레드만 만진다. */
        MulticastDelegate<void( string_view )> _moduleUnloading;
        /** @brief 모든 타입 공급자가 등록을 끝냈는지 나타냅니다(`markAllModuleTypesRegistered`). 기동 스레드가 쓰고 씬을 읽는 쪽이 읽습니다. */
        atomic<bool> _bAllModuleTypesRegistered;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 7) TypeRegistrar: 정적 초기화로 Type 등록 함수를 체인에 연결한다.
    //    Core TU 는 getHead(), 핫 리로드 모듈은 모듈 로컬 헤드를 쓴다.
    // ------------------------------------------------------------------------------
    struct SW_API TypeRegistrar
    {
        void ( *_registerFunc )( TypeRegistry& ); ///< TypeRegistry에 TypeInfo를 넣는 함수
        TypeRegistrar* _pNext;                    ///< 같은 헤드의 다음 registrar

        /** @brief Engine.dll(Core OBJECT) 전용 registrar 리스트 헤드입니다. */
        static TypeRegistrar*& getHead();

        /** @brief Engine.dll(Core OBJECT) 정적 등록에 사용합니다. */
        TypeRegistrar( void ( *registerFunc )( TypeRegistry& ) );
        /** @brief 핫 리로드 모듈 등 외부 registrar 등록에 사용합니다. */
        TypeRegistrar( void ( *registerFunc )( TypeRegistry& ), TypeRegistrar*& pModuleHead );
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 8) EnumRegistrar: 정적 초기화로 Enum 등록 함수를 체인에 연결한다.
    //    Core TU 는 getHead(), 핫 리로드 모듈은 모듈 로컬 헤드를 쓴다.
    // ------------------------------------------------------------------------------
    struct SW_API EnumRegistrar
    {
        void ( *_registerFunc )( TypeRegistry& ); ///< TypeRegistry에 EnumInfo를 넣는 함수
        EnumRegistrar* _pNext;                    ///< 같은 헤드의 다음 registrar

        /** @brief Engine.dll(Core OBJECT) 전용 enum registrar 리스트 헤드입니다. */
        static EnumRegistrar*& getHead();

        /** @brief Engine.dll(Core OBJECT) 정적 등록에 사용합니다. */
        EnumRegistrar( void ( *registerFunc )( TypeRegistry& ) );
        /** @brief 핫 리로드 모듈 등 외부 registrar 등록에 사용합니다. */
        EnumRegistrar( void ( *registerFunc )( TypeRegistry& ), EnumRegistrar*& pModuleHead );
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
