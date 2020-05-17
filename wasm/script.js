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

const deci_from_str = (s, memory_view, i, out_max) => {
    const m = s.match(/^0*([0-9]*)$/);
    if (m === null)
        return DECI_EFORMAT;
    s = m[1];

    const ns = s.length;
    const nout = _div_ceil(ns, DECI_BASE_LOG);
    if (nout > out_max)
        return DECI_ETOOBIG;

    let si = ns;
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

const deci_to_str = (memory_view, i_begin, i_end) => {
    if (i_begin === i_end)
        return '0';

    --i_end;
    let s = memory_view[i_end].toString();

    while (i_end !== i_begin) {
        --i_end;
        s += (memory_view[i_end] + DECI_BASE).toString().slice(1);
    }

    return s;
};

const deci_normalize = (memory_view, i_begin, i_end) => {
    while (i_end !== i_begin && memory_view[i_end - 1] === 0)
        --i_end;
    return i_end;
};

const deci_zero_out = (memory_view, i_begin, i_end) => {
    for (let i = i_begin; i !== i_end; ++i)
        memory_view[i] = 0;
};

//---------------------------------------------------------------------------------------

class Span {
    constructor(i_begin, i_end) {
        this.i_begin = i_begin;
        this.i_end = i_end;
    }

    size() {
        return this.i_end - this.i_begin;
    }

    bytei_begin() {
        return this.i_begin * DECI_WORD_BYTES;
    }

    bytei_end() {
        return this.i_end * DECI_WORD_BYTES;
    }

    empty() {
        return this.i_end === this.i_begin;
    }

    resize(n) {
        this.i_end = this.i_begin + n;
    }
}

const parse_forward_span = (s, memory_view, state) => {
    const i = state.i;
    const j = deci_from_str(s, memory_view, i, state.maxi - i);
    if (j < 0)
        throw new Error(deci_strerror(j));
    state.i = j;
    return new Span(i, j);
};

const stringify_span = (memory_view, a) => {
    return deci_to_str(memory_view, a.i_begin, a.i_end);
};

const zero_out_span = (memory_view, a) => {
    deci_zero_out(memory_view, a.i_begin, a.i_end);
};

const normalize_span = (memory_view, a) => {
    a.i_end = deci_normalize(memory_view, a.i_begin, a.i_end);
};

const ACTION_add = (instance, memory_view, a, b) => {
    if (a.size() < b.size()) {
        [a, b] = [b, a];
    }

    const carry = instance.exports.deci_add(
        a.bytei_begin(), a.bytei_end(),
        b.bytei_begin(), b.bytei_end());

    if (carry)
        memory_view[a.i_end++] = 1;

    return {span: a};
};

const ACTION_sub = (instance, memory_view, a, b) => {
    let neg = false;
    if (a.size() < b.size()) {
        [a, b] = [b, a];
        neg = true;
    }

    const underflow = instance.exports.WRAPPED_deci_sub(
        a.bytei_begin(), a.bytei_end(),
        b.bytei_begin(), b.bytei_end());

    if (underflow)
        neg = !neg;

    normalize_span(memory_view, a);

    return {
        negative: neg && !a.empty(),
        span: a,
    };
};

const ACTION_mul = (instance, memory_view, a, b, outi) => {
    const r = new Span(outi, outi + a.size() + b.size());

    zero_out_span(memory_view, r);

    instance.exports.deci_mul(
        a.bytei_begin(), a.bytei_end(),
        b.bytei_begin(), b.bytei_end(),
        r.bytei_begin());

    normalize_span(memory_view, r);

    return {span: r};
};

const ACTION_div = (instance, memory_view, a, b, outi) => {
    if (b.empty())
        throw new Error('division by zero');

    const nr = instance.exports.deci_div(
        a.bytei_begin(), a.bytei_end(),
        b.bytei_begin(), b.bytei_end());

    a.resize(nr);
    normalize_span(memory_view, a);

    return {span: a};
};

const ACTION_mod = (instance, memory_view, a, b, outi) => {
    if (b.empty())
        throw new Error('division by zero');

    const nr = instance.exports.deci_mod(
        a.bytei_begin(), a.bytei_end(),
        b.bytei_begin(), b.bytei_end());

    a.resize(nr);
    normalize_span(memory_view, a);

    return {span: a};
};

//---------------------------------------------------------------------------------------

const report_error = (text) => {
    const root_div = document.getElementById('root');
    const p = _from_html('<p class="error"></p>');
    p.append(text);
    root_div.prepend(p);
};

const install_global_error_handler = () => {
    window.onerror = (error_msg, url, line_num, column_num, error_obj) => {
        report_error(`Error: ${error_msg} @ ${url}:${line_num}:${column_num}`);
        console.log('Error object:');
        console.log(error_obj);
        return false;
    };
};

const _from_html = (html) => {
    const tmpl = document.createElement('template');
    tmpl.innerHTML = html;
    return tmpl.content.firstChild;
};

class DownloadError extends Error {
    constructor(xhr) {
        super(`HTTP ${xhr.status} ${xhr.statusText}`);
        this.name = 'DownloadError';
    }
}

const downloadTheDumbWay = (src) => {
    return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open('GET', src, true);
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

const streamTheSmartWay = (src) => {
    return fetch(src);
};

const wasmInstantiateTheDumbWayFrom = async (src) => {
    const blob = await downloadTheDumbWay(src);
    const module = await WebAssembly.compile(blob);
    return await WebAssembly.instantiate(module);
};

const wasmInstantiateFrom = async (src) => {
    const proto = window.location.protocol;
    if (proto !== 'http:' && proto !== 'https:') {
        return await wasmInstantiateTheDumbWayFrom(src);
    }

    if (WebAssembly.instantiateStreaming !== undefined) {
        const stream = streamTheSmartWay(src);
        return await WebAssembly.instantiateStreaming(stream);
    }

    if (WebAssembly.compileStreaming !== undefined) {
        const stream = streamTheSmartWay(src);
        const module = await WebAssembly.compileStreaming(stream);
        return await WebAssembly.instantiate(module);
    }

    return await wasmInstantiateTheDumbWayFrom(src);
};

const async_main = async () => {
    const wasm = await wasmInstantiateFrom("./deci.wasm");
    const instance = (wasm.instance !== undefined) ? wasm.instance : wasm;

    const memory = instance.exports.memory;
    const memory_view = new DECI_UINTXX_ARRAY_CLASS(memory.buffer);

    const form = _from_html(`<form>
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

    const work = (s_n1, s_act, s_n2) => {
        const parse_state = {i: 0, maxi: 65536};
        const a_span = parse_forward_span(s_n1, memory_view, parse_state);
        const b_span = parse_forward_span(s_n2, memory_view, parse_state);

        let result;
        switch (s_act) {
        case 'add': result = ACTION_add(instance, memory_view, a_span, b_span); break;
        case 'sub': result = ACTION_sub(instance, memory_view, a_span, b_span); break;
        case 'mul': result = ACTION_mul(instance, memory_view, a_span, b_span, parse_state.i); break;
        case 'div': result = ACTION_div(instance, memory_view, a_span, b_span); break;
        case 'mod': result = ACTION_mod(instance, memory_view, a_span, b_span); break;
        default: throw new Error(`unknown action: ${s_act}`);
        }

        const abs_str = stringify_span(memory_view, result.span);
        return (result.negative ? '-' : '') + abs_str;
    };

    form.onsubmit = () => {
        const s_n1 = document.getElementById('n1').value;
        const s_act = document.getElementById('act').value;
        const s_n2 = document.getElementById('n2').value;

        const answer = document.getElementById('answer');
        answer.innerHTML = '';

        try {
            const s_result = work(s_n1, s_act, s_n2);
            answer.append(s_result);
        } catch (err) {
            const span = _from_html('<span class="error"></span>');
            span.append(`${err.name}: ${err.message}`);
            answer.appendChild(span);
        }

        return false;
    };

    const root_div = document.getElementById('root');
    root_div.innerHTML = '';
    root_div.appendChild(form);
    root_div.appendChild(_from_html(`<p>
        This is a demo of
            <a href="https://github.com/shdown/libdeci">libdeci</a>,
        a big decimal library for C, compiled for WebAssembly.</p>`));
}

document.addEventListener('DOMContentLoaded', () => {
    install_global_error_handler();
    async_main()
        .catch((err) => {
            report_error(`Error: ${err.name}: ${err.message}`);
            throw err;
        });
});
