'use strict';

class Drive {
    constructor(request, onError = () => {}) {
        this.request = request;
        this.onError = onError;
        this.intent = null;
        this.pending = null;
        this.stopping = null;
    }
    async start(steering = true) {
        if (this.intent || this.stopping || this.pending) return false;
        const intent = this.intent = {id: 0, seq: 0, x: 0, y: 0, bucket: false, steering};
        try {
            const {id} = await this.send('arm');
            if (this.intent !== intent) return false;
            if (!Number.isSafeInteger(id) || id < 1) throw new Error('Invalid lease');
            intent.id = id;
            return true;
        } catch (error) {
            if (this.intent === intent) await this.fail(error);
            return false;
        }
    }
    send(op, body = '') {
        const running = this.request(op, body);
        this.pending = running;
        const clear = () => { if (this.pending === running) this.pending = null; };
        running.then(clear, clear);
        return running;
    }
    point(x, y) {
        if (this.intent && this.intent.steering) Object.assign(this.intent, {x, y});
    }
    async tick() {
        const i = this.intent;
        if (!i || !i.id || this.pending || this.stopping) return;
        const toggle = i.bucket ? 1 : 0;
        i.bucket = false;
        try {
            await this.send('frame', [i.id, ++i.seq, i.x, i.y, toggle].join(','));
        } catch (error) {
            if (this.intent === i) await this.fail(error);
        }
    }
    async bucket() {
        if (this.intent) { this.intent.bucket = true; return; }
        if (!await this.start(false)) return;
        this.intent.bucket = true;
        await this.tick();
        if (this.intent) await this.stop();
    }
    fail(error) { this.onError(error); return this.stop(); }
    stop() {
        this.intent = null;
        if (!this.stopping) {
            this.stopping = this.request('stop', '').catch(() => {}).then(() => { this.stopping = null; });
        }
        return this.stopping;
    }
}

function canPlace(a, b, width, height) {
    return a.x >= 0 && a.y >= 0 && a.x + a.w <= width && a.y + a.h <= height &&
        (a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
}

if (typeof module !== 'undefined') module.exports = {Drive, canPlace};
