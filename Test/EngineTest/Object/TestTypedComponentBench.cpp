/**
 * @file TestTypedComponentBench.cpp
 * @brief 타입별 컴포넌트 버킷 순회(`GameObjectManager::forEachComponentOfType`)의 합성 장면 벤치 — 계획 문서 TypedComponents 8.4절(④).
 * @details 숫자를 **찍기만** 하고 판정하지 않는다(기계마다 다르다). 판정은 방문 수가 맞는지만 본다. Release 로 읽는다.
 *          GPU 를 쓰지 않으므로 셰이더를 다시 굽지 않아도 된다(측정 경로에 셰이더가 없다).
 *
 *          측정하는 것:
 *          - 오브젝트 1만 · 10만 × 정확한 타입 수 K = 50 · 200 · 500 장면에서 말단 타입과 기반 타입의 질의 한 번 비용(ns).
 *            같은 장면에서 기존 등록형 목록(`ComponentRegistry::getAll`)과 옛 방식(오브젝트 훑기 + `castTo`)도 함께 측정한다.
 *            등록형 목록에 삭제 대기 검사를 더한 두 가지(컴포넌트 표시 하나 · 컴포넌트와 소유 오브젝트 둘)로 검사의 몫을 가른다.
 *          - 오브젝트 하나를 만들고 지우는 비용(버킷 넣기 · 빼기의 배타 잠금 두 번이 든다)과, 경합 없는 `std::shared_mutex` 배타 잠금 한 쌍의 비용.
 *          - `sizeof( Component )`.
 *          - `resolveComponent<T>` 의 타입 확인 비용(`ResolveTypedCheckCost`): 같은 핸들을 확인하는 찾기와 타입 없는 찾기 + `static_cast` 로 푼 값을 견준다.
 *            확인은 Shipping 이 아닌 구성에만 있으므로 Release 로 돌린 이 케이스가 "개발 빌드의 확인 비용" 이다(계획 문서 TypedComponents "남은 확인").
 *          - 비동기 씬 로드 중의 조회(`QueryWhileAsyncSceneLoad`): 다른 매니저가 비동기로 큰 씬을 지을 때 조회 한 번의 지연을 쉴 때와 견준다.
 *
 *          타입 500 개를 만드는 법: 말단 타입은 `BenchBucketComponent<N>` 템플릿 인스턴스다. 코드젠이 아니라 손으로 `TypeInfo` 를 만들어
 *          `TypeRegistry::registerClass` 로 올리고, 표가 가진 사본의 주소를 `StaticType()` 의 함수 안 정적 변수에 담는다(FQN `sw::BenchBucketComponent<N>`,
 *          부모 `sw::BenchBucketBaseComponent`). `REFLECT_BODY()` 는 템플릿 안에서도 그대로 쓰인다(파서가 아닌 빌드에서는 `StaticType` 선언과 `swReflectSelf` 뿐이다).
 *          코드젠 파서가 템플릿 인스턴스마다 `TypeInfo` 를 만들지는 않으므로, 실제 게임 코드에서 템플릿 컴포넌트를 쓰려면 인스턴스마다 이름 있는 타입이 필요하다.
 *
 *          교훈:
 *          - 타입 표의 값 주소를 오래 쥐려면 그 주소가 고정이어야 한다. 처음에는 `makeMockComponentTypeInfo` 의 캐시가 밀집 배열 해시맵(`sw::unordered_map`)의
 *            값을 그대로 담아, 타입이 늘면 값이 옮겨지고 앞서 준 포인터가 매달렸다(타입 500 개에서 버킷 하나가 사라지고 `isDerivedFrom` 이 죽었다).
 *            그 캐시는 이제 `unique_ptr` 값이다. 여기서는 리플렉션 표(`TypeRegistry::registerClass`)의 고정 주소를 쓴다.
 *          - 접기 식(fold)은 인자 500 개에서 clang 의 식 중첩 한도(256)에 걸린다. 중괄호 목록의 팩 전개로 등록한다.
 *          - `REFLECT_BODY` 타입을 익명 네임스페이스에 두면 `swReflectSelf` 가 쓰이지 않는다는 경고가 난다. 모의 컴포넌트처럼 `sw` 에 둔다.
 *          - `ComponentRegistry` 를 기반 타입으로 등록하면 `add` 의 중복 검사와 순서를 지키는 `remove` 가 목록 길이에 비례해, 오브젝트 10만에서
 *            장면 만들기가 O(n²) 이 된다. 그래서 등록형 기반 목록은 오브젝트 1만에서만 측정한다.
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( TypedComponentBenchTest, "typed component bucket query cost against the component registry and the object walk" );
SW_TEST_TIME_BUDGET( TypedComponentBenchTest, 30000, "Release run of the first two cases took about 9 s (2026-10-19, Ryzen 7 6800H); scenes of 100000 objects; the async load case is not timed yet" );

SW_LOG_CALLER( "TypedComponentBench" );

// 모의 컴포넌트(`TestGameObjectMocks.h`)처럼 `sw` 에 둔다. 익명 네임스페이스에 두면 내부 연결이라 `REFLECT_BODY` 의 `swReflectSelf` 가 쓰이지 않는다는 경고가 난다.
namespace sw
{
    /** @brief 말단 타입 500 가지의 공통 기반입니다. 질의의 "기반 타입" 입니다. */
    class BenchBucketBaseComponent : public Component
    {
    public:
        REFLECT_BODY();

        const TypeInfo* getTypeInfo() const override { return StaticType(); }

        using AddComponentFunc = Component* (*)( GameObject* ); ///< `TypeInfo::_addComponent` 에 실리는 생성 함수

        /** @brief 말단 타입 @p typeIndex 의 `TypeInfo`(FQN `sw::BenchBucketComponent<N>`, 부모는 이 타입)를 만들어 리플렉션 표에 올립니다. */
        static const TypeInfo* makeLeafTypeInfo( uint32 typeIndex, AddComponentFunc addComponent, size_t size );
        /** @brief `TypeInfo` 하나를 리플렉션 표에 올리고 표가 가진 사본(주소 고정)을 돌려줍니다. 엔진 서비스가 없으면 `getSubsystem` 이 멈춘다. */
        static const TypeInfo* registerBenchType( AddComponentFunc addComponent, hashed_string shortName, hashed_string fqn, size_t size, hashed_string parentFqn );

        static inline bool s_bRegisterLeafInRegistry = false; ///< 말단 타입 목록(`ComponentRegistry`)에 등록하는가. 변동 측정은 끄고 버킷만 남긴다
        static inline bool s_bRegisterBaseInRegistry = false; ///< 기반 타입 목록에도 등록하는가. 목록 길이에 비례하는 중복 검사 때문에 오브젝트 1만에서만 켠다(파일 머리 주석)

        uint32 _payload{ 0 }; ///< 방문이 컴포넌트 메모리를 실제로 읽게 하는 값

    protected:
        void onRegister( sw::GameObjectManager& manager ) override
        {
            if ( s_bRegisterBaseInRegistry )
                manager.getComponentRegistry().add<BenchBucketBaseComponent>( this );
        }
        void onUnregister( sw::GameObjectManager& manager ) override
        {
            manager.getComponentRegistry().remove<BenchBucketBaseComponent>( this );
        }
    };
} // namespace sw

