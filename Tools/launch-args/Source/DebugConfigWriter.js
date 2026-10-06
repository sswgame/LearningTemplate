'use strict';

/**
 * @file DebugConfigWriter.js
 * @brief CMake Tools 가 실행 · 디버그 때 읽는 `cmake.debugConfig` 의 `args` · `environment` 를 읽고 씁니다.
 * @details CMake Tools(1.24)는 `cmake.debugTarget` 에서 이 객체를 디버그 구성에 그대로 덮고(`Object.assign`),
 *          `cmake.launchTarget` 에서 `args` 를 터미널에 붙이며 `environment` 를 실행 환경에 더합니다.
 *          쓰기는 VS Code 설정 API 로만 합니다 — `.vscode/settings.json` 의 주석 · 다른 키 · 서식을 그대로 둡니다
 *          (파일을 `JSON.stringify` 로 통째로 다시 쓰면 주석이 사라진다). `debugConfig` 의 다른 칸(`cwd` · `type` …)은 건드리지 않고,
 *          넘길 것이 없으면 `args` · `environment` 칸을 지우며, 그래서 객체가 비면 `cmake.debugConfig` 키 자체를 지웁니다.
 */

const vscode = require('vscode');

const kSection = 'cmake';
const kKey = 'debugConfig';

/** @brief 값이 객체(배열 · null 제외)인지 묻습니다. */
function isPlainObjectInternal(value) {
    return value !== null && typeof value === 'object' && Array.isArray(value) === false;
}

/**
 * @brief 엔진 저장소 작업 폴더의 `cmake.debugConfig` 하나를 맡습니다.
 */
class DebugConfigWriter {
    /** @param workspaceFolder 엔진 저장소 작업 폴더입니다. */
    constructor(workspaceFolder) {
        /** @brief 엔진 저장소 작업 폴더입니다. */
        this._workspaceFolder = workspaceFolder;
    }

    /** @brief 이 설정이 바뀌었는지를 설정 변경 이벤트로 묻습니다. */
    isAffectedBy(configurationChangeEvent) {
        return configurationChangeEvent.affectsConfiguration(`${kSection}.${kKey}`, this._workspaceFolder.uri);
    }

    /**
     * @brief CMake Tools 가 실제로 쓸 값(사용자 · 작업 영역 · 폴더 설정을 합친 값)을 읽습니다.
     * @return `{ listArgument, listEnvironment }` — 모양이 틀린 칸은 빈 목록입니다.
     */
    read() {
        const debugConfig = vscode.workspace.getConfiguration(kSection, this._workspaceFolder.uri).get(kKey);
        const listArgument = [];
        const listEnvironment = [];
        if (isPlainObjectInternal(debugConfig) && Array.isArray(debugConfig.args)) {
            for (const text of debugConfig.args)
                listArgument.push(String(text));
        }
        if (isPlainObjectInternal(debugConfig) && Array.isArray(debugConfig.environment)) {
            for (const item of debugConfig.environment) {
                if (isPlainObjectInternal(item) && typeof item.name === 'string')
                    listEnvironment.push({ name: item.name, value: item.value === undefined ? '' : String(item.value) });
            }
        }
        return { listArgument, listEnvironment };
    }

    /**
     * @brief `args` · `environment` 를 작업 영역 설정(단일 폴더면 `.vscode/settings.json`)에 씁니다.
     * @details 다중 루트 작업 영역이면 그 폴더의 설정에 씁니다(`cmake.debugConfig` 는 resource 범위다).
     *          CMake Tools 가 설치되지 않아 설정이 등록되지 않았으면 VS Code 가 거절하고, 그 오류를 그대로 던집니다.
     */
    async write(listArgument, listEnvironment) {
        const configuration = vscode.workspace.getConfiguration(kSection, this._workspaceFolder.uri);
        const bSingleFolder = vscode.workspace.workspaceFile === undefined;
        const target = bSingleFolder ? vscode.ConfigurationTarget.Workspace : vscode.ConfigurationTarget.WorkspaceFolder;
        const inspected = configuration.inspect(kKey);
        const currentValue = inspected === undefined ? undefined : (bSingleFolder ? inspected.workspaceValue : inspected.workspaceFolderValue);
        const nextValue = isPlainObjectInternal(currentValue) ? { ...currentValue } : {};

        if (listArgument.length > 0)
            nextValue.args = listArgument.slice();
        else
            delete nextValue.args;
        if (listEnvironment.length > 0)
            nextValue.environment = listEnvironment.map((item) => ({ name: item.name, value: item.value }));
        else
            delete nextValue.environment;

        const bEmpty = Object.keys(nextValue).length === 0;
        const bUnchanged = JSON.stringify(bEmpty ? undefined : nextValue) === JSON.stringify(currentValue);
        if (bUnchanged)
            return;
        await configuration.update(kKey, bEmpty ? undefined : nextValue, target);
    }
}

module.exports = {
    DebugConfigWriter,
};
