// jpip_channel.mjs: one JPIP channel of an esajpip server: an image or a
// movie, a frame at a time, as 8-bit pixels, and each frame's XML and color
// table. A frame can be fetched ahead of being shown. This file does the
// HTTP exchange; the WebAssembly client (../hv_wasm.c) keeps the data-bins,
// writes the codestream back and decodes it. It runs in a browser, in a Web
// Worker (jpip_worker.mjs, for jpip_source.mjs) and in Node.js.

// The module is built against a WASI libc, which wants a few system calls
// at start-up and for its standard streams. It has no files and no
// environment, and what it prints is dropped.
function system(module, memory) {
    const view = () => new DataView(memory().buffer);
    const calls = {
        environ_sizes_get(count, size) {
            view().setUint32(count, 0, true);
            view().setUint32(size, 0, true);
            return 0;
        },
        environ_get: () => 0,
        fd_write(fd, vectors, count, written) {
            let total = 0;
            for (let i = 0; i < count; i++)
                total += view().getUint32(vectors + 8 * i + 4, true);
            view().setUint32(written, total, true);
            return 0;
        },
        fd_close: () => 0,
        proc_exit(status) {
            throw new Error(`the WebAssembly client exited with status ${status}`);
        },
    };
    const NOT_SUPPORTED = 52;
    const imports = {};
    for (const { module: from, name } of WebAssembly.Module.imports(module))
        (imports[from] ??= {})[name] = calls[name] ?? (() => NOT_SUPPORTED);
    return imports;
}

// A JPIP request is a set of fields (T.808 Annex C), written as the query
// of a URL: { stream: 3, fsiz: [512, 512, "closest"] } is
// "stream=3&fsiz=512,512,closest". The server reads them as they are,
// without percent-decoding.
function query(fields) {
    return Object.entries(fields)
        .map(([name, value]) => `${name}=${Array.isArray(value) ? value.join(",") : value}`)
        .join("&");
}

// The fields of a response header such as JPIP-cnew:
// "cid=5f1c,path=jpip,transport=http" is { cid: "5f1c", path: "jpip", ... }.
function header(value) {
    return Object.fromEntries((value ?? "").split(",").filter(Boolean).map(field => {
        const at = field.indexOf("=");
        return [field.slice(0, at).trim(), field.slice(at + 1).trim()];
    }));
}

// The `reduce` asked for, within what an image of `resolutions` levels
// has: at least the lowest remains.
function clamp(reduce, resolutions) {
    return Math.min(reduce, resolutions - 1);
}

// The view-window fields for a frame without its `reduce` highest
// resolutions: the frame size of that resolution exactly (T.800 B.5), so
// that the server sends those resolutions whatever its rounding.
function view(index, { fullWidth, fullHeight, resolutions }, reduce) {
    const scale = 2 ** clamp(reduce, resolutions);
    return {
        stream: index,
        fsiz: [Math.ceil(fullWidth / scale), Math.ceil(fullHeight / scale), "closest"],
    };
}

export class JpipChannel {
    #wasm;                  // the module's exports
    #server;
    #target;                // the image, until the channel exists; then its path
    #cid = null;            // the channel, once the server has assigned it
    #last = Promise.resolve();
    #failed = null;         // why the channel can make no more requests
    frames = 0;             // codestreams of the target: the frames of a movie
    received = 0;           // bytes of response bodies so far