namespace sw
{
    const TypeInfo* BenchBucketBaseComponent::StaticType()
    {
        static const TypeInfo* s_pType = registerBenchType( &GameObject::addComponentTo<BenchBucketBaseComponent>, hashed_string( "BenchBucketBaseComponent" ),
                                                            hashed_string( "sw::BenchBucketBaseComponent" ), sizeof( BenchBucketBaseComponent ), hashed_string( "sw::Component" ) );
        return s_pType;
    }

    const TypeInfo* BenchBucketBaseComponent::makeLeafTypeInfo( uint32 typeIndex, AddComponentFunc addComponent, size_t size )
    {
        fixed_string<constant::kMaxBuffer256> shortName{};
        formatstring( shortName.data(), shortName.capacity(), "BenchBucketComponent%#", typeIndex );
        fixed_string<constant::kMaxBuffer256> fqn{};
        formatstring( fqn.data(), fqn.capacity(), "sw::BenchBucketComponent<%#>", typeIndex );
        return registerBenchType( addComponent, hashed_string( shortName.c_str() ), hashed_string( fqn.c_str() ), size, hashed_string( "sw::BenchBucketBaseComponent" ) );
    }

    const TypeInfo* BenchBucketBaseComponent::registerBenchType( AddComponentFunc addComponent, hashed_string shortName, hashed_string fqn, size_t size,
                                                                 hashed_string parentFqn )
    {
        TypeInfo info{};
        info._name               = shortName;
        info._fullyQualifiedName = fqn;
        info._parentFQN          = parentFqn;
        info._size               = size;
        info._addComponent       = addComponent;
        TypeRegistry& registry   = engine::getSubsystem<TypeRegistry>();
        registry.registerClass( info );
        // 레지스트리의 사본을 쓴다. 그 주소는 고정이다(`TypeRegistry::registerClass`).
        return registry.findType( fqn );
    }

    /** @brief 정확한 타입 하나입니다. 인스턴스 번호마다 다른 `TypeInfo`(다른 버킷)를 가집니다. */
    template <uint32 TIndex>
    class BenchBucketComponent final : public BenchBucketBaseComponent
    {
    public:
        REFLECT_BODY();

        const TypeInfo* getTypeInfo() const override { return StaticType(); }

    protected:
        void onRegister( GameObjectManager& manager ) override
        {
            BenchBucketBaseComponent::onRegister( manager );
            if ( s_bRegisterLeafInRegistry )
                manager.getComponentRegistry().add<BenchBucketComponent>( this );
        }
        void onUnregister( GameObjectManager& manager ) override
        {
            manager.getComponentRegistry().remove<BenchBucketComponent>( this );
            BenchBucketBaseComponent::onUnregister( manager );
        }
    };

