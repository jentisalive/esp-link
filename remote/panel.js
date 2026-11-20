'use strict';

const $ = id => document.getElementById(id);
const pending = new Map();
let serial = 0;
window.espReply = (id, response) => {
    const callback = pending.get(id);
    if (callback) callback(response);
};
function request(op, body = '') {
    return new Promise((resolve, reject) => {
        const id = ++serial;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error('Нет ответа')); }, 900);
        pending.set(id, response => {
            clearTimeout(timer);
            pending.delete(id);
            if (response.code === 200) resolve(response.data || {});
            else reject(Object.assign(new Error('Нет подтверждения'), {code: response.code}));
        });
        try {
            if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.remote) window.webkit.messageHandlers.remote.postMessage({id, op, body});
            else window.RemoteNative.send(id, op, body);
        } catch (_) { window.espReply(id, {code: 0}); }
    });
}

const fixed = () => ({stick: [.24,.51], bucket: [.79,.54]});
let layout = fixed();
let layoutChanged = false;

const zone = $('controls'), stick = $('stick'), bucket = $('bucket'), nub = $('nub');
let board = null, editing = false, pointer = null, drag = null, departed = false, statusPending = false;
const landscape = () => innerWidth > innerHeight;
const interactive = () => landscape() && !document.hidden && !departed;
const message = text => { $('message').textContent = text; };
const drive = new Drive(request, () => { center(); message('Тяга отключена · коснись джойстика заново'); });

function rect(element, center) {
    const w = element.offsetWidth, h = element.offsetHeight;
    return {x: center[0] * zone.clientWidth - w / 2, y: center[1] * zone.clientHeight - h / 2, w, h};
}
function place() {
    let a = rect(stick, layout.stick), b = rect(bucket, layout.bucket);
    if (!canPlace(a,b,zone.clientWidth,zone.clientHeight) || !canPlace(b,a,zone.clientWidth,zone.clientHeight)) {
        const d = fixed();
        a = rect(stick, d.stick); b = rect(bucket, d.bucket);
    }
    for (const [el,r] of [[stick,a],[bucket,b]]) Object.assign(el.style,{left:`${r.x}px`,top:`${r.y}px`});
}
function render(text = '') {
    const visible = interactive() && ((board && board.motors) || editing);
    stick.hidden = bucket.hidden = !visible;
    bucket.disabled = !editing && !(board && board.bucket);
    $('prompt').hidden = visible;
    $('prompt').textContent = !landscape() ? 'Поверни телефон горизонтально.' : text || 'Проверяем прошивку…';
    $('edit').disabled = !interactive();
    $('edit').textContent = editing ? 'Готово' : 'Раскладка';
    $('reset').hidden = !editing || !interactive();
    zone.classList.toggle('edit', editing);
    zone.classList.toggle('prompt', !visible);
    if (visible) place();
}
function center() {
    pointer = null;
    nub.style.transform = '';
}
function stop() { center(); return drive.stop(); }
window.espSuspend = () => { departed = true; board = null; $('battery').textContent = 'АКБ —'; render(); stop(); };
window.espResume = () => { departed = false; render(); refresh(); };

function updateStick(event) {
    const r = stick.getBoundingClientRect();
    const x = (event.clientX - r.left - r.width / 2) / (r.width * .34);
    const y = (event.clientY - r.top - r.height / 2) / (r.height * .34);
    const radius = Math.hypot(x,y), scale = radius ? Math.max(0, (Math.min(radius,1) - .13) / .87) / radius : 0;
    nub.style.transform = `translate(${x * scale * r.width * .34}px,${y * scale * r.height * .34}px)`;
    drive.point(Math.round(x * scale * 100), Math.round(-y * scale * 100));
}
for (const el of [stick,bucket]) {
    el.addEventListener('pointerdown', event => {
        if (!interactive() || drag || (el === stick && pointer !== null) || event.button !== 0) return;
        if (editing) {
            const r = el.getBoundingClientRect();
            drag = {id:event.pointerId, el, dx:event.clientX-r.left, dy:event.clientY-r.top};
            el.setPointerCapture(event.pointerId);
        } else if (el === stick && board && board.motors) {
            pointer = event.pointerId;
            el.setPointerCapture(pointer);
            drive.start().then(ok => { if (!ok && pointer === event.pointerId) center(); });
            updateStick(event);
        }
    });
    el.addEventListener('pointermove', event => {
        if (drag && drag.id === event.pointerId && drag.el === el) {
            const r = zone.getBoundingClientRect(), other = el === stick ? bucket : stick;
            const next = {x:event.clientX-r.left-drag.dx, y:event.clientY-r.top-drag.dy, w:el.offsetWidth, h:el.offsetHeight};
            if (canPlace(next,{x:other.offsetLeft,y:other.offsetTop,w:other.offsetWidth,h:other.offsetHeight},zone.clientWidth,zone.clientHeight)) {
                layout = {...layout,[el.id]:[(next.x+next.w/2)/zone.clientWidth,(next.y+next.h/2)/zone.clientHeight]};
                place();
            }
        } else if (pointer === event.pointerId && el === stick) updateStick(event);
    });
    for (const kind of ['pointerup','pointercancel','lostpointercapture']) el.addEventListener(kind,event => {
        if (drag && drag.id === event.pointerId) { drag = null; save(); }
        if (pointer === event.pointerId) stop();
    });
}
function save() {
    layoutChanged = true;
    request('save', JSON.stringify(layout)).catch(() => message('Не удалось сохранить на телефоне'));
}
bucket.addEventListener('click',() => { if (!editing && interactive() && board && board.bucket) drive.bucket(); });
$('stop').onclick = () => { stop(); message('Запрошен стоп · возможен выбег'); };
$('back').onclick = () => { window.espSuspend(); request('back').catch(() => {}); };
$('edit').onclick = () => { stop(); editing = !editing; drag = null; render(); };
$('reset').onclick = () => { layout = fixed(); place(); save(); };
window.addEventListener('resize', () => { if (!landscape()) { stop(); drag = null; } render(); });
window.addEventListener('pagehide',window.espSuspend);
document.addEventListener('visibilitychange', () => { if (document.hidden) stop(); else refresh(); });

async function refresh() {
    if (!interactive() || statusPending) return;
    statusPending = true;
    try {
        const data = await request('status');
        if (!interactive()) return;
        if (data.version !== 1) throw Object.assign(new Error('Firmware version'), {code:404});
        board = data;
        $('battery').textContent = data.mv > 0 ? `АКБ ${(data.mv/1000).toFixed(2)} В${data.low ? ' · разряд' : ''}` : 'АКБ нет данных';
        $('battery').classList.toggle('low', Boolean(data.low));
        message(!data.motors ? 'Привод недоступен' : data.bucket ? 'Wi-Fi · локально' : 'Ковш: требуется настройка по USB');
        render(!data.motors ? 'Привод недоступен.' : '');
    } catch (error) {
        if (drive.intent) stop();
        board = null;
        $('battery').textContent = 'АКБ —';
        render(error.code === 404 ? 'Обнови прошивку ESP32.' : 'Нет связи с ESP32.');
    } finally { statusPending = false; }
}
setInterval(() => { if (interactive() && !editing) drive.tick(); }, 65);
setInterval(refresh, 1100);
render();
refresh();
request('layout').then(data => {
    const saved = JSON.parse(data.layout || 'null');
    if (!layoutChanged && saved && ['stick','bucket'].every(k => Array.isArray(saved[k]) && saved[k].length === 2 && saved[k].every(n => Number.isFinite(n) && n >= 0 && n <= 1))) {
        layout = saved;
        render();
    }
}).catch(() => {});
