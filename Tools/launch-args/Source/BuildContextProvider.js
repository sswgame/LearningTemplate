'use strict';

/**
 * @file BuildContextProvider.js
 * @brief CMake Tools 가 지금 고른 빌드(구성 프리셋 · 빌드 폴더)에서 프로필이 묻는 CMake 캐시 변수 값을 읽습니다.
 * @details 같은 소스라도 빌드마다 쓸 수 있는 인자가 다릅니다(배포 빌드에서 빠지는 변수 · 하나만 빌드되는 게임 폴더 …).
 *          어떤 변수를 읽고 어떻게 판정할지는 프로필(`module.variant_folders` · `rules` · `status`)이 정하고, 여기서는 값만 읽습니다.
 *          값은 빌드 폴더의 `CMakeCache.txt`(실제로 구성된 값)를 먼저 보고, 아직 구성 전이면 프리셋의 `cacheVariables` 를 봅니다.
 *          CMake Tools API(`getApi`)로 프리셋 변경 · 구성 완료 · 실행 대상 변경을 듣습니다. CMake Tools 가 없으면 문맥을 모르는 채로 둡니다
 *          (그때 목록은 걸러지지 않고 모두 보입니다).
 */

const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

/** @brief CMake Tools 확장 id 입니다. */
const kCmakeToolsExtensionId = 'ms-vscode.cmake-tools';
/** @brief 요청하는 CMake Tools API 판입니다(1.24 기준 최신 v5 — 낮은 판도 같은 객체를 돌려준다). */
const kCmakeToolsApiVersion = 5;

/** @brief 모르는 문맥입니다. */
function makeUnknownContextInternal() {
    return {
        bKnown: false,
        bCmakeToolsAvailable: false,
        presetName: '',
        buildDirectory: '',
        buildType: '',
        launchTargetName: '',
        mapCacheValue: {},
    };
}

/** @brief `CMakeCache.txt` 글에서 `NAME:TYPE=VALUE` 줄을 맵으로 읽습니다. */
function parseCmakeCacheInternal(text) {
    const mapEntry = new Map();
    for (const line of text.split(/\r?\n/)) {
        const match = /^([A-Za-z_][\w.+-]*):[A-Z]+=(.*)$/.exec(line);
        if (match !== null)
            mapEntry.set(match[1], match[2]);
    }
    return mapEntry;
}

/** @brief 프리셋 `cacheVariables` 칸 하나(글 · bool · `{ type, value }`)를 글로 읽습니다. 없으면 null 입니다. */
function readPresetCacheValueInternal(cacheVariables, name) {
    if (cacheVariables === null || typeof cacheVariables !== 'object' || (name in cacheVariables) === false)
        return null;
    const value = cacheVariables[name];
    if (value !== null && typeof value === 'object')
        return value.value === undefined ? null : String(value.value);
    return String(value);
}

/**
 * @brief CMake Tools 프로젝트 하나를 따라가며 빌드 문맥을 알려 줍니다.
 */
class BuildContextProvider {
    /**
     * @param workspaceFolder 엔진 저장소 작업 폴더입니다.
     * @param logger `LogOutputChannel` 입니다.
     */
    constructor(workspaceFolder, logger) {
        /** @brief 엔진 저장소 작업 폴더입니다. */
        this._workspaceFolder = workspaceFolder;
        /** @brief 출력 채널입니다. */
        this._logger = logger;
        /** @brief 읽어 올 CMake 캐시 변수 이름입니다(프로필이 정한다). */
        this._listCacheVariableName = [];
        /** @brief CMake Tools API 객체입니다. 없으면 null 입니다. */
        this._api = null;
        /** @brief 이 폴더의 CMake Tools 프로젝트입니다. 없으면 null 입니다. */
        this._project = null;
        /** @brief 마지막으로 알린 문맥입니다. */
        this._context = makeUnknownContextInternal();
        /** @brief API 가 마지막으로 알려 준 실행 대상 이름입니다(처음에는 모른다). */
        this._launchTargetName = '';
        /** @brief API 수준 구독입니다. */
        this._listDisposable = [];
        /** @brief 프로젝트 수준 구독입니다. 프로젝트가 바뀌면 갈아 끼웁니다. */
        this._listProjectDisposable = [];
        /** @brief 문맥이 바뀌면 알리는 이벤트입니다. */
        this._changeEmitter = new vscode.EventEmitter();
        /** @brief 문맥이 바뀐 뒤 부르는 이벤트입니다. */
        this.onDidChange = this._changeEmitter.event;
    }

