'use strict';

/**
 * @file CatalogScannerTest.js
 * @brief `CatalogProfile` · `CatalogScanner` 시험입니다.
 * @details 셋을 봅니다.
 *          - 엔진과 무관한 프로필(다른 매크로 이름 · 인자 순서 · `--` 접두사)로도 같은 일을 하는가 — 확장은 특정 프로젝트를 모른다.
 *          - 이 확장이 들어 있는 실제 저장소를 `Profiles/SwEngine.json` 으로 읽어, 매크로 호출 수와 카탈로그 수가 같은가.
 *          - 파일 하나를 다시 읽는 경로.
 */

const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const test = require('node:test');

const CatalogProfile = require('../Source/CatalogProfile');
const { CatalogScanner, parseSourceText, parseArgumentText, ValueKind, kGeneralGroup } = require('../Source/CatalogScanner');

const kRepositoryRoot = path.resolve(__dirname, '..', '..', '..');
const kSwProfilePath = path.resolve(__dirname, '..', 'Profiles', 'SwEngine.json');

/** @brief 엔진과 무관한 가상 프로젝트의 프로필입니다(인자 순서가 다르다). */
function makeForeignProfile() {
    return CatalogProfile.normalizeProfile({
        global_variable: {
            files: ['src/**/*.cc'],
            macros: [{ name: 'DEFINE_CVAR', tag: '' }, { name: 'DEFINE_DEV_CVAR', tag: 'dev' }],
            argument_index: { name: 0, type: 1, default_value: 2, description: 3 },
            type_map: { 'std::string': 'string' },
        },
        argument: {
            files: ['src/flags.inc'],
            macro: 'FLAG',
            argument_index: { name: 0, default_value: 2, spelling_start: 1 },
            exclusive_groups: [{ id: 'gpu', label: 'GPU', arguments: ['USE_GL', 'USE_VK'] }],
        },
        command_line: { prefix: '--', separator: '=' },
        module: { root: 'src', variant_folders: [{ folder: 'plugins', cache_variable: 'APP_PLUGIN' }] },
    });
}

test('a foreign profile reads its own macros, argument order and exclusive group', () => {
    const profile = makeForeignProfile();
    const source = parseSourceText('src/plugins/Fog/Fog.cc', 'DEFINE_DEV_CVAR( fog_density, float, 0.25, "Fog" );\nenum class Mode { A, B };\nDEFINE_CVAR( fog_mode, Mode, Mode::B, "M" );', profile);
    assert.deepEqual(source.listGlobalVariable.map((variable) => [variable.name, variable.valueKind, variable.tag, variable.moduleName, variable.variantName, variable.variantCacheVariable]), [
        ['fog_density', ValueKind.Float, 'dev', 'plugins', 'Fog', 'APP_PLUGIN'],
        ['fog_mode', ValueKind.Enum, '', 'plugins', 'Fog', 'APP_PLUGIN'],
    ]);
    const argument = parseArgumentText('src/flags.inc', 'FLAG( USE_GL, "gl", false )\nFLAG( USE_VK, "vk", false )\nFLAG( WIDTH, "width", 640 )', profile, new Map());
    assert.deepEqual(argument.listArgument.map((item) => [item.name, item.listSpelling[0], item.valueKind]), [
        ['USE_GL', 'gl', ValueKind.Boolean],
        ['USE_VK', 'vk', ValueKind.Boolean],
        ['WIDTH', 'width', ValueKind.Integer],
    ]);
});

test('an empty profile scans nothing and reports no problem', async () => {
    const profile = CatalogProfile.makeEmptyProfile();
    assert.equal(CatalogProfile.hasCatalogSource(profile), false);
    const scanner = new CatalogScanner(kRepositoryRoot, profile);
    await scanner.scanAll();
    const catalog = scanner.makeCatalog();
    assert.deepEqual([catalog.listGlobalVariable.length, catalog.listArgument.length, catalog.listProblem.length], [0, 0, 0]);
});

test('a broken profile field is reported instead of silently ignored', () => {
    const profile = CatalogProfile.normalizeProfile({ global_variable: { type_map: { foo: 'number' } }, rules: [{ message: 'x' }] });
    assert.equal(profile.listProblem.length, 2);
});

test('makeGlobRegExp handles **, * and braces', () => {
    const globRe = CatalogProfile.makeGlobRegExp('Source/**/*.{cpp,h}');
    assert.equal(globRe.test('Source/a.cpp'), true);
    assert.equal(globRe.test('Source/x/y/a.h'), true);
    assert.equal(globRe.test('Source/x/a.inl'), false);
    assert.equal(globRe.test('Other/a.cpp'), false);
    assert.equal(CatalogProfile.getGlobBaseFolder('Source/**/*.cpp'), 'Source');
    assert.equal(CatalogProfile.getGlobBaseFolder('Config/Engine/CookContract.json'), 'Config/Engine');
});