    template <uint32 TIndex>
    const TypeInfo* BenchBucketComponent<TIndex>::StaticType()
    {
        static const TypeInfo* s_pType = makeLeafTypeInfo( TIndex, &GameObject::addComponentTo<BenchBucketComponent<TIndex>>, sizeof( BenchBucketComponent<TIndex> ) );
        return s_pType;
    }
} // namespace sw

namespace
{
    constexpr uint32 kMaxBucketTypeCount   = 500;
    constexpr uint32 kSampleCount          = 30;
    constexpr uint64 kVisitTargetPerSample = 1000000; ///< 표본 하나가 지나는 컴포넌트 · 버킷 수의 목표. 작은 질의는 여러 번 돌려 시계 해상도를 넘긴다
    constexpr uint32 kChurnBatchCount      = 200;     ///< 변동 판 하나에서 만들고 지우는 오브젝트 수

    using BenchBucketBaseComponent = sw::BenchBucketBaseComponent;
    /** @brief 질의의 "말단 타입" 입니다. 장면의 K 가지 중 첫째라 오브젝트 K 개마다 하나가 맞습니다. */
    using BenchLeafComponent = sw::BenchBucketComponent<0>;
    template <uint32 TIndex>
    using BenchBucketComponent = sw::BenchBucketComponent<TIndex>;

    using AddBucketComponentFunc = sw::Component* (*)( sw::GameObject* );

    template <uint32... TIndex>
    std::array<AddBucketComponentFunc, sizeof...( TIndex )> makeAddBucketComponentTable( std::integer_sequence<uint32, TIndex...> )
    {
        // 기반을 먼저 올린다. 말단의 부모 이름이 표에 있어야 조상 표가 첫 질의 전에 선다.
        // 접기 식(fold)은 인자 500 개에서 clang 의 식 중첩 한도(256)에 걸린다. 중괄호 목록의 팩 전개는 중첩되지 않는다.
        BenchBucketBaseComponent::StaticType();
        [[maybe_unused]] const std::array<const sw::TypeInfo*, sizeof...( TIndex )> arrType{ BenchBucketComponent<TIndex>::StaticType()... };
        return { &sw::GameObject::addComponentTo<BenchBucketComponent<TIndex>>... };
    }

    /** @brief 말단 타입 번호에서 "그 타입을 붙이는 함수" 로 가는 표입니다. 처음 부를 때 500 가지 `TypeInfo` 를 리플렉션 표에 올립니다. */
    const std::array<AddBucketComponentFunc, kMaxBucketTypeCount>& getAddBucketComponentTable()
    {
        static const std::array<AddBucketComponentFunc, kMaxBucketTypeCount> s_arrAddComponent =
            makeAddBucketComponentTable( std::make_integer_sequence<uint32, kMaxBucketTypeCount>{} );
        return s_arrAddComponent;
    }

    /** @brief 오브젝트 @p objectCount 개에 말단 타입 @p typeCount 가지를 번갈아 하나씩 붙이고 한 번 갱신합니다. 붙이지 못한 수를 돌려줍니다. */
    uint32 buildBucketScene( sw::GameObjectManager& manager, uint32 objectCount, uint32 typeCount )
    {
        const std::array<AddBucketComponentFunc, kMaxBucketTypeCount>& arrAddComponent = getAddBucketComponentTable();
        uint32                                                         failedCount     = 0;
        for ( uint32 index = 0; index < objectCount; ++index )
        {
            sw::GameObject* pObj  = manager.createGameObject( sw::hashed_string( "BenchObject" ) );
            sw::Component*  pComp = pObj != nullptr ? arrAddComponent[index % typeCount]( pObj ) : nullptr;
            if ( pComp == nullptr )
            {
                ++failedCount;
                continue;
            }
            static_cast<BenchBucketBaseComponent*>( pComp )->_payload = index;
        }
        manager.update( 0.016f );
        return failedCount;
    }

    /** @brief 질의 한 번이 지난 컴포넌트 수와 값의 합입니다. 합은 최적화가 방문을 지우지 못하게 합니다. */
    struct QueryResult
    {
        uint64 _visitCount{ 0 };
        uint64 _payloadSum{ 0 };
    };

    /** @brief `[Bench] 이름  min · p50 · p99 · max` 를 ns 로 찍습니다. `test::logBenchSamples` 는 us 라 짧은 질의가 0 으로 뭉개진다. */
    void logNanoSamples( [[maybe_unused]] const utf8* pLabel, sw::vector<int64>& listSample )
    {
        [[maybe_unused]] const int64 minValue = test::getPercentile( listSample, 0 );
        [[maybe_unused]] const int64 p50      = test::getPercentile( listSample, 50 );
        [[maybe_unused]] const int64 p99      = test::getPercentile( listSample, 99 );
        [[maybe_unused]] const int64 maxValue = test::getPercentile( listSample, 100 );
        SW_LOG_INFO( "[Bench] %#  min %# ns  p50 %# ns  p99 %# ns  max %# ns  (%# samples)", pLabel, minValue, p50, p99, maxValue, listSample.size() );
    }

