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
     * @details `watchShader` 로 등록해 두고, `triggerReloadAll` 로 큐에 넣은 뒤 `update` 가 실제로
     *          컴파일한다. 트리거는 디버그 액션 `ReloadShaders`(Ctrl+F8) 다.
     *
     *          `.hlsl` 파일 감시로 **자동** 재컴파일하던 경로는 없앴다. 그 배선(`attachReloadFileManager`)
     *          은 호출부가 하나도 없어 등록된 적이 없었고, 앞으로도 자동 재컴파일 계획이 없다.
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

        /** @brief 셰이더와 선택 재컴파일 콜백을 등록합니다. */
        void watchShader( const ShaderCompileDesc& desc, const ShaderRecompiledDelegate& onRecompiled = {} );

        /** @brief 어떤 감시 셰이더든 재컴파일되면 호출되는 전역 콜백을 설정합니다 (PSO 바인딩 레이아웃 무효화용). */
        void setOnAnyShaderRecompiled( const ShaderRecompiledDelegate& onAnyRecompiled ) { _onAnyRecompiled = onAnyRecompiled; }

        /** @brief 대기 중인 리로드 경로를 비우고 다시 컴파일합니다. */
        void update();

        /** @brief 등록 목록과 큐를 비웁니다. */
        void shutdown();

        /** @brief 등록된 셰이더를 모두 리로드 큐에 넣습니다. */
        void triggerReloadAll();

    private:
        /// @brief 등록된 셰이더 하나
        struct WatchedShaderInfo
        {
            ShaderCompileDesc        _desc;
            ShaderRecompiledDelegate _onRecompiled;
        };

        mutable std::shared_mutex                        _mutex;
        unordered_map<string, vector<WatchedShaderInfo>> _mapWatchedShader;
        vector<string>                                   _listPendingReloadPath;
        ShaderRecompiledDelegate                         _onAnyRecompiled;
        string                                           _label;
        bool                                             _bInitialized{ false };
    };
} // namespace sw
