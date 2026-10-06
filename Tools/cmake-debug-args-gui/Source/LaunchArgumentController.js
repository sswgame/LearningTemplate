'use strict';

/**
 * @file LaunchArgumentController.js
 * @brief 카탈로그 · 선택 · CMake Tools 설정 · 상태 파일을 한자리에서 맞춥니다. 웹뷰는 의도만 보내고 화면 상태만 받습니다.
 * @details 흐름은 한 방향입니다.
 *          1. 웹뷰가 의도(`setGlobalVariable` …)를 보내면 선택을 고치고 바로 화면 상태를 다시 보냅니다(미리보기는 즉시).
 *          2. 디스크 쓰기는 `kWriteDelayMs` 동안 모았다가 한 번 합니다: `cmake.debugConfig`(켜진 것) · 상태 파일(꺼진 값 · 프리셋).
 *          3. 설정이 밖에서 바뀌면(손으로 고침 · git pull) 그 명령줄을 정본으로 읽어 들입니다. 자기가 쓴 메아리는 서명으로 거릅니다.
 *          4. 소스가 바뀌면 그 파일만 다시 읽어 카탈로그를 고칩니다. 무엇을 읽을지는 설정 `cmakeDebugArgs.catalog` 의 프로필이 정하고,
 *             프로필이 바뀌면 처음부터 다시 읽습니다(감시도 프로필의 glob 으로 다시 건다).
 */

const vscode = require('vscode');

const CatalogProfile = require('./CatalogProfile');
const { CatalogScanner, ValueKind, kGeneralGroup } = require('./CatalogScanner');
const LaunchArgumentUtil = require('./LaunchArgumentUtil');
const { SelectionStore } = require('./SelectionStore');
const { BuildContextProvider, kCmakeToolsExtensionId } = require('./BuildContextProvider');
const { DebugConfigWriter } = require('./DebugConfigWriter');
const { TargetRunner } = require('./TargetRunner');

/** @brief 마지막 변경 뒤 디스크에 쓰기까지 기다리는 시간입니다. 글을 치는 동안 설정 파일을 매 글자 다시 쓰지 않게 합니다. */
const kWriteDelayMs = 250;
/** @brief 소스 변경을 모았다가 다시 읽기까지 기다리는 시간입니다(저장 한 번에 여러 이벤트가 온다). */
const kRescanDelayMs = 400;
const kDeleteActionLabel = '지우기';
/** @brief 프로필 설정 이름입니다(package.json `contributes.configuration`). */
const kProfileSection = 'cmakeDebugArgs';
const kProfileKey = 'catalog';

/** @brief 설정에 쓰는 두 칸의 서명입니다. 같으면 같은 내용입니다. */
function makeSignatureInternal(listArgument, listEnvironment) {
    return JSON.stringify({ argument: listArgument, environment: listEnvironment });
}

/** @brief 맵을 웹뷰로 보낼 수 있는 객체로 바꿉니다. */
function makeObjectFromMapInternal(map) {
    const result = {};
    for (const [key, value] of map)
        result[key] = { ...value };
    return result;
}

/**
 * @brief 확장 하나에 하나입니다. 웹뷰 공급자와 명령이 이것을 부릅니다.
 */
