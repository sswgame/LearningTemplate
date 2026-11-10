/**
 * @file ModuleManager/ILiveReloadManager.h
 * @brief 모듈 관리자(`GameModuleManager` · App 의 `EditorModuleManager`)가 보는 핫 리로드 창구 `ILiveReloadManager` 입니다.
 * @details 구현(`DevTools/ModuleReload/LiveReload/LiveReloadManager` — 섀도 복사 · 연쇄 교체 · 파일 감시)은 개발 도구 공유 라이브러리(DevTools.dll)에 있고,
 *          모듈 관리자는 이 창구만 안다 — 의존은 DevTools → ModuleManager 한 방향이다. 배포는 구현이 없어 모듈 관리자가 nullptr 을 받는다.
 *          언리얼 Core 의 `IHotReloadInterface` 를 Developer 모듈 HotReload 가 구현하는 것과 같은 자리다. 계약은 구현 쪽 머리말(`LiveReloadManager.h`)에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class IModuleCompiler;

    /**
     * @class ILiveReloadManager
     * @brief 모듈 공유 라이브러리의 핫 리로드 창구입니다. 게임 스레드에서 씁니다.
     */
    class ILiveReloadManager
    {
    public:
        using OnBeforeReloadDelegate = Delegate<void()>;
        using OnAfterReloadDelegate  = Delegate<void( void* pLibraryModule )>;
        /** @brief onAfterReload 안에서 하드웨어 예외가 났을 때 불립니다. 인자는 예외 코드(Windows)나 시그널 번호(리눅스)입니다. */
        using OnReloadFaultDelegate = Delegate<void( uint32 faultCode )>;
        /** @brief 새로 로드한 라이브러리를 받아들일지 묻습니다(prepare 때 — false 면 옛 라이브러리를 그대로 둔다). */
        using OnValidateLibraryDelegate = Delegate<bool( void* pLibraryModule )>;
        /** @brief 연쇄 교체의 첫 commit 직전에 불립니다. false 면 아무것도 바꾸지 않는다. */
        using OnBeforeCommitBatchDelegate = Delegate<bool( const vector<string>& listModuleName )>;
        using FlushWorkersDelegate        = Delegate<void()>;
        /** @brief true 를 돌려주는 동안 리로드를 잡아 둡니다(`addReloadHold`). */
        using ReloadHoldDelegate = Delegate<bool()>;

        virtual ~ILiveReloadManager() = default;

        /** @brief 모듈을 등록하고 섀도 복사본으로 로드합니다(@p listDependsOn 이 먼저). */
        [[nodiscard]] virtual bool registerModule( string_view moduleName, const vector<string>& listDependsOn = {} ) = 0;
        /** @brief 핫 리로드하지 않는 공용 모듈(GameFramework)을 섀도 복사 없이 로드하고 제 이름으로 등록합니다. */
        [[nodiscard]] virtual bool loadSharedModule( string_view moduleName ) = 0;
        /** @brief 이미 로드한 모듈의 리로드 직후 콜백을 부릅니다(인스턴스 생성). */
        [[nodiscard]] virtual bool runAfterReload( string_view moduleName ) = 0;

        [[nodiscard]] virtual Subscription setOnBeforeReload( string_view moduleName, OnBeforeReloadDelegate delegate )       = 0;
        [[nodiscard]] virtual Subscription setOnAfterReload( string_view moduleName, OnAfterReloadDelegate delegate )         = 0;
        [[nodiscard]] virtual Subscription setOnReloadFault( string_view moduleName, OnReloadFaultDelegate delegate )         = 0;
        [[nodiscard]] virtual Subscription setOnValidateLibrary( string_view moduleName, OnValidateLibraryDelegate delegate ) = 0;
        [[nodiscard]] virtual Subscription addOnBeforeCommitBatch( OnBeforeCommitBatchDelegate delegate )                     = 0;
        [[nodiscard]] virtual Subscription addReloadHold( ReloadHoldDelegate delegate )                                       = 0;
        [[nodiscard]] virtual Subscription setFlushWorkers( FlushWorkersDelegate delegate )                                   = 0;

        /** @brief DLL 그래프가 섞인 상태로 보고 이후 리로드를 막습니다. */
        virtual void markGraphBroken( string_view reason ) = 0;
        /** @brief 그래프가 깨졌으면 true 입니다. */
        virtual bool isGraphBroken() const = 0;
        /** @brief 로드된 모듈의 핸들입니다. 없으면 nullptr 입니다. */
        virtual void* getModuleHandle( string_view moduleName ) const = 0;

        /** @brief 이 매니저에 빌드 시작 · 끝을 알리는 모듈 컴파일러를 만듭니다(에디터 모듈의 `IModuleCompiler`). */
        [[nodiscard]] virtual unique_ptr<IModuleCompiler> createModuleCompiler() = 0;
    };
} // namespace sw
