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
 *          - 키트 서비스(클라이언트 `IOnlineClientService` · 서버 `IOnlineHostService`)는 `getProtocol` 로 이 항목을 돌려줘야 합니다.
 *            그래서 항목 없이는 서비스를 만들 수 없고, 서비스를 가진 키트는 빠짐없이 해시에 섞입니다.
 *          - 해시는 이 빌드에 올라온 항목 전부의 영역, 메서드 테이블의 이름 순서, 메시지 타입마다의 스키마 해시(`FixedLayout::getSchemaHash`)를 섞습니다.
 *            필드, 순서, 범위 키, 열거자가 바뀌면 손 버전 없이 해시가 바뀝니다. 클라이언트가 키트 일부만 올려도 서버와 같은 값이 나와야 하므로 빌드 전체를 섞습니다.
 *            서버와 클라이언트는 같은 빌드로 배포하므로 같은 키트 모듈이 오르고 같은 해시가 나옵니다(`Source/Engine/Serialization/Fixed/README.md` 5절).
 *            한쪽만 키트 모듈을 끄거나(`EditorModuleOverrides`) 핫 리로드로 바꾸면 그쪽 해시만 달라져 접속이 `kVersionMismatch` 로 거절됩니다. 의도한 동작입니다.
 *          - 메서드와 알림 번호는 메서드 테이블의 열거자 순서이고, 그 이름 순서가 해시에 섞입니다. 같은 타입을 받는 두 메서드(`TradeIDRequest` 를 받는 Lock 과 Cancel)의
 *            번호를 맞바꾸면 이름 순서가 바뀌어 해시가 달라지므로, 빌드가 다른 서버와 클라이언트는 접속 때 갈립니다.
 *          - 메시지마다 머리에 싣는 타입 스키마 해시(`FixedLayout::write`)는 메서드와 타입의 짝을 확인합니다. 테이블은 메서드의 이름과 번호만 담고 그 메서드가 받는 타입은
 *            담지 않으므로, 한 메서드가 받는 타입을 목록 안의 다른 타입으로 바꾼 빌드는 해시가 같습니다. 그 어긋남은 읽기가 `SchemaMismatch` 로 거절합니다.
 *          - 메시지 타입에 코덱이 없으면(적격이 아니면) 해시를 계산하는 기동 때 이유와 함께 프로세스가 멈춥니다(`FixedLayout::getSchemaHash`).
 *          언리얼 `FNetworkVersion::GetLocalNetworkVersion`(빌드에서 나온 값 하나를 접속 때 비교)에 해당합니다(기억에 의존).
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/DedicatedServer/ServiceFactoryList.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"

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
            if ( FixedLayout::write( message, outWriter ) )
                return true;
            SW_LOG_ERROR( "Online message '%#' has a value outside its declared range and was not written", typeFqn<T>().c_str() );
            return false;
        }

        /** @brief @p reader 에 남은 비트 전체를 메시지 하나로 읽습니다. `Ok` 일 때만 @p inoutMessage 를 바꿉니다. 받은 바이트라 로그는 부르는 쪽이 정합니다. */
        template <typename T>
        [[nodiscard]] static FixedReadResult read( BitReader& reader, T& inoutMessage )
        {
            static_assert( kContains<T>, "the type is not in this kit's OnlineMessageTypes - add it there so its schema hash joins the online schema hash" );
            return FixedLayout::read( reader, inoutMessage );
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