class LaunchArgumentController {
    /**
     * @param workspaceFolder 엔진 저장소 작업 폴더입니다.
     * @param logger `LogOutputChannel` 입니다.
     */
    constructor(workspaceFolder, logger) {
        /** @brief 엔진 저장소 작업 폴더입니다. */
        this._workspaceFolder = workspaceFolder;
        /** @brief 출력 채널입니다. */
        this._logger = logger;
        /** @brief 지금 프로필입니다. 없으면 빈 프로필(카탈로그 없음)입니다. */
        this._profile = CatalogProfile.makeEmptyProfile();
        /** @brief 프로필 출처(`settings` · 파일 경로 · 빈 글)입니다. */
        this._profileSource = '';
        /** @brief 프로필을 읽다 생긴 문제입니다. */
        this._listProfileProblem = [];
        /** @brief 소스 → 카탈로그입니다. 프로필이 바뀌면 새로 만듭니다. */
        this._scanner = new CatalogScanner(workspaceFolder.uri.fsPath, this._profile);
        /** @brief 프로필 glob 으로 건 파일 감시입니다. */
        this._listWatcherDisposable = [];
        /** @brief 상태 파일입니다. */
        this._store = new SelectionStore(workspaceFolder.uri.fsPath);
        /** @brief `cmake.debugConfig` 입니다. */
        this._writer = new DebugConfigWriter(workspaceFolder);
        /** @brief CMake Tools 빌드 문맥입니다. */
        this._buildContext = new BuildContextProvider(workspaceFolder, logger);
        /** @brief 실행 · 디버그입니다. */
        this._runner = new TargetRunner(workspaceFolder, logger);
        /** @brief 지금 카탈로그입니다. */
        this._catalog = { listGlobalVariable: [], listArgument: [], listExclusiveGroup: [], listProblem: [] };
        /** @brief 카탈로그가 바뀔 때마다 오르는 번호입니다. 웹뷰가 행을 다시 만들지 정합니다. */
        this._catalogVersion = 0;
        /** @brief 지금 선택입니다. */
        this._selection = LaunchArgumentUtil.makeEmptySelection();
        /** @brief 진행 중인 첫 초기화입니다. 시작 전이면 null 입니다. */
        this._initialization = null;
        /** @brief 첫 스캔 · 설정 읽기가 끝났는지입니다. */
        this._bInitialized = false;
        /** @brief 상태 파일을 읽다 생긴 문제입니다(빈 글이면 없음). */
        this._storeProblem = '';
        /** @brief 마지막 설정 쓰기 실패입니다(빈 글이면 없음). */
        this._writeError = '';
        /** @brief 설정에 마지막으로 쓴(또는 읽어 맞춘) 내용의 서명입니다. 자기 쓰기의 메아리를 거릅니다. */
        this._lastWrittenSignature = '';
        /** @brief 미뤄 둔 디스크 쓰기 타이머입니다. */
        this._writeTimer = null;
        /** @brief 쓰기를 차례로 세우는 약속 사슬입니다(겹쳐 쓰지 않게). */
        this._writeChain = Promise.resolve();
        /** @brief 다시 읽을 소스 경로입니다. */
        this._uniquePendingRescanPath = new Set();
        /** @brief 미뤄 둔 다시 읽기 타이머입니다. */
        this._rescanTimer = null;
        /** @brief 구독입니다. */
        this._listDisposable = [];
        /** @brief 화면 상태가 바뀌면 알리는 이벤트입니다. */
        this._viewEmitter = new vscode.EventEmitter();
        /** @brief 화면 상태가 바뀐 뒤 부르는 이벤트입니다. */
        this.onDidChangeView = this._viewEmitter.event;
        /** @brief 웹뷰 의도 이름 → 처리 함수입니다. */
        this._mapMessageHandler = new Map([
            ['setGlobalVariable', this._setGlobalVariable],
            ['resetGlobalVariable', this._resetGlobalVariable],
            ['removeGlobalVariable', this._removeGlobalVariable],
            ['setArgument', this._setArgument],
            ['resetArgument', this._resetArgument],
            ['removeArgument', this._removeArgument],
            ['setExclusiveChoice', this._setExclusiveChoice],
            ['addCustomArgument', this._addCustomArgument],
            ['setCustomArgument', this._setCustomArgument],
            ['removeCustomArgument', this._removeCustomArgument],
            ['addEnvironment', this._addEnvironment],
            ['setEnvironment', this._setEnvironment],
            ['removeEnvironment', this._removeEnvironment],
            ['savePreset', this._savePreset],
            ['loadPreset', this._loadPreset],
            ['deletePreset', this._deletePreset],
            ['disableAll', this.disableAll],
            ['run', this.run],
            ['debug', this.debug],
            ['copyCommandLine', this.copyCommandLine],
            ['refresh', this.refresh],
            ['openLocation', this._openLocation],
        ]);
    }

    /** @brief 상태 파일 → 소스 스캔 → 설정 읽기 순으로 첫 상태를 만듭니다. CMake Tools 문맥은 기다리지 않고 뒤따라 옵니다. */
    initialize() {
        this._initialization = this._initializeNow();
        return this._initialization;
    }

    /** @brief `initialize` 의 본체입니다. */
    async _initializeNow() {
        this._storeProblem = await this._store.load();
        if (this._storeProblem !== '')
            this._logger.warn(this._storeProblem);
        this._selection = this._store.getSelection();

        this._loadProfile();
        await this._scanCatalog();
        this._syncFromSettings();
        this._listDisposable.push(vscode.workspace.onDidChangeConfiguration((event) => this._onConfigurationChanged(event)));
        this._listDisposable.push(this._buildContext.onDidChange(() => this._notifyView()));
        this._bInitialized = true;
        this._notifyView();
        await this._buildContext.initialize();
    }

