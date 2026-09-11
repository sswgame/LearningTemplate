#include "pch.h"

#include "Engine/Graphics/Shader/Compile/LiveShaderManager.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    namespace
    {
        struct LiveShaderManagerInternal
        {
            /**
             * @brief 스코프 동안 ShaderCompiler 의 디스크 캐시를 끕니다.
             * @details 수동 리로드가 이것을 켜 둔 채로 컴파일하면 **바뀐 `.hlsli` 가 반영되지 않는다.**
             *          그 캐시의 키는 `max(.hlsl mtime, 공유 헤더 타임스탬프)` 인데, 뒤쪽이
             *          `ShaderBaker::getSharedHeaderTimestamp` 의 함수 지역 static 이라 프로세스당 한 번만
             *          계산된다. `.hlsli` 만 고치면 두 값이 다 그대로여서 옛 바이트코드가 그대로 돌아온다.
             */
            struct ScopedDiskCacheBypass
            {
                ScopedDiskCacheBypass()
                    : _bPrevEnabled{ ShaderCompiler::isDiskCacheEnabled() }
                {
                    ShaderCompiler::enableDiskCache( false );
                }

                ~ScopedDiskCacheBypass() { ShaderCompiler::enableDiskCache( _bPrevEnabled ); }

                ScopedDiskCacheBypass( const ScopedDiskCacheBypass& )            = delete;
                ScopedDiskCacheBypass& operator=( const ScopedDiskCacheBypass& ) = delete;

            private:
                bool _bPrevEnabled;
            };
        };
    } // namespace
} // namespace sw

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

    void LiveShaderManager::triggerReloadAll()
    {
        if ( _bInitialized == false || engine::areEngineServicesBound() == false )
            return;

        // 등록표를 따로 두지 않는다 — **이 실행에서 실제로 컴파일된 셰이더**가 곧 리로드 대상이다.
        // 예전에는 `watchShader` 로 채우는 자기 표를 봤는데 그 함수의 호출부가 하나도 없어서,
        // 단축키를 눌러도 빈 표를 돌고 아무 일도 일어나지 않았다.
        vector<ShaderCompileDesc> listDesc;
        engine::getShaderCache().collectCompiledDescs( listDesc );

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _listPendingReload = std::move( listDesc );
    }

    void LiveShaderManager::update()
    {
        if ( _bInitialized == false )
            return;

        vector<ShaderCompileDesc> listToCompile;
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            if ( _listPendingReload.empty() )
                return;
            listToCompile = std::move( _listPendingReload );
            _listPendingReload.clear();
        }

        // 컴파일하는 동안만 디스크 캐시를 우회한다 (ScopedDiskCacheBypass 주석 참고).
        const LiveShaderManagerInternal::ScopedDiskCacheBypass bypass;

        uint32 succeeded = 0;
        for ( const ShaderCompileDesc& desc : listToCompile )
        {
            const ShaderCompileResult newResult = ShaderCompiler::compileHLSL( desc );
            if ( newResult._bSuccess == false )
            {
                SW_LOG_ERROR( "Live Shader Recompilation Failed for %# (Entry: %#):\n%#",
                              desc._filePath.c_str(), desc._entryPoint.c_str(), newResult._errorMessage.c_str() );
                continue;
            }

            // 캐시가 읽는 이름 그대로 쓴다 — 여기서 이름을 따로 만들면 퍼뮤테이션 해시 같은 축이 한쪽에서만
            // 빠져 재컴파일 결과가 엉뚱한 요청에 걸리거나 아무 요청에도 안 걸린다(실제로 둘 다 해시가 없었다).
            const string localPath = ShaderCache::makeLocalCachePath( desc );
            const string localDir  = FileUtil::getDirectoryPart( localPath );
            if ( localDir.empty() == false )
                FileUtil::ensureDirectoryExists( localDir );

            // 방금 쓴 파일의 mtime 이 소스보다 새로우므로 다음 getOrCompile 이 이 바이트코드를 집는다
            // (ShaderCache 의 1순위가 이 로컬 라이브 캐시다).
            FileUtil::writeFile( localPath, newResult._bytecode.data(), newResult._bytecode.size() );

            ++succeeded;
            if ( _onAnyRecompiled.isBound() )
                _onAnyRecompiled( desc._filePath, newResult );
        }

        // 인메모리 캐시는 마지막에 한 번만 비운다 — 셰이더마다 비우면 같은 패스의 남은 재컴파일이
        // 방금 지운 항목을 다시 채워 넣는다.
        if ( engine::areEngineServicesBound() )
            engine::getShaderCache().clearCache();

        SW_LOG_INFO( "Shader reload: %# / %# succeeded.", succeeded, static_cast<uint32>( listToCompile.size() ) );
    }

    void LiveShaderManager::shutdown()
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _listPendingReload.clear();
        _bInitialized = false;
    }
} // namespace sw
