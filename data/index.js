//var hst = '192.168.1.208';
var hst = '192.168.1.152';
//var hst = '192.168.1.159';
var _rooms = [{ roomId: 0, name: 'Home' }];

var errors = [
    { code: -10, desc: "Pin setting in use for Transceiver.  Output pins cannot be re-used." },
    { code: -11, desc: "Pin setting in use for Ethernet Adapter.  Output pins cannot be re-used." },
    { code: -12, desc: "Pin setting in use on another motor.  Output pins cannot be re-used." },
    { code: -21, desc: "Git Update: Flash write failed." },
    { code: -22, desc: "Git Update: Flash erase failed." },
    { code: -23, desc: "Git Update: Flash read failed." },
    { code: -24, desc: "Git Update: Not enough space." },
    { code: -25, desc: "Git Update: Invalid file size given." },
    { code: -26, desc: "Git Update: Stream read timeout." },
    { code: -27, desc: "Git Update: MD5 check failed." },
    { code: -28, desc: "Git Update: Wrong Magic Byte." },
    { code: -29, desc: "Git Update: Could not activate firmware." },
    { code: -30, desc: "Git Update: Partition could not be found." },
    { code: -31, desc: "Git Update: Bad Argument." },
    { code: -32, desc: "Git Update: Aborted." },
    { code: -40, desc: "Git Download: Http Error." },
    { code: -41, desc: "Git Download: Buffer Allocation Error." },
    { code: -42, desc: "Git Download: Download Connection Error." },
    { code: -43, desc: 'Git Download: Timeout Error.' }
]
document.oncontextmenu = (event) => {
    if (event.target && event.target.tagName.toLowerCase() === 'input' && (event.target.type.toLowerCase() === 'text' || event.target.type.toLowerCase() === 'password'))
        return;
    else {
        event.preventDefault(); event.stopPropagation(); return false;
    }
};
Date.prototype.toJSON = function () {
    let tz = this.getTimezoneOffset();
    let sign = tz > 0 ? '-' : '+';
    let tzHrs = Math.floor(Math.abs(tz) / 60).fmt('00');
    let tzMin = (Math.abs(tz) % 60).fmt('00');
    return `${this.getFullYear()}-${(this.getMonth() + 1).fmt('00')}-${this.getDate().fmt('00')}T${this.getHours().fmt('00')}:${this.getMinutes().fmt('00')}:${this.getSeconds().fmt('00')}.${this.getMilliseconds().fmt('000')}${sign}${tzHrs}${tzMin}`;
};
Date.prototype.fmt = function (fmtMask, emptyMask) {
    if (fmtMask.match(/[hHmt]/g) !== null) { if (this.isDateTimeEmpty()) return typeof emptyMask !== 'undefined' ? emptyMask : ''; }
    if (fmtMask.match(/[Mdy]/g) !== null) { if (this.isDateEmpty()) return typeof emptyMask !== 'undefined' ? emptyMask : ''; }
    let formatted = typeof fmtMask !== 'undefined' && fmtMask !== null ? fmtMask : 'MM-dd-yyyy HH:mm:ss';
    let letters = 'dMyHhmst'.split('');
    let temp = [];
    let count = 0;
    let regexA;
    let regexB = /\[(\d+)\]/;
    let year = this.getFullYear().toString();
    let formats = {
        d: this.getDate().toString(),
        dd: this.getDate().toString().padStart(2, '00'),
        ddd: this.getDay() >= 0 ? formatType.DAYS[this.getDay()].substring(0, 3) : '',
        dddd: this.getDay() >= 0 ? formatType.DAYS[this.getDay()] : '',
        M: (this.getMonth() + 1).toString(),
        MM: (this.getMonth() + 1).toString().padStart(2, '00'),
        MMM: this.getMonth() >= 0 ? formatType.MONTHS[this.getMonth()].substring(0, 3) : '',
        MMMM: this.getMonth() >= 0 ? formatType.MONTHS[this.getMonth()] : '',
        y: year.charAt(2) === '0' ? year.charAt(4) : year.substring(2, 4),
        yy: year.substring(2, 4),
        yyyy: year,
        H: this.getHours().toString(),
        HH: this.getHours().toString().padStart(2, '00'),
        h: this.getHours() === 0 ? '12' : this.getHours() > 12 ? Math.abs(this.getHours() - 12).toString() : this.getHours().toString(),
        hh: this.getHours() === 0 ? '12' : this.getHours() > 12 ? Math.abs(this.getHours() - 12).toString().padStart(2, '00') : this.getHours().toString().padStart(2, '00'),
        m: this.getMinutes().toString(),
        mm: this.getMinutes().toString().padStart(2, '00'),
        s: this.getSeconds().toString(),
        ss: this.getSeconds().toString().padStart(2, '00'),
        t: this.getHours() < 12 || this.getHours() === 24 ? 'a' : 'p',
        tt: this.getHours() < 12 || this.getHours() === 24 ? 'am' : 'pm'
    };
    for (let i = 0; i < letters.length; i++) {
        regexA = new RegExp('(' + letters[i] + '+)');
        while (regexA.test(formatted)) {
            temp[count] = RegExp.$1;
            formatted = formatted.replace(RegExp.$1, '[' + count + ']');
            count++;
        }
    }
    while (regexB.test(formatted))
        formatted = formatted.replace(regexB, formats[temp[RegExp.$1]]);
    //console.log({ formatted: formatted, fmtMask: fmtMask });
    return formatted;
};
Number.prototype.round = function (dec) { return Number(Math.round(this + 'e' + dec) + 'e-' + dec); };
Number.prototype.fmt = function (format, empty) {
    if (isNaN(this)) return empty || '';
    if (typeof format === 'undefined') return this.toString();
    let isNegative = this < 0;
    let tok = ['#', '0'];
    let pfx = '', sfx = '', fmt = format.replace(/[^#\.0\,]/g, '');
    let dec = fmt.lastIndexOf('.') > 0 ? fmt.length - (fmt.lastIndexOf('.') + 1) : 0,
        fw = '', fd = '', vw = '', vd = '', rw = '', rd = '';
    let val = String(Math.abs(this).round(dec));
    let ret = '', commaChar = ',', decChar = '.';
    for (var i = 0; i < format.length; i++) {
        let c = format.charAt(i);
        if (c === '#' || c === '0' || c === '.' || c === ',')
            break;
        pfx += c;
    }
    for (let i = format.length - 1; i >= 0; i--) {
        let c = format.charAt(i);
        if (c === '#' || c === '0' || c === '.' || c === ',')
            break;
        sfx = c + sfx;
    }
    if (dec > 0) {
        let dp = val.lastIndexOf('.');
        if (dp === -1) {
            val += '.'; dp = 0;
        }
        else
            dp = val.length - (dp + 1);
        while (dp < dec) {
            val += '0';
            dp++;
        }
        fw = fmt.substring(0, fmt.lastIndexOf('.'));
        fd = fmt.substring(fmt.lastIndexOf('.') + 1);
        vw = val.substring(0, val.lastIndexOf('.'));
        vd = val.substring(val.lastIndexOf('.') + 1);
        let ds = val.substring(val.lastIndexOf('.'), val.length);
        for (let i = 0; i < fd.length; i++) {
            if (fd.charAt(i) === '#' && vd.charAt(i) !== '0') {
                rd += vd.charAt(i);
                continue;
            } else if (fd.charAt(i) === '#' && vd.charAt(i) === '0') {
                var np = vd.substring(i);
                if (np.match('[1-9]')) {
                    rd += vd.charAt(i);
                    continue;
                }
                else
                    break;
            }
            else if (fd.charAt(i) === '0' || fd.charAt(i) === '#')
                rd += vd.charAt(i);
        }
        if (rd.length > 0) rd = decChar + rd;
    }
    else {
        fw = fmt;
        vw = val;
    }
    var cg = fw.lastIndexOf(',') >= 0 ? fw.length - fw.lastIndexOf(',') - 1 : 0;
    var nw = Math.abs(Math.floor(this.round(dec)));
    if (!(nw === 0 && fw.substr(fw.length - 1) === '#') || fw.substr(fw.length - 1) === '0') {
        var gc = 0;
        for (let i = vw.length - 1; i >= 0; i--) {
            rw = vw.charAt(i) + rw;
            gc++;
            if (gc === cg && i !== 0) {
                rw = commaChar + rw;
                gc = 0;
            }
        }
        if (fw.length > rw.length) {
            var pstart = fw.indexOf('0');
            if (pstart >= 0) {
                var plen = fw.length - pstart;
                var pos = fw.length - rw.length - 1;
                while (rw.length < plen) {
                    let pc = fw.charAt(pos);
                    if (pc === ',') pc = commaChar;
                    rw = pc + rw;
                    pos--;
                }
            }
        }
    }
    if (isNegative) rw = '-' + rw;
    if (rd.length === 0 && rw.length === 0) return '';
    return pfx + rw + rd + sfx;
};
var baseUrl = window.location.protocol === 'file:' ? `http://${hst}` : '';
//var baseUrl = '';
function makeBool(val) {
    if (typeof val === 'boolean') return val;
    if (typeof val === 'undefined') return false;
    if (typeof val === 'number') return val >= 1;
    if (typeof val === 'string') {
        if (val === '') return false;
        switch (val.toLowerCase().trim()) {
            case 'on':
            case 'true':
            case 'yes':
            case 'y':
                return true;
            case 'off':
            case 'false':
            case 'no':
            case 'n':
                return false;
        }
        if (!isNaN(parseInt(val, 10))) return parseInt(val, 10) >= 1;
    }
    return false;
}
var httpStatusText = {
    '200': 'OK',
    '201': 'Created',
    '202': 'Accepted',
    '203': 'Non-Authoritative Information',
    '204': 'No Content',
    '205': 'Reset Content',
    '206': 'Partial Content',
    '300': 'Multiple Choices',
    '301': 'Moved Permanently',
    '302': 'Found',
    '303': 'See Other',
    '304': 'Not Modified',
    '305': 'Use Proxy',
    '306': 'Unused',
    '307': 'Temporary Redirect',
    '400': 'Bad Request',
    '401': 'Unauthorized',
    '402': 'Payment Required',
    '403': 'Forbidden',
    '404': 'Not Found',
    '405': 'Method Not Allowed',
    '406': 'Not Acceptable',
    '407': 'Proxy Authentication Required',
    '408': 'Request Timeout',
    '409': 'Conflict',
    '410': 'Gone',
    '411': 'Length Required',
    '412': 'Precondition Required',
    '413': 'Request Entry Too Large',
    '414': 'Request-URI Too Long',
    '415': 'Unsupported Media Type',
    '416': 'Requested Range Not Satisfiable',
    '417': 'Expectation Failed',
    '418': 'I\'m a teapot',
    '429': 'Too Many Requests',
    '500': 'Internal Server Error',
    '501': 'Not Implemented',
    '502': 'Bad Gateway',
    '503': 'Service Unavailable',
    '504': 'Gateway Timeout',
    '505': 'HTTP Version Not Supported'
};
function getJSON(url, cb) {
    let xhr = new XMLHttpRequest();
    console.log({ get: url });
    xhr.open('GET', baseUrl.length > 0 ? `${baseUrl}${url}` : url, true);
    xhr.setRequestHeader('apikey', security.apiKey);
    xhr.responseType = 'json';
    xhr.onload = () => {
        let status = xhr.status;
        if (status !== 200) {
            let err = xhr.response || {};
            err.htmlError = status;
            err.service = `GET ${url}`;
            if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
            cb(xhr.response, null);
        }
        else {
            cb(null, xhr.response);
        }
    };
    xhr.onerror = (evt) => {
        let err = {
            htmlError: xhr.status || 500,
            service: `GET ${url}`
        };
        if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
        cb(err, null);
    };
    xhr.send();
}
function getJSONSync(url, cb) {
    return new Promise((resolve) => {
    let overlay = ui.waitMessage(document.getElementById('divContainer'));
    let xhr = new XMLHttpRequest();
    xhr.responseType = 'json';
    xhr.onload = () => {
        let status = xhr.status;
        if (status !== 200) {
            let err = xhr.response || {};
            err.htmlError = status;
            err.service = `GET ${url}`;
            if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
            cb(xhr.response, null);
        }
        else {
            console.log({ get: url, obj:xhr.response });
            cb(null, xhr.response);
        }
        if (typeof overlay !== 'undefined') overlay.remove();
        resolve();
    };
    
    xhr.onerror = (evt) => {
        let err = {
            htmlError: xhr.status || 500,
            service: `GET ${url}`
        };
        if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
        cb(err, null);
        if (typeof overlay !== 'undefined') overlay.remove();
        resolve();
    };
    xhr.onabort = (evt) => {
        console.log('Aborted');
        if (typeof overlay !== 'undefined') overlay.remove();
        resolve();
    };
    xhr.open('GET', baseUrl.length > 0 ? `${baseUrl}${url}` : url, true);
    xhr.setRequestHeader('apikey', security.apiKey);
    xhr.send();
    });
}
function getText(url, cb) {
    let xhr = new XMLHttpRequest();
    console.log({ get: url });
    xhr.open('GET', baseUrl.length > 0 ? `${baseUrl}${url}` : url, true);
    xhr.setRequestHeader('apikey', security.apiKey);
    xhr.responseType = 'text';
    xhr.onload = () => {
        let status = xhr.status;
        if (status !== 200) {
            let err = xhr.response || {};
            err.htmlError = status;
            err.service = `GET ${url}`;
            if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
            cb(err, null);
        }
        else
            cb(null, xhr.response);
    };
    xhr.onerror = (evt) => {
        let err = {
            htmlError: xhr.status || 500,
            service: `GET ${url}`
        };
        if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
        cb(err, null);
    };
    xhr.send();
}
function postJSONSync(url, data, cb) {
    let overlay = ui.waitMessage(document.getElementById('divContainer'));
    try {
        let xhr = new XMLHttpRequest();
        console.log({ post: url, data: data });
        let fd = new FormData();
        for (let name in data) {
            fd.append(name, data[name]);
        }
        xhr.open('POST', baseUrl.length > 0 ? `${baseUrl}${url}` : url, true);
        xhr.responseType = 'json';
        xhr.setRequestHeader('Accept', 'application/json');
        xhr.setRequestHeader('apikey', security.apiKey);
        xhr.onload = () => {
            let status = xhr.status;
            console.log(xhr);
            if (status !== 200) {
                let err = xhr.response || {};
                err.htmlError = status;
                err.service = `POST ${url}`;
                err.data = data;
                if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
                cb(err, null);
            }
            else {
                cb(null, xhr.response);
            }
            overlay.remove();
        };
        xhr.onerror = (evt) => {
            console.log(xhr);
            let err = {
                htmlError: xhr.status || 500,
                service: `POST ${url}`
            };
            if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
            cb(err, null);
            overlay.remove();
        };
        xhr.send(fd);
    } catch (err) { ui.serviceError(document.getElementById('divContainer'), err); }
}
function putJSON(url, data, cb) {
    let xhr = new XMLHttpRequest();
    console.log({ put: url, data: data });
    xhr.open('PUT', baseUrl.length > 0 ? `${baseUrl}${url}` : url, true);
    xhr.responseType = 'json';
    xhr.setRequestHeader('Content-Type', 'application/json; charset=utf-8');
    xhr.setRequestHeader('Accept', 'application/json');
    xhr.setRequestHeader('apikey', security.apiKey);
    xhr.onload = () => {
        let status = xhr.status;
        if (status !== 200) {
            let err = xhr.response || {};
            err.htmlError = status;
            err.service = `PUT ${url}`;
            err.data = data;
            if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
            cb(err, null);
        }
        else {
            cb(null, xhr.response);
        }
    };
    xhr.onerror = (evt) => {
        console.log(xhr);
        let err = {
            htmlError: xhr.status || 500,
            service: `PUT ${url}`
        };
        if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
        cb(err, null);
    };
    xhr.send(JSON.stringify(data));
}
function putJSONSync(url, data, cb) {
    let overlay = ui.waitMessage(document.getElementById('divContainer'));
    try {
        let xhr = new XMLHttpRequest();
        console.log({ put: url, data: data });
        //xhr.open('PUT', url, true);
        xhr.open('PUT', baseUrl.length > 0 ? `${baseUrl}${url}` : url, true);
        xhr.responseType = 'json';
        xhr.setRequestHeader('Content-Type', 'application/json; charset=utf-8');
        xhr.setRequestHeader('Accept', 'application/json');
        xhr.setRequestHeader('apikey', security.apiKey);
        xhr.onload = () => {
            let status = xhr.status;
            if (status !== 200) {
                let err = xhr.response || {};
                err.htmlError = status;
                err.service = `PUT ${url}`;
                err.data = data;
                if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
                cb(err, null);
            }
            else {
                cb(null, xhr.response);
            }
            overlay.remove();
        };
        xhr.onerror = (evt) => {
            console.log(xhr);
            let err = {
                htmlError: xhr.status || 500,
                service: `PUT ${url}`
            };
            if (typeof err.desc === 'undefined') err.desc = xhr.statusText || httpStatusText[xhr.status || 500];
            cb(err, null);
            overlay.remove();
        };
        xhr.send(JSON.stringify(data));
    } catch (err) { ui.serviceError(document.getElementById('divContainer'), err); }
}
var socket = null;
var tConnect = null;
var sockIsOpen = false;
var connecting = false;
var connects = 0;
var connectFailed = 0;
var connectTimeout = null;
async function initSockets() {
    if (connecting || (socket && socket.readyState === WebSocket.OPEN)) {
        console.log("WS already active/connecting");
        return;
    }

    console.log('Connecting to socket...');
    connecting = true;
    if (tConnect) clearTimeout(tConnect);
    tConnect = null;
    let wms = document.getElementsByClassName('socket-wait');
    for (let i = 0; i < wms.length; i++) {
        wms[i].remove();
    }
    ui.waitMessage(document.getElementById('divContainer')).classList.add('socket-wait');
    let host = window.location.protocol === 'file:' ? hst : window.location.hostname;
    try {
        socket = new WebSocket(`ws://${host}:8080/`);
        // Safety-net: if the TCP handshake hasn't completed after 20s
        // (e.g. SYN lost due to PCB exhaustion), close and let the
        // onclose handler retry.  This is long enough for the OS TCP
        // stack to exhaust its own retransmissions on a healthy network.
        if (connectTimeout) clearTimeout(connectTimeout);
        connectTimeout = setTimeout(() => {
            if (socket && socket.readyState === WebSocket.CONNECTING) {
                console.log('WebSocket connect timeout after 20s — retrying');
                socket.close();
            }
        }, 20000);
        // The browser's TCP stack handles SYN retransmissions natively
        // (typically 1s, 2s, 4s backoff).  Do NOT force-close the socket
        // on a timer — that kills the in-progress TCP handshake and forces
        // a fresh SYN sequence, making intermittent WiFi packet loss worse.
        // If the server is truly unreachable, the browser will fire onerror
        // after its own OS-level TCP timeout (~30-75s depending on OS).
        socket.onmessage = (evt) => {
            if (evt.data.startsWith('42')) {
                let ndx = evt.data.indexOf(',');
                let eventName = evt.data.substring(3, ndx);
                let data = evt.data.substring(ndx + 1, evt.data.length - 1);
                try {
                    var reISO = /^(\d{4}|\+010000)-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2}(?:\.\d*))(?:Z|(\+|-)([\d|:]*))?$/;
                    var reMsAjax = /^\/Date\((d|-|.*)\)[\/|\\]$/;
                    var msg = JSON.parse(data, (key, value) => {
                        if (typeof value === 'string') {
                            var a = reISO.exec(value);
                            if (a) return new Date(value);
                            a = reMsAjax.exec(value);
                            if (a) {
                                var b = a[1].split(/[-+,.]/);
                                return new Date(b[0] ? +b[0] : 0 - +b[1]);
                            }
                        }
                        return value;
                    });
                    switch (eventName) {
                        case 'memStatus':
                            firmware.procMemoryStatus(msg);
                            break;
                        case 'updateProgress':
                            firmware.procUpdateProgress(msg);
                            break;
                        case 'fwStatus':
                            firmware.procFwStatus(msg);
                            break;
                        case 'remoteFrame':
                            somfy.procRemoteFrame(msg);
                            break;
                        case 'groupState':
                            somfy.procGroupState(msg);
                            break;
                        case 'shadeState':
                            somfy.procShadeState(msg);
                            break;
                        case 'shadeCommand':
                            console.log(msg);
                            break;
                        case 'roomRemoved':
                            somfy.procRoomRemoved(msg);
                            break;
                        case 'roomAdded':
                            somfy.procRoomAdded(msg);
                            break;
                        case 'shadeRemoved':
                            break;
                        case 'shadeAdded':
                            break;
                        case 'ethernet':
                            wifi.procEthernet(msg);
                            break;
                        case 'wifiStrength':
                            wifi.procWifiStrength(msg);
                            break;
                        case 'packetPulses':
                            console.log(msg);
                            break;
                        case 'frequencyScan':
                            somfy.procFrequencyScan(msg);
                            break;

                    }
                } catch (err) {
                    console.log({ eventName: eventName, data: data, err: err });
                }
            }
        };
        socket.onopen = (evt) => {
            if (tConnect) clearTimeout(tConnect);
            tConnect = null;
            if (connectTimeout) clearTimeout(connectTimeout);
            connectTimeout = null;
            console.log({ msg: 'open', evt: evt });
            sockIsOpen = true;
            connecting = false;
            connects++;
            connectFailed = 0;
            let wms = document.getElementsByClassName('socket-wait');
            for (let i = 0; i < wms.length; i++) {
                wms[i].remove();
            }
            let errs = document.getElementsByClassName('socket-error');
            for (let i = 0; i < errs.length; i++)
                errs[i].remove();
            if (general.reloadApp) {
                general.reload();
            }
            else {
                (async () => {
                    ui.clearErrors();
                    await general.loadGeneral();
                    await wifi.loadNetwork();
                    await somfy.loadSomfy();
                    await mqtt.loadMQTT();
                    if (ui.isConfigOpen()) socket.send('join:0');

                    //await general.init();
                    //await somfy.init();
                    //await mqtt.init();
                    //await wifi.init();
                })();
            }
        };
        socket.onclose = (evt) => {
            wifi.procWifiStrength({ ssid: '', channel: -1, strength: -100 });
            wifi.procEthernet({ connected: false, speed: 0, fullduplex: false });
            if (document.getElementsByClassName('socket-wait').length === 0)
                ui.waitMessage(document.getElementById('divContainer')).classList.add('socket-wait');
            if (evt.wasClean) {
                console.log({ msg: 'close-clean', evt: evt });
                connectFailed = 0;
                tConnect = setTimeout(async () => { await reopenSocket(); }, 7000);
                console.log('Reconnecting socket in 7 seconds');
            }
            else {
                console.log({ msg: 'close-died', reason: evt.reason, evt: evt, sock: socket });
                if (connects > 0) {
                    console.log('Reconnecting socket in 3 seconds');
                    tConnect = setTimeout(async () => { await reopenSocket(); }, 3000);
                }
                else {
                    if (connecting) {
                        connectFailed++;
                        let timeout = Math.min(connectFailed * 100, 10000);
                        console.log(`Initial socket did not connect, retrying in ${timeout}ms (attempt ${connectFailed})`);
                        tConnect = setTimeout(async () => { await reopenSocket(); }, timeout);
                        if (connectFailed === 5) {
                            ui.socketError('Too many clients connected.  A maximum of 5 clients may be connected at any one time.  Close some connections to the ESP Somfy RTS device to proceed.');
                        }
                        let spanAttempts = document.getElementById('spanSocketAttempts');
                        if (spanAttempts) spanAttempts.innerHTML = connectFailed.fmt("#,##0");

                    }
                    else {
                        console.log('Connecting socket in .5 seconds');
                        tConnect = setTimeout(async () => { await reopenSocket(); }, 500);
                    }

                }

            }
            connecting = false;
        };
        socket.onerror = (evt) => {
            console.log({ msg: 'socket error', evt: evt, sock: socket });
        };
    } catch (err) {
        console.log({
            msg: 'Websocket connection error', err: err
        });
        tConnect = setTimeout(async () => { await reopenSocket(); }, 5000);
    }
}
async function reopenSocket() {
    if (tConnect) clearTimeout(tConnect);
    tConnect = null;
    await initSockets();
}
async function init() {
    try {
        console.log('init: security');
        await security.init();
        // Start the WebSocket connection early so the TCP handshake
        // runs concurrently with the HTTP init requests below.
        console.log('init: sockets');
        if (security.type === 0 || (security.permissions & 0x01) === 0x01) {
            await initSockets();
        }
        console.log('init: general');
        general.init();
        console.log('init: wifi');
        wifi.init();
        if (typeof somfy === 'undefined') { console.error('somfy.js failed to load — somfy is not defined'); return; }
        console.log('init: somfy');
        somfy.init();
        if (typeof mqtt === 'undefined') { console.error('extras.js failed to load — mqtt is not defined'); return; }
        console.log('init: mqtt');
        mqtt.init();
        console.log('init: firmware');
        firmware.init();
        console.log('init: homekit icon');
        hkit.updateIcon();
        setInterval(() => hkit.updateIcon(), 30000);
    } catch (err) {
        console.error('init() failed:', err);
    }
}
window.addEventListener("beforeunload", () => {
    if (socket) socket.close();
});