import assert from 'node:assert/strict';
import { existsSync, readFileSync, readdirSync } from 'node:fs';

const root = new URL('../', import.meta.url);
const read = path => readFileSync(new URL(path, root), 'utf8');
const workflow = read('.github/workflows/post_release.yml');
const AsyncFunction = Object.getPrototypeOf(async function () {}).constructor;
let checks = 0;
function check(name, action) {
  action();
  checks++;
  console.log('PASS ' + name);
}
function section(job) {
  const value = workflow.split(new RegExp(`\\r?\\n  ${job}:`))[1];
  assert.ok(value, 'missing job ' + job);
  return value.split(/\r?\n  [a-z_]+:/)[0];
}
function script(job) {
  const source = section(job).split(/script: \|\r?\n/)[1].split(/\r?\n/).map(line => line.slice(12)).join('\n');
  return new AsyncFunction('github', 'context', 'require', 'core', source);
}
check('remote test entry is removed', () => {
  assert.equal(existsSync(new URL('.github/workflows/validate.yml', root)), false);
  for (const name of readdirSync(new URL('.github/workflows/', root))) {
    assert.doesNotMatch(read('.github/workflows/' + name), /workflows\/validate\.yml|\bctest\b|vmex_tests|vmex_manager_ui_tests|--self-test/);
  }
});
for (const [job, dependency] of [['build', 'prepare_release'], ['upload_asset', 'build'], ['publish_notes', 'upload_asset']]) {
  check(job + ' depends on ' + dependency, () => assert.match(section(job), new RegExp(`needs: ${dependency}\\r?\\n`)));
}
check('draft recovery covers every release stage without a test dependency', () => {
  const failure = section('draft_failed_release');
  assert.match(failure, /needs: \[prepare_release, build, upload_asset, publish_notes\]/);
  for (const job of ['prepare_release', 'build', 'upload_asset', 'publish_notes']) {
    assert.ok(failure.includes(`needs.${job}.result != 'success'`));
  }
  assert.doesNotMatch(workflow, /needs\.validate|^  validate:/m);
});
check('release build disables testing and retains package verification', () => {
  const build = read('.github/scripts/build-release.ps1');
  assert.match(build, /-DBUILD_TESTING=OFF/);
  assert.match(build, /--config Release --target package/);
  assert.match(build, /Archive differs from fresh build/);
  assert.match(build, /DiagnosticsPackageTests\.ps1/);
  assert.match(build, /DriverPackageTests\.ps1/);
  assert.doesNotMatch(build, /\bctest\b|vmex_tests|--self-test/);
});
check('normal configure defaults to tests disabled', () => assert.match(read('CMakeLists.txt'), /option\(BUILD_TESTING "[^"]+" OFF\)\s+include\(CTest\)/));
check('promotion retains release success, asset, age and blocking checks', () => {
  const promotion = read('.github/workflows/promote_prerelease.yml');
  for (const required of ["workflow_id: 'post_release.yml'", "latestRun.conclusion !== 'success'", "latestRun.status !== 'completed'", 'asset.size > 0', 'hoursSincePublish >= 48', '[no-release]', 'block-release']) assert.ok(promotion.includes(required));
});

const release = { id: 17, tag_name: 'v1.2.3', published_at: '2026-10-01T00:00:00Z', draft: false };
const context = { repo: { owner: 'fixture', repo: 'fixture' }, payload: { release } };
const notes = 'Tagged notes\n';
for (const job of ['prepare_release', 'publish_notes', 'draft_failed_release']) {
  for (const scenario of ['success', 'republished', 'permission-error']) {
    let updates = [];
    let error;
    const github = { rest: { repos: {
      getRelease: async () => ({ data: { ...release, ...(scenario === 'republished' ? { published_at: '2026-10-02T00:00:00Z' } : {}) } }),
      updateRelease: async args => {
        if (scenario === 'permission-error') throw new Error('denied');
        updates.push(args);
        return { data: { ...release, draft: args.draft ?? false } };
      },
    } } };
    const requireFixture = name => {
      assert.equal(name, 'fs');
      return { readFileSync: () => notes };
    };
    try { await script(job)(github, context, requireFixture, { info() {} }); }
    catch (caught) { error = caught; }
    check(`${job} ${scenario}`, () => {
      assert.equal(Boolean(error), scenario === 'permission-error' || (scenario === 'republished' && job !== 'draft_failed_release'));
      assert.equal(updates.length, scenario === 'success' ? 1 : 0);
      if (scenario === 'success') {
        if (job === 'draft_failed_release') assert.equal(updates[0].draft, true);
        else assert.equal(updates[0].body, job === 'prepare_release' ? '正在构建，稍后将在此显示更新日志和软件包' : notes);
      }
    });
  }
}
console.log(`${checks} checks passed.`);