    /** @brief 타이머 · 구독을 풉니다. 미뤄 둔 쓰기는 바로 시작합니다(끝까지 기다리지는 못한다). */
    dispose() {
        if (this._writeTimer !== null)
            void this.flush();
        clearTimeout(this._rescanTimer);
        for (const disposable of this._listDisposable.concat(this._listWatcherDisposable))
            disposable.dispose();
        this._listDisposable = [];
        this._listWatcherDisposable = [];
        this._buildContext.dispose();
        this._viewEmitter.dispose();
    }

    /**
     * @brief 웹뷰가 보낸 의도 하나를 처리합니다. 모르는 의도는 출력 창에 남기고 버립니다.
     */
    async handleMessage(message) {
        if (message === null || typeof message !== 'object' || typeof message.type !== 'string')
            return;
        const handler = this._mapMessageHandler.get(message.type);
        if (handler === undefined) {
            this._logger.warn(`unknown view message: ${message.type}`);
            return;
        }
        if (this._bInitialized === false) {
            this._logger.warn(`view message before initialization is ignored: ${message.type}`);
            return;
        }
        try {
            await handler.call(this, message);
        } catch (error) {
            this._logger.error(`${message.type} failed: ${error.stack || error.message}`);
            void vscode.window.showErrorMessage(`CMake Debug Args: ${message.type} 실패 — ${error.message}`);
        }
    }

    /** @brief 미뤄 둔 쓰기를 지금 합니다: `cmake.debugConfig`(바뀌었으면) · 상태 파일. 실행 · 디버그 전에 기다립니다. */
    flush() {
        clearTimeout(this._writeTimer);
        this._writeTimer = null;
        const write = () => this._writeNow();
        this._writeChain = this._writeChain.then(write, write);
        return this._writeChain;
    }

    /** @brief 소스를 처음부터 다시 읽고 빌드 문맥을 다시 묻습니다. */
    async refresh() {
        this._loadProfile();
        await this._scanCatalog();
        if (LaunchArgumentUtil.adoptKnownCustomArguments(this._selection, this._catalog, this._makeOptions()))
            this._scheduleFlush();
        this._notifyView();
        await this._buildContext.refresh();
    }

    /** @brief 고른 인자로 실행 대상을 셸 없이 띄웁니다. */
    async run() {
        await this.flush();
        const composed = this._composeCommandLine();
        await this._runner.run(composed.listArgument, LaunchArgumentUtil.composeEnvironment(this._selection));
    }

    /** @brief 설정 쓰기를 끝낸 뒤 CMake Tools 디버그를 시작합니다. */
    async debug() {
        await this.flush();
        await this._runner.debug();
    }

    /**
     * @brief 디버그 구성에 넣을 지금 선택의 인자 · 환경 변수입니다(`DebugConfigurationInjector` 가 F5 순간에 부른다).
     * @details 아직 첫 스캔 전이면 끝날 때까지 기다립니다 — 비어 있는 채로 넣으면 인자 없이 뜬다.
     */
    async makeLaunchPayload() {
        if (this._bInitialized === false && this._initialization !== null)
            await this._initialization.catch(() => {});
        return {
            listArgument: this._composeCommandLine().listArgument,
            listEnvironment: LaunchArgumentUtil.composeEnvironment(this._selection),
        };
    }

    /** @brief 명령줄을 셸에 붙여 넣을 수 있는 한 줄로 클립보드에 넣습니다. */
    async copyCommandLine() {
        const shellText = LaunchArgumentUtil.makeShellCommandLine(this._composeCommandLine().listArgument);
        await vscode.env.clipboard.writeText(shellText);
        vscode.window.setStatusBarMessage(`$(copy) 명령줄을 복사했습니다: ${shellText === '' ? '(비어 있음)' : shellText}`, 4000);
    }

    /** @brief 모든 항목을 끕니다. 값은 기억합니다. */
    async disableAll() {
        for (const state of this._selection.mapGlobalVariable.values())
            state.bEnabled = false;
        for (const state of this._selection.mapArgument.values())
            state.bEnabled = false;
        this._selection.mapExclusiveChoice.clear();
        for (const item of this._selection.listCustomArgument)
            item.bEnabled = false;
        for (const item of this._selection.listEnvironment)
            item.bEnabled = false;
        this._commitChange();
    }

    /** @brief 상태 파일(꺼진 값 · 프리셋)을 지웁니다. 지금 켜진 것은 설정에 남아 있으므로 다시 읽어 들입니다. */
    async resetState() {
        const answer = await vscode.window.showWarningMessage('꺼 둔 값과 프리셋을 모두 지울까요? 지금 켜진 인자는 그대로 남습니다.', { modal: true }, kDeleteActionLabel);
        if (answer !== kDeleteActionLabel)
            return;
        await this._store.deleteFile();
        this._storeProblem = '';
        const setting = this._writer.read();
        this._selection = LaunchArgumentUtil.importCommandLine(setting.listArgument, setting.listEnvironment, this._catalog, LaunchArgumentUtil.makeEmptySelection(), this._makeOptions());
        this._lastWrittenSignature = makeSignatureInternal(setting.listArgument, setting.listEnvironment);
        this._commitChange();
    }