    /**
     * @brief @p query 한 번의 비용을 ns 표본 `kSampleCount` 개로 찍고, 질의 한 번의 결과를 돌려줍니다.
     * @details 표본 하나는 질의를 여러 번(`kVisitTargetPerSample` / @p workPerQuery) 돌린 평균이다. 첫 판은 데우기로 버린다.
     */
    template <typename TQuery>
    QueryResult measureQuery( const utf8* pLabel, uint64 workPerQuery, TQuery&& query )
    {
        const uint64      repeatCount = std::max<uint64>( 1, kVisitTargetPerSample / std::max<uint64>( 1, workPerQuery ) );
        const QueryResult result      = query();
        uint64            sink        = 0;
        sw::vector<int64> listNano;
        listNano.reserve( kSampleCount );
        for ( uint32 sample = 0; sample < kSampleCount; ++sample )
        {
            const sw::Stopwatch stopwatch;
            for ( uint64 repeat = 0; repeat < repeatCount; ++repeat )
            {
                sink += query()._payloadSum;
            }
            listNano.push_back( stopwatch.getElapsedNanoseconds() / static_cast<int64>( repeatCount ) );
        }
        SW_EXPECT_EQUAL( result._payloadSum * repeatCount * kSampleCount, sink );
        logNanoSamples( pLabel, listNano );
        return result;
    }

    /** @brief "오브젝트 N 개, K 가지, 질의 이름" 꼴의 표본 이름을 만듭니다. */
    sw::fixed_string<sw::constant::kMaxBuffer256> makeQueryLabel( uint32 objectCount, uint32 typeCount, const utf8* pQueryName )
    {
        sw::fixed_string<sw::constant::kMaxBuffer256> label{};
        sw::formatstring( label.data(), label.capacity(), "N=%# K=%# %#", objectCount, typeCount, pQueryName );
        return label;
    }

    /** @brief 타입 핸들 @p listHandle 을 **확인하는** 찾기(`resolveComponent<T>`)로 모두 풀고 `_payload` 를 합합니다. */
    template <typename T>
    QueryResult resolveAllTyped( const sw::GameObjectManager& manager, const sw::vector<sw::TComponentHandle<T>>& listHandle )
    {
        QueryResult result{};
        for ( const sw::TComponentHandle<T>& handle : listHandle )
        {
            const T* pComp = manager.resolveComponent( handle );
            if ( pComp == nullptr )
                continue;
            ++result._visitCount;
            result._payloadSum += pComp->_payload;
        }
        return result;
    }

    /** @brief 같은 핸들을 타입 없는 찾기 + `static_cast`(확인 없음)로 모두 풀고 `_payload` 를 합합니다. */
    template <typename T>
    QueryResult resolveAllUntyped( const sw::GameObjectManager& manager, const sw::vector<sw::ComponentHandle>& listHandle )
    {
        QueryResult result{};
        for ( const sw::ComponentHandle& handle : listHandle )
        {
            const sw::Component* pRaw = manager.resolveComponent( handle );
            if ( pRaw == nullptr )
                continue;
            ++result._visitCount;
            result._payloadSum += static_cast<const T*>( pRaw )->_payload;
        }
        return result;
    }
} // namespace

/**
 * @brief [TypedComponentBenchTest] 오브젝트 1만 · 10만 × 정확한 타입 수 50 · 200 · 500 — 말단 · 기반 타입 질의를 버킷 · 등록형 목록 · 오브젝트 훑기로 견준다
 * @details 판마다 새 매니저다(앞 판의 풀 · 버킷이 뒤 판을 측정하지 않게). 등록형 기반 목록은 오브젝트 1만에서만 측정한다(파일 머리 주석).
 */
