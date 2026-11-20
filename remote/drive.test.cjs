const test = require('node:test');
const assert = require('node:assert/strict');
const { Drive, canPlace } = require('./drive.js');

test('release while arming suppresses the first frame', async () => {
    let armed;
    const calls = [];
    const drive = new Drive((op, body) => {
        calls.push([op, body]);
        if (op === 'arm') return new Promise(resolve => { armed = resolve; });
        return Promise.resolve({});
    });
    const touching = drive.start();
    drive.point(100, 100);
    const release = drive.stop();
    armed({id: 42});
    await Promise.all([touching, release]);
    assert.deepEqual(calls.map(([op]) => op), ['arm', 'stop']);
});

test('a rejected frame ends the gesture without repeating it', async () => {
    const calls = [];
    const drive = new Drive(async (op, body) => {
        calls.push([op, body]);
        if (op === 'arm') return {id: 42};
        if (op === 'frame') throw new Error('expired');
        return {};
    });
    await drive.start();
    drive.point(100, 100);
    await drive.tick();
    await drive.tick();
    assert.deepEqual(calls, [['arm', ''], ['frame', '42,1,100,100,0'], ['stop', '']]);
});

test('stop bypasses a stalled frame without starting another frame', async () => {
    const calls = [];
    let delivered;
    const drive = new Drive((op, body) => {
        calls.push(op);
        if (op === 'frame') return new Promise(resolve => { delivered = resolve; });
        return Promise.resolve(op === 'arm' ? {id:42} : {});
    });
    await drive.start();
    drive.point(0,100);
    const future = drive.tick();
    const stopping = drive.stop();
    assert.deepEqual(calls, ['arm','frame','stop']);
    assert.equal(await drive.start(), false);
    delivered({});
    await Promise.all([future, stopping]);
});

test('stationary bucket toggles only once and never requests thrust', async () => {
    const calls = [];
    const drive = new Drive(async (op, body) => {
        calls.push([op, body]);
        return op === 'arm' ? {id: 42} : {};
    });
    const tilting = drive.bucket();
    assert.equal(await drive.start(), false);
    drive.point(100, 100);
    await tilting;
    await drive.tick();
    assert.deepEqual(calls, [['arm', ''], ['frame', '42,1,0,0,1'], ['stop', '']]);
});

test('layout constraints reject overlaps and off-screen placement', () => {
    const a = {x: 20, y: 20, w: 30, h: 30};
    assert.equal(canPlace({x: 35, y: 35, w: 30, h: 30}, a, 100, 100), false);
    assert.equal(canPlace({x: 95, y: 5, w: 20, h: 20}, a, 100, 100), false);
    assert.equal(canPlace({x: 70, y: 20, w: 20, h: 20}, a, 100, 100), true);
});
