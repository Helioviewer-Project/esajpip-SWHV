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
                await assert.rejects(channel[method](0, { reduce }), RangeError);
        for (const options of [null, 2, [], { reduce: null }, { reduce: undefined },
                               { fit: [1, 1], reduce: 0 }, { size: [1, 1] },
                               { fit: [0, 1] }, { fit: [-1, 1] }, { fit: [NaN, 1] },
                               { fit: [Infinity, 1] }, { fit: [1] }, { fit: [1, 2, 3] },
                               { fit: [, 1] }, { fit: ["1", 1] }])
            for (const method of ["frame", "fetch"])
                await assert.rejects(channel[method](channel.frames - 1, options), RangeError);
        assert.equal(requests.length, 0, "invalid calls sent a request");
        assert.equal(channel.received, received, "invalid calls sent a request");
        const first = await channel.frame(0, { reduce: Infinity });
        assert.equal(first.reduce, channel.cached(0));
        assert.equal(channel.received, received, "cached frame sent a request");

        console.log(`${channel.frames} frame(s)`);
        for (let reduce = first.reduce; reduce >= 0; reduce--)
            report(channel, await channel.frame(0, { reduce }));
        const full = await channel.frame(0);
        const beforeFit = requests.length;
        for (let reduce = 0; reduce < full.resolutions; reduce++) {
            const width = Math.ceil(full.fullWidth / 2 ** reduce);
            const height = Math.ceil(full.fullHeight / 2 ** reduce);
            // One limiting dimension; the other has ample unused space.
            // Covering the raw rectangle would wrongly select a finer level.
            for (const fit of [[width, full.fullHeight * 4], [full.fullWidth * 4, height]]) {
                const frame = await channel.frame(0, { fit });
                assert.equal(frame.reduce, reduce, `wrong fit level for ${fit}`);
                const explicit = await channel.frame(0, { reduce });
                assert.deepEqual(frame.pixels, explicit.pixels);
            }
            if (reduce > 0 && width < full.fullWidth) {
                const frame = await channel.frame(0, { fit: [width + 0.25, full.fullHeight * 4] });
                assert.equal(frame.reduce, reduce - 1, "fit did not cross a resolution boundary");
            }
        }
        assert.equal((await channel.frame(0, { fit: [full.fullWidth * 2, full.fullHeight * 2] })).reduce, 0);
        assert.equal((await channel.frame(0, { fit: [0.25, 0.25] })).reduce, full.resolutions - 1);
        assert.equal(requests.length, beforeFit, "fit requested already cached levels");
        for (let index = 1; index < channel.frames; index++) {
            const start = requests.length;
            const options = { fit: [20, 30] };
            const cached = await channel.fetch(index, options);
            assert.equal(requests.length - start, 2, `frame ${index} needs a header and pixel request`);
            const header = requests[start].searchParams;
            assert.equal(header.get("stream"), String(index));
            assert.equal(header.get("layers"), "0");
            assert.equal(header.has("fsiz"), false);
            const received = channel.received;
            const frame = await channel.frame(index, options);
            const scale = Math.min(20 / frame.fullWidth, 30 / frame.fullHeight);
            if (frame.reduce > 0) {
                assert.ok(frame.width >= frame.fullWidth * scale);
                assert.ok(frame.height >= frame.fullHeight * scale);
            }
            if (frame.reduce + 1 < frame.resolutions)
                assert.ok(Math.ceil(frame.fullWidth / 2 ** (frame.reduce + 1)) < frame.fullWidth * scale ||
                          Math.ceil(frame.fullHeight / 2 ** (frame.reduce + 1)) < frame.fullHeight * scale,
                          "fit did not select the coarsest usable level");
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
        await assert.rejects(source.frame(0, { reduce: NaN }), RangeError);
        await assert.rejects(source.fetch(0, { fit: [0, 1] }), RangeError);
        const first = await source.frame(0, { reduce: Infinity });
        assert.equal(first.reduce, await source.cached(0));
        assert.equal(first.pixels.length, first.width * first.height * first.components);
        const received = source.received;
        const repeated = await source.frame(0, { fit: [first.width, first.height] });
        assert.equal(repeated.reduce, first.reduce);
        assert.deepEqual(repeated.pixels, first.pixels);
        assert.equal(source.received, received);
        const cached = await source.fetch(0, { fit: [first.fullWidth, first.fullHeight] });
        assert.equal(cached, 0);
        const afterFetch = source.received;
        const full = await source.frame(0, { fit: [first.fullWidth, first.fullHeight] });
        assert.equal(full.reduce, 0);
        assert.equal(source.received, afterFetch);
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
