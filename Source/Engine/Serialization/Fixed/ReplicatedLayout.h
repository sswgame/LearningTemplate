/**
 * @file ReplicatedLayout.h
 * @brief 복제 스키마입니다 — 타입의 `PROPERTY( Replicated )` 칸만 고정 배치형(B) 해석기로 쓰고 읽습니다. 생성기가 낸 두 번째 표(`FixedCodec::_pReplicated`)를 씁니다.
 * @details 언리얼 `UPROPERTY( Replicated )` + `FRepLayout`, Iris `FReplicationStateDescriptor`, 유니티 Netcode `[GhostField]` 자리다(docs/DesignBacklog.md 리플렉션 남은 몫 N1).
 *          - 같은 칸을 컴포넌트와 손 스냅샷 두 곳이 알지 않게 한다: 칸 선언(`Replicated` · `Min` · `Max` · `Step` · `MaxCount`)이 곧 와이어다. 양자화 키는 따로 없다.
 *          - **스키마 해시는 스트림에 싣지 않는다.** 핸드셰이크가 확인한다(`NetMessageChannel::registerReplicatedType` 이 와이어 버전에 섞는다) — 칸 순서 · 범위가
 *            다른 빌드는 연결 때 `VersionMismatch` 로 갈린다. 그래서 이 바이트는 그 채널로 확인한 연결 안에서만 오간다.
 *          - 1 단계는 통째 쓰기다(`writeFull`). 델타는 바깥(`NetSnapshot` 바이트 델타)이 한다. 칸별 변경 비트 쓰기는 대역폭 측정 뒤다.
 *          - 스키마는 그 타입이 선언한 칸만이다. 기반 타입에 `Replicated` 칸이 있으면 찾기(`findSchema`)가 오류다 — 조용히 빼면 그 칸이 복제되지 않는다.
 *          - 읽기는 원자적이다(`FixedLayout::read` 와 같다). RepNotify(`PropertyRoleUtil::callRepNotify`)는 부르는 쪽이 바뀐 칸 비트를 보고 읽기 뒤에 부른다.
 */
#pragma once
#include "Core/Container/DynamicBitset.h"
#include "Core/Foundation/Expected.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"

namespace sw
{
    /**
     * @struct ReplicatedLayout
     * @brief 파일 머리말 참고.
     */
    struct SW_API ReplicatedLayout
    {
        /**
         * @brief @p type 의 복제 스키마(그 타입이 선언한 `Replicated` 칸만 든 코덱)입니다.
         * @return `Replicated` 칸이 없거나(NotFound), 기반 타입에 `Replicated` 칸이 있거나, 칸이 의존하는 코덱이 적격이 아니면 그 이유입니다.
         */
        [[nodiscard]] static expected<const FixedCodec*> findSchema( const TypeInfo& type );

        template <typename T>
        [[nodiscard]] static expected<const FixedCodec*> findSchema()
        {
            const TypeInfo* pType = TypeLookupCache{}.find( typeFqn<T>() );
            if ( pType == nullptr )
                return unexpected( Error{ ErrorKind::NotFound, "type is not registered", typeFqn<T>().c_str() } );
            return findSchema( *pType );
        }

        /** @brief 복제 스키마의 해시입니다(정규 문자열 `R=…` 과 의존 코덱 해시 — 칸 순서 · 범위 키 · 형식이 바뀌면 바뀐다). */
        [[nodiscard]] static uint64 getSchemaHash( const FixedCodec& schema ) { return FixedLayout::getSchemaHash( schema ); }

        template <typename T>
        [[nodiscard]] static uint64 getSchemaHash()
        {
            return getSchemaHash( requireSchema<T>() );
        }

        /** @brief `Replicated` 칸을 선언 순서로 씁니다(스키마 해시 머리 없이). 범위 밖 값이면 false 이고 @p writer 에는 쓰다 만 비트가 남습니다. */
        template <typename T>
        [[nodiscard]] static bool writeFull( const T& object, BitWriter& writer )
        {
            return FixedLayout::writeBodyWithCodec( requireSchema<T>(), &object, writer );
        }

        /**
         * @brief `writeFull` 이 쓴 칸을 읽습니다. 뒤에 다른 값이 이어질 수 있어 남은 비트는 보지 않습니다. `Ok` 일 때만 @p inoutObject 를 바꿉니다.
         * @param pOutChanged 있으면 칸 수만큼 늘리고 값이 바뀐 칸(선언 순서 번호)의 비트를 세웁니다.
         */
        template <typename T>
        [[nodiscard]] static FixedReadResult readFull( BitReader& reader, T& inoutObject, DynamicBitset* pOutChanged )
        {
            const FixedCodec&     schema    = requireSchema<T>();
            T                     temporary = inoutObject; // 스키마에 없는 칸은 이 복사본에서 이어진다
            const FixedReadResult result    = FixedLayout::readPropertiesWithCodec( schema, reader, &temporary );
            if ( result != FixedReadResult::Ok )
                return result;
            if ( pOutChanged != nullptr )
                collectChangedProperties( schema, &inoutObject, &temporary, *pOutChanged );
            inoutObject = std::move( temporary );
            return FixedReadResult::Ok;
        }

    private:
        template <typename T>
        static const FixedCodec& requireSchema()
        {
            const expected<const FixedCodec*> schema = findSchema<T>();
            if ( schema.has_value() == false )
                stopForMissingSchema( typeFqn<T>(), schema.error() );
            return **schema;
        }

        /** @brief 스키마가 없다는 오류를 남기고 멈춥니다. 대체 해시 · 빈 바이트를 내면 핸드셰이크와 스냅샷에 그 값이 섞인다. */
        [[noreturn]] static void stopForMissingSchema( const hashed_string& fqn, const Error& reason );
        static void              collectChangedProperties( const FixedCodec& schema, const void* pBefore, const void* pAfter, DynamicBitset& outChanged );
    };
} // namespace sw