    /** @brief 소스 파일 변경을 받아 둡니다. 모아서 `kRescanDelayMs` 뒤에 그 파일들만 다시 읽습니다. */
    queueRescan(uri) {
        this._uniquePendingRescanPath.add(uri.fsPath);
        clearTimeout(this._rescanTimer);
        this._rescanTimer = setTimeout(() => void this._rescanPending(), kRescanDelayMs);
    }

    /**
     * @brief 웹뷰에 보낼 화면 상태를 만듭니다.
     * @param bIncludeCatalog 카탈로그를 함께 실을지입니다(웹뷰가 행을 다시 만들어야 할 때만).
     */
    makeViewState(bIncludeCatalog) {
        const context = this._buildContext.getContext();
        const composed = this._composeCommandLine();
        const mapNote = {};
        const uniqueKnownVariable = new Set();
        for (const variable of this._catalog.listGlobalVariable) {
            uniqueKnownVariable.add(variable.name);
            const note = LaunchArgumentUtil.computeGlobalVariableNote(variable, this._profile, context, this._selection, this._catalog);
            if (note !== null)
                mapNote[`gv:${variable.name}|${variable.variantName}`] = note;
        }
        const uniqueKnownArgument = new Set(this._catalog.listArgument.map((argument) => argument.name));

        const listPreset = [];
        for (const preset of this._store.getPresetList()) {
            const presetCommandLine = LaunchArgumentUtil.composeCommandLine(this._catalog, preset.selection, this._makeOptions());
            listPreset.push({
                name: preset.name,
                createdAt: preset.createdAt,
                argumentCount: presetCommandLine.listArgument.length,
                shellText: LaunchArgumentUtil.makeShellCommandLine(presetCommandLine.listArgument),
            });
        }

        const state = {
            type: 'state',
            bInitialized: this._bInitialized,
            catalogVersion: this._catalogVersion,
            selection: {
                globalVariable: makeObjectFromMapInternal(this._selection.mapGlobalVariable),
                argument: makeObjectFromMapInternal(this._selection.mapArgument),
                exclusiveChoice: Object.fromEntries(this._selection.mapExclusiveChoice),
                listCustomArgument: this._selection.listCustomArgument.map((item) => ({ ...item })),
                listEnvironment: this._selection.listEnvironment.map((item) => ({ ...item })),
            },
            listUnknownGlobalVariable: Array.from(this._selection.mapGlobalVariable.keys()).filter((name) => uniqueKnownVariable.has(name) === false).sort(),
            listUnknownArgument: Array.from(this._selection.mapArgument.keys()).filter((name) => uniqueKnownArgument.has(name) === false).sort(),
            context,
            profile: {
                name: this._profile.name,
                source: this._profileSource,
                bHasCatalogSource: CatalogProfile.hasCatalogSource(this._profile),
                listStatusText: this._makeStatusTextList(context),
                mapModuleLabel: this._profile.module.mapLabel,
                commandLine: this._profile.commandLine,
            },
            commandLine: {
                listArgument: composed.listArgument,
                listIssue: composed.listIssue,
                shellText: LaunchArgumentUtil.makeShellCommandLine(composed.listArgument),
                listEnvironment: LaunchArgumentUtil.composeEnvironment(this._selection),
            },
            bPowerShellSplit: LaunchArgumentUtil.hasPowerShellSplitArgument(composed.listArgument),
            mapNote,
            listPreset,
            listBanner: this._makeBannerList(),
        };
        if (bIncludeCatalog)
            state.catalog = this._catalog;
        return state;
    }

    /** @brief 프로필 `status` 로 머리글에 보일 빌드 문맥 글(예: `Dev` · `게임 Empty`)을 만듭니다. */
    _makeStatusTextList(context) {
        const listText = [];
        for (const status of this._profile.listStatus) {
            if ((status.cacheVariable in context.mapCacheValue) === false)
                continue;
            const value = context.mapCacheValue[status.cacheVariable];
            if (status.whenTrue !== '' || status.whenFalse !== '') {
                const text = LaunchArgumentUtil.isCmakeTrue(value) ? status.whenTrue : status.whenFalse;
                if (text !== '')
                    listText.push(text);
            } else {
                listText.push(status.label === '' ? value : `${status.label} ${value}`);
            }
        }
        return listText;
    }

