'use strict';

/**
 * @file SelectionStore.js
 * @brief 꺼 둔 항목의 값 · 사용자 인자 순서 · 프리셋을 `.vscode/cmakeDebugArgsGui.json` 에 둡니다.
 * @details CMake Tools 설정(`cmake.debugConfig.args`)에는 **켜진 것만** 들어가므로, 껐다 켤 때 되살릴 값과 프리셋은 여기 둡니다.
 *          `.vscode/*` 는 `.gitignore` 로 빠져 있어 PC 마다 따로입니다. JSON 키는 저장소 규칙대로 snake_case 입니다.
 *          읽지 못한 파일은 덮어쓰지 않고 `.bad-<시각>.json` 으로 옮겨 두고 알립니다(프리셋을 조용히 잃지 않게).
 *          vscode 를 모르는 순수 모듈입니다.
 */

const fs = require('fs');
const path = require('path');

const LaunchArgumentUtil = require('./LaunchArgumentUtil');

/** @brief 저장소 루트 기준 상태 파일 경로입니다. */
const kStateRelativePath = '.vscode/cmakeDebugArgsGui.json';
/** @brief 상태 파일 형식 판입니다. 형식을 바꾸면 올리고, 다른 판은 읽지 않습니다(옛 형식 리더를 두지 않는다). */
const kFormatVersion = 1;
/** @brief 프리셋 최대 수입니다. 넘으면 가장 오래된 것부터 지웁니다. */
const kMaxPresetCount = 50;

/** @brief 객체인지(배열 · null 제외) 묻습니다. */
function isPlainObjectInternal(value) {
    return value !== null && typeof value === 'object' && Array.isArray(value) === false;
}

/** @brief 선택을 JSON 객체(snake_case)로 바꿉니다. */
function makeSelectionJsonInternal(selection) {
    const globalVariable = {};
    for (const [name, state] of selection.mapGlobalVariable)
        globalVariable[name] = { enabled: state.bEnabled, value: state.value };
    const argument = {};
    for (const [name, state] of selection.mapArgument)
        argument[name] = { enabled: state.bEnabled, value: state.value, spelling: state.spelling === undefined ? '' : state.spelling };
    return {
        global_variable: globalVariable,
        argument,
        exclusive_choice: Object.fromEntries(selection.mapExclusiveChoice),
        custom_argument: selection.listCustomArgument.map((item) => ({ text: item.text, enabled: item.bEnabled })),
        environment: selection.listEnvironment.map((item) => ({ name: item.name, value: item.value, enabled: item.bEnabled })),
    };
}

/** @brief JSON 객체를 선택으로 읽습니다. 모양이 틀린 칸은 건너뜁니다. */
function readSelectionJsonInternal(json) {
    const selection = LaunchArgumentUtil.makeEmptySelection();
    if (isPlainObjectInternal(json) === false)
        return selection;
    if (isPlainObjectInternal(json.global_variable)) {
        for (const [name, state] of Object.entries(json.global_variable)) {
            if (isPlainObjectInternal(state))
                selection.mapGlobalVariable.set(name, { bEnabled: state.enabled === true, value: String(state.value === undefined ? '' : state.value) });
        }
    }
    if (isPlainObjectInternal(json.argument)) {
        for (const [name, state] of Object.entries(json.argument)) {
            if (isPlainObjectInternal(state))
                selection.mapArgument.set(name, { bEnabled: state.enabled === true, value: String(state.value === undefined ? '' : state.value), spelling: String(state.spelling === undefined ? '' : state.spelling) });
        }
    }
    if (isPlainObjectInternal(json.exclusive_choice)) {
        for (const [groupId, argumentName] of Object.entries(json.exclusive_choice)) {
            if (typeof argumentName === 'string' && argumentName !== '')
                selection.mapExclusiveChoice.set(groupId, argumentName);
        }
    }
    if (Array.isArray(json.custom_argument)) {
        for (const item of json.custom_argument) {
            if (isPlainObjectInternal(item) && typeof item.text === 'string')
                selection.listCustomArgument.push({ text: item.text, bEnabled: item.enabled === true });
        }
    }
    if (Array.isArray(json.environment)) {
        for (const item of json.environment) {
            if (isPlainObjectInternal(item) && typeof item.name === 'string')
                selection.listEnvironment.push({ name: item.name, value: String(item.value === undefined ? '' : item.value), bEnabled: item.enabled === true });
        }
    }
    return selection;
}

/**
 * @brief 상태 파일 하나를 읽고 씁니다. 프리셋 목록도 여기서 고칩니다.
 */
