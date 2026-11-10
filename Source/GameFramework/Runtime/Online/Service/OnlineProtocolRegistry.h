/**
 * @file OnlineProtocolRegistry.h
 * @brief 온라인 키트 프로토콜의 메시지 타입 목록, 등록 항목, 레지스트리입니다. 접속 핸드셰이크의 첫 필드인 온라인 스키마 해시가 여기서 나옵니다.
 * @details - 키트는 와이어에 싣는 메시지 타입을 `OnlineMessageTypes<...>` 하나에 적고(`XxxProtocol::MessageTypes`), 그 목록으로만 메시지를 쓰고 읽습니다.
 *            목록에 없는 타입을 이 길로 쓰거나 읽으면 컴파일이 멈춥니다. 이 길을 지나지 않는 본문은 셋입니다. 빈 본문(본문 없는 요청과 알림),
 *            `kRateLimited` 응답의 자세한 본문(varuint 기다릴 시간, `OnlineError`), 서버 버스 본문(자기 기술형 A, 주제 정의 `ServerBusTopic`. 배포 중 빌드가 섞이므로 B 가 아니다)입니다.
 *            버스에만 쓰는 타입은 이 목록에 넣지 않습니다. 넣으면 서버끼리만 쓰는 필드를 바꿀 때 클라이언트가 업데이트를 받습니다.
 *          - 키트는 Shared `.cpp` 의 이름 공간 범위 객체로 `OnlineProtocolEntry` 하나를 두고 메서드 표(`XxxMethod`, `OnlineMethodTable`)와 같은 목록을 넘깁니다.
 *            객체를 만들면 레지스트리에 등록되고, 모듈이 언로드되며 파괴되면 제거됩니다. 그래서 메시지를 쓰는 목록과 해시에 섞는 목록이 같은 선언 하나이고,
 *            키트가 해시용 목록을 따로 적는 곳은 없습니다.
 *          - 키트 서비스(클라이언트 `IOnlineClientService` · 서버 `IOnlineServerService`)는 `getProtocol` 로 이 항목을 돌려줘야 합니다.
 *            그래서 항목 없이는 서비스를 만들 수 없고, 서비스를 가진 키트는 빠짐없이 해시에 섞입니다.
 *          - 해시는 이 빌드에 올라온 항목 전부의 영역, 메서드 테이블의 이름 순서, 메시지 타입마다의 스키마 해시(`FixedLayout::getSchemaHash`)를 섞습니다.
 *            필드, 순서, 범위 키, 열거자가 바뀌면 손 버전 없이 해시가 바뀝니다. 클라이언트가 키트 일부만 올려도 서버와 같은 값이 나와야 하므로 빌드 전체를 섞습니다.
 *            서버와 클라이언트는 같은 빌드로 배포하므로 같은 키트 모듈이 오르고 같은 해시가 나옵니다(`Source/Engine/Serialization/Fixed/README.md` 5절).
 *            한쪽만 키트 모듈을 끄거나(`EditorModuleOverrides`) 핫 리로드로 바꾸면 그쪽 해시만 달라져 접속이 `kVersionMismatch` 로 거절됩니다. 의도한 동작입니다.
 *          - 메서드와 알림 번호는 메서드 테이블의 열거자 순서이고, 그 이름 순서가 해시에 섞입니다. 같은 타입을 받는 두 메서드(`TradeIDRequest` 를 받는 Lock 과 Cancel)의
 *            번호를 맞바꾸면 이름 순서가 바뀌어 해시가 달라지므로, 빌드가 다른 서버와 클라이언트는 접속 때 갈립니다.
 *          - 메시지마다 머리에 싣는 타입 스키마 해시(`FixedLayout::write`)는 메서드와 타입의 짝을 확인합니다. 테이블은 메서드의 이름과 번호만 담고 그 메서드가 받는 타입은
 *            담지 않으므로, 한 메서드가 받는 타입을 목록 안의 다른 타입으로 바꾼 빌드는 해시가 같습니다. 그 어긋남은 읽기가 `SchemaMismatch` 로 거절합니다.
 *          - 프로토콜 표(`OnlineProtocolTable`)로 선 키트는 메시지 목록을 따로 적지 않고 표의 메서드 · 알림 서술자에서 끌어냅니다. 그 해시는 메서드마다 (요청, 응답, 호출 규칙)을 차례로 섞으므로
 *            위 문단의 어긋남(받는 타입을 목록 안의 다른 타입으로 바꾼 빌드)도 접속에서 갈립니다.
 *          - 메시지 타입에 코덱이 없으면(적격이 아니면) 해시를 계산하는 기동 때 이유와 함께 프로세스가 멈춥니다(`FixedLayout::getSchemaHash`).
 *          언리얼 `FNetworkVersion::GetLocalNetworkVersion`(빌드에서 나온 값 하나를 접속 때 비교)에 해당합니다(기억에 의존).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Registry/ServiceFactoryList.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"

#include <tuple>
#include <type_traits>

namespace sw
{
    /**
     * @brief 키트 하나가 와이어에 싣는 메시지 타입 목록입니다. 메시지를 쓰고 읽는 길은 이것뿐이며, 같은 목록의 스키마 해시가 등록 항목에 섞입니다.
     * @details 메시지는 고정 배치형(B, `FixedLayout`)입니다. 타입은 `REFLECT` 와 `PROPERTY` 만 가지며, 범위와 길이는 `Min`, `Max`, `MaxCount` 로 적습니다.
     *          범위 밖 값은 잘라 쓰지 않고 쓰기가 실패합니다. 중첩 타입(목록의 원소, 필드 구조체)은 바깥 타입의 해시에 섞이므로 목록에 적지 않습니다.
     * @tparam TMessage 요청, 응답, 알림 본문의 최상위 타입들(클라이언트와 주고받는 것만 넣는다. 서버 버스 본문은 `ServerBusTopic`)
     */
    template <typename... TMessage>
    struct OnlineMessageTypes
    {
        template <typename T>
        static constexpr bool kContains = ( std::is_same_v<T, TMessage> || ... );

        /** @brief @p message 를 @p outWriter 에 씁니다. 범위 밖 값이 있으면 false 이고 이유를 로그에 남깁니다. 고칠 곳은 값을 만든 코드입니다. */
        template <typename T>
        [[nodiscard]] static bool write( BitWriter& outWriter, const T& message )
        {
            static_assert( kContains<T>, "the type is not in this kit's OnlineMessageTypes - add it there so its schema hash joins the online schema hash" );
            if constexpr ( std::is_same_v<T, OnlineNoBody> )
            {
                (void)outWriter;
                (void)message;
                return true;
            }
            else
            {
                if ( FixedLayout::write( message, outWriter ) )
                    return true;
                SW_LOG_ERROR_ID( "OnlineProtocol.MessageOutOfRange", "Online message '%#' has a value outside its declared range and was not written", typeFqn<T>().c_str() );
                return false;
            }
        }

        /** @brief @p reader 에 남은 비트 전체를 메시지 하나로 읽습니다. `Ok` 일 때만 @p inoutMessage 를 바꿉니다. 받은 바이트라 로그는 부르는 쪽이 정합니다. */
        template <typename T>
        [[nodiscard]] static FixedReadResult read( BitReader& reader, T& inoutMessage )
        {
            static_assert( kContains<T>, "the type is not in this kit's OnlineMessageTypes - add it there so its schema hash joins the online schema hash" );
            if constexpr ( std::is_same_v<T, OnlineNoBody> )
            {
                (void)reader;
                (void)inoutMessage;
                return FixedReadResult::Ok;
            }
            else
            {
                return FixedLayout::read( reader, inoutMessage );
            }
        }

        /** @brief 목록 순서대로 타입마다의 스키마 해시를 섞습니다. 코덱이 없는 타입이 있으면 이유와 함께 멈춥니다. */
        static uint64 computeSchemaHash()
        {
            uint64 hash = 0;
            ( ( hash = HashUtil::combine( hash, FixedLayout::getSchemaHash<TMessage>() ) ), ... );
            return hash;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 키트의 메서드 서술자 목록입니다(`OnlineMethodTable::Method`). */
    template <typename... TMethod>
    struct OnlineMethodList
    {
    };

    /** @brief 키트의 알림 서술자 목록입니다(`OnlineMethodTable::Push`). */
    template <typename... TPush>
    struct OnlinePushList
    {
    };

    /** @brief 공통 오류의 자세한 본문 타입 목록입니다(`kUpdateRequired` 의 상점 주소). 메서드 · 알림이 아니어도 와이어에 오르므로 표의 메시지 목록과 해시에 섞습니다. */
    template <typename... TBody>
    struct OnlineErrorBodyList
    {
    };

    template <typename TMethodTable, typename TMethodList, typename TPushList, typename TErrorBodyList = OnlineErrorBodyList<>>
    struct OnlineProtocolTable;

    /**
     * @brief 키트 프로토콜의 표입니다. 메서드와 타입의 짝, 호출 규칙, 알림 본문을 형식 하나에 모읍니다. 메시지 목록 · 해시 · 서버 디스패치 · 클라이언트 `call` 이 모두 여기서 나옵니다.
     * @details 표에는 열거자마다 서술자가 열거자 순서로 하나씩 있어야 합니다. 빠지거나 순서가 다르면 컴파일이 멈춥니다 — 메서드를 더할 때 고칠 곳은 열거자 · 메시지 · 서술자 줄 · 서버 `handleCall` 넷이고, 넷째를 빠뜨려도 컴파일이 잡습니다.
     * @tparam TMethodTable 키트의 `XxxMethod`(`OnlineMethodTable` 상속)
     */
    template <typename TMethodTable, typename... TMethod, typename... TPush, typename... TErrorBody>
    struct OnlineProtocolTable<TMethodTable, OnlineMethodList<TMethod...>, OnlinePushList<TPush...>, OnlineErrorBodyList<TErrorBody...>>
    {
        using MethodTable  = TMethodTable;
        using MessageTypes = OnlineMessageTypes<typename TMethod::Request..., typename TMethod::Reply..., typename TPush::Body..., TErrorBody...>;
        /** @brief 메서드마다 @p TPerMethod 하나를 담는 튜플입니다(클라이언트의 메서드별 대기 표). */
        template <template <typename> class TPerMethod>
        using MethodTuple = std::tuple<TPerMethod<TMethod>...>;

        /** @brief 번호가 @p methodID 인 메서드의 `visitor.visit<TMethod>()` 를 부릅니다. 표에 없으면 false 입니다. */
        template <typename TVisitor>
        static bool visitMethod( uint16 methodID, TVisitor& visitor )
        {
            (void)visitor;
            return ( ( methodID == TMethod::kID ? ( visitor.template visit<TMethod>(), true ) : false ) || ... );
        }

        /** @brief 번호가 @p kind 인 알림의 `visitor.visit<TPush>()` 를 부릅니다. 표에 없으면 false 입니다. */
        template <typename TVisitor>
        static bool visitPush( uint16 kind, TVisitor& visitor )
        {
            (void)visitor;
            return ( ( kind == TPush::kKind ? ( visitor.template visit<TPush>(), true ) : false ) || ... );
        }

        /** @brief 로그인 없이 부를 수 있는 메서드인지입니다. */
        static constexpr bool isAnonymous( uint16 methodID ) { return ( ( methodID == TMethod::kID && TMethod::kAnonymous ) || ... ); }

        /** @brief 메서드마다 (요청, 응답, 호출 규칙), 알림마다 본문을 차례로 섞습니다. 그래서 한 메서드의 타입만 목록 안의 다른 타입으로 바꾼 빌드도 값이 다릅니다. */
        [[nodiscard]] static uint64 computeMessageHash()
        {
            static_assert( sizeof...( TMethod ) == static_cast<size_t>( TMethodTable::kCount ) && sizeof...( TPush ) == static_cast<size_t>( TMethodTable::kPushCount ),
                           "the protocol table needs one descriptor per enumerator" );
            static_assert( isInEnumeratorOrder(), "the protocol table lists its descriptors in enumerator order" );
            uint64 hash = 0;
            ( ( hash = HashUtil::combine( HashUtil::combine( HashUtil::combine( hash, computeBodyHash<typename TMethod::Request>() ), computeBodyHash<typename TMethod::Reply>() ), TMethod::kFlagBits ) ), ... );
            ( ( hash = HashUtil::combine( hash, computeBodyHash<typename TPush::Body>() ) ), ... );
            ( ( hash = HashUtil::combine( hash, computeBodyHash<TErrorBody>() ) ), ... );
            return hash;
        }

    private:
        template <typename TBody>
        [[nodiscard]] static uint64 computeBodyHash()
        {
            if constexpr ( std::is_same_v<TBody, OnlineNoBody> )
                return 0;
            else
                return FixedLayout::getSchemaHash<TBody>();
        }

        static constexpr bool isInEnumeratorOrder()
        {
            int32      methodIndex = 0;
            int32      pushIndex   = 0;
            const bool bMethodOk   = ( ( TMethod::kIndex == methodIndex++ ) && ... );
            const bool bPushOk     = ( ( TPush::kIndex == pushIndex++ ) && ... );
            (void)methodIndex;
            (void)pushIndex;
            return bMethodOk && bPushOk;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineProtocolEntry
     * @brief 키트 프로토콜 하나의 등록 항목입니다. 만들면 등록되고 파괴되면 제거됩니다. 키트는 Shared `.cpp` 의 이름 공간 범위에 둡니다.
     * @details 모듈이 로드될 때 등록되고 언로드될 때 제거돼야 빌드의 키트 목록과 같아집니다. 함수 안의 정적 객체로 두면 처음 부를 때에야 등록돼 클라이언트와 서버가 어긋납니다.
     *          메시지 스키마 해시는 타입 등록이 끝난 뒤(`OnlineProtocolRegistry::computeSchemaHash`)에 계산합니다. 정적 초기화 순서는 타입 등록보다 앞설 수 있기 때문입니다.
     */
    class SW_GF_API OnlineProtocolEntry final : private ServiceFactoryNode
    {
    public:
        /**
         * @param pName 로그에 쓰는 이름(문자열 리터럴). 빌드 안에서 하나뿐이어야 하며, 없거나 겹치면 해시가 키트 목록과 어긋나므로 모든 구성에서 프로세스를 멈춥니다.
         * 두 번째 인자는 키트의 메서드 표(`OnlineMethodTable` 을 상속한 `XxxMethod`)입니다. 영역과 이름 순서를 여기서 가져옵니다.
         */
        template <typename TMethodName, uint16 RangeBase, typename TPushName, typename... TMessage>
        OnlineProtocolEntry( const utf8* pName, OnlineMethodTable<TMethodName, RangeBase, TPushName>, OnlineMessageTypes<TMessage...> )
            : OnlineProtocolEntry( pName, RangeBase, &computeMethodHash<TMethodName, TPushName>, &OnlineMessageTypes<TMessage...>::computeSchemaHash )
        {
        }
        /** @param pName 위와 같습니다. 두 번째 인자는 키트의 프로토콜 표입니다. 영역, 이름 순서, 메서드와 타입의 짝을 여기서 가져옵니다. */
        template <typename TMethodTable, typename TMethodList, typename TPushList, typename TErrorBodyList>
        OnlineProtocolEntry( const utf8* pName, OnlineProtocolTable<TMethodTable, TMethodList, TPushList, TErrorBodyList> )
            : OnlineProtocolEntry( pName, TMethodTable::kRangeBase, &computeMethodHash<typename TMethodTable::MethodName, typename TMethodTable::PushName>,
                                   &OnlineProtocolTable<TMethodTable, TMethodList, TPushList, TErrorBodyList>::computeMessageHash )
        {
        }
        ~OnlineProtocolEntry();

        OnlineProtocolEntry( const OnlineProtocolEntry& )            = delete;
        OnlineProtocolEntry& operator=( const OnlineProtocolEntry& ) = delete;

        uint16 getMethodRange() const { return _methodRange; }

        /**
         * @brief 메서드 테이블의 해시입니다. 두 열거형의 열거자 이름을 순서대로 섞습니다(`computeNameOrderHash`).
         * @details 열거자가 있는데 리플렉션에 이름이 없으면 번호가 해시에서 빠지므로, 대신 쓸 이름 없이 모든 구성에서 프로세스를 멈춥니다.
         */
        template <typename TMethodName, typename TPushName>
        [[nodiscard]] static uint64 computeMethodHash()
        {
            return computeNameOrderHash( collectEnumNames( typeFqn<TMethodName>(), static_cast<int32>( TMethodName::Count ) ),
                                         collectEnumNames( typeFqn<TPushName>(), static_cast<int32>( TPushName::Count ) ) );
        }

        /** @brief 메서드 이름 목록과 알림 이름 목록을 순서대로 섞습니다. 순서가 와이어 번호이므로 두 이름의 위치를 맞바꾸면 값이 바뀝니다. */
        [[nodiscard]] static uint64 computeNameOrderHash( const vector<string>& listMethodName, const vector<string>& listPushName );

    private:
        friend struct OnlineProtocolRegistry;

        using SchemaHashFunction = uint64 ( * )();

        OnlineProtocolEntry( const utf8* pName, uint16 methodRange, SchemaHashFunction pComputeMethodHash, SchemaHashFunction pComputeMessageHash );

        /** @brief @p enumFqn 열거형의 0 .. @p count - 1 열거자 이름입니다. 이름이 없는 값이 있으면 이유를 남기고 멈춥니다. */
        static vector<string> collectEnumNames( const hashed_string& enumFqn, int32 count );

    private:
        SchemaHashFunction _pComputeMethodHash;
        SchemaHashFunction _pComputeMessageHash;
        uint16             _methodRange;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct OnlineProtocolRegistry
     * @brief 키트 프로토콜 항목의 레지스트리입니다. GameFramework 에 하나 있고 키트 모듈이 로드될 때 항목이 오릅니다.
     */
    struct SW_GF_API OnlineProtocolRegistry
    {
        /**
         * @brief 올라온 항목 전부의 영역, 메서드 테이블의 이름 순서, 메시지 스키마 해시와 `OnlineProtocolConstant::kBaseVersion` 을 섞은 온라인 스키마 해시입니다.
         * @details 영역 순(같으면 이름 순)으로 섞으므로 모듈을 로드한 순서와 상관없이 같은 값입니다. 호스트와 클라이언트가 `initialize` 때 한 번 부릅니다.
         */
        [[nodiscard]] static uint64 computeSchemaHash();

    private:
        friend class OnlineProtocolEntry;

        static void registerEntry( OnlineProtocolEntry* pEntry );
        static void unregisterEntry( OnlineProtocolEntry* pEntry );
    };
} // namespace sw
