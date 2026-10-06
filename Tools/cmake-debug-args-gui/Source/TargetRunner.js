'use strict';

/**
 * @file TargetRunner.js
 * @brief CMake Tools 의 실행 대상을 고른 인자로 실행 · 디버그합니다.
 * @details - 디버그: `cmake.debugTarget` 을 그대로 부릅니다. CMake Tools 가 `cmake.debugConfig` 를 읽으므로, 부르기 전에 설정 쓰기가
 *            끝나 있어야 합니다(컨트롤러가 `flush` 를 먼저 기다린다). 인자는 디버거에 배열로 가서 셸을 거치지 않습니다.
 *          - 실행: CMake Tools 의 ▷ 는 인자를 따옴표 없이 터미널에 보내 PowerShell 5.1 이 `-gv_x=a.b` 를 둘로 쪼갭니다.
 *            그래서 대상 경로만 CMake Tools 에서 받고(`cmake.launchTargetPath` — `buildBeforeRun` 이면 먼저 빌드한다),
 *            셸 없는 `ProcessExecution` 작업으로 띄웁니다. 작업 폴더 · 환경은 CMake Tools 실행과 같은 규칙입니다
 *            (`debugConfig.cwd`, 없으면 실행 파일 폴더 — 엔진은 거기서 위로 올라가며 `Resource/` 를 찾는다).
 */

const path = require('path');
const vscode = require('vscode');

const kTaskType = 'cmakeDebugArgs';
const kTaskSource = 'CMake Debug Args';

/**
 * @brief 실행 · 디버그를 맡습니다.
 */
class TargetRunner {
    /**
     * @param workspaceFolder 엔진 저장소 작업 폴더입니다.
     * @param logger `LogOutputChannel` 입니다.
     */
    constructor(workspaceFolder, logger) {
        /** @brief 엔진 저장소 작업 폴더입니다. */
        this._workspaceFolder = workspaceFolder;
        /** @brief 출력 채널입니다. */
        this._logger = logger;
    }

    /** @brief CMake Tools 디버그를 시작합니다(`cmake.debugConfig` 를 그쪽이 읽는다). */
    async debug() {
        this._logger.info('cmake.debugTarget');
        await vscode.commands.executeCommand('cmake.debugTarget');
    }

    /**
     * @brief 실행 대상을 셸 없이 띄웁니다.
     * @return 띄웠으면 true, CMake Tools 가 대상을 주지 않았으면(취소 · 빌드 실패) false 입니다.
     */
    async run(listArgument, listEnvironment) {
        const targetPath = await vscode.commands.executeCommand('cmake.launchTargetPath');
        if (typeof targetPath !== 'string' || targetPath === '') {
            this._logger.warn('cmake.launchTargetPath returned nothing (no launch target, cancelled or build failed)');
            return false;
        }
        const debugConfig = vscode.workspace.getConfiguration('cmake', this._workspaceFolder.uri).get('debugConfig');
        const configuredCwd = debugConfig !== null && typeof debugConfig === 'object' && typeof debugConfig.cwd === 'string' ? debugConfig.cwd : '';
        const cwd = configuredCwd === '' ? path.dirname(targetPath) : this._expandVariable(configuredCwd);
        const environment = {};
        for (const item of listEnvironment)
            environment[item.name] = this._expandVariable(item.value);
        const listExpandedArgument = listArgument.map((text) => this._expandVariable(text));

        const targetName = path.parse(targetPath).name;
        const execution = new vscode.ProcessExecution(targetPath, listExpandedArgument, { cwd, env: environment });
        const task = new vscode.Task({ type: kTaskType, target: targetName }, this._workspaceFolder, `Run ${targetName}`, kTaskSource, execution, []);
        task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: true, focus: false };
        this._logger.info(`run ${targetPath} ${listExpandedArgument.join(' ')} (cwd ${cwd})`);
        await vscode.tasks.executeTask(task);
        return true;
    }

    /** @brief `${workspaceFolder}` · `${workspaceRoot}` · `${env:NAME}` 를 풉니다(CMake Tools 실행이 푸는 흔한 것만). */
    _expandVariable(text) {
        const workspacePath = this._workspaceFolder.uri.fsPath;
        return String(text)
            .replace(/\$\{workspaceFolder\}|\$\{workspaceRoot\}/g, workspacePath)
            .replace(/\$\{env:([^}]+)\}/g, (match, name) => (process.env[name] === undefined ? '' : process.env[name]));
    }
}

module.exports = {
    TargetRunner,
};
