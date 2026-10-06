'use strict';

/**
 * @file Main.js
 * @brief 사이드바 웹뷰 화면입니다. 의도만 확장으로 보내고, 확장이 보낸 화면 상태로 그립니다.
 * @details - 행(전역 변수 · 인자)은 카탈로그가 바뀔 때만 다시 만들고(`renderCatalog`), 나머지 갱신은 값 · 켜짐만 고칩니다
 *            (`applyState`). 그래서 글을 치는 칸의 포커스와 커서가 지켜집니다 — 포커스가 있는 칸의 값은 덮어쓰지 않습니다.
 *          - 탭 · 검색어 · 필터 · 접힌 묶음은 `vscode.setState` 에 둡니다(뷰를 닫았다 열어도 남는다).
 *          - 확인 창(`confirm`)은 웹뷰에서 뜨지 않으므로 지우기 확인은 확장이 띄웁니다.
 */
(function () {
    const vscodeApi = acquireVsCodeApi();

    /** @brief 글을 치는 동안 의도를 보내기까지 기다리는 시간입니다. */
    const kInputDelayMs = 200;
    /** @brief 카탈로그에 없는 전역 변수 · 인자를 모으는 묶음 이름입니다. */
    const kUnknownGroupKey = '\u0000unknown';

    /** @brief id 로 요소를 찾습니다. */
    function findElement(id) {
        return document.getElementById(id);
    }

    /** @brief 요소를 만들고 클래스 · 글을 붙입니다. */
    function createElement(tagName, className, text) {
        const element = document.createElement(tagName);
        if (className !== undefined && className !== '')
            element.className = className;
        if (text !== undefined)
            element.textContent = text;
        return element;
    }

    /** @brief 확장으로 의도 하나를 보냅니다. */
    function postIntent(type, payload) {
        vscodeApi.postMessage({ type, ...payload });
    }

    /**
     * @brief 화면 하나를 맡습니다.
     */
    class LaunchArgumentView {
        constructor() {
            /** @brief 마지막으로 받은 카탈로그입니다. */
            this._catalog = null;
            /** @brief 마지막으로 받은 화면 상태입니다. */
            this._state = null;
            /** @brief 지금 그린 카탈로그 번호입니다. */
            this._renderedCatalogVersion = -1;
            /** @brief 행 키(`gv:이름|변형` · `arg:이름`) → 행 기록입니다. */
            this._mapRow = new Map();
            /** @brief 행 키 → 미뤄 둔 값 보내기 타이머입니다. */
            this._mapInputTimer = new Map();
            /** @brief 탭 · 필터 · 접힌 묶음입니다. */
            this._uiState = this._restoreUiState();
        }

        /** @brief 고정 요소에 이벤트를 걸고 확장에 준비됐다고 알립니다. */
        initialize() {
            for (const tab of document.querySelectorAll('.tab'))
                tab.addEventListener('click', () => this._selectTab(tab.dataset.tab));
            findElement('searchInput').value = this._uiState.search;
            findElement('enabledOnlyToggle').checked = this._uiState.bEnabledOnly;
            findElement('hideTaggedToggle').checked = this._uiState.bHideTagged;
            findElement('searchInput').addEventListener('input', (event) => this._updateUiState({ search: event.target.value }));
            findElement('enabledOnlyToggle').addEventListener('change', (event) => this._updateUiState({ bEnabledOnly: event.target.checked }));
            findElement('hideTaggedToggle').addEventListener('change', (event) => this._updateUiState({ bHideTagged: event.target.checked }));

            findElement('copyButton').addEventListener('click', () => postIntent('copyCommandLine', {}));
            findElement('debugButton').addEventListener('click', () => postIntent('debug', {}));
            findElement('runButton').addEventListener('click', () => postIntent('run', {}));
            findElement('disableAllButton').addEventListener('click', () => postIntent('disableAll', {}));

            const addCustom = () => {
                const input = findElement('customInput');
                if (input.value.trim() === '')
                    return;
                postIntent('addCustomArgument', { text: input.value });
                input.value = '';
            };
            findElement('customAddButton').addEventListener('click', addCustom);
            findElement('customInput').addEventListener('keydown', (event) => {
                if (event.key === 'Enter')
                    addCustom();
            });

            const addEnvironment = () => {
                const nameInput = findElement('environmentNameInput');
                const valueInput = findElement('environmentValueInput');
                if (nameInput.value.trim() === '')
                    return;
                postIntent('addEnvironment', { name: nameInput.value, value: valueInput.value });
                nameInput.value = '';
                valueInput.value = '';
                nameInput.focus();
            };
            findElement('environmentAddButton').addEventListener('click', addEnvironment);
            findElement('environmentValueInput').addEventListener('keydown', (event) => {
                if (event.key === 'Enter')
                    addEnvironment();
            });

            const savePreset = () => {
                const input = findElement('presetNameInput');
                if (input.value.trim() === '')
                    return;
                postIntent('savePreset', { name: input.value });
                input.value = '';
            };
            findElement('presetSaveButton').addEventListener('click', savePreset);
            findElement('presetNameInput').addEventListener('keydown', (event) => {
                if (event.key === 'Enter')
                    savePreset();
            });

            window.addEventListener('message', (event) => this._onMessage(event.data));
            this._selectTab(this._uiState.tab);
            postIntent('ready', {});
        }

        /** @brief 확장이 보낸 메시지입니다. */
        _onMessage(message) {
            if (message === null || typeof message !== 'object' || message.type !== 'state')
                return;
            this._state = message;
            if (message.catalog !== undefined && message.catalogVersion !== this._renderedCatalogVersion) {
                this._catalog = message.catalog;
                this._renderedCatalogVersion = message.catalogVersion;
                this._renderCatalog();
            }
            this._applyState();
        }

        /** @brief 저장된 화면 설정을 읽습니다. 없으면 기본값입니다. */
        _restoreUiState() {
            const saved = vscodeApi.getState();
            const uiState = { tab: 'globalVariable', search: '', bEnabledOnly: false, bHideTagged: false, listCollapsedGroup: [] };
            if (saved !== null && typeof saved === 'object')
                Object.assign(uiState, saved);
            return uiState;
        }

        /** @brief 화면 설정을 고치고 저장한 뒤 필터를 다시 적용합니다. */
        _updateUiState(patch) {
            Object.assign(this._uiState, patch);
            vscodeApi.setState(this._uiState);
            this._applyFilter();
        }

        /** @brief 탭을 바꿉니다. 필터 줄은 목록 탭에서만 보입니다. */
        _selectTab(tabName) {
            for (const tab of document.querySelectorAll('.tab'))
                tab.classList.toggle('active', tab.dataset.tab === tabName);
            for (const page of document.querySelectorAll('.tab-page'))
                page.classList.toggle('active', page.dataset.page === tabName);
            findElement('filterBar').hidden = tabName !== 'globalVariable' && tabName !== 'argument';
            this._uiState.tab = tabName;
            vscodeApi.setState(this._uiState);
        }

        // ------------------------------------------------------------------
        // 카탈로그 → 행 (카탈로그가 바뀔 때만)
        // ------------------------------------------------------------------

        /** @brief 전역 변수 · 인자 · 배타 묶음 행을 처음부터 만듭니다. */
        _renderCatalog() {
            this._mapRow.clear();
            this._renderGlobalVariableList();
            this._renderExclusiveGroupList();
            this._renderArgumentList();
        }

        /** @brief 전역 변수를 모듈(과 변형) 묶음으로 나눠 그립니다. */
        _renderGlobalVariableList() {
            const container = findElement('globalVariableList');
            container.replaceChildren();
            const mapGroup = new Map();
            for (const variable of this._catalog.listGlobalVariable) {
                const groupKey = variable.variantName === '' ? variable.moduleName : `${variable.moduleName}/${variable.variantName}`;
                if (mapGroup.has(groupKey) === false)
                    mapGroup.set(groupKey, []);
                mapGroup.get(groupKey).push(variable);
            }
            for (const groupKey of Array.from(mapGroup.keys()).sort()) {
                const body = this._appendGroup(container, groupKey, this._makeGroupLabel(groupKey));
                for (const variable of mapGroup.get(groupKey))
                    body.appendChild(this._createEntryRow(`gv:${variable.name}|${variable.variantName}`, 'globalVariable', variable));
            }
            this._appendGroup(container, kUnknownGroupKey, '카탈로그에 없음 (소스에서 사라졌거나 늦게 등록되는 것)');
        }

        /** @brief 배타 묶음마다 고르기 칸 하나를 그립니다. */
        _renderExclusiveGroupList() {
            const container = findElement('exclusiveGroupList');
            container.replaceChildren();
            for (const group of this._catalog.listExclusiveGroup) {
                const row = createElement('div', 'exclusive-row');
                const label = createElement('label', '', group.label);
                const select = createElement('select', 'input');
                select.appendChild(new Option(group.defaultLabel === '' ? '(지정 안 함)' : `(지정 안 함 — 기본 ${group.defaultLabel})`, ''));
                for (const argument of this._catalog.listArgument) {
                    if (argument.group === group.id)
                        select.appendChild(new Option(`${argument.label}  (${this._makeFlagText(argument.listSpelling[0])})`, argument.name));
                }
                select.addEventListener('change', () => postIntent('setExclusiveChoice', { groupId: group.id, name: select.value }));
                row.append(label, select);
                container.appendChild(row);
                this._mapRow.set(`group:${group.id}`, { kind: 'group', element: row, select, group });
            }
        }

        /** @brief 배타 묶음에 들지 않은 인자를 그립니다. */
        _renderArgumentList() {
            const container = findElement('argumentList');
            container.replaceChildren();
            const body = this._appendGroup(container, '\u0000argument', '인자');
            for (const argument of this._catalog.listArgument) {
                if (argument.group === '')
                    body.appendChild(this._createEntryRow(`arg:${argument.name}`, 'argument', argument));
            }
            this._appendGroup(container, `${kUnknownGroupKey}/argument`, '카탈로그에 없음');
        }

        /** @brief 접을 수 있는 묶음을 붙이고 그 몸통을 돌려줍니다. */
        _appendGroup(container, groupKey, label) {
            const details = createElement('details', 'group');
            details.open = this._uiState.listCollapsedGroup.includes(groupKey) === false;
            details.dataset.groupKey = groupKey;
            const summary = createElement('summary', '', label);
            summary.appendChild(createElement('span', 'count'));
            details.appendChild(summary);
            const body = createElement('div', 'group-body');
            details.appendChild(body);
            details.addEventListener('toggle', () => {
                const listCollapsed = this._uiState.listCollapsedGroup.filter((key) => key !== groupKey);
                if (details.open === false)
                    listCollapsed.push(groupKey);
                this._updateUiState({ listCollapsedGroup: listCollapsed });
            });
            container.appendChild(details);
            return body;
        }

        /** @brief 묶음 키(`모듈` · `모듈/변형`)를 화면 이름으로 바꿉니다. 프로필 `module.labels` 를 씁니다. */
        _makeGroupLabel(groupKey) {
            const mapLabel = this._state !== null && this._state.profile !== undefined ? this._state.profile.mapModuleLabel : {};
            const listPart = groupKey.split('/');
            const moduleLabel = listPart[0] === '' ? '기타' : (mapLabel[listPart[0]] || listPart[0]);
            return listPart.length > 1 ? `${moduleLabel} · ${listPart[1]}` : moduleLabel;
        }

        /** @brief 프로필 명령줄 모양의 플래그 글입니다. */
        _makeFlagText(key) {
            const commandLine = this._state !== null && this._state.profile !== undefined ? this._state.profile.commandLine : { prefix: '-', separator: '=' };
            return `${commandLine.prefix}${key}`;
        }

        /**
         * @brief 전역 변수 · 인자 행 하나를 만듭니다.
         * @param kind `globalVariable` · `argument` 입니다.
         * @param entry 카탈로그 항목입니다. 카탈로그에 없는 항목은 `{ name, bUnknown: true }` 입니다.
         */
        _createEntryRow(rowKey, kind, entry) {
            const bUnknown = entry.bUnknown === true;
            const bFlagOnly = kind === 'argument' && bUnknown === false && entry.valueKind === 'bool';
            const row = createElement('div', 'row');
            row.dataset.key = rowKey;
            const head = createElement('div', 'row-head');
            const checkbox = createElement('input');
            checkbox.type = 'checkbox';
            const nameText = kind === 'argument' && bUnknown === false ? this._makeFlagText(entry.listSpelling[0]) : entry.name;
            const nameElement = createElement('span', 'row-name', nameText);
            nameElement.title = bUnknown ? entry.name : `${entry.name} — ${entry.relativePath}:${entry.lineNumber} 열기`;
            head.append(checkbox, nameElement);
            if (bUnknown === false) {
                head.appendChild(createElement('span', 'badge', kind === 'argument' ? entry.valueKind : entry.typeName));
                if (kind === 'globalVariable' && entry.tag !== '')
                    head.appendChild(createElement('span', 'badge tag', entry.tag));
                if (kind === 'argument' && entry.listSpelling.length > 1)
                    nameElement.title += `\n철자: ${entry.listSpelling.map((spelling) => this._makeFlagText(spelling)).join(' · ')}`;
            }
            row.appendChild(head);

            const record = { kind, element: row, checkbox, entry, editor: null, resetButton: null, noteElement: null, bUnknown };
            const intentType = kind === 'globalVariable' ? 'setGlobalVariable' : 'setArgument';
            checkbox.addEventListener('change', () => postIntent(intentType, { name: entry.name, bEnabled: checkbox.checked }));
            if (bUnknown === false)
                nameElement.addEventListener('click', () => postIntent('openLocation', { relativePath: entry.relativePath, lineNumber: entry.lineNumber }));

            if (bFlagOnly === false) {
                const editorRow = createElement('div', 'row-editor');
                record.editor = this._createValueEditor(entry, bUnknown);
                const sendValue = () => {
                    clearTimeout(this._mapInputTimer.get(rowKey));
                    this._mapInputTimer.delete(rowKey);
                    postIntent(intentType, { name: entry.name, value: record.editor.value });
                };
                record.editor.addEventListener('input', () => {
                    clearTimeout(this._mapInputTimer.get(rowKey));
                    this._mapInputTimer.set(rowKey, setTimeout(sendValue, kInputDelayMs));
                });
                record.editor.addEventListener('change', sendValue);
                editorRow.appendChild(record.editor);
                if (bUnknown === false) {
                    editorRow.appendChild(createElement('span', 'row-default', `기본 ${entry.defaultText === '' ? '""' : entry.defaultText}`));
                    record.resetButton = createElement('button', 'icon-button', '↺');
                    record.resetButton.title = '기본값으로';
                    record.resetButton.addEventListener('click', () => postIntent(kind === 'globalVariable' ? 'resetGlobalVariable' : 'resetArgument', { name: entry.name }));
                    editorRow.appendChild(record.resetButton);
                }
                row.appendChild(editorRow);
            }
            if (bUnknown) {
                const removeButton = createElement('button', 'icon-button', '지우기');
                removeButton.title = '선택에서 지웁니다';
                removeButton.addEventListener('click', () => postIntent(kind === 'globalVariable' ? 'removeGlobalVariable' : 'removeArgument', { name: entry.name }));
                head.appendChild(createElement('span', 'spacer'));
                head.appendChild(removeButton);
            }
            if (bUnknown === false && entry.description !== '') {
                const description = createElement('div', 'row-description', entry.description);
                description.title = entry.description;
                row.appendChild(description);
            }
            record.noteElement = createElement('div', 'row-note');
            record.noteElement.hidden = true;
            row.appendChild(record.noteElement);
            this._mapRow.set(rowKey, record);
            return row;
        }

        /** @brief 값 종류에 맞는 편집기(bool · enum 은 고르기, 나머지는 글 칸)를 만듭니다. */
        _createValueEditor(entry, bUnknown) {
            const valueKind = bUnknown ? 'string' : entry.valueKind;
            if (valueKind === 'bool' || (valueKind === 'enum' && entry.listEnumerator.length > 0)) {
                const select = createElement('select', 'input');
                const listOption = valueKind === 'bool' ? ['true', 'false'] : entry.listEnumerator;
                for (const optionText of listOption)
                    select.appendChild(new Option(optionText, optionText));
                return select;
            }
            const input = createElement('input', 'input');
            input.type = 'text';
            input.spellcheck = false;
            if (bUnknown === false) {
                input.placeholder = entry.defaultText;
                if (valueKind === 'int' || valueKind === 'float')
                    input.inputMode = valueKind === 'int' ? 'numeric' : 'decimal';
            }
            return input;
        }

        // ------------------------------------------------------------------
        // 화면 상태 → 값 · 켜짐 (매번)
        // ------------------------------------------------------------------

        /** @brief 받은 화면 상태를 행 · 머리글 · 목록에 반영합니다. */
        _applyState() {
            if (this._catalog === null)
                return;
            const state = this._state;
            this._syncUnknownRows();
            const mapIssue = new Map();
            for (const issue of state.commandLine.listIssue)
                mapIssue.set(issue.key, issue.message);

            for (const [rowKey, record] of this._mapRow) {
                if (record.kind === 'group') {
                    const choice = state.selection.exclusiveChoice[record.group.id];
                    record.select.value = choice === undefined ? '' : choice;
                    continue;
                }
                const selectionMap = record.kind === 'globalVariable' ? state.selection.globalVariable : state.selection.argument;
                const selected = selectionMap[record.entry.name];
                const bEnabled = selected !== undefined && selected.bEnabled;
                record.checkbox.checked = bEnabled;
                record.element.classList.toggle('enabled', bEnabled);
                const value = selected !== undefined ? selected.value : this._makeInitialValue(record);
                if (record.editor !== null && document.activeElement !== record.editor && this._mapInputTimer.has(rowKey) === false) {
                    if (record.editor.tagName === 'SELECT' && Array.from(record.editor.options).some((option) => option.value === value) === false)
                        record.editor.appendChild(new Option(value, value));
                    record.editor.value = value;
                }
                const issueKey = record.kind === 'globalVariable' ? `gv:${record.entry.name}` : `arg:${record.entry.name}`;
                const issueText = bEnabled ? mapIssue.get(issueKey) : undefined;
                if (record.editor !== null)
                    record.editor.classList.toggle('invalid', issueText !== undefined);
                if (record.resetButton !== null)
                    record.resetButton.hidden = selected === undefined || selected.value === record.entry.defaultText;
                const note = record.kind === 'globalVariable' ? state.mapNote[rowKey] : undefined;
                this._applyNote(record, issueText, note);
            }
            this._applyHeader();
            this._syncCustomList();
            this._syncEnvironmentList();
            this._syncPresetList();
            this._applyEmptyStates();
            this._applyFilter();
        }

        /** @brief 처음 켤 때 들어갈 값을 화면에 미리 보입니다(확장의 `makeInitialValue` 와 같은 규칙). */
        _makeInitialValue(record) {
            const entry = record.entry;
            if (record.bUnknown)
                return '';
            if (entry.valueKind === 'bool')
                return ['1', 'true', 'yes', 'on'].includes(String(entry.defaultText).toLowerCase()) ? 'false' : 'true';
            if (entry.valueKind === 'enum' && entry.listEnumerator.length > 0 && entry.listEnumerator.includes(entry.defaultText) === false)
                return entry.listEnumerator[0];
            return entry.defaultText;
        }

        /** @brief 행 아래 알림(값 오류가 먼저, 다음은 빌드 문맥 규칙)을 고칩니다. */
        _applyNote(record, issueText, note) {
            record.element.classList.toggle('unavailable', note !== undefined && note.level === 'unavailable');
            record.bHiddenByContext = note !== undefined && note.bHidden === true;
            if (issueText !== undefined) {
                record.noteElement.className = 'row-note unavailable';
                record.noteElement.textContent = `${issueText} — 명령줄에 넣지 않았습니다`;
                record.noteElement.hidden = false;
            } else if (note !== undefined) {
                record.noteElement.className = `row-note ${note.level}`;
                record.noteElement.textContent = note.text;
                record.noteElement.hidden = false;
            } else {
                record.noteElement.hidden = true;
            }
        }

        /** @brief 카탈로그에 없는 선택 항목의 행을 맞춥니다(더하고 · 지운다). */
        _syncUnknownRows() {
            const listSpec = [
                { kind: 'globalVariable', listName: this._state.listUnknownGlobalVariable, prefix: 'gv:', suffix: '|', groupKey: kUnknownGroupKey },
                { kind: 'argument', listName: this._state.listUnknownArgument, prefix: 'arg:', suffix: '', groupKey: `${kUnknownGroupKey}/argument` },
            ];
            for (const spec of listSpec) {
                const groupBody = document.querySelector(`details[data-group-key="${CSS.escape(spec.groupKey)}"] .group-body`);
                if (groupBody === null)
                    continue;
                const uniqueWanted = new Set(spec.listName.map((name) => `${spec.prefix}${name}${spec.suffix}`));
                for (const [rowKey, record] of Array.from(this._mapRow)) {
                    if (record.bUnknown && record.kind === spec.kind && uniqueWanted.has(rowKey) === false) {
                        record.element.remove();
                        this._mapRow.delete(rowKey);
                    }
                }
                for (const name of spec.listName) {
                    const rowKey = `${spec.prefix}${name}${spec.suffix}`;
                    if (this._mapRow.has(rowKey) === false)
                        groupBody.appendChild(this._createEntryRow(rowKey, spec.kind, { name, bUnknown: true }));
                }
            }
        }

        /** @brief 머리글(빌드 문맥 · 알림 · 명령줄 · 문제 · 버튼)을 고칩니다. */
        _applyHeader() {
            const state = this._state;
            const contextLine = findElement('contextLine');
            contextLine.replaceChildren();
            const listPill = [];
            if (state.profile.name !== '')
                listPill.push({ text: state.profile.name, bMuted: true });
            if (state.context.presetName !== '')
                listPill.push({ text: state.context.presetName, bMuted: false });
            for (const text of state.profile.listStatusText)
                listPill.push({ text, bMuted: false });
            if (state.context.launchTargetName !== '')
                listPill.push({ text: `대상 ${state.context.launchTargetName}`, bMuted: false });
            if (state.context.bCmakeToolsAvailable && state.context.bKnown === false)
                listPill.push({ text: '빌드 문맥 모름 — 구성(configure) 전', bMuted: true });
            for (const pill of listPill)
                contextLine.appendChild(createElement('span', pill.bMuted ? 'pill muted' : 'pill', pill.text));

            const bannerList = findElement('bannerList');
            bannerList.replaceChildren();
            for (const banner of state.listBanner)
                bannerList.appendChild(createElement('div', `banner ${banner.level}`, banner.text));

            const body = findElement('commandLineBody');
            body.replaceChildren();
            for (const text of state.commandLine.listArgument)
                body.appendChild(createElement('span', 'token', text));
            for (const item of state.commandLine.listEnvironment)
                body.appendChild(createElement('span', 'token', `${item.name}=${item.value}`)).title = '환경 변수';
            if (state.commandLine.listArgument.length === 0 && state.commandLine.listEnvironment.length === 0)
                body.appendChild(createElement('span', 'empty', '넘길 인자가 없습니다'));
            findElement('commandLineCount').textContent = `명령줄 ${state.commandLine.listArgument.length}개` + (state.commandLine.listEnvironment.length > 0 ? ` · 환경 ${state.commandLine.listEnvironment.length}개` : '');
            body.title = state.commandLine.shellText;

            const issueList = findElement('issueList');
            issueList.replaceChildren();
            for (const issue of state.commandLine.listIssue)
                issueList.appendChild(createElement('div', 'note error', `${issue.message} — 넣지 않음`));
            findElement('powerShellNote').hidden = state.bPowerShellSplit === false;

            for (const id of ['debugButton', 'runButton', 'disableAllButton', 'copyButton'])
                findElement(id).disabled = state.bInitialized === false;
        }

        /** @brief 카탈로그가 비었을 때 왜 비었는지 보입니다. */
        _applyEmptyStates() {
            const state = this._state;
            const listPage = [
                { id: 'globalVariableList', count: this._catalog.listGlobalVariable.length, what: '전역 변수' },
                { id: 'argumentList', count: this._catalog.listArgument.length, what: '인자' },
            ];
            for (const page of listPage) {
                const container = findElement(page.id);
                let emptyElement = container.querySelector(':scope > .empty-state');
                if (page.count > 0 || state.bInitialized === false) {
                    if (emptyElement !== null)
                        emptyElement.remove();
                    continue;
                }
                if (emptyElement === null) {
                    emptyElement = createElement('div', 'empty-state');
                    container.prepend(emptyElement);
                }
                emptyElement.textContent = state.profile.bHasCatalogSource
                    ? `프로필이 가리키는 소스에서 ${page.what}를 찾지 못했습니다.`
                    : `${page.what} 목록은 설정 cmakeDebugArgs.catalog 의 프로필로 소스에서 읽습니다. 프로필이 없어도 "사용자 · 환경" 탭의 인자는 넘길 수 있습니다.`;
            }
        }

        /** @brief 검색어 · 필터로 행과 묶음을 보이거나 숨기고 개수를 고칩니다. */
        _applyFilter() {
            if (this._catalog === null || this._state === null)
                return;
            const searchText = this._uiState.search.trim().toLowerCase();
            const selection = this._state.selection;
            for (const record of this._mapRow.values()) {
                if (record.kind === 'group')
                    continue;
                const entry = record.entry;
                const selectionMap = record.kind === 'globalVariable' ? selection.globalVariable : selection.argument;
                const bEnabled = selectionMap[entry.name] !== undefined && selectionMap[entry.name].bEnabled;
                const haystack = `${entry.name} ${record.bUnknown ? '' : `${entry.description} ${(entry.listSpelling || []).join(' ')}`}`.toLowerCase();
                const bSearchMiss = searchText !== '' && haystack.includes(searchText) === false;
                const bTagged = record.kind === 'globalVariable' && record.bUnknown === false && entry.tag !== '';
                const bHidden = (record.bHiddenByContext === true && bEnabled === false)
                    || bSearchMiss
                    || (this._uiState.bEnabledOnly && bEnabled === false)
                    || (this._uiState.bHideTagged && bTagged && bEnabled === false);
                record.element.hidden = bHidden;
            }
            for (const details of document.querySelectorAll('details.group')) {
                const listRow = Array.from(details.querySelectorAll('.row'));
                const visibleCount = listRow.filter((row) => row.hidden === false).length;
                const enabledCount = listRow.filter((row) => row.classList.contains('enabled')).length;
                details.hidden = visibleCount === 0;
                details.querySelector('summary .count').textContent = `  ${enabledCount} / ${listRow.length}`;
            }
            const countEnabled = (map) => Object.values(map).filter((item) => item.bEnabled).length;
            findElement('globalVariableCount').textContent = String(countEnabled(selection.globalVariable) || '');
            const choiceCount = Object.keys(selection.exclusiveChoice).length;
            findElement('argumentCount').textContent = String(countEnabled(selection.argument) + choiceCount || '');
            const customCount = selection.listCustomArgument.filter((item) => item.bEnabled).length + selection.listEnvironment.filter((item) => item.bEnabled).length;
            findElement('customCount').textContent = String(customCount || '');
            findElement('presetCount').textContent = String(this._state.listPreset.length || '');
        }

        // ------------------------------------------------------------------
        // 사용자 인자 · 환경 변수 · 프리셋
        // ------------------------------------------------------------------

        /**
         * @brief 목록을 맞춥니다. 줄 수가 같으면 포커스 없는 칸의 값만 고치고, 다르면 다시 만듭니다.
         * @param makeRow `(item, index) → { element, listInput: [{ input, field }], checkbox }` 입니다.
         */
        _syncList(container, listItem, makeRow) {
            const listRecord = Array.isArray(container._listRecord) ? container._listRecord : null;
            if (listRecord === null || listRecord.length !== listItem.length) {
                container.replaceChildren();
                container._listRecord = [];
                for (let index = 0; index < listItem.length; index += 1) {
                    const record = makeRow(listItem[index], index);
                    container.appendChild(record.element);
                    container._listRecord.push(record);
                }
                if (listItem.length === 0)
                    container.appendChild(createElement('div', 'hint', '(없음)'));
                return;
            }
            for (let index = 0; index < listItem.length; index += 1) {
                const record = container._listRecord[index];
                const item = listItem[index];
                record.checkbox.checked = item.bEnabled;
                for (const binding of record.listInput) {
                    if (document.activeElement !== binding.input)
                        binding.input.value = item[binding.field];
                }
            }
        }

        /** @brief 사용자 인자 목록입니다. */
        _syncCustomList() {
            const listItem = this._state.selection.listCustomArgument;
            const container = findElement('customList');
            this._syncList(container, listItem, (item, index) => {
                const element = createElement('div', 'list-row');
                const checkbox = createElement('input');
                checkbox.type = 'checkbox';
                checkbox.checked = item.bEnabled;
                const input = createElement('input', 'input');
                input.value = item.text;
                input.spellcheck = false;
                const removeButton = createElement('button', 'icon-button', '×');
                removeButton.title = '지우기';
                checkbox.addEventListener('change', () => postIntent('setCustomArgument', { index, bEnabled: checkbox.checked }));
                input.addEventListener('change', () => postIntent('setCustomArgument', { index, text: input.value }));
                removeButton.addEventListener('click', () => postIntent('removeCustomArgument', { index }));
                element.append(checkbox, input, removeButton);
                return { element, checkbox, listInput: [{ input, field: 'text' }] };
            });
        }

        /** @brief 환경 변수 목록입니다. */
        _syncEnvironmentList() {
            const listItem = this._state.selection.listEnvironment;
            const container = findElement('environmentList');
            this._syncList(container, listItem, (item, index) => {
                const element = createElement('div', 'list-row');
                const checkbox = createElement('input');
                checkbox.type = 'checkbox';
                checkbox.checked = item.bEnabled;
                const nameInput = createElement('input', 'input');
                nameInput.value = item.name;
                const valueInput = createElement('input', 'input');
                valueInput.value = item.value;
                const removeButton = createElement('button', 'icon-button', '×');
                removeButton.title = '지우기';
                checkbox.addEventListener('change', () => postIntent('setEnvironment', { index, bEnabled: checkbox.checked }));
                nameInput.addEventListener('change', () => postIntent('setEnvironment', { index, name: nameInput.value }));
                valueInput.addEventListener('change', () => postIntent('setEnvironment', { index, value: valueInput.value }));
                removeButton.addEventListener('click', () => postIntent('removeEnvironment', { index }));
                element.append(checkbox, nameInput, valueInput, removeButton);
                return { element, checkbox, listInput: [{ input: nameInput, field: 'name' }, { input: valueInput, field: 'value' }] };
            });
        }

        /** @brief 프리셋 목록입니다(입력 칸이 없어 매번 다시 만든다). */
        _syncPresetList() {
            const container = findElement('presetList');
            container.replaceChildren();
            if (this._state.listPreset.length === 0) {
                container.appendChild(createElement('div', 'hint', '(저장된 프리셋 없음)'));
                return;
            }
            for (const preset of this._state.listPreset) {
                const row = createElement('div', 'preset-row');
                const head = createElement('div', 'preset-head');
                const name = createElement('span', 'preset-name', preset.name);
                name.title = preset.createdAt;
                const loadButton = createElement('button', 'icon-button', '불러오기');
                const overwriteButton = createElement('button', 'icon-button', '덮어쓰기');
                overwriteButton.title = '지금 선택으로 덮어씁니다';
                const deleteButton = createElement('button', 'icon-button', '지우기');
                loadButton.addEventListener('click', () => postIntent('loadPreset', { name: preset.name }));
                overwriteButton.addEventListener('click', () => postIntent('savePreset', { name: preset.name }));
                deleteButton.addEventListener('click', () => postIntent('deletePreset', { name: preset.name }));
                head.append(name, createElement('span', 'badge', `${preset.argumentCount}`), createElement('span', 'spacer'), loadButton, overwriteButton, deleteButton);
                row.append(head, createElement('div', 'preset-text', preset.shellText === '' ? '(인자 없음)' : preset.shellText));
                container.appendChild(row);
            }
        }
    }

    new LaunchArgumentView().initialize();
})();
