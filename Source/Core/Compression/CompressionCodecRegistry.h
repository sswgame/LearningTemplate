#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"

namespace sw
{
    /**
     * @class CompressionCodecRegistry
     * @brief 압축 코덱 관리 및 팩토리 레지스트리
     * @details 런타임에 다양한 압축 알고리즘을 등록, 조회, 교체할 수 있는 레지스트리입니다.
     *
     *          **소유는 `EngineLoop`(테스트에서는 테스트 호스트)이고, Core 에는 포인터 슬롯만 둔다.**
     *          `CompressionStream` 이 Core 에 있어서 엔진 서비스 테이블에 닿을 수 없기 때문이다
     *          (Core → Engine 은 레이어 역행). `Logger` 가 `setGlobalSink` 로 푸는 것과 같은 모양이다.
     *
     *          이 슬롯이 없던 시절에는 `pRegistry` 매개변수가 있는데 아무도 넘기지 않아 **항상**
     *          내장 코덱으로 갔고, `registerCodec` 은 호출부가 하나도 없었다 — 문서가 약속한 LZ4/Zstd
     *          확장이 통째로 죽어 있었다. 그 다음에는 `getDefault()` 가 함수 지역 static 을 들고 있었는데,
     *          그러면 **테스트 호스트가 서비스에 꽂는 인스턴스와 스트림이 보는 인스턴스가 갈라진다**
     *          (실제로 갈라져 있었다). 소유를 하나로 두고 슬롯이 그것을 가리키게 해서 둘 다 닫는다.
     *
     *          **팩(`ResourcePackReader`)은 여기를 쓰지 않는다.** 팩은 자기 포맷 enum(`PackCompressionType`)
     *          을 디스크에 박고 직접 해제한다. 두 enum 은 서로 다른 파일의 독립된 포맷이라(값도 2·3 에서
     *          다르다) 엮으면 한쪽 포맷 변경이 다른 쪽을 끌고 간다.
     */
    class SW_API CompressionCodecRegistry
    {
    public:
        CompressionCodecRegistry();
        ~CompressionCodecRegistry() = default;

        CompressionCodecRegistry( const CompressionCodecRegistry& )            = delete;
        CompressionCodecRegistry& operator=( const CompressionCodecRegistry& ) = delete;

        /**
         * @brief 이 프로세스에서 쓸 레지스트리를 슬롯에 바인딩합니다. 소유는 호출자입니다.
         * @details 바인딩 지점은 `EngineLoop::initialize` 와 `TestFramework/main.cpp` 둘뿐이고,
         *          같은 포인터를 엔진 서비스에도 꽂아야 두 경로가 하나로 남는다.
         *          해제할 때 `nullptr` 로 되돌린다 — 소유자가 죽은 뒤 슬롯이 가리키면 안 된다.
         */
        static void setActive( CompressionCodecRegistry* pRegistry );

        /**
         * @brief 바인딩된 레지스트리입니다. 아직 없으면 `nullptr`.
         * @details `CompressionStream` 이 레지스트리를 따로 받지 않았을 때 보는 곳이다. 널이면
         *          스트림은 내장 코덱으로 물러난다(Core 만 링크하는 도구 경로).
         * @note 슬롯의 실체는 `Engine.dll`(Core 가 흡수된다)에 하나뿐이다 — 로드되는 모듈들도 같은 것을 본다.
         */
        static CompressionCodecRegistry* getActive();

        void initialize();
        void shutdown();

        /**
         * @brief 코덱을 등록합니다. 같은 타입이 있으면 교체합니다.
         * @warning **로드 가능한 모듈(EditorModule · SWGame · GF_* · RHI_*)에서 등록했다면 그 모듈의
         *          shutdown 에서 반드시 `unregisterCodec` 하십시오.** 기본 레지스트리는 `Engine.dll` 에
         *          살아 모듈보다 오래 갑니다. 모듈이 내려가면 여기 남은 코덱의 vtable 과 소멸자가
         *          언맵된 주소를 가리킵니다 — Undo 스택·전역 변수와 같은 종류의 함정입니다.
         */
        void registerCodec( sw::unique_ptr<ICompressionCodec> codec );
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
        mutable mutex                                           _mutex;
        unordered_map<uint8, sw::unique_ptr<ICompressionCodec>> _mapCodec;
        CompressionCodecType                                    _defaultCodecType;
    };
} // namespace sw
