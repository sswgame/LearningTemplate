'use strict';

/**
 * @file DebugConfigurationInjector.js
 * @brief 모든 디버거의 디버그 구성이 시작되기 직전에 끼어들어, 패널에서 고른 인자 · 환경 변수를 넣습니다(CodeLLDB 포함).
 * @details `vscode.debug.registerDebugConfigurationProvider('*', …)` 의 resolve 훅입니다. 하는 일은 `LaunchArgumentUtil.applyLaunchArguments` 가 정합니다.
 *          - `launch.json` 구성에 `"launchArgs": "append"` 또는 `"replace"` 를 적은 구성만 인자를 받습니다.
 *          - CMake Tools 디버그가 CodeLLDB 로 갈 때(`cmake.debugConfig.type: "lldb"`) 환경 변수를 CodeLLDB 모양(`env`)으로 옮깁니다.
 *            인자는 CMake Tools 가 이미 `cmake.debugConfig.args` 에서 넣었으므로 다시 넣지 않습니다.
 *          F5 를 누른 순간의 패널 상태를 씁니다. 디스크에는 아무것도 쓰지 않습니다.
 */

const vscode = require('vscode');

const LaunchArgumentUtil = require('./LaunchArgumentUtil');

/**
 * @brief 디버그 구성 공급자입니다. 구성을 만들지 않고 고치기만 합니다.
 */
class DebugConfigurationInjector {
    /**
     * @param controller `LaunchArgumentController` 입니다.
     * @param logger `LogOutputChannel` 입니다.
     */
    constructor(controller, logger) {
        /** @brief 컨트롤러입니다(지금 선택의 명령줄을 준다). */
        this._controller = controller;
        /** @brief 출력 채널입니다. */
        this._logger = logger;
    }

    /** @brief 모든 디버거 종류에 대해 resolve 훅을 겁니다. @return 해제용 Disposable 입니다. */
    register() {
        return vscode.debug.registerDebugConfigurationProvider('*', this);
    }

    /**
     * @brief VS Code 가 디버그를 시작하기 직전(변수 치환 전)에 부릅니다.
     * @return 고친 구성입니다. 고칠 것이 없으면 받은 그대로입니다.
     */
    async resolveDebugConfiguration(folder, config) {
        if (config === undefined || config === null || typeof config !== 'object')
            return config;
        const bWantsInjection = LaunchArgumentUtil.kLaunchInjectKey in config;
        const launchPayload = bWantsInjection ? await this._controller.makeLaunchPayload() : { listArgument: [], listEnvironment: [] };
        const result = LaunchArgumentUtil.applyLaunchArguments(config, launchPayload.listArgument, launchPayload.listEnvironment);
        if (result.problem !== '') {
            this._logger.warn(`${config.name}: ${result.problem}`);
            void vscode.window.showWarningMessage(`Launch Args: ${config.name} — ${result.problem}`);
        }
        if (result.bInjected)
            this._logger.info(`${config.name} (${config.type}): injected ${JSON.stringify(result.config.args)}`);
        return result.config;
    }
}

module.exports = {
    DebugConfigurationInjector,
};
