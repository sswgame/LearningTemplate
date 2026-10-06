'use strict';

/**
 * @file LaunchArgumentUtilTest.js
 * @brief 선택 ↔ 명령줄 규칙 시험입니다. 프로필의 명령줄 모양(접두사 · 구분자)과 규칙(`rules`)을 따르는지 봅니다.
 */

const assert = require('node:assert/strict');
const test = require('node:test');

const CatalogProfile = require('../Source/CatalogProfile');
const LaunchArgumentUtil = require('../Source/LaunchArgumentUtil');
const { ValueKind, kGeneralGroup } = require('../Source/CatalogScanner');

const kOptions = Object.freeze({ commandLine: { prefix: '-', separator: '=' }, namePrefix: 'gv_', mapCacheValue: {} });

/** @brief 시험용 작은 카탈로그입니다. */
function makeCatalog() {
    const makeVariable = (name, valueKind, defaultText, extra) => ({
        name, typeName: valueKind, valueKind, defaultText, description: '', tag: '', moduleName: 'Engine', variantName: '', variantCacheVariable: '',
        relativePath: 'Source/Engine/A.cpp', lineNumber: 1, listEnumerator: [], ...extra,
    });
    const makeArgument = (name, listSpelling, valueKind, defaultText, group) => ({
        name, listSpelling, valueKind, defaultText, description: '', group, label: name, relativePath: 'args.xxx', lineNumber: 1,
    });
    return {
        listGlobalVariable: [
            makeVariable('gv_flag', ValueKind.Boolean, 'false'),
            makeVariable('gv_mode', ValueKind.Integer, '0'),
            makeVariable('gv_backend', ValueKind.Enum, 'DirectX12', { listEnumerator: ['DirectX11', 'DirectX12'] }),
            makeVariable('gv_scene', ValueKind.String, ''),
            makeVariable('gv_editorOnly', ValueKind.Integer, '0', { moduleName: 'Editor', tag: 'test' }),
            makeVariable('gv_gameOnly', ValueKind.Integer, '0', { moduleName: 'Games', variantName: 'NileCity', variantCacheVariable: 'SW_ACTIVE_GAME' }),
        ],
        listArgument: [
            makeArgument('WIDTH', ['W'], ValueKind.Integer, '1280', kGeneralGroup),
            makeArgument('DIRECTX_11', ['dx11', 'd3d11'], ValueKind.Boolean, 'false', 'rhi'),
            makeArgument('DIRECTX_12', ['dx12'], ValueKind.Boolean, 'false', 'rhi'),
            makeArgument('ENABLE_EDITOR', ['EnableEditor'], ValueKind.Boolean, 'false', kGeneralGroup),
        ],
        listExclusiveGroup: [{ id: 'rhi', label: 'RHI', defaultLabel: 'DirectX12' }],
        listProblem: [],
    };
}

test('validateValue checks each value kind', () => {
    assert.deepEqual(LaunchArgumentUtil.validateValue(ValueKind.Boolean, 'ON', []), { bValid: true, value: 'true', message: '' });
    assert.equal(LaunchArgumentUtil.validateValue(ValueKind.Boolean, 'ture', []).bValid, false);
    assert.equal(LaunchArgumentUtil.validateValue(ValueKind.Integer, '-3', []).value, '-3');
    assert.equal(LaunchArgumentUtil.validateValue(ValueKind.Integer, '1.5', []).bValid, false);
    assert.equal(LaunchArgumentUtil.validateValue(ValueKind.Integer, '2147483648', []).bValid, false);
    assert.equal(LaunchArgumentUtil.validateValue(ValueKind.Float, '.25', []).bValid, true);
    assert.equal(LaunchArgumentUtil.validateValue(ValueKind.Enum, 'Vulkan', ['DirectX11']).bValid, false);
    assert.equal(LaunchArgumentUtil.validateValue(ValueKind.Enum, '1', ['DirectX11']).bValid, true);
    assert.equal(LaunchArgumentUtil.validateValue(ValueKind.String, 'a\nb', []).bValid, false);
});

test('splitArgumentText follows the profile prefix and separator', () => {
    assert.deepEqual(LaunchArgumentUtil.splitArgumentText('--gv_scene=a=b', kOptions.commandLine), { key: 'gv_scene', value: 'a=b', bHasPrefix: true });
    assert.deepEqual(LaunchArgumentUtil.splitArgumentText('-dx12', kOptions.commandLine), { key: 'dx12', value: null, bHasPrefix: true });
    assert.deepEqual(LaunchArgumentUtil.splitArgumentText('/W:800', { prefix: '/', separator: ':' }), { key: 'W', value: '800', bHasPrefix: true });
    assert.deepEqual(LaunchArgumentUtil.splitArgumentText('W:800', { prefix: '/', separator: ':' }).bHasPrefix, false);
    assert.equal(LaunchArgumentUtil.makeValueText('W', '800', { prefix: '/', separator: ':' }), '/W:800');
});

