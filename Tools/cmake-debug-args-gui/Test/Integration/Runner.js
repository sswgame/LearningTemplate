'use strict';

/**
 * @file Runner.js
 * @brief 실제 VS Code 확장 호스트 안에서 도는 통합 시험입니다(`--extensionTestsPath`).
 * @details `Scripts/ExtensionTool.py test --integration` 이 임시 작업 폴더(`Fixture/` 사본 — 엔진과 무관한 프로필)를 만들고
 *          격리된 VS Code 를 띄워 이 파일의 `run()` 을 부릅니다. 보는 것:
 *          - 프로필대로 카탈로그가 만들어지는가(`--` 접두사 · 다른 매크로).
 *          - 값을 바꾸면 `.vscode/settings.json` 의 `cmake.debugConfig.args` 에 쓰이고 **주석과 다른 키가 남는가**.
 *          - 설정을 밖에서 고치면 선택으로 읽어 들이는가(모르는 글은 사용자 인자로).
 *          - 모두 끄면 `cmake.debugConfig` 키가 사라지는가.
 *          실패하면 던져서 VS Code 가 0 이 아닌 코드로 끝나게 합니다.
 */

const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

const kExtensionId = 'sw-engine.cmake-debug-args-gui';

/** @brief 조건이 참이 될 때까지 기다립니다. 시한을 넘기면 던집니다. */
async function waitUntil(predicate, description, timeoutMs = 10000) {
    const startTime = Date.now();
    while (Date.now() - startTime < timeoutMs) {
        if (await predicate())
            return;
        await new Promise((resolve) => setTimeout(resolve, 50));
    }
    throw new Error(`timed out waiting for: ${description}`);
}

/** @brief 시험 작업 폴더의 settings.json 글입니다. */
function readSettingsText() {
    const workspacePath = vscode.workspace.workspaceFolders[0].uri.fsPath;
    return fs.readFileSync(path.join(workspacePath, '.vscode', 'settings.json'), 'utf8');
}

/** @brief 통합 시험 본체입니다. */
async function run() {
    const extension = vscode.extensions.getExtension(kExtensionId);
    assert.ok(extension !== undefined, `${kExtensionId} is not loaded`);
    const api = await extension.activate();
    assert.ok(api !== undefined && api.controller !== undefined, 'activate() returned no controller');
    await api.initialization;
    const controller = api.controller;

    // 1) 카탈로그 — 엔진과 무관한 프로필(Fixture/profile.json)
    let state = controller.makeViewState(true);
    assert.deepEqual(state.catalog.listGlobalVariable.map((variable) => variable.name), ['fog_density', 'fog_mode']);
    assert.deepEqual(state.catalog.listArgument.map((argument) => argument.name), ['USE_GL', 'USE_VK', 'WIDTH']);
    assert.deepEqual(state.listBanner.filter((banner) => banner.level !== 'info'), [], 'no banner expected on a clean fixture');

    // 2) 값을 바꾸면 cmake.debugConfig 에 쓰이고, 주석 · 다른 키는 남는다
    await controller.handleMessage({ type: 'setGlobalVariable', name: 'fog_density', value: '0.5' });
    await controller.handleMessage({ type: 'setExclusiveChoice', groupId: 'gpu', name: 'USE_VK' });
    await controller.handleMessage({ type: 'setArgument', name: 'WIDTH', value: '800' });
    await controller.handleMessage({ type: 'addEnvironment', name: 'FOG_LOG', value: '1' });
    await controller.flush();
    await waitUntil(() => readSettingsText().includes('--fog_density=0.5'), 'settings.json gets the argument');
    const settingsText = readSettingsText();
    assert.ok(settingsText.includes('// fixture comment that must survive'), 'comments in settings.json are preserved');
    assert.ok(settingsText.includes('"cmakeDebugArgs.catalog"'), 'other settings are preserved');
    const debugConfig = vscode.workspace.getConfiguration('cmake').get('debugConfig');
    assert.deepEqual(debugConfig.args, ['--vk', '--width=800', '--fog_density=0.5']);
    assert.deepEqual(debugConfig.environment, [{ name: 'FOG_LOG', value: '1' }]);
    assert.equal(debugConfig.stopAtEntry, false, 'other debugConfig keys are preserved');

    // 3) 밖에서 고친 설정을 읽어 들인다
    await vscode.workspace.getConfiguration('cmake').update('debugConfig', { stopAtEntry: false, args: ['--fog_mode=A', '--gl', '--unknown-flag'] }, vscode.ConfigurationTarget.Workspace);
    await waitUntil(() => {
        const selection = controller.makeViewState(false).selection;
        return selection.globalVariable.fog_mode !== undefined && selection.globalVariable.fog_mode.bEnabled;
    }, 'external edit is imported');
    state = controller.makeViewState(false);
    assert.equal(state.selection.globalVariable.fog_density.bEnabled, false, 'a variable missing from the edited command line is off');
    assert.equal(state.selection.globalVariable.fog_density.value, '0.5', '... but keeps its value');
    assert.equal(state.selection.exclusiveChoice.gpu, 'USE_GL');
    assert.deepEqual(state.selection.listCustomArgument, [{ text: '--unknown-flag', bEnabled: true }]);

    // 4) 모두 끄면 cmake.debugConfig 에서 args 가 빠진다(다른 키만 남는다)
    await controller.disableAll();
    await controller.flush();
    await waitUntil(() => {
        const value = vscode.workspace.getConfiguration('cmake').inspect('debugConfig').workspaceValue;
        return value !== undefined && value.args === undefined;
    }, 'args removed after disableAll');
    assert.deepEqual(vscode.workspace.getConfiguration('cmake').inspect('debugConfig').workspaceValue, { stopAtEntry: false });

    // 5) 프리셋 저장 · 불러오기
    await controller.handleMessage({ type: 'setGlobalVariable', name: 'fog_mode', value: 'B' });
    await controller.handleMessage({ type: 'savePreset', name: 'fog' });
    await controller.disableAll();
    await controller.handleMessage({ type: 'loadPreset', name: 'fog' });
    await controller.flush();
    assert.deepEqual(vscode.workspace.getConfiguration('cmake').get('debugConfig').args, ['--fog_mode=B']);
    const stateFile = JSON.parse(fs.readFileSync(path.join(vscode.workspace.workspaceFolders[0].uri.fsPath, '.vscode', 'cmakeDebugArgsGui.json'), 'utf8'));
    assert.equal(stateFile.format_version, 1);
    assert.deepEqual(stateFile.preset.map((preset) => preset.name), ['fog']);

    // 6) 소스를 고치면 그 파일만 다시 읽어 카탈로그가 바뀐다
    const sourcePath = path.join(vscode.workspace.workspaceFolders[0].uri.fsPath, 'src', 'render', 'Fog.cc');
    fs.appendFileSync(sourcePath, '\nDEFINE_CVAR( fog_color, std::string, "white", "Fog color" );\n');
    await waitUntil(() => controller.makeViewState(true).catalog.listGlobalVariable.some((variable) => variable.name === 'fog_color'), 'catalog picks up a new definition');

    fs.writeFileSync(path.join(vscode.workspace.workspaceFolders[0].uri.fsPath, 'integration-result.txt'), 'PASS\n');
}

module.exports = {
    run,
};
