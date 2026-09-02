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
     * @brief 압축 코덱 관리 및 팩토리 레지스트리 (EngineServices / ModuleService 관리 대상)
     * @details 런타임에 다양한 압축 알고리즘을 등록, 조회, 교체할 수 있는 서비스 레지스트리입니다.
     */
    class SW_API CompressionCodecRegistry
    {
    public:
        CompressionCodecRegistry();
        ~CompressionCodecRegistry() = default;

        CompressionCodecRegistry( const CompressionCodecRegistry& )            = delete;
        CompressionCodecRegistry& operator=( const CompressionCodecRegistry& ) = delete;

        void initialize();
        void shutdown();

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