SW_TEST_CASE( TypedComponentBenchTest, BucketQueryByObjectAndTypeCount )
{
    for ( const uint32 objectCount : { 10000u, 100000u } )
    {
        for ( const uint32 typeCount : { 50u, 200u, 500u } )
        {
            BenchBucketBaseComponent::s_bRegisterLeafInRegistry = true;
            BenchBucketBaseComponent::s_bRegisterBaseInRegistry = objectCount <= 10000u;
            sw::GameObjectManager manager;
            SW_EXPECT_EQUAL( 0u, buildBucketScene( manager, objectCount, typeCount ) );
            SW_EXPECT_EQUAL( typeCount, manager.getTypedComponentBucketCount() );

            const uint64                 leafCount    = ( objectCount + typeCount - 1 ) / typeCount;
            const sw::TypeInfo*          pLeafType    = BenchLeafComponent::StaticType();
            const sw::TypeInfo*          pBaseType    = BenchBucketBaseComponent::StaticType();
            const sw::GameObjectManager& constManager = manager;

            const QueryResult bucketLeaf = measureQuery( makeQueryLabel( objectCount, typeCount, "bucket leaf" ).c_str(), leafCount + typeCount, [&constManager]()
            {
                QueryResult result{};
                constManager.forEachComponentOfType<BenchLeafComponent>( [&result]( const BenchLeafComponent* pComp )
                {
                    ++result._visitCount;
                    result._payloadSum += pComp->_payload;
                } );
                return result;
            } );
            SW_EXPECT_EQUAL( leafCount, bucketLeaf._visitCount );

            const QueryResult bucketBase = measureQuery( makeQueryLabel( objectCount, typeCount, "bucket base" ).c_str(), objectCount + typeCount, [&constManager]()
            {
                QueryResult result{};
                constManager.forEachComponentOfType<BenchBucketBaseComponent>( [&result]( const BenchBucketBaseComponent* pComp )
                {
                    ++result._visitCount;
                    result._payloadSum += pComp->_payload;
                } );
                return result;
            } );
            SW_EXPECT_EQUAL( static_cast<uint64>( objectCount ), bucketBase._visitCount );

            const QueryResult registryLeaf = measureQuery( makeQueryLabel( objectCount, typeCount, "registry leaf" ).c_str(), leafCount, [&constManager]()
            {
                QueryResult result{};
                for ( const BenchLeafComponent* pComp : constManager.getComponentRegistry().getAll<BenchLeafComponent>() )
                {
                    ++result._visitCount;
                    result._payloadSum += pComp->_payload;
                }
                return result;
            } );
            SW_EXPECT_EQUAL( leafCount, registryLeaf._visitCount );

            // 등록형 목록에 지금 버킷 순회가 하는 삭제 대기 검사(컴포넌트 표시 하나)를 더한다.
            const QueryResult registryLeafComponentChecked = measureQuery( makeQueryLabel( objectCount, typeCount, "registry leaf + component check" ).c_str(), leafCount,
                                                                           [&constManager]()
            {
                QueryResult result{};
                for ( const BenchLeafComponent* pComp : constManager.getComponentRegistry().getAll<BenchLeafComponent>() )
                {
                    if ( pComp->isPendingDestroy() )
                        continue;
                    ++result._visitCount;
                    result._payloadSum += pComp->_payload;
                }
                return result;
            } );
            SW_EXPECT_EQUAL( leafCount, registryLeafComponentChecked._visitCount );

            // 7-c 의 첫 버킷 순회가 하던 검사(컴포넌트 · 소유 오브젝트)다. 위와 견주어 소유 오브젝트 읽기의 몫을 같은 바이너리에서 가른다(TypedComponents 8.4절).
            const QueryResult registryLeafChecked = measureQuery( makeQueryLabel( objectCount, typeCount, "registry leaf + pending checks" ).c_str(), leafCount,
                                                                  [&constManager]()
            {
                QueryResult result{};
                for ( const BenchLeafComponent* pComp : constManager.getComponentRegistry().getAll<BenchLeafComponent>() )
                {
                    if ( pComp->isPendingDestroy() || pComp->getOwner()->isPendingDestroy() )
                        continue;
                    ++result._visitCount;
                    result._payloadSum += pComp->_payload;
                }
                return result;
            } );
            SW_EXPECT_EQUAL( leafCount, registryLeafChecked._visitCount );

            if ( BenchBucketBaseComponent::s_bRegisterBaseInRegistry )
            {
                const QueryResult registryBase = measureQuery( makeQueryLabel( objectCount, typeCount, "registry base" ).c_str(), objectCount, [&constManager]()
                {
                    QueryResult result{};
                    for ( const BenchBucketBaseComponent* pComp : constManager.getComponentRegistry().getAll<BenchBucketBaseComponent>() )
                    {
                        ++result._visitCount;
                        result._payloadSum += pComp->_payload;
                    }
                    return result;
                } );
                SW_EXPECT_EQUAL( static_cast<uint64>( objectCount ), registryBase._visitCount );
            }

            // 옛 방식(7-c 전): 오브젝트를 모두 훑고 컴포넌트마다 `castTo` 로 거른다.
            const QueryResult walkLeaf = measureQuery( makeQueryLabel( objectCount, typeCount, "object walk leaf" ).c_str(), objectCount, [&constManager, pLeafType]()
            {
                QueryResult result{};
                constManager.forEachGameObject( [&result, pLeafType]( const sw::GameObject* pObj )
                {
                    pObj->forEachComponentOfType<BenchLeafComponent>( pLeafType, [&result]( const BenchLeafComponent* pComp )
                    {
                        ++result._visitCount;
                        result._payloadSum += pComp->_payload;
                    } );
                } );
                return result;
            } );
            SW_EXPECT_EQUAL( leafCount, walkLeaf._visitCount );

            const QueryResult walkBase = measureQuery( makeQueryLabel( objectCount, typeCount, "object walk base" ).c_str(), objectCount, [&constManager, pBaseType]()
            {
                QueryResult result{};
                constManager.forEachGameObject( [&result, pBaseType]( const sw::GameObject* pObj )
                {
                    pObj->forEachComponentOfType<BenchBucketBaseComponent>( pBaseType, [&result]( const BenchBucketBaseComponent* pComp )
                    {
                        ++result._visitCount;
                        result._payloadSum += pComp->_payload;
                    } );
                } );
                return result;
            } );
            SW_EXPECT_EQUAL( static_cast<uint64>( objectCount ), walkBase._visitCount );
        }
    }
    BenchBucketBaseComponent::s_bRegisterLeafInRegistry = false;
    BenchBucketBaseComponent::s_bRegisterBaseInRegistry = false;
}

