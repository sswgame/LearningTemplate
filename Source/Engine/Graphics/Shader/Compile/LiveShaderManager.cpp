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
            /** @brief 파일 내용이 주어진 바이트열과 같은지 봅니다. 파일이 없으면 false. */
            static bool fileHasSameBytes( const string& filePath, const vector<uint8>& bytes )
            {
                if ( FileUtil::fileExists( filePath ) == false )
                    return false;

                vector<uint8> listExisting;
                if ( FileUtil::readFile( filePath, listExisting ) == false )
                    return false;
                return listExisting == bytes;
            }
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

        // 공유 헤더(.hlsli) 타임스탬프 캐시를 한 번 버린다. 이게 없으면 컴파일 캐시의 키가 그대로라
        // **바뀐 헤더가 반영되지 않는다.** 반대로 캐시를 통째로 우회하지는 않는다 — 그러면 이번
        // 편집과 무관한 셰이더까지 전부 다시 컴파일한다. 키를 정확하게 만들어 두고 캐시가 거르게 한다.
        ShaderBaker::invalidateSharedHeaderTimestamp();

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

        uint32 changed = 0;
        uint32 failed  = 0;
        for ( const ShaderCompileDesc& desc : listToCompile )
        {
            // 컴파일 캐시는 켜 둔 채로 돈다 — 소스가 안 바뀐 셰이더는 여기서 캐시에 맞아 컴파일러를
            // 타지 않는다. 키에 `.hlsl` mtime 과 공유 헤더 타임스탬프가 들어 있으므로, 방금 무효화한
            // 덕분에 바뀐 것만 실제로 다시 컴파일된다.
            const ShaderCompileResult newResult = ShaderCompiler::compileHLSL( desc );
            if ( newResult._bSuccess == false )
            {
                ++failed;
                SW_LOG_ERROR( "Shader reload failed for %# (Entry: %#):\n%#",
                              desc._filePath.c_str(), desc._entryPoint.c_str(), newResult._errorMessage.c_str() );
                continue;
            }

            // 캐시가 읽는 이름 그대로 쓴다 — 여기서 이름을 따로 만들면 퍼뮤테이션 해시 같은 축이 한쪽에서만
            // 빠져 재컴파일 결과가 엉뚱한 요청에 걸리거나 아무 요청에도 안 걸린다(실제로 둘 다 해시가 없었다).
            const string localPath = ShaderCache::makeLocalCachePath( desc );

            // **바이트가 같으면 쓰지 않는다.** 그냥 덮어쓰면 mtime 이 새로워져 ShaderCache 가 무관한
            // 셰이더까지 디스크에서 다시 읽고, PSO 도 전부 다시 만들게 된다.
            if ( LiveShaderManagerInternal::fileHasSameBytes( localPath, newResult._bytecode ) )
                continue;

            const string localDir = FileUtil::getDirectoryPart( localPath );
            if ( localDir.empty() == false )
                FileUtil::ensureDirectoryExists( localDir );

            // 방금 쓴 파일의 mtime 이 소스보다 새로우므로 다음 getOrCompile 이 이 바이트코드를 집는다
            // (ShaderCache 의 1순위가 이 로컬 라이브 캐시다).
            FileUtil::writeFile( localPath, newResult._bytecode.data(), newResult._bytecode.size() );

            ++changed;
            if ( _onAnyRecompiled.isBound() )
                _onAnyRecompiled( desc._filePath, newResult );
        }

        if ( changed > 0 && engine::areEngineServicesBound() )
        {
            // 인메모리 캐시는 마지막에 한 번만 비운다 — 셰이더마다 비우면 같은 패스의 남은 재컴파일이
            // 방금 지운 항목을 다시 채워 넣는다.
            engine::getShaderCache().clearCache();
        }

        SW_LOG_INFO( "Shader reload: %# 개 확인, %# 개 갱신, %# 개 실패.",
                     static_cast<uint32>( listToCompile.size() ), changed, failed );
    }

    void LiveShaderManager::shutdown()
    {
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _listPendingReload.clear();
        _bInitialized = false;
    }
} // namespace sw
