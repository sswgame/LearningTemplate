'use strict';

/**
 * @file Extension.js
 * @brief 확장 진입점입니다 — 주석 접기 범위 제공자 · `Toggle Comments` 명령 · 상태 표시줄 단추를 잇습니다.
 * @details 접기 범위는 `FoldingRangeProvider` 로 `FoldingRangeKind.Comment` 를 내고, 접기 · 펼치기는 VS Code 의 `editor.fold` /
 *          `editor.unfold` 에 주석 범위의 시작 줄만 넘깁니다 — 코드 블록의 접힘 상태는 건드리지 않습니다. VS Code 는 여러 제공자
 *          (C/C++ · clangd)의 범위를 합치므로, 같은 줄에서 시작하는 남의 주석 범위가 이겨도 접히는 것은 그 주석입니다.
 *          그런 확장이 없으면 들여쓰기 코드 범위도 함께 냅니다 — 제공자를 등록하면 VS Code 가 들여쓰기 접기를 끄기 때문입니다.
 *          상용 대조: VS Code 내장 `editor.foldAllBlockComments` 는 블록 주석만 접고 토글 상태가 없다 — 이 확장은 `//` 줄 묶음까지
 *          접고 문서마다 접힘 상태를 기억한다(Visual Studio "Toggle All Outlining" 과 같은 한 단추 토글).
 */

const vscode = require('vscode');

const { scanComments, collectFoldRanges } = require('./CommentScanner');
const { collectIndentRanges } = require('./IndentFolding');

const kConfigurationSection = 'commentFold';
const kToggleCommand = 'commentFold.toggle';
/** @brief 다루는 언어입니다(VS Code 내장 언어 id — `.h` · `.hpp` · `.inl` · `.cc` 는 cpp, `.hlsli` 는 hlsl). */
const kListLanguageId = ['c', 'cpp', 'hlsl'];
/** @brief C · C++ 코드 접기 범위를 내는 확장입니다. 하나라도 켜져 있으면 들여쓰기 범위를 내지 않습니다(그 확장의 범위가 정확하다). */
const kListCppFoldingExtensionId = ['ms-vscode.cpptools', 'llvm-vs-code-extensions.vscode-clangd'];

/** @brief 설정 값을 읽습니다. 기본값은 package.json `contributes.configuration` 한 곳에 둡니다. */
function readOptionsInternal(document) {
    const configuration = vscode.workspace.getConfiguration(kConfigurationSection, document);
    return {
        minimumLineCount: configuration.get('minimumLineCount'),
        excludeFileHeader: configuration.get('excludeFileHeader'),
        foldOnOpen: configuration.get('foldOnOpen'),
    };
}

/**
 * @brief 이 확장이 코드(들여쓰기) 접기 범위도 내야 하는가.
 * @details 접기 제공자를 등록하면 VS Code 는 그 언어의 들여쓰기 접기를 끈다 — 코드 범위를 내는 다른 확장이 없으면 대신 낸다.
 *          C/C++ 확장은 `C_Cpp.codeFolding` 이 `disabled` 이면 범위를 내지 않는다. HLSL 은 아는 제공자가 없어 늘 낸다.
 */
function needsCodeRangesInternal(document) {
    if (document.languageId === 'hlsl')
        return true;
    const bHasCpptools = vscode.extensions.getExtension(kListCppFoldingExtensionId[0]) !== undefined
        && vscode.workspace.getConfiguration('C_Cpp', document).get('codeFolding') !== 'disabled';
    return !bHasCpptools && vscode.extensions.getExtension(kListCppFoldingExtensionId[1]) === undefined;
}

/**
 * @brief 문서마다 접을 범위 캐시 · 접힘 상태를 들고, 명령 · 상태 표시줄을 고칩니다.
 */
class CommentFoldController {
    constructor() {
        /** @brief 문서 URI → `{ version, listRange, listCodeRange }`. 토크나이즈는 문서 판이 바뀔 때만 합니다. */
        this._mapCache = new Map();
        /** @brief 문서 URI → 주석이 접혀 있는가. 없으면 펼침입니다. */
        this._mapFolded = new Map();
        /** @brief 이미 연 문서입니다(`foldOnOpen` 은 처음 볼 때 한 번만). */
        this._setOpened = new Set();
        /** @brief 접기 범위가 바뀌었다고 VS Code 에 알립니다(설정이 바뀔 때). */
        this._rangeChangeEmitter = new vscode.EventEmitter();
        this.onDidChangeFoldingRanges = this._rangeChangeEmitter.event;
        /** @brief 지금 상태를 보이는 상태 표시줄 단추입니다. */
        this._statusItem = vscode.window.createStatusBarItem('commentFold.status', vscode.StatusBarAlignment.Right, 100);
        this._statusItem.name = 'Comment Fold';
        this._statusItem.command = kToggleCommand;
    }

    /** @brief 문서가 다루는 언어인가. */
    isSupported(document) {
        return kListLanguageId.includes(document.languageId);
    }

    /** @brief 문서 판의 캐시 항목입니다. 판이 바뀌었으면 다시 셉니다. */
    getCacheEntryInternal(document) {
        const key = document.uri.toString();
        const cached = this._mapCache.get(key);
        if (cached !== undefined && cached.version === document.version)
            return cached;
        const text = document.getText();
        const listRange = collectFoldRanges(scanComments(text), readOptionsInternal(document));
        let listCodeRange = [];
        if (needsCodeRangesInternal(document)) {
            // 같은 줄에서 시작하는 범위는 VS Code 가 하나만 남긴다 — 주석 범위가 남게 코드 범위 쪽을 뺀다.
            const setCommentStart = new Set(listRange.map((range) => range.start));
            const tabSize = vscode.workspace.getConfiguration('editor', document).get('tabSize');
            listCodeRange = collectIndentRanges(text, tabSize).filter((range) => !setCommentStart.has(range.start));
        }
        const entry = { version: document.version, listRange, listCodeRange };
        this._mapCache.set(key, entry);
        return entry;
    }

