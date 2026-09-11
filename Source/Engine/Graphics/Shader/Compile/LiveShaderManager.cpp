#include "pch.h"

#include "Engine/Graphics/Shader/Compile/LiveShaderManager.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "LiveShaderManager" );

    LiveShaderManager::LiveShaderManager() = default;

    LiveShaderManager::~LiveShaderManager()
    {
        shutdown();
    }

    bool LiveShaderManager::initialize( string_view label )
    {
        _label        = label;
        _bInitialized = true;
        return true;
    }

    void LiveShaderManager::watchShader( const ShaderCompileDesc& desc, const ShaderRecompiledDelegate& onRecompiled )
    {
        BLOCK( "LiveShaderManager 셰이더 등록" )
        {
            string ioPath = ResourceUtil::getResourcePath( desc._filePath );
            if ( ioPath.empty() )
                ioPath = desc._filePath;

            // Map / notify keys are lowercase; I/O keeps the real FS path.
            const string keyPath = FileUtil::normalizePath( ioPath );

            std::unique_lock<std::shared_mutex> lock{ _mutex };
            _mapWatchedShader[keyPath].push_back( { desc, onRecompiled } );

            if ( _bInitialized == false )
            {
                string dirPart = FileUtil::getDirectoryPart( desc._filePath );
                if ( dirPart.empty() == false )
                    initialize( dirPart );
            }

            SW_LOG_INFO( "Registered live shader watch target: %#", ioPath.c_str() );
        }
    }

    void LiveShaderManager::update()
    {
        if ( _bInitialized == false )
            return;

        if ( _listPendingReloadPath.empty() == false )
        {
            vector<string> listReloadToProcess;
            {
                std::unique_lock<std::shared_mutex> lock{ _mutex };
                listReloadToProcess = std::move( _listPendingReloadPath );
                _listPendingReloadPath.clear();
            }

            for ( const string& changedPath : listReloadToProcess )
            {
                vector<WatchedShaderInfo> listToCompile;
                {
                    std::shared_lock<std::shared_mutex>                        lock{ _mutex };
                    unordered_map<string, vector<WatchedShaderInfo>>::iterator it = _mapWatchedShader.find( changedPath );
                    if ( it != _mapWatchedShader.end() )
                        listToCompile = it->second;
                }

                for ( WatchedShaderInfo& watchedInfo : listToCompile )
                {
                    SW_LOG_INFO( "Recompiling shader live: %# (Entry: %#)",
                                 watchedInfo._desc._filePath.c_str(), watchedInfo._desc._entryPoint.c_str() );

                    ShaderCompileResult newResult = ShaderCompiler::compileHLSL( watchedInfo._desc );
                    if ( newResult._bSuccess )
                    {
                        // 캐시가 읽는 이름 그대로 쓴다 — 여기서 이름을 따로 만들면 퍼뮤테이션 해시 같은 축이 한쪽에서만
                        // 빠져 재컴파일 결과가 엉뚱한 요청에 걸리거나 아무 요청에도 안 걸린다(실제로 둘 다 해시가 없었다).
                        const string localPath = ShaderCache::makeLocalCachePath( watchedInfo._desc );

                        const string localDir = FileUtil::getDirectoryPart( localPath );
                        if ( localDir.empty() == false )
                            FileUtil::ensureDirectoryExists( localDir );
                        FileUtil::writeFile( localPath, newResult._bytecode.data(), newResult._bytecode.size() );

                        engine::getShaderCache().clearCache();
                        SW_LOG_INFO( "Live Shader Recompilation Succeeded for %#!",
                                     watchedInfo._desc._filePath.c_str() );

                        if ( watchedInfo._onRecompiled.isBound() )
                            watchedInfo._onRecompiled( changedPath, newResult );
                        if ( _onAnyRecompiled.isBound() )
                            _onAnyRecompiled( watchedInfo._desc._filePath, newResult );
                    }
                    else
                    {
                        SW_LOG_ERROR( "Live Shader Recompilation Failed for %#:\n%#",
                                      watchedInfo._desc._filePath.c_str(), newResult._errorMessage.c_str() );
                    }
                }
            }
        }
    }

    void LiveShaderManager::shutdown()
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _mapWatchedShader.clear();
        _listPendingReloadPath.clear();
        _bInitialized = false;
    }

    void LiveShaderManager::triggerReloadAll()
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _listPendingReloadPath.clear();
        _listPendingReloadPath.reserve( _mapWatchedShader.size() );
        for ( const pair<const string, vector<WatchedShaderInfo>>& pair : _mapWatchedShader )
        {
            _listPendingReloadPath.push_back( pair.first );
        }
    }

} // namespace sw