    // Opens `image` (a path below the server's image directory) on a new
    // channel. `wasm` is esajpip_client.wasm: compiled (a
    // WebAssembly.Module), its bytes, or a URL to fetch it from. `server`
    // is the server's address, such as http://localhost:8900.
    static async open(wasm, server, image) {
        const module = wasm instanceof WebAssembly.Module ? wasm
            : await WebAssembly.compile(typeof wasm === "string" || wasm instanceof URL
                ? await (await fetch(wasm)).arrayBuffer() : wasm);
        let instance = null;
        instance = await WebAssembly.instantiate(
            module, system(module, () => instance.exports.memory));
        instance.exports._initialize();

        const channel = new JpipChannel();
        channel.#wasm = instance.exports;
        channel.#server = server.replace(/\/+$/, "");
        channel.#target = image.replace(/^\/+/, "");
        // The metadata comes with the first response, whatever it asks for:
        // this one asks for the lowest resolution of the first frame, whose
        // size the later requests start from.
        await channel.#request({ stream: 0, fsiz: [1, 1, "closest"] });
        channel.frames = channel.#wasm.hv_wasm_codestreams();
        if (channel.frames === 0)
            throw new Error(channel.#error());
        return channel;
    }

    // A frame without its `reduce` highest resolutions (0 for the whole
    // image; each halves the size; Infinity for the lowest), fetching what
    // the channel lacks of it: { index, reduce, width, height, components,
    // pixels, fullWidth, fullHeight, resolutions }. `pixels` is the
    // caller's: a Uint8Array of `components` (1 gray, 3 RGB) values per
    // pixel, rows from the top. Calls are served one at a time, in order.
    frame(index, reduce = 0) {
        return this.#turn(
            async () => this.#decode(index, await this.#fetch(index, reduce), reduce));
    }

    // Fetches what the channel lacks of a frame without its `reduce`
    // highest resolutions, and does not decode it: a later frame() of it
    // costs no request. Gives cached(index) as it is then. Served in turn
    // with the calls of frame(): to fetch a movie ahead, wait for each
    // frame before asking for the next, so that a frame to show waits for
    // one of them at most.
    fetch(index, reduce = 0) {
        return this.#turn(async () => {
            const { resolutions, complete } = await this.#fetch(index, reduce);
            return resolutions - complete;
        });
    }

    // The least `reduce` that frame(index, reduce) costs no request for: 0
    // when the whole frame is cached, null when no level of it is.
    cached(index) {
        const { resolutions, complete } = this.#status(index);
        return complete === 0 ? null : resolutions - complete;
    }

    // The XML that describes a frame (for a FITS image, its header), or
    // null if the file has none.
    xml(index) {
        this.#checkIndex(index);
        const size = this.#wasm.hv_wasm_xml_size(index);
        if (size < 0)
            throw new Error(this.#error());
        const at = this.#wasm.hv_wasm_xml();
        return at === 0 ? null
            : new TextDecoder().decode(new Uint8Array(this.#wasm.memory.buffer, at, size));
    }

    // The color table of a frame, or null if it has none: { entries,
    // channels, table }, where `table` is the caller's: a Uint8Array of
    // `channels` values (3 for red, green, blue) for each of `entries`
    // sample values. It is not applied to the frame's pixels.
    palette(index) {
        this.#checkIndex(index);
        const entries = this.#wasm.hv_wasm_palette(index);
        if (entries < 0)
            throw new Error(this.#error());
        if (entries === 0)
            return null;
        const channels = this.#wasm.hv_wasm_palette_channels();
        return {
            entries, channels,
            table: new Uint8Array(this.#wasm.memory.buffer, this.#wasm.hv_wasm_palette_table(),
                                  entries * channels).slice(),
        };
    }

    // Closes the channel on the server, after the calls under way, and
    // forgets its data.
    close() {
        const closed = this.#last.then(async () => {
            const open = this.#failed === null;
            this.#failed = new Error("the channel is closed");
            this.#wasm.hv_wasm_reset();
            if (open && this.#cid !== null)
                await fetch(this.#url({ cid: this.#cid, cclose: this.#cid }));
        });
        this.#last = closed.catch(() => {});
        return closed;
    }

    // A channel serves one request at a time: each call waits for the one
    // before it, whether that succeeded or not.
    #turn(action) {
        const result = this.#last.then(action);
        this.#last = result.catch(() => {});
        return result;
    }

    #url(fields) {
        return `${this.#server}/${this.#target}?${query(fields)}`;
    }

    // One request for image data, its response body into the module. The
    // first asks for a channel on the image; the rest name the channel. A
    // request that fails is the channel's last: the server has ended it
    // too, or no longer agrees with the client about what was delivered.
    // What is cached stays, and is served without a request.
    async #request(fields) {
        if (this.#failed !== null)
            throw this.#failed;
        try {
            const response = await fetch(this.#url(this.#cid === null
                ? { cnew: "http", type: "jpp-stream", ...fields }
                : { cid: this.#cid, ...fields }));
            const body = new Uint8Array(await response.arrayBuffer());
            if (!response.ok)
                throw new Error(`${response.status} ${new TextDecoder().decode(body).trim()}`);
            const channel = header(response.headers.get("JPIP-cnew"));
            if (channel.cid !== undefined) {
                this.#cid = channel.cid;
                this.#target = channel.path ?? "jpip";
            }
            if (this.#cid === null)
                throw new Error("the server did not assign a channel (JPIP-cnew)");
            this.received += body.length;

            const wasm = this.#wasm;
            const at = wasm.hv_wasm_alloc(body.length);
            if (at === 0)
                throw new Error("out of memory");
            new Uint8Array(wasm.memory.buffer, at, body.length).set(body);
            const reason = wasm.hv_wasm_response(at, body.length);
            wasm.hv_wasm_free(at);
            if (reason < 0)
                throw new Error(this.#error());
        } catch (error) {
            this.#failed = error;
            throw error;
        }
    }

    // A frame as the module has it (hv_status): its size and resolution
    // levels, 0 until its header has arrived, and `complete`, how many of
    // the levels, from the lowest, are cached whole.
    #checkIndex(index) {
        if (!Number.isInteger(index) || index < 0 || index >= this.frames)
            throw new RangeError(`no frame ${index}: the image has ${this.frames}`);
    }

    #status(index) {
        this.#checkIndex(index);
        const at = this.#wasm.hv_wasm_status(index);
        if (at === 0)
            throw new Error(this.#error());
        const [fullWidth, fullHeight, , resolutions, complete] =
            new Uint32Array(this.#wasm.memory.buffer, at, 5);
        return { fullWidth, fullHeight, resolutions, complete };
    }

    // Requests what the channel lacks of a frame without its `reduce`
    // highest resolutions; gives its status then.
    async #fetch(index, reduce) {
        if (reduce !== Infinity && (!Number.isInteger(reduce) || reduce < 0))
            throw new RangeError("reduce must be a nonnegative integer or Infinity");
        const lacks = ({ resolutions, complete }) =>
            complete === 0 || resolutions - complete > clamp(reduce, resolutions);
        let status = this.#status(index);
        if (lacks(status)) {
            if (status.resolutions === 0) {
                await this.#request({ stream: index, layers: 0 });
                status = this.#status(index);
            }
            if (status.resolutions === 0)
                throw new Error(`the server did not send frame ${index}'s header`);
            await this.#request(view(index, status, reduce));
            status = this.#status(index);
            if (lacks(status))
                throw new Error(`the server did not send frame ${index} whole`);
        }
        return status;
    }

    // Decodes a frame from the cache. The pixels are copied out of the
    // module, which keeps its own until the next decode.
    #decode(index, { fullWidth, fullHeight, resolutions }, reduce) {
        const wasm = this.#wasm;
        reduce = clamp(reduce, resolutions);
        if (wasm.hv_wasm_decode(index, reduce) !== 0)
            throw new Error(this.#error());
        const width = wasm.hv_wasm_width(), height = wasm.hv_wasm_height();
        const components = wasm.hv_wasm_components();
        return {
            index, reduce, width, height, components,
            pixels: new Uint8Array(wasm.memory.buffer, wasm.hv_wasm_pixels(),
                                   width * height * components).slice(),
            fullWidth, fullHeight, resolutions,
        };
    }

    #error() {
        const bytes = new Uint8Array(this.#wasm.memory.buffer, this.#wasm.hv_wasm_error());
        return new TextDecoder().decode(bytes.subarray(0, bytes.indexOf(0)));
    }
}
