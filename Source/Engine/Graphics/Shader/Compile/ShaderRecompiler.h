/**
 * @file ShaderRecompiler.h
 * @brief 이 실행에서 컴파일된 셰이더를 요청이 있을 때 다시 컴파일합니다(수동 리로드).
 */
#pragma once
#include "Core/Common/StdHeaders.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
    using ShaderRecompiledDelegate = Delegate<void( string_view, const ShaderCompileResult& )>;

    /**
     * @class ShaderRecompiler
     * @brief 이 실행에서 컴파일된 셰이더를 **요청이 있을 때** 다시 컴파일합니다.
     * @details `triggerReloadAll` 이 큐를 채우고 `update` 가 실제로 컴파일합니다. 트리거는 디버그 액션
     *          `ReloadShaders`(Ctrl+F8)입니다. 큐를 채울 때 **`ShaderCache` 가 들고 있는 목록**을 씁니다.
     *          이 실행에서 실제로 컴파일된 셰이더가 곧 리로드 대상이기 때문입니다(따로 채우는 등록표를 두면 채우는 쪽을
     *          빠뜨렸을 때 단축키가 빈 표를 돕니다).
     *
     *          `.hlsl` 파일 감시로 **자동** 재컴파일하는 경로는 두지 않습니다.
     */
    class SW_API ShaderRecompiler
    {
    public:
        /** @brief 빈 대기 큐로 시작합니다. */
        ShaderRecompiler();
        /** @brief shutdown 을 불러 대기 큐를 비웁니다. */
        ~ShaderRecompiler();

        /** @brief 복사를 금지합니다. */
        ShaderRecompiler( const ShaderRecompiler& ) = delete;
        /** @brief 대입을 금지합니다. */
        ShaderRecompiler& operator=( const ShaderRecompiler& ) = delete;
        /** @brief 이동을 금지합니다. */
        ShaderRecompiler( ShaderRecompiler&& ) = delete;
        /** @brief 이동 대입을 금지합니다. */
        ShaderRecompiler& operator=( ShaderRecompiler&& ) = delete;

        /** @brief 라벨을 기억하고 초기화된 것으로 표시합니다. */
        bool initialize( string_view label = "Shaders" );

        /** @brief 어떤 셰이더든 다시 컴파일되면 부르는 전역 콜백을 설정합니다(PSO 바인딩 레이아웃 무효화용). */
        void setOnAnyShaderRecompiled( const ShaderRecompiledDelegate& onAnyRecompiled ) { _onAnyRecompiled = onAnyRecompiled; }

        /** @brief 대기 중인 리로드 목록을 꺼내 다시 컴파일합니다. */
        void update();

        /** @brief 대기 큐를 비우고 초기화 표시를 내립니다. */
        void shutdown();

        /**
         * @brief 이 실행에서 컴파일된 셰이더를 모두 리로드 큐에 넣습니다.
         * @details 대상 목록은 `ShaderCache::collectCompiledDescs` 에서 가져옵니다. 별도 등록표를 두지
         *          않는 이유는 클래스 주석을 참고하십시오.
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