test('the SW profile on this repository: every macro call under Source is in the catalog', async () => {
    const loaded = CatalogProfile.loadProfile(kSwProfilePath, kRepositoryRoot);
    assert.equal(loaded.problem, '');
    assert.deepEqual(loaded.profile.listProblem, []);
    const scanner = new CatalogScanner(kRepositoryRoot, loaded.profile);
    await scanner.scanAll();
    const catalog = scanner.makeCatalog();
    assert.deepEqual(catalog.listProblem, []);

    // 매크로 호출 수를 스캐너와 다른 방법(줄 단위 정규식)으로 센다 — 여러 줄 호출도 첫 줄은 한 줄이다.
    let expectedCount = 0;
    const callRe = /^\s*SW_(?:TEST_)?GLOBAL_VARIABLE(?:_SHIPPED)?\s*\(\s*[A-Za-z_]/;
    const listPending = [path.join(kRepositoryRoot, 'Source')];
    while (listPending.length > 0) {
        const directoryPath = listPending.pop();
        for (const dirent of fs.readdirSync(directoryPath, { withFileTypes: true })) {
            const entryPath = path.join(directoryPath, dirent.name);
            if (dirent.isDirectory()) {
                listPending.push(entryPath);
                continue;
            }
            if (/\.(cpp|h|inl)$/.test(dirent.name) === false)
                continue;
            for (const line of fs.readFileSync(entryPath, 'utf8').split('\n')) {
                if (callRe.test(line))
                    expectedCount += 1;
            }
        }
    }
    assert.equal(catalog.listGlobalVariable.length, expectedCount);

    const findVariable = (name) => catalog.listGlobalVariable.find((variable) => variable.name === name);
    assert.equal(findVariable('gv_viewMode').valueKind, ValueKind.Integer);
    assert.equal(findVariable('gv_viewMode').moduleName, 'Engine');
    const rhiBackend = findVariable('gv_rhiBackend');
    assert.equal(rhiBackend.valueKind, ValueKind.Enum);
    assert.deepEqual(rhiBackend.listEnumerator, ['DirectX11', 'DirectX12', 'Vulkan', 'OpenGL']);
    assert.equal(rhiBackend.defaultText, catalog.listExclusiveGroup[0].defaultLabel, 'SW_RHI_BACKEND_DEFAULT resolves through the JSON table');
    assert.equal(findVariable('gv_rhiImmediateSubmit').valueKind, ValueKind.Boolean, 'a call split over several lines is read');
    assert.equal(findVariable('gv_profileFrames').tag, 'test-shipped');
    assert.equal(findVariable('gv_tracyViewerPath').valueKind, ValueKind.String, 'sw::string maps through the profile type_map');
    assert.equal(findVariable('gv_benchMeshes').variantName, 'Empty');

    assert.deepEqual(catalog.listTag.map((tagInfo) => tagInfo.tag), ['', 'test', 'test-shipped'], 'tags in profile macro order');
    assert.equal(catalog.listTag[0].label, '일반');
    assert.equal(catalog.listTag.reduce((sum, tagInfo) => sum + tagInfo.count, 0), catalog.listGlobalVariable.length, 'every variable falls in exactly one tag');

    const width = catalog.listArgument.find((argument) => argument.name === 'WIDTH');
    assert.deepEqual(width.listSpelling, ['W']);
    assert.equal(width.group, kGeneralGroup);
    assert.deepEqual(catalog.listArgument.filter((argument) => argument.group === 'rhi').map((argument) => argument.listSpelling[0]), ['dx11', 'dx12', 'vk', 'gl']);
});

test('rescanFile picks up a new definition and forgets a removed one', async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'cmake-debug-args-'));
    try {
        const sourcePath = path.join(root, 'src', 'core', 'Probe.cc');
        fs.mkdirSync(path.dirname(sourcePath), { recursive: true });
        fs.writeFileSync(sourcePath, 'int x;\n');
        const scanner = new CatalogScanner(root, makeForeignProfile());
        await scanner.scanAll();
        assert.equal(scanner.makeCatalog().listGlobalVariable.length, 0);

        fs.writeFileSync(sourcePath, 'DEFINE_CVAR( probe, bool, false, "p" );\n');
        assert.equal(await scanner.rescanFile(sourcePath), true);
        assert.deepEqual(scanner.makeCatalog().listGlobalVariable.map((variable) => variable.name), ['probe']);

        fs.rmSync(sourcePath);
        assert.equal(await scanner.rescanFile(sourcePath), true);
        assert.equal(scanner.makeCatalog().listGlobalVariable.length, 0);
        assert.equal(await scanner.rescanFile(path.join(root, 'README.md')), false);
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
});
