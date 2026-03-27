class Security {
    type = 0;
    authenticated = false;
    apiKey = '';
    permissions = 0;
    async init() {
        let fld = document.getElementById('divUnauthenticated').querySelector('.pin-digit[data-bind="security.pin.d0"]');
        document.getElementById('divUnauthenticated').querySelector('.pin-digit[data-bind="login.pin.d3"]').addEventListener('digitentered', (evt) => {
            security.login();
        });
        await this.loadContext();
        if (this.type === 0 || (this.permissions & 0x01) === 0x01) { // No login required or only the config is protected.
            // initSockets() is called by init() after all sub-inits complete.
            //ui.setMode(mode);
            document.getElementById('divUnauthenticated').style.display = 'none';
            document.getElementById('divAuthenticated').style.display = '';
            document.getElementById('divContainer').setAttribute('data-auth', true);
        }
    }
    async loadContext() {
        let pnl = document.getElementById('divUnauthenticated');
        pnl.querySelector('#loginButtons').style.display = 'none';
        pnl.querySelector('#divLoginPassword').style.display = 'none';
        pnl.querySelector('#divLoginPin').style.display = 'none';
        await new Promise((resolve, reject) => {
            getJSONSync('/loginContext', (err, ctx) => {
                pnl.querySelector('#loginButtons').style.display = '';
                resolve();
                if (err) ui.serviceError(err);
                else {
                    console.log(ctx);
                    document.getElementById('divContainer').setAttribute('data-securitytype', ctx.type);
                    this.type = ctx.type;
                    this.permissions = ctx.permissions;
                    switch (ctx.type) {
                        case 1:
                            pnl.querySelector('#divLoginPin').style.display = '';
                            pnl.querySelector('#divLoginPassword').style.display = 'none';
                            pnl.querySelector('.pin-digit[data-bind="login.pin.d0"]').focus();
                            break;
                        case 2:
                            pnl.querySelector('#divLoginPassword').style.display = '';
                            pnl.querySelector('#divLoginPin').style.display = 'none';
                            pnl.querySelector('#fldLoginUsername').focus();
                            break;
                    }
                    pnl.querySelector('#fldLoginType').value = ctx.type;
                }
            });
        });
    }
    authUser() {
        document.getElementById('divAuthenticated').style.display = 'none';
        document.getElementById('divUnauthenticated').style.display = '';
        this.loadContext();
        document.getElementById('btnCancelLogin').style.display = 'inline-block';
    }
    cancelLogin() {
        let evt = new CustomEvent('afterlogin', { detail: { authenticated: this.authenticated } });
        document.getElementById('divAuthenticated').style.display = '';
        document.getElementById('divUnauthenticated').style.display = 'none';
        document.getElementById('divContainer').dispatchEvent(evt);
    }
    login() {
        console.log('Logging in...');
        let pnl = document.getElementById('divUnauthenticated');
        let msg = pnl.querySelector('#spanLoginMessage');
        msg.innerHTML = '';
        let sec = ui.fromElement(pnl).login;
        console.log(sec);
        let pin = '';
        switch (sec.type) {
            case 1:
                for (let i = 0; i < 4; i++) {
                    pin += sec.pin[`d${i}`];
                }
                if (pin.length !== 4) return;
                break;
            case 2:
                break;
        }
        sec.pin = pin;
        putJSONSync('/login', sec, (err, log) => {
            if (err) ui.serviceError(err);
            else {
                console.log(log);
                if (log.success) {
                    if (typeof socket === 'undefined' || !socket) (async () => { await initSockets(); })();
                    //ui.setMode(mode);

                    document.getElementById('divUnauthenticated').style.display = 'none';
                    document.getElementById('divAuthenticated').style.display = '';
                    document.getElementById('divContainer').setAttribute('data-auth', true);
                    this.apiKey = log.apiKey;
                    this.authenticated = true;
                    let evt = new CustomEvent('afterlogin', { detail: { authenticated: true } });
                    document.getElementById('divContainer').dispatchEvent(evt);
                }
                else
                    msg.innerHTML = log.msg;
            }
        });
    }
}
var security = new Security();

