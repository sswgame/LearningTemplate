/**
 * @file NetMessageChannel.h
 * @brief 고정 배치형(B) 메시지를 넷 채널에 싣는 등록 창구입니다 — 등록한 타입의 스키마 해시가 핸드셰이크 버전에 섞이고, 확인을 마친 채널 안에서는 본문만 보냅니다.
 * @details 계획은 `Source/Engine/Serialization/Fixed/README.md` 3.3 · 3.10 · 5절 "게임 넷 연결" 입니다.
 *          - 등록(`registerMessage<T>( kind )`)이 (종류 바이트, `FixedLayout::getSchemaHash<T>`) 를 모으고, 봉인(`seal`)이 그 목록을 호스트 설정의 와이어 버전에
 *            섞은 설정을 돌려준다. 배치가 다른 빌드는 핸드셰이크에서 `VersionMismatch` 로 먼저 거절된다 — 메시지마다 싣던 스키마 해시 8 바이트가 빠진다.
 *          - **섞는 일을 따로 부를 자리가 없다**: 본문만 쓰고 읽는 길(`writeMessage` · `readMessage`)은 봉인한 채널에만 열리고, 봉인이 내준 설정만 그 해시를 가진다.
 *            봉인 뒤의 등록과 등록하지 않은 종류 · 타입의 쓰기는 오류다(거절 · 오류 로그). 머리 없는 바이트를 만드는 길은 이 클래스뿐이다(`FixedLayout` 의 friend).
 *          - 연결 하나에 쓰는 채널 하나다. 같은 호스트에 그 설정으로 붙은 상대끼리만 본문을 주고받는다.
 *          - **복제 타입**(`registerReplicatedType<T>( kind )`)은 `PROPERTY( Replicated )` 칸의 복제 스키마 해시(`ReplicatedLayout`)를 같은 목록에 싣는다 —
 *            칸 순서 · 범위가 다른 빌드는 핸드셰이크에서 `VersionMismatch` 다(Iris 프로토콜 해시 · 유니티 고스트 모음 해시 자리). 메시지 종류와 엔티티 타입은 다른 이름공간이라
 *            같은 번호여도 겹치지 않는다. 전역 세션은 봉인 대신 `computeSchemaHash` 를 `GameNetSession::combineWireContent` 에 넘긴다(섞는 함수는 그 하나다).
 * @code
 *     NetMessageChannel channel;
 *     if ( channel.registerMessage<MechArenaSnapshot>( kMechSnapshotKind ) == false ) return false;
 *     NetEndpointConfig hostSettings;
 *     if ( channel.seal( baseSettings, hostSettings ) == false ) return false;
 *     if ( host.initialize( &transport, hostSettings ) == false ) return false; // 핸드셰이크가 스키마를 확인한다
 *     (void)channel.writeMessage( kMechSnapshotKind, snapshot, writer ); // 본문만
 * @endcode
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetMessage.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

namespace sw
{
    /**
     * @class NetMessageChannel
     * @brief 종류 바이트마다 B 타입 하나를 등록하고, 봉인으로 와이어 버전을 만든 뒤 본문만 주고받습니다.
     */
    class SW_API NetMessageChannel
    {
    public:
        NetMessageChannel();

        /**
         * @brief 종류 @p kind 에 타입 @p T 를 등록합니다. 고정 배치형 코덱이 없거나(이유를 로그에), 이미 봉인했거나, 같은 종류가 이미 있으면 오류 로그와 false 입니다.
         */
        template <typename T>
        [[nodiscard]] bool registerMessage( uint8 kind )
        {
            if ( const expected<const FixedCodec*> codec = FixedLayout::findCodec<T>(); codec.has_value() == false )
            {
                logMissingCodec( typeFqn<T>(), codec.error() );
                return false;
            }
            return addEntry( EntryRole::Message, kind, FixedLayout::getSchemaHash<T>() );
        }

        /**
         * @brief 엔티티 타입 @p kind 에 타입 @p T 의 복제 스키마(`Replicated` 칸)를 등록합니다. 스키마가 없거나(이유를 로그에), 이미 봉인했거나, 같은 타입 번호가 이미 있으면 false 입니다.
         */
        template <typename T>
        [[nodiscard]] bool registerReplicatedType( uint8 kind )
        {
            if ( const expected<const FixedCodec*> schema = ReplicatedLayout::findSchema<T>(); schema.has_value() == false )
            {
                logMissingCodec( typeFqn<T>(), schema.error() );
                return false;
            }
            return addEntry( EntryRole::Replicated, kind, ReplicatedLayout::getSchemaHash<T>() );
        }

        /**
         * @brief 등록한 (역할, 번호, 스키마 해시) 를 역할 · 번호 순으로 접은 값입니다. 등록이 없으면 0 입니다(와이어 버전이 바뀌지 않는다).
         * @details 등록 순서와 무관하다. 전역 세션이 와이어 버전에 섞는다(`GameNetSession::combineWireContent`).
         */
        [[nodiscard]] uint64 computeSchemaHash() const;

        /**
         * @brief 등록을 닫고, @p baseSettings 의 와이어 버전에 등록한 (종류, 스키마 해시) 를 종류 순으로 섞은 설정을 @p outSettings 에 줍니다.
         * @details 두 번째 봉인과 등록이 하나도 없는 봉인은 오류 로그와 false 입니다(빈 채널은 섞을 것이 없어 버전이 기반과 같아진다 — 채널을 쓰지 않는 것과 구별되지 않는다).
         */
        [[nodiscard]] bool seal( const NetEndpointConfig& baseSettings, NetEndpointConfig& outSettings );
        bool               isSealed() const { return _bSealed == SW_TRUE; }

        /** @brief 종류 바이트와 본문(스키마 해시 머리 없이)을 @p outWriter 에 씁니다. 봉인 전이거나, 그 종류에 @p T 를 등록하지 않았거나, 범위 밖 값이면 false 입니다. */
        template <typename T>
        [[nodiscard]] bool writeMessage( uint8 kind, const T& value, NetMessageWriter& outWriter ) const
        {
            if ( isRegisteredForUse( EntryRole::Message, kind, FixedLayout::getSchemaHash<T>() ) == false )
                return false;
            return FixedLayout::writeBody( value, outWriter.begin( kind ) );
        }

        /**
         * @brief 받은 본문(@p body — 종류 바이트 다음부터)을 읽습니다. 봉인 전이거나 그 종류에 @p T 를 등록하지 않았으면 `SchemaMismatch` 입니다(이 채널의 스키마가 아니다).
         * @details `Ok` 일 때만 @p inoutValue 를 바꾼다(`FixedLayout::read` 와 같다).
         */
        template <typename T>
        [[nodiscard]] FixedReadResult readMessage( uint8 kind, BitReader& body, T& inoutValue ) const
        {
            if ( isRegisteredForUse( EntryRole::Message, kind, FixedLayout::getSchemaHash<T>() ) == false )
                return FixedReadResult::SchemaMismatch;
            return FixedLayout::readBody( body, inoutValue );
        }

        /** @brief 엔티티 타입 @p kind 의 복제 상태(`Replicated` 칸)를 씁니다. 봉인 전이거나, 그 번호에 @p T 를 등록하지 않았거나, 범위 밖 값이면 false 입니다. */
        template <typename T>
        [[nodiscard]] bool writeReplicated( uint8 kind, const T& value, BitWriter& outWriter ) const
        {
            if ( isRegisteredForUse( EntryRole::Replicated, kind, ReplicatedLayout::getSchemaHash<T>() ) == false )
                return false;
            return ReplicatedLayout::writeFull( value, outWriter );
        }

        /** @brief 받은 복제 상태를 읽습니다. 봉인 전이거나 그 번호에 @p T 를 등록하지 않았으면 `SchemaMismatch` 입니다. `Ok` 일 때만 @p inoutValue 를 바꿉니다. */
        template <typename T>
        [[nodiscard]] FixedReadResult readReplicated( uint8 kind, BitReader& body, T& inoutValue, DynamicBitset* pOutChanged ) const
        {
            if ( isRegisteredForUse( EntryRole::Replicated, kind, ReplicatedLayout::getSchemaHash<T>() ) == false )
                return FixedReadResult::SchemaMismatch;
            return ReplicatedLayout::readFull( body, inoutValue, pOutChanged );
        }

    private:
        /** @brief 등록 한 줄의 역할입니다. 번호는 역할마다 따로다(메시지 종류 · 엔티티 타입). */
        enum class EntryRole : uint8
        {
            Message,
            Replicated,
        };

        struct Entry
        {
            uint64    _schemaHash{ 0 };
            uint8     _kind{ 0 };
            EntryRole _role{ EntryRole::Message };
        };

        [[nodiscard]] bool addEntry( EntryRole role, uint8 kind, uint64 schemaHash );
        /** @brief 봉인했고 @p role 의 @p kind 에 @p schemaHash 를 등록했으면 true 입니다. 아니면 오류 로그를 남깁니다(호출부 결함이다). */
        bool        isRegisteredForUse( EntryRole role, uint8 kind, uint64 schemaHash ) const;
        static void logMissingCodec( const hashed_string& typeFqn, const Error& reason );

    private:
        vector<Entry> _listEntry; ///< 종류 순(봉인이 정렬한다)
        uint8         _bSealed;
    };
} // namespace sw