test('composeCommandLine orders exclusive groups, arguments, variables and custom text', () => {
    const catalog = makeCatalog();
    const selection = LaunchArgumentUtil.makeEmptySelection();
    selection.mapExclusiveChoice.set('rhi', 'DIRECTX_11');
    selection.mapArgument.set('ENABLE_EDITOR', { bEnabled: true, value: 'true', spelling: 'EnableEditor' });
    selection.mapArgument.set('WIDTH', { bEnabled: true, value: '1920', spelling: 'W' });
    selection.mapGlobalVariable.set('gv_scene', { bEnabled: true, value: 'game/a.scene.xml' });
    selection.mapGlobalVariable.set('gv_flag', { bEnabled: true, value: 'yes' });
    selection.mapGlobalVariable.set('gv_mode', { bEnabled: false, value: '2' });
    selection.listCustomArgument.push({ text: ' -custom ', bEnabled: true }, { text: '-off', bEnabled: false });
    const composed = LaunchArgumentUtil.composeCommandLine(catalog, selection, kOptions);
    assert.deepEqual(composed.listArgument, ['-dx11', '-W=1920', '-EnableEditor', '-gv_flag=true', '-gv_scene=game/a.scene.xml', '-custom']);
    assert.deepEqual(composed.listIssue, []);

    const slashOptions = { ...kOptions, commandLine: { prefix: '/', separator: ':' } };
    assert.deepEqual(LaunchArgumentUtil.composeCommandLine(catalog, selection, slashOptions).listArgument.slice(0, 2), ['/dx11', '/W:1920']);
});

test('composeCommandLine leaves out invalid values and reports them', () => {
    const catalog = makeCatalog();
    const selection = LaunchArgumentUtil.makeEmptySelection();
    selection.mapGlobalVariable.set('gv_mode', { bEnabled: true, value: 'two' });
    selection.mapArgument.set('WIDTH', { bEnabled: true, value: '', spelling: 'W' });
    const composed = LaunchArgumentUtil.composeCommandLine(catalog, selection, kOptions);
    assert.deepEqual(composed.listArgument, []);
    assert.deepEqual(composed.listIssue.map((issue) => issue.key), ['arg:WIDTH', 'gv:gv_mode']);
});

test('importCommandLine classifies text and keeps unknown text as custom arguments', () => {
    const catalog = makeCatalog();
    const previous = LaunchArgumentUtil.makeEmptySelection();
    previous.mapGlobalVariable.set('gv_mode', { bEnabled: true, value: '5' });
    previous.listCustomArgument.push({ text: '-remembered', bEnabled: false });
    previous.listEnvironment.push({ name: 'OFF_VAR', value: '1', bEnabled: false });
    const selection = LaunchArgumentUtil.importCommandLine(
        ['-d3d11', '-W=800', '-gv_flag', '-gv_fromModule=3', 'free text', '-EnableEditor'],
        [{ name: 'SW_X', value: '1' }],
        catalog,
        previous,
        kOptions,
    );
    assert.equal(selection.mapExclusiveChoice.get('rhi'), 'DIRECTX_11');
    assert.deepEqual(selection.mapArgument.get('WIDTH'), { bEnabled: true, value: '800', spelling: 'W' });
    assert.deepEqual(selection.mapGlobalVariable.get('gv_flag'), { bEnabled: true, value: 'true' });
    assert.deepEqual(selection.mapGlobalVariable.get('gv_fromModule'), { bEnabled: true, value: '3' }, 'a variable the catalog does not know yet is still a variable (name_prefix)');
    assert.deepEqual(selection.mapGlobalVariable.get('gv_mode'), { bEnabled: false, value: '5' }, 'a variable missing from the command line is off but keeps its value');
    assert.deepEqual(selection.listCustomArgument, [{ text: 'free text', bEnabled: true }, { text: '-remembered', bEnabled: false }]);
    assert.deepEqual(selection.listEnvironment, [{ name: 'SW_X', value: '1', bEnabled: true }, { name: 'OFF_VAR', value: '1', bEnabled: false }]);
    assert.equal(previous.mapGlobalVariable.get('gv_mode').bEnabled, true, 'the previous selection is not modified');

    const noPrefixSelection = LaunchArgumentUtil.importCommandLine(['-gv_fromModule=3'], [], catalog, LaunchArgumentUtil.makeEmptySelection(), { ...kOptions, namePrefix: '' });
    assert.deepEqual(noPrefixSelection.listCustomArgument, [{ text: '-gv_fromModule=3', bEnabled: true }], 'without a name prefix only catalog names are variables');
});

