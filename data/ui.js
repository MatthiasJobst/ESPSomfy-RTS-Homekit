class UIBinder {
    setValue(el, val) {
        if (el instanceof HTMLInputElement) {
            switch (el.type.toLowerCase()) {
                case 'checkbox':
                    el.checked = makeBool(val);
                    break;
                case 'range':
                    let dt = el.getAttribute('data-datatype');
                    let mult = parseInt(el.getAttribute('data-mult') || 1, 10);
                    switch (dt) {
                        // We always range with integers
                        case 'float':
                            el.value = Math.round(parseInt(val * mult, 10));
                            break;
                        case 'index':
                            let ivals = JSON.parse(el.getAttribute('data-values'));
                            for (let i = 0; i < ivals.length; i++) {
                                if (ivals[i].toString() === val.toString()) {
                                    el.value = i;
                                    break;
                                }
                            }
                            break;
                        default:
                            el.value = parseInt(val, 10) * mult;
                            break;
                    }
                    break;
                default:
                    el.value = val;
                    break;
            }
        }
        else if (el instanceof HTMLSelectElement) {
            let ndx = 0;
            for (let i = 0; i < el.options.length; i++) {
                let opt = el.options[i];
                if (opt.value === val.toString()) {
                    ndx = i;
                    break;
                }
            }
            el.selectedIndex = ndx;
        }
        else if (el instanceof HTMLElement) el.innerHTML = val;
    }
    getValue(el, defVal) {
        let val = defVal;
        if (el instanceof HTMLInputElement) {
            switch (el.type.toLowerCase()) {
                case 'checkbox':
                    val = el.checked;
                    break;
                case 'range':
                    let dt = el.getAttribute('data-datatype');
                    let mult = parseInt(el.getAttribute('data-mult') || 1, 10);
                    switch (dt) {
                        // We always range with integers
                        case 'float':
                            val = parseInt(el.value, 10) / mult;
                            break;
                        case 'index':
                            let ivals = JSON.parse(el.getAttribute('data-values'));
                            val = ivals[parseInt(el.value, 10)];
                            break;
                        default:
                            val = parseInt(el.value / mult, 10);
                            break;
                    }
                    break;
                default:
                    val = el.value;
                    break;
            }
        }
        else if (el instanceof HTMLSelectElement) val = el.value;
        else if (el instanceof HTMLElement) val = el.innerHTML;
        return val;
    }
    toElement(el, val) {
        let flds = el.querySelectorAll('*[data-bind]');
        flds.forEach((fld) => {
            let prop = fld.getAttribute('data-bind');
            let arr = prop.split('.');
            let tval = val;
            for (let i = 0; i < arr.length; i++) {
                var s = arr[i];
                if (typeof s === 'undefined' || !s) continue;
                let ndx = s.indexOf('[');
                if (ndx !== -1) {
                    ndx = parseInt(s.substring(ndx + 1, s.indexOf(']') - 1), 10);
                    s = s.substring(0, ndx - 1);
                }
                tval = tval[s];
                if (typeof tval === 'undefined') break;
                if (ndx >= 0) tval = tval[ndx];
            }
            if (typeof tval !== 'undefined') {
                if (typeof fld.val === 'function') this.val(tval);
                else {
                    switch (fld.getAttribute('data-fmttype')) {
                        case 'time':
                            {
                                var dt = new Date();
                                dt.setHours(0, 0, 0);
                                dt.addMinutes(tval);
                                tval = dt.fmt(fld.getAttribute('data-fmtmask'), fld.getAttribute('data-fmtempty') || '');
                            }
                            break;
                        case 'date':
                        case 'datetime':
                            {
                                let dt = new Date(tval);
                                tval = dt.fmt(fld.getAttribute('data-fmtmask'), fld.getAttribute('data-fmtempty') || '');
                            }
                            break;
                        case 'number':
                            if (typeof tval !== 'number') tval = parseFloat(tval);
                            tval = tval.fmt(fld.getAttribute('data-fmtmask'), fld.getAttribute('data-fmtempty') || '');
                            break;
                        case 'duration':
                            tval = ui.formatDuration(tval, $this.attr('data-fmtmask'));
                            break;
                    }
                    this.setValue(fld, tval);
                }
            }
        });
    }
    fromElement(el, obj, arrayRef) {
        if (typeof arrayRef === 'undefined' || arrayRef === null) arrayRef = [];
        if (typeof obj === 'undefined' || obj === null) obj = {};
        if (typeof el.getAttribute('data-bind') !== 'undefined') this._bindValue(obj, el, this.getValue(el), arrayRef);
        let flds = el.querySelectorAll('*[data-bind]');
        flds.forEach((fld) => {
            if (!makeBool(fld.getAttribute('data-setonly')))
                this._bindValue(obj, fld, this.getValue(fld), arrayRef);
        });
        return obj;
    }
    parseNumber(val) {
        if (val === null) return;
        if (typeof val === 'undefined') return val;
        if (typeof val === 'number') return val;
        if (typeof val.getMonth === 'function') return val.getTime();
        var tval = val.replace(/[^0-9\.\-]+/g, '');
        return tval.indexOf('.') !== -1 ? parseFloat(tval) : parseInt(tval, 10);
    }
    _bindValue(obj, el, val, arrayRef) {
        var binding = el.getAttribute('data-bind');
        var dataType = el.getAttribute('data-datatype');
        if (binding && binding.length > 0) {
            var sRef = '';
            var arr = binding.split('.');
            var t = obj;
            for (var i = 0; i < arr.length - 1; i++) {
                let s = arr[i];
                if (typeof s === 'undefined' || s.length === 0) continue;
                sRef += '.' + s;
                var ndx = s.lastIndexOf('[');
                if (ndx !== -1) {
                    var v = s.substring(0, ndx);
                    var ndxEnd = s.lastIndexOf(']');
                    var ord = parseInt(s.substring(ndx + 1, ndxEnd), 10);
                    if (isNaN(ord)) ord = 0;
                    if (typeof arrayRef[sRef] === 'undefined') {
                        if (typeof t[v] === 'undefined') {
                            t[v] = new Array();
                            t[v].push(new Object());
                            t = t[v][0];
                            arrayRef[sRef] = ord;
                        }
                        else {
                            k = arrayRef[sRef];
                            if (typeof k === 'undefined') {
                                a = t[v];
                                k = a.length;
                                arrayRef[sRef] = k;
                                a.push(new Object());
                                t = a[k];
                            }
                            else
                                t = t[v][k];
                        }
                    }
                    else {
                        k = arrayRef[sRef];
                        if (typeof k === 'undefined') {
                            a = t[v];
                            k = a.length;
                            arrayRef[sRef] = k;
                            a.push(new Object());
                            t = a[k];
                        }
                        else
                            t = t[v][k];
                    }
                }
                else if (typeof t[s] === 'undefined') {
                    t[s] = new Object();
                    t = t[s];
                }
                else
                    t = t[s];
            }
            if (typeof dataType === 'undefined') dataType = 'string';
            t[arr[arr.length - 1]] = this.parseValue(val, dataType);
        }
    }
    parseValue(val, dataType) {
        switch (dataType) {
            case 'int':
                return Math.floor(this.parseNumber(val));
            case 'uint':
                return Math.abs(this.parseNumber(val));
            case 'float':
            case 'real':
            case 'double':
            case 'decimal':
            case 'number':
                return this.parseNumber(val);
            case 'date':
                if (typeof val === 'string') return Date.parseISO(val);
                else if (typeof val === 'number') return new Date(number);
                else if (typeof val.getMonth === 'function') return val;
                return undefined;
            case 'time':
                var dt = new Date();
                if (typeof val === 'number') {
                    dt.setHours(0, 0, 0);
                    dt.addMinutes(tval);
                    return dt;
                }
                else if (typeof val === 'string' && val.indexOf(':') !== -1) {
                    var n = val.lastIndexOf(':');
                    var min = this.parseNumber(val.substring(n));
                    var nsp = val.substring(0, n).lastIndexOf(' ') + 1;
                    var hrs = this.parseNumber(val.substring(nsp, n));
                    dt.setHours(0, 0, 0);
                    if (hrs <= 12 && val.substring(n).indexOf('p')) hrs += 12;
                    dt.addMinutes(hrs * 60 + min);
                    return dt;
                }
                break;
            case 'duration':
                if (typeof val === 'number') return val;
                return Math.floor(this.parseNumber(val));
            default:
                return val;
        }
    }
    formatValue(val, dataType, fmtMask, emptyMask) {
        var v = this.parseValue(val, dataType);
        if (typeof v === 'undefined') return emptyMask || '';
        switch (dataType) {
            case 'int':
            case 'uint':
            case 'float':
            case 'real':
            case 'double':
            case 'decimal':
            case 'number':
                return v.fmt(fmtMask, emptyMask || '');
            case 'time':
            case 'date':
            case 'dateTime':
                return v.fmt(fmtMask, emptyMask || '');
        }
        return v;
    }
    waitMessage(el) {
        let div = document.createElement('div');
        div.innerHTML = '<div class="lds-roller"><div></div><div></div><div></div><div></div><div></div><div></div><div></div><div></div></div></div>';
        div.classList.add('wait-overlay');
        if (typeof el === 'undefined') el = document.getElementById('divContainer');
        el.appendChild(div);
        return div;
    }
    serviceError(el, err) {
        let title = 'Service Error'
        if (arguments.length === 1) {
            err = el;
            el = document.getElementById('divContainer');
        }
        let msg = '';
        if (typeof err === 'string' && err.startsWith('{')) {
            let e = JSON.parse(err);
            if (typeof e !== 'undefined' && typeof e.desc === 'string') msg = e.desc;
            else msg = err;
        }
        else if (typeof err === 'string') msg = err;
        else if (typeof err === 'number') {
            switch (err) {
                case 404:
                    msg = `404: Service not found`;
                    break;
                default:
                    msg = `${err}: Service Error`;
                    break;
            }
        }
        else if (typeof err !== 'undefined') {
            if (typeof err.desc === 'string') {
                msg = typeof err.desc !== 'undefined' ? err.desc : err.message;
                if (typeof err.code === 'number') {
                    let e = errors.find(x => x.code === err.code) || { code: err.code, desc: 'Unspecified error' };
                    msg = e.desc;
                    title = err.desc;
                }
            }
        }
        console.log(err);
        let div = this.errorMessage(`${err.htmlError || 500}:${title}`);
        let sub = div.querySelector('.sub-message');
        sub.innerHTML = `<div><label>Service:</label>${err.service}</div><div style="font-size:22px;">${msg}</div>`;
        return div;
    }
    socketError(el, msg) {
        if (arguments.length === 1) {
            msg = el;
            el = document.getElementById('divContainer');
        }
        let div = document.createElement('div');
        div.innerHTML = '<div id="divSocketAttempts" style="position:absolute;width:100%;left:0px;padding-right:24px;text-align:right;top:0px;font-size:18px;"><span>Attempts: </span><span id="spanSocketAttempts"></span></div><div class="inner-error"><div>Could not connect to server</div><hr></hr><div style="font-size:.7em">' + msg + '</div></div>';
        div.classList.add('error-message');
        div.classList.add('socket-error');
        div.classList.add('message-overlay');
        el.appendChild(div);
        return div;
    }
    errorMessage(el, msg) {
        if (arguments.length === 1) {
            msg = el;
            el = document.getElementById('divContainer');
        }
        let div = document.createElement('div');
        div.innerHTML = '<div class="inner-error">' + msg + '</div><div class="sub-message"></div><button type="button" onclick="ui.clearErrors();">Close</button></div>';
        div.classList.add('error-message');
        div.classList.add('message-overlay');
        el.appendChild(div);
        return div;
    }
    promptMessage(el, msg, onYes) {
        if (arguments.length === 2) {
            onYes = msg;
            msg = el;
            el = document.getElementById('divContainer');
        }
        let div = document.createElement('div');
        div.innerHTML = '<div class="prompt-text">' + msg + '</div><div class="sub-message"></div><div class="button-container"><button id="btnYes" type="button">Yes</button><button type="button" onclick="ui.clearErrors();">No</button></div></div>';
        div.classList.add('prompt-message');
        div.classList.add('message-overlay');
        el.appendChild(div);
        div.querySelector('#btnYes').addEventListener('click', onYes);
        return div;
    }
    infoMessage(el, msg, onOk) {
        if (arguments.length === 1) {
            onOk = msg;
            msg = el;
            el = document.getElementById('divContainer');
        }
        let div = document.createElement('div');
        div.innerHTML = '<div class="info-text">' + msg + '</div><div class="sub-message"></div><div class="button-container" style="text-align:center;"><button id="btnOk" type="button" style="width:40%;display:inline-block;">Ok</button></div></div>';
        div.classList.add('info-message');
        div.classList.add('message-overlay');
        el.appendChild(div);
        if (typeof onOk === 'function') div.querySelector('#btnOk').addEventListener('click', onOk);
        else div.querySelector('#btnOk').addEventListener('click', (e) => { div.remove() });
        //div.querySelector('#btnYes').addEventListener('click', onYes);
        return div;

    }
    clearErrors() {
        let errors = document.querySelectorAll('div.message-overlay');
        if (errors && errors.length > 0) errors.forEach((el) => { el.remove(); });
    }
    selectTab(elTab) {
        for (let tab of elTab.parentElement.children) {
            if (tab.classList.contains('selected')) tab.classList.remove('selected');
            document.getElementById(tab.getAttribute('data-grpid')).style.display = 'none';
        }
        if (!elTab.classList.contains('selected')) elTab.classList.add('selected');
        document.getElementById(elTab.getAttribute('data-grpid')).style.display = '';
    }
    wizSetPrevStep(el) { this.wizSetStep(el, Math.max(this.wizCurrentStep(el) - 1, 1)); }
    wizSetNextStep(el) { this.wizSetStep(el, this.wizCurrentStep(el) + 1); }
    wizSetStep(el, step) {
        let curr = this.wizCurrentStep(el);
        let max = parseInt(el.getAttribute('data-maxsteps'), 10);
        if (!isNaN(max)) {
            let next = el.querySelector(`#btnNextStep`);
            if (next) next.style.display = max < step ? 'inline-block' : 'none';
        }
        let prev = el.querySelector(`#btnPrevStep`);
        if (prev) prev.style.display = step <= 1 ? 'none' : 'inline-block';
        if (curr !== step) {
            el.setAttribute('data-stepid', step);
            let evt = new CustomEvent('stepchanged', { detail: { oldStep: curr, newStep: step }, bubbles: true, cancelable: true, composed: false });
            el.dispatchEvent(evt);
        }
    }
    wizCurrentStep(el) { return parseInt(el.getAttribute('data-stepid') || 1, 10); }
    pinKeyPressed(evt) {
        let parent = evt.srcElement.parentElement;
        let digits = parent.querySelectorAll('.pin-digit');
        switch (evt.key) {
            case 'Backspace':
                setTimeout(() => {
                    // Focus to the previous element.
                    for (let i = 0; i < digits.length; i++) {
                        if (digits[i] === evt.srcElement && i > 0) {
                            digits[i - 1].focus();
                            break;
                        }
                    }
                }, 0);
                return;
            case 'ArrowLeft':
                setTimeout(() => {
                    for (let i = 0; i < digits.length; i++) {
                        if (digits[i] === evt.srcElement && i > 0) {
                            digits[i - 1].focus();
                        }
                    }
                });
                return;
            case 'CapsLock':
            case 'Control':
            case 'Shift':
            case 'Enter':
            case 'Tab':
                return;
            case 'ArrowRight':
                if (evt.srcElement.value !== '') {
                    setTimeout(() => {
                        for (let i = 0; i < digits.length; i++) {
                            if (digits[i] === evt.srcElement && i < digits.length - 1) {
                                digits[i + 1].focus();
                            }
                        }
                    });
                }
                return;
            default:
                if (evt.srcElement.value !== '') evt.srcElement.value = '';
                setTimeout(() => {
                    let e = new CustomEvent('digitentered', { detail: {}, bubbles: true, cancelable: true, composed: false });
                    evt.srcElement.dispatchEvent(e);
                }, 100);
                break;
        }
        setTimeout(() => {
            // Focus to the first empty element.
            for (let i = 0; i < digits.length; i++) {
                if (digits[i].value === '') {
                    if (digits[i] !== evt.srcElement) digits[i].focus();
                    break;
                }
            }
        }, 0);

    }
    pinDigitFocus(evt) {
        // Find the first empty digit and place the cursor there.
        if (evt.srcElement.value !== '') return;
        let parent = evt.srcElement.parentElement;
        let digits = parent.querySelectorAll('.pin-digit');
        for (let i = 0; i < digits.length; i++) {
            if (digits[i].value === '') {
                if (digits[i] !== evt.srcElement) digits[i].focus();
                break;
            }
        }
    }
    isConfigOpen() { return window.getComputedStyle(document.getElementById('divConfigPnl')).display !== 'none'; }
    setConfigPanel() {
        if (this.isConfigOpen()) return;
        let divCfg = document.getElementById('divConfigPnl');
        let divHome = document.getElementById('divHomePnl');
        divHome.style.display = 'none';
        divCfg.style.display = '';
        document.getElementById('icoConfig').className = 'icss-home';
        if (sockIsOpen) socket.send('join:0');
        let overlay = ui.waitMessage(document.getElementById('divSecurityOptions'));
        overlay.style.borderRadius = '5px';
        getJSON('/getSecurity', (err, security) => {
            overlay.remove();
            if (err) ui.serviceError(err);
            else {
                console.log(security);
                general.setSecurityConfig(security);
            }
        });
    }
    setHomePanel() {
        if (!this.isConfigOpen()) return;
        let divCfg = document.getElementById('divConfigPnl');
        let divHome = document.getElementById('divHomePnl');
        divHome.style.display = '';
        divCfg.style.display = 'none';
        document.getElementById('icoConfig').className = 'icss-gear';
        if (sockIsOpen) socket.send('leave:0');
        general.setSecurityConfig({ type: 0, username: '', password: '', pin: '', permissions: 0 });
    }
    toggleConfig() {
        if (this.isConfigOpen())
            this.setHomePanel();
        else {
            if (!security.authenticated && security.type !== 0) {
                document.getElementById('divContainer').addEventListener('afterlogin', (evt) => {
                    if (security.authenticated) this.setConfigPanel();
                }, { once: true });
                security.authUser();
            }
            else this.setConfigPanel();
        }
        somfy.showEditShade(false);
        somfy.showEditGroup(false);
    }
}
var ui = new UIBinder();