    /** @brief 명령줄 규칙 · 빌드 캐시 값을 한데 묶은 옵션입니다(`LaunchArgumentUtil` 이 받는 모양). */
    _makeOptions() {
        return {
            commandLine: this._profile.commandLine,
            namePrefix: this._profile.globalVariable.namePrefix,
            mapCacheValue: this._buildContext.getContext().mapCacheValue,
        };
    }

    /** @brief 프로필 설정값입니다(객체 · 파일 경로 · 없음). */
    _readProfileSetting() {
        return vscode.workspace.getConfiguration(kProfileSection, this._workspaceFolder.uri).get(kProfileKey);
    }

    /**
     * @brief 설정 `cmakeDebugArgs.catalog` 에서 프로필을 읽고, 스캐너 · 빌드 문맥 · 파일 감시를 그 프로필로 갈아 끼웁니다.
     */
    _loadProfile() {
        const settingValue = this._readProfileSetting();
        const loaded = CatalogProfile.loadProfile(settingValue, this._workspaceFolder.uri.fsPath);
        this._profile = loaded.profile;
        this._profileSource = loaded.sourceText;
        this._listProfileProblem = loaded.problem === '' ? loaded.profile.listProblem.slice() : [loaded.problem];
        for (const problem of this._listProfileProblem)
            this._logger.warn(`profile: ${problem}`);
        this._logger.info(`profile: ${this._profile.name || '(unnamed)'} from ${this._profileSource || '(none)'}`);
        this._scanner = new CatalogScanner(this._workspaceFolder.uri.fsPath, this._profile);
        this._buildContext.setCacheVariableNameList(CatalogProfile.collectCacheVariableNames(this._profile));

        for (const disposable of this._listWatcherDisposable)
            disposable.dispose();
        this._listWatcherDisposable = [];
        const listGlob = this._scanner.getWatchGlobList();
        if (typeof settingValue === 'string' && settingValue !== '')
            listGlob.push(settingValue.replace(/\\/g, '/'));
        for (const glob of listGlob) {
            const watcher = vscode.workspace.createFileSystemWatcher(new vscode.RelativePattern(this._workspaceFolder, glob));
            const onFileChanged = (uri) => this.queueRescan(uri);
            this._listWatcherDisposable.push(watcher, watcher.onDidChange(onFileChanged), watcher.onDidCreate(onFileChanged), watcher.onDidDelete(onFileChanged));
        }
    }

    /** @brief 경로가 프로필 파일인지 묻습니다(그 파일이 바뀌면 프로필부터 다시 읽는다). */
    _isProfileFile(filePath) {
        const settingValue = this._readProfileSetting();
        if (typeof settingValue !== 'string' || settingValue === '')
            return false;
        const profileUri = vscode.Uri.joinPath(this._workspaceFolder.uri, ...settingValue.replace(/\\/g, '/').split('/'));
        return vscode.Uri.file(filePath).fsPath.toLowerCase() === profileUri.fsPath.toLowerCase();
    }

    /** @brief 지금 화면에 띄울 알림 띠 목록입니다. */
    _makeBannerList() {
        const listBanner = [];
        if (this._bInitialized === false)
            listBanner.push({ level: 'info', text: '소스에서 전역 변수와 인자를 읽는 중입니다…' });
        if (vscode.extensions.getExtension(kCmakeToolsExtensionId) === undefined)
            listBanner.push({ level: 'error', text: 'CMake Tools 확장이 없습니다. 인자를 cmake.debugConfig 에 쓸 수 없고 실행 · 디버그도 할 수 없습니다.' });
        if (this._writeError !== '')
            listBanner.push({ level: 'error', text: `CMake Tools 설정에 쓰지 못했습니다: ${this._writeError}` });
        if (this._storeProblem !== '')
            listBanner.push({ level: 'warning', text: this._storeProblem });
        for (const problem of this._listProfileProblem)
            listBanner.push({ level: 'warning', text: `프로필: ${problem}` });
        if (this._catalog.listProblem.length > 0)
            listBanner.push({ level: 'warning', text: `소스에서 읽지 못한 것이 ${this._catalog.listProblem.length} 개 있습니다 — 출력 창 "CMake Debug Args" 를 보세요.` });
        return listBanner;
    }

    /** @brief 지금 선택의 명령줄입니다. */
    _composeCommandLine() {
        return LaunchArgumentUtil.composeCommandLine(this._catalog, this._selection, this._makeOptions());
    }