test('compose after import gives the same command line back', () => {
    const catalog = makeCatalog();
    const listText = ['-dx12', '-W=640', '-EnableEditor', '-gv_backend=DirectX11', '-gv_mode=1', '-x'];
    const selection = LaunchArgumentUtil.importCommandLine(listText, [], catalog, LaunchArgumentUtil.makeEmptySelection(), kOptions);
    assert.deepEqual(LaunchArgumentUtil.composeCommandLine(catalog, selection, kOptions).listArgument, listText);
});

test('adoptKnownCustomArguments moves pasted known text into its own slot', () => {
    const catalog = makeCatalog();
    const selection = LaunchArgumentUtil.makeEmptySelection();
    for (const text of LaunchArgumentUtil.splitCommandLineText('-dx11 "-gv_scene=game/a b.xml" -unknown'))
        selection.listCustomArgument.push({ text, bEnabled: true });
    assert.equal(LaunchArgumentUtil.adoptKnownCustomArguments(selection, catalog, kOptions), true);
    assert.equal(selection.mapExclusiveChoice.get('rhi'), 'DIRECTX_11');
    assert.equal(selection.mapGlobalVariable.get('gv_scene').value, 'game/a b.xml');
    assert.deepEqual(selection.listCustomArgument, [{ text: '-unknown', bEnabled: true }]);
});

test('makeInitialValue flips a bool default and keeps other defaults', () => {
    const catalog = makeCatalog();
    assert.equal(LaunchArgumentUtil.makeInitialValue(catalog.listGlobalVariable[0]), 'true');
    assert.equal(LaunchArgumentUtil.makeInitialValue(catalog.listGlobalVariable[1]), '0');
});

test('computeGlobalVariableNote applies variant folders and profile rules', () => {
    const catalog = makeCatalog();
    const profile = CatalogProfile.normalizeProfile({
        rules: [
            { when: { cache_variable: 'SHIP', is_true: true }, match: { tag: 'test' }, level: 'unavailable', message: 'not in shipping' },
            { match: { module: 'Editor' }, unless_argument: 'EnableEditor', level: 'warning', message: 'needs editor' },
        ],
    });
    const editorOnly = catalog.listGlobalVariable.find((variable) => variable.name === 'gv_editorOnly');
    const gameOnly = catalog.listGlobalVariable.find((variable) => variable.name === 'gv_gameOnly');
    const devContext = { bKnown: true, mapCacheValue: { SHIP: 'OFF', SW_ACTIVE_GAME: 'Empty' } };
    const shipContext = { bKnown: true, mapCacheValue: { SHIP: 'ON', SW_ACTIVE_GAME: 'Empty' } };
    const unknownContext = { bKnown: false, mapCacheValue: {} };
    const emptySelection = LaunchArgumentUtil.makeEmptySelection();
    const editorSelection = LaunchArgumentUtil.makeEmptySelection();
    editorSelection.mapArgument.set('ENABLE_EDITOR', { bEnabled: true, value: 'true', spelling: 'EnableEditor' });

    assert.equal(LaunchArgumentUtil.computeGlobalVariableNote(gameOnly, profile, devContext, emptySelection, catalog).bHidden, true);
    assert.equal(LaunchArgumentUtil.computeGlobalVariableNote(gameOnly, profile, unknownContext, emptySelection, catalog), null);
    assert.equal(LaunchArgumentUtil.computeGlobalVariableNote(editorOnly, profile, shipContext, editorSelection, catalog).text, 'not in shipping');
    assert.equal(LaunchArgumentUtil.computeGlobalVariableNote(editorOnly, profile, devContext, emptySelection, catalog).text, 'needs editor');
    assert.equal(LaunchArgumentUtil.computeGlobalVariableNote(editorOnly, profile, devContext, editorSelection, catalog), null);
});

test('PowerShell split detection matches what CMake Tools leaves unquoted', () => {
    assert.equal(LaunchArgumentUtil.hasPowerShellSplitArgument(['-gv_screenshot=out.ppm']), true);
    assert.equal(LaunchArgumentUtil.hasPowerShellSplitArgument(['-gv_scene=a b.xml']), false, 'a space makes CMake Tools quote it');
    assert.equal(LaunchArgumentUtil.hasPowerShellSplitArgument(['-gv_mode=2', 'file.txt']), false);
    assert.equal(LaunchArgumentUtil.makeShellCommandLine(['-dx12', '-gv_screenshot=out.ppm', '-gv_s=a b']), '-dx12 "-gv_screenshot=out.ppm" "-gv_s=a b"');
});
