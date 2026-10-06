'use strict';

/**
 * @file StatusBarIndicator.js
 * @brief 상태 표시줄에 "지금 넘기는 인자 수" 를 띄웁니다. 누르면 Launch Args 패널이 열립니다.
 * @details CMake Tools 의 디버그 · 실행 버튼은 상태 표시줄에 있지만, 그 버튼이 어떤 인자로 띄울지는 보이지 않습니다.
 *          이 항목이 그 옆에서 "인자 3개" 처럼 알리고, 마우스를 올리면 명령줄 전체를 보입니다.
 */

const vscode = require('vscode');

/** @brief 누르면 여는 웹뷰 view id 의 focus 명령입니다(package.json `launchArgsView`). */
const kFocusViewCommand = 'launchArgsView.focus';

/**
 * @brief 상태 표시줄 항목 하나입니다. 컨트롤러의 화면 상태가 바뀔 때마다 글을 고칩니다.
 */
class StatusBarIndicator {
    /** @param controller `LaunchArgumentController` 입니다. */
    constructor(controller) {
        /** @brief 컨트롤러입니다. */
        this._controller = controller;
        /** @brief 상태 표시줄 항목입니다. CMake Tools 항목 근처(왼쪽)에 둡니다. */
        this._item = vscode.window.createStatusBarItem('launchArgs.summary', vscode.StatusBarAlignment.Left, 0);
        this._item.name = 'Launch Args';
        this._item.command = kFocusViewCommand;
        /** @brief 컨트롤러 구독입니다. */
        this._subscription = controller.onDidChangeView(() => this.refresh());
        this.refresh();
        this._item.show();
    }

    /** @brief 지금 명령줄로 글 · 도움말 · 경고 색을 고칩니다. */
    refresh() {
        const summary = this._controller.makeCommandLineSummary();
        const environmentText = summary.environmentCount > 0 ? ` · 환경 ${summary.environmentCount}` : '';
        this._item.text = summary.argumentCount > 0 || summary.environmentCount > 0
            ? `$(symbol-parameter) 인자 ${summary.argumentCount}${environmentText}`
            : '$(symbol-parameter) 인자 없음';
        this._item.backgroundColor = summary.issueCount > 0 ? new vscode.ThemeColor('statusBarItem.warningBackground') : undefined;

        const tooltip = new vscode.MarkdownString(undefined, true);
        tooltip.appendMarkdown('**Launch Args** — CMake Tools 디버그 · 실행과 `launchArgs` 를 적은 launch.json 구성에 넘기는 인자\n\n');
        if (summary.shellText !== '')
            tooltip.appendCodeblock(summary.shellText, 'text');
        else
            tooltip.appendMarkdown('_넘길 인자가 없습니다._\n\n');
        if (summary.issueCount > 0)
            tooltip.appendMarkdown(`\n$(warning) 값이 틀려 뺀 항목 ${summary.issueCount} 개\n\n`);
        tooltip.appendMarkdown('\n누르면 패널을 엽니다.');
        this._item.tooltip = tooltip;
    }

    /** @brief 항목과 구독을 풉니다. */
    dispose() {
        this._subscription.dispose();
        this._item.dispose();
    }
}

module.exports = {
    StatusBarIndicator,
};
