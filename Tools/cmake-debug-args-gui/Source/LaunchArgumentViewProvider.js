'use strict';

/**
 * @file LaunchArgumentViewProvider.js
 * @brief 사이드바 웹뷰를 띄우고 컨트롤러와 메시지를 주고받습니다.
 * @details 웹뷰 파일(`Webview/Index.html` · `Style.css` · `Main.js`)은 `asWebviewUri` 로 걸고, 스크립트는 nonce 가 붙은 것만 돕니다(CSP).
 *          카탈로그는 바뀌었을 때만 싣습니다 — 웹뷰가 행을 다시 만드는 것은 그때뿐이고, 나머지는 값만 고칩니다(글을 치는 칸의 포커스를 지킨다).
 */

const crypto = require('crypto');
const fs = require('fs');
const vscode = require('vscode');

/** @brief package.json 의 웹뷰 view id 입니다. */
const kViewId = 'cmakeDebugArgsView';

/**
 * @brief 웹뷰 공급자입니다. 뷰가 닫혔다 열려도 같은 컨트롤러를 씁니다.
 */
class LaunchArgumentViewProvider {
    /**
     * @param extensionUri 확장 루트 URI 입니다.
     * @param controller `LaunchArgumentController` 입니다.
     */
    constructor(extensionUri, controller) {
        /** @brief 확장 루트 URI 입니다. */
        this._extensionUri = extensionUri;
        /** @brief 컨트롤러입니다. */
        this._controller = controller;
        /** @brief 지금 떠 있는 웹뷰 뷰입니다. 없으면 null 입니다. */
        this._webviewView = null;
        /** @brief 웹뷰에 마지막으로 보낸 카탈로그 번호입니다. -1 이면 아직 보내지 않았습니다. */
        this._postedCatalogVersion = -1;
        /** @brief 이 뷰에 걸린 구독입니다. */
        this._listViewDisposable = [];
        /** @brief 컨트롤러 구독입니다(공급자 수명). */
        this._controllerSubscription = controller.onDidChangeView(() => this._postState());
    }

    /** @brief VS Code 가 뷰를 처음 보이거나 다시 만들 때 부릅니다. */
    resolveWebviewView(webviewView) {
        this._disposeViewSubscription();
        this._webviewView = webviewView;
        this._postedCatalogVersion = -1;
        const webviewRoot = vscode.Uri.joinPath(this._extensionUri, 'Webview');
        webviewView.webview.options = { enableScripts: true, localResourceRoots: [webviewRoot] };
        webviewView.webview.html = this._makeHtml(webviewView.webview, webviewRoot);
        this._listViewDisposable.push(webviewView.webview.onDidReceiveMessage((message) => this._onMessage(message)));
        this._listViewDisposable.push(webviewView.onDidDispose(() => {
            this._disposeViewSubscription();
            this._webviewView = null;
        }));
    }

    /** @brief 구독을 풉니다. */
    dispose() {
        this._disposeViewSubscription();
        this._controllerSubscription.dispose();
    }

    /** @brief 웹뷰가 보낸 메시지입니다. `ready` 는 여기서 받아 카탈로그까지 다시 보냅니다. */
    _onMessage(message) {
        if (message !== null && typeof message === 'object' && message.type === 'ready') {
            this._postedCatalogVersion = -1;
            this._postState();
            return;
        }
        void this._controller.handleMessage(message);
    }

    /** @brief 화면 상태를 보냅니다. 카탈로그는 웹뷰가 가진 것과 다를 때만 싣습니다. */
    _postState() {
        if (this._webviewView === null)
            return;
        const viewState = this._controller.makeViewState(false);
        const bIncludeCatalog = viewState.catalogVersion !== this._postedCatalogVersion;
        const message = bIncludeCatalog ? this._controller.makeViewState(true) : viewState;
        void this._webviewView.webview.postMessage(message).then((bDelivered) => {
            if (bDelivered && bIncludeCatalog)
                this._postedCatalogVersion = message.catalogVersion;
        });
    }

    /** @brief `Index.html` 의 자리표시자(`{{…}}`)를 채운 HTML 입니다. */
    _makeHtml(webview, webviewRoot) {
        const nonce = crypto.randomBytes(16).toString('base64');
        const template = fs.readFileSync(vscode.Uri.joinPath(webviewRoot, 'Index.html').fsPath, 'utf8');
        const mapPlaceholder = new Map([
            ['{{CSP_SOURCE}}', webview.cspSource],
            ['{{NONCE}}', nonce],
            ['{{STYLE_URI}}', webview.asWebviewUri(vscode.Uri.joinPath(webviewRoot, 'Style.css')).toString()],
            ['{{SCRIPT_URI}}', webview.asWebviewUri(vscode.Uri.joinPath(webviewRoot, 'Main.js')).toString()],
        ]);
        let html = template;
        for (const [placeholder, value] of mapPlaceholder)
            html = html.split(placeholder).join(value);
        return html;
    }

    /** @brief 뷰 구독을 풉니다. */
    _disposeViewSubscription() {
        for (const disposable of this._listViewDisposable)
            disposable.dispose();
        this._listViewDisposable = [];
    }
}

module.exports = {
    kViewId,
    LaunchArgumentViewProvider,
};
