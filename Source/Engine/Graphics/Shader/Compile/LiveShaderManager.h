/**
 * @file LiveShaderManager.h
 * @brief 등록된 셰이더를 요청 시 다시 컴파일합니다 (수동 리로드).
 */
#pragma once
#include "Core/Common/StdHeaders.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
    using ShaderRecompiledDelegate = Delegate<void( string_view, const ShaderCompileResult& )>;

    /**
     * @class LiveShaderManager
     * @brief 등록된 셰이더를 **요청이 있을 때** 다시 컴파일합니다.
     * @details `triggerReloadAll` 이 큐를 채우고 `update` 가 실제로 컴파일한다. 트리거는 디버그 액션
     *          `ReloadShaders`(Ctrl+F8) 다. 큐를 채울 때 **`ShaderCache` 가 들고 있는 목록**을 쓴다 —
     *          이 실행에서 실제로 컴파일된 셰이더가 곧 리로드 대상이기 때문이다.
     *
     *          예전에는 `watchShader` 로 채우는 자기 등록표를 봤는데, 그 함수의 호출부가 하나도 없어서
     *          단축키가 빈 표를 돌았다 — 리로드가 아무 일도 하지 않았다.
     *
     *          `.hlsl` 파일 감시로 **자동** 재컴파일하던 경로는 없앴다. 그 배선(`attachReloadFileManager`)
     *          역시 호출부가 없어 등록된 적이 없었고, 앞으로도 자동 재컴파일 계획이 없다.
     */
    class SW_API LiveShaderManager
    {
    public:
        /** @brief 워치와 캐시를 비운 채 시작합니다. */
        LiveShaderManager();
        /** @brief 워치와 캐시를 정리합니다. */
        ~LiveShaderManager();

        /** @brief 복사를 금지합니다. */
        LiveShaderManager( const LiveShaderManager& ) = delete;
        /** @brief 대입을 금지합니다. */
        LiveShaderManager& operator=( const LiveShaderManager& ) = delete;
        /** @brief 이동을 금지합니다. */
        LiveShaderManager( LiveShaderManager&& ) = delete;
        /** @brief 이동 대입을 금지합니다. */
        LiveShaderManager& operator=( LiveShaderManager&& ) = delete;

        /** @brief 논리 라벨로 초기화합니다. */
        bool initialize( string_view label = "Shaders" );

        /** @brief 어떤 감시 셰이더든 재컴파일되면 호출되는 전역 콜백을 설정합니다 (PSO 바인딩 레이아웃 무효화용). */
        void setOnAnyShaderRecompiled( const ShaderRecompiledDelegate& onAnyRecompiled ) { _onAnyRecompiled = onAnyRecompiled; }

        /** @brief 대기 중인 리로드 경로를 비우고 다시 컴파일합니다. */
        void update();

        /** @brief 등록 목록과 큐를 비웁니다. */
        void shutdown();

        /**
         * @brief 이 실행에서 컴파일된 셰이더를 모두 리로드 큐에 넣습니다.
         * @details 대상 목록은 `ShaderCache::collectCompiledDescs` 에서 가져온다 — 별도 등록표를 두지
         *          않는 이유는 클래스 주석 참고.
         */
        void triggerReloadAll();

    private:
        mutable std::shared_mutex _mutex;
        vector<ShaderCompileDesc> _listPendingReload;
        ShaderRecompiledDelegate  _onAnyRecompiled;
        string                    _label;
        bool                      _bInitialized{ false };
    };
} // namespace sw