    /** @brief 소스 전체를 읽어 카탈로그를 바꿉니다. */
    async _scanCatalog() {
        const startTime = Date.now();
        await this._scanner.scanAll();
        this._catalog = this._scanner.makeCatalog();
        this._catalogVersion += 1;
        this._logger.info(`scanned ${this._catalog.listGlobalVariable.length} global variables and ${this._catalog.listArgument.length} arguments in ${Date.now() - startTime} ms`);
        for (const problem of this._catalog.listProblem)
            this._logger.warn(problem);
    }

    /** @brief 받아 둔 소스 경로들을 다시 읽고, 카탈로그가 바뀌었으면 화면을 고칩니다. */
    async _rescanPending() {
        const listPath = Array.from(this._uniquePendingRescanPath);
        this._uniquePendingRescanPath.clear();
        if (listPath.some((filePath) => this._isProfileFile(filePath))) {
            this._logger.info('profile file changed - reloading the profile');
            await this.refresh();
            return;
        }
        let bChanged = false;
        for (const filePath of listPath) {
            if (await this._scanner.rescanFile(filePath))
                bChanged = true;
        }
        if (bChanged === false)
            return;
        const previousSignature = JSON.stringify(this._catalog);
        this._catalog = this._scanner.makeCatalog();
        if (JSON.stringify(this._catalog) === previousSignature)
            return;
        this._catalogVersion += 1;
        this._logger.info(`catalog updated from ${listPath.length} changed file(s)`);
        if (LaunchArgumentUtil.adoptKnownCustomArguments(this._selection, this._catalog, this._makeOptions()))
            this._scheduleFlush();
        this._notifyView();
    }

    /**
     * @brief 설정의 명령줄이 지금 선택과 다르면 설정을 정본으로 읽어 들입니다(시작할 때 — 닫혀 있는 동안 바뀌었을 수 있다).
     * @return 읽어 들였으면 true 입니다.
     */
    _syncFromSettings() {
        const setting = this._writer.read();
        const settingSignature = makeSignatureInternal(setting.listArgument, setting.listEnvironment);
        const composedSignature = makeSignatureInternal(this._composeCommandLine().listArgument, LaunchArgumentUtil.composeEnvironment(this._selection));
        this._lastWrittenSignature = settingSignature;
        if (settingSignature === composedSignature)
            return false;
        this._logger.info('cmake.debugConfig differs from the saved selection - importing it');
        this._selection = LaunchArgumentUtil.importCommandLine(setting.listArgument, setting.listEnvironment, this._catalog, this._selection, this._makeOptions());
        this._scheduleFlush();
        return true;
    }

    /** @brief 설정 변경 이벤트입니다. 자기 쓰기의 메아리가 아니면 읽어 들입니다. */
    _onConfigurationChanged(event) {
        if (event.affectsConfiguration(`${kProfileSection}.${kProfileKey}`, this._workspaceFolder.uri)) {
            this._logger.info('cmakeDebugArgs.catalog changed - reloading the profile');
            void this.refresh();
            return;
        }
        if (this._writer.isAffectedBy(event) === false)
            return;
        const setting = this._writer.read();
        const signature = makeSignatureInternal(setting.listArgument, setting.listEnvironment);
        if (signature === this._lastWrittenSignature)
            return;
        this._logger.info('cmake.debugConfig changed outside this view - importing it');
        this._selection = LaunchArgumentUtil.importCommandLine(setting.listArgument, setting.listEnvironment, this._catalog, this._selection, this._makeOptions());
        this._lastWrittenSignature = signature;
        this._scheduleFlush();
        this._notifyView();
    }

    /** @brief 설정 · 상태 파일 쓰기 본체입니다. `flush` 의 사슬 안에서만 부릅니다. */
    async _writeNow() {
        const composed = this._composeCommandLine();
        const listEnvironment = LaunchArgumentUtil.composeEnvironment(this._selection);
        const signature = makeSignatureInternal(composed.listArgument, listEnvironment);
        if (signature !== this._lastWrittenSignature) {
            const previousSignature = this._lastWrittenSignature;
            this._lastWrittenSignature = signature;   // 쓰는 도중에 오는 변경 이벤트를 메아리로 알아보게 먼저 바꾼다
            try {
                await this._writer.write(composed.listArgument, listEnvironment);
                this._logger.info(`cmake.debugConfig.args = ${JSON.stringify(composed.listArgument)}`);
                if (this._writeError !== '') {
                    this._writeError = '';
                    this._notifyView();
                }
            } catch (error) {
                this._lastWrittenSignature = previousSignature;
                this._writeError = error.message;
                this._logger.error(`failed to write cmake.debugConfig: ${error.message}`);
                this._notifyView();
            }
        }
        try {
            await this._store.save(this._selection);
        } catch (error) {
            this._logger.error(`failed to save selection state: ${error.message}`);
        }
    }