class General {
    initialized = false; 
    appVersion = 'v2.4.7-Homekit';
    reloadApp = false;
    init() {
        if (this.initialized) return;
        this.setAppVersion();
        this.setTimeZones();
        if (sockIsOpen && ui.isConfigOpen()) socket.send('join:0');
        ui.toElement(document.getElementById('divSystemSettings'), { general: { hostname: 'ESPSomfyRTS', username: '', password: '', posixZone: 'UTC0', ntpServer: 'pool.ntp.org' } });
        this.initialized = true;
    }
    getCookie(cname) {
        let n = cname + '=';
        let cookies = document.cookie.split(';');
        console.log(cookies);
        for (let i = 0; i < cookies.length; i++) {
            let c = cookies[i];
            while (c.charAt(0) === ' ') c = c.substring(0);
            if (c.indexOf(n) === 0) return c.substring(n.length, c.length);
        }
        return '';
    }
    reload() {
        let addMetaTag = (name, content) => {
            let meta = document.createElement('meta');
            meta.httpEquiv = name;
            meta.content = content;
            document.getElementsByTagName('head')[0].appendChild(meta);
        };
        addMetaTag('pragma', 'no-cache');
        addMetaTag('expires', '0');
        addMetaTag('cache-control', 'no-cache');
        document.location.reload();
    }
    timeZones = [
    { city: 'Africa/Cairo', code: 'EET-2' },
    { city: 'Africa/Johannesburg', code: 'SAST-2' },
    { city: 'Africa/Juba', code: 'CAT-2' },
    { city: 'Africa/Lagos', code: 'WAT-1' },
    { city: 'Africa/Mogadishu', code: 'EAT-3' },
    { city: 'Africa/Tunis', code: 'CET-1' },
    { city: 'America/Adak', code: 'HST10HDT,M3.2.0,M11.1.0' },
    { city: 'America/Anchorage', code: 'AKST9AKDT,M3.2.0,M11.1.0' },
    { city: 'America/Asuncion', code: '<-04>4<-03>,M10.1.0/0,M3.4.0/0' },
    { city: 'America/Bahia_Banderas', code: 'CST6CDT,M4.1.0,M10.5.0' },
    { city: 'America/Barbados', code: 'AST4' },
    { city: 'America/Bermuda', code: 'AST4ADT,M3.2.0,M11.1.0' },
    { city: 'America/Cancun', code: 'EST5' },
    { city: 'America/Central_Time', code: 'CST6CDT,M3.2.0,M11.1.0' },
    { city: 'America/Chihuahua', code: 'MST7MDT,M4.1.0,M10.5.0' },
    { city: 'America/Eastern_Time', code: 'EST5EDT,M3.2.0,M11.1.0' },
    { city: 'America/Godthab', code: '<-03>3<-02>,M3.5.0/-2,M10.5.0/-1' },
    { city: 'America/Havana', code: 'CST5CDT,M3.2.0/0,M11.1.0/1' },
    { city: 'America/Mexico_City', code: 'CST6' },
    { city: 'America/Miquelon', code: '<-03>3<-02>,M3.2.0,M11.1.0' },
    { city: 'America/Mountain_Time', code: 'MST7MDT,M3.2.0,M11.1.0' },
    { city: 'America/Pacific_Time', code: 'PST8PDT,M3.2.0,M11.1.0' },
    { city: 'America/Phoenix', code: 'MST7' },
    { city: 'America/Santiago', code: '<-04>4<-03>,M9.1.6/24,M4.1.6/24' },
    { city: 'America/St_Johns', code: 'NST3:30NDT,M3.2.0,M11.1.0' },
    { city: 'Antarctica/Troll', code: '<+00>0<+02>-2,M3.5.0/1,M10.5.0/3' },
    { city: 'Asia/Amman', code: 'EET-2EEST,M2.5.4/24,M10.5.5/1' },
    { city: 'Asia/Beirut', code: 'EET-2EEST,M3.5.0/0,M10.5.0/0' },
    { city: 'Asia/Colombo', code: '<+0530>-5:30' },
    { city: 'Asia/Damascus', code: 'EET-2EEST,M3.5.5/0,M10.5.5/0' },
    { city: 'Asia/Gaza', code: 'EET-2EEST,M3.4.4/50,M10.4.4/50' },
    { city: 'Asia/Hong_Kong', code: 'HKT-8' },
    { city: 'Asia/Jakarta', code: 'WIB-7' },
    { city: 'Asia/Jayapura', code: 'WIT-9' },
    { city: 'Asia/Jerusalem', code: 'IST-2IDT,M3.4.4/26,M10.5.0' },
    { city: 'Asia/Kabul', code: '<+0430>-4:30' },
    { city: 'Asia/Karachi', code: 'PKT-5' },
    { city: 'Asia/Kathmandu', code: '<+0545>-5:45' },
    { city: 'Asia/Kolkata', code: 'IST-5:30' },
    { city: 'Asia/Makassar', code: 'WITA-8' },
    { city: 'Asia/Manila', code: 'PST-8' },
    { city: 'Asia/Seoul', code: 'KST-9' },
    { city: 'Asia/Shanghai', code: 'CST-8' },
    { city: 'Asia/Tehran', code: '<+0330>-3:30' },
    { city: 'Asia/Tokyo', code: 'JST-9' },
    { city: 'Atlantic/Azores', code: '<-01>1<+00>,M3.5.0/0,M10.5.0/1' },
    { city: 'Australia/Adelaide', code: 'ACST-9:30ACDT,M10.1.0,M4.1.0/3' },
    { city: 'Australia/Brisbane', code: 'AEST-10' },
    { city: 'Australia/Darwin', code: 'ACST-9:30' },
    { city: 'Australia/Eucla', code: '<+0845>-8:45' },
    { city: 'Australia/Lord_Howe', code: '<+1030>-10:30<+11>-11,M10.1.0,M4.1.0' },
    { city: 'Australia/Melbourne', code: 'AEST-10AEDT,M10.1.0,M4.1.0/3' },
    { city: 'Australia/Perth', code: 'AWST-8' },
    { city: 'Etc/GMT-1', code: '<+01>-1' },
    { city: 'Etc/GMT-2', code: '<+02>-2' },
    { city: 'Etc/GMT-3', code: '<+03>-3' },
    { city: 'Etc/GMT-4', code: '<+04>-4' },
    { city: 'Etc/GMT-5', code: '<+05>-5' },
    { city: 'Etc/GMT-6', code: '<+06>-6' },
    { city: 'Etc/GMT-7', code: '<+07>-7' },
    { city: 'Etc/GMT-8', code: '<+08>-8' },
    { city: 'Etc/GMT-9', code: '<+09>-9' },
    { city: 'Etc/GMT-10',code: '<+10>-10' },
    { city: 'Etc/GMT-11', code: '<+11>-11' },
    { city: 'Etc/GMT-12', code: '<+12>-12' },
    { city: 'Etc/GMT-13', code: '<+13>-13' },
    { city: 'Etc/GMT-14', code: '<+14>-14' },
    { city: 'Etc/GMT+0', code: 'GMT0' },
    { city: 'Etc/GMT+1', code: '<-01>1' },
    { city: 'Etc/GMT+2', code: '<-02>2' },
    { city: 'Etc/GMT+3', code: '<-03>3' },
    { city: 'Etc/GMT+4', code: '<-04>4' },
    { city: 'Etc/GMT+5', code: '<-05>5' },
    { city: 'Etc/GMT+6', code: '<-06>6' },
    { city: 'Etc/GMT+7', code: '<-07>7' },
    { city: 'Etc/GMT+8', code: '<-08>8' },
    { city: 'Etc/GMT+9', code: '<-09>9' },
    { city: 'Etc/GMT+10', code: '<-10>10' },
    { city: 'Etc/GMT+11', code: '<-11>11' },
    { city: 'Etc/GMT+12', code: '<-12>12' },
    { city: 'Etc/UTC', code: 'UTC0' },
    { city: 'Europe/Athens', code: 'EET-2EEST,M3.5.0/3,M10.5.0/4' },
    { city: "Europe/Berlin", code: "CEST-1CET,M3.2.0/2:00:00,M11.1.0/2:00:00" },
    { city: 'Europe/Brussels', code: 'CET-1CEST,M3.5.0,M10.5.0/3' },
    { city: 'Europe/Chisinau', code: 'EET-2EEST,M3.5.0,M10.5.0/3' },
    { city: 'Europe/Dublin', code: 'IST-1GMT0,M10.5.0,M3.5.0/1' },
    { city: 'Europe/Lisbon',  code: 'WET0WEST,M3.5.0/1,M10.5.0' },
    { city: 'Europe/London', code: 'GMT0BST,M3.5.0/1,M10.5.0' },
    { city: 'Europe/Moscow', code: 'MSK-3' },
    { city: 'Europe/Paris', code: 'CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00' },
    { city: 'Indian/Cocos',  code: '<+0630>-6:30' },
    { city: 'Pacific/Auckland', code: 'NZST-12NZDT,M9.5.0,M4.1.0/3' },
    { city: 'Pacific/Chatham', code: '<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45' },
    { city: 'Pacific/Easter', code: '<-06>6<-05>,M9.1.6/22,M4.1.6/22' },
    { city: 'Pacific/Fiji', code: '<+12>-12<+13>,M11.2.0,M1.2.3/99' },
    { city: 'Pacific/Guam',  code: 'ChST-10' },
    { city: 'Pacific/Honolulu', code: 'HST10' },
    { city: 'Pacific/Marquesas', code: '<-0930>9:30' },
    { city: 'Pacific/Midway',  code: 'SST11' },
    { city: 'Pacific/Norfolk', code: '<+11>-11<+12>,M10.1.0,M4.1.0/3' }
    ];
    async loadGeneral() {
        let pnl = document.getElementById('divSystemOptions');
        await getJSONSync('/modulesettings', (err, settings) => {
            if (err) {
                console.log(err);
            }
            else {
                console.log(settings);
                document.getElementById('spanFwVersion').innerText = settings.fwVersion;
                document.getElementById('spanHwVersion').innerText = settings.chipModel.length > 0 ? '-' + settings.chipModel : '';
                document.getElementById('divContainer').setAttribute('data-chipmodel', settings.chipModel);
                somfy.initPins();
                general.setAppVersion();
                ui.toElement(pnl, { general: settings });
            }
        });
    }
    loadLogin() {
        getJSONSync('/loginContext', (err, ctx) => {
            if (err) ui.serviceError(err);
            else {
                console.log(ctx);
                let pnl = document.getElementById('divContainer');
                pnl.setAttribute('data-securitytype', ctx.type);
                let fld;
                switch (ctx.type) {
                    case 1:
                        document.getElementById('divPinSecurity').style.display = '';
                        fld = document.getElementById('divPinSecurity').querySelector('.pin-digit[data-bind="security.pin.d0"]');
                        document.getElementById('divPinSecurity').querySelector('.pin-digit[data-bind="security.pin.d3"]').addEventListener('digitentered', (evt) => {
                            general.login();
                        });
                        break;
                    case 2:
                        document.getElementById('divPasswordSecurity').style.display = '';
                        fld = document.getElementById('fldUsername');
                        break;
                }
                if (fld) fld.focus();
            }
        });
    }
    setAppVersion() { document.getElementById('spanAppVersion').innerText = this.appVersion; }
    setTimeZones() {
        let dd = document.getElementById('selTimeZone');
        dd.length = 0;
        let maxLength = 0;
        for (let i = 0; i < this.timeZones.length; i++) {
            let opt = document.createElement('option');
            opt.text = this.timeZones[i].city;
            opt.value = this.timeZones[i].code;
            maxLength = Math.max(maxLength, this.timeZones[i].code.length);
            dd.add(opt);
        }
        dd.value = 'UTC0';
        console.log(`Max TZ:${maxLength}`);
    }
    setGeneral() {
        let valid = true;
        let pnl = document.getElementById('divSystemSettings');
        let obj = ui.fromElement(pnl).general;
        if (typeof obj.hostname === 'undefined' || !obj.hostname || obj.hostname === '') {
            ui.errorMessage('Invalid Host Name').querySelector('.sub-message').innerHTML = 'You must supply a valid Host Name.';
            valid = false;
        }
        if (valid && !/^[a-zA-Z0-9-]+$/.test(obj.hostname)) {
            ui.errorMessage('Invalid Host Name').querySelector('.sub-message').innerHTML = 'The host name must only include numbers, letters, or dash.';
            valid = false;
        }
        if (valid && obj.hostname.length > 32) {
            ui.errorMessage('Invalid Host Name').querySelector('.sub-message').innerHTML = 'The maximum Host Name length is 32 characters.';
            valid = false;
        }
        if (valid && typeof obj.ntpServer === 'string' && obj.ntpServer.length > 64) {
            ui.errorMessage('Invalid NTP Server').querySelector('.sub-message').innerHTML = 'The maximum NTP Server length is 64 characters.';
            valid = false;
        }
        if (valid) {
            putJSONSync('/setgeneral', obj, (err, response) => {
                if (err) ui.serviceError(err);
                console.log(response);
            });
        }
    }
    setSecurityConfig(security) {
        // We need to transform the security object so that it can be set to the configuration.
        let obj = {
            security: {
                type: security.type, username: security.username, password: security.password,
                permissions: { configOnly: makeBool(security.permissions & 0x01) },
                pin: {
                    d0: security.pin[0],
                    d1: security.pin[1],
                    d2: security.pin[2],
                    d3: security.pin[3]
                }
            }
        };
        ui.toElement(document.getElementById('divSecurityOptions'), obj);
        this.onSecurityTypeChanged();
    }
    rebootDevice() {
        ui.promptMessage(document.getElementById('divContainer'), 'Are you sure you want to reboot the device?', () => {
            if(typeof socket !== 'undefined') socket.close(3000, 'reboot');
            putJSONSync('/reboot', {}, (err, response) => {
                document.getElementById('btnSaveGeneral').classList.remove('disabled');
                console.log(response);
            });
            ui.clearErrors();
        });
    }
    onSecurityTypeChanged() {
        let pnl = document.getElementById('divSecurityOptions');
        let sec = ui.fromElement(pnl).security;
        switch (sec.type) {
            case 0:
                pnl.querySelector('#divPermissions').style.display = 'none';
                pnl.querySelector('#divPinSecurity').style.display = 'none';
                pnl.querySelector('#divPasswordSecurity').style.display = 'none';
                break;
            case 1:
                pnl.querySelector('#divPermissions').style.display = '';
                pnl.querySelector('#divPinSecurity').style.display = '';
                pnl.querySelector('#divPasswordSecurity').style.display = 'none';
                break;
            case 2:
                pnl.querySelector('#divPermissions').style.display = '';
                pnl.querySelector('#divPinSecurity').style.display = 'none';
                pnl.querySelector('#divPasswordSecurity').style.display = '';
                break;

        }
    }
    saveSecurity() {
        let security = ui.fromElement(document.getElementById('divSecurityOptions')).security;
        console.log(security);
        let sec = { type: security.type, username: security.username, password: security.password, pin: '', perm: 0 };
        // Pin entry.
        for (let i = 0; i < 4; i++) {
            sec.pin += security.pin[`d${i}`];
        }
        sec.permissions |= security.permissions.configOnly ? 0x01 : 0x00;
        let confirm = '';
        console.log(sec);
        if (security.type === 1) { // Pin Entry
            // Make sure our pin is 4 digits.
            if (sec.pin.length !== 4) {
                ui.errorMessage('Invalid Pin').querySelector('.sub-message').innerHTML = 'Pins must be exactly 4 alpha-numeric values in length.  Please enter a complete pin.';
                return;
            }
            confirm = '<p>Please keep your PIN safe and above all remember it.  The only way to recover a lost PIN is to completely reload the onboarding firmware which will wipe out your configuration.</p><p>Have you stored your PIN in a safe place?</p>';
        }
        else if (security.type === 2) { // Password
            if (sec.username.length === 0) {
                ui.errorMessage('No Username Provided').querySelector('.sub-message').innerHTML = 'You must provide a username for password security.';
                return;
            }
            if (sec.username.length > 32) {
                ui.errorMessage('Invalid Username').querySelector('.sub-message').innerHTML = 'The maximum username length is 32 characters.';
                return;
            }

            if (sec.password.length === 0) {
                ui.errorMessage('No Password Provided').querySelector('.sub-message').innerHTML = 'You must provide a password for password security.';
                return;
            }
            if (sec.password.length > 32) {
                ui.errorMessage('Invalid Password').querySelector('.sub-message').innerHTML = 'The maximum password length is 32 characters.';
                return;
            }

            if (security.repeatpassword.length === 0) {
                ui.errorMessage('Re-enter Password').querySelector('.sub-message').innerHTML = 'You must re-enter the password in the Re-enter Password field.';
                return;
            }
            if (sec.password !== security.repeatpassword) {
                ui.errorMessage('Passwords do not Match').querySelector('.sub-message').innerHTML = 'Please re-enter the password exactly as you typed it in the Re-enter Password field.';
                return;
            }
            confirm = '<p>Please keep your password safe and above all remember it.  The only way to recover a password is to completely reload the onboarding firmware which will wipe out your configuration.</p><p>Have you stored your username and password in a safe place?</p>';
        }
        let prompt = ui.promptMessage('Confirm Security', () => {
            putJSONSync('/saveSecurity', sec, (err, objApiKey) => {
                prompt.remove();
                if (err) ui.serviceError(err);
                else {
                    console.log(objApiKey);
                }
            });
        });
        prompt.querySelector('.sub-message').innerHTML = confirm;

    }
}
var general = new General();
class Wifi {
    initialized = false;
    ethBoardTypes = [{ val: 0, label: 'Custom Config' },
    { val: 7, label: 'EST-PoE-32 - Everything Smart', clk: 3, ct: 0, addr: 0, pwr: 12, mdc: 23, mdio: 18 },
    { val: 3, label: 'ESP32-EVB - Olimex', clk: 0, ct: 0, addr: 0, pwr: -1, mdc: 23, mdio: 18 },
    { val: 2, label: 'ESP32-POE - Olimex', clk: 3, ct: 0, addr: 0, pwr: 12, mdc: 23, mdio: 18 },
    { val: 4, label: 'T-Internet POE - LILYGO', clk: 3, ct: 0, addr: 0, pwr: 16, mdc: 23, mdio: 18 },
    { val: 5, label: 'wESP32 v7+ - Silicognition', clk: 0, ct: 2, addr: 0, pwr: -1, mdc: 16, mdio: 17 },
    { val: 6, label: 'wESP32 < v7 - Silicognition', clk: 0, ct: 0, addr: 0, pwr: -1, mdc: 16, mdio: 17 },
    { val: 1, label: 'WT32-ETH01 - Wireless Tag', clk: 0, ct: 0, addr: 1, pwr: 16, mdc: 23, mdio: 18 }
    ];
    ethClockModes = [{ val: 0, label: 'GPIO0 IN' }, { val: 1, label: 'GPIO0 OUT' }, { val: 2, label: 'GPIO16 OUT' }, { val: 3, label: 'GPIO17 OUT' }];
    ethPhyTypes = [{ val: 0, label: 'LAN8720' }, { val: 1, label: 'TLK110' }, { val: 2, label: 'RTL8201' }, { val: 3, label: 'DP83848' }, { val: 4, label: 'DM9051' }, { val: 5, label: 'KZ8081' }];
    init() {
        document.getElementById("divNetworkStrength").innerHTML = this.displaySignal(-100);
        if (this.initialized) return;
        let addr = [];
        this.loadETHDropdown(document.getElementById('selETHClkMode'), this.ethClockModes);
        this.loadETHDropdown(document.getElementById('selETHPhyType'), this.ethPhyTypes);
        this.loadETHDropdown(document.getElementById('selETHBoardType'), this.ethBoardTypes);
        for (let i = 0; i < 32; i++) addr.push({ val: i, label: `PHY ${i}` });
        this.loadETHDropdown(document.getElementById('selETHAddress'), addr);
        this.loadETHPins(document.getElementById('selETHPWRPin'), 'power');
        this.loadETHPins(document.getElementById('selETHMDCPin'), 'mdc', 23);
        this.loadETHPins(document.getElementById('selETHMDIOPin'), 'mdio', 18);
        ui.toElement(document.getElementById('divNetAdapter'), {
            wifi: {ssid:'', passphrase:''},
            ethernet: { boardType: 1, wirelessFallback: false, dhcp: true, dns1: '', dns2: '', ip: '', gateway: '' }
        });
        this.onETHBoardTypeChanged(document.getElementById('selETHBoardType'));
        this.initialized = true;
    }
    loadETHPins(sel, type, selected) {
        let arr = [];
        switch (type) {
            case 'power':
                arr.push({ val: -1, label: 'None' });
                break;
        }
        for (let i = 0; i < 36; i++) {
            arr.push({ val: i, label: `GPIO ${i}` });
        }
        this.loadETHDropdown(sel, arr, selected);
    }
    loadETHDropdown(sel, arr, selected) {
        while (sel.firstChild) sel.removeChild(sel.firstChild);
        for (let i = 0; i < arr.length; i++) {
            let elem = arr[i];
            sel.options[sel.options.length] = new Option(elem.label, elem.val, elem.val === selected, elem.val === selected);
        }
    }
    onETHBoardTypeChanged(sel) {
        let type = this.ethBoardTypes.find(elem => parseInt(sel.value, 10) === elem.val);
        if (typeof type !== 'undefined') {
            // Change the values to represent what the board type says.
            if(typeof type.ct !== 'undefined') document.getElementById('selETHPhyType').value = type.ct;
            if (typeof type.clk !== 'undefined') document.getElementById('selETHClkMode').value = type.clk;
            if (typeof type.addr !== 'undefined') document.getElementById('selETHAddress').value = type.addr;
            if (typeof type.pwr !== 'undefined') document.getElementById('selETHPWRPin').value = type.pwr;
            if (typeof type.mdc !== 'undefined') document.getElementById('selETHMDCPin').value = type.mdc;
            if (typeof type.mdio !== 'undefined') document.getElementById('selETHMDIOPin').value = type.mdio;
            document.getElementById('divETHSettings').style.display = type.val === 0 ? '' : 'none';
        }
    }
    onDHCPClicked(cb) { document.getElementById('divStaticIP').style.display = cb.checked ? 'none' : ''; }
    async loadNetwork() {
        let pnl = document.getElementById('divNetAdapter');
        await getJSONSync('/networksettings', (err, settings) => {
            console.log(settings);
            if (err) {
                ui.serviceError(err);
            }
            else {
                document.getElementById('cbHardwired').checked = settings.connType >= 2;
                document.getElementById('cbFallbackWireless').checked = settings.connType === 3;
                ui.toElement(pnl, settings);
                /*
                if (settings.connType >= 2) {
                    document.getElementById('divWiFiMode').style.display = 'none';
                    document.getElementById('divEthernetMode').style.display = '';
                    document.getElementById('divRoaming').style.display = 'none';
                    document.getElementById('divFallbackWireless').style.display = 'inline-block';
                }
                else {
                    document.getElementById('divWiFiMode').style.display = '';
                    document.getElementById('divEthernetMode').style.display = 'none';
                    document.getElementById('divFallbackWireless').style.display = 'none';
                    document.getElementById('divRoaming').style.display = 'inline-block';
                }
                */
                ui.toElement(document.getElementById('divDHCP'), settings);
                document.getElementById('divETHSettings').style.display = settings.ethernet.boardType === 0 ? '' : 'none';
                document.getElementById('divStaticIP').style.display = settings.ip.dhcp ? 'none' : '';
                document.getElementById('spanCurrentIP').innerHTML = settings.ip.ip;
                this.useEthernetClicked();
                this.hiddenSSIDClicked();
            }
        });

    }
    useEthernetClicked() {
        let useEthernet = document.getElementById('cbHardwired').checked;
        document.getElementById('divWiFiMode').style.display = useEthernet ? 'none' : '';
        document.getElementById('divEthernetMode').style.display = useEthernet ? '' : 'none';
        document.getElementById('divFallbackWireless').style.display = useEthernet ? 'inline-block' : 'none';
        document.getElementById('divRoaming').style.display = useEthernet ? 'none' : 'inline-block';
        document.getElementById('divHiddenSSID').style.display = useEthernet ? 'none' : 'inline-block';
    }
    hiddenSSIDClicked() {
        let hidden = document.getElementById('cbHiddenSSID').checked;
        if (hidden) document.getElementById('cbRoaming').checked = false;
        document.getElementById('cbRoaming').disabled = hidden;
    }
    async loadAPs() {
        if (document.getElementById('btnScanAPs').classList.contains('disabled')) return;
        document.getElementById('divAps').innerHTML = '<div style="display:flex;justify-content:center;align-items:center;"><div class="lds-roller"><div></div><div></div><div></div><div></div><div></div><div></div><div></div><div></div></div></div>';
        document.getElementById('btnScanAPs').classList.add('disabled');
        //document.getElementById('btnConnectWiFi').classList.add('disabled');
        getJSON('/scanaps', (err, aps) => {
            document.getElementById('btnScanAPs').classList.remove('disabled');
            //document.getElementById('btnConnectWiFi').classList.remove('disabled');
            console.log(aps);
            if (err) {
                this.displayAPs({ connected: { name: '', passphrase: '' }, accessPoints: [] });
            }
            else {
                this.displayAPs(aps);
            }
        });
    }
    displayAPs(aps) {
        let div = '';
        let nets = [];
        for (let i = 0; i < aps.accessPoints.length; i++) {
            let ap = aps.accessPoints[i];
            let p = nets.find(elem => elem.name === ap.name);
            if (typeof p !== 'undefined' && p) {
                p.channel = p.strength > ap.strength ? p.channel : ap.channel;
                p.macAddress = p.strength > ap.strength ? p.macAddress : ap.macAddress;
                p.strength = Math.max(p.strength, ap.strength);
            }
            else
                nets.push(ap);
        }
        // Sort by the best signal strength.
        nets.sort((a, b) => b.strength - a.strength);
        for (let i = 0; i < nets.length; i++) {
            let ap = nets[i];
            div += `<div class="wifiSignal" onclick="wifi.selectSSID(this);" data-channel="${ap.channel}" data-encryption="${ap.encryption}" data-strength="${ap.strength}" data-mac="${ap.macAddress}"><span class="ssid">${ap.name}</span><span class="strength">${this.displaySignal(ap.strength)}</span></div>`;
        }
        let divAps = document.getElementById('divAps');
        divAps.setAttribute('data-lastloaded', new Date().getTime());
        divAps.innerHTML = div;
        //document.getElementsByName('ssid')[0].value = aps.connected.name;
        //document.getElementsByName('passphrase')[0].value = aps.connected.passphrase;
        //this.procWifiStrength(aps.connected);
    }
    selectSSID(el) {
        let obj = {
            name: el.querySelector('span.ssid').innerHTML,
            encryption: el.getAttribute('data-encryption'),
            strength: parseInt(el.getAttribute('data-strength'), 10),
            channel: parseInt(el.getAttribute('data-channel'), 10)
        };
        console.log(obj);
        document.getElementsByName('ssid')[0].value = obj.name;
    }
    calcWaveStrength(sig) {
        let wave = 0;
        if (sig > -90) wave++;
        if (sig > -80) wave++;
        if (sig > -70) wave++;
        if (sig > -67) wave++;
        if (sig > -30) wave++;
        return wave;
    }
    displaySignal(sig) {
        return `<div class="signal waveStrength-${this.calcWaveStrength(sig)}"><div class="wv4 wave"><div class="wv3 wave"><div class="wv2 wave"><div class="wv1 wave"><div class="wv0 wave"></div></div></div></div></div></div>`;
    }
    saveIPSettings() {
        let pnl = document.getElementById('divDHCP');
        let obj = ui.fromElement(pnl).ip;
        console.log(obj);
        if (!obj.dhcp) {
            let fnValidateIP = (addr) => { return /^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/.test(addr); };
            if (typeof obj.ip !== 'string' || obj.ip.length === 0 || obj.ip === '0.0.0.0') {
                ui.errorMessage('You must supply a valid IP address for the Static IP Address');
                return;
            }
            else if (!fnValidateIP(obj.ip)) {
                ui.errorMessage('Invalid Static IP Address.  IP addresses are in the form XXX.XXX.XXX.XXX');
                return;
            }
            if (typeof obj.subnet !== 'string' || obj.subnet.length === 0 || obj.subnet === '0.0.0.0') {
                ui.errorMessage('You must supply a valid IP address for the Subnet Mask');
                return;
            }
            else if (!fnValidateIP(obj.subnet)) {
                ui.errorMessage('Invalid Subnet IP Address.  IP addresses are in the form XXX.XXX.XXX.XXX');
                return;
            }
            if (typeof obj.gateway !== 'string' || obj.gateway.length === 0 || obj.gateway === '0.0.0.0') {
                ui.errorMessage('You must supply a valid Gateway IP address');
                return;
            }
            else if (!fnValidateIP(obj.gateway)) {
                ui.errorMessage('Invalid Gateway IP Address.  IP addresses are in the form XXX.XXX.XXX.XXX');
                return;
            }
            if (obj.dns1.length !== 0 && !fnValidateIP(obj.dns1)) {
                ui.errorMessage('Invalid Domain Name Server 1 IP Address.  IP addresses are in the form XXX.XXX.XXX.XXX');
                return;
            }
            if (obj.dns2.length !== 0 && !fnValidateIP(obj.dns2)) {
                ui.errorMessage('Invalid Domain Name Server 2 IP Address.  IP addresses are in the form XXX.XXX.XXX.XXX');
                return;
            }
        }
        putJSONSync('/setIP', obj, (err, response) => {
            if (err) {
                ui.serviceError(err);
            }
            console.log(response);
        });
    }
    saveNetwork() {
        let pnl = document.getElementById('divNetAdapter');
        let obj = ui.fromElement(pnl);
        obj.connType = obj.ethernet.hardwired ? (obj.ethernet.wirelessFallback ? 3 : 2) : 1;
        console.log(obj);
        if (obj.connType >= 2) {
            let boardType = this.ethBoardTypes.find(elem => obj.ethernet.boardType === elem.val);
            let phyType = this.ethPhyTypes.find(elem => obj.ethernet.phyType === elem.val);
            let clkMode = this.ethClockModes.find(elem => obj.ethernet.CLKMode === elem.val);
            let div = document.createElement('div');
            let html = `<div id="divLanSettings" class="inst-overlay">`;
            html += '<div style="width:100%;color:red;text-align:center;font-weight:bold;"><span style="padding:10px;display:inline-block;width:100%;border-radius:5px;border-top-right-radius:17px;border-top-left-radius:17px;background:white;">BEWARE ... WARNING ... DANGER<span></div>';
            html += '<p style="font-size:14px;">Incorrect Ethernet settings can damage your ESP32.  Please verify the settings below and ensure they match the manufacturer spec sheet.</p>';
            html += '<p style="font-size:14px;margin-bottom:0px;">If you are unsure do not press the Red button and press the Green button.  If any of the settings are incorrect please use the Custom Board type and set them to the correct values.';
            html += '<hr/><div>';
            html += `<div class="eth-setting-line"><label>Board Type</label><span>${boardType.label} [${boardType.val}]</span></div>`;
            html += `<div class="eth-setting-line"><label>PHY Chip Type</label><span>${phyType.label} [${phyType.val}]</span></div>`;
            html += `<div class="eth-setting-line"><label>PHY Address</label><span>${obj.ethernet.phyAddress}</span ></div >`;
            html += `<div class="eth-setting-line"><label>Clock Mode</label><span>${clkMode.label} [${clkMode.val}]</span></div >`;
            html += `<div class="eth-setting-line"><label>Power Pin</label><span>${obj.ethernet.PWRPin === -1 ? 'None' : obj.ethernet.PWRPin}</span></div>`;
            html += `<div class="eth-setting-line"><label>MDC Pin</label><span>${obj.ethernet.MDCPin}</span></div>`;
            html += `<div class="eth-setting-line"><label>MDIO Pin</label><span>${obj.ethernet.MDIOPin}</span></div>`;
            html += '</div>';
            html += `<div class="button-container">`;
            html += `<button id="btnSaveEthernet" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;background:orangered;">Save Ethernet Settings</button>`;
            html += `<button id="btnCancel" type="button" style="padding-left:20px;padding-right:20px;display:inline-block;background:lawngreen;color:gray" onclick="document.getElementById('divLanSettings').remove();">Cancel</button>`;
            html += `</div><form>`;
            div.innerHTML = html;
            document.getElementById('divContainer').appendChild(div);
            div.querySelector('#btnSaveEthernet').addEventListener('click', (el, event) => {
                console.log(obj);
                this.sendNetworkSettings(obj);
                setTimeout(() => { div.remove(); }, 1);
            });
        }
        else {
            this.sendNetworkSettings(obj);
        }
    }
    sendNetworkSettings(obj) {
        putJSONSync('/setNetwork', obj, (err, response) => {
            if (err) {
                ui.serviceError(err);
            }
            console.log(response);
        });
    }
    connectWiFi() {
        if (document.getElementById('btnConnectWiFi').classList.contains('disabled')) return;
        document.getElementById('btnConnectWiFi').classList.add('disabled');
        let obj = {
            ssid: document.getElementsByName('ssid')[0].value,
            passphrase: document.getElementsByName('passphrase')[0].value
        };
        if (obj.ssid.length > 64) {
            ui.errorMessage('Invalid SSID').querySelector('.sub-message').innerHTML = 'The maximum length of the SSID is 64 characters.';
            return;
        }
        if (obj.passphrase.length > 64) {
            ui.errorMessage('Invalid Passphrase').querySelector('.sub-message').innerHTML = 'The maximum length of the passphrase is 64 characters.';
            return;
        }


        let overlay = ui.waitMessage(document.getElementById('divNetAdapter'));
        putJSON('/connectwifi', obj, (err, response) => {
            overlay.remove();
            document.getElementById('btnConnectWiFi').classList.remove('disabled');
            console.log(response);

        });
    }
    procWifiStrength(strength) {
        //console.log(strength);
        let ssid = strength.ssid || strength.name;
        document.getElementById('spanNetworkSSID').innerHTML = !ssid || ssid === '' ? '-------------' : ssid;
        document.getElementById('spanNetworkChannel').innerHTML = isNaN(strength.channel) || strength.channel < 0 ? '--' : strength.channel;
        let cssClass = 'waveStrength-' + (isNaN(strength.strength) || strength > 0 ? -100 : this.calcWaveStrength(strength.strength));
        let elWave = document.getElementById('divNetworkStrength').children[0];
        if (typeof elWave !== 'undefined' && !elWave.classList.contains(cssClass)) {
            elWave.classList.remove('waveStrength-0', 'waveStrength-1', 'waveStrength-2', 'waveStrength-3', 'waveStrength-4');
            elWave.classList.add(cssClass);
        }
        document.getElementById('spanNetworkStrength').innerHTML = isNaN(strength.strength) || strength.strength <= -100 ? '----' : strength.strength;
    }
    procEthernet(ethernet) {
        console.log(ethernet);
        document.getElementById('divEthernetStatus').style.display = ethernet.connected ? '' : 'none';
        document.getElementById('divWiFiStrength').style.display = ethernet.connected ? 'none' : '';
        document.getElementById('spanEthernetStatus').innerHTML = ethernet.connected ? 'Connected' : 'Disconnected';
        document.getElementById('spanEthernetSpeed').innerHTML = !ethernet.connected ? '--------' : `${ethernet.speed}Mbps ${ethernet.fullduplex ? 'Full-duplex' : 'Half-duplex'}`;
    }
}
var wifi = new Wifi();
