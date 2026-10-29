#include "pch.h"

#include "DevTools/Shader/ShaderRecompiler.h"

#include "Core/Concurrency/mutex.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderRecompiler" );

    ShaderRecompiler::ShaderRecompiler() = default;

    ShaderRecompiler::~ShaderRecompiler()
    {
        shutdown();
    }

    bool ShaderRecompiler::initialize( string_view label )
    {
        _label = label;
        _bInitialized.store( true, std::memory_order_release );
        return true;
    }

    void ShaderRecompiler::triggerReloadAll()
    {
        if ( _bInitialized.load( std::memory_order_acquire ) == false || engine::areEngineServicesBound() == false )
            return;

        // 파일 내용 해시 캐시와 스탬프 읽기 결과를 한 번 버린다("전부 다시 본다" 는 약속). 컴파일 캐시는 항목마다 연 헤더의 내용을 직접 보고 낡음을 아니 이것이 없어도 편집은 반영되지만,
        // 같은 시각 · 같은 크기로 다시 쓴 파일은 내용 해시 캐시가 놓칠 수 있다. 반대로 컴파일 캐시를 통째로 우회하지는 않는다. 그러면 이번
        // 편집과 무관한 셰이더까지 모두 다시 컴파일한다. 항목이 자기 헤더만 보고 거른다.
        ShaderCooker::invalidateSharedHeaderCache();

        // 등록 목록을 따로 두지 않는다. **이 실행에서 실제로 컴파일된 셰이더**가 곧 리로드 대상이다
        // (따로 채우는 목록은 채우는 쪽을 빠뜨리면 단축키가 빈 목록을 돈다).
        vector<ShaderCompileDesc> listDesc;
        engine::getSubsystem<ShaderCache>().collectCompiledDescs( listDesc );

        UniqueLock<shared_mutex> lock{ _mutex };
        _listPendingReload = std::move( listDesc );
    }

    void ShaderRecompiler::update()
    {
        if ( _bInitialized.load( std::memory_order_acquire ) == false )
            return;

        vector<ShaderCompileDesc> listToCompile;
        {
            UniqueLock<shared_mutex> lock{ _mutex };
            if ( _listPendingReload.empty() )
                return;
            listToCompile = std::move( _listPendingReload );
            _listPendingReload.clear();
        }

        uint32 changed = 0;
        uint32 failed  = 0;
        for ( const ShaderCompileDesc& desc : listToCompile )
        {
            // 컴파일 캐시는 켜 둔 채로 돈다. 소스가 안 바뀐 셰이더는 여기서 캐시에 맞아 컴파일러를
            // 타지 않는다. 캐시 항목은 소스와 자기가 연 헤더의 내용(의존 기록)을 보므로, 바뀐 헤더를
            // 연 셰이더만 실제로 다시 컴파일된다.
            // 캐시의 실시간 컴파일과 **같은 요청**으로 컴파일한다(Debug 는 디버그 코드젠). 요청 그대로(최적화) 컴파일하면 리로드한
            // 셰이더만 디버그 정보를 잃고 캐시가 디버그 코드젠을 쓴 곳에 최적화 코드젠을 덮어쓴다.
            const ShaderCompileDesc   liveDesc  = ShaderCache::makeLiveCompileDesc( desc );
            const ShaderCompileResult newResult = ShaderCompiler::compileHLSL( liveDesc );
            if ( newResult._bSuccess == false )
            {
                ++failed;
                SW_LOG_ERROR( "Shader reload failed for %# (Entry: %#):\n%#",
                              desc._filePath.c_str(), desc._entryPoint.c_str(), newResult._errorMessage.c_str() );
                continue;
            }

            // 캐시가 읽는 이름 그대로 쓴다. 이름은 캐시(`ShaderCache::storeLocalCache`)가 정하므로 퍼뮤테이션 해시 같은 축이 한쪽에서만
            // 빠져 재컴파일 결과가 엉뚱한 요청에 걸리거나 아무 요청에도 안 걸리는 일이 없다(실제로 둘 다 해시가 없었다).
            // **바이트가 같으면 바이트 파일은 쓰지 않는다.** 쓰면 "갱신" 으로 세어 재컴파일 콜백이 불리고, 바뀌지 않은
            // 셰이더의 PSO 까지 다시 만들게 된다. 연 헤더의 의존 기록은 같아도 새로 쓴다(헤더만 고쳤으면 옛 기록이 낡음으로 남는다).
            // 경로에 지금 소스의 지문이 들어 있고 기록이 지금 헤더와 맞으므로 다음 getOrCompile 이 이 바이트코드를 집는다
            // (ShaderCache 의 1순위가 이 로컬 라이브 캐시다).
            const bool bBytesChanged = ShaderCache::storeLocalCache( liveDesc, newResult );
            if ( bBytesChanged == false )
                continue;

            ++changed;
            if ( _onAnyRecompiled.isBound() )
                _onAnyRecompiled.get()( desc._filePath, newResult );
        }

        if ( changed > 0 && engine::areEngineServicesBound() )
        {
            // 인메모리 캐시는 마지막에 한 번만 비운다. 셰이더마다 비우면 같은 패스의 남은 재컴파일이
            // 방금 지운 항목을 다시 채워 넣는다.
            engine::getSubsystem<ShaderCache>().clearCache();
            // 리플렉션도 비운다. 셰이더가 머티리얼 구조(SW_MATERIAL_BEGIN/END)를 바꿨을 수 있고, 머티리얼은 이 캐시 세대를 보고 레이아웃을
            // 다시 맞춘다(Material::isShaderLayoutSynced). 비우지 않으면 맞춘 레이아웃이 옛 셰이더 것 그대로 남는다.
            ShaderReflectionLibrary::clearCache();
        }

        SW_LOG_INFO( "Shader reload: %# checked, %# updated, %# failed.",
                     static_cast<uint32>( listToCompile.size() ), changed, failed );
    }

    void ShaderRecompiler::shutdown()
    {
        UniqueLock<shared_mutex> lock{ _mutex };
        _listPendingReload.clear();
        _bInitialized.store( false, std::memory_order_release );
    }
} // namespace sw
