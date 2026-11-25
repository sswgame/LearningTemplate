#include "pch.h"

#include "DevTools/Client/Shader/ShaderRecompiler.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/AssetReload/ReloadSource.h"
#include "DevTools/AssetReload/ReloadTracker.h"
#include "DevTools/Client/Shader/LiveShaderSubsystem.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"

namespace sw
{
    namespace
    {
        struct ShaderRecompilerInternal
        {
            /** @brief 묶음 컴파일의 결과를 요청 번호의 칸에 둡니다(워커에서 불린다 — 칸마다 한 번). */
            struct BatchReceiver
            {
                vector<ShaderCompileResult> _listResult;

                void recordResult( uint32 requestIndex, const ShaderCompileResult& result ) { _listResult[requestIndex] = result; }
            };

            static const ShaderRecompiler* findRecompiler()
            {
                const LiveShaderSubsystem* const pLiveShader = engine::findSubsystem<LiveShaderSubsystem>();
                return pLiveShader != nullptr ? pLiveShader->findShaderRecompiler() : nullptr;
            }

            /** @brief 감시 값 `Shader.LiveRecompile` — 이 실행에 셰이더 재컴파일기(Ctrl+F8)가 섰으면 1 입니다(Debug 클라이언트만 세운다 — `LiveShaderSubsystem`). 셰이더 리로드 시나리오의 `<SkipUnless>` 가 본다. */
            [[nodiscard]] static bool readLiveCompiler( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = findRecompiler() != nullptr ? 1.0 : 0.0;
                return true;
            }