    /** @brief 선택을 바꾼 뒤 부릅니다: 화면은 바로, 디스크는 미뤄서. */
    _commitChange() {
        this._notifyView();
        this._scheduleFlush();
    }

    /** @brief 디스크 쓰기를 `kWriteDelayMs` 뒤로 미룹니다(그 사이 변경은 한 번에 쓴다). */
    _scheduleFlush() {
        clearTimeout(this._writeTimer);
        this._writeTimer = setTimeout(() => void this.flush(), kWriteDelayMs);
    }

    /** @brief 화면 상태가 바뀌었다고 알립니다. */
    _notifyView() {
        this._viewEmitter.fire();
    }

    /** @brief 전역 변수 칸을 켜고 끄거나 값을 바꿉니다. 값을 바꾸면 켭니다(고친다는 것은 넘기고 싶다는 뜻). */
    _setGlobalVariable(message) {
        const name = String(message.name);
        const variable = LaunchArgumentUtil.findGlobalVariable(this._catalog, name, this._buildContext.getContext().mapCacheValue);
        const previous = this._selection.mapGlobalVariable.get(name);
        const state = previous === undefined ? { bEnabled: false, value: variable === null ? '' : LaunchArgumentUtil.makeInitialValue(variable) } : { ...previous };
        if (typeof message.value === 'string') {
            state.value = message.value;
            if (typeof message.bEnabled !== 'boolean')
                state.bEnabled = true;
        }
        if (typeof message.bEnabled === 'boolean')
            state.bEnabled = message.bEnabled;
        this._selection.mapGlobalVariable.set(name, state);
        this._commitChange();
    }

    /** @brief 전역 변수 값을 기본값으로 되돌립니다(켜짐 상태는 그대로). */
    _resetGlobalVariable(message) {
        const name = String(message.name);
        const variable = LaunchArgumentUtil.findGlobalVariable(this._catalog, name, this._buildContext.getContext().mapCacheValue);
        const state = this._selection.mapGlobalVariable.get(name);
        if (variable === null || state === undefined)
            return;
        state.value = variable.defaultText;
        this._commitChange();
    }

    /** @brief 카탈로그에 없는(소스에서 사라진) 전역 변수 칸을 선택에서 지웁니다. */
    _removeGlobalVariable(message) {
        if (this._selection.mapGlobalVariable.delete(String(message.name)))
            this._commitChange();
    }

    /** @brief 인자 칸을 켜고 끄거나 값을 바꿉니다. 값을 바꾸면 켭니다. */
    _setArgument(message) {
        const name = String(message.name);
        const argument = this._catalog.listArgument.find((item) => item.name === name && item.group === kGeneralGroup);
        const previous = this._selection.mapArgument.get(name);
        let state;
        if (previous !== undefined)
            state = { ...previous };
        else if (argument !== undefined)
            state = { bEnabled: false, value: LaunchArgumentUtil.makeInitialValue(argument), spelling: argument.listSpelling[0] };
        else
            state = { bEnabled: false, value: '', spelling: '' };
        if (argument !== undefined)
            state.spelling = argument.listSpelling[0];
        if (argument !== undefined && argument.valueKind === ValueKind.Boolean)
            state.value = 'true';
        if (typeof message.value === 'string') {
            state.value = message.value;
            if (typeof message.bEnabled !== 'boolean')
                state.bEnabled = true;
        }
        if (typeof message.bEnabled === 'boolean')
            state.bEnabled = message.bEnabled;
        this._selection.mapArgument.set(name, state);
        this._commitChange();
    }

    /** @brief 인자 값을 기본값으로 되돌립니다. */
    _resetArgument(message) {
        const name = String(message.name);
        const argument = this._catalog.listArgument.find((item) => item.name === name);
        const state = this._selection.mapArgument.get(name);
        if (argument === undefined || state === undefined)
            return;
        state.value = argument.defaultText;
        this._commitChange();
    }

    /** @brief 카탈로그에 없는 인자 칸을 선택에서 지웁니다. */
    _removeArgument(message) {
        if (this._selection.mapArgument.delete(String(message.name)))
            this._commitChange();
    }

    /** @brief 배타 묶음(예: 그래픽 백엔드)에서 인자 하나를 고릅니다. 빈 글이면 고르지 않습니다(프로그램 기본값). */
    _setExclusiveChoice(message) {
        const groupId = String(message.groupId);
        const name = typeof message.name === 'string' ? message.name : '';
        if (name === '')
            this._selection.mapExclusiveChoice.delete(groupId);
        else
            this._selection.mapExclusiveChoice.set(groupId, name);
        this._commitChange();
    }

