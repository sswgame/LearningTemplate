'use strict';

/**
 * @file Extension.js
 * @brief 확장 진입점입니다 — CMake 작업 폴더를 골라 컨트롤러 · 웹뷰 · 명령을 잇습니다.
 * @details 어느 CMake 프로젝트에서나 돕니다. 카탈로그(전역 변수 · 인자 목록)는 설정 `launchArgs.catalog` 의 프로필이 있을 때만
 *          생기고, 없으면 사용자 인자 · 환경 변수 · 프리셋 · 실행 · 디버그만 됩니다. 시작은 기다리지 않습니다(스캔은 뒤에서 돈다).
 */

const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

const { DebugConfigurationInjector } = require('./DebugConfigurationInjector');
const { LaunchArgumentController } = require('./LaunchArgumentController');
const { LaunchArgumentViewProvider, kViewId } = require('./LaunchArgumentViewProvider');
const { StatusBarIndicator } = require('./StatusBarIndicator');

const kContextKey = 'launchArgs.isActive';
const kOutputChannelName = 'Launch Args';
/** @brief CMake 프로젝트 루트를 알아보는 파일입니다. */
const kListCmakeRootFile = ['CMakePresets.json', 'CMakeLists.txt'];

/** @brief 작업 폴더 중 CMake 프로젝트를 찾습니다. 없으면 첫 파일 시스템 폴더, 그것도 없으면 null 입니다. */
function findWorkspaceFolderInternal() {
    const listFolder = (vscode.workspace.workspaceFolders === undefined ? [] : vscode.workspace.workspaceFolders).filter((folder) => folder.uri.scheme === 'file');
    for (const folder of listFolder) {
        if (kListCmakeRootFile.some((fileName) => fs.existsSync(path.join(folder.uri.fsPath, fileName))))
            return folder;
    }
    return listFolder.length > 0 ? listFolder[0] : null;
}

/** @brief 명령 팔레트 · 뷰 제목 줄 명령을 겁니다. */
function registerCommandInternal(context, controller) {
    const mapCommand = new Map([
        ['launchArgs.refresh', () => controller.refresh()],
        ['launchArgs.copyCommandLine', () => controller.copyCommandLine()],
        ['launchArgs.run', () => controller.run()],
        ['launchArgs.debug', () => controller.debug()],
        ['launchArgs.disableAll', () => controller.disableAll()],
        ['launchArgs.resetState', () => controller.resetState()],
    ]);
    for (const [commandId, callback] of mapCommand)
        context.subscriptions.push(vscode.commands.registerCommand(commandId, callback));
}

/**
 * @brief VS Code 가 확장을 켤 때 부릅니다.
 * @return 시험이 쓰는 내부 손잡이 `{ controller, initialization, injector }` 입니다. 작업 폴더가 없으면 undefined 입니다.
 */
async function activate(context) {
    const workspaceFolder = findWorkspaceFolderInternal();
    await vscode.commands.executeCommand('setContext', kContextKey, workspaceFolder !== null);
    if (workspaceFolder === null)
        return undefined;

    const logger = vscode.window.createOutputChannel(kOutputChannelName, { log: true });
    const controller = new LaunchArgumentController(workspaceFolder, logger);
    const provider = new LaunchArgumentViewProvider(context.extensionUri, controller);
    context.subscriptions.push(
        logger,
        controller,
        provider,
        vscode.window.registerWebviewViewProvider(kViewId, provider, { webviewOptions: { retainContextWhenHidden: true } }),
    );
    registerCommandInternal(context, controller);
    const injector = new DebugConfigurationInjector(controller, logger);
    context.subscriptions.push(injector.register(), new StatusBarIndicator(controller));

    const initialization = controller.initialize().catch((error) => {
        logger.error(`initialization failed: ${error.stack || error.message}`);
    });
    return { controller, initialization, injector };
}

/** @brief VS Code 가 확장을 끌 때 부릅니다. 정리는 `context.subscriptions` 가 합니다. */
function deactivate() {
}

module.exports = {
    activate,
    deactivate,
};
