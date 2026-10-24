/**
 * @file OnlinePushRelay.h
 * @brief 다른 서버에 접속한 계정에게 가는 클라이언트 알림의 중계 봉투와, 받는 서버가 그 봉투를 자기 빌드의 알림으로 바꾸는 등록 테이블입니다.
 * @details docs/09_Decisions.md 5-9 서버 버스 push 중계 입니다.
 *          - 알림 번호(메서드 테이블의 열거자 순서)와 알림 본문(B)은 빌드마다 다를 수 있으므로 봉투는 그것을 싣지 않습니다. 알림 열거형 FQN, 열거자 이름, 본문 타입 FQN,
 *            본문의 A(본문 타입의 `kBusVersion` 머리)를 싣고, 받는 서버가 자기 빌드의 번호와 B 로 다시 씁니다. 그래서 클라이언트는 언제나 접속한 서버 빌드의 B 를 받습니다.
 *          - 중계할 수 있는 알림은 키트 서버 `.cpp` 가 이름 공간 범위의 `OnlinePushRelayEntry` 로 등록합니다(메서드 테이블, 알림 열거자, 키트 메시지 목록, 본문 타입).
 *            등록하지 않은 알림, 본문 타입이 다른 알림, 본문을 읽지 못한 알림은 (열거형, 열거자)마다 한 번 오류 로그와 카운터를 남기고 버립니다.
 *          - 열거자 이름을 바꾸면 섞인 동안 옛 빌드가 보낸 중계를 찾지 못합니다(위험 10).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Network/BitStream.h"
#include "Core/String/hashed_string.h"

#include "Engine/Observability/ServiceFactoryList.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Bus/MixedBuildCodec.h"
#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief push 중계 봉투입니다(주제 `push.<서버 id>`). */
    REFLECT()
    struct SW_GF_API OnlinePushRelay
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< 의미 버전(docs/09_Decisions.md 5-9 서버 버스 의미 버전)
        PROPERTY( MaxCount = 128 )
        string _pushEnum{}; ///< 알림 열거형의 FQN(`typeFqn<XxxPushName>`)
        PROPERTY( MaxCount = 64 )
        string _pushName{}; ///< 알림 열거자 이름
        PROPERTY( MaxCount = 128 )
        string _bodyType{}; ///< 본문 타입의 FQN
        PROPERTY( MaxCount = 61440, Min = 0, Max = 255 )
        vector<uint8> _bodyBytes{}; ///< 본문의 A(`MixedBuildCodec::encode`, 본문 타입의 `kBusVersion` 머리)
        PROPERTY()
        AccountID _accountID{ kInvalidAccountID };
    };
} // namespace sw

namespace sw
{
    /** @brief `OnlinePushRelayEntry` 의 본문 타입을 넘기는 태그입니다. */
    template <typename TBody>
    struct OnlinePushBody
    {
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlinePushRelayEntry
     * @brief 중계할 수 있는 알림 하나의 등록 항목입니다. 만들면 등록되고 파괴되면 제거됩니다. 키트 서버 `.cpp` 의 이름 공간 범위에 둡니다.
     */
    class SW_GF_API OnlinePushRelayEntry final : private ServiceFactoryNode
    {
    public:
        /**
         * @param pName 로그와 목록에 쓰는 이름(문자열 리터럴, 빌드 안에서 하나뿐)입니다.
         * 나머지는 키트의 메서드 테이블(`XxxMethod`), 알림 열거자, 키트 메시지 목록(`XxxProtocol::MessageTypes`), 본문 타입 태그입니다.
         */
        template <typename TMethodTable, typename TMessageTypes, typename TBody>
        OnlinePushRelayEntry( const utf8* pName, TMethodTable, typename TMethodTable::PushName pushName, TMessageTypes, OnlinePushBody<TBody> )
            : OnlinePushRelayEntry( pName, TMethodTable::toPush( pushName ), static_cast<int64>( pushName ), &typeFqn<typename TMethodTable::PushName>, &getBodyFqn<TBody>,
                                    &convertBody<TMessageTypes, TBody> )
        {
            static_assert( HasServerBusVersion<TBody>::value, "a relayed push body declares its semantic version as 'static constexpr uint32 kBusVersion'" );
        }
        ~OnlinePushRelayEntry();