    /**
     * @brief 사용자 인자를 더합니다. 여러 개를 한 줄로 붙여 넣어도 되고, 카탈로그가 아는 것은 제 칸(전역 변수 · 인자)으로 갑니다.
     */
    _addCustomArgument(message) {
        const listText = LaunchArgumentUtil.splitCommandLineText(String(message.text));
        if (listText.length === 0)
            return;
        for (const text of listText)
            this._selection.listCustomArgument.push({ text, bEnabled: true });
        LaunchArgumentUtil.adoptKnownCustomArguments(this._selection, this._catalog, this._makeOptions());
        this._commitChange();
    }

    /** @brief 사용자 인자 한 줄의 글 · 켜짐을 바꿉니다. 고치는 중인 글은 제 칸으로 옮기지 않습니다(치는 도중에 사라지지 않게). */
    _setCustomArgument(message) {
        const item = this._selection.listCustomArgument[message.index];
        if (item === undefined)
            return;
        if (typeof message.text === 'string')
            item.text = message.text;
        if (typeof message.bEnabled === 'boolean')
            item.bEnabled = message.bEnabled;
        this._commitChange();
    }

    /** @brief 사용자 인자 한 줄을 지웁니다. */
    _removeCustomArgument(message) {
        if (this._selection.listCustomArgument[message.index] === undefined)
            return;
        this._selection.listCustomArgument.splice(message.index, 1);
        this._commitChange();
    }

    /** @brief 환경 변수를 더합니다. 같은 이름이 있으면 그 값을 바꾸고 켭니다. */
    _addEnvironment(message) {
        const name = String(message.name).trim();
        if (name === '')
            return;
        const value = typeof message.value === 'string' ? message.value : '';
        const existing = this._selection.listEnvironment.find((item) => item.name === name);
        if (existing !== undefined) {
            existing.value = value;
            existing.bEnabled = true;
        } else {
            this._selection.listEnvironment.push({ name, value, bEnabled: true });
        }
        this._commitChange();
    }

    /** @brief 환경 변수 한 줄의 이름 · 값 · 켜짐을 바꿉니다. */
    _setEnvironment(message) {
        const item = this._selection.listEnvironment[message.index];
        if (item === undefined)
            return;
        if (typeof message.name === 'string')
            item.name = message.name;
        if (typeof message.value === 'string')
            item.value = message.value;
        if (typeof message.bEnabled === 'boolean')
            item.bEnabled = message.bEnabled;
        this._commitChange();
    }

    /** @brief 환경 변수 한 줄을 지웁니다. */
    _removeEnvironment(message) {
        if (this._selection.listEnvironment[message.index] === undefined)
            return;
        this._selection.listEnvironment.splice(message.index, 1);
        this._commitChange();
    }

    /** @brief 지금 선택을 이름 붙여 프리셋으로 둡니다(같은 이름은 덮어쓴다). */
    async _savePreset(message) {
        const name = String(message.name).trim();
        if (name === '')
            return;
        this._store.putPreset(name, this._selection);
        this._notifyView();
        await this.flush();
    }

    /** @brief 프리셋을 지금 선택으로 불러옵니다. */
    _loadPreset(message) {
        const selection = this._store.findPreset(String(message.name));
        if (selection === null) {
            this._logger.warn(`preset not found: ${message.name}`);
            return;
        }
        this._selection = selection;
        LaunchArgumentUtil.adoptKnownCustomArguments(this._selection, this._catalog, this._makeOptions());
        this._commitChange();
    }

    /** @brief 확인을 받고 프리셋을 지웁니다(웹뷰는 `confirm()` 을 띄우지 못한다). */
    async _deletePreset(message) {
        const name = String(message.name);
        const answer = await vscode.window.showWarningMessage(`프리셋 "${name}" 을 지울까요?`, { modal: true }, kDeleteActionLabel);
        if (answer !== kDeleteActionLabel || this._store.removePreset(name) === false)
            return;
        this._notifyView();
        await this.flush();
    }

    /** @brief 항목이 정의된 소스 줄을 엽니다. 저장소 밖 경로는 열지 않습니다. */
    async _openLocation(message) {
        const relativePath = String(message.relativePath);
        if (relativePath.includes('..') || /^[A-Za-z]:|^[\\/]/.test(relativePath))
            return;
        const uri = vscode.Uri.joinPath(this._workspaceFolder.uri, ...relativePath.split('/'));
        const lineIndex = Math.max(0, Number(message.lineNumber) - 1);
        await vscode.window.showTextDocument(uri, { selection: new vscode.Range(lineIndex, 0, lineIndex, 0), preview: true });
    }
}

module.exports = {
    LaunchArgumentController,
};
