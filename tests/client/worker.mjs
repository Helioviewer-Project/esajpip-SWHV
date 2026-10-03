// Browser worker transport for Node tests; the application worker is unchanged.
import { Worker as NodeWorker } from "node:worker_threads";

export class Worker {
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
}
