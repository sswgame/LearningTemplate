/**
 * @file CompressionCodecRegistry.h
 * @brief 압축 코덱 레지스트리입니다. 등록 · 조회 · 기본 코덱을 다룹니다.
 */
#pragma once
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /**
     * @class CompressionCodecRegistry
     * @brief 압축 코덱을 등록하고 찾는 레지스트리입니다.
     * @details 런타임에 압축 알고리즘을 등록 · 조회 · 교체할 수 있습니다.
     *
     *          소유자는 `Engine`(테스트에서는 테스트 호스트)이고, Core 에는 이를 가리키는 포인터 슬롯만 둡니다.
     *          `CompressionStream` 은 Core 에 있어서 엔진 서비스 테이블에 닿을 수 없기 때문입니다(Core → Engine 은 레이어
     *          역방향입니다). `Logger` 가 `setGlobalSink` 로 같은 문제를 푸는 방식과 같습니다.
     *
     *          소유자를 하나로 두고 슬롯이 그것을 가리키므로, 등록한 코덱(LZ4/Zstd 등)을 스트림이 실제로 씁니다.
     *          주의: 함수 지역 static 인스턴스를 따로 두면 테스트 호스트가 서비스에 등록하는 인스턴스와 스트림이 보는
     *          인스턴스가 서로 달라집니다.
     *
     *          리소스 팩(`PackReader`)도 이 레지스트리에서 코덱을 찾습니다. 팩은 자기 포맷의 enum(`PackCompressionType`)을
     *          디스크에 기록하고, 두 enum 은 서로 다른 파일의 독립된 포맷이라 값이 다릅니다. 숫자를 건너다니지 않고 표 한 곳
     *          (`PackCompressionUtil::kArrCodecMapping`, Engine)에서 옮긴 뒤 조회합니다.
     */
    class SW_API CompressionCodecRegistry : public ISubsystem
    {
    public:
        CompressionCodecRegistry();
        /** @brief 슬롯(`getActive`)이 이 레지스트리를 가리키면 비웁니다. */
        ~CompressionCodecRegistry() override;

        CompressionCodecRegistry( const CompressionCodecRegistry& )            = delete;
        CompressionCodecRegistry& operator=( const CompressionCodecRegistry& ) = delete;

        /**
         * @brief 이 프로세스에서 쓸 레지스트리를 슬롯에 연결합니다. 소유는 호출하는 쪽에 있습니다.
         * @details 연결하는 곳은 `Engine::initialize` 와 `TestFramework/main.cpp` 둘뿐입니다. 두 경로가 같은 인스턴스를 보도록
         *          같은 포인터를 엔진 서비스에도 등록해야 합니다. 해제할 때는 `nullptr` 로 되돌립니다. 소유자가 사라진 뒤에도
         *          슬롯이 그 주소를 가리키면 안 됩니다.
         */
        static void setActive( CompressionCodecRegistry* pRegistry );

        /**
         * @brief 연결된 레지스트리를 반환합니다. 아직 없으면 `nullptr` 입니다.
         * @details `CompressionStream` 이 레지스트리를 따로 받지 않았을 때 여기를 봅니다. `nullptr` 이면 스트림은 내장 코덱으로
         *          대신합니다(Core 만 링크하는 도구 경로).
         * @note 슬롯의 실체는 `Engine.dll`(Core 가 흡수됩니다)에 하나뿐이라, 로드되는 모듈들도 같은 슬롯을 봅니다.
         */
        static CompressionCodecRegistry* getActive();

        void initialize();
        void shutdown();

        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Compression";
        /**
         * @brief 기동 순서의 자리 — 내장 코덱을 갖추고 슬롯(`setActive`)에 연결합니다. 맨 먼저 서므로 맨 나중에 소멸한다(팩을 내리는 Resource 해제까지 코덱이 산다).
         * @details 종료는 코덱을 거두지 않는다 — 등록한 쪽이 든 구독이 거둔다(`registerCodec`). 외부 라이브러리 코덱은 호스트가 기동 전에 붙인다.
         */
        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 코덱을 등록하고 그 구독을 돌려줍니다. 같은 타입이 이미 있으면 교체합니다. 구독이 사라지면 거둡니다(교체된 옛 구독은 지금 등록을 지우지 않는다).
         * @details 레지스트리는 `Engine.dll` 에 있어 모듈보다 오래 산다 — 모듈이 등록한 코덱(vtable 이 모듈에 있다)은 모듈의 객체가 든 구독과 함께 빠진다.
         */
        [[nodiscard]] Subscription registerCodec( unique_ptr<ICompressionCodec> codec );

        ICompressionCodec* getCodec( CompressionCodecType type ) const;
        ICompressionCodec* getCodec( string_view name ) const;

        ICompressionCodec*   getDefaultCodec() const;
        CompressionCodecType getDefaultCodecType() const;
        void                 setDefaultCodecType( CompressionCodecType type );

        bool isCodecRegistered( CompressionCodecType type ) const;

    private:
        void registerBuiltinCodecs() SW_REQUIRES_LOCK( _mutex );
        /** @brief 등록 구독 @p id 가 풀렸습니다. 그 타입의 지금 등록이 이 구독이면 거둡니다. */
        void releaseSubscription( uint64 id );

    private:
        mutable mutex                                                                _mutex;
        unordered_map<CompressionCodecType, unique_ptr<ICompressionCodec>> _mapCodec SW_GUARDED_BY( _mutex );
        unordered_map<CompressionCodecType, uint64> _mapOwner                        SW_GUARDED_BY( _mutex ); ///< 타입마다 지금 등록의 구독 번호(내장은 없다)
        uint64 _nextOwnerID                                                          SW_GUARDED_BY( _mutex ){ 1 };
        /**
         * @brief 기본 코덱 종류입니다. `_mutex` 로 보호할 수 없어서 원자 변수로 둡니다.
         * @details `getDefaultCodec()` 은 이 값을 읽은 직후 `getCodec()` 을 부르고, 그쪽이 `_mutex` 를 잡습니다. 여기서도 같은 뮤텍스를
         *          잡으면 재귀 잠금으로 교착합니다(`sw::mutex` 는 재귀 뮤텍스가 아닙니다). 렌더 스레드와 잡 스레드가 함께 지나가는
         *          경로라 보호 없이 둘 수도 없습니다.
         */
        atomic<CompressionCodecType> _defaultCodecType;

        /** @brief 이 객체가 내준 구독입니다. 맨 끝 멤버라 먼저 사라져 남은 구독을 무효화한다. */
        SubscriptionList _subscriptions{ SubscriptionList::ReleaseFunction::create<&CompressionCodecRegistry::releaseSubscription>( this ) };
    };
} // namespace sw