/**
 * @brief [TypedComponentBenchTest] 오브젝트 1만 · K = 200 장면에서 오브젝트 하나를 만들고 지우는 비용 — 버킷 넣기 · 빼기가 잡는 배타 잠금의 몫을 가늠한다
 * @details 등록형 목록은 끈다(버킷만 남긴다). 같은 실행에서 경합 없는 `std::shared_mutex` 배타 잠금 한 쌍을 따로 측정해, 오브젝트 하나가 더 내는 잠금 두 쌍의 비중을 본다.
 */
SW_TEST_CASE( TypedComponentBenchTest, SpawnDestroyChurnAndLockCost )
{
    constexpr uint32 kObjectCount                       = 10000;
    constexpr uint32 kTypeCount                         = 200;
    BenchBucketBaseComponent::s_bRegisterLeafInRegistry = false;
    BenchBucketBaseComponent::s_bRegisterBaseInRegistry = false;

    sw::GameObjectManager manager;
    SW_EXPECT_EQUAL( 0u, buildBucketScene( manager, kObjectCount, kTypeCount ) );

    const std::array<AddBucketComponentFunc, kMaxBucketTypeCount>& arrAddComponent = getAddBucketComponentTable();
    sw::vector<sw::GameObject*>                                    listChurn;
    listChurn.reserve( kChurnBatchCount );
    sw::vector<int64> listNano;
    for ( uint32 round = 0; round < kSampleCount + 1; ++round )
    {
        listChurn.clear();
        const sw::Stopwatch stopwatch;
        for ( uint32 index = 0; index < kChurnBatchCount; ++index )
        {
            sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "ChurnObject" ) );
            arrAddComponent[( round + index ) % kTypeCount]( pObj );
            listChurn.push_back( pObj );
        }
        for ( sw::GameObject* pObj : listChurn )
        {
            manager.destroyObject( pObj );
        }
        manager.processDeferredDestruction();
        const int64 nanos = stopwatch.getElapsedNanoseconds();
        if ( round > 0 ) // 첫 판은 데우기(풀 블록 · 이름 표)
            listNano.push_back( nanos / kChurnBatchCount );
    }
    logNanoSamples( "spawn + destroy one object with one bucketed component (N=10000 K=200)", listNano );
    SW_EXPECT_EQUAL( static_cast<size_t>( kObjectCount ), manager.getAllGameObjects().size() );
    SW_EXPECT_EQUAL( kTypeCount, manager.getTypedComponentBucketCount() );

    std::shared_mutex            lockProbe;
    [[maybe_unused]] const int64 lockDeciNanos = test::measureBestDeciNanosPerOp( 1000000, 5, [&lockProbe]()
    {
        for ( uint32 index = 0; index < 1000000; ++index )
        {
            lockProbe.lock();
            lockProbe.unlock();
        }
    } );
    test::logBenchDeciNanos( "uncontended std::shared_mutex lock + unlock", lockDeciNanos );
    SW_LOG_INFO( "[Bench] sizeof Component %#  BenchBucketBaseComponent %#", sizeof( sw::Component ), sizeof( BenchBucketBaseComponent ) );
}

/**
 * @brief [TypedComponentBenchTest] 개발 빌드(Shipping 이 아닌 구성)의 `resolveComponent<T>` 타입 확인 비용 — 같은 핸들을 타입 없는 찾기 + `static_cast` 와 견준다
 * @details 오브젝트 1만 · K = 200. 말단 타입 핸들(정확히 같은 타입이라 `isDerivedFrom` 이 바로 끝난다)과 기반 타입 핸들(말단 200 가지를 한 핸들 형식으로 푼다)을 따로 잰다.
 *          확인은 `SW_SHIPPING` 이 아니면 켜지므로 Release 로 돌린 이 케이스의 두 줄 차(`typed` 대 `untyped`)가 개발 빌드의 확인 비용이다. 값은 핸들 하나를 푸는 ns 다.
 *          앱에서 프레임당 비용은 (프레임당 `resolveComponent<T>` 횟수) × (그 차)로 어림한다.
 */
