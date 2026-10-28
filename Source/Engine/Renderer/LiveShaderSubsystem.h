/**
 * @file Engine/Renderer/LiveShaderSubsystem.h
 * @brief 기동 순서에서 LiveShader 를 맡는 서브시스템입니다. 셰이더 라이브 리로드(개발 도구, Debug 클라이언트만)를 시작합니다. 다른 구성은 자리만 둡니다(순서와 백엔드 교체의
 *        재시작 목록이 구성마다 같습니다).
 * @details `onShaderRecompiled` 는 셰이더 바인딩 레이아웃 캐시 항목을 **파괴**하는데, 렌더러의 PSO 레이아웃 테이블과 패스 컨텍스트의 1-entry 캐시가 그 실체를
 *          가리키는 날 포인터를 보관하고 있습니다. 콜백은 게임 스레드(틱의 핫 리로드 블록)에서 불리고 렌더 스레드는 직전 패킷을 그리는 중이라, 렌더 스레드를
 *          멈춘 뒤에 반영합니다. 재컴파일은 개발 중 가끔이라 이때의 스톨은 문제가 되지 않습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    struct ShaderCompileResult;

    class FrameRendererSubsystem;
    class RenderThreadSubsystem;
    class ShaderRecompiler;

    /**
     * @class LiveShaderSubsystem
     * @brief 셰이더 재컴파일을 렌더러에 알리는 서브시스템입니다.
     */
    class SW_API LiveShaderSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 항목이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "LiveShader";

        /** @brief @p frameRendererSubsystem 과 @p renderThreadSubsystem 은 이것보다 오래 삽니다(이것이 그들에 의존합니다). */
        LiveShaderSubsystem( const FrameRendererSubsystem& frameRendererSubsystem, const RenderThreadSubsystem& renderThreadSubsystem );
        ~LiveShaderSubsystem() override;

        LiveShaderSubsystem( const LiveShaderSubsystem& )            = delete;
        LiveShaderSubsystem& operator=( const LiveShaderSubsystem& ) = delete;

        /** @brief 실패해도 성공으로 칩니다. 개발 도구 하나 때문에 기동을 멈추지 않습니다. */
        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override;
        MemoryTag               getSubsystemMemoryTag() const override { return MemoryTag::Shader; }

#if !defined( SW_SHIPPING )
        /** @brief 셰이더 라이브 리로드입니다. Debug 가 아니거나 시작하지 않았으면 nullptr 입니다(배포본에는 라이브 리로드 코드가 없습니다). */
        ShaderRecompiler* findShaderRecompiler() const { return _bReady ? _shaderRecompiler.get() : nullptr; }
#endif

    private:
#if !defined( SW_SHIPPING )
        void onShaderRecompiled( string_view shaderPath, const ShaderCompileResult& result );

        unique_ptr<ShaderRecompiler> _shaderRecompiler;
        Subscription                 _recompiledSubscription; ///< 재컴파일 알림 칸 구독(재컴파일기가 먼저 사라지면 무효가 된다)
#endif
        [[maybe_unused]] const FrameRendererSubsystem& _frameRendererSubsystem; ///< 배포본은 라이브 리로드가 없어 쓰지 않는다
        [[maybe_unused]] const RenderThreadSubsystem&  _renderThreadSubsystem;
        bool                                           _bReady;
    };
} // namespace sw