            /** @brief 감시 값 `Shader.RecompilePassCount` — 끝난 재컴파일 묶음 수입니다. 단축키를 누르기 전 값을 `Store` 하고 달라질 때까지 기다리면 그 묶음이 끝났다. */
            [[nodiscard]] static bool readCompilePassCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ShaderRecompiler* const pRecompiler = findRecompiler();
                if ( pRecompiler == nullptr )
                    return false;
                outValue = static_cast<float64>( pRecompiler->getCompletedPassCount() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( shaderLiveRecompile, "Shader.LiveRecompile", "1 when this run has the live shader recompiler (Ctrl+F8 - Debug clients only), else 0",
                         &ShaderRecompilerInternal::readLiveCompiler );
    SW_AUTOMATION_WATCH( shaderRecompilePassCount, "Shader.RecompilePassCount", "Shader recompile passes (Ctrl+F8) that finished in this run",
                         &ShaderRecompilerInternal::readCompilePassCount );

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

        // 파일 내용 해시 캐시와 경로 캐시를 한 번 버린다("전부 다시 본다" 는 약속). 컴파일 캐시는 항목마다 연 헤더의 내용을 직접 보고 낡음을 아니 이것이 없어도 편집은 반영되지만,
        // 같은 시각 · 같은 크기로 다시 쓴 파일은 내용 해시 캐시가 놓칠 수 있다. 반대로 컴파일 캐시를 통째로 우회하지는 않는다. 그러면 이번
        // 편집과 무관한 셰이더까지 모두 다시 컴파일한다. 항목이 자기 헤더만 보고 거른다.
        ContentHash::clearCache();
        engine::getSubsystem<AssetPaths>().clearPathCache(); // 없던 헤더가 생긴 것도 다시 본다(기록의 "없음" 입력)

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

        const float64        startSeconds = MonotonicClock::nowSeconds();
        ShaderCache&         shaderCache  = engine::getSubsystem<ShaderCache>();
        vector<string>       listChangedPath;
        uint32               changed  = 0;
        uint32               failed   = 0;
        ReloadTracker* const pTracker = engine::findSubsystem<ReloadTracker>();
        // 셰이더 DDC 는 켜 둔 채로 돈다. 소스가 안 바뀐 셰이더는 컴파일러 안에서 DDC 에 맞아 컴파일러를
        // 타지 않는다. DDC 항목은 소스와 자기가 연 헤더의 내용(의존 기록)을 보므로, 바뀐 헤더를
        // 연 셰이더만 실제로 다시 컴파일된다. 한 번에 묶어 워커에서 컴파일한다 — 공용 헤더를 고치면 수백 개가 낡아, 하나씩 하면 게임 스레드가 그 합만큼 멈춘다.
        // 캐시의 실시간 컴파일과 **같은 요청**으로 컴파일한다(진단 정책의 디버그 코드젠). 요청 그대로 컴파일하면 리로드한
        // 셰이더만 디버그 정보를 잃는다.
        // 엔진 설정 값은 지금 캐시의 사본으로 싣는다 — 모은 요청에는 그 항목을 넣을 때의 사본이 실려 있다.
        vector<ShaderCompileDesc> listLiveDesc;
        listLiveDesc.reserve( listToCompile.size() );
        for ( const ShaderCompileDesc& desc : listToCompile )
        {
            listLiveDesc.push_back( ShaderCache::makeLiveCompileDesc( desc, shaderCache.isLiveDebugCodegen() ) );
            listLiveDesc.back()._enginePolicy = shaderCache.findEnginePolicy();
        }
        ShaderRecompilerInternal::BatchReceiver receiver;
        receiver._listResult.resize( listLiveDesc.size() );
        ShaderCompiler::compileBatch( listLiveDesc, ShaderRecordScope::Request, SW_DELEGATE_METHOD( ShaderCompiledDelegate, &ShaderRecompilerInternal::BatchReceiver::recordResult, &receiver ) );
        for ( size_t index = 0; index < listToCompile.size(); ++index )
        {
            const ShaderCompileDesc&   desc      = listToCompile[index];
            const ShaderCompileResult& newResult = receiver._listResult[index];
            if ( newResult._bSuccess == false )
            {
                ++failed;
                if ( pTracker != nullptr )
                    pTracker->record( ReloadSource::Shader, desc._source.str(), ReloadResult::Failed, newResult._errorMessage );
                SW_LOG_ERROR_ID( "ShaderRecompiler.ShaderReloadFailedEntry", "Shader reload failed for %# (Entry: %#):\n%#",
                                 desc._source.c_str(), desc._entryPoint.c_str(), newResult._errorMessage.c_str() );
                continue;
            }

            // **바이트가 같으면 "갱신" 으로 세지 않는다.** 세면 재컴파일 콜백이 불리고, 바뀌지 않은
            // 셰이더의 PSO 까지 다시 만들게 된다. 견줄 바이트는 이 실행이 쓰는 메모리 캐시 항목이다.
            vector<uint8> previousBytecode;
            const bool    bBytesChanged = shaderCache.findCachedBytecode( desc, previousBytecode ) == false || previousBytecode != newResult._bytecode;
            if ( bBytesChanged == false )
            {
                // 같은 바이트는 갱신으로 세지 않지만, 앞 묶음에서 깨졌던 파일을 되돌린 것이면 낡음을 푼다(되돌린 원본은 캐시 바이트와 같다).
                if ( pTracker != nullptr && pTracker->isStale( ReloadSource::Shader, desc._source.str() ) )
                    pTracker->record( ReloadSource::Shader, desc._source.str(), ReloadResult::Reloaded );
                continue;
            }

            // 바뀐 항목만 새 바이트로 바꾼다 — 캐시를 통째로 비우면 이번 묶음에서 깨진 셰이더의 마지막 성공 판까지 사라진다(`ShaderCache::replaceCompiled`).
            if ( shaderCache.replaceCompiled( desc, newResult ) == false )
            {
                ++failed;
                continue;
            }
            ++changed;
            if ( pTracker != nullptr )
                pTracker->record( ReloadSource::Shader, desc._source.str(), ReloadResult::Reloaded );
            listChangedPath.emplace_back( desc._source.str() );
        }

        if ( changed > 0 )
        {
            // 리플렉션은 비운다. 셰이더가 머티리얼 구조(SW_MATERIAL_BEGIN/END)를 바꿨을 수 있고, 머티리얼은 이 캐시 세대를 보고 레이아웃을
            // 다시 맞춘다(Material::isShaderLayoutSynced). 비우지 않으면 맞춘 레이아웃이 옛 셰이더 것 그대로 남는다.
            ShaderReflectionLibrary::clearCache();
        }
        // 알림은 항목을 바꾸고 리플렉션을 비운 **뒤에** 한다 — 받는 쪽(렌더러)이 PSO 를 다시 만들 때 새 바이트코드 · 레이아웃을 집는다.
        if ( listChangedPath.empty() == false && _onAnyRecompiled.isBound() )
            _onAnyRecompiled.get()( listChangedPath );

        if ( pTracker != nullptr )
            pTracker->recordElapsed( ReloadSource::Shader, ( MonotonicClock::nowSeconds() - startSeconds ) * static_cast<float64>( constant::time::kMillisecondsPerSecond ) );
        _completedPassCount.fetch_add( 1, std::memory_order_release );
        SW_LOG_INFO_ID( "ShaderRecompiler.ShaderReloadCheckedUpdated", "Shader reload: %# checked, %# updated, %# failed.",
                        static_cast<uint32>( listToCompile.size() ), changed, failed );
    }

    void ShaderRecompiler::shutdown()
    {
        UniqueLock<shared_mutex> lock{ _mutex };
        _listPendingReload.clear();
        _bInitialized.store( false, std::memory_order_release );
    }
} // namespace sw
