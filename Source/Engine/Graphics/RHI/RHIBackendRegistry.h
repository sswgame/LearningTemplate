/**
 * @file RHIBackendRegistry.h
 * @brief 플러그형 RHI 백엔드 팩토리 레지스트리입니다(Shipping 은 백엔드 하나를 정적 링크, Dev 는 RHI_* MODULE DLL 을 로드).
 *
 * @note 수명: create() 로 만든 디바이스는 unloadModules() 와 레지스트리 해체 전에 파괴해야 합니다.
 *       RHI::shutdown() 이 활성 디바이스를 놓은 뒤 unloadModules 를 부릅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Module/DynamicModules.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;

    using RHIDeviceFactoryDelegate = Delegate<unique_ptr<IRHIDevice>()>;

    // ------------------------------------------------------------------------------
    // 1) RHIBackendEntry: 백엔드 + 팩토리 + capability
    // ------------------------------------------------------------------------------
    struct RHIBackendEntry
    {
        RHIBackend               _backend{};
        RHIDeviceFactoryDelegate _factory;
        RHICapabilities          _caps{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class RHIBackendRegistry
     * @brief 백엔드 팩토리의 등록, 조회와 MODULE 로드를 맡습니다.
     */
    class SW_API RHIBackendRegistry : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. 초기화하거나 종료할 것이 없고, 수명만 컬렉션이 정합니다. 서비스 범위는 엔진 범위보다 먼저 생성되고 나중에 사라집니다. */
        static constexpr const utf8* kSubsystemName = "RHIBackendRegistry";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        MemoryTag           getSubsystemMemoryTag() const override { return MemoryTag::RenderCPU; }

    public:
        // ------------------------------------------------------------------------------
        // 2) 등록, 조회, 생성
        // ------------------------------------------------------------------------------
        /** @brief 백엔드 팩토리와 능력을 등록합니다. */
        void registerBackend( RHIBackend backend, const RHIDeviceFactoryDelegate& factory, const RHICapabilities& caps );
        /** @brief 등록된 백엔드 항목을 찾습니다. */
        const RHIBackendEntry* findBackend( RHIBackend backend ) const;

        /**
         * @brief 지금 등록된 백엔드 이름을 쉼표로 이어 반환합니다(오류 메시지용).
         * @details 배포본은 `SW_RHI_TARGET_*` 로 고른 **하나만** 링크합니다. 요청한 백엔드가 없을 때
         *          무엇이 있는지 같이 알려 주지 않으면, 설정 파일을 고친 사람이 원인을 알 수 없습니다.
         */
        string describeRegisteredBackends() const;

        /** @brief 등록된 팩토리로 IRHIDevice 를 만듭니다. */
        unique_ptr<IRHIDevice> createDevice( RHIBackend backend ) const;

        // ------------------------------------------------------------------------------
        // 3) MODULE: createRHIDevice export DLL, 언로드 전 디바이스 파괴 필수
        // ------------------------------------------------------------------------------
#if SW_WITH_DYNAMIC_MODULES
        /** @brief createRHIDevice 를 export 하는 DLL 을 골라 로드합니다. */
        [[nodiscard]] bool tryLoadModule( RHIBackend backend, string_view modulePath );
#endif

        /**
         * @brief MODULE 팩토리를 지운 뒤 FreeLibrary 합니다.
         * @warning MODULE 백엔드에서 만든 IRHIDevice 를 먼저 모두 파괴하십시오.
         */
        void unloadModules();

    private:
    public:
        /** @brief 빈 레지스트리로 만듭니다. */
        RHIBackendRegistry();
        /** @brief 로드된 모듈을 정리합니다. */
        ~RHIBackendRegistry() override;

        /** @brief 복사를 금지합니다. */
        RHIBackendRegistry( const RHIBackendRegistry& ) = delete;
        /** @brief 대입을 금지합니다. */
        RHIBackendRegistry& operator=( const RHIBackendRegistry& ) = delete;

        struct LoadedModule
        {
            RHIBackend _backend{};
            void*      _pHandle{ nullptr };
            string     _modulePath; ///< 언로드할 때 로그에 쓰는 이름. 종료 중에는 리플렉션 조회(`RHI::getBackendTypeName`)를 쓸 수 없다
        };

        vector<RHIBackendEntry> _listEntry;
        vector<LoadedModule>    _listLoadedModule;
    };
} // namespace sw