SW_TEST_CASE( TypedComponentBenchTest, ResolveTypedCheckCost )
{
    constexpr uint32 kObjectCount                       = 10000;
    constexpr uint32 kTypeCount                         = 200;
    BenchBucketBaseComponent::s_bRegisterLeafInRegistry = false;
    BenchBucketBaseComponent::s_bRegisterBaseInRegistry = false;

    sw::GameObjectManager manager;
    SW_EXPECT_EQUAL( 0u, buildBucketScene( manager, kObjectCount, kTypeCount ) );
    const sw::GameObjectManager& constManager = manager;

    sw::vector<sw::TComponentHandle<BenchBucketBaseComponent>> listBaseTyped;
    sw::vector<sw::ComponentHandle>                            listBaseUntyped;
    constManager.forEachComponentOfType<BenchBucketBaseComponent>( [&listBaseTyped, &listBaseUntyped]( const BenchBucketBaseComponent* pComp )
    {
        listBaseTyped.push_back( sw::TComponentHandle<BenchBucketBaseComponent>{ pComp } );
        listBaseUntyped.push_back( pComp->getHandle() );
    } );
    sw::vector<sw::TComponentHandle<BenchLeafComponent>> listLeafTyped;
    sw::vector<sw::ComponentHandle>                      listLeafUntyped;
    constManager.forEachComponentOfType<BenchLeafComponent>( [&listLeafTyped, &listLeafUntyped]( const BenchLeafComponent* pComp )
    {
        listLeafTyped.push_back( sw::TComponentHandle<BenchLeafComponent>{ pComp } );
        listLeafUntyped.push_back( pComp->getHandle() );
    } );
    const uint64 leafCount = ( kObjectCount + kTypeCount - 1 ) / kTypeCount;
    SW_ASSERT_EQUAL( static_cast<size_t>( kObjectCount ), listBaseTyped.size() );
    SW_ASSERT_EQUAL( static_cast<size_t>( leafCount ), listLeafTyped.size() );

    const QueryResult baseTyped   = measureQuery( "N=10000 K=200 resolve base handles, typed (checked)", kObjectCount, [&constManager, &listBaseTyped]()
      {
        return resolveAllTyped<BenchBucketBaseComponent>( constManager, listBaseTyped );
    } );
    const QueryResult baseUntyped = measureQuery( "N=10000 K=200 resolve base handles, untyped + static_cast", kObjectCount, [&constManager, &listBaseUntyped]()
    {
        return resolveAllUntyped<BenchBucketBaseComponent>( constManager, listBaseUntyped );
    } );
    SW_EXPECT_EQUAL( static_cast<uint64>( kObjectCount ), baseTyped._visitCount );
    SW_EXPECT_EQUAL( baseTyped._payloadSum, baseUntyped._payloadSum );

    const QueryResult leafTyped   = measureQuery( "N=10000 K=200 resolve leaf handles, typed (checked)", leafCount, [&constManager, &listLeafTyped]()
      {
        return resolveAllTyped<BenchLeafComponent>( constManager, listLeafTyped );
    } );
    const QueryResult leafUntyped = measureQuery( "N=10000 K=200 resolve leaf handles, untyped + static_cast", leafCount, [&constManager, &listLeafUntyped]()
    {
        return resolveAllUntyped<BenchLeafComponent>( constManager, listLeafUntyped );
    } );
    SW_EXPECT_EQUAL( leafCount, leafTyped._visitCount );
    SW_EXPECT_EQUAL( leafTyped._payloadSum, leafUntyped._payloadSum );
    SW_LOG_INFO( "[Bench] each line above is the cost of the whole handle list (base %# handles, leaf %# handles) - divide by the count for ns per resolve", kObjectCount, leafCount );
}

/**
 * @brief [TypedComponentBenchTest] 다른 매니저가 비동기로 큰 씬을 짓는 동안 버킷 조회 한 번의 지연 — 쉴 때와 견준다
 * @details 조회하는 매니저(1만 · K = 200)는 `SceneManager` 밖의 독립 매니저다. 로드 워커는 자기 전용 새 씬의 저장소에만 넣으므로(`SceneManager.cpp` 의 로드 작업)
 *          조회와 겹치지 않아야 하고, 이 케이스는 그 말을 지연으로 확인한다. 조회 한 번(말단 타입, 50 개 방문)을 시계 사이에 직접 끼워 재므로 값에 시계 읽기 비용
 *          (수십 ns)이 들어 있다 — 쉴 때와 로드 중을 같은 방식으로 재서 견주는 값이다. 게임 프레임 하나는 조회 64 번마다 전환을 한 번 갱신하는 것으로 흉내 낸다.
 *          로드 중 표본이 너무 적으면(`samples` 가 수십 개 아래) `kLoadedObjectCount` 를 키운다.
 */
