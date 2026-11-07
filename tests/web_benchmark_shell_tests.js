#!/usr/bin/env node
// Focused shell smoke with mocked browser DOM and Emscripten FS. Run with:
//   node tests/web_benchmark_shell_tests.js
// Or pass build-web/realm_client.html to check the generated shell too.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync(process.argv[2] || new URL('../web/shell.html', `file://${__dirname}/`), 'utf8');
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(match => match[1]);
assert.equal(scripts.length, 1, 'expected the inlined shell config and Module script');

function runShell(search) {
  const nodes = new Map();
  const handlers = {};
  const intervals = [];
  const blobs = [];
  function node() {
    return {
      hidden: false,
      textContent: '',
      children: [],
      classList: { added: [], add(value) { this.added.push(value); } },
      addEventListener() {},
      appendChild(child) { this.children.push(child); },
    };
  }
  const document = {
    documentElement: node(),
    getElementById(id) {
      if (!nodes.has(id)) nodes.set(id, node());
      return nodes.get(id);
    },
    createElement() { return node(); },
  };
  const window = {
    innerWidth: 1440,
    innerHeight: 900,
    devicePixelRatio: 2,
    addEventListener(name, callback) { handlers[name] = callback; },
  };
  const context = {
    window,
    document,
    location: { search, hostname: 'localhost' },
    navigator: { userAgent: 'Mock Browser/1.0' },
    URLSearchParams,
    Blob: class { constructor(parts, options) { this.parts = parts; this.options = options; } },
    URL: { createObjectURL(blob) { blobs.push(blob); return 'blob:mock-' + blobs.length; } },
    setInterval(callback, ms) { intervals.push({ callback, ms }); return intervals.length; },
    clearInterval(id) { intervals[id - 1].cleared = true; },
    console: { log() {}, error() {} },
    Date,
    JSON,
    Number,
    Object,
    String,
    Boolean,
    Math,
  };
  vm.runInNewContext(scripts.join('\n'), context, { filename: 'web/shell.html' });
  return { context, nodes, intervals, handlers, blobs };
}

const normal = runShell('?server=game.example&port=2051&appport=7777&renderer=webgpu');
assert.deepEqual(Array.from(normal.context.Module.arguments), [
  '--server', 'game.example', '--port', '2051', '--app-port', '7777', '--renderer', 'webgpu',
]);
assert.equal(normal.context.Module.noInitialRun, false);

const webgpu = runShell('?bench=mixed&renderer=webgpu&frames=240');
assert.deepEqual(Array.from(webgpu.context.Module.arguments), [
  '--renderer', 'webgpu', '--benchmark', '--frames', '240', '--width', '1280', '--height', '720',
  '--audio', 'off', '--stress', '/realm-bench/S1-mixed-dense.json', '--stress-summary',
  '/realm-bench/summary.json', '--telemetry', '/realm-bench/telemetry.jsonl', '--telemetry-warmup', '120',
]);
assert.equal(webgpu.context.Module.noInitialRun, false);
assert.deepEqual(webgpu.context.document.documentElement.classList.added, ['benchmark-mode']);
webgpu.context.Module.FS = {
  readFile(path) {
    if (path.endsWith('summary.json')) return '{"state_checksum":"fixture"}';
    return [
      { type: 'metadata', key: 'renderer', value: 'WebGPU' },
      { type: 'metadata', key: 'GL_renderer', value: 'Mock Adapter' },
    ].map(row => JSON.stringify(row)).join('\n');
  },
};
webgpu.context.Module.onRuntimeInitialized();
assert.match(webgpu.nodes.get('benchmark-message').textContent, /Running S1-mixed-dense/);
webgpu.intervals[0].callback();
assert.match(webgpu.nodes.get('benchmark-message').textContent, /Active renderer: WebGPU/);
assert.equal(webgpu.nodes.get('benchmark-downloads').children.length, 3);
assert.equal(webgpu.intervals[0].cleared, true);
assert.deepEqual(JSON.parse(webgpu.blobs[2].parts[0]).runtime_errors, []);

const webgl2 = runShell('?bench=models&renderer=webgl2');
assert.ok(Array.from(webgl2.context.Module.arguments).includes('gl'));
assert.ok(Array.from(webgl2.context.Module.arguments).includes('/realm-bench/models-repeated.json'));
assert.ok(Array.from(webgl2.context.Module.arguments).includes('600'));
webgl2.context.Module.FS = {
  readFile(path) {
    if (path.endsWith('summary.json')) return '{}';
    return [
      { type: 'metadata', key: 'renderer', value: 'OpenGL' },
      { type: 'metadata', key: 'renderer_fallback', value: 'WebGPU startup failed' },
    ].map(row => JSON.stringify(row)).join('\n');
  },
};
webgl2.context.Module.onRuntimeInitialized();
webgl2.intervals[0].callback();
assert.match(webgl2.nodes.get('benchmark-message').textContent, /WebGL2/);

const webgpuFallback = runShell('?bench=mixed&renderer=webgpu');
webgpuFallback.context.Module.FS = {
  readFile(path) {
    if (path.endsWith('summary.json')) return '{}';
    return [
      { type: 'metadata', key: 'renderer', value: 'OpenGL' },
      { type: 'metadata', key: 'renderer_fallback', value: 'WebGPU adapter unavailable' },
    ].map(row => JSON.stringify(row)).join('\n');
  },
};
webgpuFallback.context.Module.onRuntimeInitialized();
webgpuFallback.intervals[0].callback();
assert.match(webgpuFallback.nodes.get('benchmark-message').textContent, /Requested WebGPU/);
assert.match(webgpuFallback.nodes.get('benchmark-message').textContent, /adapter unavailable/);

const webgpuError = runShell('?bench=mixed&renderer=webgpu');
webgpuError.context.Module.FS = {
  readFile(path) {
    if (path.endsWith('summary.json')) return '{}';
    return JSON.stringify({ type: 'metadata', key: 'renderer', value: 'WebGPU' });
  },
};
webgpuError.context.Module.onRuntimeInitialized();
webgpuError.context.Module.printErr('WebGPU error (1): validation failure');
webgpuError.intervals[0].callback();
assert.match(webgpuError.nodes.get('benchmark-message').textContent, /validation failure/);
assert.deepEqual(JSON.parse(webgpuError.blobs[2].parts[0]).runtime_errors,
  ['WebGPU error (1): validation failure']);

for (const search of ['?bench=constructor', '?bench=mixed&frames=200', '?bench=ui&renderer=vulkan']) {
  const invalid = runShell(search);
  assert.equal(invalid.context.Module.noInitialRun, true, `${search} should not start the normal client`);
  assert.equal(Array.from(invalid.context.Module.arguments).includes('--stress'), false);
  invalid.context.Module.onRuntimeInitialized();
  assert.ok(invalid.nodes.get('benchmark-message').textContent.length > 0);
}

console.log('Web benchmark shell: normal args, bounded fixtures, renderer fallback, downloads and invalid queries pass');
