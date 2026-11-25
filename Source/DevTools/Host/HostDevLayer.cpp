#include "pch.h"

#include "ModuleManager/Host/HostDevLayer.h"

#include "DevTools/FileWatch/FileWatchService.h"
#include "DevTools/Host/EditorModuleHeadlessTasks.h"
#include "DevTools/Host/HostDevTools.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/DebugKeyActions.h"
#include "Engine/Input/InputManager.h"

#if SW_WITH_DYNAMIC_MODULES
    #include "DevTools/ModuleReload/LiveReload/LiveReloadManager.h"
    #include "DevTools/ModuleReload/ModuleReloadDriver.h"
#endif

namespace sw
{
    namespace
    {
        /**
         * @class HostDevLayer
         * @brief 개발 구성의 개발 계층입니다 — 개발 도구 연결(`HostDevTools`)과, 동적 모듈 구성이면 핫 리로드(`ModuleReloadDriver`)를 듭니다.
         * @details 정적 모듈 구성(웹 · Android 개발, `SW_STATIC_MODULES`)은 리로드 구동기(`ModuleReload/`)를 짓지 않으므로 리로드 창구가 형식에 없다.
         */
        class HostDevLayer final : public IHostDevLayer
        {
        public:
            void addToHostScope( SubsystemScope& host, Engine& engine ) override { _devTools.addToHostScope( host, engine, _fileWatch ); }

            void pollShellActions( float32 deltaTime, bool bEnableEditor ) override
            {
                // 디버그 키(개발 콘솔 키 · 리로드 단축키)는 구성과 상관없이 이번 프레임 것으로 갱신한다 — 리로드 단축키를 요청으로 바꾸는 것만 동적 모듈 구성의 일이다.
                DebugKeyActions& debugKeys = engine::getSubsystem<DebugKeyActions>();
                debugKeys.update( engine::getSubsystem<InputManager>(), deltaTime );
#if SW_WITH_DYNAMIC_MODULES
                _moduleReload.pollReloadActions( debugKeys, bEnableEditor );
#else
                (void)bEnableEditor;
#endif
            }

#if SW_WITH_DYNAMIC_MODULES
            bool initializeModuleReload( uint8 targetMask, SubsystemScope& host, ModuleLoaderSubsystem& moduleLibraryHost, ModuleCatalog& outCatalog,
                                         ModuleResolution& outResolution ) override
            {
                return _moduleReload.initialize( targetMask, host, moduleLibraryHost, _fileWatch, outCatalog, outResolution );
            }

            ILiveReloadManager* getLiveReloadManager() const override { return _moduleReload.getLiveReloadManager(); }
            Subscription        addOnBeforeReloadBatch( BeforeReloadBatchDelegate onBeforeBatch ) override { return _moduleReload.addOnBeforeReloadBatch( onBeforeBatch ); }
            void                requestReload( const utf8* pModuleName ) override { _moduleReload.requestReload( pModuleName ); }
            void                updateModuleReload() override { _moduleReload.update(); }
#endif

            bool runEditorModuleHeadlessTasks( const CommandLineManager& commandLine ) override { return EditorModuleHeadlessTask::runRequested( commandLine ); }
            void attachFrameHooks( Engine& engine ) override { _devTools.attachFrameHooks( engine ); }
            void startConsole( bool bEnableEditor ) override { _devTools.startConsole( bEnableEditor ); }
            void stopConsole() override { _devTools.stopConsole(); }
            void detach() override { _devTools.detach(); }

        private:
            FileWatchService _fileWatch; ///< 프로세스의 파일 감시 창구 — 아래 둘이 빌려 써 맨 앞에 둔다(마지막에 사라진다)
            HostDevTools     _devTools;
#if SW_WITH_DYNAMIC_MODULES
            ModuleReloadDriver _moduleReload;
#endif
        };
    } // namespace

    SW_HOST_DEV_API unique_ptr<IHostDevLayer> makeHostDevLayer()
    {
        return make_unique<HostDevLayer>();
    }
} // namespace sw
