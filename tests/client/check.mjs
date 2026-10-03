// Checks the channel API, movie decoding and worker against a local server:
// node tests/client/check.mjs <built esajpip_client.wasm> <server URL> <image>
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { createServer } from "node:http";
import { Worker as NodeWorker } from "node:worker_threads";
import { JpipChannel } from "../../client/js/jpip_channel.mjs";
import { JpipSource } from "../../client/js/jpip_source.mjs";

const [wasmPath, server, image] = process.argv.slice(2);
if (image === undefined)
    throw new Error("usage: check.mjs wasm server image");
const wasm = await readFile(wasmPath);

// FNV-1a, 32 bits.
function checksum(bytes) {
    let hash = 0x811c9dc5;
    for (const byte of bytes)
        hash = Math.imul(hash ^ byte, 0x01000193);
    return (hash >>> 0).toString(16).padStart(8, "0");
}

function report(channel, frame) {
    const xml = channel.xml(frame.index);
    const palette = channel.palette(frame.index);
    console.log(`frame ${frame.index}: ${frame.width}x${frame.height}, ` +
                `${frame.components} component(s), ${channel.received} bytes received, ` +
                `pixels ${checksum(frame.pixels)}, ` +
                (xml === null ? "no XML" : `XML ${new TextEncoder().encode(xml).length} bytes`) +
                (palette === null ? "" : `, color table of ${palette.entries}`));
}

// Only the browser's worker transport is adapted; jpip_worker.mjs runs unchanged.
globalThis.Worker = class {
    constructor(url) {
        this.worker = new NodeWorker(`
            const { parentPort, workerData } = require("node:worker_threads");
            globalThis.self = { postMessage: (data, moved) => parentPort.postMessage(data, moved) };
            import(workerData).then(() => {
                parentPort.on("message", data => self.onmessage({ data }));
            });
        `, { eval: true, workerData: String(url) });
        this.worker.on("message", data => this.onmessage?.({ data }));
        this.worker.on("error", error => this.onerror?.(error));
    }
    postMessage(data) { this.worker.postMessage(data); }
    terminate() { this.worker.terminate(); }
};

const host = createServer((request, response) => response.end(wasm));
await new Promise(resolve => host.listen(0, "127.0.0.1", resolve));
try {
    const channel = await JpipChannel.open(wasm, server, image);
    const requests = [];
    const originalFetch = globalThis.fetch;
    globalThis.fetch = (url, options) => {
        requests.push(new URL(url));
        return originalFetch(url, options);
    };
    try {
        const received = channel.received;
        for (const index of [-1, channel.frames, 0.5, NaN]) {
            for (const method of ["cached", "xml", "palette"])
                assert.throws(() => channel[method](index), RangeError);
            for (const method of ["frame", "fetch"])
                await assert.rejects(channel[method](index), RangeError);
        }
        for (const reduce of [-1, 0.5, NaN, "1"])
            for (const method of ["frame", "fetch"])
                await assert.rejects(channel[method](0, reduce), RangeError);
        assert.equal(channel.received, received, "invalid calls sent a request");
        const first = await channel.frame(0, Infinity);
        assert.equal(first.reduce, channel.cached(0));
        assert.equal(channel.received, received, "cached frame sent a request");

        console.log(`${channel.frames} frame(s)`);
        for (let reduce = first.reduce; reduce >= 0; reduce--)
            report(channel, await channel.frame(0, reduce));
        for (let index = 1; index < channel.frames; index++) {
            const start = requests.length;
            const cached = await channel.fetch(index, Infinity);
            assert.equal(requests.length - start, 2, `frame ${index} needs a header and pixel request`);
            const header = requests[start].searchParams;
            assert.equal(header.get("stream"), String(index));
            assert.equal(header.get("layers"), "0");
            assert.equal(header.has("fsiz"), false);
            const received = channel.received;
            const frame = await channel.frame(index, Infinity);
            const pixels = requests[start + 1].searchParams;
            assert.equal(pixels.get("stream"), String(index));
            assert.equal(pixels.has("layers"), false);
            assert.equal(pixels.get("fsiz"),
                `${Math.ceil(frame.fullWidth / 2 ** frame.reduce)},${Math.ceil(frame.fullHeight / 2 ** frame.reduce)},closest`);
            assert.equal(requests.length - start, 2, `cached frame ${index} made another request`);
            assert.equal(cached, channel.cached(index));
            assert.ok(cached <= frame.reduce, `frame ${index} was not fetched ahead`);
            assert.equal(channel.received, received, `frame ${index} sent a request after fetch`);
            report(channel, frame);
        }
    } finally {
        globalThis.fetch = originalFetch;
        await channel.close();
    }

    const source = await JpipSource.open({
        wasm: `http://127.0.0.1:${host.address().port}/client.wasm`, server, image,
    });
    try {
        for (const method of ["cached", "xml", "palette", "frame", "fetch"])
            await assert.rejects(source[method](source.frames), RangeError);
        await assert.rejects(source.frame(0, NaN), RangeError);
        const first = await source.frame(0, Infinity);
        assert.equal(first.reduce, await source.cached(0));
        assert.equal(first.pixels.length, first.width * first.height * first.components);
        const received = source.received;
        const repeated = await source.frame(0, Infinity);
        assert.deepEqual(repeated.pixels, first.pixels);
        assert.equal(source.received, received);
        await source.xml(0);
        await source.palette(0);
    } finally {
        await source.close();
    }
    await assert.rejects(source.frame(0), /source is closed/);
    console.log("Channel, decoding and worker checks passed");
} finally {
    host.closeAllConnections();
    await new Promise(resolve => host.close(resolve));
}