    /** @brief 주석 접을 범위 `[{ start, end }]` 입니다. */
    getFoldRanges(document) {
        return this.getCacheEntryInternal(document).listRange;
    }

    /** @brief `FoldingRangeProvider` 입니다 — 주석 범위와, 필요하면 들여쓰기 코드 범위입니다. */
    provideFoldingRanges(document) {
        const entry = this.getCacheEntryInternal(document);
        return entry.listRange.map((range) => new vscode.FoldingRange(range.start, range.end, vscode.FoldingRangeKind.Comment))
            .concat(entry.listCodeRange.map((range) => new vscode.FoldingRange(range.start, range.end)));
    }

    /** @brief 문서의 주석이 접혀 있는가. */
    isFolded(document) {
        return this._mapFolded.get(document.uri.toString()) === true;
    }

    /** @brief 편집기의 주석을 모두 접거나(bFold) 펼칩니다. `editor.fold` 는 활성 편집기에 하므로 먼저 활성으로 만듭니다. */
    async setFolded(editor, bFold) {
        if (vscode.window.activeTextEditor !== editor)
            await vscode.window.showTextDocument(editor.document, editor.viewColumn);
        const listRange = this.getFoldRanges(editor.document);
        if (listRange.length > 0) {
            // levels 1 · direction down: 시작 줄을 품은 가장 안쪽 범위(그 주석) 하나만 — 바깥 코드 블록과 안쪽 범위는 그대로 둔다.
            await vscode.commands.executeCommand(bFold ? 'editor.fold' : 'editor.unfold', {
                levels: 1,
                direction: 'down',
                selectionLines: listRange.map((range) => range.start),
            });
        }
        this._mapFolded.set(editor.document.uri.toString(), bFold);
        this.refreshStatus();
    }

    /** @brief `Toggle Comments` — 펼쳐져 있으면 모두 접고, 접혀 있으면 모두 펼칩니다. */
    async toggle() {
        const editor = vscode.window.activeTextEditor;
        if (editor === undefined || !this.isSupported(editor.document))
            return;
        await this.setFolded(editor, !this.isFolded(editor.document));
    }

    /** @brief 편집기가 활성이 되면: 처음 보는 문서이고 `foldOnOpen` 이면 접고, 상태 표시줄을 고칩니다. */
    async handleActiveEditor(editor) {
        this.refreshStatus();
        if (editor === undefined || !this.isSupported(editor.document))
            return;
        const key = editor.document.uri.toString();
        if (this._setOpened.has(key))
            return;
        this._setOpened.add(key);
        if (readOptionsInternal(editor.document).foldOnOpen)
            await this.setFolded(editor, true);
    }

    /** @brief 문서를 닫으면 캐시 · 상태를 지웁니다. */
    forgetDocument(document) {
        const key = document.uri.toString();
        this._mapCache.delete(key);
        this._mapFolded.delete(key);
        this._setOpened.delete(key);
    }

    /** @brief 설정이 바뀌면 캐시를 버리고 범위를 다시 받게 합니다. */
    handleConfigurationChange(event) {
        if (!event.affectsConfiguration(kConfigurationSection) && !event.affectsConfiguration('editor.tabSize') && !event.affectsConfiguration('C_Cpp.codeFolding'))
            return;
        this._mapCache.clear();
        this._rangeChangeEmitter.fire();
    }

    /** @brief 상태 표시줄 단추를 활성 편집기 상태로 고칩니다. 다루지 않는 언어면 숨깁니다. */
    refreshStatus() {
        const editor = vscode.window.activeTextEditor;
        if (editor === undefined || !this.isSupported(editor.document)) {
            this._statusItem.hide();
            return;
        }
        const bFolded = this.isFolded(editor.document);
        this._statusItem.text = bFolded ? '$(fold) Comments' : '$(unfold) Comments';
        this._statusItem.tooltip = bFolded ? 'Comments are folded - click to unfold them' : 'Comments are expanded - click to fold them';
        this._statusItem.show();
    }

    dispose() {
        this._statusItem.dispose();
        this._rangeChangeEmitter.dispose();
    }
}

/**
 * @brief VS Code 가 확장을 켤 때 부릅니다.
 * @return 통합 테스트가 쓰는 손잡이(컨트롤러)입니다.
 */
function activate(context) {
    const controller = new CommentFoldController();
    const selector = kListLanguageId.map((language) => ({ language }));
    context.subscriptions.push(
        controller,
        vscode.languages.registerFoldingRangeProvider(selector, controller),
        vscode.commands.registerCommand(kToggleCommand, () => controller.toggle()),
        vscode.window.onDidChangeActiveTextEditor((editor) => controller.handleActiveEditor(editor)),
        vscode.workspace.onDidCloseTextDocument((document) => controller.forgetDocument(document)),
        vscode.workspace.onDidChangeConfiguration((event) => controller.handleConfigurationChange(event)),
        vscode.extensions.onDidChange(() => controller.handleConfigurationChange({ affectsConfiguration: () => true })),
    );
    controller.handleActiveEditor(vscode.window.activeTextEditor);
    return controller;
}

function deactivate() {}

module.exports = {
    activate,
    deactivate,
};