        OnlinePushRelayEntry( const OnlinePushRelayEntry& )            = delete;
        OnlinePushRelayEntry& operator=( const OnlinePushRelayEntry& ) = delete;

        /** @brief 이 빌드의 알림 번호입니다. */
        uint16 getKind() const { return _kind; }
        /** @brief @p relay 가 이 알림(열거형 FQN, 열거자 이름)이면 true 입니다. */
        bool matches( const OnlinePushRelay& relay ) const;

    private:
        friend struct OnlinePushRelayRegistry;

        using FqnFunction     = hashed_string ( * )();
        using ConvertFunction = bool ( * )( const vector<uint8>& bodyBytes, BitWriter& outBody );

        OnlinePushRelayEntry( const utf8* pName, uint16 kind, int64 pushValue, FqnFunction pGetPushEnumFqn, FqnFunction pGetBodyFqn, ConvertFunction pConvert );

        template <typename TBody>
        static hashed_string getBodyFqn()
        {
            return TBody::StaticType()->_fullyQualifiedName;
        }

        /** @brief 본문 A 를 혼합 빌드 읽기로 읽어 이 빌드의 B 로 씁니다. */
        template <typename TMessageTypes, typename TBody>
        [[nodiscard]] static bool convertBody( const vector<uint8>& bodyBytes, BitWriter& outBody )
        {
            TBody  body{};
            uint32 readVersion = 0;
            if ( MixedBuildCodec::decode( bodyBytes.data(), bodyBytes.size(), TBody::kBusVersion, body, readVersion ) != MixedBuildReadResult::Read )
                return false;
            return TMessageTypes::write( outBody, body );
        }

        FqnFunction     _pGetPushEnumFqn;
        FqnFunction     _pGetBodyFqn;
        ConvertFunction _pConvert;
        int64           _pushValue;
        uint16          _kind;
    };
} // namespace sw

namespace sw
{
    /** @struct OnlinePushRelayRegistry @brief 중계 항목의 레지스트리와 봉투 만들기 · 바꾸기입니다. GameFramework 에 하나 있습니다. */
    struct SW_GF_API OnlinePushRelayRegistry
    {
        /** @brief @p relay 의 항목입니다. 없으면 nullptr 입니다. */
        static const OnlinePushRelayEntry* findEntry( const OnlinePushRelay& relay );

        /**
         * @brief @p relay 를 이 빌드의 알림 번호와 B 본문으로 바꿉니다.
         * @return 항목이 없거나, 본문 타입이 다르거나, 본문을 읽지 못하면 (열거형, 열거자)마다 한 번 오류 로그와 카운터를 남기고 false 입니다.
         */
        [[nodiscard]] static bool convert( const OnlinePushRelay& relay, uint16& outKind, BitWriter& outBody );

        /** @brief 바꾸지 못해 버린 중계 수입니다(진단, 테스트). */
        static uint64 getRejectedCount();

        /** @brief 알림 @p pushName 과 본문 @p body 로 봉투를 만듭니다. 열거자에 리플렉션 이름이 없거나 본문을 쓰지 못하면 오류 로그를 남기고 false 입니다. */
        template <typename TPushName, typename TBody>
        [[nodiscard]] static bool makeRelay( const AccountID accountID, const TPushName pushName, const TBody& body, OnlinePushRelay& outRelay )
        {
            static_assert( HasServerBusVersion<TBody>::value, "a relayed push body declares its semantic version as 'static constexpr uint32 kBusVersion'" );
            const hashed_string pushEnum = typeFqn<TPushName>();
            const utf8*         pName    = findEnumName( pushEnum, static_cast<int64>( pushName ) );
            if ( pName == nullptr )
            {
                noteMissingPushName( pushEnum, static_cast<int64>( pushName ) );
                return false;
            }
            outRelay._accountID = accountID;
            outRelay._pushEnum  = pushEnum.c_str();
            outRelay._pushName  = pName;
            outRelay._bodyType  = TBody::StaticType()->_fullyQualifiedName.c_str();
            return MixedBuildCodec::encode( TBody::kBusVersion, body, outRelay._bodyBytes );
        }

    private:
        friend class OnlinePushRelayEntry;

        static void registerEntry( OnlinePushRelayEntry* pEntry );
        static void unregisterEntry( OnlinePushRelayEntry* pEntry );
        static void noteMissingPushName( const hashed_string& pushEnum, int64 value );
    };
} // namespace sw
