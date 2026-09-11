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
     *          **소유는 Core 다.** 예전에는 `EngineLoop` 이 인스턴스를 들고 엔진 서비스로 공개했는데,
     *          정작 이걸 봐야 하는 `CompressionStream` 은 Core 에 있어서 **닿을 수 없었다**(Core → Engine
     *          은 레이어 역행이다). 그래서 `pRegistry` 매개변수는 있는데 아무도 넘기지 않았고,
     *          `registerCodec` 은 호출부가 하나도 없었다 — 문서가 약속한 LZ4/Zstd 확장이 통째로 죽어
     *          있었다. 프로세스 기본 인스턴스를 Core 가 들면 그 구멍이 사라진다.
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
         * @brief 이 프로세스의 기본 레지스트리입니다.
         * @details `CompressionStream` 이 레지스트리를 따로 받지 않았을 때 보는 곳이다. 생성자가 내장
         *          코덱을 채우므로 `initialize` 전에도 쓸 수 있다(도구·테스트 경로).
         * @note 실체는 `Engine.dll`(Core 가 흡수된다)에 하나뿐이다 — 로드되는 모듈들도 같은 것을 본다.
         */
        static CompressionCodecRegistry& getDefault();

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
