#include "pch.h"

#include "ModuleManager/HostDevLayer.h"

#include "ModuleManager/Dev/HostDevTools.h"
#include "ModuleManager/Dev/ModuleReloadDriver.h"

namespace sw
{
    namespace
    {
        /**
         * @class HostDevLayer
         * @brief 개발 구성의 개발 계층입니다 — 개발 도구 연결(`HostDevTools`)과 핫 리로드(`ModuleReloadDriver`)를 듭니다.
         */
        class HostDevLayer final : public IHostDevLayer
        {
        public:
            void addToHostScope( SubsystemScope& host, Engine& engine ) override { _devTools.addToHostScope( host, engine ); }

            bool initializeModuleReload( uint8 targetMask, SubsystemScope& host, ModuleLoaderSubsystem& moduleLibraryHost, ModuleCatalog& outCatalog,
                                         ModuleResolution& outResolution ) override
            {
                return _moduleReload.initialize( targetMask, host, moduleLibraryHost, outCatalog, outResolution );
            }

            LiveReloadManager* getLiveReloadManager() const override { return _moduleReload.getLiveReloadManager(); }
            Subscription       addOnBeforeReloadBatch( BeforeReloadBatchDelegate onBeforeBatch ) override { return _moduleReload.addOnBeforeReloadBatch( onBeforeBatch ); }
            void               requestReload( const utf8* pModuleName ) override { _moduleReload.requestReload( pModuleName ); }
            void               updateModuleReload() override { _moduleReload.update(); }

            void attachFrameHooks( Engine& engine ) override { _devTools.attachFrameHooks( engine ); }
            void startConsole( bool bEnableEditor ) override { _devTools.startConsole( bEnableEditor ); }
            void stopConsole() override { _devTools.stopConsole(); }
            void detach() override { _devTools.detach(); }

        private:
            HostDevTools       _devTools;
            ModuleReloadDriver _moduleReload;
        };
    } // namespace

    unique_ptr<IHostDevLayer> makeHostDevLayer()
    {
        return make_unique<HostDevLayer>();
    }
} // namespace sw
