#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompileSession.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Asset/Cook/CookRecord.h"
#include "Engine/Content/CookJob.h"

namespace sw
{
    ShaderCompileSession::ShaderCompileSession( const AssetPaths& assetPaths )
        : _pResolver{}
        , _mutex{}
        , _mapInput{}
        , _mapShadowedHeaders{}
        , _mapInputHash{}
    {
        optional<ShaderSourceResolver> resolver = ShaderSourceResolver::make( assetPaths, sw::make_shared<ShaderIncludeCache>() );
        if ( resolver.has_value() )
            _pResolver = sw::make_shared<const ShaderSourceResolver>( std::move( *resolver ) );
    }

    expected<ShaderCompileInput> ShaderCompileSession::resolve( const ShaderSourceID& source )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        iter = _mapInput.find( source.str() );
            if ( iter != _mapInput.end() )
                return iter->second;
        }
        // 디스크는 잠금 밖에서 본다 — 두 워커가 같은 소스를 함께 풀어도 답이 같다.
        const expected<ShaderCompileInput> input = _pResolver->resolve( source );
        ScopedLock<mutex>                  lock{ _mutex };
        return _mapInput.emplace( source.str(), input ).first->second;
    }

    bool ShaderCompileSession::findShadowedHeaders( const DiskPath& shadersFolder, vector<string>& outListProblem )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        iter = _mapShadowedHeaders.find( shadersFolder.str() );
            if ( iter != _mapShadowedHeaders.end() )
            {
                outListProblem = iter->second._listProblem;
                return iter->second._bClean;
            }
        }
        ShadowedHeaders headers;
        headers._bClean = _pResolver->getIncludeResolver().findShadowedHeaders( shadersFolder, headers._listProblem );
        outListProblem  = headers._listProblem;
        ScopedLock<mutex> lock{ _mutex };
        return _mapShadowedHeaders.emplace( shadersFolder.str(), std::move( headers ) ).first->second._bClean;
    }

    bool ShaderCompileSession::isRecordFresh( const CookRecord& record, const CookJobDesc& desc )
    {
        return areInputsFresh( record._listInput ) && CookJob::computeKey( desc, record._listInput ) == record._key;
    }

    bool ShaderCompileSession::areInputsFresh( const vector<CookInput>& listInput )
    {
        const CookInput* const pChanged = VectorUtil::findIf( listInput, [this]( const CookInput& input )
        { return computeInputHash( input ) != input._contentHash; } );
        return pChanged == nullptr;
    }

    uint64 ShaderCompileSession::computeInputHash( const CookInput& input )
    {
        string key = input.makeSourceKey();
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        iter = _mapInputHash.find( key );
            if ( iter != _mapInputHash.end() )
                return iter->second;
        }
        const uint64      hash = input.computeCurrentHash();
        ScopedLock<mutex> lock{ _mutex };
        return _mapInputHash.emplace( std::move( key ), hash ).first->second;
    }
} // namespace sw
