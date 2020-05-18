const DECI_BASE_LOG = 4;
const DECI_BASE = 10000;
const DECI_WORD_BYTES = 2;
const DECI_UINTXX_ARRAY_CLASS = Uint16Array;

const _div_ceil = (a, b) => Math.ceil(a / b);

const DECI_EFORMAT = -1;
const DECI_ETOOBIG = -2;

const deci_strerror = (errno) => {
    switch (errno) {
    case DECI_EFORMAT: return "invalid number format";
    case DECI_ETOOBIG: return "number is too big";
    default: return null;
    }
};

const deci_from_str = (s, memory_view, out_begin, out_end) => {
    const m = s.match(/^0*([0-9]*)$/);
    if (m === null)
        return DECI_EFORMAT;
    s = m[1];

    const ns = s.length;
    const nresult = _div_ceil(ns, DECI_BASE_LOG);
    if (nresult > (out_end - out_begin))
        return DECI_ETOOBIG;

    let si = ns;
    let i = out_begin;
    for (;;) {
        const si_1 = si - DECI_BASE_LOG;
        if (si_1 < 0)
            break;
        memory_view[i++] = parseInt(s.slice(si_1, si));
        si = si_1;
    }
    if (si !== 0) {
        memory_view[i++] = parseInt(s.slice(0, si));
    }
    return i;
};

const deci_to_str = (memory_view, begin, end) => {
    if (begin === end)
        return '0';

    --end;
    let s = memory_view[end].toString();

    while (end !== begin) {
        --end;
        s += (memory_view[end] + DECI_BASE).toString().slice(1);
    }

    return s;
};

const deci_normalize = (memory_view, begin, end) => {
    while (end !== begin && memory_view[end - 1] === 0)
        --end;
    return end;
};

const deci_zero_out = (memory_view, begin, end) => {
    for (let i = begin; i !== end; ++i)
        memory_view[i] = 0;
};

//---------------------------------------------------------------------------------------

class Span {
    constructor(begin, end) {
        this.begin = begin;
        this.end = end;
    }

    get size() {
        return this.end - this.begin;
    }

    set size(n) {
        this.end = this.begin + n;
    }

    get beginPointer() {
        return this.begin * DECI_WORD_BYTES;
    }

    get endPointer() {
        return this.end * DECI_WORD_BYTES;
    }

    get empty() {
        return this.end === this.begin;
    }
}

const spanParseForward = (s, memoryView, parseState) => {
    const begin = parseState.cur;
    const end = deci_from_str(s, memoryView, begin, parseState.max);
    if (end < 0)
        throw new Error(deci_strerror(end));
    parseState.cur = end;
    return new Span(begin, end);
};

const spanStringify = (memoryView, a) => {
    return deci_to_str(memoryView, a.begin, a.end);
};

const spanZeroOut = (memoryView, a) => {
    deci_zero_out(memoryView, a.begin, a.end);
};

const spanNormalize = (memoryView, a) => {
    a.end = deci_normalize(memoryView, a.begin, a.end);
};

const ACTION_add = (instance, memoryView, a, b) => {
    if (a.size < b.size) {
        [a, b] = [b, a];
    }

    const carry = instance.exports.deci_add(
        a.beginPointer, a.endPointer,
        b.beginPointer, b.endPointer);

    if (carry)
        memoryView[a.end++] = 1;

    return {span: a};
};

const ACTION_sub = (instance, memoryView, a, b) => {
    let neg = false;
    if (a.size < b.size) {
        [a, b] = [b, a];
        neg = true;
    }

    const underflow = instance.exports.WRAPPED_deci_sub(
        a.beginPointer, a.endPointer,
        b.beginPointer, b.endPointer);

    if (underflow)
        neg = !neg;

    spanNormalize(memoryView, a);

    return {
        negative: neg && !a.empty,
        span: a,
    };
};

const ACTION_mul = (instance, memoryView, a, b, outBegin) => {
    const r = new Span(outBegin, outBegin + a.size + b.size);

    spanZeroOut(memoryView, r);

    instance.exports.deci_mul(
        a.beginPointer, a.endPointer,
        b.beginPointer, b.endPointer,
        r.beginPointer);

    spanNormalize(memoryView, r);

    return {span: r};
};

const ACTION_div = (instance, memoryView, a, b) => {
    if (b.empty)
        throw new Error('division by zero');

    a.size = instance.exports.deci_div(
        a.beginPointer, a.endPointer,
        b.beginPointer, b.endPointer);

    spanNormalize(memoryView, a);

    return {span: a};
};

const ACTION_mod = (instance, memoryView, a, b) => {
    if (b.empty)
        throw new Error('division by zero');

    a.size = instance.exports.deci_mod(
        a.beginPointer, a.endPointer,
        b.beginPointer, b.endPointer);

    spanNormalize(memoryView, a);

    return {span: a};
};

//---------------------------------------------------------------------------------------

const fromHtml = (html) => {
    const tmpl = document.createElement('template');
    tmpl.innerHTML = html;
    return tmpl.content.firstElementChild;
};