SW_TEST_CASE( TypedComponentBenchTest, QueryWhileAsyncSceneLoad )
{
    constexpr uint32 kQueryObjectCount                  = 10000;
    constexpr uint32 kQueryTypeCount                    = 200;
    constexpr uint32 kLoadedObjectCount                 = 50000;
    constexpr uint32 kIdleSampleCount                   = 4000;
    constexpr uint32 kQueriesPerTransitionUpdate        = 64;
    constexpr int64  kLoadTimeoutMilliseconds           = 20000;
    BenchBucketBaseComponent::s_bRegisterLeafInRegistry = false;
    BenchBucketBaseComponent::s_bRegisterBaseInRegistry = false;

    // 로드할 씬: 이름만 있는 오브젝트 kLoadedObjectCount 개. XML 과 바이너리를 같은 내용으로 둔다(어느 쪽을 읽는 구성이든 같은 씬이다).
    const sw::string  xmlPath = test::makeTempPath( "sw_test_typed_bench_async.scene.xml" );
    const sw::string  binPath = test::makeTempPath( "sw_test_typed_bench_async.scene.bin" );
    sw::string        xmlText = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Scene formatVersion=\"2\" name=\"BenchStream\">\n  <WorldSettings _bStreaming=\"false\" _unloadMargin=\"0\" _maxCellLoadsPerFrame=\"0\"><_cellGrid _cellSize=\"0\" _loadingRadius=\"0\"/></WorldSettings><entities>\n";
    sw::SceneDocument doc{};
    doc._name = "BenchStream";
    for ( uint32 index = 0; index < kLoadedObjectCount; ++index )
    {
        sw::fixed_string<sw::constant::kMaxBuffer256> entityName{};
        sw::formatstring( entityName.data(), entityName.capacity(), "Filler%#", index );
        sw::fixed_string<sw::constant::kMaxBuffer256> line{};
        sw::formatstring( line.data(), line.capacity(), "    <entity id=\"%#\" name=\"%#\"/>\n", index + 1, entityName.c_str() );
        xmlText += line.c_str();
        sw::SceneDocument::SceneObjectNode node{};
        node._name   = entityName.c_str();
        node._fileID = index + 1;
        doc._listSceneObjectNode.push_back( std::move( node ) );
    }
    xmlText += "  </entities>\n</Scene>\n";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath, reinterpret_cast<const uint8*>( xmlText.data() ), static_cast<uint64>( xmlText.size() ) ) );
    SW_ASSERT_TRUE( doc.saveBinary( binPath ) );

    sw::GameObjectManager queried;
    SW_EXPECT_EQUAL( 0u, buildBucketScene( queried, kQueryObjectCount, kQueryTypeCount ) );

    uint64     sink        = 0;
    const auto sampleQuery = [&queried, &sink]( sw::vector<int64>& listNano )
    {
        const sw::Stopwatch stopwatch;
        queried.forEachComponentOfType<BenchLeafComponent>( [&sink]( const BenchLeafComponent* pComp )
        {
            sink += pComp->_payload;
        } );
        listNano.push_back( stopwatch.getElapsedNanoseconds() );
    };

    sw::vector<int64> listIdle;
    listIdle.reserve( kIdleSampleCount );
    for ( uint32 index = 0; index < kIdleSampleCount; ++index )
    {
        sampleQuery( listIdle );
    }
    logNanoSamples( "single bucket query, idle (N=10000 K=200 leaf)", listIdle );

    sw::SceneManager sceneManager;
    SW_ASSERT_TRUE( sceneManager.initialize() );
    sw::vector<int64>   listDuring;
    const sw::Stopwatch loadStopwatch;
    SW_ASSERT_TRUE( sceneManager.requestLoadAsync( ::test::makeTempResourceID( xmlPath ) ) );
    const sw::Deadline deadline   = sw::Deadline::afterMilliseconds( kLoadTimeoutMilliseconds );
    uint32             queryCount = 0;
    while ( sceneManager.isTransitioning() && deadline.isExpired() == false )
    {
        sampleQuery( listDuring );
        if ( ++queryCount % kQueriesPerTransitionUpdate == 0 )
            sceneManager.updateTransitions();
    }
    sceneManager.waitForPendingLoad();
    sceneManager.updateTransitions();
    [[maybe_unused]] const int64 loadMilliseconds = loadStopwatch.getElapsedMilliseconds();
    SW_ASSERT_FALSE( listDuring.empty() );
    logNanoSamples( "single bucket query, while an async scene loads (N=10000 K=200 leaf)", listDuring );
    SW_LOG_INFO( "[Bench] async load of %# objects took %# ms (%# queries ran meanwhile)", kLoadedObjectCount, loadMilliseconds, queryCount );

    SW_EXPECT_FALSE( sceneManager.isTransitioning() );
    SW_ASSERT_NOT_NULL( sceneManager.getActiveScene() );
    sceneManager.getActiveScene()->getObjectManager()->update( 0.0f );
    SW_EXPECT_EQUAL( static_cast<size_t>( kLoadedObjectCount ), sceneManager.getActiveScene()->getObjectManager()->getAllGameObjects().size() );
    SW_EXPECT_TRUE( sink != 0 );
    sceneManager.shutdown();
}