class SelectionStore {
    /** @param workspaceRoot 엔진 저장소 루트(절대 경로)입니다. */
    constructor(workspaceRoot) {
        /** @brief 상태 파일 절대 경로입니다. */
        this._filePath = path.join(workspaceRoot, ...kStateRelativePath.split('/'));
        /** @brief 마지막으로 읽거나 쓴 선택입니다. */
        this._selection = LaunchArgumentUtil.makeEmptySelection();
        /** @brief 프리셋 목록입니다: `[{ name, createdAt, selection }]`(만든 순서). */
        this._listPreset = [];
    }

    /**
     * @brief 파일을 읽습니다. 없으면 빈 상태입니다.
     * @return 알릴 문제 글(빈 글이면 문제 없음)입니다. 읽지 못한 파일은 옆으로 옮겨 두고 그 경로를 알립니다.
     */
    async load() {
        let text;
        try {
            text = await fs.promises.readFile(this._filePath, 'utf8');
        } catch (error) {
            if (error.code === 'ENOENT')
                return '';
            return `cannot read ${this._filePath}: ${error.message}`;
        }

        let json = null;
        let problem = '';
        try {
            json = JSON.parse(text);
        } catch (error) {
            problem = error.message;
        }
        if (problem === '' && (isPlainObjectInternal(json) === false || json.format_version !== kFormatVersion))
            problem = `format_version is not ${kFormatVersion}`;
        if (problem !== '') {
            const backupPath = this._filePath.replace(/\.json$/, `.bad-${Date.now()}.json`);
            await fs.promises.rename(this._filePath, backupPath).catch(() => {});
            return `${kStateRelativePath} was unreadable (${problem}) — moved to ${path.basename(backupPath)} and started empty`;
        }

        this._selection = readSelectionJsonInternal(json.selection);
        this._listPreset = [];
        if (Array.isArray(json.preset)) {
            for (const item of json.preset) {
                if (isPlainObjectInternal(item) && typeof item.name === 'string' && item.name.trim() !== '')
                    this._listPreset.push({ name: item.name, createdAt: String(item.created_at === undefined ? '' : item.created_at), selection: readSelectionJsonInternal(item.selection) });
            }
        }
        return '';
    }

    /** @brief 지금 선택을 파일에 씁니다(임시 파일에 쓰고 바꿔 넣는다). */
    async save(selection) {
        this._selection = LaunchArgumentUtil.cloneSelection(selection);
        const json = {
            format_version: kFormatVersion,
            selection: makeSelectionJsonInternal(this._selection),
            preset: this._listPreset.map((preset) => ({ name: preset.name, created_at: preset.createdAt, selection: makeSelectionJsonInternal(preset.selection) })),
        };
        await fs.promises.mkdir(path.dirname(this._filePath), { recursive: true });
        const temporaryPath = `${this._filePath}.tmp`;
        await fs.promises.writeFile(temporaryPath, `${JSON.stringify(json, null, 4)}\n`, 'utf8');
        await fs.promises.rename(temporaryPath, this._filePath);
    }

    /** @brief 상태 파일을 지우고 빈 상태로 돌아갑니다. */
    async deleteFile() {
        await fs.promises.rm(this._filePath, { force: true });
        this._selection = LaunchArgumentUtil.makeEmptySelection();
        this._listPreset = [];
    }

    /** @brief 마지막으로 읽거나 쓴 선택의 사본입니다. */
    getSelection() {
        return LaunchArgumentUtil.cloneSelection(this._selection);
    }

    /** @brief 프리셋 목록(이름 · 만든 시각 · 사본)입니다. */
    getPresetList() {
        return this._listPreset.map((preset) => ({ name: preset.name, createdAt: preset.createdAt, selection: LaunchArgumentUtil.cloneSelection(preset.selection) }));
    }

    /** @brief 이름으로 프리셋을 찾습니다. 없으면 null 입니다. */
    findPreset(name) {
        const preset = this._listPreset.find((item) => item.name === name);
        return preset === undefined ? null : LaunchArgumentUtil.cloneSelection(preset.selection);
    }

    /** @brief 프리셋을 더하거나 같은 이름을 덮어씁니다. 파일에는 다음 `save` 가 씁니다. */
    putPreset(name, selection) {
        const trimmed = name.trim();
        const preset = { name: trimmed, createdAt: new Date().toISOString(), selection: LaunchArgumentUtil.cloneSelection(selection) };
        const existingIndex = this._listPreset.findIndex((item) => item.name === trimmed);
        if (0 <= existingIndex)
            this._listPreset[existingIndex] = preset;
        else
            this._listPreset.push(preset);
        while (kMaxPresetCount < this._listPreset.length)
            this._listPreset.shift();
    }

    /** @brief 프리셋을 지웁니다. @return 지웠으면 true 입니다. */
    removePreset(name) {
        const beforeCount = this._listPreset.length;
        this._listPreset = this._listPreset.filter((item) => item.name !== name);
        return this._listPreset.length !== beforeCount;
    }
}

module.exports = {
    kStateRelativePath,
    SelectionStore,
};
