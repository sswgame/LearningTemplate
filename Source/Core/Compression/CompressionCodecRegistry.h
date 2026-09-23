/**
 * @file CompressionCodecRegistry.h
 * @brief 압축 코덱 레지스트리입니다. 등록 · 조회 · 기본 코덱을 다룹니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"

namespace sw
{
    /**
     * @class CompressionCodecRegistry
     * @brief 압축 코덱을 등록하고 찾는 레지스트리입니다.
     * @details 런타임에 압축 알고리즘을 등록 · 조회 · 교체할 수 있습니다.
     *
     *          소유자는 `EngineLoop`(테스트에서는 테스트 호스트)이고, Core 에는 이를 가리키는 포인터 슬롯만 둡니다.
     *          `CompressionStream` 은 Core 에 있어서 엔진 서비스 테이블에 닿을 수 없기 때문입니다(Core → Engine 은 레이어
     *          역방향입니다). `Logger` 가 `setGlobalSink` 로 같은 문제를 푸는 방식과 같습니다.
     *
     *          이 슬롯이 생기기 전에는 `pRegistry` 매개변수가 있었지만 아무도 넘기지 않아서 **항상** 내장 코덱이 쓰였고,
     *          `registerCodec` 을 부르는 곳도 하나도 없었습니다. 문서가 약속한 LZ4/Zstd 확장이 통째로 동작하지 않았던 셈입니다.
     *          그다음에는 `getDefault()` 가 함수 지역 static 을 들고 있었는데, 그러면 테스트 호스트가 서비스에 등록하는
     *          인스턴스와 스트림이 보는 인스턴스가 서로 달라집니다(실제로 달랐습니다). 소유자를 하나로 두고 슬롯이 그것을
     *          가리키게 해서 두 문제를 모두 막습니다.
     *
     *          리소스 팩(`ResourcePackReader`)은 이 레지스트리를 쓰지 않습니다. 팩은 자기 포맷의 enum(`PackCompressionType`)을
     *          디스크에 기록하고 직접 해제합니다. 두 enum 은 서로 다른 파일의 독립된 포맷이라(값도 2 · 3 에서 다릅니다), 둘을
     *          엮으면 한쪽의 포맷 변경이 다른 쪽까지 끌고 갑니다.
     */
    class SW_API CompressionCodecRegistry
    {
    public:
        CompressionCodecRegistry();
        ~CompressionCodecRegistry() = default;

        CompressionCodecRegistry( const CompressionCodecRegistry& )            = delete;
        CompressionCodecRegistry& operator=( const CompressionCodecRegistry& ) = delete;

        /**
         * @brief 이 프로세스에서 쓸 레지스트리를 슬롯에 연결합니다. 소유는 호출하는 쪽에 있습니다.
         * @details 연결하는 곳은 `EngineLoop::initialize` 와 `TestFramework/main.cpp` 둘뿐입니다. 두 경로가 같은 인스턴스를 보도록
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

        /**
         * @brief 코덱을 등록합니다. 같은 타입이 이미 있으면 교체합니다.
         * @warning 로드 가능한 모듈(EditorModule · SWGame · GF_* · RHI_*)에서 등록했다면 **그 모듈의 shutdown 에서 반드시
         *          `unregisterCodec` 을 부르십시오.** 기본 레지스트리는 `Engine.dll` 에 있어 모듈보다 오래 삽니다. 모듈이 내려간 뒤
         *          여기 남은 코덱의 vtable 과 소멸자는 언맵된 주소를 가리킵니다. Undo 스택이나 전역 변수와 같은 종류의 함정입니다.
         */
        void registerCodec( unique_ptr<ICompressionCodec> codec );
        void unregisterCodec( CompressionCodecType type );

        ICompressionCodec* getCodec( CompressionCodecType type ) const;
        ICompressionCodec* getCodec( string_view name ) const;

        ICompressionCodec*   getDefaultCodec() const;
        CompressionCodecType getDefaultCodecType() const;
        void                 setDefaultCodecType( CompressionCodecType type );

        bool isCodecRegistered( CompressionCodecType type ) const;

    private:
        void registerBuiltinCodecs();

    private:
        mutable mutex                                                      _mutex;
        unordered_map<CompressionCodecType, unique_ptr<ICompressionCodec>> _mapCodec;
        /**
         * @brief 기본 코덱 종류입니다. `_mutex` 로 보호할 수 없어서 원자 변수로 둡니다.
         * @details `getDefaultCodec()` 은 이 값을 읽은 직후 `getCodec()` 을 부르고, 그쪽이 `_mutex` 를 잡습니다. 여기서도 같은 뮤텍스를
         *          잡으면 재귀 잠금으로 교착합니다(`sw::mutex` 는 재귀 뮤텍스가 아닙니다). 예전에는 아무 동기화도 없어서, 잠금으로
         *          보호되는 맵 바로 옆에서 이 필드만 보호 없이 읽고 쓰였습니다. 렌더 스레드와 잡 스레드가 함께 지나가는 경로입니다.
         */
        atomic<CompressionCodecType> _defaultCodecType;
    };
} // namespace sw