const htmlEscape = (s) => {
    const entityMap = {
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#39;',
        '/': '&#x2F;',
        '`': '&#x60;',
        '=': '&#x3D;',
    };
    return String(s).replace(/[&<>"'`=/]/g, c => entityMap[c]);
};

//---------------------------------------------------------------------------------------

class DownloadError extends Error {
    constructor(xhr) {
        super(`HTTP ${xhr.status} ${xhr.statusText}`);
        this.name = 'DownloadError';
    }
}

const downloadBlob = (url) => {
    return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open('GET', url, true);
        xhr.responseType = 'arraybuffer';
        xhr.onload = () => {
            if (xhr.status >= 200 && xhr.status < 300) {
                resolve(xhr.response);
            } else {
                reject(new DownloadError(xhr));
            }
        };
        xhr.onerror = () => {
            reject(new DownloadError(xhr));
        };
        xhr.send(null);
    });
};

const wasmInstantiateBlob = async (url) => {
    const blob = await downloadBlob(url);
    const module = await WebAssembly.compile(blob);
    return await WebAssembly.instantiate(module);
};

const wasmInstantiate = async (url) => {
    const proto = window.location.protocol;
    if (proto !== 'http:' && proto !== 'https:') {
        // Seems like local document ('file://' protocol), fetch() just would not work.
        return await wasmInstantiateBlob(url);
    }

    if (WebAssembly.instantiateStreaming !== undefined) {
        return await WebAssembly.instantiateStreaming(fetch(url));
    }
    // Older browser.
    return await wasmInstantiateBlob(url);
};

// Compatibility shim for older browsers.
const wasmInstantiateCompat = async (url) => {
    const result = await wasmInstantiate(url);
    if (result.instance !== undefined)
        return result.instance;
    // Older browser.
    return result;
};

//---------------------------------------------------------------------------------------

const asyncMain = async () => {
    const instance = await wasmInstantiateCompat('./deci.wasm');

    const memory = instance.exports.memory;
    const memoryView = new DECI_UINTXX_ARRAY_CLASS(memory.buffer);

    const form = fromHtml(`
        <form>
            <div>
                <input
                    id="n1"
                    required
                    size="40"
                    value="76202983060594244005608103922128835">
                </input>
            </div>
            <div>
                <select id="act" required>
                    <option value="add" selected>+</option>
                    <option value="sub">-</option>
                    <option value="mul">*</option>
                    <option value="div">/</option>
                    <option value="mod">%</option>
                </select>
            </div>
            <div>
                <input
                    id="n2"
                    required
                    size="40"
                    value="998644324631202810324180654468">
                </input>
            </div>
            <div>
                <input type="submit" value="=">
                </input>
            </div>
            <div id="answer">
            </div>
        </form>
    `);

    const compute = (s1, action, s2) => {
        const parseState = {cur: 0, max: 65536};
        const a = spanParseForward(s1, memoryView, parseState);
        const b = spanParseForward(s2, memoryView, parseState);

        let result;
        switch (action) {
        case 'add': result = ACTION_add(instance, memoryView, a, b); break;
        case 'sub': result = ACTION_sub(instance, memoryView, a, b); break;
        case 'mul': result = ACTION_mul(instance, memoryView, a, b, parseState.cur); break;
        case 'div': result = ACTION_div(instance, memoryView, a, b); break;
        case 'mod': result = ACTION_mod(instance, memoryView, a, b); break;
        default: throw new Error(`unknown action: ${action}`);
        }

        const absStr = spanStringify(memoryView, result.span);
        return (result.negative ? '-' : '') + absStr;
    };

    form.onsubmit = () => {
        const n1 = document.getElementById('n1');
        const act = document.getElementById('act');
        const n2 = document.getElementById('n2');
        const answer = document.getElementById('answer');

        answer.innerHTML = '';
        try {
            const resultText = compute(n1.value, act.value, n2.value);
            answer.append(resultText);
        } catch (err) {
            answer.appendChild(fromHtml(`
                <span class="error">
                    ${htmlEscape(`${err.name}: ${err.message}`)}
                </span>`));
        }

        return false;
    };

    const rootDiv = document.getElementById('root');
    rootDiv.innerHTML = '';
    rootDiv.appendChild(form);
    rootDiv.appendChild(fromHtml(`
        <p>This is a demo of <a href="https://github.com/shdown/libdeci">libdeci</a>,
        a big decimal library for C, compiled for WebAssembly.</p>`));
}

//---------------------------------------------------------------------------------------

const reportError = (text) => {
    const rootDiv = document.getElementById('root');
    rootDiv.prepend(fromHtml(`<div class="error">${htmlEscape(text)}</div>`));
};

const installGlobalErrorHandler = () => {
    window.onerror = (errorMsg, url, lineNum, columnNum, errorObj) => {
        reportError(`Error: ${errorMsg} @ ${url}:${lineNum}:${columnNum}`);

        console.log('Error object:');
        console.log(errorObj);

        return false;
    };
};

document.addEventListener('DOMContentLoaded', () => {
    installGlobalErrorHandler();
    asyncMain().catch((err) => {
        reportError(`Error: ${err.name}: ${err.message}`);
        throw err;
    });
});
