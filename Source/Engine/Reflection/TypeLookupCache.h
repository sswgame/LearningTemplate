/**
 * @file TypeLookupCache.h
 * @brief 타입 테이블 세대와 그 세대를 담은 캐시 칸(`TypeCacheSlot` · `TypeLookupCache`)입니다. `TypeInfo` 를 전방 선언만 보는 잎 헤더입니다.
 * @details `REFLECT_BODY` 가 클래스마다 인라인 `StaticType()` 과 이 캐시 하나를 두므로 리플렉션 매크로 헤더가 include 한다 — 적중 경로가 생성 .cpp 로 가는 호출 없이
 *          세대 비교 하나로 끝난다(언리얼 `StaticClass()` 가 정적 포인터를 바로 돌려주는 것과 같은 자리). `TypeRegistry.h` 는 무거워 끌지 않는다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct TypeInfo;

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
         * @brief 세대 카운터의 주소입니다. 첫 `findSlow` 가 받아 둔다(그 전에는 nullptr 이라 적중하지 않는다).
         * @details 적중 경로가 `getTypeTableGeneration` 의 함수 지역 정적(부를 때마다 스레드 안전 초기화 확인)을 거치지 않게 칸이 스스로 든다. 카운터는 Engine 안의 정적 저장소라 주소가 고정이다.
         */
        mutable atomic<const atomic<uint32>*> _pGenerationCounter{ nullptr };

        /**
         * @brief fqn 의 TypeInfo 입니다. 지금 세대에 찾아 둔 답이 있으면 그것(해제됐으면 nullptr), 없으면 `findSlow` 로 찾습니다. 미등록이면 nullptr 입니다.
         * @details 적중 경로는 여기 인라인이다. 캐스트마다 `StaticType()` 과 `getTypeInfo()` 로 두 번 거치므로
         *          DLL 경계 호출 하나가 곧 비용이다. 놓친 경로는 `findSlow` 호출 하나다.
         */
        const TypeInfo* find( const hashed_string& fqn ) const
        {
            const TypeInfo* pType = nullptr;
            return tryGetCached( pType ) ? pType : findSlow( fqn );
        }
        /**
         * @brief 지금 세대에 찾아 둔 답이 있으면 @p outType 에 담고(없음이면 nullptr) true 입니다. `TypeInfo` 를 읽지 않는다 — `REFLECT_BODY` 의 인라인 `StaticType()` 이 쓴다.
         * @details 죽은 타입은 `findSlow` 가 nullptr 로 적고, 해제 · 되살림은 모두 세대를 올리므로(`unregisterTypesByModule` · `registerClass`) 적중한 답은 살아 있다.
         */
        bool tryGetCached( const TypeInfo*& outType ) const
        {
            const atomic<uint32>* const pCounter = _pGenerationCounter.load( std::memory_order_acquire );
            return pCounter != nullptr && _slot.tryGet( pCounter->load( std::memory_order_acquire ), outType );
        }
        /** @brief 지금 세대의 답이 없을 때 레지스트리에서 찾아 기록합니다(해제된 타입은 nullptr 로). 레지스트리가 없으면 칸을 건드리지 않고 nullptr 을 반환합니다. */
        const TypeInfo* findSlow( const hashed_string& fqn ) const;
    };
} // namespace sw
