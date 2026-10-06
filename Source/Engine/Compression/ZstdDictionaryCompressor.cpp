#include "pch.h"

#include "Engine/Compression/ZstdDictionaryCompressor.h"

#include <zdict.h>
#include <zstd.h>

namespace sw
{
    struct ZstdDictionaryCompressor::State
    {
        ZSTD_CDict* _pCompressDictionary{ nullptr };
        ZSTD_DDict* _pDecompressDictionary{ nullptr };
        ZSTD_CCtx*  _pCompressContext{ nullptr };
        ZSTD_DCtx*  _pDecompressContext{ nullptr };
    };
} // namespace sw

namespace sw
{
    ZstdDictionaryCompressor::ZstdDictionaryCompressor()
        : _state{ make_unique<State>() }
    {
    }

    ZstdDictionaryCompressor::~ZstdDictionaryCompressor() { shutdown(); }

    bool ZstdDictionaryCompressor::trainDictionary( const vector<vector<uint8>>& listSample, int32 dictionaryByteCount, vector<uint8>& outDictionaryBytes )
    {
        vector<uint8>  joinedBytes;
        vector<size_t> listSampleSize;
        for ( const vector<uint8>& sample : listSample )
        {
            joinedBytes.insert( joinedBytes.end(), sample.begin(), sample.end() );
            listSampleSize.push_back( sample.size() );
        }
        outDictionaryBytes.resize( static_cast<size_t>( dictionaryByteCount ) );
        const size_t result = ZDICT_trainFromBuffer( outDictionaryBytes.data(), outDictionaryBytes.size(), joinedBytes.data(), listSampleSize.data(),
                                                     static_cast<unsigned>( listSampleSize.size() ) );
        if ( ZDICT_isError( result ) != 0 )
        {
            outDictionaryBytes.clear();
            return false;
        }
        outDictionaryBytes.resize( result );
        return true;
    }

    bool ZstdDictionaryCompressor::initialize( const vector<uint8>& dictionaryBytes, int32 level )
    {
        shutdown();
        _state->_pCompressDictionary   = ZSTD_createCDict( dictionaryBytes.data(), dictionaryBytes.size(), level );
        _state->_pDecompressDictionary = ZSTD_createDDict( dictionaryBytes.data(), dictionaryBytes.size() );
        _state->_pCompressContext      = ZSTD_createCCtx();
        _state->_pDecompressContext    = ZSTD_createDCtx();
        return _state->_pCompressDictionary != nullptr && _state->_pDecompressDictionary != nullptr && _state->_pCompressContext != nullptr &&
               _state->_pDecompressContext != nullptr;
    }

    void ZstdDictionaryCompressor::shutdown()
    {
        ZSTD_freeCDict( _state->_pCompressDictionary );
        ZSTD_freeDDict( _state->_pDecompressDictionary );
        ZSTD_freeCCtx( _state->_pCompressContext );
        ZSTD_freeDCtx( _state->_pDecompressContext );
        *_state = State{};
    }

    bool ZstdDictionaryCompressor::compress( const uint8* pSource, int32 sourceSize, vector<uint8>& outBytes )
    {
        outBytes.resize( ZSTD_compressBound( static_cast<size_t>( sourceSize ) ) );
        const size_t result = ZSTD_compress_usingCDict( _state->_pCompressContext, outBytes.data(), outBytes.size(), pSource, static_cast<size_t>( sourceSize ),
                                                        _state->_pCompressDictionary );
        if ( ZSTD_isError( result ) != 0 )
        {
            outBytes.clear();
            return false;
        }
        outBytes.resize( result );
        return true;
    }

    bool ZstdDictionaryCompressor::decompress( const uint8* pSource, int32 sourceSize, int32 maxRawSize, vector<uint8>& outBytes )
    {
        // 원래 크기는 프레임 머리에 있다 — 상한을 넘으면 풀기 전에 거절하고, 버퍼는 그 크기만 잡는다.
        const uint64 contentSize = ZSTD_getFrameContentSize( pSource, static_cast<size_t>( sourceSize ) );
        if ( contentSize == ZSTD_CONTENTSIZE_ERROR || contentSize == ZSTD_CONTENTSIZE_UNKNOWN || contentSize > static_cast<uint64>( maxRawSize ) )
        {
            outBytes.clear();
            return false;
        }
        outBytes.resize( static_cast<size_t>( contentSize ) );
        const size_t result = ZSTD_decompress_usingDDict( _state->_pDecompressContext, outBytes.data(), outBytes.size(), pSource, static_cast<size_t>( sourceSize ),
                                                          _state->_pDecompressDictionary );
        if ( ZSTD_isError( result ) != 0 )
        {
            outBytes.clear();
            return false; // 상한을 넘게 풀리면 dstSize_tooSmall
        }
        outBytes.resize( result );
        return true;
    }
} // namespace sw
