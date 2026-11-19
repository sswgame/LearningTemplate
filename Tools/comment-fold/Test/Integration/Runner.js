'use strict';

/**
 * @file Runner.js
 * @brief 실제 VS Code 확장 호스트 안에서 도는 통합 테스트입니다(`--extensionTestsPath`).
 * @details `Tools/Scripts/ExtensionTool.py test --integration Tools/comment-fold` 가 빈 작업 폴더로 격리된 VS Code 를 띄워
 *          이 파일의 `run()` 을 부릅니다. 보는 것:
 *          - `Toggle Comments` 가 주석만 접고(숨은 줄 = 주석 안쪽 줄) 코드 블록 · 문자열 안 `//` 는 그대로 두는가, 다시 누르면 다 펼치는가.
 *          - `commentFold.foldOnOpen` 을 켜면 새로 연 파일의 주석이 접혀 열리는가.
 *          - 기본 단축키가 다른 기본 단축키와 겹치지 않는가.
 *          실패하면 던져서 VS Code 가 0 이 아닌 코드로 끝나게 합니다. 접힘은 편집기의 `visibleRanges`(접힌 줄은 빠진다)로 봅니다.
 */

const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

const kExtensionId = 'sw-engine.comment-fold';
const kListSampleLine = [
    '/** @file Sample.cpp',                     // 0
    ' *  license text */',                      // 1
    '#include <cstdio>',                        // 2
    '// one',                                   // 3
    '// two',                                   // 4
    'int f() {',                                // 5
    '    const char* s = "// not /* a comment";', // 6
    '    /* block',                             // 7
    '       lines */',                          // 8
    '    return 1; // trailing',                // 9
    '}',                                        // 10
    '/// doc a',                                // 11
    '/// doc b',                                // 12
    '// plain',                                 // 13
    'int g();',                                 // 14
];

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

/** @brief 편집기에서 접혀 숨은 줄 번호입니다. */
function collectHiddenLines(editor) {
    const listHidden = [];
    for (let line = 0; line < editor.document.lineCount; ++line) {
        if (!editor.visibleRanges.some((range) => range.start.line <= line && line <= range.end.line))
            listHidden.push(line);
    }
    return listHidden;
}

/** @brief 예시 글로 제목 없는 C++ 문서를 열어 보입니다. */
async function openSampleEditor() {
    const document = await vscode.workspace.openTextDocument({ language: 'cpp', content: kListSampleLine.join('\n') });
    return vscode.window.showTextDocument(document);
}

async function run() {
    const extension = vscode.extensions.getExtension(kExtensionId);
    assert.ok(extension, `${kExtensionId} is loaded`);

    const editor = await openSampleEditor();
    const controller = await extension.activate();
    assert.deepEqual(controller.getFoldRanges(editor.document).map((range) => [range.start, range.end]), [[0, 1], [3, 4], [7, 8], [11, 12]]);
    // 접기 모델이 제공자 범위를 받을 때까지(첫 계산은 비동기) 기다린다.
    await new Promise((resolve) => setTimeout(resolve, 500));

    await vscode.commands.executeCommand('commentFold.toggle');
    await waitUntil(() => collectHiddenLines(editor).length > 0, 'comments fold');
    assert.deepEqual(collectHiddenLines(editor), [1, 4, 8, 12], 'only comment lines hide; the function body and the string stay visible');
    assert.equal(controller.isFolded(editor.document), true);

    await vscode.commands.executeCommand('commentFold.toggle');
    await waitUntil(() => collectHiddenLines(editor).length === 0, 'comments unfold');
    assert.equal(controller.isFolded(editor.document), false);

    // 코드 블록을 사용자가 접어 둔 채로 토글해도 코드 블록은 그대로다.
    await vscode.commands.executeCommand('editor.fold', { levels: 1, direction: 'down', selectionLines: [5] });
    await waitUntil(() => collectHiddenLines(editor).length > 0, 'function body folds');
    const listHiddenBody = collectHiddenLines(editor);
    await vscode.commands.executeCommand('commentFold.toggle');
    await vscode.commands.executeCommand('commentFold.toggle');
    await new Promise((resolve) => setTimeout(resolve, 300));
    assert.deepEqual(collectHiddenLines(editor), listHiddenBody, 'toggling twice leaves the folded function body folded');

    await vscode.workspace.getConfiguration('commentFold').update('foldOnOpen', true, vscode.ConfigurationTarget.Global);
    const openedEditor = await openSampleEditor();
    await waitUntil(() => collectHiddenLines(openedEditor).length > 0, 'foldOnOpen folds a newly opened file');
    assert.deepEqual(collectHiddenLines(openedEditor), [1, 4, 8, 12]);
    await vscode.workspace.getConfiguration('commentFold').update('foldOnOpen', undefined, vscode.ConfigurationTarget.Global);

    await vscode.commands.executeCommand('workbench.action.openDefaultKeybindingsFile');
    await waitUntil(() => vscode.window.activeTextEditor !== undefined && vscode.window.activeTextEditor.document.getText().includes('commentFold.toggle'), 'default keybindings open');
    // 키 이름은 자판 배치를 따른다(`/` 가 한국어 · 미국 배치에서 `oem_2`) — 이 확장 줄에서 읽어 같은 키를 쓰는 줄을 모두 본다.
    const listDefaultLine = vscode.window.activeTextEditor.document.getText().split('\n');
    const ownLine = listDefaultLine.find((line) => line.includes('"commentFold.toggle"'));
    const keyMatch = /"key":\s*("[^"]+")/.exec(ownLine);
    assert.ok(keyMatch, `the default key is listed: ${ownLine}`);
    const listKeyLine = listDefaultLine.filter((line) => line.includes(`"key": ${keyMatch[1]}`) || line.includes(`"key":${keyMatch[1]}`));
    assert.ok(listKeyLine.every((line) => line.includes('commentFold.toggle')), `the default key is not taken by another command:\n${listKeyLine.join('\n')}`);

    fs.writeFileSync(path.join(vscode.workspace.workspaceFolders[0].uri.fsPath, 'integration-result.txt'), 'PASS\n');
}

module.exports = {
    run,
};