    /** @brief CMake Tools 를 찾아 구독하고 첫 문맥을 읽습니다. CMake Tools 가 없거나 실패하면 모르는 문맥으로 둡니다. */
    async initialize() {
        const extension = vscode.extensions.getExtension(kCmakeToolsExtensionId);
        if (extension === undefined) {
            this._logger.warn(`${kCmakeToolsExtensionId} is not installed - build context (cache variables) is unknown`);
            return;
        }
        let extensionExports;
        try {
            extensionExports = extension.isActive ? extension.exports : await extension.activate();
        } catch (error) {
            this._logger.error(`failed to activate ${kCmakeToolsExtensionId}: ${error.message}`);
            return;
        }
        if (extensionExports === undefined || extensionExports === null || typeof extensionExports.getApi !== 'function') {
            this._logger.warn(`${kCmakeToolsExtensionId} does not expose getApi()`);
            return;
        }
        this._api = extensionExports.getApi(kCmakeToolsApiVersion);
        this._listDisposable.push(this._api.onActiveProjectChanged(() => this._bindProject()));
        this._listDisposable.push(this._api.onLaunchTargetChanged((targetName) => {
            this._launchTargetName = typeof targetName === 'string' ? targetName : '';
            void this.refresh();
        }));
        await this._bindProject();
    }

    /** @brief 마지막으로 읽은 문맥의 사본입니다. */
    getContext() {
        return { ...this._context, mapCacheValue: { ...this._context.mapCacheValue } };
    }

    /** @brief 읽어 올 캐시 변수 이름을 바꿉니다(프로필이 바뀌었을 때). 다음 `refresh` 부터 씁니다. */
    setCacheVariableNameList(listName) {
        this._listCacheVariableName = listName.slice();
    }

    /** @brief 문맥을 다시 읽고, 바뀌었으면 `onDidChange` 를 부릅니다. */
    async refresh() {
        const context = await this._readContext();
        if (JSON.stringify(context) === JSON.stringify(this._context))
            return;
        this._context = context;
        this._logger.info(`build context: preset=${context.presetName || '-'} cache=${JSON.stringify(context.mapCacheValue)} target=${context.launchTargetName || '-'}`);
        this._changeEmitter.fire(this.getContext());
    }

    /** @brief 구독을 모두 풉니다. */
    dispose() {
        for (const disposable of this._listProjectDisposable.concat(this._listDisposable))
            disposable.dispose();
        this._listProjectDisposable = [];
        this._listDisposable = [];
        this._changeEmitter.dispose();
    }

    /** @brief 이 폴더의 프로젝트를 다시 잡고 그 이벤트를 구독합니다. */
    async _bindProject() {
        for (const disposable of this._listProjectDisposable)
            disposable.dispose();
        this._listProjectDisposable = [];
        try {
            const project = await this._api.getProject(this._workspaceFolder.uri);
            this._project = project === undefined ? null : project;
        } catch (error) {
            this._logger.error(`CMake Tools getProject failed: ${error.message}`);
            this._project = null;
        }
        if (this._project !== null) {
            const refreshLater = () => void this.refresh();
            this._listProjectDisposable.push(this._project.onSelectedConfigurationChanged(refreshLater));
            if (typeof this._project.onConfigureResult === 'function')
                this._listProjectDisposable.push(this._project.onConfigureResult(refreshLater));
            if (typeof this._project.onCodeModelChanged === 'function')
                this._listProjectDisposable.push(this._project.onCodeModelChanged(refreshLater));
        }
        await this.refresh();
    }

    /** @brief 프로젝트 · 빌드 폴더 · 캐시에서 문맥을 읽습니다. */
    async _readContext() {
        const context = makeUnknownContextInternal();
        context.bCmakeToolsAvailable = this._api !== null;
        context.launchTargetName = this._launchTargetName;
        if (this._project === null)
            return context;

        const preset = this._project.configurePreset;
        const cacheVariables = preset !== undefined && preset !== null ? preset.cacheVariables : null;
        context.presetName = preset !== undefined && preset !== null && typeof preset.name === 'string' ? preset.name : '';
        try {
            const buildDirectory = await this._project.getBuildDirectory();
            context.buildDirectory = typeof buildDirectory === 'string' ? buildDirectory : '';
        } catch (error) {
            context.buildDirectory = '';
        }

        let mapCache = null;
        if (context.buildDirectory !== '') {
            try {
                mapCache = parseCmakeCacheInternal(await fs.promises.readFile(path.join(context.buildDirectory, 'CMakeCache.txt'), 'utf8'));
            } catch (error) {
                mapCache = null;
            }
        }
        const readValue = (name) => {
            if (mapCache !== null && mapCache.has(name))
                return mapCache.get(name);
            return readPresetCacheValueInternal(cacheVariables, name);
        };
        const buildTypeText = readValue('CMAKE_BUILD_TYPE');
        context.buildType = buildTypeText === null ? '' : buildTypeText;
        let bAnyValue = false;
        for (const name of this._listCacheVariableName) {
            const value = readValue(name);
            if (value === null)
                continue;
            context.mapCacheValue[name] = value;
            bAnyValue = true;
        }
        context.bKnown = mapCache !== null || bAnyValue;
        return context;
    }
}

module.exports = {
    kCmakeToolsExtensionId,
    BuildContextProvider,
};
